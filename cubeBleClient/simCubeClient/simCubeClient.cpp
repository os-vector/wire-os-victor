#ifdef STANDALONE_SIM

#include "simCubeClient/simCubeClient.h"

#include "clad/externalInterface/messageCubeToEngine.h"

#include "vic_cube_protocol.h"
#include "vic_net.h"

extern "C" {
uint8_t intensity[12];
#include "animation.c"
}

#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <cstdlib>
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
  if (_udpFd >= 0) { close(_udpFd); }
}

void SimCubeClient::Start()
{
  _udpFd = vicnet_udp_server(nullptr, VIC_NET_PORT_CUBE);
  if (_udpFd < 0) {
    return;
  }
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
  struct sockaddr_storage peer;
  socklen_t peerLen = 0;
  bool havePeer = false;
  double lastRx = -1.0;

  while (_run.load()) {
    struct pollfd pfd;
    pfd.fd = _udpFd; pfd.events = POLLIN; pfd.revents = 0;

    const int pr = poll(&pfd, 1, 50 /*ms*/);

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
      if (ledsChanged && havePeer) {
        VicCubeDown down;
        memset(&down, 0, sizeof(down));
        down.magic = VIC_CUBE_MAGIC;
        down.version = VIC_CUBE_VERSION;
        down.type = VIC_CUBE_MSG_LEDS;
        memcpy(down.rgb, _lastSentLeds, sizeof(down.rgb));
        sendto(_udpFd, &down, sizeof(down), MSG_NOSIGNAL | MSG_DONTWAIT,
               (struct sockaddr*)&peer, peerLen);
      }
    }

    if (havePeer && NowSec() - lastRx > 3.0) {
      havePeer = false;
      _connectedToCube.store(false);
    }

    if (pr <= 0) {
      continue;
    }

    if (pfd.revents & POLLIN) {
      for (;;) {
        VicCubeUp up;
        struct sockaddr_storage src;
        socklen_t srcLen = sizeof(src);
        const ssize_t r = recvfrom(_udpFd, &up, sizeof(up), MSG_DONTWAIT,
                                   (struct sockaddr*)&src, &srcLen);
        if (r >= 0) {
          peer = src;
          peerLen = srcLen;
          havePeer = true;
          lastRx = NowSec();
          if (r != (ssize_t)sizeof(up)) {
            continue;
          }
        }
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
        break;
      }
    }
  }
}

} // namespace Vector
} // namespace Anki

#endif // STANDALONE_SIM
