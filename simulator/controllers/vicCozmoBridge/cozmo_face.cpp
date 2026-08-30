#include "cozmo_face.h"

#include <string.h>

namespace cozmo {

void FaceToLuma(const uint16_t* rgb565, int w, int h, uint8_t* luma)
{
  const size_t n = (size_t)w * h;
  for (size_t i = 0; i < n; ++i) {
    const uint16_t px = rgb565[i];
    const uint32_t r = (((px >> 11) & 0x1F) * 255u) / 31u;
    const uint32_t g = (((px >> 5) & 0x3F) * 255u) / 63u;
    const uint32_t b = ((px & 0x1F) * 255u) / 31u;
    luma[i] = (uint8_t)((r * 77u + g * 150u + b * 29u) >> 8);
  }
}

void DownscaleLuma(const uint8_t* luma, int w, int h, uint8_t* out)
{
  for (int oy = 0; oy < kOledHeight; ++oy) {
    const int y0 = (oy * h) / kOledHeight;
    int y1 = ((oy + 1) * h) / kOledHeight;
    if (y1 <= y0) { y1 = y0 + 1; }
    for (int ox = 0; ox < kOledWidth; ++ox) {
      const int x0 = (ox * w) / kOledWidth;
      int x1 = ((ox + 1) * w) / kOledWidth;
      if (x1 <= x0) { x1 = x0 + 1; }
      uint32_t sum = 0;
      uint32_t cnt = 0;
      for (int y = y0; y < y1 && y < h; ++y) {
        for (int x = x0; x < x1 && x < w; ++x) {
          sum += luma[(size_t)y * w + x];
          ++cnt;
        }
      }
      out[(size_t)oy * kOledWidth + ox] = (uint8_t)(cnt ? (sum / cnt) : 0);
    }
  }
}

void ThresholdOled(const uint8_t* small, uint8_t* bits, int threshold)
{
  for (size_t i = 0; i < kOledPixels; ++i) {
    bits[i] = (small[i] >= threshold) ? 1 : 0;
  }
}

static int BayerValue(int x, int y, int n)
{
  int v = 0;
  for (int s = 1; s < n; s <<= 1) {
    const int qx = (x & s) ? 1 : 0;
    const int qy = (y & s) ? 1 : 0;
    const int q = qy ? (qx ? 1 : 3) : (qx ? 2 : 0);
    v = v * 4 + q;
  }
  return v;
}

void DitherOled(const uint8_t* small, uint8_t* bits, int threshold, int matrix, int amplitude)
{
  const int n = (matrix >= 8) ? 8 : ((matrix >= 4) ? 4 : 2);
  const int cells = n * n;
  for (int y = 0; y < kOledHeight; ++y) {
    for (int x = 0; x < kOledWidth; ++x) {
      const int bias =
          ((2 * BayerValue(x & (n - 1), y & (n - 1), n) + 1) * amplitude) / (2 * cells) - (amplitude / 2);
      const size_t i = (size_t)y * kOledWidth + x;
      bits[i] = ((int)small[i] >= threshold + bias) ? 1 : 0;
    }
  }
}

void FaceToOled(const uint16_t* rgb565, int w, int h, uint8_t* bits, int threshold,
                int dither, int amplitude)
{
  std::vector<uint8_t> luma((size_t)w * h);
  uint8_t small[kOledPixels];
  FaceToLuma(rgb565, w, h, &luma[0]);
  DownscaleLuma(&luma[0], w, h, small);
  if (dither) {
    DitherOled(small, bits, threshold, dither, amplitude);
  } else {
    ThresholdOled(small, bits, threshold);
  }
}

namespace {

struct Encoder {
  const uint8_t* px;
  std::vector<uint8_t>* buf;
  std::vector<uint8_t> lastCol;
  std::vector<uint8_t> curCol;
  int skipCols;
  int repeatCols;
  int x;
  int y;

  Encoder(const uint8_t* bits, std::vector<uint8_t>& out)
    : px(bits), buf(&out), skipCols(0), repeatCols(0), x(0), y(0) {}

  uint8_t At(int cx, int cy) const { return px[(size_t)cy * kOledWidth + cx]; }

  bool EncodeSeq(int color, int cnt, uint8_t& cmd)
  {
    if (color) {
      if (cnt <= 15) {
        cmd = (uint8_t)(0x80 + (cnt << 2) + 0x01);
      } else {
        cmd = (uint8_t)(0xc0 + ((cnt - 16) << 2) + 0x01);
      }
      return true;
    }
    if (cnt <= 15) {
      cmd = (uint8_t)(0x80 + (cnt << 2));
      return true;
    }
    if (cnt < 31) {
      cmd = (uint8_t)(0xc0 + ((cnt - 16) << 2));
      return true;
    }
    ++skipCols;
    return false;
  }

