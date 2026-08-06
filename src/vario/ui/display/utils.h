#pragma once

#include <stddef.h>
#include <cstdint>

size_t printMultiLine(const char* str, int16_t x, int16_t y, uint16_t width = 0,
                      uint16_t height = 0);

void drawCircleD(int16_t centerX, int16_t centerY, uint8_t diameter);
void drawDiscD(int16_t centerX, int16_t centerY, uint8_t diameter);
void drawRingD(int16_t centerX, int16_t centerY, uint8_t outerDiameter, uint8_t thickness);
