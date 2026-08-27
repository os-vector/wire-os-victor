#ifdef STANDALONE_SIM

#include "simCubeClient/simCubeClient.h"

#include "clad/externalInterface/messageCubeToEngine.h"

#include "vic_cube_protocol.h"

extern "C" {
uint8_t intensity[12];
#include "animation.c"
}

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <chrono>

namespace Anki {
namespace Vector {

namespace {
double NowSec()
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

SimCubeClient::~SimCubeClient()
{
  _run.store(false);
  if (_thread.joinable()) {
    _thread.join();
  }
  if (_connFd >= 0)   { close(_connFd); }
  if (_listenFd >= 0) { close(_listenFd); }
}

void SimCubeClient::Start()
{
  _listenFd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (_listenFd < 0) {
    return;
  }
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, VIC_BRIDGE_CUBE_SOCK_PATH, sizeof(addr.sun_path) - 1);
  unlink(VIC_BRIDGE_CUBE_SOCK_PATH);
  if (bind(_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0 ||
      listen(_listenFd, 1) < 0) {
    close(_listenFd);
    _listenFd = -1;
    return;
  }
  chmod(VIC_BRIDGE_CUBE_SOCK_PATH, 0777);
  int fl = fcntl(_listenFd, F_GETFL, 0);
  if (fl >= 0) { fcntl(_listenFd, F_SETFL, fl | O_NONBLOCK); }

  animation_init();

  _serverUp.store(true);
  _run.store(true);
  _thread = std::thread(&SimCubeClient::RunLoop, this);
}

void SimCubeClient::StartScanForCubes()
{
  _scanDeadline_sec.store(NowSec() + _scanDuration_sec);
  _scanDeadlineValid.store(true);
  _scanning.store(true);
}

void SimCubeClient::StopScanForCubes()
{
  _scanning.store(false);
  _scanDeadlineValid.store(false);
  if (_scanFinishedCallback) { _scanFinishedCallback(); }
}

void SimCubeClient::ConnectToCube(const std::string& /*factoryId*/)
{
  _connectedToCube.store(true);
}

void SimCubeClient::DisconnectFromCube()
{
  _connectedToCube.store(false);
}

bool SimCubeClient::Send(const std::vector<uint8_t>& data)
{
  if (data.empty()) {
    return true;
  }
  std::lock_guard<std::mutex> lock(_animMutex);
  if (data[0] == COMMAND_LIGHT_INDEX && data.size() >= sizeof(MapCommand)) {
    MapCommand cmd;
    memcpy(&cmd, data.data(), sizeof(cmd));
    animation_index(&cmd);
  } else if (data[0] == COMMAND_LIGHT_KEYFRAMES && data.size() >= sizeof(FrameCommand)) {
    FrameCommand cmd;
    memcpy(&cmd, data.data(), sizeof(cmd));
    animation_frames(&cmd);
  }
  return true;
}

void SimCubeClient::RunLoop()
{
  while (_run.load()) {
    struct pollfd pfds[2];
    int n = 0;
    int listenIdx = -1, connIdx = -1;
    if (_connFd < 0 && _listenFd >= 0) {
      pfds[n].fd = _listenFd; pfds[n].events = POLLIN; pfds[n].revents = 0; listenIdx = n++;
    }
    if (_connFd >= 0) {
      pfds[n].fd = _connFd; pfds[n].events = POLLIN; pfds[n].revents = 0; connIdx = n++;
    }

    const int pr = poll(pfds, n, 50 /*ms*/);

    // bound the scan even if no cube shows up, so the coordinator doesn't wait forever
    if (_scanning.load() && _scanDeadlineValid.load() && NowSec() >= _scanDeadline_sec.load()) {
      _scanning.store(false);
      _scanDeadlineValid.store(false);
      if (_scanFinishedCallback) { _scanFinishedCallback(); }
    }

    {
      const double now = NowSec();
      bool ledsChanged = false;
      {
        std::lock_guard<std::mutex> lock(_animMutex);
        if (_nextAnimTick_sec == 0.0 || now - _nextAnimTick_sec > 0.5) {
          _nextAnimTick_sec = now;  // first pass, or stalled: don't replay the gap
        }
        while (now >= _nextAnimTick_sec) {
          animation_tick();
          _nextAnimTick_sec += 0.010;
        }
        if (!_ledsEverSent || memcmp(intensity, _lastSentLeds, sizeof(_lastSentLeds)) != 0) {
          memcpy(_lastSentLeds, intensity, sizeof(_lastSentLeds));
          _ledsEverSent = true;
          ledsChanged = true;
        }
      }
      if (ledsChanged && _connFd >= 0) {
        VicCubeDown down;
        memset(&down, 0, sizeof(down));
        down.magic = VIC_CUBE_MAGIC;
        down.version = VIC_CUBE_VERSION;
        down.type = VIC_CUBE_MSG_LEDS;
        memcpy(down.rgb, _lastSentLeds, sizeof(down.rgb));
        send(_connFd, &down, sizeof(down), MSG_NOSIGNAL | MSG_DONTWAIT);
      }
    }

    if (pr <= 0) {
      continue;
    }

    if (listenIdx >= 0 && (pfds[listenIdx].revents & POLLIN)) {
      const int fd = accept(_listenFd, nullptr, nullptr);
      if (fd >= 0) {
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) { fcntl(fd, F_SETFL, fl | O_NONBLOCK); }
        _connFd = fd;
      }
    }

    if (connIdx >= 0 && (pfds[connIdx].revents & (POLLIN | POLLHUP | POLLERR))) {
      for (;;) {
        VicCubeUp up;
        const ssize_t r = recv(_connFd, &up, sizeof(up), 0);
        if (r == (ssize_t)sizeof(up)) {
          if (up.magic != VIC_CUBE_MAGIC || up.version != VIC_CUBE_VERSION) {
            continue;
          }
          up.factoryId[VIC_CUBE_FACTORYID_LEN - 1] = '\0';
          const std::string factoryId(up.factoryId);

          // copied + heavily modified real cube logic
          if (up.type == VIC_CUBE_MSG_ADVERTISE) {
            if (_scanning.load() && _advertisementCallback) { _advertisementCallback(factoryId, 0); }
          } else if (up.type == VIC_CUBE_MSG_ACCEL && _connectedToCube.load()) {
            for (int a = 0; a < 3; ++a) { _accelBuf[_accelFrameCount][a] = up.accel[a]; }
            if (up.tapped) { ++_tapCount; }
            ++_accelFrameCount;
            if (_accelFrameCount >= 3) {
              _accelFrameCount = 0;
              CubeAccelData accelData;
              accelData.tap_count = _tapCount;
              for (int i = 0; i < 3; ++i) {
                for (int a = 0; a < 3; ++a) {
                  accelData.accelReadings[i].accel[a] = _accelBuf[i][a];
                }
              }
              MessageCubeToEngine msg(std::move(accelData));
              std::vector<uint8_t> buff(msg.Size());
              msg.Pack(buff.data(), msg.Size());
              if (_receiveDataCallback) { _receiveDataCallback(factoryId, buff); }

              if (++_voltageCtr >= 66) {
                _voltageCtr = 0;
                CubeVoltageData volt;
                volt.unused = 0;
                volt.railVoltageCnts = up.railVoltageCnts;
                MessageCubeToEngine vmsg(std::move(volt));
                std::vector<uint8_t> vbuff(vmsg.Size());
                vmsg.Pack(vbuff.data(), vmsg.Size());
                if (_receiveDataCallback) { _receiveDataCallback(factoryId, vbuff); }
              }
            }
          }
          continue;
        }
        if (r == 0) {
          close(_connFd); _connFd = -1; _connectedToCube.store(false); break;
        }
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { break; }
        if (r < 0) {
          close(_connFd); _connFd = -1; _connectedToCube.store(false); break;
        }
        break;
      }
    }
  }
}

} // namespace Vector
} // namespace Anki

#endif // STANDALONE_SIM
