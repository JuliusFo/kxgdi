///////////////////////////////////////////////////////////////////////////////
//
// test.c - smoke test for kxgdi.dll (runs on Windows 7 and later).
//
// Checks:
//   1. kxgdi.dll loads and exports its own D3DKMTOpenAdapterFromLuid.
//   2. Forwarded exports resolve to the same address as in gdi32.dll.
//   3. For every display adapter, both lookup strategies and the exported
//      function find the adapter by its LUID, a second call is answered from
//      the cache, and a stale cache entry falls back to the full search.
//   4. An unknown LUID is rejected with STATUS_INVALID_PARAMETER, invalid
//      pointers with STATUS_INVALID_PARAMETER
//      too (like Windows 8+), instead of crashing.
//   5. On Windows 8+, every status code is compared with the native
//      gdi32!D3DKMTOpenAdapterFromLuid.
//   6. Repeated calls do not leak handles.
//
// Exit code = number of failed checks.
//
// Option /ci: on machines without a desktop display adapter (headless build
// servers), skip the adapter checks instead of failing.
//
///////////////////////////////////////////////////////////////////////////////

// Pull in the implementation so the static strategy functions and the cache
// can be tested directly. (The DLL itself is loaded separately below.)
#include "../src/kxgdi.c"

__declspec(dllimport) HANDLE	WINAPI GetStdHandle(DWORD StdHandle);
__declspec(dllimport) BOOL		WINAPI WriteFile(HANDLE File, const void *Buffer,
	DWORD BytesToWrite, DWORD *BytesWritten, PVOID Overlapped);
__declspec(dllimport) void		WINAPI ExitProcess(UINT ExitCode);
__declspec(dllimport) PWSTR		WINAPI GetCommandLineW(void);
__declspec(dllimport) BOOL		WINAPI GetProcessHandleCount(HANDLE Process, DWORD *HandleCount);

#define CURRENT_PROCESS ((HANDLE) (LONG_PTR_MINUS_ONE))

typedef NTSTATUS (WINAPI *PFN_OPENFROMLUID)(D3DKMT_OPENADAPTERFROMLUID *Args);

static HANDLE StdOut;
static UINT Failures;

static void Print(LOG_BUFFER *Log)
{
	char Ascii[LOG_BUFFER_CCH];
	DWORD Written;
	UINT Index;

	// All messages are ASCII. WriteFile works for consoles, pipes and files.
	LogAppend(Log, L"\r\n");

	for (Index = 0; Index < Log->Length; ++Index) {
		Ascii[Index] = (Log->Text[Index] < 0x80) ? (char) Log->Text[Index] : '?';
	}

	WriteFile(StdOut, Ascii, Log->Length, &Written, NULL);
	Log->Length = 0;
	Log->Text[0] = 0;
}

static void PrintText(PCWSTR Text)
{
	LOG_BUFFER Log;

	Log.Length = 0;
	LogAppend(&Log, Text);
	Print(&Log);
}

// TRUE if the command line contains Arg (case-insensitive, ASCII only).
static BOOL HasArgument(PCWSTR Arg)
{
	PCWSTR Cmd = GetCommandLineW();
	UINT i, j;

	for (i = 0; Cmd[i]; ++i) {
		for (j = 0; Arg[j]; ++j) {
			WCHAR a = Cmd[i + j], b = Arg[j];
			if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
			if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
			if (a != b) break;
		}
		if (!Arg[j]) return TRUE;
	}

	return FALSE;
}

static void Check(BOOL Condition, PCWSTR What)
{
	LOG_BUFFER Log;

	Log.Length = 0;
	LogAppend(&Log, Condition ? L"  [ OK ] " : L"  [FAIL] ");
	LogAppend(&Log, What);
	Print(&Log);

	if (!Condition) {
		++Failures;
	}
}

// Calls an implementation; an exception (instead of a status) is reported as
// 0xEEEEEEEE so the comparison with the native function still works.
static NTSTATUS CallSafely(PFN_OPENFROMLUID Function, D3DKMT_OPENADAPTERFROMLUID *Args)
{
	__try {
		return Function(Args);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return (NTSTATUS) 0xEEEEEEEE;
	}
}