  int CountColor(int color)
  {
    int cnt = 0;
    if (y < kOledHeight) {
      while (At(x, y) == color) {
        ++cnt;
        ++y;
        if (y > kOledHeight - 1) {
          ++x;
          y = 0;
          break;
        }
      }
    } else {
      ++x;
      y = 0;
    }
    return cnt;
  }

  void DropTrailingSkip()
  {
    if (!buf->empty()) {
      const uint8_t cmd = buf->back();
      if ((cmd & 0xc3) == 0x80 || (cmd & 0xc3) == 0xc0) {
        buf->pop_back();
      }
    }
  }

  void FlushSkipCols()
  {
    if (skipCols) {
      repeatCols = 0;
      lastCol.clear();
      DropTrailingSkip();
    }
    while (skipCols >= 64) {
      buf->push_back(63);
      skipCols -= 64;
    }
    if (skipCols) {
      buf->push_back((uint8_t)(skipCols - 1));
      skipCols = 0;
    }
  }

  void FlushRepeatCols()
  {
    if (repeatCols) {
      DropTrailingSkip();
    }
    while (repeatCols >= 64) {
      buf->push_back((uint8_t)(0x40 + 0x3f));
      repeatCols -= 64;
    }
    if (repeatCols) {
      buf->push_back((uint8_t)(0x40 + repeatCols - 1));
      repeatCols = 0;
    }
  }

  void Run()
  {
    while (x < kOledWidth && y < kOledHeight) {
      const int color = At(x, y);
      ++y;
      const int cnt = CountColor(color);
      uint8_t cmd = 0;
      if (EncodeSeq(color, cnt, cmd)) {
        if (y == 0) {
          FlushSkipCols();
          if ((cmd & 0xc3) == 0x81 || (cmd & 0xc3) == 0xc1) {
            cmd = (uint8_t)(cmd + 1);
          }
        }
        curCol.push_back(cmd);
      }
      if (y == 0) {
        if (!skipCols) {
          if (curCol == lastCol) {
            ++repeatCols;
          } else {
            FlushRepeatCols();
            buf->insert(buf->end(), curCol.begin(), curCol.end());
            lastCol = curCol;
          }
        } else {
          FlushRepeatCols();
        }
        curCol.clear();
      }
    }
    if (y == 0) {
      FlushSkipCols();
      FlushRepeatCols();
    }
  }
};

struct Decoder {
  uint8_t* img;
  int x;
  int y;
  bool lastDraw;
  bool repeatColumnShift;

  explicit Decoder(uint8_t* out)
    : img(out), x(0), y(0), lastDraw(false), repeatColumnShift(false) {}

  void Draw()
  {
    if (y < kOledHeight && x < kOledWidth && x >= 0 && y >= 0) {
      img[(size_t)y * kOledWidth + x] = 1;
    }
  }

  void Execute(uint8_t b)
  {
    const int cmd = (b & 0xc0) >> 6;
    int cnt = b & 0x3f;
    if (cmd == 0) {
      cnt += 1;
      if (lastDraw) { ++x; }
      x += cnt;
      y = 0;
      lastDraw = false;
      repeatColumnShift = false;
      return;
    }
    if (cmd == 1) {
      cnt += 1;
      if (!repeatColumnShift) { ++x; }
      for (int r = 0; r < cnt; ++r) {
        if (x > 0 && x < kOledWidth) {
          for (int row = 0; row < kOledHeight; ++row) {
            img[(size_t)row * kOledWidth + x] = img[(size_t)row * kOledWidth + x - 1];
          }
        }
        ++x;
      }
      y = 0;
      lastDraw = false;
      repeatColumnShift = true;
      return;
    }

    const bool extended = (cmd == 3);
    const int draw = cnt & 0x01;
    cnt >>= 1;
    const int draw2 = cnt & 0x01;
    cnt >>= 1;
    cnt += 1;
    if (extended) { cnt += 16; }
    if (draw || draw2) {
      for (int i = 0; i < cnt; ++i) {
        Draw();
        ++y;
      }
    } else {
      y += cnt;
    }
    if (y > kOledHeight - 1) {
      repeatColumnShift = true;
      ++x;
      y -= kOledHeight;
    } else {
      repeatColumnShift = false;
    }
    lastDraw = extended ? false : true;
  }
};

}  // namespace

void EncodeOled(const uint8_t* bits, std::vector<uint8_t>& out)
{
  out.clear();
  Encoder enc(bits, out);
  enc.Run();
}

bool DecodeOled(const uint8_t* buf, size_t n, uint8_t* bits)
{
  memset(bits, 0, kOledPixels);
  Decoder dec(bits);
  for (size_t i = 0; i < n; ++i) {
    dec.Execute(buf[i]);
  }
  return dec.x >= kOledWidth;
}

}  // namespace cozmo
