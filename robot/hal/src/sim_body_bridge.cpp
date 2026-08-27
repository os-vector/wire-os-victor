#ifdef STANDALONE_SIM

#include "sim_body_bridge.h"
#include "anki/cozmo/robot/logging.h"

#include "vic_bridge_protocol.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace Anki {
namespace Vector {
namespace SimBodyBridge {

namespace {
  int listenFd_ = -1;
  int connFd_   = -1;
  uint32_t txSeq_ = 0;

  bool haveBody_ = false;
  VicBridgeB2H latest_ = {};

  bool gripperOn_ = false;

  void SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
      fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
  }

  void TryAccept() {
    if (connFd_ >= 0 || listenFd_ < 0) {
      return;
    }
    int fd = accept(listenFd_, nullptr, nullptr);
    if (fd >= 0) {
      SetNonBlocking(fd);
      connFd_ = fd;
    }
  }
}

void Init()
{
  listenFd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (listenFd_ < 0) {
    return;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, VIC_BRIDGE_BODY_SOCK_PATH, sizeof(addr.sun_path) - 1);

  unlink(VIC_BRIDGE_BODY_SOCK_PATH);

  if (bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(listenFd_);
    listenFd_ = -1;
    return;
  }

  if (listen(listenFd_, 1) < 0) {
    close(listenFd_);
    listenFd_ = -1;
    return;
  }

  if (chmod(VIC_BRIDGE_BODY_SOCK_PATH, 0777) < 0) {
    AnkiWarn("WIRE CAN'T CHMOD BODY THING", "BLA");
  }

  SetNonBlocking(listenFd_);
}

void SetGripper(bool on)
{
  gripperOn_ = on;
}

bool Exchange(const HeadToBody& headData)
{
  TryAccept();
  if (connFd_ < 0) {
    return false;
  }

  VicBridgeH2B cmd = {};
  cmd.magic = VIC_BRIDGE_MAGIC;
  cmd.version = VIC_BRIDGE_PROTO_VERSION;
  cmd.seq = ++txSeq_;
  cmd.h2b = headData;
  cmd.gripper = gripperOn_ ? 1 : 0;

  ssize_t w = send(connFd_, &cmd, sizeof(cmd), MSG_NOSIGNAL);
  if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    close(connFd_);
    connFd_ = -1;
    return false;
  }

  bool gotFresh = false;
  {
    struct pollfd pfd;
    pfd.fd = connFd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int pr = poll(&pfd, 1, 200 /*ms*/);
    if (pr > 0 && (pfd.revents & (POLLHUP | POLLERR))) {
      close(connFd_);
      connFd_ = -1;
    } else if (pr > 0 && (pfd.revents & POLLIN)) {
      VicBridgeB2H frame;
      ssize_t r = recv(connFd_, &frame, sizeof(frame), 0);
      if (r == (ssize_t)sizeof(frame)) {
        if (frame.magic == VIC_BRIDGE_MAGIC && frame.version == VIC_BRIDGE_PROTO_VERSION) {
          latest_ = frame;
          haveBody_ = true;
          gotFresh = true;
        }
      } else if (r == 0) {
        close(connFd_);
        connFd_ = -1;
      } else if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        close(connFd_);
        connFd_ = -1;
      }
    }
  }

  return gotFresh;
}

const BodyToHead* LatestBody()
{
  return haveBody_ ? &latest_.b2h : nullptr;
}

bool LatestImu(HAL::IMU_DataStructure& imu)
{
  if (!haveBody_) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    imu.gyro[i]  = latest_.imu.gyro[i];
    imu.accel[i] = latest_.imu.accel[i];
  }
  imu.temperature_degC = latest_.imu.temperature_degC;
  return true;
}

bool IsConnected()
{
  return connFd_ >= 0;
}

void Shutdown()
{
  if (connFd_ >= 0) { close(connFd_); connFd_ = -1; }
  if (listenFd_ >= 0) { close(listenFd_); listenFd_ = -1; }
  unlink(VIC_BRIDGE_BODY_SOCK_PATH);
}

} // namespace SimBodyBridge
} // namespace Vector
} // namespace Anki

#endif // STANDALONE_SIM
