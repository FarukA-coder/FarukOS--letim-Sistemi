#include "pic.h"
#include "io.h"

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20); // Send EOI to Slave
    }
    outb(PIC1_CMD, 0x20);     // Send EOI to Master
}

void pic_remap(int offset1, int offset2) {
    uint8_t a1 = inb(PIC1_DATA);                        // save masks
    uint8_t a2 = inb(PIC2_DATA);
    
    outb(PIC1_CMD, 0x11);  // starts the initialization sequence (in cascade mode)
    outb(PIC2_CMD, 0x11);
    
    outb(PIC1_DATA, offset1);                 // ICW2: Master PIC vector offset
    outb(PIC2_DATA, offset2);                 // ICW2: Slave PIC vector offset
    
    outb(PIC1_DATA, 4);                       // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    outb(PIC2_DATA, 2);                       // ICW3: tell Slave PIC its cascade identity (0000 0010)
    
    outb(PIC1_DATA, 0x01);                    // ICW4: have the PICs use 8086 mode (and not 8080 mode)
    outb(PIC2_DATA, 0x01);
    
    outb(PIC1_DATA, a1);   // restore saved masks.
    outb(PIC2_DATA, a2);
}
