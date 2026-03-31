global idt_flush
idt_flush:
    mov eax, [esp+4]
    lidt [eax]
    sti             ; ENABLE HARDWARE INTERRUPTS
    ret

global irq1_handler_asm
extern keyboard_handler

irq1_handler_asm:
    pusha
    call keyboard_handler
    popa
    iretd

global irq12_handler_asm
extern mouse_handler

irq12_handler_asm:
    pusha
    call mouse_handler
    popa
    iretd

extern isr_handler

global isr0
isr0:
    cli
    push byte 0 ; fake err code
    push byte 0 ; int_no
    jmp isr_common_stub

global isr14
isr14:
    cli
    push byte 14 ; CPU pushes err code automatically
    jmp isr_common_stub

isr_common_stub:
    pusha
    mov ax, ds
    push eax
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp
    call isr_handler
    
    ; BSOD halts, but just in case
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8
    sti
    iretd
