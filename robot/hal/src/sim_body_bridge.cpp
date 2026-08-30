#ifdef STANDALONE_SIM

#include "sim_body_bridge.h"
#include "anki/cozmo/robot/logging.h"

#include "vic_bridge_protocol.h"
#include "vic_net.h"

#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <cstring>
#include <cstdlib>
#include <deque>

namespace Anki {
namespace Vector {
namespace SimBodyBridge {

namespace {
  int udpFd_ = -1;
  uint32_t txSeq_ = 0;

  struct sockaddr_storage peer_;
  socklen_t peerLen_ = 0;
  bool havePeer_ = false;
  double lastRx_ = -1.0;

  bool haveBody_ = false;
  VicBridgeB2H latest_ = {};

  std::deque<VicBridgeImu> imuQueue_;
  const size_t kMaxImuQueue = 8;

  bool gripperOn_ = false;
  uint8_t setpointValid_ = 0;
  uint32_t headCmdSeq_ = 0;
  float headCmdAngleRad_ = 0.f;
  float headCmdSpeedRadPerSec_ = 0.f;
  float headCmdAccelRadPerSec2_ = 0.f;
  float headCmdDurationSec_ = 0.f;
  uint32_t liftCmdSeq_ = 0;
  float liftCmdHeightMm_ = 0.f;
  float liftCmdSpeedRadPerSec_ = 0.f;
  float liftCmdAccelRadPerSec2_ = 0.f;
  float liftCmdDurationSec_ = 0.f;
  float desiredLeftWheelMmps_ = 0.f;
  float desiredRightWheelMmps_ = 0.f;

  double MonoNow() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
  }

  int DrainUdp() {
    int gotFresh = 0;
    for (;;) {
      VicBridgeB2H frame;
      struct sockaddr_storage src;
      socklen_t srcLen = sizeof(src);
      const ssize_t r = recvfrom(udpFd_, &frame, sizeof(frame), MSG_DONTWAIT,
                                 (struct sockaddr*)&src, &srcLen);
      if (r < 0) {
        break;
      }
      peer_ = src;
      peerLen_ = srcLen;
      havePeer_ = true;
      lastRx_ = MonoNow();
      if (r == (ssize_t)sizeof(frame) &&
          frame.magic == VIC_BRIDGE_MAGIC && frame.version == VIC_BRIDGE_PROTO_VERSION) {
        latest_ = frame;
        haveBody_ = true;
        ++gotFresh;
        imuQueue_.push_back(frame.imu);
        while (imuQueue_.size() > kMaxImuQueue) {
          imuQueue_.pop_front();
        }
      }
    }
    if (havePeer_ && MonoNow() - lastRx_ > 1.0) {
      havePeer_ = false;
    }
    return gotFresh;
  }
}

void Init()
{
  udpFd_ = vicnet_udp_server(nullptr, VIC_NET_PORT_BODY);
  if (udpFd_ < 0) {
    AnkiWarn("WIRE body UDP bind failed", "port %d", VIC_NET_PORT_BODY);
  }
}

void SetGripper(bool on)
{
  gripperOn_ = on;
}

void SetMotorSetpoints(uint8_t valid)
{
  setpointValid_ = valid;
}

void SetHeadCommand(uint32_t seq, float angleRad, float speedRadPerSec,
                    float accelRadPerSec2, float durationSec)
{
  headCmdSeq_ = seq;
  headCmdAngleRad_ = angleRad;
  headCmdSpeedRadPerSec_ = speedRadPerSec;
  headCmdAccelRadPerSec2_ = accelRadPerSec2;
  headCmdDurationSec_ = durationSec;
}

void SetLiftCommand(uint32_t seq, float heightMm, float speedRadPerSec,
                    float accelRadPerSec2, float durationSec)
{
  liftCmdSeq_ = seq;
  liftCmdHeightMm_ = heightMm;
  liftCmdSpeedRadPerSec_ = speedRadPerSec;
  liftCmdAccelRadPerSec2_ = accelRadPerSec2;
  liftCmdDurationSec_ = durationSec;
}

void SetWheelSetpoints(float leftMmps, float rightMmps)
{
  desiredLeftWheelMmps_ = leftMmps;
  desiredRightWheelMmps_ = rightMmps;
}

int Exchange(const HeadToBody& headData)
{
  if (udpFd_ < 0) {
    return 0;
  }

  VicBridgeH2B cmd = {};
  cmd.magic = VIC_BRIDGE_MAGIC;
  cmd.version = VIC_BRIDGE_PROTO_VERSION;
  cmd.seq = ++txSeq_;
  cmd.h2b = headData;
  cmd.gripper = gripperOn_ ? 1 : 0;
  cmd.setpointValid = setpointValid_;
  cmd.headCmdSeq = headCmdSeq_;
  cmd.headCmdAngleRad = headCmdAngleRad_;
  cmd.headCmdSpeedRadPerSec = headCmdSpeedRadPerSec_;
  cmd.headCmdAccelRadPerSec2 = headCmdAccelRadPerSec2_;
  cmd.headCmdDurationSec = headCmdDurationSec_;
  cmd.liftCmdSeq = liftCmdSeq_;
  cmd.liftCmdHeightMm = liftCmdHeightMm_;
  cmd.liftCmdSpeedRadPerSec = liftCmdSpeedRadPerSec_;
  cmd.liftCmdAccelRadPerSec2 = liftCmdAccelRadPerSec2_;
  cmd.liftCmdDurationSec = liftCmdDurationSec_;
  cmd.desiredLeftWheelMmps = desiredLeftWheelMmps_;
  cmd.desiredRightWheelMmps = desiredRightWheelMmps_;

  int fresh = DrainUdp();
  if (havePeer_) {
    sendto(udpFd_, &cmd, sizeof(cmd), MSG_NOSIGNAL | MSG_DONTWAIT,
           (struct sockaddr*)&peer_, peerLen_);
  }

  struct pollfd pfd;
  pfd.fd = udpFd_;
  pfd.events = POLLIN;
  pfd.revents = 0;
  const int timeoutMs = havePeer_ ? 200 : 0;
  if (poll(&pfd, 1, timeoutMs) > 0 && (pfd.revents & POLLIN)) {
    fresh += DrainUdp();
  }
  return fresh;
}

const BodyToHead* LatestBody()
{
  return haveBody_ ? &latest_.b2h : nullptr;
}

bool PopImu(HAL::IMU_DataStructure& imu)
{
  if (imuQueue_.empty()) {
    return false;
  }
  const VicBridgeImu& src = imuQueue_.front();
  for (int i = 0; i < 3; ++i) {
    imu.gyro[i]  = src.gyro[i];
    imu.accel[i] = src.accel[i];
  }
  imu.temperature_degC = src.temperature_degC;
  imuQueue_.pop_front();
  return true;
}

bool IsConnected()
{
  return havePeer_;
}

void Shutdown()
{
  if (udpFd_ >= 0) { close(udpFd_); udpFd_ = -1; }
}

} // namespace SimBodyBridge
} // namespace Vector
} // namespace Anki

#endif // STANDALONE_SIM
