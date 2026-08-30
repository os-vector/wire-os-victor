#include "cozmo_audio.h"

namespace cozmo {

static const int32_t kMuLawMax = 0x7FFF;
static const int32_t kMuLawBias = 132;

static bool gPycozmoQuirk = true;

void SetMuLawVariant(bool pycozmoQuirk)
{
  gPycozmoQuirk = pycozmoQuirk;
}

uint8_t MuLawEncode(int16_t sample)
{
  int32_t s = sample;
  uint32_t sign = 0;
  if (s < 0) {
    s = -s;
    sign = 0x80;
  }
  s += kMuLawBias;
  if (s > kMuLawMax) {
    s = kMuLawMax;
  }

  int32_t mask = 0x4000;
  int32_t position = 14;
  while ((s & mask) != mask && position >= 7) {
    mask >>= 1;
    --position;
  }

  const uint32_t lsb = (uint32_t)((s >> (position - 4)) & 0x0f);
  const uint32_t v = sign | ((uint32_t)(position - 7) << 4) | lsb;
  return gPycozmoQuirk ? (uint8_t)(v + 1u) : (uint8_t)(~v);
}

int16_t MuLawDecode(uint8_t code)
{
  const uint32_t c = (uint32_t)(~code) & 0xFFu;
  const uint32_t sign = c & 0x80u;
  const int32_t position = (int32_t)((c >> 4) & 0x07u) + 7;
  const int32_t lsb = (int32_t)(c & 0x0fu);
  int32_t v = ((1 << position) | (lsb << (position - 4)) | (1 << (position - 5))) - kMuLawBias;
  if (sign) {
    v = -v;
  }
  if (v > 32767) { v = 32767; }
  if (v < -32768) { v = -32768; }
  return (int16_t)v;
}

Resampler::Resampler()
  : step_(1.0), phase_(0.0), prev_(0), havePrev_(false)
{
}

void Resampler::SetRates(uint32_t inRate, uint32_t outRate)
{
  step_ = (outRate > 0) ? ((double)inRate / (double)outRate) : 1.0;
  Reset();
}

void Resampler::Reset()
{
  phase_ = 0.0;
  prev_ = 0;
  havePrev_ = false;
}

void Resampler::Push(const int16_t* in, size_t n, std::deque<int16_t>& out)
{
  for (size_t i = 0; i < n; ++i) {
    const int16_t cur = in[i];
    if (!havePrev_) {
      prev_ = cur;
      havePrev_ = true;
      continue;
    }
    while (phase_ < 1.0) {
      const double v = (double)prev_ * (1.0 - phase_) + (double)cur * phase_;
      int32_t s = (int32_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
      if (s > 32767) { s = 32767; }
      if (s < -32768) { s = -32768; }
      out.push_back((int16_t)s);
      phase_ += step_;
    }
    phase_ -= 1.0;
    prev_ = cur;
  }
}

}  // namespace cozmo
