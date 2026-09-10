BITS 16
ORG 0x7C00

STAGE2_LOAD_SEG equ 0x0000
STAGE2_LOAD_OFF equ 0x8000
STAGE2_START_LBA equ 1
STAGE2_SECTORS equ 96

%macro DBG 1
    push ax
    mov al, %1
    out 0xE9, al
    pop ax
%endmacro

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    DBG "1"
    mov si, msg_start
    call print_str

    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .no_lba
    cmp bx, 0xAA55
    jne .no_lba

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    DBG "2"
    mov si, msg_ok
    call print_str

    mov dl, [boot_drive]
    DBG "3"
    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

.no_lba:
    DBG "L"
    mov si, msg_no_lba
    call print_str
    jmp halt

disk_error:
    DBG "E"
    mov si, msg_disk_err
    call print_str
    jmp halt

halt:
    cli
    hlt
    jmp halt

print_str:
    push ax
    push bx
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

boot_drive: db 0

dap:
    db 0x10
    db 0
    dw STAGE2_SECTORS
    dw STAGE2_LOAD_OFF
    dw STAGE2_LOAD_SEG
    dq STAGE2_START_LBA

msg_start: db "VTX2 MBR: booting...", 13, 10, 0
msg_ok: db "Stage2 loaded, jumping...", 13, 10, 0
msg_no_lba: db "ERR: no INT13h LBA extension", 13, 10, 0
msg_disk_err: db "ERR: disk read failed", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55