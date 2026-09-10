BITS 16
ORG 0x8000

%macro DBG 1
    push ax
    mov al, %1
    out 0xE9, al
    pop ax
%endmacro

MAX_E820_ENTRIES equ 128
MAX_BACKBUFFER_PIXELS equ (1920*1200)

stage2_start:
    DBG "4"
    mov [boot_drive], dl

    mov si, msg_stage2
    call print_str

    call enable_a20
    jc .a20_fail

    DBG "5"
    mov si, msg_a20_ok
    call print_str

    call collect_e820
    jc .e820_fail

    DBG "6"
    mov si, msg_e820_ok
    call print_str

    call find_rsdp
    jc .rsdp_fail

    DBG "7"
    mov si, msg_rsdp_ok
    call print_str

.rsdp_done:
    call setup_vbe
    jc .vbe_fail

    DBG "8"
    mov si, msg_vbe_ok
    call print_str

    call load_kernel_low
    jc .kernel_fail

    DBG "9"
    mov si, msg_kernel_ok
    call print_str

    call enter_protected_mode
    jmp halt

.a20_fail:
    DBG "X"
    mov si, msg_a20_fail
    call print_str
    jmp halt

.e820_fail:
    DBG "M"
    mov si, msg_e820_fail
    call print_str
    jmp halt

.rsdp_fail:
    DBG "R"
    mov si, msg_rsdp_fail
    call print_str

    mov dword [rsdp_addr], 0
    mov dword [rsdp_addr+4], 0
    jmp .rsdp_done

.vbe_fail:
    DBG "V"
    mov si, msg_vbe_fail
    call print_str
    jmp halt

.kernel_fail:
    DBG "K"
    mov si, msg_kernel_fail
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

enable_a20:
    mov ax, 0x2401
    int 0x15
    jnc .done

    in al, 0x92
    test al, 0x02
    jnz .done
    or al, 0x02
    and al, 0xFE
    out 0x92, al

    call test_a20
    jc .fail

.done:
    clc
    ret
.fail:
    stc
    ret

test_a20:
    push ax
    push bx
    push es
    push ds
    push di
    push si

    xor ax, ax
    mov es, ax
    mov di, 0x0500

    mov ax, 0xFFFF
    mov ds, ax
    mov si, 0x0510

    mov al, [es:di]
    push ax
    mov al, [ds:si]
    push ax

    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF

    cmp byte [es:di], 0xFF

    pop ax
    mov [ds:si], al
    pop ax
    mov [es:di], al

    pop si
    pop di
    pop ds
    pop es
    pop bx
    pop ax

    je .closed
    clc
    ret
.closed:
    stc
    ret

collect_e820:
    pusha
    push es
    push ds
    xor ax, ax
    mov es, ax
    mov ds, ax

    xor ebx, ebx
    mov edi, e820_raw_buf
    xor si, si

.loop:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    jc .maybe_done

    cmp eax, 0x534D4150
    jne .fail

    cmp cx, 20
    jb .skip_entry

    inc si
    add edi, 24
    cmp si, MAX_E820_ENTRIES
    jae .convert

.check_more:
    test ebx, ebx
    jz .convert
    jmp .loop

.skip_entry:
    test ebx, ebx
    jz .convert
    jmp .loop

.maybe_done:
    test si, si
    jz .fail

.convert:
    call e820_to_efi
    pop ds
    pop es
    clc
    popa
    ret

.fail:
    pop ds
    pop es
    stc
    popa
    ret

KERNEL_RESERVED_START equ 0x00100000
KERNEL_RESERVED_END equ 0x00E00000

e820_to_efi:
    pusha
    mov cx, si
    mov word [mem_map_entry_count], 0
    mov si, e820_raw_buf
    mov di, mem_map_buf

.conv_loop:
    test cx, cx
    jz .conv_done

    mov eax, [si+16]
    cmp eax, 1
    je .usable_check_split
    mov dword [di], 0
    jmp .write_single

.usable_check_split:
    mov eax, [si+0]
    mov edx, [si+8]
    add edx, eax
    jc .usable_no_split

    cmp eax, KERNEL_RESERVED_END
    jae .usable_no_split
    cmp edx, KERNEL_RESERVED_START
    jbe .usable_no_split

    call emit_split_usable
    jmp .conv_next_no_single

.usable_no_split:
    mov dword [di], 7

.write_single:
    call write_efi_entry_from_e820
    add si, 24
    add di, 40
    inc word [mem_map_entry_count]

.conv_next_no_single:
    dec cx
    jmp .conv_loop

.conv_done:
    popa
    ret

