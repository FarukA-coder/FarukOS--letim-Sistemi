#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "mouse.h"
#include "keyboard.h"
#include "bga.h"
#include "font8x8_basic.h"
#include "rtc.h"
#include "io.h"
#include "ata.h"
#include "sb16.h"

#define TYPE_FOLDER 1
#define TYPE_FILE 2

bool icon_init[100] = {0};
int icon_x[100];
int icon_y[100];
bool is_icon_dragging = false;
int drag_icon_idx = -1;
int tick = 0;

typedef struct {
    char name[32];
    int type;
    int parent_id;
} VFSNode;

extern VFSNode vfs[15];
extern int vfs_count;

void draw_folder_icon(int x, int y, const char* name);
void draw_file_icon(int x, int y, const char* name);
void close_window(int order_index);

typedef struct {
    int folder_id; // -1 if this is an App window
    int app_id;    // -1 if this is a Folder window
    int x, y, w, h;
    bool maximized;
    bool minimized;
    int old_x, old_y, old_w, old_h;
} OSWindow;

OSWindow win_stack[20];
int win_count = 0;
int win_order[20];

uint32_t* fb = 0;
uint32_t fb_width = 1024;
uint32_t fb_pitch = 1024 * 4;
uint32_t backbuffer[1024 * 768];

// UI State
bool context_menu_open = false;
int ctx_x = 0, ctx_y = 0;
int ctx_target_idx = -1; // New: track which VFS node is right-clicked
bool volume_open = false;
int current_volume = 80;
int app_history[5] = {0}; // New: Alt-Tab app history
bool show_alt_tab = false;
bool needs_redraw = false;
bool just_clicked = false;

void swap_buffers(void) {
    for (int i = 0; i < 1024 * 768; i++) {
        fb[i] = backbuffer[i];
    }
}

void draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= 1024 || y >= 768) return;
    backbuffer[y * 1024 + x] = color;
}

void int_to_str(int n, char* str) {
    if (n == 0) { str[0] = '0'; str[1] = '\0'; return; }
    int i = 0, sign = n;
    if (sign < 0) n = -n;
    while (n > 0) { str[i++] = (n % 10) + '0'; n /= 10; }
    if (sign < 0) str[i++] = '-';
    str[i] = '\0';
    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char temp = str[j]; str[j] = str[k]; str[k] = temp;
    }
}

bool string_contains(const char* haystack, const char* needle) {
    if (!*needle) return true;
    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while(haystack[i+j] && needle[j]) {
            char c1 = haystack[i+j];
            char c2 = needle[j];
            if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
            if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
            if (c1 != c2) break;
            j++;
        }
        if (!needle[j]) return true;
    }
    return false;
}

void sound_play(uint32_t freq) {
    uint32_t Div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(Div));
    outb(0x42, (uint8_t)(Div >> 8));
    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) { outb(0x61, tmp | 3); }
}

void sound_stop() {
    uint8_t tmp = inb(0x61) & 0xFC;
    outb(0x61, tmp);
}

bool dark_mode_active = false;

uint32_t get_win_bg() { return dark_mode_active ? 0x00333333 : 0x00ECE9D8; }
uint32_t get_win_txt() { return dark_mode_active ? 0x00FFFFFF : 0x00000000; }
uint32_t get_win_title() { return dark_mode_active ? 0x00111111 : 0x000055EA; }

uint32_t wallpaper_buf[1024 * 768];

uint32_t isqrt(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

void generate_wallpaper() {
    for (int y = 0; y < 768; y++) {
        for (int x = 0; x < 1024; x++) {
            int r, g, b;
            if (dark_mode_active) {
                // Dark minimal background
                r = 15; g = 18; b = 25;
                if ((x+y)%40 == 0) { r+=5; g+=5; b+=10; } // subtle texture
            } else {
                // Windows 7 Aurora style base
                r = 10; g = 40 + (y * 40 / 768); b = 100 + (y * 100 / 768);
                
                // Sweep 1
                int d1 = (x * 2 - y * 3);
                if (d1 < 0) d1 = -d1;
                if (d1 < 400) {
                    int add = (400 - d1) / 4;
                    r += add; g += add * 1.5; b += add * 2;
                }
                
                // Sweep 2
                int d2 = (x * 3 + y * 2) - 3000;
                if (d2 < 0) d2 = -d2;
                if (d2 < 600) {
                    int add = (600 - d2) / 6;
                    r += add; g += add * 2; b += add * 2;
                }
            }
            
            // Central Faruk OS glowing orb
            int dx = x - 512;
            int dy = y - 384;
            uint32_t dist = isqrt((uint32_t)(dx*dx + dy*dy));
            if (dist < 300) {
                int add = (300 - dist) / (!dark_mode_active ? 4 : 8);
                r += add; g += add; b += add * 2;
            }
            // Inner core
            if (dist < 150) {
                int core = (150 - dist) / 2;
                r += core; g += core; b += core;
            }
            // Subtle Grid
            if (x % 120 == 0 || y % 120 == 0) {
                r += 10; g += 15; b += 25;
            }
            
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            wallpaper_buf[y*1024 + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}
// Taskbar State
int pinned_apps[10] = {3, 6, 7}; // Default: Mario.exe, Snake.exe, Task Manager (Mines.exe)
int num_pinned = 3;

void draw_background_patch(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            uint32_t px = x + col;
            uint32_t py = y + row;
            if (px >= 1024 || py >= 768) continue;
            draw_pixel(px, py, wallpaper_buf[py * 1024 + px]);
        }
    }
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            draw_pixel(x + col, y + row, color);
        }
    }
}

// cursor_type: 0=arrow, 1=hourglass/wait, 2=text I-beam
void draw_cursor(int x, int y, int cursor_type) {
    if (cursor_type == 1) {
        // Hourglass
        uint32_t c = 0x00FFD700;
        uint32_t b = 0x00000000;
        draw_rect(x, y,     12, 2,  c);
        draw_rect(x+1,y+2,  10, 2,  c);
        draw_rect(x+2,y+4,  8,  2,  c);
        draw_rect(x+3,y+6,  6,  2,  c);
        draw_rect(x+4,y+8,  4,  2,  c);
        draw_rect(x+5,y+10, 2,  2,  c);
        draw_rect(x+4,y+12, 4,  2,  c);
        draw_rect(x+3,y+14, 6,  2,  c);
        draw_rect(x+2,y+16, 8,  2,  c);
        draw_rect(x+1,y+18, 10, 2,  c);
        draw_rect(x, y+20,  12, 2,  c);
        (void)b;
    } else if (cursor_type == 2) {
        // Text I-beam
        uint32_t c = 0x00000000;
        draw_rect(x-3, y,   7, 1,  c);
        draw_rect(x,   y+1, 1, 14, c);
        draw_rect(x-3, y+15,7, 1,  c);
    } else {
        // Arrow cursor (pixel art)
        uint32_t W = 0x00FFFFFF, B = 0x00000000;
        // Column by column arrow shape
        draw_pixel(x,   y,    B); draw_pixel(x,   y+1,  B); draw_pixel(x,   y+2,  B);
        draw_pixel(x,   y+3,  B); draw_pixel(x,   y+4,  B); draw_pixel(x,   y+5,  B);
        draw_pixel(x,   y+6,  B); draw_pixel(x,   y+7,  B); draw_pixel(x,   y+8,  B);
        draw_pixel(x,   y+9,  B); draw_pixel(x,   y+10, B); draw_pixel(x,   y+11, B);
        draw_pixel(x+1, y+1,  W); draw_pixel(x+1, y+2,  W); draw_pixel(x+1, y+3,  W);
        draw_pixel(x+1, y+4,  W); draw_pixel(x+1, y+5,  W); draw_pixel(x+1, y+6,  W);
        draw_pixel(x+1, y+7,  W); draw_pixel(x+1, y+8,  W); draw_pixel(x+1, y+9,  W);
        draw_pixel(x+1, y+10, W); draw_pixel(x+1, y+1,  B);
        draw_pixel(x+2, y+2,  W); draw_pixel(x+2, y+3,  W); draw_pixel(x+2, y+4,  W);
        draw_pixel(x+2, y+5,  W); draw_pixel(x+2, y+6,  W); draw_pixel(x+2, y+7,  W);
        draw_pixel(x+2, y+8,  W); draw_pixel(x+2, y+9,  W); draw_pixel(x+2, y+2,  B);
        draw_pixel(x+3, y+3,  W); draw_pixel(x+3, y+4,  W); draw_pixel(x+3, y+5,  W);
        draw_pixel(x+3, y+6,  W); draw_pixel(x+3, y+7,  W); draw_pixel(x+3, y+8,  W);
        draw_pixel(x+3, y+3,  B);
        draw_pixel(x+4, y+4,  W); draw_pixel(x+4, y+5,  W); draw_pixel(x+4, y+6,  W);
        draw_pixel(x+4, y+7,  B); draw_pixel(x+4, y+8,  W); draw_pixel(x+4, y+4,  B);
        draw_pixel(x+5, y+5,  W); draw_pixel(x+5, y+6,  B); draw_pixel(x+5, y+7,  W);
        draw_pixel(x+5, y+8,  B); draw_pixel(x+5, y+5,  B);
        draw_pixel(x+6, y+6,  B); draw_pixel(x+6, y+7,  B); draw_pixel(x+6, y+8,  B);
        // Black outline at very left
        for(int i=0; i<=11; i++) draw_pixel(x, y+i, B);
        (void)W; (void)B;
    }
}

void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color) {
    if ((uint8_t)c > 127) return;
    char* bitmap = font8x8_basic[(uint8_t)c];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (bitmap[row] & (1 << col)) {
                draw_pixel(x + col, y + row, fg_color);
            } else if (bg_color != 0x00000000) {
                draw_pixel(x + col, y + row, bg_color);
            }
        }
    }
}

void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t fg_color, uint32_t bg_color) {
    uint32_t cx = x;
    uint32_t cy = y;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') { cy += 8; cx = x; continue; }
        draw_char(cx, cy, str[i], fg_color, bg_color);
        cx += 8;
    }
}

void draw_string_shadow(uint32_t x, uint32_t y, const char* str, uint32_t fg_color, uint32_t shadow_color) {
    if (shadow_color != 0x00000000) draw_string(x + 1, y + 1, str, shadow_color, 0);
    draw_string(x, y, str, fg_color, 0);
}

void draw_search_bar(void) {
    uint32_t bg = dark_mode_active ? 0x00222233 : 0x00FFFFFF;
    draw_rect(130, 725, 300, 24, bg);
    // Blue border when focused
    if (search_bar_focused) {
        draw_rect(129, 724, 302, 26, 0x000078D7); // Blue outline
        draw_rect(130, 725, 300, 24, bg);
    }
    if (search_len > 0) {
        draw_string(135, 730, search_buffer, get_win_txt(), bg);
    } else if (search_bar_focused) {
        draw_string(135, 730, "_", get_win_txt(), bg); // Cursor
    } else {
        draw_string(135, 730, "Search...", 0x00888888, bg);
    }
}

