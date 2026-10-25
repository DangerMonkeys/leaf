#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace training {
  constexpr size_t PILOT_NAME_BYTES = 40, HELLO_NAME_BYTES = 32;
  // Bounded UTF-8, whitespace-normalized, with no control bytes or split code points.
  inline size_t nameCodepoint(const char* p, uint32_t& code) {
    const uint8_t a = uint8_t(*p);
    if (a < 128) {
      code = a;
      return a ? 1 : 0;
    }
    const size_t n = a >= 0xc2 && a <= 0xdf   ? 2
                     : a >= 0xe0 && a <= 0xef ? 3
                     : a >= 0xf0 && a <= 0xf4 ? 4
                                              : 0;
    if (!n) return 0;
    code = a & ((1U << (7 - n)) - 1);
    for (size_t i = 1; i < n; ++i) {
      const uint8_t b = uint8_t(p[i]);
      if (!b || (b & 0xc0) != 0x80) return 0;
      code = (code << 6) | (b & 63);
    }
    if ((n == 2 && code < 128) || (n == 3 && code < 2048) || (n == 4 && code < 65536) ||
        (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff)
      return 0;
    return n;
  }
  inline void copyPilotName(char* out, const char* text, size_t limit = PILOT_NAME_BYTES) {
    size_t used = 0;
    bool space = false;
    if (!text) text = "";
    while (*text) {
      uint32_t code = 0;
      size_t n = nameCodepoint(text, code);
      if (!n) {
        code = '?';
        n = 1;
      }
      if (code <= 32 || (code >= 127 && code <= 159)) {
        space = used > 0;
        text += n;
        continue;
      }
      const size_t bytes = code == '?' ? 1 : n;
      if (used + bytes + unsigned(space) > limit) break;
      if (space) out[used++] = ' ';
      if (code == '?')
        out[used++] = '?';
      else {
        memcpy(out + used, text, n);
        used += n;
      }
      space = false;
      text += n;
    }
    out[used] = 0;
  }
  inline bool validPilotName(const char* text, size_t limit = PILOT_NAME_BYTES) {
    size_t n = 0;
    while (n <= limit && text[n]) ++n;
    if (n > limit) return false;
    char normalized[PILOT_NAME_BYTES + 1];
    copyPilotName(normalized, text, limit);
    return !strcmp(text, normalized);
  }
  // Leaf's bitmap fonts use custom 8-bit glyphs: render plain letters, not UTF-8 byte glyphs.
  inline void pilotNameForDisplay(char* out, const char* text) {
    size_t used = 0;
    while (*text && used < PILOT_NAME_BYTES) {
      uint32_t code = 0;
      const size_t n = nameCodepoint(text, code);
      if (!n) {
        out[used++] = '?';
        ++text;
        continue;
      }
      char c = code < 127 ? char(code) : '?';
      if (code >= 0xc0 && code <= 0xff) {
        const char* latin = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
        c = latin[code - 0xc0];
      }
      out[used++] = c;
      text += n;
    }
    out[used] = 0;
  }
}  // namespace training
