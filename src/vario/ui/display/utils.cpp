#include "ui/display/utils.h"

#include <U8g2lib.h>
#include <stddef.h>
#include <string.h>
#include <cstdint>

#include "ui/display/display.h"

namespace {
  bool evenCircleContainsPixel(int16_t px, int16_t py, int16_t cx2, int16_t cy2, int16_t diameter) {
    const int32_t dx2 = int32_t(px) * 2 + 1 - cx2;
    const int32_t dy2 = int32_t(py) * 2 + 1 - cy2;
    const int32_t r2 = int32_t(diameter) * diameter;
    return dx2 * dx2 + dy2 * dy2 <= r2;
  }

  int16_t floorSqrt(int32_t n) {
    int16_t root = 0;
    while (static_cast<int32_t>(root + 1) * (root + 1) <= n) {
      root++;
    }
    return root;
  }

  bool circleRowSpanD(int16_t centerX, int16_t centerY, uint8_t diameter, int16_t py,
                      int16_t& start, int16_t& end) {
    const int16_t centerX2 = (diameter & 1) ? 2 * centerX + 1 : 2 * centerX;
    const int16_t centerY2 = (diameter & 1) ? 2 * centerY + 1 : 2 * centerY;
    const int32_t dy2 = int32_t(py) * 2 + 1 - centerY2;
    const int32_t remaining = int32_t(diameter) * diameter - dy2 * dy2;
    if (remaining < 0) return false;

    int16_t maxDx2 = floorSqrt(remaining);
    const uint8_t dxParity = (centerX2 & 1) ? 0 : 1;
    if ((maxDx2 & 1) != dxParity) maxDx2--;
    if (maxDx2 < 0) return false;

    start = (centerX2 - maxDx2 - 1) / 2;
    end = (centerX2 + maxDx2 - 1) / 2;
    return end >= start;
  }

  void drawHLineIfOrdered(int16_t start, int16_t y, int16_t end) {
    if (end >= start) u8g2.drawHLine(start, y, end - start + 1);
  }
}  // namespace

void drawCircleD(int16_t centerX, int16_t centerY, uint8_t diameter) {
  if (diameter == 0) return;

  if (diameter & 1) {
    const uint8_t radius = diameter / 2;
    u8g2.drawCircle(centerX, centerY, radius);
    return;
  }

  const int16_t top = centerY - diameter / 2;
  for (int16_t py = top; py < top + diameter; ++py) {
    int16_t start;
    int16_t end;
    if (!circleRowSpanD(centerX, centerY, diameter, py, start, end)) continue;

    int16_t prevStart;
    int16_t prevEnd;
    const bool hasPrev = circleRowSpanD(centerX, centerY, diameter, py - 1, prevStart, prevEnd);
    int16_t nextStart;
    int16_t nextEnd;
    const bool hasNext = circleRowSpanD(centerX, centerY, diameter, py + 1, nextStart, nextEnd);

    u8g2.drawPixel(start, py);
    if (end != start) u8g2.drawPixel(end, py);

    if (!hasPrev) {
      drawHLineIfOrdered(start, py, end);
    } else {
      drawHLineIfOrdered(start, py, prevStart - 1);
      drawHLineIfOrdered(prevEnd + 1, py, end);
    }

    if (!hasNext) {
      drawHLineIfOrdered(start, py, end);
    } else {
      drawHLineIfOrdered(start, py, nextStart - 1);
      drawHLineIfOrdered(nextEnd + 1, py, end);
    }
  }
}

void drawDiscD(int16_t centerX, int16_t centerY, uint8_t diameter) {
  if (diameter == 0) return;

  if (diameter & 1) {
    const uint8_t radius = diameter / 2;
    u8g2.drawDisc(centerX, centerY, radius);
    return;
  }

  const int16_t left = centerX - diameter / 2;
  const int16_t top = centerY - diameter / 2;
  const int16_t centerX2 = 2 * centerX;
  const int16_t centerY2 = 2 * centerY;

  for (int16_t py = top; py < top + diameter; ++py) {
    int16_t start = left + diameter;
    int16_t end = left - 1;
    for (int16_t px = left; px < left + diameter; ++px) {
      if (evenCircleContainsPixel(px, py, centerX2, centerY2, diameter)) {
        if (px < start) start = px;
        end = px;
      }
    }
    if (end >= start) u8g2.drawHLine(start, py, end - start + 1);
  }
}

