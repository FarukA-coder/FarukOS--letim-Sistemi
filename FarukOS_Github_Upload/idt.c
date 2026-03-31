#include "idt.h"
#include "pic.h"
#include "io.h"

struct idt_entry_struct {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry_struct idt_entries[256];
struct idt_ptr_struct   idt_ptr;

extern void idt_flush(uint32_t);
extern void irq1_handler_asm(void);
extern void irq12_handler_asm(void);

struct registers {
    uint32_t ds; 
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; 
    uint32_t int_no, err_code; 
    uint32_t eip, cs, eflags, useresp, ss; 
};

extern void draw_bsod(const char* error, uint32_t eip);

void isr_handler(struct registers* regs) {
    char* msgs[] = {
        "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
        "Into Detected Overflow", "Out of Bounds", "Invalid Opcode",
        "No Coprocessor", "Double Fault", "Coprocessor Segment Overrun",
        "Bad TSS", "Segment Not Present", "Stack Fault",
        "General Protection Fault", "Page Fault", "Unknown Interrupt",
        "Coprocessor Fault", "Alignment Check", "Machine Check"
    };

    if (regs->int_no < 19) draw_bsod(msgs[regs->int_no], regs->eip);
    else draw_bsod("Reserved Exception", regs->eip);
    
    while(1) { __asm__ volatile("cli; hlt"); }
}

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_lo = base & 0xFFFF;
    idt_entries[num].base_hi = (base >> 16) & 0xFFFF;
    idt_entries[num].sel     = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags   = flags /* | 0x60 */;
}

void init_idt(void) {
    idt_ptr.limit = sizeof(struct idt_entry_struct) * 256 - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    // Clear out the entire IDT, initializing it to zeros
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }
    
    // Remap the PIC so IRQ0-15 mapped to IDT 32-47
    pic_remap(32, 40);

    // Unmask IRQ1 (Keyboard) AND IRQ2 (Cascade for Slave PIC) on Master PIC
    outb(PIC1_DATA, 0xF9); // 1111 1001 (bit 1 and 2 are 0)
    
    // Unmask IRQ12 (Mouse) on Slave PIC, mask others
    outb(PIC2_DATA, 0xEF); // 1110 1111 (bit 4 is 0 for IRQ12)

    // Register exceptions
    extern void isr0(void);
    extern void isr14(void);
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);

    // Register Keyboard handler at IDT index 33 (32 + 1)
    idt_set_gate(33, (uint32_t)irq1_handler_asm, 0x08, 0x8E);
    
    // Register Mouse handler at IDT index 44 (32 + 12)
    idt_set_gate(44, (uint32_t)irq12_handler_asm, 0x08, 0x8E);
    
    idt_flush((uint32_t)&idt_ptr);
}
