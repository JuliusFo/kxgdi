///////////////////////////////////////////////////////////////////////////////
//
// kxgdildr.c - standalone loader for kxgdi (no VxKex required).
//
// kxgdildr.dll is registered as an Application Verifier provider for selected
// programs (IFEO values GlobalFlag/VerifierDlls). Windows loads it very early,
// before the imports of the program are resolved. It then renames the
// "gdi32.dll" entries in the import tables of the program and of every
// non-system DLL it loads to "kxgdi.dll", so the program gets
// D3DKMTOpenAdapterFromLuid from kxgdi.dll on Windows 7.
//
// This runs before kernel32.dll is initialized, so the DLL only imports
// ntdll.dll. Log messages are collected in memory; kxgdi.dll and the test
// program print them later via the exported KxGdiLdrGetLog().
//
// Can be combined with VxKex (list KexDll.dll first in VerifierDlls): in that
// case VxKex already reduces the Application Verifier, and kxgdildr only
// rewrites the gdi32 imports.
//
///////////////////////////////////////////////////////////////////////////////

#include "kxtypes.h"

//
// No C runtime: the compiler may still emit calls to memset/memcpy.
//

#pragma function(memset)
void *memset(void *Dest, int Value, SIZE_T Count)
{
	BYTE *D = (BYTE *) Dest;
	while (Count--) *D++ = (BYTE) Value;
	return Dest;
}

#pragma function(memcpy)
void *memcpy(void *Dest, const void *Src, SIZE_T Count)
{
	BYTE *D = (BYTE *) Dest;
	const BYTE *S = (const BYTE *) Src;
	while (Count--) *D++ = *S++;
	return Dest;
}

///////////////////////////////////////////////////////////////////////////////
//
// In-memory log
//
///////////////////////////////////////////////////////////////////////////////

#define KXLDR_LOG_CCH 4096

static WCHAR LdrLog[KXLDR_LOG_CCH];
static UINT LdrLogLength;

static void LogStr(PCWSTR Text)
{
	while (*Text && LdrLogLength < KXLDR_LOG_CCH - 1) {
		LdrLog[LdrLogLength++] = *Text++;
	}
	LdrLog[LdrLogLength] = 0;
}

static void LogUnicode(const UNICODE_STRING *Text)
{
	UINT i;
	if (!Text || !Text->Buffer) return;
	for (i = 0; i < Text->Length / sizeof(WCHAR) && LdrLogLength < KXLDR_LOG_CCH - 1; ++i) {
		LdrLog[LdrLogLength++] = Text->Buffer[i];
	}
	LdrLog[LdrLogLength] = 0;
}

static void LogDec(ULONG Value)
{
	WCHAR Digits[12];
	int n = 0;
	do { Digits[n++] = (WCHAR) (L'0' + Value % 10); Value /= 10; } while (Value && n < 11);
	while (n > 0 && LdrLogLength < KXLDR_LOG_CCH - 1) LdrLog[LdrLogLength++] = Digits[--n];
	LdrLog[LdrLogLength] = 0;
}

static void LogHex(ULONG Value)
{
	static const WCHAR Hex[] = L"0123456789ABCDEF";
	WCHAR Text[11];
	int i;
	Text[0] = L'0'; Text[1] = L'x';
	for (i = 0; i < 8; ++i) Text[2 + i] = Hex[(Value >> (28 - 4 * i)) & 0xF];
	Text[10] = 0;
	LogStr(Text);
}

static void LogLineStart(void)
{
	LogStr(L"KxGdiLdr: ");
}

static void LogLineEnd(void)
{
	LogStr(L"\r\n");
}

//
// Returns all collected log lines (each starting with "KxGdiLdr: ").
//
__declspec(dllexport) PCWSTR KxGdiLdrGetLog(void)
{
	return LdrLog;
}

///////////////////////////////////////////////////////////////////////////////
//
// Import rewriting
//
///////////////////////////////////////////////////////////////////////////////

