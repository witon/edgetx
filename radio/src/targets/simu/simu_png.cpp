/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * Minimal PNG writer (RGB8, zlib stored blocks). The simulator tree does
 * not ship stb_image_write.h, so screenshots do not depend on it.
 */

#include "simu_png.h"

#include <stdio.h>
#include <string.h>

#include <vector>

namespace {

uint32_t crc32(const uint8_t* data, size_t len)
{
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

uint32_t adler32(const uint8_t* data, size_t len)
{
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < len; i++) {
    a = (a + data[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

void put32(std::vector<uint8_t>& out, uint32_t v)
{
  out.push_back((uint8_t)(v >> 24));
  out.push_back((uint8_t)(v >> 16));
  out.push_back((uint8_t)(v >> 8));
  out.push_back((uint8_t)v);
}

void chunk(std::vector<uint8_t>& out, const char* type, const uint8_t* data, size_t len)
{
  put32(out, (uint32_t)len);
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data, data + len);
  uint32_t c = crc32(&out[start], 4 + len);
  put32(out, c);
}

void storedZlib(std::vector<uint8_t>& out, const uint8_t* data, size_t len)
{
  out.push_back(0x78);
  out.push_back(0x01);
  size_t off = 0;
  while (off < len || len == 0) {
    size_t n = len - off;
    if (n > 65535) n = 65535;
    int last = (off + n >= len);
    out.push_back(last ? 1 : 0);
    out.push_back((uint8_t)(n & 0xFF));
    out.push_back((uint8_t)((n >> 8) & 0xFF));
    uint16_t nlen = (uint16_t)~(uint16_t)n;
    out.push_back((uint8_t)(nlen & 0xFF));
    out.push_back((uint8_t)((nlen >> 8) & 0xFF));
    out.insert(out.end(), data + off, data + off + n);
    off += n;
    if (last) break;
  }
  uint32_t sum = adler32(data, len);
  put32(out, sum);
}

}  // namespace

bool simuWriteRgbPng(const char* path, const uint8_t* rgb, int width, int height)
{
  if (!path || !rgb || width <= 0 || height <= 0) return false;

  std::vector<uint8_t> raw;
  raw.reserve((size_t)height * (1 + (size_t)width * 3));
  for (int y = 0; y < height; y++) {
    raw.push_back(0);
    const uint8_t* row = rgb + (size_t)y * (size_t)width * 3;
    raw.insert(raw.end(), row, row + (size_t)width * 3);
  }

  std::vector<uint8_t> zlib;
  storedZlib(zlib, raw.data(), raw.size());

  std::vector<uint8_t> png;
  static const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
  png.insert(png.end(), sig, sig + 8);

  uint8_t ihdr[13];
  ihdr[0] = (uint8_t)(width >> 24);
  ihdr[1] = (uint8_t)(width >> 16);
  ihdr[2] = (uint8_t)(width >> 8);
  ihdr[3] = (uint8_t)width;
  ihdr[4] = (uint8_t)(height >> 24);
  ihdr[5] = (uint8_t)(height >> 16);
  ihdr[6] = (uint8_t)(height >> 8);
  ihdr[7] = (uint8_t)height;
  ihdr[8] = 8;
  ihdr[9] = 2;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  chunk(png, "IHDR", ihdr, 13);
  chunk(png, "IDAT", zlib.data(), zlib.size());
  uint8_t end = 0;
  chunk(png, "IEND", &end, 0);

  FILE* f = fopen(path, "wb");
  if (!f) return false;
  size_t n = fwrite(png.data(), 1, png.size(), f);
  fclose(f);
  return n == png.size();
}
