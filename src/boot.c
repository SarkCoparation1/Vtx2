#include <efi.h>
#include <efilib.h>

typedef struct {
    UINT8 *framebuffer;
    UINT32 width;
    UINT32 height;
    UINT32 ppsl;
    UINT8 bpp;
    UINT8 bytes_per_pixel;
    UINT8 is_bgr;
    UINT64 rsdp;
    UINT64 memory_map_addr;
    UINT64 memory_map_size;
    UINT64 memory_map_descriptor_size;
} BootInfo;

#define KERNEL_PAGES 512

/* QEMU: -debugcon file:boot-debug.log -global isa-debugcon.iobase=0xe9 */
static void debug_mark(char mark) {
    __asm__ volatile ("outb %0, $0xe9" : : "a"(mark));
}

EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;
    InitializeLib(ImageHandle, SystemTable);
    debug_mark('A');

    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    Status = uefi_call_wrapper(BS->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);
    if (EFI_ERROR(Status)) return Status;
    debug_mark('B');

    BootInfo bootInfo;
    bootInfo.framebuffer = (UINT8 *)gop->Mode->FrameBufferBase;
    bootInfo.width = gop->Mode->Info->HorizontalResolution;
    bootInfo.height = gop->Mode->Info->VerticalResolution;
    bootInfo.ppsl = gop->Mode->Info->PixelsPerScanLine;

    if (gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) {
        bootInfo.is_bgr = 1;
        bootInfo.bpp = 32;
        bootInfo.bytes_per_pixel = 4;
    } else if (gop->Mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        bootInfo.is_bgr = 0;
        bootInfo.bpp = 32;
        bootInfo.bytes_per_pixel = 4;
    } else {
        bootInfo.is_bgr = 0;
        bootInfo.bpp = 24;
        bootInfo.bytes_per_pixel = 3;
    }

    bootInfo.rsdp = 0;
    EFI_GUID acpi20Guid = ACPI_20_TABLE_GUID;
    for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        if (CompareGuid(&SystemTable->ConfigurationTable[i].VendorGuid, &acpi20Guid) == 0) {
            bootInfo.rsdp = (UINT64)SystemTable->ConfigurationTable[i].VendorTable;
            break;
        }
    }

    EFI_LOADED_IMAGE *loadedImage;
    EFI_GUID loadedGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    uefi_call_wrapper(BS->HandleProtocol, 3, ImageHandle, &loadedGuid, (void**)&loadedImage);

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fileSystem;
    EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    uefi_call_wrapper(BS->HandleProtocol, 3, loadedImage->DeviceHandle, &fsGuid, (void**)&fileSystem);
    debug_mark('C');

    EFI_FILE_PROTOCOL *rootDir;
    uefi_call_wrapper(fileSystem->OpenVolume, 2, fileSystem, &rootDir);

    EFI_FILE_PROTOCOL *kernelFile;
    Status = uefi_call_wrapper(rootDir->Open, 5, rootDir, &kernelFile, L"kernel.bin", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status)) return Status;
    debug_mark('D');

    EFI_PHYSICAL_ADDRESS kernelAddr = 0x00100000;
    Status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress, EfiLoaderData, KERNEL_PAGES, &kernelAddr);
    if (EFI_ERROR(Status)) {
        Status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, KERNEL_PAGES, &kernelAddr);
        if (EFI_ERROR(Status)) return Status;
    }

    UINTN readSize = 4096 * KERNEL_PAGES;
    Status = uefi_call_wrapper(kernelFile->Read, 3, kernelFile, &readSize, (void*)kernelAddr);
    if (EFI_ERROR(Status)) return Status;
    debug_mark('E');

    uefi_call_wrapper(kernelFile->Close, 1, kernelFile);
    uefi_call_wrapper(rootDir->Close, 1, rootDir);

    UINTN mapSize = 0, mapKey, descriptorSize;
    UINT32 descriptorVersion;
    
    Status = uefi_call_wrapper(BS->GetMemoryMap, 5, &mapSize, NULL, &mapKey, &descriptorSize, &descriptorVersion);
    if (Status != EFI_BUFFER_TOO_SMALL) return Status;

    mapSize += 4 * descriptorSize;
    EFI_MEMORY_DESCRIPTOR *memoryMap;
    Status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, mapSize, (void **)&memoryMap);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(BS->GetMemoryMap, 5, &mapSize, memoryMap, &mapKey, &descriptorSize, &descriptorVersion);
    if (EFI_ERROR(Status)) return Status;

    Status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, mapKey);
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(BS->GetMemoryMap, 5, &mapSize, memoryMap, &mapKey, &descriptorSize, &descriptorVersion);
        
        Status = uefi_call_wrapper(BS->ExitBootServices, 2, ImageHandle, mapKey);
        if (EFI_ERROR(Status)) return Status;
    }

    bootInfo.memory_map_addr = (UINT64)memoryMap;
    bootInfo.memory_map_size = (UINT64)mapSize;
    bootInfo.memory_map_descriptor_size = (UINT64)descriptorSize;

    void (*kernel_entry)(BootInfo*) = (void(*)(BootInfo*))kernelAddr;
    debug_mark('F');
    kernel_entry(&bootInfo);

    return EFI_SUCCESS;
}