static int AsciiLower(int c)
{
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static BOOL AsciiEqualN(PCSTR A, PCSTR B, SIZE_T Count)
{
	SIZE_T i;
	for (i = 0; i < Count; ++i) {
		if (AsciiLower((BYTE) A[i]) != AsciiLower((BYTE) B[i])) return FALSE;
	}
	return TRUE;
}

//
// Decides whether an import DLL name refers to gdi32 and returns the
// replacement name (never longer than the original). Name is at most
// MaxLength bytes long (bounded by the image size).
//
static PCSTR GetReplacementName(PCSTR Name, SIZE_T MaxLength)
{
	static const char ApiSetPrefix[] = "api-ms-win-dx-d3dkmt-";
	SIZE_T Length = 0;

	while (Length < MaxLength && Length < 128 && Name[Length]) ++Length;
	if (Length >= MaxLength || Length >= 128) return NULL;	// no terminator

	if (Length == 9 && AsciiEqualN(Name, "gdi32.dll", 9)) return "kxgdi.dll";
	if (Length == 5 && AsciiEqualN(Name, "gdi32", 5)) return "kxgdi";

	if (Length >= sizeof(ApiSetPrefix) - 1 + 4 &&
		AsciiEqualN(Name, ApiSetPrefix, sizeof(ApiSetPrefix) - 1)) {
		return "kxgdi.dll";
	}

	return NULL;
}

//
// Writes Size bytes to read-only image memory by temporarily making it
// writable. The image is not executing yet (it was just mapped), so changing
// the protection for a moment is safe.
//
static BOOL WriteImageMemory(void *Dest, const void *Src, SIZE_T Size)
{
	PVOID Base = Dest;
	SIZE_T RegionSize = Size;
	ULONG OldProtect;
	ULONG Unused;
	NTSTATUS Status;

	Status = NtProtectVirtualMemory(NtCurrentProcess(), &Base, &RegionSize, PAGE_READWRITE, &OldProtect);
	if (!NT_SUCCESS(Status)) return FALSE;

	memcpy(Dest, Src, Size);

	Base = Dest;
	RegionSize = Size;
	NtProtectVirtualMemory(NtCurrentProcess(), &Base, &RegionSize, OldProtect, &Unused);
	return TRUE;
}

static BOOL RewriteName(BYTE *Image, DWORD SizeOfImage, DWORD NameRva)
{
	PCSTR Name;
	PCSTR Replacement;
	SIZE_T ReplacementSize;

	if (NameRva == 0 || NameRva >= SizeOfImage) return FALSE;

	Name = (PCSTR) (Image + NameRva);
	Replacement = GetReplacementName(Name, SizeOfImage - NameRva);
	if (!Replacement) return FALSE;

	ReplacementSize = 0;
	while (Replacement[ReplacementSize]) ++ReplacementSize;

	return WriteImageMemory((void *) Name, Replacement, ReplacementSize + 1);
}

//
// Renames gdi32 imports (normal and delay-load) of a mapped image whose imports
// have not been resolved yet. Returns the number of renamed entries in *Count.
//
NTSTATUS KxRewriteGdi32Imports(PVOID ImageBase, UINT *Count)
{
	BYTE *Image = (BYTE *) ImageBase;
	IMAGE_DOS_HEADER *Dos;
	IMAGE_NT_HEADERS64 *Nt;
	IMAGE_DATA_DIRECTORY Dir;
	DWORD SizeOfImage;
	DWORD Rva;

	*Count = 0;

	__try {
		Dos = (IMAGE_DOS_HEADER *) Image;
		if (Dos->e_magic != IMAGE_DOS_SIGNATURE || Dos->e_lfanew <= 0 || Dos->e_lfanew > 0x1000) {
			return STATUS_INVALID_IMAGE_FORMAT;
		}

		Nt = (IMAGE_NT_HEADERS64 *) (Image + Dos->e_lfanew);
		if (Nt->Signature != IMAGE_NT_SIGNATURE ||
			Nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
			return STATUS_INVALID_IMAGE_FORMAT;
		}

		SizeOfImage = Nt->OptionalHeader.SizeOfImage;

		//
		// Normal imports.
		//

		if (Nt->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_IMPORT) {
			Dir = Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

			for (Rva = Dir.VirtualAddress;
				 Dir.VirtualAddress != 0 && Rva + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= SizeOfImage;
				 Rva += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {

				IMAGE_IMPORT_DESCRIPTOR *Desc = (IMAGE_IMPORT_DESCRIPTOR *) (Image + Rva);
				if (Desc->Name == 0) break;

				if (RewriteName(Image, SizeOfImage, Desc->Name)) {
					++*Count;

					// A bound descriptor would make the loader trust the
					// pre-bound gdi32 addresses. Mark it as unbound so the
					// imports are resolved against kxgdi.dll.
					if (Desc->TimeDateStamp != 0 && Desc->OriginalFirstThunk != 0) {
						DWORD Zero = 0;
						WriteImageMemory(&Desc->TimeDateStamp, &Zero, sizeof(Zero));
					}
				}
			}
		}

		//
		// Delay-load imports (RVA-based descriptors only).
		//

		if (Nt->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT) {
			Dir = Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];

			for (Rva = Dir.VirtualAddress;
				 Dir.VirtualAddress != 0 && Rva + sizeof(IMAGE_DELAYLOAD_DESCRIPTOR) <= SizeOfImage;
				 Rva += sizeof(IMAGE_DELAYLOAD_DESCRIPTOR)) {

				IMAGE_DELAYLOAD_DESCRIPTOR *Desc = (IMAGE_DELAYLOAD_DESCRIPTOR *) (Image + Rva);
				if (Desc->DllNameRVA == 0) break;

				if ((Desc->Attributes & 1) && RewriteName(Image, SizeOfImage, Desc->DllNameRVA)) {
					++*Count;
				}
			}
		}
	} __except (1) {
		return STATUS_ACCESS_VIOLATION;
	}

	return STATUS_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////
//
// Loader integration
//
///////////////////////////////////////////////////////////////////////////////

static WCHAR UpcaseW(WCHAR c)
{
	return (c >= L'a' && c <= L'z') ? (WCHAR) (c - (L'a' - L'A')) : c;
}

// TRUE if Path starts with "<SystemRoot>\" (case-insensitive).
static BOOL IsInSystemRoot(const UNICODE_STRING *Path)
{
	PCWSTR Root = KxSharedNtSystemRoot();
	UINT Cch = Path->Length / sizeof(WCHAR);
	UINT i;

	for (i = 0; Root[i]; ++i) {
		if (i >= Cch || UpcaseW(Root[i]) != UpcaseW(Path->Buffer[i])) return FALSE;
	}

	return (i > 0 && i < Cch && Path->Buffer[i] == L'\\');
}

static BOOL UnicodeEqualsI(const UNICODE_STRING *A, PCWSTR B)
{
	UINT Cch = A->Length / sizeof(WCHAR);
	UINT i;

	for (i = 0; i < Cch; ++i) {
		if (!B[i] || UpcaseW(A->Buffer[i]) != UpcaseW(B[i])) return FALSE;
	}

	return B[i] == 0;
}

static void InitUnicode(UNICODE_STRING *String, PCWSTR Text)
{
	USHORT Cch = 0;
	while (Text[Cch]) ++Cch;
	String->Length = (USHORT) (Cch * sizeof(WCHAR));
	String->MaximumLength = String->Length + sizeof(WCHAR);
	String->Buffer = (PWSTR) Text;
}

static BOOL IsDllLoaded(PCWSTR Name)
{
	UNICODE_STRING DllName;
	PVOID Handle;
	InitUnicode(&DllName, Name);
	return NT_SUCCESS(LdrGetDllHandle(NULL, NULL, &DllName, &Handle));
}

static void NTAPI DllNotification(
	ULONG								Reason,
	const LDR_DLL_LOADED_NOTIFICATION_DATA	*Data,
	PVOID								Context)
{
	UINT Count;
	NTSTATUS Status;

	(void) Context;

	if (Reason != LDR_DLL_NOTIFICATION_REASON_LOADED || !Data) return;
	if (!Data->FullDllName || !Data->BaseDllName) return;

	// System DLLs import the real gdi32 and must keep doing so; kxgdi.dll
	// itself forwards to gdi32.
	if (IsInSystemRoot(Data->FullDllName)) return;
	if (UnicodeEqualsI(Data->BaseDllName, L"kxgdi.dll")) return;

	Status = KxRewriteGdi32Imports(Data->DllBase, &Count);

	if (!NT_SUCCESS(Status)) {
		LogLineStart();
		LogUnicode(Data->BaseDllName);
		LogStr(L": rewrite failed, status ");
		LogHex((ULONG) Status);
		LogLineEnd();
	} else if (Count > 0) {
		LogLineStart();
		LogUnicode(Data->BaseDllName);
		LogStr(L": gdi32 -> kxgdi (");
		LogDec(Count);
		LogStr(L" import(s))");
		LogLineEnd();
	}
}

typedef BOOL (WINAPI *PFN_DLLMAIN)(PVOID DllBase, ULONG Reason, PVOID Reserved);

//
// Application Verifier is only used as a vehicle to get loaded early. On
// Windows 7, switch off as much of it as possible (heap and handle tracking),
// as VxKex does. Skipped if VxKex is loaded as well, because VxKex does the
// same thing and doing it twice would de-initialize verifier.dll twice.
//
static void ReduceApplicationVerifier(ULONG Major, ULONG Minor)
{
	ULONG *GlobalFlag = KxPebNtGlobalFlag();
	UNICODE_STRING VerifierName;
	PVOID VerifierBase;
	IMAGE_NT_HEADERS64 *Nt;
	PFN_DLLMAIN VerifierEntry;
	NTSTATUS Status;

	LogLineStart();

	if (!(*GlobalFlag & FLG_APPLICATION_VERIFIER)) {
		LogStr(L"Application Verifier already reduced (VxKex?), leaving it alone");
		LogLineEnd();
		return;
	}

	if (IsDllLoaded(L"KexDll.dll")) {
		LogStr(L"VxKex is loaded, leaving Application Verifier to VxKex");
		LogLineEnd();
		return;
	}

	if (Major != 6 || Minor != 1) {
		LogStr(L"not Windows 7, Application Verifier left as is");
		LogLineEnd();
		return;
	}

	InitUnicode(&VerifierName, L"verifier.dll");
	Status = LdrGetDllHandle(NULL, NULL, &VerifierName, &VerifierBase);

	if (NT_SUCCESS(Status)) {
		__try {
			Nt = (IMAGE_NT_HEADERS64 *) ((BYTE *) VerifierBase +
				((IMAGE_DOS_HEADER *) VerifierBase)->e_lfanew);
			if (Nt->OptionalHeader.AddressOfEntryPoint != 0) {
				VerifierEntry = (PFN_DLLMAIN) ((BYTE *) VerifierBase + Nt->OptionalHeader.AddressOfEntryPoint);
				VerifierEntry(VerifierBase, DLL_PROCESS_DETACH, NULL);
			}
		} __except (1) {
			LogStr(L"(verifier.dll de-init raised an exception) ");
		}
	}

	*GlobalFlag &= ~(FLG_APPLICATION_VERIFIER | FLG_HEAP_PAGE_ALLOCS);
	NtSetInformationProcess(NtCurrentProcess(), ProcessHandleTracing, NULL, 0);

	LogStr(L"Application Verifier reduced");
	LogLineEnd();
}

static RTL_VERIFIER_DLL_DESCRIPTOR VerifierDlls[] = {
	{NULL, 0, NULL, NULL}
};

static RTL_VERIFIER_PROVIDER_DESCRIPTOR VerifierProvider = {
	sizeof(RTL_VERIFIER_PROVIDER_DESCRIPTOR),
	VerifierDlls
};

static void Initialize(void)
{
	ULONG Major, Minor, Build;
	PVOID Cookie;
	NTSTATUS Status;
	UINT Count;

	RtlGetNtVersionNumbers(&Major, &Minor, &Build);

	LogLineStart();
	LogStr(L"loaded on Windows ");
	LogDec(Major); LogStr(L"."); LogDec(Minor); LogStr(L"."); LogDec(Build & 0xFFFF);
	LogLineEnd();

	ReduceApplicationVerifier(Major, Minor);

	// Later DLLs: rewritten when mapped, before their imports are resolved.
	Status = LdrRegisterDllNotification(0, DllNotification, NULL, &Cookie);
	if (!NT_SUCCESS(Status)) {
		LogLineStart();
		LogStr(L"LdrRegisterDllNotification failed, status ");
		LogHex((ULONG) Status);
		LogLineEnd();
	}

	// The program itself is already mapped, but its imports are not resolved yet.
	Status = KxRewriteGdi32Imports(KxPebImageBaseAddress(), &Count);
	LogLineStart();
	LogStr(L"main executable: ");
	if (NT_SUCCESS(Status)) {
		LogStr(L"gdi32 -> kxgdi (");
		LogDec(Count);
		LogStr(L" import(s))");
	} else {
		LogStr(L"rewrite failed, status ");
		LogHex((ULONG) Status);
	}
	LogLineEnd();
}

//
// As a verifier provider, DllMain is called with DLL_PROCESS_VERIFIER first
// (Reserved points to where the provider descriptor must be stored), and
// later with DLL_PROCESS_ATTACH like any DLL.
//
BOOL WINAPI DllMain(PVOID DllBase, ULONG Reason, PVOID Reserved)
{
	static BOOL Initialized = FALSE;

	(void) DllBase;

	if (Reason == DLL_PROCESS_VERIFIER) {
		if (Reserved) {
			*(RTL_VERIFIER_PROVIDER_DESCRIPTOR **) Reserved = &VerifierProvider;
		}

		if (!Initialized) {
			Initialized = TRUE;
			Initialize();
		}
	}

	return TRUE;
}