void drawRingD(int16_t centerX, int16_t centerY, uint8_t outerDiameter, uint8_t thickness) {
  if (outerDiameter == 0 || thickness == 0) return;

  const uint8_t innerDiameter = outerDiameter > 2 * thickness ? outerDiameter - 2 * thickness : 0;
  const int16_t top = centerY - outerDiameter / 2;
  for (int16_t py = top; py < top + outerDiameter; ++py) {
    int16_t outerStart;
    int16_t outerEnd;
    if (!circleRowSpanD(centerX, centerY, outerDiameter, py, outerStart, outerEnd)) continue;

    int16_t innerStart;
    int16_t innerEnd;
    const bool hasInner = innerDiameter > 0 &&
                          circleRowSpanD(centerX, centerY, innerDiameter, py, innerStart, innerEnd);
    if (!hasInner) {
      drawHLineIfOrdered(outerStart, py, outerEnd);
      continue;
    }

    drawHLineIfOrdered(outerStart, py, innerStart - 1);
    drawHLineIfOrdered(innerEnd + 1, py, outerEnd);
  }
}

/**
 * Print `str` into a box whose top-left corner is (x,y).
 * - If width==0, lines are only
 * broken on '\n' (no width constraint).
 * - If height==0, vertical space is unbounded.
 * - Wrap whole words when they fit; otherwise wrap by the maximum number of characters that fit.
 * - Treat '\n' as a hard line break; ignore '\r'.
 * - Returns the index into `str` just beyond the last character that was printed.
 */