write_efi_entry_from_e820:
    pusha
    mov dword [di+4], 0

    mov eax, [si+0]
    mov [di+8], eax
    mov eax, [si+4]
    mov [di+12], eax

    mov dword [di+16], 0
    mov dword [di+20], 0

    mov eax, [si+8]
    mov edx, [si+12]
    shrd eax, edx, 12
    shr edx, 12
    mov [di+24], eax
    mov [di+28], edx

    mov dword [di+32], 0
    mov dword [di+36], 0
    popa
    ret

emit_split_usable:
    push eax
    push ebx
    push ecx
    push edx
    push esi

    mov ebx, eax

    cmp ebx, KERNEL_RESERVED_START
    jae .skip_part1
    mov eax, KERNEL_RESERVED_START
    cmp edx, eax
    jae .part1_full_range
    mov eax, edx
.part1_full_range:
    mov dword [di], 7
    mov dword [di+4], 0
    mov [di+8], ebx
    mov dword [di+12], 0
    mov dword [di+16], 0
    mov dword [di+20], 0
    push eax
    sub eax, ebx
    shr eax, 12
    mov [di+24], eax
    mov dword [di+28], 0
    mov dword [di+32], 0
    mov dword [di+36], 0
    pop eax
    add di, 40
    inc word [mem_map_entry_count]
.skip_part1:
    mov eax, ebx
    cmp eax, KERNEL_RESERVED_START
    jae .p2_start_ok
    mov eax, KERNEL_RESERVED_START
.p2_start_ok:
    mov ecx, edx
    cmp ecx, KERNEL_RESERVED_END
    jbe .p2_end_ok
    mov ecx, KERNEL_RESERVED_END
.p2_end_ok:
    cmp eax, ecx
    jae .skip_part2
    mov dword [di], 0
    mov dword [di+4], 0
    mov [di+8], eax
    mov dword [di+12], 0
    mov dword [di+16], 0
    mov dword [di+20], 0
    push eax
    mov eax, ecx
    sub eax, [di+8]
    shr eax, 12
    mov [di+24], eax
    mov dword [di+28], 0
    mov dword [di+32], 0
    mov dword [di+36], 0
    pop eax
    add di, 40
    inc word [mem_map_entry_count]
.skip_part2:
    mov eax, ebx
    cmp eax, KERNEL_RESERVED_END
    jae .p3_start_ok
    mov eax, KERNEL_RESERVED_END
.p3_start_ok:
    cmp eax, edx
    jae .skip_part3
    mov dword [di], 7
    mov dword [di+4], 0
    mov [di+8], eax
    mov dword [di+12], 0
    mov dword [di+16], 0
    mov dword [di+20], 0
    push eax
    mov eax, edx
    sub eax, [di+8]
    shr eax, 12
    mov [di+24], eax
    mov dword [di+28], 0
    mov dword [di+32], 0
    mov dword [di+36], 0
    pop eax
    add di, 40
    inc word [mem_map_entry_count]
.skip_part3:

    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

find_rsdp:
    pusha
    push es

    xor ax, ax
    mov es, ax
    mov ax, [es:0x040E]
    test ax, ax
    jz .try_bios_area

    mov es, ax
    mov cx, 64
    call scan_for_rsdp
    jnc .found

.try_bios_area:
    mov ax, 0xE000
    mov es, ax
    mov cx, 8192
    call scan_for_rsdp
    jnc .found

    pop es
    popa
    stc
    ret

.found:
    mov ax, es
    movzx eax, ax
    shl eax, 4
    movzx ecx, di
    add eax, ecx
    mov [rsdp_addr], eax
    mov dword [rsdp_addr+4], 0

    pop es
    popa
    clc
    ret

scan_for_rsdp:
    xor di, di
.scan_loop:
    push si
    mov si, rsdp_sig
    mov dx, 8
.sig_cmp:
    mov al, [es:di]
    cmp al, [si]
    jne .sig_no_match
    inc di
    inc si
    dec dx
    jnz .sig_cmp

    sub di, 8
    call checksum_20
    jnc .found_ret
    jmp .next

.sig_no_match:
    xor di, di

.next:
    pop si
    mov ax, es
    inc ax
    mov es, ax
    dec cx
    jnz .scan_loop
    stc
    ret

.found_ret:
    pop si
    clc
    ret

checksum_20:
    push ax
    push bx
    push cx
    xor al, al
    xor bx, bx
.loop:
    add al, [es:di+bx]
    inc bx
    cmp bx, 20
    jb .loop
    pop cx
    pop bx
    test al, al
    jnz .bad
    pop ax
    clc
    ret
.bad:
    pop ax
    stc
    ret

