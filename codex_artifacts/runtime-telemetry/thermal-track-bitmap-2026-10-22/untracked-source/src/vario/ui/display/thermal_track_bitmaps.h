#pragma once

#include <array>
#include <cstdint>

namespace thermal_track_bitmaps {
  constexpr int MAP_X = 48;
  constexpr int MAP_Y = 124;
  constexpr int MAP_DIAMETER = 96;
  constexpr int OVERLAY_TOP = 68;
  constexpr int OVERLAY_HEIGHT = 112;
  // The legacy 128-pixel-wide display also needs to erase marker overhang at x=96..102.
  constexpr int OVERLAY_WIDTH = 104;

  constexpr bool inside(int x, int y, int diameter) {
    const int dx = 2 * (x - MAP_X) + 1;
    const int dy = 2 * (y - MAP_Y) + 1;
    return dx * dx + dy * dy <= diameter * diameter;
  }

  constexpr bool ring(int x, int y, int diameter) {
    return inside(x, y, diameter) && (!inside(x - 1, y, diameter) || !inside(x + 1, y, diameter) ||
                                      !inside(x, y - 1, diameter) || !inside(x, y + 1, diameter));
  }

  struct Overlay {
    std::array<uint8_t, OVERLAY_WIDTH / 8 * OVERLAY_HEIGHT> keep{};
    std::array<uint8_t, OVERLAY_WIDTH / 8 * OVERLAY_HEIGHT> black{};
  };

  // Bake black range rings, white outside-map pixels and a transparent interior into
  // two native framebuffer bitplanes. No circle calculations occur while drawing.
  template <bool reverse>
  constexpr Overlay makeOverlay() {
    Overlay result{};
    for (int page = 0; page < OVERLAY_WIDTH / 8; ++page) {
      for (int column = 0; column < OVERLAY_HEIGHT; ++column) {
        const int y = OVERLAY_TOP + (reverse ? column : OVERLAY_HEIGHT - 1 - column);
        const int index = page * OVERLAY_HEIGHT + column;
        for (int bit = 0; bit < 8; ++bit) {
          const int x = reverse ? OVERLAY_WIDTH - 1 - (8 * page + bit) : 8 * page + bit;
          if (inside(x, y, MAP_DIAMETER)) result.keep[index] |= 1 << bit;
          if (ring(x, y, 50) || ring(x, y, 74) || ring(x, y, 96)) result.black[index] |= 1 << bit;
        }
      }
    }
    return result;
  }

  template <int width, int height>
  struct Sprite {
    static_assert(width <= 16, "Sprite rows must fit in 16 bits");
    std::array<uint16_t, height> black{};
    std::array<uint16_t, height> white{};
    std::array<uint16_t, height> reverseBlack{};
    std::array<uint16_t, height> reverseWhite{};

    constexpr void set(int x, int y, bool ink) {
      (ink ? black : white)[y] |= uint16_t(1) << x;
      (ink ? reverseBlack : reverseWhite)[y] |= uint16_t(1) << (width - 1 - x);
    }

    constexpr bool contains(int x, int y) const {
      return x >= 0 && x < width && y >= 0 && y < height && (black[y] & (uint16_t(1) << x));
    }
  };

  constexpr bool softDisc(int x, int y, int radius) {
    return x * x + y * y <= radius * (radius + 1);
  }

  constexpr bool softRing(int x, int y, int radius) {
    return softDisc(x, y, radius) && (radius == 0 || x * x + y * y > radius * (radius - 1));
  }

  constexpr Sprite<9, 9> makeMarker(int quality) {
    Sprite<9, 9> result{};
    for (int y = 0; y < 9; ++y) {
      for (int x = 0; x < 9; ++x) {
        const int dx = x - 4, dy = y - 4;
        if (quality == 4 && dx == 0 && dy == 0) {
          result.set(x, y, false);
        } else if (quality >= 4 ? softDisc(dx, dy, 4)
                                : softRing(dx, dy, 4) || (quality >= 2 && softRing(dx, dy, 3)) ||
                                      (quality >= 3 && softRing(dx, dy, 2))) {
          result.set(x, y, true);
        }
      }
    }
    return result;
  }

