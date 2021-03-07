#include <efi.h>
#include <protocol/efi-sfsp.h>
#include <protocol/efi-lip.h>

#include "elf.h"
#include "../common/memory_map.h"

#define kernel_path L"\\EFI\\Custom\\kernel.elf"

EFI_BOOT_SERVICES *BS;
EFI_SYSTEM_TABLE *ST;
EFI_HANDLE IH;
EFI_STATUS status;

CHAR16* EFI_ERRORS[] = {
	L"EFI_LOAD_ERROR",
	L"EFI_INVALID_PARAMETER",
	L"EFI_UNSUPPORTED",
	L"EFI_BAD_BUFFER_SIZE",
	L"EFI_BUFFER_TOO_SMALL",
	L"EFI_NOT_READY",
	L"EFI_DEVICE_ERROR",
	L"EFI_WRITE_PROTECTED",
	L"EFI_OUT_OF_RESOURCES",
	L"EFI_VOLUME_CORRUPTED",
	L"EFI_VOLUME_FULL",
	L"EFI_NO_MEDIA",
	L"EFI_MEDIA_CHANGED",
	L"EFI_NOT_FOUND",
	L"EFI_ACCESS_DENIED",
	L"EFI_NO_RESPONSE",
	L"EFI_NO_MAPPING",
	L"EFI_TIMEOUT",
	L"EFI_NOT_STARTED",
	L"EFI_ALREADY_STARTED",
	L"EFI_ABORTED",
	L"EFI_ICMP_ERROR",
	L"EFI_TFTP_ERROR",
	L"EFI_PROTOCOL_ERROR",
	L"EFI_INCOMPATIBLE_VERSION",
	L"EFI_SECURITY_VIOLATION",
	L"EFI_CRC_ERROR",
	L"EFI_END_OF_MEDIA",
	L"EFI_END_OF_FILE",
	L"EFI_INVALID_LANGUAGE",
	L"EFI_COMPROMISED_DATA",
	L"EFI_IP_ADDRESS_CONFLICT",
	L"EFI_HTTP_ERROR"
};

typedef __attribute__((sysv_abi)) uint64_t(*KernelEntryPoint)(MemoryMap); 

void hang() {
		for (;;);
}

void output_error() {
		ST->ConOut->OutputString(ST->ConOut, EFI_ERRORS[(status ^ EFI_ERR) - 1]);
		hang();
}

void output_number(uint64_t i) {
	//FIXME: This function is kinda garbage (it literally outputs the number in reverse order)
	if (i == 0) {
		ST->ConOut->OutputString(ST->ConOut, L"0");
		return;
	}
	char const digit[] = "0123456789";
	char text_digit[4];
	while (i != 0) {
		int current_digit = (i % 10);
		text_digit[0] = digit[current_digit];
		text_digit[1] = '\0';
		text_digit[2] = '\0';
		text_digit[3] = '\0';
		ST->ConOut->OutputString(ST->ConOut, (CHAR16*) text_digit);
		i /= 10;
	}
}

void error_check(CHAR16 *name) {
	if (status != EFI_SUCCESS) {
		ST->ConOut->OutputString(ST->ConOut, name);
		ST->ConOut->OutputString(ST->ConOut, L" failed: ");
		output_error();
	}
}

MemoryMap get_memory_map() {
	EFI_MEMORY_DESCRIPTOR *memory_descriptors;
	UINT32 version = 0;
	UINTN map_key = 0;
	UINTN descriptor_size = 0;
	UINTN memory_map_size = 0;
	
	status = BS->GetMemoryMap(&memory_map_size, memory_descriptors, &map_key, &descriptor_size, &version);
	if (status == EFI_SUCCESS) {
		ST->ConOut->OutputString(ST->ConOut, L"First call to memory map succeeded (memory map is empty?)");
		hang();
	}
	if (status != EFI_BUFFER_TOO_SMALL) {
		error_check(L"GetMemoryMap 1");
	}
	UINTN correct_size = memory_map_size + (2 * descriptor_size);

	status = BS->AllocatePool(EfiLoaderData, correct_size, (void**) &memory_descriptors);
	if (status != EFI_SUCCESS) error_check(L"AllocatePool memory_map");
	
	memory_map_size = correct_size;
	status = BS->GetMemoryMap(&memory_map_size, memory_descriptors, &map_key, &descriptor_size, &version);
	error_check(L"GetMemoryMap 2");
	
	MemoryMap memory_map;
	memory_map.memory_descriptors = memory_descriptors;
	memory_map.memory_map_size = memory_map_size;
	memory_map.map_key = map_key;
	memory_map.descriptor_size = descriptor_size;

	return memory_map;
}

