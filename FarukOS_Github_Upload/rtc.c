#include "rtc.h"
#include "io.h"
#include <stdint.h>

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static int get_update_in_progress_flag(void) {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t get_RTC_register(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

void rtc_get_time(int* hours, int* minutes, int* seconds) {
    uint8_t s, m, h;
    
    // RTC guncellemesini bekle
    while (get_update_in_progress_flag());
    
    s = get_RTC_register(0x00);
    m = get_RTC_register(0x02);
    h = get_RTC_register(0x04);
    
    uint8_t registerB = get_RTC_register(0x0B);
    
    // BCD'den Binary'e donustur
    if (!(registerB & 0x04)) {
        s = (s & 0x0F) + ((s / 16) * 10);
        m = (m & 0x0F) + ((m / 16) * 10);
        h = ( (h & 0x0F) + (((h & 0x70) / 16) * 10) ) | (h & 0x80);
    }
    
    // 12-saat formatindaysa 24-saate cevir
    if (!(registerB & 0x02) && (h & 0x80)) {
        h = ((h & 0x7F) + 12) % 24;
    }
    
    // UTC zamani geldiginden Turkiye saati (UTC+3) ofseti ekle
    h = (h + 3) % 24;
    
    *seconds = s;
    *minutes = m;
    *hours = h;
}