  constexpr Sprite<13, 13> makeSelection(bool filled) {
    Sprite<13, 13> result{};
    for (int y = 0; y < 13; ++y)
      for (int x = 0; x < 13; ++x)
        if (filled ? softDisc(x - 6, y - 6, 6) : softRing(x - 6, y - 6, 6)) result.set(x, y, true);
    return result;
  }

  constexpr std::array<Sprite<9, 9>, 5> markers = {makeMarker(1), makeMarker(2), makeMarker(3),
                                                   makeMarker(4), makeMarker(5)};
  constexpr auto selection = makeSelection(false);
  constexpr auto selectionDisc = makeSelection(true);

  // U8g2's full-buffer R1 and R3 displays store eight logical horizontal pixels per
  // byte. Clip only to the rectangular screen here; the outer bitmap erases overhang.
  class Canvas {
   public:
    Canvas(uint8_t* buffer, int stride, int width, int height, bool reverse)
        : buffer_(buffer), stride_(stride), width_(width), height_(height), reverse_(reverse) {}

    template <int width, int height>
    void draw(int x, int y, const Sprite<width, height>& sprite) const {
      int first = reverse_ ? width_ - x - width : x;
      if (first >= width_ || first + width <= 0) return;
      const int crop = first < 0 ? -first : 0;
      if (first < 0) first = 0;
      const int bits = width_ - first < width - crop ? width_ - first : width - crop;
      const uint32_t visible = (uint32_t(1) << bits) - 1;
      const int shift = first & 7;
      for (int row = 0; row < height; ++row) {
        const int py = y + row;
        if (py < 0 || py >= height_) continue;
        uint32_t black =
            (((reverse_ ? sprite.reverseBlack[row] : sprite.black[row]) >> crop) & visible)
            << shift;
        uint32_t white =
            (((reverse_ ? sprite.reverseWhite[row] : sprite.white[row]) >> crop) & visible)
            << shift;
        int target = (first / 8) * stride_ + (reverse_ ? py : height_ - 1 - py);
        while (black || white) {
          buffer_[target] = (buffer_[target] & ~uint8_t(white)) | uint8_t(black);
          black >>= 8;
          white >>= 8;
          target += stride_;
        }
      }
    }

    void pixel(int x, int y) const {
      if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
      const int bit = reverse_ ? width_ - 1 - x : x;
      buffer_[(bit / 8) * stride_ + (reverse_ ? y : height_ - 1 - y)] |= 1 << (bit & 7);
    }

    void overlay(const Overlay& bitmap) const {
      const int pages = width_ / 8 < OVERLAY_WIDTH / 8 ? width_ / 8 : OVERLAY_WIDTH / 8;
      const int firstPage = reverse_ && width_ > OVERLAY_WIDTH ? (width_ - OVERLAY_WIDTH) / 8 : 0;
      const int bitmapPage = reverse_ && width_ < OVERLAY_WIDTH ? (OVERLAY_WIDTH - width_) / 8 : 0;
      const int firstColumn = reverse_ ? OVERLAY_TOP : height_ - OVERLAY_TOP - OVERLAY_HEIGHT;
      for (int page = 0; page < pages; ++page) {
        uint8_t* target = buffer_ + (firstPage + page) * stride_ + firstColumn;
        const uint8_t* keep = bitmap.keep.data() + (page + bitmapPage) * OVERLAY_HEIGHT;
        const uint8_t* black = bitmap.black.data() + (page + bitmapPage) * OVERLAY_HEIGHT;
        for (int column = 0; column < OVERLAY_HEIGHT; ++column)
          target[column] = (target[column] & keep[column]) | black[column];
      }
    }

   private:
    uint8_t* buffer_;
    int stride_;
    int width_;
    int height_;
    bool reverse_;
  };
}  // namespace thermal_track_bitmaps
