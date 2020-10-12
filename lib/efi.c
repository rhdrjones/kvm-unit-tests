/*
 * efi_main() and supporting functions to wrap tests into EFI apps
 *
 * Copyright (c) 2020 Red Hat Inc
 *
 * Authors:
 *  Andrew Jones <drjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <libcflat.h>
#include <asm/setup.h>
#undef ALIGN
#include <efi.h>
#include <efilib.h>

#ifndef EFI_DEBUG
#undef ASSERT
static void _no_assert(bool a) {}
#define _ASSERT(a)	_no_assert(a)
#define ASSERT(a)	_ASSERT(a)
#endif

#define VAR_GUID \
	{ 0x97ef3e03, 0x7329, 0x4a6a, {0xb9, 0xba, 0x6c, 0x1f, 0xdc, 0xc5, 0xf8, 0x23} }
#define DTB_BASENAME L"\\dtb"

extern int __argc, __envc;
extern char *__argv[100];
extern char *__environ[200];
extern struct mem_region *mem_regions;

extern void primary_entry(void *fdt, uint64_t freemem_start, uint64_t stacktop);

static EFI_GUID efi_var_guid = VAR_GUID;

static CHAR16 *__efi_dtb_basename;
static const CHAR16 *efi_dtb_basename = DTB_BASENAME;

/*
 * Convert a string of CHAR16 to a string of char in place.
 * This is possible because we only expect ASCII characters.
 */
static char *efi_char16_to_char(CHAR16 *Str)
{
	CHAR16 *w = Str;
	char *s = (char *)Str;

	while (*w) {
		char t = *(char *)w;
		ASSERT((CHAR16)t == *w);
		*s = t;
		s++, w++;
	}
	*s = '\0';

	return (char *)Str;
}

static void efi_setup_argv(EFI_HANDLE Image, EFI_SYSTEM_TABLE *SysTab)
{
	CHAR16 Name[256];
	EFI_GUID Vendor;
	CHAR16 **Argv;
	INTN Argc;
	int i;

	Argc = GetShellArgcArgv(Image, &Argv);

	__argc = (int)Argc;
	for (i = 0; i < __argc; ++i) {
		__argv[i] = efi_char16_to_char(Argv[i]);
	}

	Name[0] = 0;
	Vendor = NullGuid;

	while (1) {
		EFI_STATUS Status;
		UINTN NameSize = sizeof(Name), ValSize;
		CHAR16 *Val, *Env;

		Status = uefi_call_wrapper(RT->GetNextVariableName, 3,
					   &NameSize, Name, &Vendor);
		if (Status != EFI_SUCCESS)
			break;

		if (CompareGuid(&Vendor, &efi_var_guid) != 0)
			continue;

		Val = LibGetVariableAndSize(Name, &Vendor, &ValSize);
		Env = AllocateZeroPool(StrSize(Name) + ValSize + sizeof(CHAR16));
		StrCpy(Env, Name);
		StrCat(Env, L"=");
		CopyMem(&Env[StrLen(Name) + 1], Val, ValSize);
		FreePool(Val);

		__environ[__envc++] = efi_char16_to_char(Env);
        }
}

/*
 * Generate a file path given the loaded image's path.
 */
static void efi_generate_path(EFI_LOADED_IMAGE *LoadedImage,
			      const CHAR16 *ImagePath,
			      CHAR16 **PathName)
{
	CHAR16 *CurrPath = DevicePathToStr(LoadedImage->FilePath);
	unsigned int path_len = StrLen(CurrPath);
	int i;

	for (i = path_len; i > 0; --i) {
		if (CurrPath[i] == '\\')
			break;
	}
	CurrPath[i+1] = 0;

	if (i == 0 || CurrPath[i-1] == '\\')
		CurrPath[i] = 0;

	*PathName = AllocatePool(StrSize(CurrPath) + StrSize(ImagePath));
	ASSERT(*PathName);

	*PathName[0] = 0;
	StrCat(*PathName, CurrPath);
	StrCat(*PathName, ImagePath);
}

/*
 * Open the file and read it into a buffer.
 */
