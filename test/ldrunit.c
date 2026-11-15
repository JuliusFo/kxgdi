///////////////////////////////////////////////////////////////////////////////
//
// ldrunit.c - unit test for the import rewriting of kxgdildr (no admin
// rights or registry changes needed).
//
// Maps kxgdi-ldrtest-dynamic.dll without resolving its imports (like the
// loader does before snapping), rewrites it and checks the import names.
//
///////////////////////////////////////////////////////////////////////////////

#include "../src/kxgdildr.c"
#include "testutil.h"

#define DONT_RESOLVE_DLL_REFERENCES	0x00000001

// Returns the name of the import descriptor whose name is gdi32/kxgdi, or NULL.
static PCSTR FindGdiImportName(BYTE *Image)
{
	IMAGE_NT_HEADERS64 *Nt = (IMAGE_NT_HEADERS64 *) (Image + ((IMAGE_DOS_HEADER *) Image)->e_lfanew);
	IMAGE_DATA_DIRECTORY Dir = Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	IMAGE_IMPORT_DESCRIPTOR *Desc = (IMAGE_IMPORT_DESCRIPTOR *) (Image + Dir.VirtualAddress);

	for (; Desc->Name; ++Desc) {
		PCSTR Name = (PCSTR) (Image + Desc->Name);
		if (AsciiEqualN(Name, "gdi32.dll", 10) || AsciiEqualN(Name, "kxgdi.dll", 10)) return Name;
	}

	return NULL;
}

//
// Synthetic PE32+ image with a bound gdi32 import, a user32 import, a
// delay-load gdi32 import and a bound import directory. Page aligned, so the
// rewrite has to deal with real page protection boundaries.
//

__declspec(align(4096)) static BYTE FakeImage[0x3000];

static void CopyStr(DWORD Rva, PCSTR Text)
{
	UINT i = 0;
	do { FakeImage[Rva + i] = (BYTE) Text[i]; } while (Text[i++]);
}

static void BuildFakeImage(void)
{
	IMAGE_DOS_HEADER *Dos = (IMAGE_DOS_HEADER *) FakeImage;
	IMAGE_NT_HEADERS64 *Nt = (IMAGE_NT_HEADERS64 *) (FakeImage + 0x40);
	IMAGE_IMPORT_DESCRIPTOR *Imports = (IMAGE_IMPORT_DESCRIPTOR *) (FakeImage + 0x1000);
	IMAGE_DELAYLOAD_DESCRIPTOR *Delay = (IMAGE_DELAYLOAD_DESCRIPTOR *) (FakeImage + 0x2100);

	Dos->e_magic = IMAGE_DOS_SIGNATURE;
	Dos->e_lfanew = 0x40;
	Nt->Signature = IMAGE_NT_SIGNATURE;
	Nt->FileHeader.Machine = 0x8664;
	Nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
	Nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
	Nt->OptionalHeader.SizeOfImage = sizeof(FakeImage);
	Nt->OptionalHeader.NumberOfRvaAndSizes = 16;
	Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0x1000;
	Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 3 * sizeof(IMAGE_IMPORT_DESCRIPTOR);
	Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0x800;
	Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size = 0x20;
	Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress = 0x2100;
	Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size = 2 * sizeof(IMAGE_DELAYLOAD_DESCRIPTOR);

	Imports[0].OriginalFirstThunk = 0x1400;
	Imports[0].TimeDateStamp = 0xFFFFFFFF;		// bound
	Imports[0].Name = 0x1200;
	Imports[0].FirstThunk = 0x1500;
	Imports[1].OriginalFirstThunk = 0x1600;
	Imports[1].Name = 0x1220;
	Imports[1].FirstThunk = 0x1700;
	CopyStr(0x1200, "GDI32.dll");
	CopyStr(0x1220, "USER32.dll");

	// The delay-load name crosses the page boundary at 0x2000 ("gdi32.dll"
	// occupies 0x1FFA..0x2003), so the write touches two pages.
	Delay[0].Attributes = 1;
	Delay[0].DllNameRVA = 0x1FFA;
	CopyStr(0x1FFA, "gdi32.dll");
}

