#!/bin/bash
set -e

echo "=========================================="
echo " Modern UEFI OS Build Script (Full Hardware & UI)"
echo "=========================================="

EFI_INC="/usr/include/efi"
EFI_LIB="/usr/lib"
LDS="/usr/lib/elf_x86_64_efi.lds"
CRT0="/usr/lib/crt0-efi-x86_64.o"

if [ ! -f "$LDS" ]; then
    LDS="/usr/lib64/elf_x86_64_efi.lds"
    CRT0="/usr/lib64/crt0-efi-x86_64.o"
    EFI_LIB="/usr/lib64"
fi

echo "[1/6] Eski derleme kalintilari temizleniyor..."
rm -rf iso BOOTX64.EFI kernel.bin kernel.elf *.o *.so vtx2.iso vtx2_bios.img bios_boot/*.bin

echo "[2/6] Bootloader (boot.c) derleniyor..."
gcc -I$EFI_INC -I$EFI_INC/x86_64 -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall -c boot.c -o boot.o
ld -nostdlib -znocombreloc -shared -Bsymbolic -L$EFI_LIB -T $LDS $CRT0 boot.o -o boot.so -lefi -lgnuefi
objcopy -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc --target=efi-app-x86_64 --subsystem=10 boot.so BOOTX64.EFI

echo "[3/6] Kernel, Scheduler, Donanim Suruculeri ve Uygulamalar Derleniyor..."
KERNEL_CFLAGS="-ffreestanding -mno-red-zone -mno-sse -mno-mmx -mno-80387 -fno-stack-protector -fno-pic -fno-pie"

gcc $KERNEL_CFLAGS -c kernel.c -o kernel.o
gcc $KERNEL_CFLAGS -c idt.c -o idt.o
gcc $KERNEL_CFLAGS -c pic.c -o pic.o
gcc $KERNEL_CFLAGS -c pit.c -o pit.o
gcc $KERNEL_CFLAGS -c task.c -o task.o
gcc $KERNEL_CFLAGS -c isr_stub.s -o isr_stub.o
gcc $KERNEL_CFLAGS -c exceptions.c -o exceptions.o
gcc $KERNEL_CFLAGS -c exceptions_stub.s -o exceptions_stub.o
gcc $KERNEL_CFLAGS -c syscall_stub.s -o syscall_stub.o
gcc $KERNEL_CFLAGS -c syscall.c -o syscall.o
gcc $KERNEL_CFLAGS -c gdt.c -o gdt.o
gcc $KERNEL_CFLAGS -c tss.c -o tss.o
gcc $KERNEL_CFLAGS -c paging.c -o paging.o
gcc $KERNEL_CFLAGS -c console.c -o console.o
gcc $KERNEL_CFLAGS -c acpi.c -o acpi.o
gcc $KERNEL_CFLAGS -c madt.c -o madt.o
gcc $KERNEL_CFLAGS -c lapic.c -o lapic.o
gcc $KERNEL_CFLAGS -c ioapic.c -o ioapic.o
gcc $KERNEL_CFLAGS -c irqctl.c -o irqctl.o
gcc $KERNEL_CFLAGS -c pmm.c -o pmm.o
gcc $KERNEL_CFLAGS -c kheap.c -o kheap.o
gcc $KERNEL_CFLAGS -c power.c -o power.o

gcc $KERNEL_CFLAGS -c mouse.c -o mouse.o
gcc $KERNEL_CFLAGS -c keyboard.c -o keyboard.o
gcc $KERNEL_CFLAGS -c ui.c -o ui.o
gcc $KERNEL_CFLAGS -c window.c -o window.o
gcc $KERNEL_CFLAGS -c desktop.c -o desktop.o
gcc $KERNEL_CFLAGS -c info.c -o info.o
gcc $KERNEL_CFLAGS -c maxbash.c -o maxbash.o

gcc $KERNEL_CFLAGS -c pci.c -o pci.o
gcc $KERNEL_CFLAGS -c xhci.c -o xhci.o
gcc $KERNEL_CFLAGS -c ahci.c -o ahci.o
gcc $KERNEL_CFLAGS -c vtx2fs.c -o vtx2fs.o
gcc $KERNEL_CFLAGS -c vtx2fs_ahci_bridge.c -o vtx2fs_ahci_bridge.o
gcc $KERNEL_CFLAGS -c vtx2fs_state.c -o vtx2fs_state.o

gcc $KERNEL_CFLAGS -c filemanager.c -o filemanager.o
gcc $KERNEL_CFLAGS -c editor.c -o editor.o

gcc $KERNEL_CFLAGS -c backbuffer.c -o backbuffer.o

ld -m elf_x86_64 -T linker.ld \
   kernel.o idt.o pic.o pit.o task.o isr_stub.o syscall_stub.o syscall.o \
   exceptions.o exceptions_stub.o \
   gdt.o tss.o paging.o console.o \
   acpi.o power.o madt.o lapic.o ioapic.o irqctl.o pmm.o kheap.o \
   mouse.o keyboard.o ui.o window.o desktop.o info.o maxbash.o \
   pci.o xhci.o ahci.o vtx2fs.o vtx2fs_ahci_bridge.o vtx2fs_state.o filemanager.o editor.o backbuffer.o \
   -o kernel.elf

objcopy -O binary kernel.elf kernel.bin

echo "[3b/6] Legacy BIOS Bootloader (MBR + Stage2) derleniyor..."
BIOS_BOOT_DIR="bios_boot"
if [ -d "$BIOS_BOOT_DIR" ]; then
    nasm -f bin "$BIOS_BOOT_DIR/stage1.asm" -o "$BIOS_BOOT_DIR/stage1.bin"
    nasm -f bin "$BIOS_BOOT_DIR/stage2.asm" -o "$BIOS_BOOT_DIR/stage2.bin"

    # Disk imaji duzeni (LBA cinsinden):
    #   sektor 0      : stage1.bin (MBR, 512 byte)
    #   sektor 1-96   : stage2.bin (96 sektor = 48 KiB ayrilmis)
    #   sektor 97+    : kernel.bin (256 sektor = 128 KiB ayrilmis, kernel bunun altinda kalmali)
    STAGE2_SECTORS=96
    KERNEL_START_LBA=$((1 + STAGE2_SECTORS))   # stage2.asm'deki KERNEL_START_LBA ile ayni olmali
    KERNEL_MAX_SECTORS=256                      # stage2.asm'deki KERNEL_MAX_SECTORS ile ayni olmali

    KERNEL_SIZE=$(stat -c%s kernel.bin)
    KERNEL_SECTORS_NEEDED=$(( (KERNEL_SIZE + 511) / 512 ))
    if [ "$KERNEL_SECTORS_NEEDED" -gt "$KERNEL_MAX_SECTORS" ]; then
        echo "    !! HATA: kernel.bin ($KERNEL_SECTORS_NEEDED sektor) BIOS bootloader'in" \
             "ayirdigi $KERNEL_MAX_SECTORS sektorden buyuk." \
             "bios_boot/stage1.asm ve stage2.asm'deki KERNEL_MAX_SECTORS'u artirip yeniden derleyin."
        exit 1
    fi

    VTX2_BIOS_IMG="vtx2_bios.img"
    dd if=/dev/zero of="$VTX2_BIOS_IMG" bs=1M count=16 status=none
    dd if="$BIOS_BOOT_DIR/stage1.bin" of="$VTX2_BIOS_IMG" conv=notrunc status=none
    dd if="$BIOS_BOOT_DIR/stage2.bin" of="$VTX2_BIOS_IMG" bs=512 seek=1 conv=notrunc status=none
    dd if=kernel.bin of="$VTX2_BIOS_IMG" bs=512 seek=$KERNEL_START_LBA conv=notrunc status=none
    echo "    -> $VTX2_BIOS_IMG hazir (kernel: $KERNEL_SECTORS_NEEDED/$KERNEL_MAX_SECTORS sektor kullanildi)"
else
    echo "    -> $BIOS_BOOT_DIR bulunamadi, BIOS imaji atlaniyor"
fi

echo "[4/6] ISO Dizin Yapisi ve EFI FAT Imai Hazirlaniyor..."
mkdir -p iso/EFI/BOOT
cp BOOTX64.EFI iso/EFI/BOOT/BOOTX64.EFI
cp kernel.bin iso/kernel.bin
printf "\\EFI\\BOOT\\BOOTX64.EFI\r\n" > iso/startup.nsh

dd if=/dev/zero of=iso/efiboot.img bs=1K count=4096
mkfs.vfat iso/efiboot.img
mmd -i iso/efiboot.img ::EFI
mmd -i iso/efiboot.img ::EFI/BOOT

mcopy -i iso/efiboot.img BOOTX64.EFI ::EFI/BOOT/
mcopy -i iso/efiboot.img kernel.bin ::kernel.bin
mcopy -i iso/efiboot.img iso/startup.nsh ::startup.nsh

VTX2_DISK_IMG="vtx2_test.img"
if [ ! -f "$VTX2_DISK_IMG" ]; then
    echo "    -> $VTX2_DISK_IMG bulunamadi, 64MB bos disk imaji olusturuluyor..."
    qemu-img create -f raw "$VTX2_DISK_IMG" 64M
fi

USB_DISK_IMG="usb_disk.img"
if [ ! -f "$USB_DISK_IMG" ]; then
    echo "    -> $USB_DISK_IMG bulunamadi, 64MB bos USB imaji olusturuluyor..."
    qemu-img create -f raw "$USB_DISK_IMG" 64M
fi

echo "[5/6] ISO Olusturuluyor..."
xorriso -as mkisofs \
  -r -V "VTX2_OS" \
  -e efiboot.img \
  -no-emul-boot \
  -o vtx2.iso \
  iso

echo "[6/6] QEMU Baslatiliyor..."
if [ "${SKIP_QEMU:-0}" = "1" ]; then
    echo "    -> SKIP_QEMU=1: derleme tamamlandi; emulator baslatilmadi"
    exit 0
fi

if [ "${BIOS_TARGET:-0}" = "1" ]; then
    echo "    -> BIOS_TARGET=1: vtx2_bios.img Legacy BIOS ile (OVMF/UEFI KULLANILMADAN) baslatiliyor"
    if [ ! -f vtx2_bios.img ]; then
        echo "    !! vtx2_bios.img bulunamadi (bios_boot/ eksik olabilir)"
        exit 1
    fi

    # ONEMLI: vtx2_bios.img, UEFI akisindaki gibi AHCI portuna
    # (if=none + -device ide-hd,bus=ahci0.0) baglaniyor - 'if=ide' ile
    # eklenirse QEMU otomatik LEGACY bir IDE controller olusturur ve
    # kernelin pci_find_device_by_class(0x01,0x06,0x01,...) ile aradigi
    # gercek AHCI aygitina hicbir disk baglanmamis olur (AHCI: baslatilamadi).
    BIOS_QEMU_ARGS=(
        -m 512
        -vga std
        -drive id=vtx2bootdisk,if=none,format=raw,file=vtx2_bios.img
        -device ahci,id=ahci0
        -device ide-hd,drive=vtx2bootdisk,bus=ahci0.0
        -device qemu-xhci,id=xhci
        -boot order=c,menu=off
    )

    if [ "${QEMU_DEBUG:-0}" = "1" ]; then
        echo "    -> QEMU_DEBUG=1: yeniden baslatma durduruluyor; log: boot-debug.log"
        rm -f boot-debug.log
        BIOS_QEMU_ARGS+=(
            -no-reboot
            -debugcon file:boot-debug.log
            -global isa-debugcon.iobase=0xe9
        )
    fi

    qemu-system-x86_64 "${BIOS_QEMU_ARGS[@]}"
    exit 0
fi

OVMF_PATH="/usr/share/ovmf/OVMF.fd"
if [ ! -f "$OVMF_PATH" ]; then OVMF_PATH="/usr/share/OVMF/OVMF_CODE.fd"; fi

QEMU_ARGS=(
    -bios "$OVMF_PATH"
    -cdrom vtx2.iso
    -drive id=vtx2disk,if=none,format=raw,file="$VTX2_DISK_IMG"
    -device ahci,id=ahci0
    -device ide-hd,drive=vtx2disk,bus=ahci0.0
    -device qemu-xhci,id=xhci
    -boot order=d,menu=off
)

if [ "${ATTACH_USB:-0}" = "1" ]; then
    echo "    -> ATTACH_USB=1: USB Mass Storage cihazi xhci.0'a baglaniyor (usb_disk.img)"
    QEMU_ARGS+=(
        -drive if=none,id=usbstick,format=raw,file="$USB_DISK_IMG"
        -device usb-storage,bus=xhci.0,drive=usbstick
    )
fi

if [ "${QEMU_DEBUG:-0}" = "1" ]; then
    echo "    -> QEMU_DEBUG=1: yeniden baslatma durduruluyor; loglar: qemu-debug.log, boot-debug.log"
    rm -f qemu-debug.log boot-debug.log
    QEMU_ARGS+=(
        -no-reboot
        -d guest_errors,cpu_reset
        -D qemu-debug.log
        -debugcon file:boot-debug.log
        -global isa-debugcon.iobase=0xe9
    )
fi

qemu-system-x86_64 "${QEMU_ARGS[@]}"