static void efi_load_image(EFI_LOADED_IMAGE *LoadedImage, void **data,
			   int *datasize, CHAR16 *PathName)
{
	UINTN BufferSize = sizeof(EFI_FILE_INFO);
	EFI_FILE_INFO *FileInfo;
	EFI_FILE_IO_INTERFACE *IoIf;
	EFI_FILE *Root, *File;
	EFI_STATUS Status;

	/* Open the device */
	Status = uefi_call_wrapper(BS->HandleProtocol, 3,
				   LoadedImage->DeviceHandle,
				   &gEfiSimpleFileSystemProtocolGuid,
				   (void **)&IoIf);
	ASSERT(Status == EFI_SUCCESS);

	Status = uefi_call_wrapper(IoIf->OpenVolume, 2, IoIf, &Root);
	ASSERT(Status == EFI_SUCCESS);

	/* And then open the file */
	Status = uefi_call_wrapper(Root->Open, 5, Root, &File, PathName,
				   EFI_FILE_MODE_READ, 0);
	if (Status != EFI_SUCCESS) {
		Print(L"Failed to open %s - %lx\n", PathName, Status);
		ASSERT(Status == EFI_SUCCESS);
	}

	/* Find the file size in order to allocate the buffer */
	FileInfo = AllocatePool(BufferSize);
	ASSERT(FileInfo);

	Status = uefi_call_wrapper(File->GetInfo, 4, File, &gEfiFileInfoGuid,
				   &BufferSize, FileInfo);

	if (Status == EFI_BUFFER_TOO_SMALL) {
		FreePool(FileInfo);
		FileInfo = AllocatePool(BufferSize);
		ASSERT(FileInfo);
		Status = uefi_call_wrapper(File->GetInfo, 4, File,
					   &gEfiFileInfoGuid, &BufferSize,
					   FileInfo);
	}

	ASSERT(Status == EFI_SUCCESS);

	BufferSize = FileInfo->FileSize;

	FreePool(FileInfo);

	*data = AllocatePool(BufferSize);
	ASSERT(*data);

	/* Perform the actual read */
	Status = uefi_call_wrapper(File->Read, 3, File, &BufferSize, *data);

	if (Status == EFI_BUFFER_TOO_SMALL) {
		FreePool(*data);
		*data = AllocatePool(BufferSize);
		Status = uefi_call_wrapper(File->Read, 3, File, &BufferSize, *data);
	}

	ASSERT(Status == EFI_SUCCESS);

	*datasize = BufferSize;
}

static void *efi_get_fdt(EFI_HANDLE Image, EFI_SYSTEM_TABLE *SysTab)
{
	EFI_GUID LoadedImageProtocol = LOADED_IMAGE_PROTOCOL;
	EFI_LOADED_IMAGE *LoadedImage;
	EFI_STATUS Status;
	CHAR16 Name[256], *Val, *PathName = NULL;
	UINTN ValSize;
	void *fdt = NULL;
	int fdtsize;

	StrCpy(Name, L"DTB_BASENAME");
	Val = LibGetVariableAndSize(Name, &efi_var_guid, &ValSize);
	if (Val) {
		__efi_dtb_basename = AllocateZeroPool(ValSize + 2 * sizeof(CHAR16));
		__efi_dtb_basename[0] = '\\';
		CopyMem(&__efi_dtb_basename[1], Val, ValSize);
		FreePool(Val);
		efi_dtb_basename = __efi_dtb_basename;
	}

	/* Get the loaded image protocol to find the path */
	Status = uefi_call_wrapper(BS->HandleProtocol, 3, Image,
				   &LoadedImageProtocol, (void **)&LoadedImage);
	ASSERT(Status == EFI_SUCCESS);

	/* Build the new path from the existing one plus the dtb name */
	efi_generate_path(LoadedImage, efi_dtb_basename, &PathName);

	/* Load the dtb */
	efi_load_image(LoadedImage, &fdt, &fdtsize, PathName);

	FreePool(PathName);

	return fdt;
}

#if 0
static const CHAR16 *MemTypes[] = {
	L"EfiReservedMemoryType",
	L"EfiLoaderCode",
	L"EfiLoaderData",
	L"EfiBootServicesCode",
	L"EfiBootServicesData",
	L"EfiRuntimeServicesCode",
	L"EfiRuntimeServicesData",
	L"EfiConventionalMemory",
	L"EfiUnusableMemory",
	L"EfiACPIReclaimMemory",
	L"EfiACPIMemoryNVS",
	L"EfiMemoryMappedIO",
	L"EfiMemoryMappedIOPortSpace",
	L"EfiPalCode",
};
#endif

#define EXTRA_MEM_REGIONS	17	/* 16 plus one for the unit test code section */

extern unsigned long _text, _etext, _data, _edata;

