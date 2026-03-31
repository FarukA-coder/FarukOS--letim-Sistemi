#include "sb16.h"
#include "io.h"

#define DSP_RESET 0x226
#define DSP_READ  0x22A
#define DSP_WRITE 0x22C
#define DSP_STATUS 0x22E

// Allocate enough space to guarantee we can find a 64K aligned chunk
uint8_t dma_buffer_raw[65536 * 2];
uint8_t* dma_buffer;

static void dsp_write(uint8_t val) {
    while ((inb(DSP_WRITE) & 0x80) != 0);
    outb(DSP_WRITE, val);
}

static uint8_t dsp_read(void) {
    while ((inb(DSP_STATUS) & 0x80) == 0);
    return inb(DSP_READ);
}

static void sb16_mixer_write(uint8_t reg, uint8_t val) {
    outb(0x224, reg);
    outb(0x225, val);
}

void init_sb16(void) {
    uint32_t raw_addr = (uint32_t)(uintptr_t) dma_buffer_raw;
    uint32_t aligned_addr = (raw_addr + 65535) & ~65535;
    dma_buffer = (uint8_t*)(uintptr_t)aligned_addr;

    outb(DSP_RESET, 1);
    for(volatile int i=0; i<10000; i++); // Short delay
    outb(DSP_RESET, 0);
    
    // Wait for ready
    for(volatile int i=0; i<100000; i++) {
        if (inb(DSP_STATUS) & 0x80) {
            uint8_t status = inb(DSP_READ);
            if (status == 0xAA) {
                // Found
                dsp_write(0xD1); // Turn on speaker
                
                // Max out volumes
                sb16_mixer_write(0x22, 0xFF); // Master
                sb16_mixer_write(0x04, 0xFF); // Voice
                sb16_mixer_write(0x26, 0xFF); // MIDI
                return;
            }
        }
    }
}

void sb16_play_buffer(uint8_t* buffer, uint32_t len) {
    if (len > 65535) len = 65535;
    for (uint32_t i = 0; i < len; i++) {
        dma_buffer[i] = buffer[i];
    }
    
    // Physical address
    uint32_t phys = (uint32_t)(uintptr_t) dma_buffer;
    
    // Setup DMA channel 1
    outb(0x0A, 0x05); // mask ch 1
    outb(0x0C, 0x00); // clear flip flop
    outb(0x0B, 0x49); // single transfer, read, channel 1
    outb(0x83, (phys >> 16) & 0xFF);
    outb(0x02, phys & 0xFF);
    outb(0x02, (phys >> 8) & 0xFF);
    outb(0x03, (len - 1) & 0xFF);
    outb(0x03, ((len - 1) >> 8) & 0xFF);
    outb(0x0A, 0x01); // unmask ch 1
    
    // Set Sample Rate (11.025k)
    dsp_write(0x41);
    dsp_write(11025 >> 8);
    dsp_write(11025 & 0xFF);
    
    // Play 8-bit mono
    dsp_write(0xC0);
    dsp_write(0x00);
    dsp_write((len - 1) & 0xFF);
    dsp_write(((len - 1) >> 8) & 0xFF);
}

void sb16_play_test_tone(void) {
    uint8_t tone_data[11025]; // 1 second
    int freq = 440; // A4
    int period = 11025 / freq;
    for (int i = 0; i < 11025; i++) {
        tone_data[i] = (i % period < period / 2) ? 160 : 96;
    }
    sb16_play_buffer(tone_data, 11025);
}

void sb16_play_startup_sound(void) {
    uint8_t startup_data[4000];
    for (int i = 0; i < 4000; i++) {
        int freq = (i < 1000) ? 261 : (i < 2000) ? 329 : (i < 3000) ? 392 : 523;
        int period = 11025 / freq;
        startup_data[i] = (i % period < period/2) ? 160 : 96;
    }
    sb16_play_buffer(startup_data, 4000);
}
void sb16_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    // Map 0-100 to 0-15 (4 bits)
    uint8_t vol = (uint8_t)((percent * 15) / 100);
    // Register 0x22 is Master Volume (Left: 4 bits, Right: 4 bits)
    outb(0x224, 0x22);
    outb(0x225, (vol << 4) | vol);
}
