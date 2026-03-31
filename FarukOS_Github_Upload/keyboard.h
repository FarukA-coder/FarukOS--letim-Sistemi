#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdbool.h>

extern char search_buffer[256];
extern int search_len;
extern char notepad_buffer[1024];
extern int notepad_len;
extern char cmd_buffer[256];
extern int cmd_len;
extern bool cmd_entered;
extern int current_app;
extern volatile bool kb_changed;
extern volatile char last_key;
extern bool search_bar_focused;
extern int boot_phase;
extern bool is_guest;
extern char login_buffer[32];
extern int login_len;

void keyboard_handler(void);

#endif