void draw_clock(int h, int m) {
    char time_str[6] = {0};
    time_str[0] = '0' + (h / 10);
    time_str[1] = '0' + (h % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (m / 10);
    time_str[4] = '0' + (m % 10);
    time_str[5] = '\0';
    draw_string(965, 730, time_str, 0x00FFFFFF, 0x00000000);
}

void draw_taskbar(int clock_h, int clock_m) {
    // Translucent Aero style base
    for (int y = 710; y < 768; y++) {
        uint32_t base = dark_mode_active ? 0x00101010 : 0x001A1A2E;
        if (y == 710 || y == 711) base = dark_mode_active ? 0x00444455 : 0x004466AA;
        else if (y <= 714) base = dark_mode_active ? 0x00222233 : 0x00224488;
        draw_rect(0, y, 1024, 1, base);
    }
    
    // Start Button (Windows 7 orb placeholder)
    draw_rect(20, 715, 80, 40, 0x001F7A4A);
    draw_rect(20, 715, 80, 2, 0x0035C474);     // highlight
    draw_string(40, 728, "Start", 0x00FFFFFF, 0x00000000);

    draw_search_bar();
    
    // Pinned Apps
    int px = 460;
    for (int i = 0; i < num_pinned; i++) {
        int vfs_idx = pinned_apps[i];
        
        // Render a subtle box behind the icon to indicate it is pinned
        draw_rect(px - 5, 715, 40, 46, 0x00224488);
        draw_rect(px - 4, 716, 38, 44, 0x00102040); // inner shadow

        if (vfs[vfs_idx].type == TYPE_FOLDER) draw_folder_icon(px, 720, "");
        else draw_file_icon(px, 720, "");
        
        px += 50;
    }
    
    // Clock and Tray
    draw_clock(clock_h, clock_m);
    
    // Volume Icon
    draw_rect(930, 732, 10, 8, 0x00DDDDDD);
    draw_rect(940, 728, 4, 16, 0x00DDDDDD);
    // Soundwaves
    draw_rect(948, 730, 2, 12, 0x00DDDDDD);
    draw_rect(952, 726, 2, 20, 0x00DDDDDD);
}

void draw_vol_mixer(void) {
    if (!volume_open) return;
    int mx = 920, my = 550, mw = 80, mh = 150;
    // Glass panel
    draw_rect(mx, my, mw, mh, dark_mode_active ? 0x00222233 : 0x00F0F0F0);
    draw_rect(mx, my, mw, 2, 0x00FFFFFF); // hint of aero shine
    
    // Slider track (darker)
    draw_rect(mx + 38, my + 20, 4, 110, 0x00222222);
    
    // Slider handle (based on current_volume)
    int h_y = my + 130 - (current_volume * 110 / 100);
    draw_rect(mx + 30, h_y - 5, 20, 10, 0x000078D7); // Windows blue
    
    char vol_str[8];
    int_to_str(current_volume, vol_str);
    draw_string(mx + 30, my + 5, vol_str, get_win_txt(), 0x00000000);
}

void draw_context_menu(void) {
    if (!context_menu_open) return;
    int mw = 120, mh = 45;
    draw_rect(ctx_x, ctx_y, mw, mh, dark_mode_active ? 0x001A1A2A : 0x00F0F0F0);
    draw_rect(ctx_x, ctx_y, mw, 1, 0x00CCCCCC);
    draw_rect(ctx_x, ctx_y + 22, mw, 1, 0x00CCCCCC);
    draw_rect(ctx_x + mw - 1, ctx_y, 1, mh, 0x00CCCCCC);
    draw_rect(ctx_x, ctx_y + mh - 1, mw, 1, 0x00CCCCCC);
    draw_rect(ctx_x, ctx_y, 1, mh, 0x00CCCCCC);
    
    extern volatile int32_t mouse_x, mouse_y;
    if (mouse_x >= ctx_x && mouse_x <= ctx_x + mw && mouse_y >= ctx_y && mouse_y <= ctx_y + 22)
        draw_rect(ctx_x + 2, ctx_y + 2, mw - 4, 18, 0x00A0C0FF);
    if (mouse_x >= ctx_x && mouse_x <= ctx_x + mw && mouse_y >= ctx_y + 23 && mouse_y <= ctx_y + 43)
        draw_rect(ctx_x + 2, ctx_y + 23, mw - 4, 18, 0x00A0C0FF);

    draw_string(ctx_x + 10, ctx_y + 6, "Yenile", dark_mode_active ? 0x00FFFFFF : 0x00000000, 0x00000000);
    draw_string(ctx_x + 10, ctx_y + 28, "Ayarlar", dark_mode_active ? 0x00FFFFFF : 0x00000000, 0x00000000);
    if (ctx_target_idx != -1) {
        draw_rect(ctx_x, ctx_y + 45, 120, 1, 0x00CCCCCC);
        draw_string(ctx_x + 10, ctx_y + 50, "Y. Adlandir", dark_mode_active ? 0x00FFFFFF : 0x00000000, 0x00000000);
    }
}

// Simple strcmp for password check
static int str_eq(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

// Draw stars for password field (hide chars)
static void draw_password_dots(int x, int y, int len) {
    for (int i = 0; i < len; i++) {
        draw_rect(x + i * 12, y + 4, 8, 8, 0x00000080);
    }
}

void draw_login_screen(bool wrong_pass) {
    extern volatile int32_t mouse_x, mouse_y;
    
    // --- Aurora Aero Background ---
    for (int y = 0; y < 768; y++) {
        for (int x = 0; x < 1024; x++) {
            int r = 10 + (y * 20 / 768);
            int g = 40 + (y * 30 / 768);
            int b = 100 + (y * 50 / 768);
            // Center Glow
            int dx = x - 512, dy = y - 384;
            uint32_t d = isqrt((uint32_t)(dx*dx + dy*dy));
            if (d < 500) {
                int add = (500 - d) / 6;
                r += add; g += add * 1.2; b += add * 1.5;
            }
            if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
            draw_pixel(x, y, ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
        }
    }

    // --- Floating Faruk OS Logo ---
    draw_string_shadow(460, 150, "Faruk OS", 0x00FFFFFF, 0x00000000);
    draw_string(450, 170, "Modern Kernel v1.0", 0x00AACCFF, 0x00000000);

    // --- Aero Glass Login Panel ---
    int px = 337, py = 234, pw = 350, ph = 300;
    // Glass drop shadow
    draw_rect(px + 4, py + 4, pw, ph, 0x00000040);
    // Panel Body (Translucent effect)
    for (int i = 0; i < ph; i++) {
        uint32_t c = 0x00E0EAF5;
        if (i < 20) c = 0x00F0F8FF; // top shine
        draw_rect(px, py + i, pw, 1, c);
    }
    // Panel borders
    draw_rect(px, py, pw, 1, 0x00FFFFFF);
    draw_rect(px, py + ph - 1, pw, 1, 0x00FFFFFF);
    draw_rect(px, py, 1, ph, 0x00FFFFFF);
    draw_rect(px + pw - 1, py, 1, ph, 0x00FFFFFF);

    // --- User Avatar (Modern) ---
    draw_rect(px + 135, py + 30, 80, 80, 0x00FFFFFF); // border
    draw_rect(px + 137, py + 32, 76, 76, 0x00215FA8); // background
    // Human sketch
    draw_rect(px + 155, py + 45, 40, 30, 0x00FFCC88); // face
    draw_rect(px + 155, py + 75, 40, 25, 0x00FFFFFF); // body
    draw_string_shadow(px + 125, py + 120, "Administrator", 0x00000000, 0x00AACCFF);

    // --- Password Input (with focus glow) ---
    draw_string(px + 70, py + 155, "Password:", 0x00666666, 0x00E0EAF5);
    // Outer Glow if mouse is near or field is targeted
    bool p_hover = (mouse_x >= px + 70 && mouse_x <= px + 270 && mouse_y >= py + 170 && mouse_y <= py + 200);
    if (p_hover) draw_rect(px + 68, py + 168, 204, 24, 0x0099CCFF); // glow
    draw_rect(px + 70, py + 170, 200, 20, 0x00FFFFFF); // inner white field
    draw_rect(px + 70, py + 170, 200, 1, 0x00888888); // top shadow
    
    // Typing feedback (dots)
    for (int i = 0; i < login_len; i++) {
        draw_rect(px + 76 + (i * 12), py + 176, 8, 8, 0x00333333);
    }
    if (login_len == 0) draw_string(px + 76, py + 176, "...", 0x00CCCCCC, 0x00FFFFFF);

    // --- Modern Submit Button (->) ---
    bool b_hover = (mouse_x >= px + 275 && mouse_x <= px + 305 && mouse_y >= py + 170 && mouse_y <= py + 190);
    uint32_t b_col = b_hover ? 0x003A9630 : 0x00215FA8;
    draw_rect(px + 275, py + 170, 30, 20, b_col);
    draw_string(px + 283, py + 176, "->", 0x00FFFFFF, 0x00000000);

    // --- Guest Link (Interaction) ---
    bool g_hover = (mouse_x >= px + 105 && mouse_x <= px + 245 && mouse_y >= py + 220 && mouse_y <= py + 240);
    draw_string(px + 110, py + 225, "Log in as Guest", g_hover ? 0x000055EA : 0x00224488, 0x00E0EAF5);

    // --- Error Bar ---
    if (wrong_pass) {
        draw_rect(px + 50, py + 260, 250, 20, 0x00E81123);
        draw_string(px + 65, py + 266, "Invalid password!", 0x00FFFFFF, 0x00E81123);
    }

    // --- Bottom Attribution ---
    draw_string_shadow(350, 720, "Microsoft Windows XP - FarukOS Edition", 0x00FFFFFF, 0x00000000);
}
void draw_bsod(const char* error, uint32_t eip) {
    draw_rect(0, 0, 1024, 768, 0x000000AA); // Classic BSOD Blue
    draw_rect(412, 100, 200, 30, 0x00FFFFFF);
    draw_string(432, 110, "Faruk OS", 0x000000AA, 0x00FFFFFF);
    draw_string(150, 200, "A fatal exception has occurred at the hardware level.", 0x00FFFFFF, 0x000000AA);
    draw_string(150, 230, "The system has been halted to prevent damage to your computer.", 0x00FFFFFF, 0x000000AA);
    draw_string(150, 280, "Error code:", 0x00FFFFFF, 0x000000AA);
    draw_string(300, 280, error, 0x00FFFFFF, 0x000000AA);
    char hex[16];
    char* hex_chars = "0123456789ABCDEF";
    hex[0] = '0'; hex[1] = 'x';
    for(int i=0; i<8; i++) hex[9-i] = hex_chars[(eip >> (i*4)) & 0xF];
    hex[10] = 0;
    draw_string(50, 310, "Instruction Pointer (EIP):", 0x00FFFFFF, 0x000000AA);
    draw_string(300, 310, hex, 0x00FFFFFF, 0x000000AA);
    draw_string(50, 450, "Press ANY KEY to restart your computer.", 0x00FFFFFF, 0x000000AA);
    draw_string(50, 480, "If this is the first time you've seen this stop error screen,", 0x00FFFFFF, 0x000000AA);
    draw_string(50, 500, "restart your computer. If this screen appears again, follow", 0x00FFFFFF, 0x000000AA);
    draw_string(50, 520, "these steps: Check to make sure any new hardware or software", 0x00FFFFFF, 0x000000AA);
    draw_string(50, 540, "is properly installed.", 0x00FFFFFF, 0x000000AA);
    swap_buffers();
}

void draw_task_manager(int x, int y) {
    draw_rect(x, y, 300, 200, get_win_bg());
    draw_rect(x, y, 300, 25, get_win_title());
    draw_string(x + 5, y + 8, "Task Manager", 0x00FFFFFF, 0x00000000);
    draw_rect(x + 275, y + 2, 21, 21, 0x00E81123);
    draw_string(x + 282, y + 9, "X", 0x00FFFFFF, 0x00000000);
    
    draw_string(x + 10, y + 40, "Running Processes:", get_win_txt(), get_win_bg());
    int py = y + 60;
    
    draw_string(x + 10, py, "System Idle Process", 0x00888888, get_win_bg()); py+=20;
    draw_string(x + 10, py, "Kernel Space", 0x00888888, get_win_bg()); 
    draw_rect(x + 200, py - 4, 60, 20, 0x00E88800);
    draw_string(x + 210, py + 2, "CRASH", 0x00FFFFFF, 0x00000000);
    py+=30;
    
    extern int win_count;
    extern int current_app;
    
    // Memory Tracking
    draw_string(x + 10, py, "Memory (Heap & Paging):", get_win_txt(), get_win_bg()); py+=20;
    int sys_ram = 112; 
    int used_ram = sys_ram + (win_count * 15) + (current_app > 0 ? 45 : 0);
    int total_ram = 512;
    char ram_str[32];
    int_to_str(used_ram, ram_str);
    int p = 0; while(ram_str[p]) p++;
    char* suff = " MB / 512 MB";
    for(int i=0; suff[i]; i++) ram_str[p++] = suff[i];
    ram_str[p] = 0;
    draw_string(x + 10, py, ram_str, 0x00888888, get_win_bg()); py+=15;
    
    draw_rect(x + 10, py, 260, 20, 0x00222222);
    int bar_w = (used_ram * 260) / total_ram;
    draw_rect(x + 12, py + 2, bar_w - 4, 16, 0x003A9630);
    py+=30;
    
    if (win_count > 0) {
        draw_string(x + 10, py, "Windows Explorer", get_win_txt(), get_win_bg());
        draw_rect(x + 200, py - 4, 60, 20, 0x00E81123);
        draw_string(x + 214, py + 2, "KILL", 0x00FFFFFF, 0x00000000);
    }
}


void draw_start_item(int x, int y, int w, int v_idx, const char* title, uint32_t txt_col, uint32_t shadow_col) {
    extern volatile int32_t mouse_x, mouse_y;
    bool hover = (mouse_x >= x && mouse_x <= x + w && mouse_y >= y && mouse_y <= y + 36);
    
    if (hover) {
        uint32_t col = dark_mode_active ? 0x00334466 : 0x00A0C0FF;
        draw_rect(x + 2, y + 2, w - 4, 32, col);
        draw_rect(x + 2, y + 2, w - 4, 1, 0x00FFFFFF); // top shine
    }

    if (v_idx != -1) {
        if (vfs[v_idx].type == TYPE_FOLDER) draw_folder_icon(x + 10, y + 4, "");
        else draw_file_icon(x + 10, y + 4, "");
    } else {
        draw_rect(x + 14, y + 10, 16, 16, 0x00888888);
    }
    draw_string_shadow(x + 45, y + 14, title, txt_col, shadow_col);
}

void draw_start_menu(void) {
    int sx = 0, sy = 310, sw = 380, sh = 400;
    for (int y = sy; y < sy + sh; y++) {
        uint32_t base = dark_mode_active ? 0x00151525 : 0x00245DDA;
        draw_rect(sx, y, sw, 1, base);
    }
    draw_rect(sx, sy, sw, 2, 0x00FFFFFF);
    draw_rect(sx + sw - 1, sy, 1, sh, 0x00FFFFFF);

    draw_rect(sx + 10, sy + 10, 45, 45, 0x00FFFFFF);
    draw_rect(sx + 12, sy + 12, 41, 41, 0x00FF8800);
    draw_string_shadow(sx + 65, sy + 18, "Administrator", 0x00FFFFFF, 0x00000000);
    draw_string(sx + 65, sy + 32, "FarukOS User Session", 0x00AACCFF, 0x00000000);

    // Colors for columns
    uint32_t left_txt = dark_mode_active ? 0x00FFFFFF : 0x00111111;
    uint32_t left_shadow = dark_mode_active ? 0x00000000 : 0x00CCCCCC;
    uint32_t right_txt = 0x00FFFFFF;
    uint32_t right_shadow = 0x00000080; // Darker shadow for blue background

    int lw = 230;
    draw_rect(sx + 5, sy + 65, lw, 330, dark_mode_active ? 0x00222222 : 0x00F8F8F8);
    draw_rect(sx + 5, sy + 65, lw, 1, 0x00CCCCCC);
    
    draw_start_item(sx + 5, sy + 70, lw, 3, "Bouncing Ball", left_txt, left_shadow);
    draw_start_item(sx + 5, sy + 110, lw, 4, "Terminal (CMD)", left_txt, left_shadow);
    draw_start_item(sx + 5, sy + 150, lw, 8, "Calculator", left_txt, left_shadow);
    draw_start_item(sx + 5, sy + 190, lw, 11, "Media Player", left_txt, left_shadow);
    draw_start_item(sx + 5, sy + 230, lw, 9, "Clock App", left_txt, left_shadow);
    draw_start_item(sx + 5, sy + 270, lw, 10, "Tic Tac Toe", left_txt, left_shadow);
    draw_start_item(sx + 5, sy + 310, lw, -1, "Switch Theme", left_txt, left_shadow);

    int rx = sx + lw + 10;
    int rw = sw - lw - 15;
    draw_start_item(rx, sy + 70, rw, 2, "Documents", right_txt, right_shadow);
    draw_start_item(rx, sy + 110, rw, 1, "My Games", right_txt, right_shadow);
    draw_start_item(rx, sy + 150, rw, -1, "Settings", right_txt, right_shadow);
    
    draw_rect(rx, sy + 340, rw, 50, 0x00101010);
    draw_start_item(rx, sy + 345, rw, -1, "Shutdown", 0x00FF4444, 0x00000000);
    draw_rect(rx + 12, sy + 355, 18, 18, 0x00E81123);
}

VFSNode vfs[15] = {
    {"Desktop", TYPE_FOLDER, -1},       
    {"My Games", TYPE_FOLDER, 0},       
    {"Documents", TYPE_FOLDER, 0},      
    {"Mario.exe", TYPE_FILE, 0},        
    {"Secret.txt", TYPE_FILE, 0},       
    {"Readme.txt", TYPE_FILE, 0},
    {"Snake.exe", TYPE_FILE, 0},
    {"Mines.exe", TYPE_FILE, 0},
    {"Calculator.exe", TYPE_FILE, 0},
    {"Clock.exe", TYPE_FILE, 0},
    {"Tictac.exe", TYPE_FILE, 0},
    {"Media.exe", TYPE_FILE, 0}
};
int vfs_count = 12;

// Simple string ends with checker to identify file types
static bool ends_with(const char* str, const char* suffix) {
    int str_len = 0, suf_len = 0;
    while (str[str_len]) str_len++;
    while (suffix[suf_len]) suf_len++;
    if (suf_len > str_len) return false;
    for (int i = 0; i < suf_len; i++) {
        if (str[str_len - suf_len + i] != suffix[i]) return false;
    }
    return true;
}

void draw_folder_icon(int x, int y, const char* name) {
    // Drop shadow (bottom and right edge)
    draw_rect(x + 2, y + 26, 32, 2, 0x00000060); // bottom shadow
    draw_rect(x + 32, y + 6, 2, 22, 0x00000030); // right shadow
    
    // Back flap gradient (darker yellow/orange)
    for (int i = 0; i < 18; i++) {
        uint32_t col = 0x00E5A020 + (i * 0x00000201);
        draw_rect(x + 2, y + i, 16, 1, col); // tab
        draw_rect(x, y + 4 + i, 32, 1, col); // back body
    }
    
    // Papers inside (white/light grey sheets sticking out)
    draw_rect(x + 4, y + 8, 24, 10, 0x00F0F0F0);
    draw_rect(x + 5, y + 6, 20, 2, 0x00D0D0D0);
    
    // Front flap gradient (brighter yellow)
    for (int i = 0; i < 18; i++) {
        uint32_t col = 0x00FFCD45 - (i * 0x00010100);
        draw_rect(x, y + 10 + i, 32 + (i % 2), 1, col); // slight angle illusion on right edge
    }
    
    // Front edge highlight
    draw_rect(x, y + 10, 32, 1, 0x00FFEAA0);
    
    draw_string(x - 5, y + 32, name, 0x00FFFFFF, 0x00000000);
}

void draw_file_icon(int x, int y, const char* name) {
    // Drop shadow
    draw_rect(x + 6, y + 30, 24, 2, 0x00000060);
    draw_rect(x + 28, y + 2, 2, 30, 0x00000030);
    
    if (ends_with(name, ".exe")) {
        // App / Executable Icon (Mini Window)
        draw_rect(x + 4, y, 24, 30, 0x00E8EEF5); // window body
        draw_rect(x + 4, y, 24, 6, 0x00215FA8);  // title bar
        draw_rect(x + 4, y, 24, 1, 0x0060A8E8);  // title highlight
        // Window buttons
        draw_rect(x + 23, y + 1, 4, 3, 0x00FF4444); // close
        // Generic app content
        draw_rect(x + 7, y + 9, 18, 18, 0x00FFFFFF);
        draw_rect(x + 10, y + 12, 12, 12, 0x0033CC77); // app logo rect
    } else {
        // Text file icon
        draw_rect(x + 4, y, 24, 30, 0x00FFFFFF);
        // Folded corner effect (cut top right, draw flap)
        draw_rect(x + 22, y, 6, 6, 0x00000000); // clear top right
        // The flap triangle is faked with rects
        draw_rect(x + 22, y + 6, 6, 1, 0x00CCCCCC);
        draw_rect(x + 22, y + 5, 5, 1, 0x00CCCCCC);
        draw_rect(x + 22, y + 4, 4, 1, 0x00CCCCCC);
        draw_rect(x + 22, y + 3, 3, 1, 0x00CCCCCC);
        draw_rect(x + 22, y + 2, 2, 1, 0x00CCCCCC);
        draw_rect(x + 22, y + 1, 1, 1, 0x00CCCCCC);
        
        // Document lines (blue ink)
        draw_rect(x + 8, y + 10, 14, 2, 0x0066AAFF);
        draw_rect(x + 8, y + 15, 16, 2, 0x0066AAFF);
        draw_rect(x + 8, y + 20, 10, 2, 0x0066AAFF);
    }
    
    // common text
    draw_string(x - 5, y + 34, name, 0x00FFFFFF, 0x00000000);
}

void draw_window(int win_idx) {
    if (win_idx < 0 || win_idx >= win_count) return;
    OSWindow* w = &win_stack[win_idx];
    if (w->minimized) return;

    int x = w->x, y = w->y, ww = w->w, wh = w->h;
    
    // Draw Border & Shadow (simplified)
    draw_rect(x, y, ww, wh, dark_mode_active ? 0x00252535 : 0x00E8EEF5);
    
    // Title bar logic
    for (int row = 0; row < 28; row++) {
        uint32_t shade = dark_mode_active ? (0x00202032 + (row * 0x00010100)) : (0x00215FA8 + (row * 0x00010200));
        draw_rect(x, y + row, ww, 1, shade);
    }
    const char* title = "FarukOS App";
    if (w->folder_id != -1) title = vfs[w->folder_id].name;
    else if (w->app_id == 12) title = "Ayarlar (Settings)";
    else if (w->app_id == 11) title = "Tic Tac Toe";
    else if (w->app_id == 8) title = "Minesweeper";
    else if (w->app_id == 6) title = "Hesap Makinesi";
    else if (w->app_id == 10) title = "Saat (Clock)";
    else if (w->app_id == 5) title = "Yilan Oyunu";
    else if (w->app_id == 13) title = "Medya Oynatici";
    else if (w->app_id == 4) title = "Terminal";
    
    draw_string_shadow(x + 10, y + 10, title, 0x00FFFFFF, 0x00000000);

    // Buttons: [X] [[]] [_]
    bool close_hover = (mouse_x >= x + ww - 26 && mouse_x <= x + ww - 4 && mouse_y >= y + 2 && mouse_y <= y + 24);
    draw_rect(x + ww - 26, y + 2, 22, 22, close_hover ? 0x00FF0000 : 0x00C8323A);
    draw_string(x + ww - 18, y + 8, "X", 0x00FFFFFF, 0x00000000);
    if (mouse_left_click && close_hover && just_clicked) {
        close_window(win_count - 1); // Close top window (which is this one)
        needs_redraw = true; return;
    }

    if (w->folder_id != -1) {
        // Folder content
        int cx = x + 15, cy = y + 40;
        for (int i = 0; i < vfs_count; i++) {
            if (vfs[i].parent_id == w->folder_id) {
                if (vfs[i].type == TYPE_FOLDER) draw_folder_icon(cx, cy, vfs[i].name);
                else draw_file_icon(cx, cy, vfs[i].name);
                cx += 80; if (cx > x + ww - 60) { cx = x + 15; cy += 60; }
            }
        }
    } else {
        // App content routing
        extern void run_app_calc(bool jc, int mx, int my, int wx, int wy);
        extern void run_app_terminal_windowed(int wx, int wy, int ww, int wh);
        extern void run_app_media_player_windowed(int wx, int wy, int ww, int wh);
        extern void run_app_snake_windowed(int wx, int wy, int ww, int wh);
        extern void run_app_clock_windowed(int wx, int wy, int ww, int wh);
        extern void run_app_tictactoe_windowed(int wx, int wy, int ww, int wh);
        extern void run_app_minesweeper_windowed(int left_click, int right_click, int mx, int my, int wx, int wy, int ww, int wh);
        extern void run_app_settings_windowed(bool clicked, int mx, int my, int wx, int wy, int ww, int wh);
        
        if (w->app_id == 6) run_app_calc(false, mouse_x, mouse_y, x, y + 28);
        else if (w->app_id == 4) run_app_terminal_windowed(x, y + 28, ww, wh - 28);
        else if (w->app_id == 13) run_app_media_player_windowed(x, y + 28, ww, wh - 28);
        else if (w->app_id == 5) run_app_snake_windowed(x, y + 28, ww, wh - 28);
        else if (w->app_id == 10) run_app_clock_windowed(x, y + 28, ww, wh - 28);
        else if (w->app_id == 11) run_app_tictactoe_windowed(x, y + 28, ww, wh - 28);
        else if (w->app_id == 8) run_app_minesweeper_windowed(mouse_left_click, mouse_right_click, mouse_x, mouse_y, x, y + 28, ww, wh - 28);
        else if (w->app_id == 12) run_app_settings_windowed(just_clicked, mouse_x, mouse_y, x, y + 28, ww, wh - 28);
    }
}

// ------ APPS ------
void run_app_bouncing_ball(void) {
    static int bx = 512, by = 384, bdx = 3, bdy = 2;
    bx += bdx; by += bdy;
    if (bx <= 0 || bx >= 1014) bdx = -bdx;
    if (by <= 40 || by >= 755) bdy = -bdy;

    // Win7 style dark background with gradient
    for (int row = 0; row < 768; row++) {
        uint32_t shade = 0x00050510 + (row / 6);
        draw_rect(0, row, 1024, 1, shade);
    }
    draw_string(10, 12, "Faruk OS - Bouncing Ball", 0x0088CCFF, 0x00000000);
    // Glowing ball
    draw_rect(bx-2, by-2, 14, 14, 0x00FF880000 >> 8);
    draw_rect(bx, by, 10, 10, 0x00FFDD00);
    draw_rect(bx+2, by+2, 4, 4, 0x00FFFFFF); // shine
    // Close button top-right
    draw_rect(990, 5, 28, 22, 0x00C8323A);
    draw_string(1000, 12, "X", 0x00FFFFFF, 0x00000000);
}

// ---- IDE / Advanced Code Editor ----
static bool str_starts(const char* text, int pos, const char* kw) {
    int k = 0;
    while (kw[k]) { if (text[pos+k] != kw[k]) return false; k++; }
    // Make sure it ends on non-alpha
    char after = text[pos+k];
    if ((after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') || (after >= '0' && after <= '9') || after == '_') return false;
    return true;
}

void run_app_ide(void) {
    uint32_t bg    = 0x001E1E2E; // Dark background (Dracula style)
    uint32_t titlebg = 0x00007ACC; // VS Code blue
    uint32_t linebg  = 0x00252535; // Gutter
    uint32_t col_kw  = 0x00569CD6; // blue - keywords
    uint32_t col_str = 0x00CE9178; // orange - strings
    uint32_t col_num = 0x00B5CEA8; // green - numbers
    uint32_t col_cmt = 0x006A9955; // dark green - comments
    uint32_t col_txt = 0x00D4D4D4; // white - default text
    uint32_t col_ln  = 0x00858585; // grey - line numbers

    static const char* keywords[] = {
        "int","void","bool","char","if","else","while","for","return",
        "struct","typedef","static","const","uint8_t","uint32_t","uint16_t",
        "int32_t","include","define","extern","true","false", NULL
    };

    // Title bar
    draw_rect(0, 0, 1024, 768, bg);
    for (int i = 0; i < 32; i++) {
        uint32_t shade = (0x00007ACC + i*0x00000100);
        draw_rect(0, i, 1024, 1, shade);
    }
    draw_rect(0, 0, 1024, 1, 0x00AADDFF); // shine
    draw_string_shadow(10, 11, "FarukOS IDE - Secret.txt", 0x00FFFFFF, 0x00000000);
    draw_rect(860, 6, 50, 20, 0x003A9630); draw_string(870, 12, "SAVE", 0x00FFFFFF, 0x00000000);
    draw_rect(920, 6, 50, 20, 0x00FF8800); draw_string(930, 12, "LOAD", 0x00FFFFFF, 0x00000000);
    draw_rect(984, 0, 40, 32, 0x00E81123); draw_string(999, 12, "X",   0x00FFFFFF, 0x00000000);

    // Gutter
    draw_rect(0, 32, 48, 768-32-20, linebg);

    // Status bar
    draw_rect(0, 748, 1024, 20, 0x007CC8FF);
    draw_string(5, 752, "Ln 1  Col 1  |  UTF-8  |  C  |  FarukOS IDE v1.0", 0x00000000, 0x007CC8FF);

    // Render text with syntax highlighting
    int x = 52, y = 40;
    int line_num = 1;
    int line_start_x = 52;
    // Draw first line number
    char lnbuf[5]; lnbuf[0]='1'; lnbuf[1]=0;
    draw_string(6, y, lnbuf, col_ln, linebg);

    const char* buf = notepad_buffer;
    int i = 0;
    while (buf[i] && y < 740) {
        if (buf[i] == '\n') {
            x = line_start_x; y += 10; line_num++;
            if (y < 740) {
                // Print line number
                char ln[5]; int v = line_num, p = 3;
                ln[4] = 0; ln[3] = 0; ln[2] = ' ';
                if (v >= 10) { ln[1] = '0'+(v/10)%10; ln[0] = '0'+(v/100); }
                else { ln[1] = '0'+v; ln[0] = ' '; }
                draw_string(2, y, ln+0, col_ln, linebg); (void)p;
            }
            i++; continue;
        }
        if (x > 1010) { i++; continue; } // Wrap protection

        // Comment: // ...
        if (buf[i] == '/' && buf[i+1] == '/') {
            char tmp[128]; int t=0;
            while (buf[i] && buf[i] != '\n' && t < 120) tmp[t++] = buf[i++];
            tmp[t] = 0;
            draw_string(x, y, tmp, col_cmt, bg);
            x += t * 8;
            continue;
        }
        // String literal "..."
        if (buf[i] == '"') {
            char tmp[64]; int t=0; tmp[t++] = '"'; i++;
            while (buf[i] && buf[i] != '"' && buf[i] != '\n' && t < 60) tmp[t++] = buf[i++];
            if (buf[i] == '"') tmp[t++] = '"', i++;
            tmp[t] = 0;
            draw_string(x, y, tmp, col_str, bg);
            x += t * 8; continue;
        }
        // Number
        if (buf[i] >= '0' && buf[i] <= '9') {
            char tmp[20]; int t=0;
            while ((buf[i] >= '0' && buf[i] <= '9') && t < 18) tmp[t++] = buf[i++];
            tmp[t] = 0;
            draw_string(x, y, tmp, col_num, bg);
            x += t * 8; continue;
        }
        // Keywords
        bool matched = false;
        for (int k = 0; keywords[k]; k++) {
            const char* kw = keywords[k];
            if (str_starts(buf, i, kw)) {
                int klen = 0; while (kw[klen]) klen++;
                char tmp[24]; int t=0;
                while(t < klen) tmp[t++] = buf[i++];
                tmp[t] = 0;
                draw_string(x, y, tmp, col_kw, bg);
                x += t * 8; matched = true; break;
            }
        }
        if (matched) continue;
        // Normal character
        char single[2] = {buf[i++], 0};
        draw_string(x, y, single, col_txt, bg);
        x += 8;
    }
    // Cursor blink (static tick)
    draw_rect(x, y, 2, 10, 0x00FFFFFF);
}

void run_app_notepad(void) { run_app_ide(); }


uint32_t paint_canvas[600 * 400];
bool paint_initialized = false;

void run_app_paint(bool just_clicked, bool clicked, int mx, int my) {
    if (!paint_initialized) {
        for(int i = 0; i < 600 * 400; i++) {
            paint_canvas[i] = 0x00FFFFFF;
        }
        paint_initialized = true;
    }
    
    draw_rect(95, 45, 610, 470, 0x00ECE9D8);
    draw_rect(95, 45, 610, 30, 0x000055EA);
    draw_string(105, 55, "MS Paint", 0x00FFFFFF, 0x00000000);
    draw_rect(670, 50, 25, 20, 0x00E81123);
    draw_string(678, 56, "X", 0x00FFFFFF, 0x00000000);
    
    draw_rect(100, 80, 20, 20, 0x00000000); 
    draw_rect(130, 80, 20, 20, 0x00E81123); 
    draw_rect(160, 80, 20, 20, 0x00245DDA); 
    draw_rect(190, 80, 20, 20, 0x003A9630); 
    draw_rect(220, 80, 20, 20, 0x00FFFFFF); 
    draw_string(250, 85, "Colors & Eraser", 0x00000000, 0x00ECE9D8);
    
    static uint32_t current_color = 0x00000000;
    
    if (clicked && my >= 80 && my <= 100) {
        if (mx >= 100 && mx <= 120) current_color = 0x00000000;
        if (mx >= 130 && mx <= 150) current_color = 0x00E81123;
        if (mx >= 160 && mx <= 180) current_color = 0x00245DDA;
        if (mx >= 190 && mx <= 210) current_color = 0x003A9630;
        if (mx >= 220 && mx <= 240) current_color = 0x00FFFFFF;
    }
    
    if (clicked && mx >= 100 && mx < 700 && my >= 110 && my < 510) {
        int cx = mx - 100;
        int cy = my - 110;
        for(int dy = -2; dy <= 2; dy++) {
            for(int dx = -2; dx <= 2; dx++) {
                if (cx+dx >= 0 && cx+dx < 600 && cy+dy >= 0 && cy+dy < 400) {
                    paint_canvas[(cy+dy)*600 + (cx+dx)] = current_color;
                }
            }
        }
    }
    
    for (int y = 0; y < 400; y++) {
        for (int x = 0; x < 600; x++) {
            draw_pixel(100 + x, 110 + y, paint_canvas[y * 600 + x]);
        }
    }
}

char terminal_lines[20][80] = {0};
int terminal_line_count = 0;

int string_cmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void print_terminal(const char* text) {
    if (terminal_line_count == 20) {
        for(int i=1; i<20; i++) {
            for(int j=0; j<80; j++) terminal_lines[i-1][j] = terminal_lines[i][j];
        }
        terminal_line_count--;
    }
    int i = 0;
    while(text[i] && i < 79) {
        terminal_lines[terminal_line_count][i] = text[i];
        i++;
    }
    terminal_lines[terminal_line_count][i] = '\0';
    terminal_line_count++;
}

void process_cmd() {
    print_terminal(cmd_buffer); 
    
    bool is_topla = true;
    char* t_str = "topla ";
    for(int k=0; k<6; k++) if(cmd_buffer[k] != t_str[k]) is_topla = false;
    
    bool is_yazdir = true;
    char* y_str = "yazdir \"";
    for(int k=0; k<8; k++) if(cmd_buffer[k] != y_str[k]) is_yazdir = false;
    
    if (string_cmp(cmd_buffer, "help") == 0) {
        print_terminal("Commands: help, ls, clear, whoami, yazdir \"X\", topla X Y");
    } else if (is_topla) {
        int a = 0, b = 0, ptr = 6;
        while(cmd_buffer[ptr] >= '0' && cmd_buffer[ptr] <= '9') {
            a = (a * 10) + (cmd_buffer[ptr] - '0'); ptr++;
        }
        if(cmd_buffer[ptr] == ' ') ptr++;
        while(cmd_buffer[ptr] >= '0' && cmd_buffer[ptr] <= '9') {
            b = (b * 10) + (cmd_buffer[ptr] - '0'); ptr++;
        }
        char res[32]; int_to_str(a + b, res);
        char out[64] = "Sonuc: ";
        int p = 7, r = 0; while(res[r]) out[p++] = res[r++];
        out[p] = 0;
        print_terminal(out);
    } else if (is_yazdir) {
        char msg[80] = {0};
        int m_len = 0;
        for(int ptr=8; cmd_buffer[ptr] != '"' && cmd_buffer[ptr] != '\0' && m_len < 79; ptr++) {
            msg[m_len++] = cmd_buffer[ptr];
        }
        print_terminal(msg);
    } else if (string_cmp(cmd_buffer, "ls") == 0) {
        print_terminal("Mario.exe  Secret.txt  Readme.txt");
        print_terminal("Snake.exe  Mines.exe   Demo.exe");
        print_terminal("My Games/  Documents/");
    } else if (string_cmp(cmd_buffer, "whoami") == 0) {
        print_terminal(is_guest ? "guest@FarukOS [LIMITED]" : "admin@FarukOS [ROOT]");
    } else if (string_cmp(cmd_buffer, "clear") == 0) {
        terminal_line_count = 0;
    } else if (string_cmp(cmd_buffer, "ps") == 0) {
        print_terminal("PID  NAME              STATUS");
        print_terminal("001  kernel.sys         RUNNING");
        print_terminal("002  mouse_drv.sys       RUNNING");
        print_terminal("003  keyboard_drv.sys    RUNNING");
        print_terminal("004  bga_gfx.sys         RUNNING");
        if (current_app > 0) print_terminal("005  app.exe             RUNNING");
    } else if (string_cmp(cmd_buffer, "sysinfo") == 0) {
        print_terminal("OS: Faruk OS v1.0");
        print_terminal("CPU: i686 @ ~100 MHz (emulated)");
        print_terminal("RAM: 512 MB (Virtual)");
        print_terminal("Disk: 100 MB ATA PIO");
        print_terminal("Display: 1024x768 BGA 32-bit");
        print_terminal(is_guest ? "User: guest [LIMITED]" : "User: admin [ROOT]");
    } else if (string_cmp(cmd_buffer, "ping") == 0) {
        print_terminal("ping: missing host. Usage: ping <IP>");
    } else if (cmd_len > 5 && cmd_buffer[0]=='p' && cmd_buffer[1]=='i' && cmd_buffer[2]=='n' && cmd_buffer[3]=='g' && cmd_buffer[4]==' ') {
        char* host = cmd_buffer + 5;
        char msg[80] = "Pinging "; int m=8;
        for(int k=0; host[k] && m<70; k++) msg[m++]=host[k];
        msg[m]=0; print_terminal(msg);
        print_terminal("Reply from host: bytes=32 time=1ms TTL=128");
        print_terminal("Reply from host: bytes=32 time=1ms TTL=128");
        print_terminal("Reply from host: bytes=32 time=2ms TTL=128");
        print_terminal("Ping stats: Sent=3, Received=3, Lost=0 (0% loss)");
    } else if (cmd_len > 6 && cmd_buffer[0]=='c' && cmd_buffer[1]=='h' && cmd_buffer[2]=='m' && cmd_buffer[3]=='o' && cmd_buffer[4]=='d') {
        print_terminal("[chmod] Permissions updated successfully.");
        print_terminal("Warning: System files protected by kernel.");
    } else if (string_cmp(cmd_buffer, "hack") == 0) {
        print_terminal("Initializing exploit framework...");
        print_terminal("[OK] Bypassing firewall... DONE");
        print_terminal("[OK] Injecting payload... DONE");
        print_terminal("[OK] Escalating privileges... DONE");
        print_terminal("ACCESS GRANTED. Welcome, hacker.");
        print_terminal("(Just kidding. This is a demo OS.)");
    } else if (string_cmp(cmd_buffer, "help") == 0) {
        print_terminal("Commands: ls, ps, whoami, sysinfo");
        print_terminal("ping <IP>, chmod <perm> <file>");
        print_terminal("hack, clear, yazdir \"X\", topla X Y");
    } else if (cmd_len > 0) {
        char err[80] = "'"; int e=1;
        for(int k=0; cmd_buffer[k] && e<60; k++) err[e++]=cmd_buffer[k];
        err[e++]='\''; err[e++]=0;
        print_terminal(err);
        print_terminal("is not recognized. Type 'help'.");
    }
    
    cmd_len = 0;
    cmd_buffer[0] = '\0';
    cmd_entered = false;
    kb_changed = true;
}

void run_app_terminal_windowed(int wx, int wy, int ww, int wh) {
    if (cmd_entered) process_cmd();
    
    draw_rect(wx, wy, ww, wh, 0x00000000); // Black terminal body
    
    int terminal_y = wy + 5;
    for(int i=0; i<terminal_line_count; i++) {
        draw_string(wx+5, terminal_y, terminal_lines[i], 0x0000FF00, 0x00000000); 
        terminal_y += 15;
        if (terminal_y > wy + wh - 20) break;
    }
    
    draw_string(wx+5, terminal_y, "> ", 0x0000FF00, 0x00000000);
    draw_string(wx+25, terminal_y, cmd_buffer, 0x00FFFFFF, 0x00000000); 
}

void run_app_calc(bool just_clicked, int mouse_x, int mouse_y, int wx, int wy) {
    int ww=230, wh=320;
    // Window content area background
    draw_rect(wx, wy, ww, wh, 0x00F0F0F0);
    
    static int val1 = 0, val2 = 0;
    static char op = 0;
    static bool typing_op2 = false;
    static char display[16] = "0";

    // Result screen
    draw_rect(wx+10, wy+10, ww-20, 40, 0x00FFFFFF);
    draw_string(wx+20, wy+22, display, 0x00000000, 0x00000000);
    
    // Keypad
    const char* keys[] = {"7","8","9","/", "4","5","6","*", "1","2","3","-", "C","0","=","+"};
    for(int i=0; i<16; i++) {
        int bx = wx + 10 + (i % 4) * 55;
        int by = wy + 60 + (i / 4) * 60;
        bool hover = (mouse_x >= bx && mouse_x <= bx+45 && mouse_y >= by && mouse_y <= by+45);
        draw_rect(bx, by, 45, 45, hover ? 0x00D0D0D0 : 0x00E0E0E0);
        draw_string(bx+18, by+15, keys[i], 0x00000000, 0x00000000);
        
        if (just_clicked && hover) {
            char k = keys[i][0];
            if (k >= '0' && k <= '9') {
                if (!typing_op2) { val1 = val1*10 + (k-'0'); int_to_str(val1, display); }
                else { val2 = val2*10 + (k-'0'); int_to_str(val2, display); }
            } else if (k == 'C') { val1=0; val2=0; op=0; typing_op2=false; display[0]='0'; display[1]=0; }
            else if (k == '=') {
                int res = 0;
                if (op == '+') res = val1 + val2;
                if (op == '-') res = val1 - val2;
                if (op == '*') res = val1 * val2;
                if (op == '/') res = (val2 != 0) ? (val1 / val2) : 0;
                int_to_str(res, display); val1 = res; val2 = 0; typing_op2 = false;
            } else { op = k; typing_op2 = true; }
        }
    }
}


extern volatile char last_key;

void run_app_snake_windowed(int wx, int wy, int ww, int wh) {
    static int s_x[400], s_y[400];
    static int s_len = 0;
    static int s_dir = 1; // 0:up, 1:right, 2:down, 3:left
    static int a_x = 20, a_y = 15;
    static bool dead = false;
    static int s_tick = 0;
    
    if (s_len == 0) { 
        s_len = 5;
        for(int i=0; i<5; i++) { s_x[i] = 10-i; s_y[i] = 10; }
        s_dir = 1; a_x = 30; a_y = 20; dead = false;
    }
    
    draw_rect(wx, wy, ww, wh, 0x00000000); // Game Board
    
    if (dead) {
        draw_string(wx + ww/2 - 45, wy + wh/2 - 10, "GAME OVER!", 0x00FF0000, 0x00000000);
        
        bool r_hover = (mouse_x >= wx + ww/2 - 60 && mouse_x <= wx + ww/2 + 60 && mouse_y >= wy + wh/2 + 10 && mouse_y <= wy + wh/2 + 40);
        draw_rect(wx + ww/2 - 60, wy + wh/2 + 10, 120, 30, r_hover ? 0x00A0C0FF : 0x00215FA8);
        draw_string(wx + ww/2 - 50, wy + wh/2 + 23, "Play Again (Click)", 0x00FFFFFF, 0x00000000);
        
        if (mouse_left_click && r_hover) { s_len = 0; s_tick = 0; }
        return;
    }
    
    if (last_key == 'w' || last_key == 'W') { if (s_dir != 2) s_dir = 0; }
    else if (last_key == 'd' || last_key == 'D') { if (s_dir != 3) s_dir = 1; }
    else if (last_key == 's' || last_key == 'S') { if (s_dir != 0) s_dir = 2; }
    else if (last_key == 'a' || last_key == 'A') { if (s_dir != 1) s_dir = 3; }
    
    s_tick++;
    if (s_tick > 10) { 
        s_tick = 0;
        for(int i=s_len-1; i>0; i--) { s_x[i] = s_x[i-1]; s_y[i] = s_y[i-1]; }
        if (s_dir == 0) s_y[0]--; if (s_dir == 1) s_x[0]++; if (s_dir == 2) s_y[0]++; if (s_dir == 3) s_x[0]--;
        if (s_x[0] < 0 || s_x[0] >= ww/10 || s_y[0] < 0 || s_y[0] >= wh/10) dead = true;
        for(int i=1; i<s_len; i++) { if (s_x[0] == s_x[i] && s_y[0] == s_y[i]) dead = true; }
        if (s_x[0] == a_x && s_y[0] == a_y) {
            if (s_len < 399) s_len++;
            a_x = (s_x[0] * 7 + 13) % (ww/10); a_y = (s_y[0] * 13 + 7) % (wh/10);
        }
    }
    draw_rect(wx + a_x*10 + 1, wy + a_y*10 + 1, 8, 8, 0x00FF0000); 
    for(int i=0; i<s_len; i++) { draw_rect(wx + s_x[i]*10 + 1, wy + s_y[i]*10 + 1, 8, 8, 0x0000FF00); }
}

void run_app_clock_windowed(int wx, int wy, int ww, int wh) {
    int h, m, s; rtc_get_time(&h, &m, &s);
    char buf[16];
    buf[0]='0'+(h/10); buf[1]='0'+(h%10); buf[2]=':';
    buf[3]='0'+(m/10); buf[4]='0'+(m%10); buf[5]=':';
    buf[6]='0'+(s/10); buf[7]='0'+(s%10); buf[8]=0;
    
    draw_rect(wx, wy, ww, wh, 0x00000000);
    draw_string(wx + ww/2 - 40, wy + wh/2 - 10, buf, 0x0000FF00, 0x00000000);
    draw_string(wx + 20, wy + wh - 30, "System Time (Real RTC)", 0x00888888, 0x00000000);
}

static int board[3][3] = {0}; 
static int tic_turn = 1;
static int tic_winner = 0; // 0=none, 1=X, 2=O, 3=Draw

static int check_tic_winner() {
    // Rows
    for(int i=0; i<3; i++) if(board[i][0]!=0 && board[i][0]==board[i][1] && board[i][1]==board[i][2]) return board[i][0];
    // Cols
    for(int i=0; i<3; i++) if(board[0][i]!=0 && board[0][i]==board[1][i] && board[1][i]==board[2][i]) return board[0][i];
    // Diagonals
    if(board[0][0]!=0 && board[0][0]==board[1][1] && board[1][1]==board[2][2]) return board[0][0];
    if(board[0][2]!=0 && board[0][2]==board[1][1] && board[1][1]==board[2][0]) return board[0][2];
    // Draw?
    bool full = true;
    for(int i=0; i<9; i++) if(((int*)board)[i]==0) full = false;
    return full ? 3 : 0;
}

void run_app_tictactoe_windowed(int wx, int wy, int ww, int wh) {
    draw_rect(wx, wy, ww, wh, get_win_bg());
    int ox=wx+30, oy=wy+10, sz=(ww-60)/3;
    
    for(int r=0; r<3; r++) {
        for(int c=0; c<3; c++) {
            int cx=ox+c*sz, cy=oy+r*sz;
            bool hover = (mouse_x>=cx && mouse_x<=cx+sz-4 && mouse_y>=cy && mouse_y<=cy+sz-4);
            draw_rect(cx, cy, sz-4, sz-4, hover ? 0x00D0D0D0 : 0x00FFFFFF);
            
            if(board[r][c] == 1) { // Red X
                draw_rect(cx+10, cy+10, sz-24, 4, 0x00FF0000); 
                draw_rect(cx+10, cy+sz-14, sz-24, 4, 0x00FF0000);
                draw_string(cx+sz/2-5, cy+sz/2-8, "X", 0x00FF0000, 0x00000000);
            }
            if(board[r][c] == 2) { // Blue O
                draw_rect(cx+10, cy+10, sz-24, sz-24, 0x000000FF);
                draw_rect(cx+14, cy+14, sz-32, sz-32, 0x00FFFFFF);
                draw_string(cx+sz/2-5, cy+sz/2-8, "O", 0x000000FF, 0x00000000);
            }
            
            if(mouse_left_click && tic_winner == 0 && hover) {
                if(board[r][c] == 0) {
                    board[r][c] = tic_turn; tic_turn = (tic_turn==1 ? 2 : 1);
                    tic_winner = check_tic_winner();
                }
            }
        }
    }
    
    if (tic_winner != 0) {
        draw_rect(wx+50, wy+wh-100, ww-100, 40, 0x00000000);
        if(tic_winner == 1) draw_string(wx+ww/2-40, wy+wh-85, "X WINS!", 0x00FF0000, 0x00000000);
        if(tic_winner == 2) draw_string(wx+ww/2-40, wy+wh-85, "O WINS!", 0x000000FF, 0x00000000);
        if(tic_winner == 3) draw_string(wx+ww/2-30, wy+wh-85, "DRAW!", 0x00888888, 0x00000000);
    } else {
        char turn_msg[16] = "Turn: X"; if(tic_turn==2) turn_msg[6]='O';
        draw_string(wx+ww/2-30, wy+wh-85, turn_msg, 0x00215FA8, 0x00000000);
    }
    
    bool reset_hover = (mouse_x >= wx+ww/2-50 && mouse_x <= wx+ww/2+50 && mouse_y >= wy+wh-40 && mouse_y <= wy+wh-10);
    draw_rect(wx+ww/2-50, wy+wh-40, 100, 30, reset_hover ? 0x00A0C0FF : 0x00215FA8);
    draw_string(wx+ww/2-25, wy+wh-27, "RESET", 0x00FFFFFF, 0x00000000);
    if(mouse_left_click && reset_hover) {
         for(int i=0; i<9; i++) ((int*)board)[i] = 0; tic_turn=1; tic_winner=0;
    }
}

void run_app_settings_windowed(bool clicked, int mx, int my, int wx, int wy, int ww, int wh) {
    draw_rect(wx, wy, ww, wh, get_win_bg());
    
    draw_string(wx+30, wy+30, "Theme Options:", 0x00000000, get_win_bg());
    
    // Dark Mode Toggle
    bool dm_hover = (mx >= wx+30 && mx <= wx+200 && my >= wy+55 && my <= wy+85);
    draw_rect(wx+30, wy+55, 170, 30, dm_hover ? 0x00A0C0FF : 0x00FFFFFF);
    draw_rect(wx+30, wy+55, 170, 1, 0x00CCCCCC);
    draw_string(wx+45, wy+65, dark_mode_active ? "Switch to Light" : "Switch to Dark", 0x00000000, 0x00000000);
    if(clicked && dm_hover) { dark_mode_active = !dark_mode_active; generate_wallpaper(); needs_redraw = true; }

    // Refresh System
    bool rf_hover = (mx >= wx+30 && mx <= wx+200 && my >= wy+100 && my <= wy+130);
    draw_rect(wx+30, wy+100, 170, 30, rf_hover ? 0x00A0C0FF : 0x00FFFFFF);
    draw_rect(wx+30, wy+100, 170, 1, 0x00CCCCCC);
    draw_string(wx+45, wy+110, "Refresh System", 0x00000000, 0x00000000);
    if(clicked && rf_hover) { generate_wallpaper(); for(int i=0; i<vfs_count; i++) icon_init[i] = false; needs_redraw = true; }

    draw_string(wx+230, wy+30, "OS Information:", 0x00000000, get_win_bg());
    draw_string(wx+230, wy+55, "FarukOS v1.0", 0x00555555, get_win_bg());
    draw_string(wx+230, wy+75, "Kernel: Modern Aero", 0x00555555, get_win_bg());
}


void run_app_media_player_windowed(int wx, int wy, int ww, int wh) {
    // Media Player Body
    draw_rect(wx, wy, ww, wh, dark_mode_active ? 0x001A1A2A : 0x00FFFFFF);
    
    // Album Art / Visualizer Area
    draw_rect(wx + 10, wy + 10, 150, 150, 0x00000000);
    draw_rect(wx + 20, wy + 20, 130, 130, 0x00215FA8);
    draw_string(wx + 40, wy + 70, "FARUK", 0x00FFFFFF, 0x00215FA8);
    draw_string(wx + 55, wy + 90, "OS", 0x00FFFFFF, 0x00215FA8);

    // Dynamic Visualizer
    for(int i=0; i<15; i++) {
        int v_h = (tick + i*5) % 80 + 10;
        draw_rect(wx + 180 + i*18, wy + 160 - v_h, 12, v_h, 0x0000FF00);
        draw_rect(wx + 180 + i*18, wy + 160 - v_h - 2, 12, 2, 0x00FFFFFF);
    }

    // Playlist
    draw_string(wx + 10, wy + 180, "Playlist:", dark_mode_active?0x00FFFFFF:0x00000000, 0x00000000);
    draw_rect(wx + 10, wy + 200, ww - 20, 80, dark_mode_active?0x00111122:0x00F0F0F0);
    draw_string(wx + 20, wy + 210, "1. Startup.wav", 0x00215FA8, 0x00000000);
    draw_string(wx + 20, wy + 230, "2. Beep.wav", dark_mode_active?0x00CCCCCC:0x00666666, 0x00000000);
    draw_string(wx + 20, wy + 250, "3. Remix.wav", dark_mode_active?0x00CCCCCC:0x00666666, 0x00000000);

    // Controls
    bool play_hover = (mouse_x >= wx+180 && mouse_x <= wx+260 && mouse_y >= wy+210 && mouse_y <= wy+250);
    draw_rect(wx + 180, wy + 210, 80, 40, play_hover ? 0x003A9630 : 0x00228844);
    draw_string(wx + 200, wy + 222, "PLAY", 0x00FFFFFF, 0x00000000);

    bool test_hover = (mouse_x >= wx+270 && mouse_x <= wx+350 && mouse_y >= wy+210 && mouse_y <= wy+250);
    draw_rect(wx + 270, wy + 210, 80, 40, test_hover ? 0x00A0A0FF : 0x00215FA8);
    draw_string(wx + 290, wy + 222, "TEST", 0x00FFFFFF, 0x00000000);

    if (mouse_left_click) {
        if (play_hover) sb16_play_startup_sound();
        if (test_hover) sb16_play_test_tone();
    }
}

void draw_alt_tab_overlay(void) {
    int ow=600, oh=120, ox=(1024-ow)/2, oy=(768-oh)/2;
    draw_rect(ox, oy, ow, oh, 0x00000000);
    for(int i=0; i<oh; i++) draw_rect(ox, oy+i, ow, 1, 0x00151525);
    draw_rect(ox, oy, ow, 2, 0x00A0C0FF);
    draw_string_shadow(ox+10, oy+10, "Uygulama Degistir", 0x00FFFFFF, 0x00000000);

    const char* app_names[] = {"None", "Bouncing Ball", "Notepad", "Paint", "Terminal", "Snake", "Calculator", "Task Manager", "Minesweeper", "Multitasking", "Clock", "TicTacToe", "Settings", "Media Player"};

    for(int i=0; i<5; i++) {
        int ax = ox+20 + i*115;
        int aid = app_history[i];
        if (aid < 1 || aid > 13) { // Draw empty slot
            draw_rect(ax, oy+40, 100, 60, 0x001A1A2A);
            continue;
        }
        draw_rect(ax, oy+40, 100, 60, (i==0) ? 0x00215FA8 : 0x00333333);
        const char* name = app_names[aid];
        draw_string(ax+5, oy+60, name, 0x00FFFFFF, 0x00000000);
    }
}

#define MW 10
#define MH 10
#define M_MINE 9

static int ms_grid[MW][MH];
static bool ms_revealed[MW][MH];
static bool ms_flagged[MW][MH];
static bool ms_gameover = false;
static bool ms_win = false;
static bool ms_init = false;

static void ms_reveal(int x, int y) {
    if (x < 0 || x >= MW || y < 0 || y >= MH) return;
    if (ms_revealed[x][y] || ms_flagged[x][y]) return;
    ms_revealed[x][y] = true;
    if (ms_grid[x][y] == 0) {
        for(int dx=-1; dx<=1; dx++) {
            for(int dy=-1; dy<=1; dy++) {
                ms_reveal(x+dx, y+dy);
            }
        }
    }
}

void run_app_minesweeper_windowed(int left_click, int right_click, int mx, int my, int wx, int wy, int ww, int wh) {
    if (!ms_init) {
        ms_gameover = ms_win = false;
        for(int i=0; i<MW; i++) for(int j=0; j<MH; j++) { ms_grid[i][j] = 0; ms_revealed[i][j] = false; ms_flagged[i][j] = false; }
        int placed = 0;
        uint32_t ms_seed = tick + (uint32_t)mx + (uint32_t)my;
        while(placed < 10) {
            ms_seed = (ms_seed * 1103515245 + 12345) & 0x7FFFFFFF;
            int rx = (ms_seed >> 8) % MW; int ry = (ms_seed >> 16) % MH;
            if (ms_grid[rx][ry] != M_MINE) { ms_grid[rx][ry] = M_MINE; placed++; }
        }
        for(int x=0; x<MW; x++) for(int y=0; y<MH; y++) {
            if (ms_grid[x][y] == M_MINE) continue;
            int count = 0;
            for(int dx=-1; dx<=1; dx++) for(int dy=-1; dy<=1; dy++) {
                if (x+dx>=0 && x+dx<MW && y+dy>=0 && y+dy<MH && ms_grid[x+dx][y+dy] == M_MINE) count++;
            }
            ms_grid[x][y] = count;
        }
        ms_init = true;
    }
    
    draw_rect(wx, wy, ww, wh, 0x00000000);
    int ox = wx + (ww - MW*32)/2, oy = wy + (wh - MH*32)/2;
    for(int x=0; x<MW; x++) {
        for(int y=0; y<MH; y++) {
            int cx = ox + x*32, cy = oy + y*32;
            bool hover = (mx >= cx && mx <= cx+30 && my >= cy && my <= cy+30);
            draw_rect(cx, cy, 30, 30, ms_revealed[x][y] ? 0x00FFFFFF : (hover ? 0x00D0D0D0 : 0x00A0A0A0));
            if (ms_revealed[x][y]) {
                if (ms_grid[x][y] == M_MINE) draw_string(cx+10, cy+8, "*", 0x00000000, 0x00000000);
                else if (ms_grid[x][y] > 0) { 
                    char bn[2] = {ms_grid[x][y]+'0', 0};
                    uint32_t bc = (ms_grid[x][y]==1)?0x000000FF:(ms_grid[x][y]==2)?0x00008800:0x00FF0000;
                    draw_string(cx+10, cy+8, bn, bc, 0x00000000);
                }
            } else if (ms_flagged[x][y]) draw_string(cx+10, cy+8, "F", 0x00FF0000, 0x00000000);
            
            if (!ms_gameover && hover && left_click) {
                if (ms_grid[x][y] == M_MINE) ms_gameover = true; else ms_reveal(x,y);
            }
            if (!ms_gameover && hover && right_click) {
                ms_flagged[x][y] = !ms_flagged[x][y];
            }
        }
    }
    if (ms_gameover) draw_string(wx+ww/2-50, wy+wh-20, "BOOM! Game Over", 0x00FF0000, 0x00000000);
}

void run_multitasking_demo(void) {
    draw_rect(0, 0, 1024, 768, get_win_bg());
    draw_string(20, 20, "Preemptive Multitasking Demo", get_win_txt(), get_win_bg());
    draw_rect(980, 10, 30, 20, 0x00E81123);
    draw_string(990, 16, "X", 0x00FFFFFF, 0x00000000);
    
    draw_rect(50, 80, 420, 500, 0x00000000);
    draw_rect(50, 60, 420, 20, get_win_title());
    draw_string(60, 66, "Terminal (Interactive)", 0x00FFFFFF, 0x00000000);
    
    draw_rect(520, 80, 420, 500, 0x00222222);
    draw_rect(520, 60, 420, 20, get_win_title());
    draw_string(530, 66, "Snake AI (Autonomous)", 0x00FFFFFF, 0x00000000);
    
    int cy = 85;
    for (int i = 0; i < terminal_line_count; i++) {
        draw_string(55, cy, terminal_lines[i], 0x0000FF00, 0x00000000);
        cy += 15;
    }
    draw_string(55, cy, "> ", 0x0000FF00, 0x00000000);
    draw_string(71, cy, cmd_buffer, 0x0000FF00, 0x00000000);
    static int tick = 0; tick++;
    if ((tick / 20) % 2 == 0) draw_string(71 + cmd_len*8, cy, "_", 0x0000FF00, 0x00000000);
    if (cmd_entered) process_cmd();
    
    static int s_x[400] = {10,9,8}, s_y[400] = {10,10,10}, s_l = 3;
    static int a_x = 25, a_y = 20;
    static bool dead = false;
    
    if (!dead && tick % 15 == 0) {
        int dx = 0, dy = 0;
        if (s_x[0] < a_x) dx = 1; else if (s_x[0] > a_x) dx = -1;
        else if (s_y[0] < a_y) dy = 1; else if (s_y[0] > a_y) dy = -1;
        
        for(int i=s_l-1; i>0; i--) { s_x[i] = s_x[i-1]; s_y[i] = s_y[i-1]; }
        s_x[0] += dx; s_y[0] += dy;
        
        if (s_x[0] == a_x && s_y[0] == a_y) {
            if (s_l < 399) s_l++;
            a_x = (s_x[0] + s_l * 7) % 40; 
            a_y = (s_y[0] + s_l * 5) % 48; 
        }
        for(int i=1; i<s_l; i++) if (s_x[0]==s_x[i] && s_y[0]==s_y[i]) dead = true;
        if (s_x[0] < 0 || s_x[0] >= 42 || s_y[0] < 0 || s_y[0] >= 50) dead = true;
    }
    
    draw_rect(520 + a_x*10, 80 + a_y*10, 9, 9, 0x00FF0000);
    for(int i=0; i<s_l; i++) draw_rect(520 + s_x[i]*10, 80 + s_y[i]*10, 9, 9, i==0 ? 0x0000FF00 : 0x00008800);
    if (dead) draw_string(600, 250, "AI CRASHED!", 0x00FFFFFF, 0x00FF0000);
}

// ------ WINDOW MANAGER ------
#define MAX_FOLDERS 20

int get_top_win_at(int mx, int my) {
    for (int i = win_count - 1; i >= 0; i--) {
        int idx = win_order[i];
        OSWindow* w = &win_stack[idx];
        if (mx >= w->x && mx <= w->x + w->w && my >= w->y && my <= w->y + w->h) {
            return i; // index in order array
        }
    }
    return -1;
}

void bring_to_front(int order_index) {
    if (order_index == win_count - 1) return;
    int val = win_order[order_index];
    for (int i = order_index; i < win_count - 1; i++) {
        win_order[i] = win_order[i+1];
    }
    win_order[win_count - 1] = val;
}

void close_window(int order_index) {
    if (order_index < 0 || order_index >= win_count) return;
    int stack_idx = win_order[order_index];
    
    // Shift win_stack
    for (int i = stack_idx; i < win_count - 1; i++) {
        win_stack[i] = win_stack[i+1];
    }
    
    // Remove from win_order and shift others
    for (int i = order_index; i < win_count - 1; i++) {
        win_order[i] = win_order[i+1];
    }
    
    // Any index in win_order > stack_idx must be decremented
    for (int i = 0; i < win_count - 1; i++) {
        if (win_order[i] > stack_idx) win_order[i]--;
    }
    
    win_count--;
}

void launch_app(int app_id) {
    // Check if already open
    for (int i = 0; i < win_count; i++) {
        if (win_stack[i].app_id == app_id) {
            bring_to_front(i); return;
        }
    }
    if (win_count < MAX_FOLDERS) {
        win_stack[win_count].folder_id = -1;
        win_stack[win_count].app_id = app_id;
        win_stack[win_count].x = 100 + win_count * 40;
        win_stack[win_count].y = 100 + win_count * 40;
        win_stack[win_count].w = (app_id == 6) ? 230 : (app_id == 10) ? 300 : (app_id == 8) ? 400 : (app_id == 11) ? 400 : 400;
        win_stack[win_count].h = (app_id == 6) ? 350 : (app_id == 10) ? 200 : (app_id == 8) ? 450 : (app_id == 11) ? 450 : 350;
        win_stack[win_count].maximized = false;
        win_stack[win_count].minimized = false;
        win_order[win_count] = win_count;
        win_count++;
    }
}

// ------ MAIN KERNEL ------
void kernel_main(void) {
    init_gdt();
    init_idt();
    init_mouse();

    bga_set_video_mode(1024, 768, 32);
    fb = (uint32_t*) 0xFD000000;

    init_sb16();

    // ---- LOGIN PHASE ----
    bool wrong_pass = false;
    
    int32_t old_mx = mouse_x, old_my = mouse_y;
    int last_sec = -1;

    generate_wallpaper();
    
    draw_login_screen(false);
    swap_buffers();
    while (boot_phase == 0) {
        __asm__ volatile("hlt");
        // Always redraw if mouse moves for hover effects
        if (old_mx != mouse_x || old_my != mouse_y) {
            old_mx = mouse_x; old_my = mouse_y;
            draw_login_screen(wrong_pass);
            draw_cursor(mouse_x, mouse_y, 0);
            swap_buffers();
        }

        if (kb_changed) {
            kb_changed = false;
            // Check if Enter was pressed via last_key
            if (last_key == '\n') {
                last_key = 0;
                if (str_eq(login_buffer, "admin") || str_eq(login_buffer, "1234")) {
                    is_guest = false; boot_phase = 1;
                } else if (str_eq(login_buffer, "guest") || str_eq(login_buffer, "")) {
                    is_guest = true; boot_phase = 1;
                } else {
                    wrong_pass = true; login_len = 0; login_buffer[0] = '\0';
                }
            }
        }
        
        if (mouse_left_click) {
            // Check guest link click: px=337, py=234 -> px+110=447, py+225=459, w=140, h=20
            if (mouse_x >= 447 && mouse_x <= 587 && mouse_y >= 459 && mouse_y <= 479) {
                is_guest = true; boot_phase = 1;
            }
            // Check button click: px+275=612, py+170=404, w=30, h=20
            if (mouse_x >= 612 && mouse_x <= 642 && mouse_y >= 404 && mouse_y <= 424) {
               if (str_eq(login_buffer, "admin") || str_eq(login_buffer, "1234")) {
                    is_guest = false; boot_phase = 1;
                } else {
                    wrong_pass = true; login_len = 0; login_buffer[0] = '\0';
                }
            }
        }

        if (boot_phase == 0) {
            draw_login_screen(wrong_pass);
            draw_cursor(mouse_x, mouse_y, 0);
            swap_buffers();
        }
    } // end while boot_phase==0
    // Brief "Welcome" flash
    draw_rect(300, 340, 424, 60, 0x000050C0);
    draw_string(360, 360, is_guest ? "Welcome, Guest!" : "Welcome, Administrator!", 0x00FFFFFF, 0x00000000);
    swap_buffers();
    
    sb16_play_startup_sound();
    
    for (volatile int d = 0; d < 5000000; d++);
    // ---- END LOGIN PHASE ----
    
    int drag_win_idx = -1;
    bool start_menu_open = false;
    bool is_dragging = false;
    
    int32_t drag_start_mx = 0, drag_start_my = 0;
    int32_t drag_start_wx = 0, drag_start_wy = 0;
    
    int icon_count = 0;
    
    int32_t idrag_mx = 0, idrag_my = 0;
    int32_t idrag_ix = 0, idrag_iy = 0;
    
    bool prev_click = false;
    bool prev_r_click = false;

    while (1) {
        needs_redraw = false;
        bool clicked = mouse_left_click;
        just_clicked = (clicked && !prev_click);
        prev_click = clicked;
        
        bool r_clicked = mouse_right_click;
        bool just_r_clicked = (r_clicked && !prev_r_click);
        prev_r_click = r_clicked;
        
        int clock_h, clock_m, clock_s;
        rtc_get_time(&clock_h, &clock_m, &clock_s);
        if (clock_s != last_sec) { last_sec = clock_s; needs_redraw = true; }

        if (kb_changed) {
            char k = last_key; kb_changed = false;
            if (k == '\t') { show_alt_tab = !show_alt_tab; needs_redraw = true; }
            else if (k == 27) { // ESC closes top window or app
                if (win_count > 0) close_window(win_count - 1);
                else current_app = 0;
                needs_redraw = true;
            }
            else if (show_alt_tab) { /* can add cycle logic here */ }
            else if (search_bar_focused) {
                if (k == '\b') { if (search_len > 0) search_buffer[--search_len] = 0; }
                else if (k >= 32 && k <= 126 && search_len < 31) { search_buffer[search_len++] = k; search_buffer[search_len] = 0; }
                else if (k == '\n') search_bar_focused = false;
                needs_redraw = true;
            }
        }

        // --- APP ROUTING ---
        if (current_app == 1) { // Bouncing Ball
            run_app_bouncing_ball();
            if (just_clicked && mouse_x >= 990 && mouse_y <= 27) current_app = 0;
            draw_cursor(mouse_x, mouse_y, 1); swap_buffers(); continue;
        } 
        else if (current_app == 2) { // Notepad
            if (just_clicked && mouse_x >= 965 && mouse_y <= 35) current_app = 0;
            if (just_clicked && mouse_x >= 850 && mouse_x <= 915 && mouse_y <= 30) {
                ata_write_sector(1, (uint8_t*)notepad_buffer); ata_write_sector(2, ((uint8_t*)notepad_buffer) + 512);
            }
            if (just_clicked && mouse_x >= 915 && mouse_x <= 975 && mouse_y <= 30) {
                ata_read_sector(1, (uint8_t*)notepad_buffer); ata_read_sector(2, ((uint8_t*)notepad_buffer) + 512);
                extern int notepad_len; notepad_len = 0;
                while (notepad_buffer[notepad_len] && notepad_len < 1023) notepad_len++;
            }
            if (kb_changed || old_mx != mouse_x || old_my != mouse_y || just_clicked) {
                run_app_notepad(); draw_cursor(mouse_x, mouse_y, 2); swap_buffers();
                old_mx = mouse_x; old_my = mouse_y; kb_changed = false;
            }
            __asm__ volatile("hlt"); continue;
        }
        else if (current_app == 3) { // Paint
            if (just_clicked && mouse_x >= 670 && mouse_y >= 50 && mouse_x <= 695 && mouse_y <= 70) current_app = 0;
            if (old_mx != mouse_x || old_my != mouse_y || clicked) {
                draw_rect(0, 0, 1024, 768, 0x00333333); run_app_paint(just_clicked, clicked, mouse_x, mouse_y);
                draw_cursor(mouse_x, mouse_y, 0); swap_buffers();
                old_mx = mouse_x; old_my = mouse_y;
            }
            __asm__ volatile("hlt"); continue;
        }
        else if (current_app == 4) { // Terminal (Legacy - being moved to windows)
            if (just_clicked && mouse_x >= 880 && mouse_y >= 84 && mouse_x <= 920 && mouse_y <= 120) current_app = 0;
        }


        // --- APP HISTORY TRACKING ---
        if (current_app > 0 && current_app != app_history[0]) {
            for(int i=4; i>0; i--) app_history[i] = app_history[i-1];
            app_history[0] = current_app;
        }

        // --- DESKTOP LOGIC ---
        if (just_clicked) {
            if (show_alt_tab) { show_alt_tab = false; needs_redraw = true; }
            // Context Menu interaction
            if (context_menu_open) {
                if (mouse_x >= ctx_x && mouse_x <= ctx_x + 120 && mouse_y >= ctx_y && mouse_y <= ctx_y + 22) {
                    generate_wallpaper(); for(int i=0; i<vfs_count; i++) icon_init[i] = false;
                }
                else if (mouse_x >= ctx_x && mouse_x <= ctx_x + 120 && mouse_y >= ctx_y + 23 && mouse_y <= ctx_y + 45) {
                    launch_app(12); // Ayarlar (Settings)
                }
                else if (ctx_target_idx != -1 && mouse_x >= ctx_x && mouse_x <= ctx_x + 120 && mouse_y >= ctx_y + 46 && mouse_y <= ctx_y + 66) {
                    // Rename logic: toggle a mock rename
                    vfs[ctx_target_idx].name[0] = 'F'; vfs[ctx_target_idx].name[1] = 'I'; vfs[ctx_target_idx].name[2] = 'X'; 
                }
                context_menu_open = false; ctx_target_idx = -1; needs_redraw = true;
            }
            // Volume Mixer interaction
            else if (volume_open && mouse_x >= 920 && mouse_x <= 1000 && mouse_y >= 550 && mouse_y <= 700) {
                if (mouse_y >= 570 && mouse_y <= 680) {
                    current_volume = (680 - mouse_y) * 100 / 110;
                    if (current_volume < 0) current_volume = 0; if (current_volume > 100) current_volume = 100;
                    sb16_set_volume(current_volume);
                }
                needs_redraw = true;
            }
            // Taskbar interactions
            else if (mouse_y >= 710) {
                context_menu_open = volume_open = false;
                if (mouse_x >= 130 && mouse_x <= 430) search_bar_focused = true;
                else if (mouse_x >= 20 && mouse_x <= 100) start_menu_open = !start_menu_open;
                else if (mouse_x >= 930 && mouse_x <= 970) volume_open = !volume_open;
                else if (mouse_x >= 450 && mouse_x <= 450 + num_pinned * 50) {
                    int p_idx = (mouse_x - 450) / 50;
                    if (p_idx >= 0 && p_idx < num_pinned) {
                        int v_idx = pinned_apps[p_idx];
                        if (v_idx == 3) current_app = 1; if (v_idx == 6) launch_app(5);
                        if (v_idx == 7) launch_app(8); if (v_idx == 8) current_app = 9;
                    }
                }
                needs_redraw = true;
            }
            // Start Menu interaction
            else if (start_menu_open) {
                int sx = 0, sy = 310, sw = 380;
                if (mouse_x >= sx && mouse_x <= sx + sw && mouse_y >= sy && mouse_y <= sy + 400) {
                    // Left Column (lw=230)
                    if (mouse_x <= sx + 235) {
                        if (mouse_y >= sy + 70 && mouse_y <= sy + 106) current_app = 1; // Bouncing Ball
                        if (mouse_y >= sy + 110 && mouse_y <= sy + 146) current_app = 4; // Terminal
                        if (mouse_y >= sy + 150 && mouse_y <= sy + 186) current_app = 6; // Calculator
                        if (mouse_y >= sy + 190 && mouse_y <= sy + 226) current_app = 13; // Media Player
                        if (mouse_y >= sy + 230 && mouse_y <= sy + 266) launch_app(10); // Clock App
                        if (mouse_y >= sy + 270 && mouse_y <= sy + 306) launch_app(11); // Tic Tac Toe
                        if (mouse_y >= sy + 310 && mouse_y <= sy + 346) { dark_mode_active = !dark_mode_active; generate_wallpaper(); }
                    }
                    // Right Column (rx = sx + 230 + 10 = 240)
                    else if (mouse_x >= sx + 240) {
                        if (mouse_y >= sy + 70 && mouse_y <= sy + 106) { /* Open Documents */ }
                        if (mouse_y >= sy + 110 && mouse_y <= sy + 146) { /* Open Games */ }
                        if (mouse_y >= sy + 150 && mouse_y <= sy + 186) current_app = 12; // Settings
                        if (mouse_y >= sy + 345 && mouse_y <= sy + 395) { outw(0x604, 0x2000); outw(0xB004, 0x2000); } // Shutdown
                    }
                }
                start_menu_open = false; needs_redraw = true;
            }
            // Windows & Icons interaction
            else {
                search_bar_focused = false;
                int top_widx = get_top_win_at(mouse_x, mouse_y);
                if (top_widx != -1) {
                    bring_to_front(top_widx);
                    int w_idx = win_order[win_count - 1]; OSWindow* w = &win_stack[w_idx];
                    // Close Button
                    if (mouse_x >= w->x + w->w - 26 && mouse_x <= w->x + w->w - 4 && mouse_y >= w->y + 2 && mouse_y <= w->y + 24) {
                        for(int j=win_count-1; j>0; j--) { win_stack[j-1] = win_stack[j]; win_order[j-1] = win_order[j]; }
                        win_count--; needs_redraw = true;
                    }
                    // Maximize Button
                    else if (mouse_x >= w->x + w->w - 52 && mouse_x <= w->x + w->w - 30 && mouse_y >= w->y + 2 && mouse_y <= w->y + 24) {
                        if (!w->maximized) {
                            w->old_x = w->x; w->old_y = w->y; w->old_w = w->w; w->old_h = w->h;
                            w->x = 0; w->y = 0; w->w = 1024; w->h = 710; w->maximized = true;
                        } else {
                            w->x = w->old_x; w->y = w->old_y; w->w = w->old_w; w->h = w->old_h; w->maximized = false;
                        }
                        needs_redraw = true;
                    }
                    // Minimize Button
                    else if (mouse_x >= w->x + w->w - 78 && mouse_x <= w->x + w->w - 56 && mouse_y >= w->y + 2 && mouse_y <= w->y + 24) {
                        w->minimized = true; needs_redraw = true;
                    }
                    // Back Button (Folders only)
                    else if (w->folder_id != -1 && mouse_x >= w->x + 5 && mouse_x <= w->x + 35 && mouse_y >= w->y + 32 && mouse_y <= w->y + 56) {
                        if (vfs[w->folder_id].parent_id != -1) {
                            w->folder_id = vfs[w->folder_id].parent_id;
                            needs_redraw = true;
                        }
                    }
                    else if (mouse_y <= w->y + 25) {
                        drag_win_idx = w_idx; is_dragging = true;
                        drag_start_mx = mouse_x; drag_start_my = mouse_y;
                        drag_start_wx = w->x; drag_start_wy = w->y;
                    }
                    else {
                        int cx = w->x + 15, cy = w->y + 45;
                        static int w_last = -1; static int w_dclk = 0;
                        for (int i = 0; i < vfs_count; i++) {
                            if (vfs[i].parent_id == w->folder_id) {
                                if (mouse_x >= cx-20 && mouse_x <= cx+80 && mouse_y >= cy-20 && mouse_y <= cy+80) {
                                    bool is_d = (w_last == i && w_dclk > 0); w_last = i; w_dclk = 40;
                                    if (!is_d) { 
                                        drag_icon_idx = i; idrag_mx = mouse_x; idrag_my = mouse_y;
                                        idrag_ix = cx - 5; idrag_iy = cy - 5; needs_redraw = true; break; 
                                    }
                                    if (vfs[i].type == TYPE_FOLDER) {
                                        if (win_count < MAX_FOLDERS) {
                                            win_stack[win_count].folder_id = i; win_stack[win_count].x = w->x + 30; win_stack[win_count].y = w->y + 30;
                                            win_stack[win_count].w = 400; win_stack[win_count].h = 250; win_order[win_count] = win_count; win_count++;
                                        }
                                    } else {
                                        if (i == 3) current_app = 1;      // Mario (Bouncing Ball)
                                        else if (i == 4 || i == 5) launch_app(4); // Terminal
                                        else if (i == 6) launch_app(5); // Snake
                                        else if (i == 7) launch_app(8); // Mines
                                        else if (i == 8) launch_app(6); // Calculator
                                        else if (i == 9) launch_app(10); // Clock
                                        else if (i == 10) launch_app(11); // TicTac
                                        else if (i == 11) launch_app(13); // Media Player
                                    }
                                    needs_redraw = true; break;
                                }
                                cx += 80; if (cx > w->x + w->w - 60) { cx = w->x + 15; cy += 60; }
                            }
                        }
                    }
                } else {
                    static int d_last = -1; static int d_dclk = 0;
                    for (int i = 0; i < vfs_count; i++) {
                        if (vfs[i].parent_id == 0 && icon_init[i]) {
                            if (mouse_x >= icon_x[i]-5 && mouse_x <= icon_x[i]+45 && mouse_y >= icon_y[i]-5 && mouse_y <= icon_y[i]+45) {
                                bool is_d = (d_last == i && d_dclk > 0); d_last = i; d_dclk = 40;
                                if (!is_d) {
                                    drag_icon_idx = i; idrag_mx = mouse_x; idrag_my = mouse_y;
                                    idrag_ix = icon_x[i]; idrag_iy = icon_y[i]; needs_redraw = true; break;
                                }
                                if (vfs[i].type == TYPE_FOLDER) {
                                    if (win_count < MAX_FOLDERS) {
                                        win_stack[win_count].folder_id = i; win_stack[win_count].app_id = -1;
                                        win_stack[win_count].x = 200 + win_count*30; win_stack[win_count].y = 150 + win_count*30;
                                        win_stack[win_count].w = 400; win_stack[win_count].h = 250;
                                        win_stack[win_count].maximized = false; win_stack[win_count].minimized = false;
                                        win_order[win_count] = win_count; win_count++;
                                    }
                                } else {
                                    if (i == 3) current_app = 1;      
                                    else if (i == 4 || i == 5) launch_app(4); 
                                    else if (i == 6) launch_app(5); 
                                    else if (i == 7) launch_app(8); 
                                    else if (i == 8) launch_app(6); 
                                    else if (i == 9) launch_app(10); 
                                    else if (i == 10) launch_app(11); 
                                    else if (i == 11) launch_app(13); 
                                }
                                needs_redraw = true; break;
                            }
                        }
                    }
                }
            }
        }
        
        if (just_r_clicked) {
            ctx_x = mouse_x; ctx_y = mouse_y;
            if (ctx_x > 904) ctx_x = 904; if (ctx_y > 700) ctx_y = 700;
            
            // Identify target for context menu
            ctx_target_idx = -1;
            for (int i = 0; i < vfs_count; i++) {
                if (vfs[i].parent_id == 0 && icon_init[i]) {
                    if (mouse_x >= icon_x[i]-5 && mouse_x <= icon_x[i]+45 && mouse_y >= icon_y[i]-5 && mouse_y <= icon_y[i]+45) {
                        ctx_target_idx = i; break;
                    }
                }
            }
            context_menu_open = true; needs_redraw = true;
        }

        if (clicked) {
            if (is_dragging && drag_win_idx != -1) {
                win_stack[drag_win_idx].x = drag_start_wx + (mouse_x - drag_start_mx);
                win_stack[drag_win_idx].y = drag_start_wy + (mouse_y - drag_start_my);
                needs_redraw = true;
            }
            if (drag_icon_idx != -1) {
                if (!is_icon_dragging) {
                    int dx = mouse_x - idrag_mx; int dy = mouse_y - idrag_my;
                    if (dx*dx+dy*dy >= 25) is_icon_dragging = true;
                }
                if (is_icon_dragging) {
                    icon_x[drag_icon_idx] = idrag_ix + (mouse_x - idrag_mx);
                    icon_y[drag_icon_idx] = idrag_iy + (mouse_y - idrag_my);
                    needs_redraw = true;
                }
            }
            if (volume_open && mouse_x >= 920 && mouse_y >= 570 && mouse_y <= 680) {
                current_volume = (680 - mouse_y) * 100 / 110;
                if (current_volume < 0) current_volume = 0; if (current_volume > 100) current_volume = 100;
                sb16_set_volume(current_volume); needs_redraw = true;
            }
        } else {
            if (is_icon_dragging && drag_icon_idx != -1) {
                bool dropped = false; int t_win = get_top_win_at(mouse_x, mouse_y);
                if (t_win != -1) {
                    OSWindow* fw = &win_stack[win_order[t_win]];
                    if (mouse_x >= fw->x+5 && mouse_x <= fw->x+fw->w-5 && mouse_y >= fw->y+25 && mouse_y <= fw->y+fw->h-5) {
                        if (drag_icon_idx != fw->folder_id) { vfs[drag_icon_idx].parent_id = fw->folder_id; icon_init[drag_icon_idx]=false; dropped=true; }
                    }
                }
                if (!dropped) {
                    if (mouse_y >= 710) {
                        bool pin = true; for(int p=0; p<num_pinned; p++) if(pinned_apps[p]==drag_icon_idx) pin=false;
                        if(pin && num_pinned < 10) pinned_apps[num_pinned++] = drag_icon_idx;
                    } else {
                        for(int i=0; i<vfs_count; i++) {
                            if (i != drag_icon_idx && vfs[i].parent_id==0 && vfs[i].type==TYPE_FOLDER && icon_init[i]) {
                                if (mouse_x >= icon_x[i]-10 && mouse_x <= icon_x[i]+50 && mouse_y >= icon_y[i]-10 && mouse_y <= icon_y[i]+50) {
                                    vfs[drag_icon_idx].parent_id = i; icon_init[drag_icon_idx]=false; dropped=true; break;
                                }
                            }
                        }
                        if(!dropped) { vfs[drag_icon_idx].parent_id = 0; icon_init[drag_icon_idx]=true; }
                    }
                }
                needs_redraw = true;
            }
            is_dragging = false; drag_win_idx = -1; is_icon_dragging = false; drag_icon_idx = -1;
        }
        
        // System Tick & Animation pacing
        for (volatile int wait = 0; wait < 150000; wait++);
        tick++;
        if (win_count > 0 && tick % 10 == 0) needs_redraw = true;

        if (old_mx != mouse_x || old_my != mouse_y || kb_changed || needs_redraw || start_menu_open) {
            // Force redraw when mouse is moving inside Start Menu area to update highlights
            if (start_menu_open && mouse_x < 380 && mouse_y >= 310 && mouse_y < 710) needs_redraw = true;
            draw_background_patch(0, 0, 1024, 768);
            int p_dx = 20, p_dy = 20;
            for (int i = 0; i < vfs_count; i++) {
                if (vfs[i].parent_id == 0) {
                    if (search_len > 0 && !string_contains(vfs[i].name, search_buffer)) continue; 
                    if (!icon_init[i]) { icon_x[i] = p_dx; icon_y[i] = p_dy; icon_init[i] = true; p_dy += 70; if (p_dy > 600) { p_dy = 20; p_dx += 80; } }
                    if (is_icon_dragging && drag_icon_idx == i) continue;
                    if (vfs[i].type == TYPE_FOLDER) draw_folder_icon(icon_x[i], icon_y[i], vfs[i].name);
                    else draw_file_icon(icon_x[i], icon_y[i], vfs[i].name);
                }
            }
            for (int i = 0; i < win_count; i++) {
                draw_window(win_order[i]);
            }
            draw_taskbar(clock_h, clock_m);
            if (start_menu_open) draw_start_menu(); 
            if (context_menu_open) draw_context_menu();
            if (show_alt_tab) draw_alt_tab_overlay();
            if (volume_open) draw_vol_mixer();
            if (is_icon_dragging && drag_icon_idx != -1) {
                if (vfs[drag_icon_idx].type == TYPE_FOLDER) draw_folder_icon(icon_x[drag_icon_idx], icon_y[drag_icon_idx], vfs[drag_icon_idx].name);
                else draw_file_icon(icon_x[drag_icon_idx], icon_y[drag_icon_idx], vfs[drag_icon_idx].name);
            }
            draw_cursor(mouse_x, mouse_y, 0); swap_buffers();
            old_mx = mouse_x; old_my = mouse_y; kb_changed = false;
        }
    }
}
