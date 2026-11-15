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

	{
		static BYTE NotAnImage[64];
		Status = KxRewriteGdi32Imports(NotAnImage, &Count);
		TestCheck(Status == STATUS_INVALID_IMAGE_FORMAT && Count == 0, "rejects data that is not a PE image");
	}

	TestFinish();
}
