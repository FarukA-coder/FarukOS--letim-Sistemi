global gdt_flush

gdt_flush:
    mov eax, [esp+4]  ; Get pointer to GDT structure
    lgdt [eax]        ; Load the new GDT

    mov ax, 0x10      ; Offset 0x10 is our Data Segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush   ; Far jump to our Code Segment (0x08)
.flush:
    ret
