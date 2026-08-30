#ifndef __COZMO_AUDIO_H__
#define __COZMO_AUDIO_H__

#include <stdint.h>
#include <stddef.h>

#include <deque>

namespace cozmo {

static const uint32_t kCozmoAudioRate = 22050;
static const size_t kCozmoAudioFrame = 744;

uint8_t MuLawEncode(int16_t sample);
void SetMuLawVariant(bool pycozmoQuirk);
int16_t MuLawDecode(uint8_t code);

class Resampler {
public:
  Resampler();

  void SetRates(uint32_t inRate, uint32_t outRate);
  void Reset();
  void Push(const int16_t* in, size_t n, std::deque<int16_t>& out);

private:
  double step_;
  double phase_;
  int16_t prev_;
  bool havePrev_;
};

}  // namespace cozmo

#endif
