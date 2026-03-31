#include "mouse.h"
#include "io.h"
#include "pic.h"
#include <stdint.h>

volatile int32_t mouse_x = 400; // Varsayılan merkez X
volatile int32_t mouse_y = 300; // Varsayılan merkez Y
volatile uint8_t mouse_left_click = 0;
volatile uint8_t mouse_right_click = 0;
uint8_t mouse_cycle = 0;
uint8_t mouse_bytes[3];

static void mouse_wait(uint8_t a_type) {
    uint32_t timeout = 100000;
    if (a_type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    } else {
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    }
}

static void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, write);
}

static uint8_t mouse_read(void) {
    mouse_wait(0); 
    return inb(0x60);
}

void init_mouse(void) {
    uint8_t status;
    
    // Enable auxiliary mouse device
    mouse_wait(1);
    outb(0x64, 0xA8);
    
    // Enable interrupts
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    status = (inb(0x60) | 2); // Enable IRQ12
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);
    
    // Use default settings
    mouse_write(0xF6);
    mouse_read(); // ACK
    
    // Enable data reporting
    mouse_write(0xF4);
    mouse_read(); // ACK
}

void mouse_handler(void) {
    uint8_t status = inb(0x64);
    // Is it really the mouse?
    if (!(status & 0x20)) {
        pic_send_eoi(12);
        return;
    }

    // Process all available bytes
    while (status & 0x01) { 
        uint8_t data = inb(0x60);
        
        if (status & 0x20) { // From AUX device
            // Synchronization check: Byte 0 MUST have bit 3 set to 1
            if (mouse_cycle == 0 && !(data & 0x08)) {
                // Out of sync! Discard this byte
                status = inb(0x64);
                continue;
            }
            
            mouse_bytes[mouse_cycle++] = data;
            
            if (mouse_cycle == 3) {
                mouse_cycle = 0;
                
                // Update click states
                mouse_left_click = (mouse_bytes[0] & 0x01) ? true : false;
                mouse_right_click = (mouse_bytes[0] & 0x02) ? true : false;
                
                // Calculate movement
                int32_t d_x = mouse_bytes[1];
                int32_t d_y = mouse_bytes[2];

                if (mouse_bytes[0] & 0x10) d_x |= 0xFFFFFF00; // Sign extend X
                if (mouse_bytes[0] & 0x20) d_y |= 0xFFFFFF00; // Sign extend Y
                
                mouse_x += d_x;
                mouse_y -= d_y; // Y axis is inverted
                
                // Screen boundaries
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x > 1023) mouse_x = 1023;
                if (mouse_y > 767) mouse_y = 767;
            }
        }
        status = inb(0x64); 
    }
    
    pic_send_eoi(12);
}
