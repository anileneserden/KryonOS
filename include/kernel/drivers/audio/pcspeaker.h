#ifndef PCSPEAKER_H
#define PCSPEAKER_H

#include <stdint.h>

void pcspeaker_play(uint32_t freq);
void pcspeaker_stop(void);
void pcspeaker_beep(uint32_t freq, uint32_t duration_ms);

#endif // PCSPEAKER_H