rsdp_sig: db "RSD PTR "

setup_vbe:
    pusha
    push es
    push ds

    xor ax, ax
    mov es, ax
    mov ds, ax

    mov di, vbe_info_buf
    mov dword [di], 'VBE2'
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov ax, [vbe_info_buf+16]
    mov [mode_list_seg], ax
    mov ax, [vbe_info_buf+14]
    mov [mode_list_off], ax

    mov word [best_width], 0
    mov word [best_height], 0
    mov word [best_mode], 0xFFFF

    mov si, [mode_list_off]
    mov ax, [mode_list_seg]
    mov fs, ax

.mode_loop:
    mov ax, [fs:si]
    cmp ax, 0xFFFF
    je .list_done
    add si, 2

    push si
    push ax

    mov di, vbe_mode_buf
    mov cx, ax
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .skip_mode

    mov ax, [vbe_mode_buf+0]
    test ax, 0x80
    jz .skip_mode

    mov al, [vbe_mode_buf+27]
    cmp al, 6
    jne .skip_mode

    mov al, [vbe_mode_buf+25]
    cmp al, 32
    jne .skip_mode

    mov ax, [vbe_mode_buf+18]
    mov bx, [vbe_mode_buf+20]

    movzx eax, ax
    movzx ebx, bx
    imul eax, ebx
    cmp eax, MAX_BACKBUFFER_PIXELS
    ja .skip_mode

    mov ax, [vbe_mode_buf+18]
    mov bx, [vbe_mode_buf+20]
    cmp ax, [best_width]
    jb .skip_mode
    ja .take_mode
    cmp bx, [best_height]
    jbe .skip_mode

.take_mode:
    mov [best_width], ax
    mov [best_height], bx
    pop ax
    push ax
    mov [best_mode], ax

    push si
    push cx
    mov si, vbe_mode_buf
    mov di, best_mode_info_buf
    mov cx, 256
    rep movsb
    pop cx
    pop si

.skip_mode:
    pop ax
    pop si
    jmp .mode_loop

.list_done:
    cmp word [best_mode], 0xFFFF
    je .fail

    mov ax, [best_mode]
    or ax, 0x4000
    mov bx, ax
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov di, best_mode_info_buf

    mov eax, [di+40]
    mov [fb_addr], eax
    mov dword [fb_addr+4], 0

    movzx eax, word [di+18]
    mov [fb_width], eax
    movzx eax, word [di+20]
    mov [fb_height], eax

    movzx eax, word [di+16]
    xor edx, edx
    mov ecx, 4
    div ecx
    mov [fb_ppsl], eax

    mov byte [fb_bpp], 32
    mov byte [fb_bytes_per_pixel], 4

    mov al, [di+32]
    cmp al, 16
    jne .set_rgb
    mov byte [fb_is_bgr], 1
    jmp .fb_done
.set_rgb:
    mov byte [fb_is_bgr], 0
.fb_done:

    pop ds
    pop es
    clc
    popa
    ret

.fail:
    pop ds
    pop es
    stc
    popa
    ret

KERNEL_START_LBA equ 97
KERNEL_MAX_SECTORS equ 256
KERNEL_LOW_SEG equ 0x1000
CHUNK_SECTORS equ 64

load_kernel_low:
    pusha

    mov word [kload_remaining], KERNEL_MAX_SECTORS
    mov dword [kload_lba], KERNEL_START_LBA
    mov word [kload_seg], KERNEL_LOW_SEG

.chunk_loop:
    cmp word [kload_remaining], 0
    je .done

    mov ax, CHUNK_SECTORS
    cmp ax, [kload_remaining]
    jbe .size_ok
    mov ax, [kload_remaining]
.size_ok:
    mov [kernel_dap_count], ax

    mov ax, [kload_seg]
    mov [kernel_dap_seg], ax
    mov eax, [kload_lba]
    mov [kernel_dap_lba], eax
    mov dword [kernel_dap_lba+4], 0

    mov si, kernel_dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .fail

    movzx eax, word [kernel_dap_count]
    add [kload_lba], eax
    mov cx, ax
    shl cx, 5
    add [kload_seg], cx

    movzx eax, word [kernel_dap_count]
    sub [kload_remaining], ax

    jmp .chunk_loop

.done:
    popa
    clc
    ret

.fail:
    popa
    stc
    ret

kload_remaining: dw 0
kload_lba: dd 0
kload_seg: dw 0

kernel_dap:
    db 0x10
    db 0
kernel_dap_count: dw 0
    dw 0x0000
kernel_dap_seg: dw 0
kernel_dap_lba: dq 0

