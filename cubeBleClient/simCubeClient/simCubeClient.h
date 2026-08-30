#ifndef __Victor_SimCubeClient_H__
#define __Victor_SimCubeClient_H__

#ifdef STANDALONE_SIM

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Anki {
namespace Vector {

class SimCubeClient
{
public:
  using AdvertisementCallback = std::function<void(const std::string& addr, const int rssi)>;
  using ReceiveDataCallback   = std::function<void(const std::string& addr, const std::vector<uint8_t>& data)>;
  using ScanFinishedCallback  = std::function<void(void)>;

  SimCubeClient() = default;
  ~SimCubeClient();

  void RegisterAdvertisementCallback(const AdvertisementCallback& cb) { _advertisementCallback = cb; }
  void RegisterReceiveDataCallback(const ReceiveDataCallback& cb)     { _receiveDataCallback = cb; }
  void RegisterScanFinishedCallback(const ScanFinishedCallback& cb)   { _scanFinishedCallback = cb; }

  void Start();

  bool IsConnectedToServer() const            { return _serverUp.load(); }
  bool IsConnectedToCube() const              { return _connectedToCube.load(); }
  bool IsPendingFirmwareCheckOrUpdate() const { return false; } // no cube firmware in sim

  void StartScanForCubes();
  void StopScanForCubes();
  void ConnectToCube(const std::string& factoryId);
  void DisconnectFromCube();
  bool Send(const std::vector<uint8_t>& data);

  void SetScanDuration(const float duration_sec) { _scanDuration_sec = duration_sec; }
  void SetCubeFirmwareFilepath(const std::string&) {}

private:
  void RunLoop();

  int _udpFd = -1;
  std::thread _thread;

  std::atomic<bool> _run{false};
  std::atomic<bool> _serverUp{false};
  std::atomic<bool> _connectedToCube{false};
  std::atomic<bool> _scanning{false};
  std::atomic<bool> _scanDeadlineValid{false};
  std::atomic<double> _scanDeadline_sec{0.0};
  float _scanDuration_sec = 15.f;

  AdvertisementCallback _advertisementCallback;
  ReceiveDataCallback   _receiveDataCallback;
  ScanFinishedCallback  _scanFinishedCallback;

  std::mutex _animMutex;
  double  _nextAnimTick_sec = 0.0;
  uint8_t _lastSentLeds[12] = {0};
  bool    _ledsEverSent = false;

  int16_t _accelBuf[3][3] = {{0}};
  int     _accelFrameCount = 0;
  uint8_t _tapCount = 0;
  int     _voltageCtr = 0;
};

} // namespace Vector
} // namespace Anki

#endif // STANDALONE_SIM
#endif // __Victor_SimCubeClient_H__
