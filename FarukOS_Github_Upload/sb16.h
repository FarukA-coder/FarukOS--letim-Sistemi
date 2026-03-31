#ifndef SB16_H
#define SB16_H

#include <stdint.h>
#include <stdbool.h>

void init_sb16(void);
void sb16_play_test_tone(void);
void sb16_play_startup_sound(void);
void sb16_set_volume(int percent);
void sb16_play_buffer(uint8_t* buffer, uint32_t len);

#endif