enter_protected_mode:
    cli
    lgdt [gdt32_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE32_SEL:pm_start

BITS 32
pm_start:
    mov ax, DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov esi, 0x00010000
    mov edi, 0x00100000
    mov ecx, (KERNEL_MAX_SECTORS * 512) / 4
    cld
    rep movsd

    call setup_paging_temp

    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    mov eax, pml4_table
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    jmp CODE64_SEL:lm_start

setup_paging_temp:
    mov edi, pml4_table
    xor eax, eax
    mov ecx, (4096*3)/4
    rep stosd

    mov edi, pml4_table
    mov eax, pdpt_table
    or eax, 0x03
    mov [edi], eax

    mov edi, pdpt_table
    mov eax, pd_table
    or eax, 0x03
    mov [edi], eax

    mov edi, pd_table
    mov eax, 0x00000083
    mov ecx, 512
.pd_loop:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .pd_loop

    ret

align 4096
pml4_table: times 4096 db 0
align 4096
pdpt_table: times 4096 db 0
align 4096
pd_table: times 4096 db 0

align 8
gdt32_start:
    dq 0x0000000000000000

gdt32_code32:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00

gdt32_data32:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00

gdt32_code64:
    dw 0x0000, 0x0000
    db 0x00, 10011010b, 00100000b, 0x00

gdt32_end:

CODE32_SEL equ gdt32_code32 - gdt32_start
DATA32_SEL equ gdt32_data32 - gdt32_start
CODE64_SEL equ gdt32_code64 - gdt32_start

gdt32_descriptor:
    dw gdt32_end - gdt32_start - 1
    dd gdt32_start

BITS 64
lm_start:
    mov ax, DATA32_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000

    mov rdi, BOOTINFO_ADDR

    mov eax, [fb_addr]
    mov dword [rdi+0], eax
    mov dword [rdi+4], 0

    mov eax, [fb_width]
    mov [rdi+8], eax
    mov eax, [fb_height]
    mov [rdi+12], eax
    mov eax, [fb_ppsl]
    mov [rdi+16], eax

    mov al, [fb_bpp]
    mov [rdi+20], al
    mov al, [fb_bytes_per_pixel]
    mov [rdi+21], al
    mov al, [fb_is_bgr]
    mov [rdi+22], al

    mov rax, [rsdp_addr]
    mov [rdi+24], rax

    mov rax, mem_map_buf
    mov [rdi+32], rax

    movzx eax, word [mem_map_entry_count]
    mov ebx, 40
    mul ebx
    mov [rdi+40], rax

    mov qword [rdi+48], 40

    mov rdi, BOOTINFO_ADDR
    mov rax, KERNEL_ENTRY
    jmp rax

BOOTINFO_ADDR equ 0x00098000
KERNEL_ENTRY equ 0x00100000

BITS 16

boot_drive: db 0
mem_map_entry_count: dw 0
rsdp_addr: dq 0

mode_list_seg: dw 0
mode_list_off: dw 0
best_width: dw 0
best_height: dw 0
best_mode: dw 0xFFFF

fb_addr: dq 0
fb_width: dd 0
fb_height: dd 0
fb_ppsl: dd 0
fb_bpp: db 0
fb_bytes_per_pixel: db 0
fb_is_bgr: db 0

msg_stage2: db "Stage2: real mode, A20 deneniyor...", 13, 10, 0
msg_a20_ok: db "A20 acik.", 13, 10, 0
msg_a20_fail: db "ERR: A20 acilamadi", 13, 10, 0
msg_e820_ok: db "E820 memory map toplandi.", 13, 10, 0
msg_e820_fail: db "ERR: E820 basarisiz", 13, 10, 0
msg_rsdp_ok: db "RSDP bulundu.", 13, 10, 0
msg_rsdp_fail: db "UYARI: RSDP bulunamadi, ACPI'siz devam.", 13, 10, 0
msg_vbe_ok: db "VBE framebuffer ayarlandi.", 13, 10, 0
msg_vbe_fail: db "ERR: uygun VBE LFB modu bulunamadi", 13, 10, 0
msg_kernel_ok: db "Kernel diskten okundu.", 13, 10, 0
msg_kernel_fail: db "ERR: kernel okuma basarisiz", 13, 10, 0

align 16
e820_raw_buf:
    times (MAX_E820_ENTRIES * 24) db 0

align 16
mem_map_buf:
    times (MAX_E820_ENTRIES * 40) db 0

align 16
vbe_info_buf:
    times 512 db 0

align 16
vbe_mode_buf:
    times 256 db 0

align 16
best_mode_info_buf:
    times 256 db 0

times 49152 - ($ - $$) db 0