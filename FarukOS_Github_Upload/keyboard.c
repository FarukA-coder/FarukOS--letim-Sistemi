#include "io.h"
#include "pic.h"
#include "keyboard.h"
#include <stdint.h>
#include <stdbool.h>

char search_buffer[256] = {0};
int search_len = 0;
char notepad_buffer[1024] = {0};
int notepad_len = 0;
char cmd_buffer[256] = {0};
int cmd_len = 0;
bool cmd_entered = false;
int current_app = 0; 
volatile bool kb_changed = true; 
volatile char last_key = 0; 
bool search_bar_focused = false;

// Login system
int boot_phase = 0;         // 0=login, 1=desktop
bool is_guest = false;
char login_buffer[32] = {0};
int  login_len = 0;

bool shift_pressed = false;
bool caps_lock = false;

const char kbd_us[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0
};

const char kbd_us_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0
};

void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);
    
    // Check modifiers
    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = true; goto end; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = false; goto end; }
    if (scancode == 0x3A) { caps_lock = !caps_lock; goto end; } // CapsLock Make

    if (!(scancode & 0x80)) { 
        // ESC key = scancode 0x01: always go back to desktop
        if (scancode == 0x01) {
            if (current_app != 0) { current_app = 0; kb_changed = true; }
            goto end;
        }
        if (scancode < 128) {
            char c;
            if (shift_pressed) c = kbd_us_shift[scancode];
            else c = kbd_us[scancode];
            
            // Caps lock specifically inverses letter cases independent of shift
            if (caps_lock) {
                if (c >= 'a' && c <= 'z') c -= 32;
                else if (c >= 'A' && c <= 'Z') c += 32;
            }
            
            last_key = c; // Export to games
            
            // During login phase, capture into login_buffer
            if (boot_phase == 0) {
                if (c == '\b') {
                    if (login_len > 0) { login_len--; login_buffer[login_len] = '\0'; kb_changed = true; }
                } else if (c == '\n') {
                    kb_changed = true; // Signal kernel to check password
                } else if (c >= ' ' && c < 127 && login_len < 30) {
                    login_buffer[login_len++] = c; login_buffer[login_len] = '\0'; kb_changed = true;
                }
                goto end;
            }
            
            if (current_app == 2) {
                if (c == '\b') {
                    if (notepad_len > 0) { notepad_len--; notepad_buffer[notepad_len] = '\0'; kb_changed = true; }
                } else if (c >= ' ' || c == '\n') {
                    if (notepad_len < 1000) { notepad_buffer[notepad_len++] = c; notepad_buffer[notepad_len] = '\0'; kb_changed = true; }
                }
            } else if (current_app == 4) {
                if (c == '\b') {
                    if (cmd_len > 0) { cmd_len--; cmd_buffer[cmd_len] = '\0'; kb_changed = true; }
                } else if (c == '\n') {
                    cmd_entered = true; kb_changed = true;
                } else if (c >= ' ' && c <= '~') {
                    if (cmd_len < 75) { cmd_buffer[cmd_len++] = c; cmd_buffer[cmd_len] = '\0'; kb_changed = true; }
                }
            } else if (current_app == 5) {
                // Snake game simply eats 'last_key', handled in kernel.c
            } else {
                // Only type into search if user has clicked the search bar
                if (search_bar_focused) {
                    if (c == '\b') {
                        if (search_len > 0) { search_len--; search_buffer[search_len] = '\0'; kb_changed = true; }
                    } else if (c == '\n' || c == 27) { // Enter or ESC clears focus
                        search_bar_focused = false; kb_changed = true;
                    } else if (c >= ' ' && c <= '~') {
                        if (search_len < 250) { search_buffer[search_len++] = c; search_buffer[search_len] = '\0'; kb_changed = true; }
                    }
                }
            }
        }
    }
end:
    pic_send_eoi(1);
}
