#pragma once
#include <stdint.h>

enum class Sound : uint8_t { Boot, Click, PrintStart, PrintFinish, PrintFailed };

void audioBegin();
void audioPlay(Sound s);
void audioSetVolume(uint8_t percent);
uint8_t audioGetVolume();
