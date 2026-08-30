#ifndef __COZMO_CAMERA_H__
#define __COZMO_CAMERA_H__

#include <stdint.h>

#include <vector>

#include "cozmo_link.h"

namespace cozmo {

static const int kCozmoCamWidth  = 320;
static const int kCozmoCamHeight = 240;
static const size_t kCozmoCamPixels = (size_t)kCozmoCamWidth * kCozmoCamHeight;
static const size_t kCozmoCamRGBBytes = kCozmoCamPixels * 3;

class Camera {
public:
  Camera();

  void OnPacket(const Packet& pkt);
  bool TakeFrame(uint8_t* rgb);

  uint32_t Frames() const { return frames_; }
  uint32_t Drops() const { return drops_; }

private:
  void Complete();

  std::vector<uint8_t> partial_;
  std::vector<uint8_t> jpeg_;
  std::vector<uint8_t> rgb_;
  uint32_t imageId_;
  int lastChunk_;
  int8_t encoding_;
  bool valid_;
  bool haveFrame_;
  uint32_t frames_;
  uint32_t drops_;
};

}  // namespace cozmo

#endif
