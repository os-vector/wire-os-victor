#ifndef __COZMO_FACE_H__
#define __COZMO_FACE_H__

#include <stdint.h>
#include <stddef.h>

#include <vector>

namespace cozmo {

static const int kOledWidth = 128;
static const int kOledHeight = 32;
static const size_t kOledPixels = (size_t)kOledWidth * kOledHeight;

void FaceToLuma(const uint16_t* rgb565, int w, int h, uint8_t* luma);
void DownscaleLuma(const uint8_t* luma, int w, int h, uint8_t* out);
void ThresholdOled(const uint8_t* small, uint8_t* bits, int threshold);
void DitherOled(const uint8_t* small, uint8_t* bits, int threshold, int matrix, int amplitude);

void FaceToOled(const uint16_t* rgb565, int w, int h, uint8_t* bits, int threshold,
                int dither = 0, int amplitude = 255);

void EncodeOled(const uint8_t* bits, std::vector<uint8_t>& out);
bool DecodeOled(const uint8_t* buf, size_t n, uint8_t* bits);

}  // namespace cozmo

#endif
