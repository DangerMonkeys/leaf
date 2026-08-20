// A dependency-free PNG encoder for screenshots.
//
// Stored (uncompressed) deflate blocks: a 96x192 screen is tiny, so the size never matters and
// this stays free of a zlib dependency inside the container.

#include <stdint.h>
#include <string.h>

#include <array>
#include <vector>

#include "display_capture.h"

namespace sim {

  namespace {

    const uint32_t* crcTable() {
      static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; n++) {
          uint32_t c = n;
          for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
          t[n] = c;
        }
        return t;
      }();
      return table.data();
    }

    uint32_t crc32Of(const uint8_t* data, size_t length) {
      const uint32_t* table = crcTable();
      uint32_t c = 0xFFFFFFFFu;
      for (size_t i = 0; i < length; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
      return c;
    }

    uint32_t adler32Of(const uint8_t* data, size_t length) {
      uint32_t a = 1;
      uint32_t b = 0;
      for (size_t i = 0; i < length; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
      }
      return (b << 16) | a;
    }

    void pushBE32(std::vector<uint8_t>& out, uint32_t value) {
      out.push_back((uint8_t)(value >> 24));
      out.push_back((uint8_t)(value >> 16));
      out.push_back((uint8_t)(value >> 8));
      out.push_back((uint8_t)value);
    }

    void pushChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
      pushBE32(out, (uint32_t)data.size());
      const size_t crcStart = out.size();
      out.insert(out.end(), type, type + 4);
      out.insert(out.end(), data.begin(), data.end());
      pushBE32(out, crc32Of(out.data() + crcStart, out.size() - crcStart) ^ 0xFFFFFFFFu);
    }

    std::vector<uint8_t> deflateStored(const std::vector<uint8_t>& raw) {
      std::vector<uint8_t> out;
      out.push_back(0x78);
      out.push_back(0x01);

      size_t offset = 0;
      while (offset < raw.size()) {
        const size_t remaining = raw.size() - offset;
        const uint16_t blockSize = (uint16_t)(remaining > 65535 ? 65535 : remaining);
        const bool finalBlock = (offset + blockSize) >= raw.size();

        out.push_back(finalBlock ? 0x01 : 0x00);
        out.push_back((uint8_t)(blockSize & 0xFF));
        out.push_back((uint8_t)(blockSize >> 8));
        const uint16_t inverted = (uint16_t)~blockSize;
        out.push_back((uint8_t)(inverted & 0xFF));
        out.push_back((uint8_t)(inverted >> 8));
        out.insert(out.end(), raw.begin() + offset, raw.begin() + offset + blockSize);
        offset += blockSize;
      }

      pushBE32(out, adler32Of(raw.data(), raw.size()));
      return out;
    }

  }  // namespace

  std::string encodePng(const Frame& frame, int scale) {
    if (frame.width <= 0 || frame.height <= 0) return std::string();
    if (scale < 1) scale = 1;

    const int width = frame.width * scale;
    const int height = frame.height * scale;

    // The panel's colours, so a screenshot reads like the device rather than like a bitmap.
    const uint8_t on[3] = {0x21, 0x25, 0x2A};
    const uint8_t off[3] = {0xC7, 0xCC, 0xC1};

    std::vector<uint8_t> raw;
    raw.reserve((size_t)height * (1 + (size_t)width * 3));
    for (int y = 0; y < height; y++) {
      raw.push_back(0);  // filter type: none
      const int sourceY = y / scale;
      for (int x = 0; x < width; x++) {
        const uint8_t lit = frame.pixels[(size_t)sourceY * frame.width + (x / scale)];
        const uint8_t* colour = lit ? on : off;
        raw.push_back(colour[0]);
        raw.push_back(colour[1]);
        raw.push_back(colour[2]);
      }
    }

    std::vector<uint8_t> png;
    const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    png.insert(png.end(), signature, signature + 8);

    std::vector<uint8_t> ihdr;
    pushBE32(ihdr, (uint32_t)width);
    pushBE32(ihdr, (uint32_t)height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // truecolour RGB
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pushChunk(png, "IHDR", ihdr);
    pushChunk(png, "IDAT", deflateStored(raw));
    pushChunk(png, "IEND", std::vector<uint8_t>());

    return std::string((const char*)png.data(), png.size());
  }

}  // namespace sim
