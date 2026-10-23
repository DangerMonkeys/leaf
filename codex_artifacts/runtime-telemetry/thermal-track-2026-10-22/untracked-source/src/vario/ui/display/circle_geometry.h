#pragma once

#include <array>
#include <cstdint>

namespace circle_geometry {
  // Integer extents of x*x + y*y <= radiusSq, visiting consecutive signed y offsets.
  // Each horizontal step is reused by the next row; no square roots or pixel scans.
  class RowExtents {
   public:
    RowExtents(int32_t radiusSq, int16_t firstY)
        : remaining_(radiusSq - int32_t(firstY) * firstY), y_(firstY) {}

    int16_t next() {
      int16_t result = -1;
      if (remaining_ >= 0) {
        while (extentSq_ > remaining_) {
          extentSq_ -= 2 * extent_ - 1;
          --extent_;
        }
        while (extentSq_ + 2 * extent_ + 1 <= remaining_) {
          extentSq_ += 2 * extent_ + 1;
          ++extent_;
        }
        result = extent_;
      }
      remaining_ -= 2 * y_ + 1;
      ++y_;
      return result;
    }

   private:
    int32_t remaining_;
    int32_t extentSq_ = 0;
    int16_t y_;
    int16_t extent_ = 0;
  };

  // Pixel centers of an even-diameter circle are half a pixel from its center.
  // Fixed clip geometry can be evaluated at compile time, storing one byte per
  // half-row. Mirroring gives both horizontal bounds without per-pixel arithmetic.
  template <uint8_t diameter>
  class EvenCircleClip {
    static_assert(diameter > 0 && !(diameter & 1), "Clip diameter must be positive and even");

   public:
    constexpr EvenCircleClip(int16_t centerX, int16_t centerY)
        : centerX_(centerX), centerY_(centerY) {
      int16_t dx2 = diameter - 1;
      for (int16_t row = 0; row < diameter / 2; ++row) {
        const int16_t dy2 = 2 * row + 1;
        while (int32_t(dx2) * dx2 + int32_t(dy2) * dy2 > int32_t(diameter) * diameter) dx2 -= 2;
        halfWidths_[row] = (dx2 + 1) / 2;
      }
    }

    bool contains(int16_t x, int16_t y) const {
      const uint8_t width = halfWidth(y);
      return width && x >= centerX_ - width && x < centerX_ + width;
    }

    bool clipSpan(int16_t y, int16_t& start, int16_t& end) const {
      const uint8_t width = halfWidth(y);
      if (!width) return false;
      if (start < centerX_ - width) start = centerX_ - width;
      if (end >= centerX_ + width) end = centerX_ + width - 1;
      return start <= end;
    }

   private:
    uint8_t halfWidth(int16_t y) const {
      const int32_t offset = int32_t(y) - centerY_;
      if (offset < -int16_t(diameter / 2) || offset >= diameter / 2) return 0;
      return halfWidths_[offset < 0 ? -offset - 1 : offset];
    }

    int16_t centerX_;
    int16_t centerY_;
    std::array<uint8_t, diameter / 2> halfWidths_{};
  };
}  // namespace circle_geometry