KernelEntryPoint load_kernel() {
	//Make sure the simple file protocol is supported
	EFI_LOADED_IMAGE_PROTOCOL *loaded_image;
	EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	status = ST->BootServices->OpenProtocol(IH, &li_guid, (void**)&loaded_image, IH, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	error_check(L"OpenProtocol LoadedImage");

	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *sfs;
	EFI_GUID sfs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	status = ST->BootServices->OpenProtocol(loaded_image->DeviceHandle, &sfs_guid, (void**)&sfs, IH, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	error_check(L"OpenProtocol SimpleFileSystem");

	//Load kernel off disk into memory
	EFI_FILE_PROTOCOL *drive_root;
	status = sfs->OpenVolume(sfs, &drive_root);
	error_check(L"OpenVolume drive_root");

	EFI_FILE_PROTOCOL *kernel_file;
	status = drive_root->Open(drive_root, &kernel_file, kernel_path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
	if (status == EFI_NOT_FOUND) {
		ST->ConOut->OutputString(ST->ConOut, L"Kernel could not be found at ");
		ST->ConOut->OutputString(ST->ConOut, kernel_path);
		ST->ConOut->OutputString(ST->ConOut, L"\r\n");
		hang();
	}
	error_check(L"Open kernel_file");


	//TODO: This may cause a buffer size error
	EFI_FILE_INFO *file_info;
	EFI_GUID fi_guid = EFI_FILE_INFO_ID;
	UINTN buffer_size = 256;

	status = BS->AllocatePool(EfiBootServicesData, buffer_size, (void**) &file_info);
	error_check(L"AllocatePool file_info");
	
	status = kernel_file->GetInfo(kernel_file, &fi_guid, &buffer_size, file_info);
	error_check(L"GetInfo kernel_file");
	
	UINTN file_size = file_info->FileSize;
	status = BS->FreePool(file_info);
	error_check(L"FreePool file_info");

	UINTN pages_required = (file_size / 4096) + 1;
	uint8_t *kernel_buffer;
	status = BS->AllocatePages(AllocateAnyPages, EfiBootServicesData, pages_required, (EFI_PHYSICAL_ADDRESS*) kernel_buffer);
	error_check(L"AllocatePages kernel_buffer");
	
	status = kernel_file->Read(kernel_file, &file_size, (void*) kernel_buffer);
	error_check(L"Read kernel_file");
	
	//Parse kernel file (ELF format)
	//NOTE: This is a 64 bit header, it will not corrctly parse 32 bit ELF executable
 	ElfHeader *elf_header = (ElfHeader*) kernel_buffer;
	char *magic = elf_header->magic;
	if (magic[0] != 0x7f || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') {
		ST->ConOut->OutputString(ST->ConOut, L"Kernel is not a valid ELF file\r\n");
		hang();
	}

	if (elf_header->architecture != 2) {
		ST->ConOut->OutputString(ST->ConOut, L"Kernel is not 64 bit\r\n");
		hang();
	}

	uint64_t lowest_address = -1;
	uint64_t highest_address = -1;
	for (int i = 0; i < elf_header->header_entry_count; i++) {
		ProgramHeader *program_header = (ProgramHeader*) ((uint64_t) kernel_buffer + (elf_header->header_table + (i * elf_header->header_entry_size)));
		if (program_header->segment_type == 1) {
			uint64_t memory_end = program_header->virtual_address + program_header->memory_segment_size;
			if (lowest_address == -1 || program_header->virtual_address < lowest_address) lowest_address = program_header->virtual_address;
			if (highest_address == -1 || memory_end > highest_address) highest_address = memory_end;
		}
	}
	
	uint64_t program_size = highest_address - lowest_address;
	pages_required = (program_size / 4096) + 1;

	//Allocate memory for kernel
	EFI_PHYSICAL_ADDRESS kernel = lowest_address;
	status = BS->AllocatePages(AllocateAddress, EfiBootServicesData, pages_required, &kernel);
	error_check(L"AllocatePages kernel");
	
	//Copy kernel sections into newly allocated memory
	for (int i = 0; i < elf_header->header_entry_count; i++) {
		ProgramHeader *program_header = (ProgramHeader*) ((uint64_t) kernel_buffer + (elf_header->header_table + (i * elf_header->header_entry_size)));
		if (program_header->segment_type == 1) {
			for (int j = 0; j < program_header->file_segment_size; j++) {
				*((uint8_t*) program_header->virtual_address + j) = *(kernel_buffer + program_header->offset + j);
				//zero the rest?
			}
		}
	}
	
	//FIXME: For some god forsaken reason this throws an error every time, so I've had to comment it out for now
	//Free original kernel buffer
	//status = BS->FreePool(&kernel_buffer);
	//error_check(L"FreePool kernel_buffer");
	
	return (KernelEntryPoint) elf_header->entry_point;
}

//TODO: Write your own UEFI call wrapper which integrates error checking
EFI_STATUS efi_main (EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *systab) {
	//TODO: Investigate the InitializeLib function and see if it is necessary (our current EFI headers don't support it)
	//InitializeLib(IH, systab);
	ST = systab;
	BS = ST->BootServices;
	IH = image_handle;

	//Clear screen before using it
	status = ST->ConOut->ClearScreen(ST->ConOut);
	error_check(L"ClearScreen");

	//Disable Watchdog timer to prevent system from rebooting after 5 minutes
	status = BS->SetWatchdogTimer(0, 0, 0, 0);
	error_check(L"ClearWatchdogTimer");

	KernelEntryPoint entry_point = load_kernel();
	MemoryMap memory_map = get_memory_map();
	BS->ExitBootServices(IH, memory_map.map_key);
	
	entry_point(memory_map);
	
	ST->ConOut->ClearScreen(ST->ConOut);
	ST->ConOut->OutputString(ST->ConOut, L"Kernel exited, this should never happen.\r\n");
	hang();

	return EFI_SUCCESS;
}