// Compares kxgdi's status with the native Windows 8+ implementation.
static void CompareWithNative(PFN_OPENFROMLUID Native, D3DKMT_OPENADAPTERFROMLUID *NativeArgs, NTSTATUS Status)
{
	NTSTATUS NativeStatus;
	LOG_BUFFER Log;

	if (!Native) {
		return;
	}

	NativeStatus = CallSafely(Native, NativeArgs);
	if (NT_SUCCESS(NativeStatus)) CloseKmtAdapter(NativeArgs->hAdapter);

	Log.Length = 0;
	LogAppend(&Log, L"         native gdi32 status ");
	LogAppendHex(&Log, (DWORD) NativeStatus);
	LogAppend(&Log, L", kxgdi status ");
	LogAppendHex(&Log, (DWORD) Status);
	Print(&Log);

	Check(NativeStatus == Status, L"status matches the native Windows 8+ implementation");
}

static void TestLuid(LUID Luid, PFN_OPENFROMLUID Exported, PFN_OPENFROMLUID Native, BOOL ExpectFound)
{
	D3DKMT_HANDLE Adapter;
	D3DKMT_OPENADAPTERFROMLUID Args;
	NTSTATUS Status;
	BOOL Found;
	WCHAR Name[ADAPTER_NAME_CCH];
	LOG_BUFFER Log;

	Log.Length = 0;
	LogAppend(&Log, ExpectFound ? L"Adapter LUID " : L"Unknown LUID ");
	LogAppendLuid(&Log, Luid);
	Print(&Log);

	if (ExpectFound) {
		// Independent of how the LUIDs were collected (GDI display names).
		Adapter = 0;
		Name[0] = 0;
		Found = OpenAdapterByLuidFromDeviceInterfaces(Luid, &Adapter, Name, ADAPTER_NAME_CCH);
		Check(Found && Adapter != 0 && Name[0] != 0, L"strategy 1 (device interfaces) finds adapter and its path");
		if (Found) CloseKmtAdapter(Adapter);

		// Consistency check only: the LUIDs were collected with the same API.
		Adapter = 0;
		Name[0] = 0;
		Found = OpenAdapterByLuidFromGdiDisplayNames(Luid, &Adapter, Name, ADAPTER_NAME_CCH);
		Check(Found && Adapter != 0 && Name[0] != 0, L"strategy 2 (GDI display names) finds adapter and its name");
		if (Found) CloseKmtAdapter(Adapter);
	}

	Args.AdapterLuid = Luid;
	Args.hAdapter = 0;
	Status = Exported(&Args);

	if (ExpectFound) {
		Check(Status == STATUS_SUCCESS && Args.hAdapter != 0,
			L"kxgdi!D3DKMTOpenAdapterFromLuid returns STATUS_SUCCESS + handle");
		if (NT_SUCCESS(Status)) CloseKmtAdapter(Args.hAdapter);

		// Second call: answered from the cache.
		Args.hAdapter = 0;
		Status = Exported(&Args);
		Check(Status == STATUS_SUCCESS && Args.hAdapter != 0, L"second call (from the cache) succeeds");
		if (NT_SUCCESS(Status)) CloseKmtAdapter(Args.hAdapter);
	} else {
		Check(Status == STATUS_INVALID_PARAMETER && Args.hAdapter == 0,
			L"kxgdi!D3DKMTOpenAdapterFromLuid returns STATUS_INVALID_PARAMETER");
	}

	{
		D3DKMT_OPENADAPTERFROMLUID NativeArgs;
		NativeArgs.AdapterLuid = Luid;
		NativeArgs.hAdapter = 0;
		CompareWithNative(Native, &NativeArgs, Status);
	}

	//
	// Cache of the linked-in copy (the DLL's cache is not visible from here).
	//

	if (ExpectFound) {
		Args.AdapterLuid = Luid;
		Args.hAdapter = 0;
		Status = Ext_D3DKMTOpenAdapterFromLuid(&Args);
		if (NT_SUCCESS(Status)) CloseKmtAdapter(Args.hAdapter);
		Check(NT_SUCCESS(Status) && CacheLookup(Luid, Name) != ADAPTER_KIND_NONE,
			L"successful lookup is cached");

		// A stale entry (e.g. the adapter got a new device path) must fall back to the search.
		CacheStore(Luid, ADAPTER_KIND_DEVICE_PATH, L"\\\\?\\kxgdi#stale#entry");
		Args.hAdapter = 0;
		Status = Ext_D3DKMTOpenAdapterFromLuid(&Args);
		if (NT_SUCCESS(Status)) CloseKmtAdapter(Args.hAdapter);
		Check(NT_SUCCESS(Status) && Args.hAdapter != 0, L"stale cache entry falls back to the full search");

		Name[0] = 0;
		CacheLookup(Luid, Name);
		Check(Name[0] != 0 && Name[4] != L'k', L"stale cache entry is replaced");
	}
}

