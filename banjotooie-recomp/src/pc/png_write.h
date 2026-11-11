// Gravador PNG mínimo (RGBA 8 bits, deflate em blocos "stored"), sem dependências.
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace pc {

inline uint32_t Crc32(const uint8_t* d, size_t n, uint32_t c = 0) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t v = i;
      for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
      table[i] = v;
    }
    init = true;
  }
  c ^= 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

// rgba: width*height*4 bytes, linhas com 'stride' bytes.
inline bool WritePng(const std::filesystem::path& path, uint32_t width, uint32_t height,
                     const uint8_t* rgba, size_t stride) {
  std::vector<uint8_t> raw;
  raw.reserve(size_t(height) * (size_t(width) * 4 + 1));
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);  // filtro "none"
    const uint8_t* row = rgba + size_t(y) * stride;
    raw.insert(raw.end(), row, row + size_t(width) * 4);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  size_t pos = 0;
  do {
    size_t n = raw.size() - pos;
    if (n > 65535) n = 65535;
    bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
    z.push_back(uint8_t(~n)); z.push_back(uint8_t(~n >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  } while (pos < raw.size());
  uint32_t adler = (b << 16) | a;
  for (int s = 24; s >= 0; s -= 8) z.push_back(uint8_t(adler >> s));

  auto be32 = [](std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(uint8_t(x >> s));
  };
  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, Crc32(td.data(), td.size()));
  };
  std::vector<uint8_t> ihdr;
  be32(ihdr, width); be32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  chunk("IHDR", ihdr);
  chunk("IDAT", z);
  chunk("IEND", {});
  std::error_code ec;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
  FILE* f = _wfopen(path.c_str(), L"wb");
  if (!f) return false;
  bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
  ok = (fclose(f) == 0) && ok;
  return ok;
}

}  // namespace pc