static void TestFakeImage(void)
{
	IMAGE_NT_HEADERS64 *Nt = (IMAGE_NT_HEADERS64 *) (FakeImage + 0x40);
	IMAGE_IMPORT_DESCRIPTOR *Imports = (IMAGE_IMPORT_DESCRIPTOR *) (FakeImage + 0x1000);
	UINT Count;
	NTSTATUS Status;

	BuildFakeImage();

	Status = KxRewriteGdi32Imports(FakeImage, &Count);
	TestCheck(NT_SUCCESS(Status) && Count == 2, "synthetic image: normal + delay-load gdi32 renamed");
	TestCheck(AsciiEqualN((PCSTR) FakeImage + 0x1200, "kxgdi.dll", 10), "synthetic image: import name is kxgdi.dll");
	TestCheck(AsciiEqualN((PCSTR) FakeImage + 0x1220, "USER32.dll", 11), "synthetic image: user32 import untouched");
	TestCheck(AsciiEqualN((PCSTR) FakeImage + 0x1FFA, "kxgdi.dll", 10), "synthetic image: delay-load name across page boundary renamed");
	TestCheck(Imports[0].TimeDateStamp == 0, "synthetic image: bound descriptor marked unbound");
	TestCheck(Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress == 0 &&
		Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size == 0,
		"synthetic image: bound import directory removed");
}

void WINAPI TestMain(void)
{
	HMODULE Image;
	PCSTR Name;
	UINT Count;
	NTSTATUS Status;

	TestWrite("kxgdildr unit test\r\n");

	TestCheck(GetReplacementName("GDI32.dll", 64) != NULL, "matches GDI32.dll");
	TestCheck(GetReplacementName("gdi32", 64) != NULL, "matches gdi32");
	TestCheck(GetReplacementName("api-ms-win-dx-d3dkmt-l1-1-0.dll", 64) != NULL, "matches api-ms-win-dx-d3dkmt-l1-1-0.dll");
	TestCheck(GetReplacementName("user32.dll", 64) == NULL, "ignores user32.dll");
	TestCheck(GetReplacementName("xgdi32.dll", 64) == NULL, "ignores xgdi32.dll");
	TestCheck(GetReplacementName("gdi32.dllx", 64) == NULL, "ignores gdi32.dllx");
	TestCheck(GetReplacementName("gdi32.dll", 5) == NULL, "ignores unterminated names");

	Image = LoadLibraryExW(L"kxgdi-ldrtest-dynamic.dll", NULL, DONT_RESOLVE_DLL_REFERENCES);
	TestCheck(Image != NULL, "kxgdi-ldrtest-dynamic.dll maps without resolving imports");
	if (!Image) TestFinish();

	Name = FindGdiImportName((BYTE *) Image);
	TestCheck(Name != NULL && AsciiEqualN(Name, "gdi32.dll", 10), "import name is gdi32.dll before rewrite");

	Status = KxRewriteGdi32Imports(Image, &Count);
	TestCheck(NT_SUCCESS(Status) && Count == 1, "rewrite renames exactly one import");

	Name = FindGdiImportName((BYTE *) Image);
	TestCheck(Name != NULL && AsciiEqualN(Name, "kxgdi.dll", 10), "import name is kxgdi.dll after rewrite");

	Status = KxRewriteGdi32Imports(Image, &Count);
	TestCheck(NT_SUCCESS(Status) && Count == 0, "second rewrite changes nothing");

	TestFakeImage();

	{
		static BYTE NotAnImage[64];
		Status = KxRewriteGdi32Imports(NotAnImage, &Count);
		TestCheck(Status == STATUS_INVALID_IMAGE_FORMAT && Count == 0, "rejects data that is not a PE image");
	}

	TestFinish();
}