size_t printMultiLine(const char* str, int16_t x, int16_t y, uint16_t width, uint16_t height) {
  if (!str) return 0;

  const size_t n = strlen(str);
  const bool limitWidth = (width > 0);
  const bool limitHeight = (height > 0);

  // Recommended vertical metrics: baseline at top + ascent, line step = ascent - descent
  const int16_t ascent = u8g2.getAscent();
  const int16_t descent = u8g2.getDescent();    // usually negative
  const int16_t lineHeight = ascent - descent;  // e.g. 12 - (-2) = 14
  int16_t baselineY = y + ascent;

  if (limitHeight && baselineY > (int32_t)y + height) {
    return 0;  // no room for even one line
  }

  // Small helper: copy a slice [s,e) from `str` into `dst`, stripping '\r'.
  auto copy_slice_no_cr = [](char* dst, size_t dst_cap, const char* src, size_t s,
                             size_t e) -> size_t {
    size_t out = 0;
    for (size_t i = s; i < e && out + 1 < dst_cap; ++i) {
      char c = src[i];
      if (c == '\r') continue;
      dst[out++] = c;
    }
    dst[out] = '\0';
    return out;
  };

  size_t i = 0;
  size_t lastPrintedIdx = 0;
  bool printedAnything = false;

  while (i < n) {
    if (printedAnything) {
      baselineY += lineHeight;
      if (limitHeight && baselineY > (int32_t)y + height) break;
    }

    // Width-unlimited: print until newline or end (ignoring CRs)
    if (!limitWidth) {
      // Skip leading CRs on this line
      while (i < n && str[i] == '\r') ++i;

      size_t j = i;
      while (j < n && str[j] != '\n') ++j;

      u8g2.setCursor(x, baselineY);
      for (size_t k = i; k < j; ++k) {
        if (str[k] == '\r') continue;
        u8g2.write((uint8_t)str[k]);
      }

      if (j > i) {
        lastPrintedIdx = j - 1;
        printedAnything = true;
      }
      i = (j < n && str[j] == '\n') ? (j + 1) : j;
      continue;
    }

    // -------- Width-limited path: build a line by *measuring exactly what we'll print* ----------
    // Skip CRs and leading spaces/tabs at the start of a line
    while (i < n && (str[i] == '\r' || str[i] == ' ' || str[i] == '\t')) ++i;

    // Hard break on newline (empty visual line)
    if (i < n && str[i] == '\n') {
      ++i;
      continue;
    }
    if (i >= n) break;

    // We'll accumulate the exact characters that will be printed into this line buffer.
    // Keep it reasonably large; with a 96px width and tiny fonts, 256 is plenty.
    static constexpr size_t LINEBUF_MAX = 256;
    char lineBuf[LINEBUF_MAX];
    size_t lineLen = 0;  // current length in lineBuf
    lineBuf[0] = '\0';

    size_t scan = i;
    size_t lineEndOriginal =
        i;  // index right after the last character from `str` that we decided to print

    auto fits_width = [&](const char* s) -> bool {
      if (!limitWidth) return true;
      int w = u8g2.getStrWidth(s);
      return w <= (int)width;
    };

    // Attempt to add as many words (with a single inter-word space) as fit
    bool firstWord = true;
    while (scan < n && str[scan] != '\n') {
      // Find next word [ws,we): a run of non-space/tab/CR/newline
      size_t ws = scan;
      // skip inter-word whitespace (space/tab/CR) but not newline
      while (ws < n && (str[ws] == ' ' || str[ws] == '\t' || str[ws] == '\r')) ++ws;
      if (ws >= n || str[ws] == '\n') break;

      size_t we = ws;
      while (we < n) {
        char c = str[we];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') break;
        ++we;
      }

      // Build a trial buffer = current line + (space if needed) + word
      char trial[LINEBUF_MAX];
      // start with existing line
      size_t tlen = 0;
      for (size_t q = 0; q < lineLen && tlen + 1 < LINEBUF_MAX; ++q) trial[tlen++] = lineBuf[q];

      if (!firstWord && tlen + 1 < LINEBUF_MAX) trial[tlen++] = ' ';
      // append word (without CR)
      for (size_t q = ws; q < we && tlen + 1 < LINEBUF_MAX; ++q) {
        char c = str[q];
        if (c == '\r') continue;
        trial[tlen++] = c;
      }
      trial[tlen] = '\0';

      if (fits_width(trial)) {
        // Accept whole word
        memcpy(lineBuf, trial, tlen + 1);
        lineLen = tlen;
        lineEndOriginal = we;

        // Advance scan to after this word and skip trailing whitespace (except newline)
        scan = we;
        while (scan < n) {
          char c = str[scan];
          if (c == '\r' || c == ' ' || c == '\t') {
            ++scan;
            continue;
          }
          break;
        }
        firstWord = false;
        continue;
      } else {
        // Word doesn't fit. If it's the first on the line, split it by characters to the max width.
        if (firstWord) {
          // Try to add as many characters from [ws,we) as fit.
          size_t best = 0;
          // We'll grow trial one character at a time (still efficient for tiny lines on
          // microcontrollers).
          for (size_t take = 1; ws + take <= we; ++take) {
            // trial = (line is empty, maybe) + first 'take' chars of the word
            tlen = 0;
            if (!firstWord && tlen + 1 < LINEBUF_MAX) trial[tlen++] = ' ';
            for (size_t q = ws; q < ws + take && tlen + 1 < LINEBUF_MAX; ++q) {
              char c = str[q];
              if (c == '\r') continue;
              trial[tlen++] = c;
            }
            trial[tlen] = '\0';
            if (fits_width(trial))
              best = take;
            else
              break;
          }
          if (best > 0) {
            // Commit the partial word
            tlen = 0;
            if (!firstWord && tlen + 1 < LINEBUF_MAX) trial[tlen++] = ' ';
            for (size_t q = ws; q < ws + best && tlen + 1 < LINEBUF_MAX; ++q) {
              char c = str[q];
              if (c == '\r') continue;
              trial[tlen++] = c;
            }
            trial[tlen] = '\0';
            memcpy(lineBuf, trial, tlen + 1);
            lineLen = tlen;
            lineEndOriginal = ws + best;
          }
        }
        // Finish this line (either we split first word or nothing else fits)
        break;
      }
    }

    // If nothing fit (extremely narrow width), try to place at least one printable char
    if (lineLen == 0) {
      if (i < n && str[i] != '\n') {
        // try the next non-CR char only
        char one[2] = {0, 0};
        size_t j = i;
        while (j < n && str[j] == '\r') ++j;
        if (j < n && str[j] != '\n') {
          one[0] = str[j];
          if (!limitWidth || fits_width(one)) {
            lineBuf[0] = one[0];
            lineBuf[1] = '\0';
            lineLen = 1;
            lineEndOriginal = j + 1;
          }
        }
      }
      if (lineLen == 0) break;  // truly nothing can be printed
    }

    // Print the built line
    u8g2.setCursor(x, baselineY);
    u8g2.print(lineBuf);

    if (lineLen > 0) {
      lastPrintedIdx = lineEndOriginal - 1;
      printedAnything = true;
    }

    // Advance source index to just after what we printed
    i = lineEndOriginal;
    // Consume a hard newline if it's next
    if (i < n && str[i] == '\n') ++i;
  }

  return printedAnything ? (lastPrintedIdx + 1) : 0;
}