static void TestInvalidPointers(PFN_OPENFROMLUID Exported, PFN_OPENFROMLUID Native)
{
	NTSTATUS Status;

	PrintText(L"Invalid pointers");

	Status = CallSafely(Exported, NULL);
	Check(Status == STATUS_INVALID_PARAMETER, L"NULL argument returns STATUS_INVALID_PARAMETER");
	CompareWithNative(Native, NULL, Status);

	Status = CallSafely(Exported, (D3DKMT_OPENADAPTERFROMLUID *) (ULONG_PTR) 0x10);
	Check(Status == STATUS_INVALID_PARAMETER, L"invalid argument pointer returns STATUS_INVALID_PARAMETER");
	CompareWithNative(Native, (D3DKMT_OPENADAPTERFROMLUID *) (ULONG_PTR) 0x10, Status);
}

// Many calls (cache hits and full searches) must not increase the handle count.
static void TestHandleLeaks(PFN_OPENFROMLUID Exported, const LUID *Known, LUID Bogus)
{
	DWORD Before = 0, After = 0;
	UINT i;
	LOG_BUFFER Log;

	PrintText(L"Handle leaks (64 calls)");

	for (i = 0; i < 64; ++i) {
		D3DKMT_OPENADAPTERFROMLUID Args;

		// The first round loads modules and fills caches.
		if (i == 1) GetProcessHandleCount(CURRENT_PROCESS, &Before);

		Args.AdapterLuid = (Known && (i & 1)) ? *Known : Bogus;
		Args.hAdapter = 0;
		if (NT_SUCCESS(Exported(&Args))) CloseKmtAdapter(Args.hAdapter);
	}

	GetProcessHandleCount(CURRENT_PROCESS, &After);

	Log.Length = 0;
	LogAppend(&Log, L"         process handles before ");
	LogAppendHex(&Log, Before);
	LogAppend(&Log, L", after ");
	LogAppendHex(&Log, After);
	Print(&Log);

	// A real leak grows with the number of calls; allow a little noise.
	Check(After <= Before + 4, L"no handle leak over repeated calls");
}

// TRUE if Address lies inside the image of Module (headers parsed by hand).
static BOOL IsInModule(HMODULE Module, PVOID Address)
{
	BYTE *Base = (BYTE *) Module;
	LONG NtOffset = *(LONG *) (Base + 0x3C);			// IMAGE_DOS_HEADER.e_lfanew
	DWORD SizeOfImage = *(DWORD *) (Base + NtOffset + 0x50);	// OptionalHeader.SizeOfImage

	return (BYTE *) Address >= Base && (BYTE *) Address < Base + SizeOfImage;
}

