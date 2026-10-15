#pragma once
#include <stdint.h>
#include <math.h>

inline uint8_t connectionPulseLevel(uint32_t elapsedMs, uint8_t maximum) {
  const uint32_t periodMs = 3000;
  float phase = (float)(elapsedMs % periodMs) / periodMs;
  float wave = 0.5f - 0.5f * cosf(6.28318530718f * phase);
  return (uint8_t)lroundf(wave * maximum);
}
