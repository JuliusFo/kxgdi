///////////////////////////////////////////////////////////////////////////////
//
// testutil.h - console output and checks for the loader tests (no CRT).
// Include after kxtypes.h.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

__declspec(dllimport) HANDLE	WINAPI GetStdHandle(DWORD StdHandle);
__declspec(dllimport) BOOL		WINAPI WriteFile(HANDLE File, const void *Buffer,
	DWORD BytesToWrite, DWORD *BytesWritten, PVOID Overlapped);
__declspec(dllimport) void		WINAPI ExitProcess(UINT ExitCode);
__declspec(dllimport) HMODULE	WINAPI GetModuleHandleW(PCWSTR ModuleName);
__declspec(dllimport) HMODULE	WINAPI LoadLibraryW(PCWSTR LibFileName);
__declspec(dllimport) HMODULE	WINAPI LoadLibraryExW(PCWSTR LibFileName, HANDLE File, DWORD Flags);
__declspec(dllimport) PVOID		WINAPI GetProcAddress(HMODULE Module, PCSTR ProcName);
__declspec(dllimport) DWORD		WINAPI GetModuleFileNameW(HMODULE Module, PWSTR FileName, DWORD Size);

static UINT TestFailures;

static void TestWrite(PCSTR Text)
{
	DWORD Length = 0, Written;
	while (Text[Length]) ++Length;
	WriteFile(GetStdHandle((DWORD) -11), Text, Length, &Written, NULL);
}

// Writes a UTF-16 string as ASCII ('?' for anything else).
static void TestWriteW(PCWSTR Text)
{
	char Buffer[256];
	UINT n = 0;

	while (*Text) {
		Buffer[n++] = (*Text < 0x80) ? (char) *Text : '?';
		++Text;
		if (n == sizeof(Buffer) - 1) {
			Buffer[n] = 0;
			TestWrite(Buffer);
			n = 0;
		}
	}

	Buffer[n] = 0;
	TestWrite(Buffer);
}

static void TestWriteHex(ULONG_PTR Value, int Digits)
{
	static const char Hex[] = "0123456789ABCDEF";
	char Text[20];
	int i;

	Text[0] = '0'; Text[1] = 'x';
	for (i = 0; i < Digits; ++i) Text[2 + i] = Hex[(Value >> (4 * (Digits - 1 - i))) & 0xF];
	Text[2 + Digits] = 0;
	TestWrite(Text);
}

static void TestCheck(BOOL Condition, PCSTR What)
{
	TestWrite(Condition ? "  [ OK ] " : "  [FAIL] ");
	TestWrite(What);
	TestWrite("\r\n");
	if (!Condition) ++TestFailures;
}

// Like TestCheck, but a failure only counts if Required is TRUE.
static void TestCheckOrInfo(BOOL Condition, BOOL Required, PCSTR What)
{
	if (Condition || Required) {
		TestCheck(Condition, What);
	} else {
		TestWrite("  [INFO] not on this Windows version: ");
		TestWrite(What);
		TestWrite("\r\n");
	}
}

static void TestFinish(void)
{
	TestWrite(TestFailures ? "RESULT: FAILED, failed checks: " : "RESULT: all checks passed ");
	TestWriteHex(TestFailures, 8);
	TestWrite("\r\n");
	ExitProcess(TestFailures);
}