void WINAPI TestMain(void)
{
	HMODULE KxGdi;
	HMODULE Gdi32;
	PFN_OPENFROMLUID Exported;
	PFN_OPENFROMLUID Native;
	LUID Seen[16];
	UINT SeenCount;
	DWORD Index;
	LUID Bogus;
	LOG_BUFFER Log;

	StdOut = GetStdHandle((DWORD) -11); // STD_OUTPUT_HANDLE
	Failures = 0;
	Log.Length = 0;

	PrintText(L"kxgdi smoke test");

	KxGdi = LoadLibraryW(L"kxgdi.dll");
	Check(KxGdi != NULL, L"kxgdi.dll loads");
	if (!KxGdi) ExitProcess(Failures);

	Gdi32 = LoadLibraryW(L"gdi32.dll");
	Exported = (PFN_OPENFROMLUID) GetProcAddress(KxGdi, "D3DKMTOpenAdapterFromLuid");
	Native = (PFN_OPENFROMLUID) GetProcAddress(Gdi32, "D3DKMTOpenAdapterFromLuid");

	Check(Exported != NULL, L"kxgdi exports D3DKMTOpenAdapterFromLuid");
	if (!Exported) ExitProcess(Failures);

	Check(IsInModule(KxGdi, (PVOID) Exported),
		L"exported D3DKMTOpenAdapterFromLuid is kxgdi's own implementation (not forwarded)");

	{
		static const char *Forwarded[] = {
			"CreateCompatibleDC", "BitBlt", "SelectObject", "D3DKMTOpenAdapterFromHdc",
			"D3DKMTCloseAdapter", "GetDeviceCaps", "CreateFontIndirectW"
		};
		UINT i;
		BOOL AllMatch = TRUE;

		for (i = 0; i < sizeof(Forwarded) / sizeof(Forwarded[0]); ++i) {
			PVOID A = GetProcAddress(KxGdi, Forwarded[i]);
			PVOID B = GetProcAddress(Gdi32, Forwarded[i]);
			if (A == NULL || A != B) AllMatch = FALSE;
		}

		Check(AllMatch, L"forwarded exports resolve to the gdi32.dll functions");
	}

	PrintText(Native ? L"Native D3DKMTOpenAdapterFromLuid present (Windows 8+): comparing results"
	                 : L"No native D3DKMTOpenAdapterFromLuid (Windows 7): testing emulation only");

	//
	// Collect LUIDs of all desktop display adapters via a Windows 7 API.
	//

	ResolveApis();
	SeenCount = 0;

	for (Index = 0; pEnumDisplayDevicesW && SeenCount < 16; ++Index) {
		DISPLAY_DEVICEW Dev;
		D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
		UINT c, s;
		BOOL Duplicate;

		memset(&Dev, 0, sizeof(Dev));
		Dev.cb = sizeof(Dev);
		if (!pEnumDisplayDevicesW(NULL, Index, &Dev, 0)) break;

		memset(&Open, 0, sizeof(Open));
		for (c = 0; c < 31 && Dev.DeviceName[c]; ++c) Open.DeviceName[c] = Dev.DeviceName[c];
		if (!NT_SUCCESS(D3DKMTOpenAdapterFromGdiDisplayName(&Open))) continue;
		CloseKmtAdapter(Open.hAdapter);

		Duplicate = FALSE;
		for (s = 0; s < SeenCount; ++s) {
			if (LuidEqual(Seen[s], Open.AdapterLuid)) Duplicate = TRUE;
		}

		if (!Duplicate) Seen[SeenCount++] = Open.AdapterLuid;
	}

	if (SeenCount == 0 && HasArgument(L"/ci")) {
		// Headless build machines may have no desktop display adapter.
		PrintText(L"  [SKIP] no display adapter found via GDI (/ci): adapter checks skipped");
	} else {
		Check(SeenCount > 0, L"at least one display adapter found via GDI");
	}

	for (Index = 0; Index < SeenCount; ++Index) {
		TestLuid(Seen[Index], Exported, Native, TRUE);
	}

	Bogus.LowPart = 0xDEADBEEF;
	Bogus.HighPart = 0x7FFF0000;
	TestLuid(Bogus, Exported, Native, FALSE);

	TestInvalidPointers(Exported, Native);
	TestHandleLeaks(Exported, SeenCount ? &Seen[0] : NULL, Bogus);

	Log.Length = 0;
	LogAppend(&Log, Failures ? L"RESULT: FAILED, failed checks: " : L"RESULT: all checks passed ");
	LogAppendHex(&Log, Failures);
	Print(&Log);

	ExitProcess(Failures);
}
