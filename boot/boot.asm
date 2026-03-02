; LIXOS Temel BIOS Bootloader
bits 16
org 0x7c00

start:
    ; Segmentleri sıfırla
    xor ax, ax
    mov ds, ax
    mov es, ax
    
    ; Ekrana mesaj yazdır
    mov si, boot_msg
    call print_string

    ; Sonsuz döngü ve durdurma
    cli
halt:
    hlt
    jmp halt

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print_string
.done:
    ret

boot_msg db "LIXOS BIOS uzerinden baslatildi!", 13, 10, 0

; Boot sektörünü 512 bayta tamamla
times 510-($-$$) db 0
dw 0xAA55