static uint64_t efi_set_mem_regions(UINTN *MapKey)
{
	UINTN NoEntries, DescriptorSize;
	UINT32 DescriptorVersion;
	uintptr_t text = (uintptr_t)&_text, etext = __ALIGN((uintptr_t)&_etext, 4096);
	uintptr_t data = (uintptr_t)&_data, edata = __ALIGN((uintptr_t)&_edata, 4096);
	uint64_t biggest_conventional = 0, freemem_start = 0;
	struct mem_region *r;
	char *buffer;
	int i;

	buffer = (char *)LibMemoryMap(&NoEntries, MapKey, &DescriptorSize, &DescriptorVersion);
	ASSERT(DescriptorVersion == 1);

	mem_regions = AllocateZeroPool(sizeof(struct mem_region) * (NoEntries + EXTRA_MEM_REGIONS));

	/*
	 * Need to get the memory map again after the mem_regions allocation
	 * to get the new MapKey.
	 */
	FreePool(buffer);
	buffer = (char *)LibMemoryMap(&NoEntries, MapKey, &DescriptorSize, &DescriptorVersion);

	for (i = 0, r = &mem_regions[0]; i < NoEntries * DescriptorSize; i += DescriptorSize, ++r) {
		EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)&buffer[i];

		r->start = d->PhysicalStart;
		r->end = d->PhysicalStart + d->NumberOfPages * 4096;

		switch (d->Type) {
		case EfiReservedMemoryType:
		case EfiUnusableMemory:
		case EfiACPIReclaimMemory:
		case EfiACPIMemoryNVS:
		case EfiPalCode:
		case EfiBootServicesCode:
		case EfiBootServicesData:
		case EfiRuntimeServicesCode:
			r->flags = MR_F_RESERVED;
			break;
		case EfiMemoryMappedIO:
		case EfiMemoryMappedIOPortSpace:
			r->flags = MR_F_IO;
			break;
		case EfiLoaderCode:
			if (r->start <= text && r->end > text) {
				/* This is the unit test region. Flag the code separately. */
				phys_addr_t tmp = r->end;
				ASSERT(etext <= data);
				ASSERT(edata <= r->end);
				r->flags = MR_F_CODE;
				r->end = data;
				++r;
				r->start = data;
				r->end = tmp;
			} else {
				r->flags = MR_F_RESERVED;
			}
			break;
		case EfiConventionalMemory:
			if (biggest_conventional < d->NumberOfPages) {
				biggest_conventional = d->NumberOfPages;
				freemem_start = d->PhysicalStart;
			}
			break;
		}

//		Print(L"type=%s %lx %lx\n", MemTypes[d->Type], d->PhysicalStart, d->PhysicalStart + d->NumberOfPages * 4096);
	}

	ASSERT(freemem_start);

	return freemem_start;
}

static uint64_t efi_allocate_stack(void)
{
	EFI_STATUS Status;
	EFI_PHYSICAL_ADDRESS Memory;
	UINTN Pages;
	uint64_t stack;

	Pages = EFI_SIZE_TO_PAGES(2 * MIN_STACK_SIZE - 1);

	Status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages,
				   EfiRuntimeServicesData, Pages, &Memory);
	ASSERT(Status == EFI_SUCCESS);

	/* Naturally align the stack base */
	stack = ((uint64_t)(UINTN)Memory + MIN_STACK_SIZE - 1) & ~(MIN_STACK_SIZE - 1);

	/* Return the stack top */
	return stack + MIN_STACK_SIZE;
}

EFI_STATUS efi_main(EFI_HANDLE Image, EFI_SYSTEM_TABLE *SysTab);

EFI_STATUS efi_main(EFI_HANDLE Image, EFI_SYSTEM_TABLE *SysTab)
{
	EFI_STATUS Status;
	UINTN MapKey;
	uint64_t freemem_start, stacktop;
	void *fdt;

	InitializeLib(Image, SysTab);

	efi_setup_argv(Image, SysTab);

	fdt = efi_get_fdt(Image, SysTab);

	stacktop = efi_allocate_stack();
//	Print(L"stacktop=%lx\n", stacktop);

	freemem_start = efi_set_mem_regions(&MapKey);

	Status = uefi_call_wrapper(BS->ExitBootServices, 2, Image, MapKey);
	ASSERT(Status == EFI_SUCCESS);

	primary_entry(fdt, freemem_start, stacktop);

	/* Unreachable */
	return EFI_UNSUPPORTED;
}

void efi_exit(int type, int code);

void efi_exit(int type, int code)
{
	switch (type) {
	case EfiResetCold:
	case EfiResetWarm:
	case EfiResetShutdown:
		break;
	default:
		type = EfiResetCold;
		break;
	}

	uefi_call_wrapper(RT->ResetSystem, 4, type, EFI_SUCCESS, 0, NULL);
}
