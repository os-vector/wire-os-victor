#include "cozmo_robot.h"
#include "cozmo_body.h"
#include "cozmo_face.h"
#include "cozmo_audio.h"
#include "cozmo_camera.h"

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>

#include "vic_net.h"
#include "vic_bridge_protocol.h"
#include "vic_face_protocol.h"
#include "vic_spkr_protocol.h"
#include "vic_cam_protocol.h"
#include "miniaudio.h"

using namespace cozmo;

static const double kTickSec = 0.005;
static const double kMotorKeepAlive = 0.2;
static const double kLedPeriod = 0.1;
static const double kFaceHelloPeriod = 1.0;
static const double kBrainTimeout = 1.0;
static const double kChargeJumpVolts = 0.2;
static const double kChargeJumpWindow = 0.5;
static const double kChargeJumpHold = 3.0;
static const double kCamParamPeriod = 0.1;


static const char* kCozmoHost = "172.31.1.1";
static const char* kBrainHost = "127.0.0.1";
static const double kWheelMaxMmps = 200.0;
static const float kWheelAccel = 10000.f;
static const float kHeadPosSpeed = 6.f;
static const float kHeadPosAccel = 20.f;
static const float kLiftPosSpeed = 3.f;
static const float kLiftPosAccel = 20.f;
static const uint16_t kRobotVolume = 50000;
static const uint16_t kCamExposureMs = 33;
static const int32_t kFaceLead = 8;
static const int kFaceMaxSkips = 3;
static const double kWheelStreamPeriod = 1.0 / 50.0;
static const float kWheelStreamDeadband = 2.f;

static const uint16_t kRearCliffValue = 1000;
static const float kCliffScale = 180.f / 400.f;
static const float kGravityMmps2 = 9800.f;
static const float kStableTiltMmps2 = 2500.f;
static const float kStableMagTolMmps2 = 1800.f;
static const float kStableGyroRadps = 0.5f;
static const float kMotorIdleRate = 0.05f;
static const double kUnstableConfirmSec = 0.2;
static const double kStableConfirmSec = 0.75;
static const uint16_t kProxFarMM = 1200;
static const uint8_t  kProxStatusValid = 0x58;

#define VSOCK_OK(s) ((s) != VICNET_INVALID_SOCK)

static volatile sig_atomic_t gStop = 0;

static void OnStopSignal(int)
{
  gStop = 1;
}

static const int kCamOutW = kCozmoCamWidth * 2;
static const int kCamOutH = kCozmoCamHeight * 2;

static void RGBToYUV420sp(const uint8_t* rgb, uint8_t* yuv)
{
  uint8_t* uvPlane = yuv + (size_t)kCamOutW * kCamOutH;

  for (int j = 0; j < kCamOutH; ++j) {
    const int sj = j / 2;
    const uint8_t* srow = rgb + (size_t)sj * kCozmoCamWidth * 3;
    uint8_t* drow = yuv + (size_t)j * kCamOutW;
    uint8_t* uvRow = uvPlane + (size_t)(j / 2) * kCamOutW;
    for (int i = 0; i < kCozmoCamWidth; ++i) {
      const uint8_t* s = srow + i * 3;
      const int r = s[0], g = s[1], b = s[2];
      const uint8_t y = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
      drow[i * 2 + 0] = y;
      drow[i * 2 + 1] = y;
      if ((j & 1) == 0) {
        int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
        int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
        u = u < 0 ? 0 : (u > 255 ? 255 : u);
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        uvRow[i * 2 + 0] = (uint8_t)u;
        uvRow[i * 2 + 1] = (uint8_t)v;
      }
    }
  }
}

static double WallNow()
{
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static double EnvFloat(const char* name, double def)
{
  const char* v = getenv(name);
  if (v == NULL || v[0] == '\0') {
    return def;
  }
  return atof(v);
}

static std::mutex gMicMutex;
static std::deque<int16_t> gMicRing;
static const size_t kMicRingMax = 16000 / 5;

static void MicCaptureCB(ma_device*, void*, const void* input, ma_uint32 frames)
{
  const int16_t* in = (const int16_t*)input;
  std::lock_guard<std::mutex> lk(gMicMutex);
  for (ma_uint32 i = 0; i < frames; ++i) {
    gMicRing.push_back(in[i]);
  }
  while (gMicRing.size() > kMicRingMax) {
    gMicRing.pop_front();
  }
}

static std::mutex gRobotAudioMutex;
static std::deque<int16_t> gRobotAudioRing;
static Resampler gRobotResampler;
static const size_t kRobotAudioTrim = kCozmoAudioFrame * 2;
static const size_t kRobotAudioMax = kCozmoAudioFrame * 4;

static std::atomic<bool> gSpkRxRun(false);
static std::atomic<unsigned> gSpkChunks(0);

static void SpkRxLoop(vicnet_sock_t spkSock)
{
  static VicSpkrChunk chunk;
  while (gSpkRxRun.load()) {
    if (vicnet_poll_in(spkSock, 200) <= 0) {
      continue;
    }
    for (;;) {
      const ssize_t r = recv(spkSock, (char*)&chunk, sizeof(chunk), MSG_DONTWAIT);
      if (r < (ssize_t)(sizeof(chunk) - sizeof(chunk.samples))) {
        break;
      }
      if (chunk.magic != VIC_SPKR_MAGIC || chunk.version != VIC_SPKR_VERSION) {
        continue;
      }
      uint32_t n = chunk.nsamples;
      if (n > VIC_SPKR_CHUNK) {
        n = VIC_SPKR_CHUNK;
      }
      gSpkChunks.fetch_add(1);
      std::lock_guard<std::mutex> lk(gRobotAudioMutex);
      gRobotResampler.Push(chunk.samples, n, gRobotAudioRing);
      if (gRobotAudioRing.size() > kRobotAudioMax) {
        while (gRobotAudioRing.size() > kRobotAudioTrim) {
          gRobotAudioRing.pop_front();
        }
      }
    }
  }
}

static inline uint16_t FlipBytes(uint16_t v)
{
  return (uint16_t)(((v & 0x00FF) << 8) | (v >> 8));
}

static float Clampf(float v, float lim)
{
  if (v > lim) { return lim; }
  if (v < -lim) { return -lim; }
  return v;
}

int main(int argc, char** argv)
{
  vicnet_init();

  gRobotResampler.SetRates(VIC_SPKR_SAMPLE_RATE, kCozmoAudioRate);
  SetMuLawVariant(true);
  const bool camEnabled = (EnvFloat("COZMO_CAM", 1.0) != 0.0);
  const float camGain = (float)EnvFloat("COZMO_CAM_GAIN", 2.0);
  const int faceThreshold = (int)EnvFloat("COZMO_FACE_THRESHOLD", 48.0);
  const int faceDither = (int)EnvFloat("COZMO_FACE_DITHER", 0.0);
  const int faceDitherAmp = (int)EnvFloat("COZMO_FACE_DITHER_AMP", 255.0);
  // one anim frame = one cozmo audio frame
  // 744 samples at 22050 hz = 33.787 ms = 29.637 fps
  const double facePeriod = (double)kCozmoAudioFrame / (double)kCozmoAudioRate;
  bool quiet = true;
  bool connProbe = false;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--verbose") == 0) {
      quiet = false;
    }
    if (strcmp(argv[i], "--conn") == 0) {
      connProbe = true;
    }
  }

  signal(SIGINT, OnStopSignal);
  signal(SIGTERM, OnStopSignal);
  signal(SIGHUP, OnStopSignal);

  if (connProbe) {
    Robot probe;
    probe.SetVerbose(false);
    probe.SetEnableAnimationState(false);
    probe.SetEnableCamera(false);
    probe.SetEnableColor(false);
    if (!probe.Open(kCozmoHost, kRobotPort)) {
      printf("robot never connected\n");
      return 1;
    }
    const double deadline = WallNow() + 60.0;
    double lastTry = WallNow();
    while (!gStop && WallNow() < deadline) {
      probe.Update();
      if (probe.GetLink().GetState() == Link::kConnected) {
        probe.Close();
        printf("robot connected\n");
        return 0;
      }
      const double now = WallNow();
      if (now - lastTry >= 1.0) {
        lastTry = now;
        probe.GetLink().Connect();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    probe.Close();
    printf("robot never connected\n");
    return 1;
  }

  Robot robot;
  robot.SetVerbose(!quiet);
  robot.SetEnableAnimationState(true);
  robot.SetEnableCamera(camEnabled);
  robot.SetEnableColor(false);
  if (!robot.Open(kCozmoHost, kRobotPort)) {
    printf("failed to open link to %s:%u\n", kCozmoHost, (unsigned)kRobotPort);
    return 1;
  }
  printf("connecting to cozmo at %s:%u\n", kCozmoHost, (unsigned)kRobotPort);
  fflush(stdout);

  bool buttonPressed = false;
  Camera camera;
  robot.SetObserver([&buttonPressed, &camera](const Packet& p) {
    if (p.type == kPacketCommand && p.id == kCmdButtonPressed && p.payload.size() >= 1) {
      buttonPressed = (p.payload[0] != 0);
    }
    camera.OnPacket(p);
  });

  vicnet_sock_t camSock = VICNET_INVALID_SOCK;
  double lastCamConnect = -1.0;
  static VicCamFrame camTx;
  size_t camTxOff = sizeof(camTx);
  uint32_t camSeq = 0;
  bool camAnnounced = false;
  float camWantGain = camGain;
  uint16_t camWantExposure = kCamExposureMs;
  double lastCamParamSend = -1.0;
  static uint8_t camRGB[kCozmoCamRGBBytes];

  ma_device micDev;
  bool micDevOk = false;
  {
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_s16;
    cfg.capture.channels = 1;
    cfg.sampleRate = 16000;
    cfg.periodSizeInFrames = 160;
    cfg.dataCallback = MicCaptureCB;
    if (ma_device_init(NULL, &cfg, &micDev) == MA_SUCCESS &&
        ma_device_start(&micDev) == MA_SUCCESS) {
      micDevOk = true;
    } else {
      printf("mic capture unavailable\n");
      fflush(stdout);
    }
  }

  vicnet_sock_t spkSock = vicnet_udp_server(NULL, VIC_NET_PORT_SPKR);
  std::thread spkRxThread;
  if (VSOCK_OK(spkSock)) {
    gSpkRxRun.store(true);
    spkRxThread = std::thread(SpkRxLoop, spkSock);
    printf("speaker, listening on udp/%d\n", VIC_NET_PORT_SPKR);
    fflush(stdout);
  } else {
    printf("speaker socket unavailable\n");
    fflush(stdout);
  }

  vicnet_sock_t sock = VICNET_INVALID_SOCK;
  vicnet_sock_t faceSock = VICNET_INVALID_SOCK;
  double lastFaceConnect = -1.0;
  double lastFaceHello = -1.0;
  double lastFaceSend = -1.0;
  uint32_t faceFrames = 0;
  uint32_t faceSent = 0;
  uint32_t faceSkips = 0;
  uint32_t faceDegrades = 0;
  uint32_t audioFramesSent = 0;
  bool volumeSent = false;
  int faceSkipRun = 0;
  int32_t faceClockSent = 0;
  bool faceClockBased = false;
  bool faceAnnounced = false;
  std::vector<uint8_t> faceRle;
  std::vector<uint8_t> sentFaceRle;
  double lastConnectAttempt = -1.0;
  bool brainAlive = false;
  double lastCmdWall = -1.0;

  VicBridgeH2B cmd;
  memset(&cmd, 0, sizeof(cmd));

  AxisEstimate axis[MOTOR_COUNT];
  uint32_t lastStateStamp = 0;
  bool haveStateStamp = false;

  double lastWheelSend = -1.0;
  double lastLedSend = -1.0;
  float sentWheelL = 0.f, sentWheelR = 0.f;
  uint32_t sentHeadCmdSeq = 0;
  uint32_t sentLiftCmdSeq = 0;
  bool liftWasPos = false;
  uint8_t headActionId = 0;
  uint8_t liftActionId = 0;
  bool headWasPos = false;
  bool motorsStopped = true;
  uint16_t sentLed[3] = { 0xFFFF, 0xFFFF, 0xFFFF };

  uint32_t frameCounter = 0;
  uint16_t proxSampleCount = 0;
  uint16_t maxCliffSeen = 0;
  bool onStableSurface = true;
  double disturbedSince = -1.0;
  double calmSince = -1.0;
  bool readyAnnounced = false;
  double lastStats = WallNow();

  std::deque<std::pair<double, double> > battHist;
  double chargeJumpUntil = -1.0;

  double nextTick = WallNow();

  while (!gStop) {
    nextTick += kTickSec;

    if (!VSOCK_OK(sock)) {
      const double now = WallNow();
      if (now - lastConnectAttempt >= 0.25) {
        lastConnectAttempt = now;
        sock = vicnet_udp_client(kBrainHost, VIC_NET_PORT_BODY);
      }
    }

    for (;;) {
      const double now = WallNow();
      const int waitMs = (int)((nextTick - now) * 1000.0);
      if (waitMs <= 0) {
        break;
      }
      if (!VSOCK_OK(sock)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(waitMs > 2 ? 2 : waitMs));
        break;
      }
      const int pr = vicnet_poll_in(sock, waitMs > 2 ? 2 : waitMs);
      if (pr <= 0) {
        robot.Update();
        continue;
      }
      for (;;) {
        VicBridgeH2B next;
        const ssize_t r = recv(sock, (char*)&next, sizeof(next), MSG_DONTWAIT);
        if (r == (ssize_t)sizeof(next)) {
          if (next.magic == VIC_BRIDGE_MAGIC && next.version == VIC_BRIDGE_PROTO_VERSION) {
            cmd = next;
            lastCmdWall = WallNow();
            if (!brainAlive) {
              brainAlive = true;
              printf("brain found\n");
              fflush(stdout);
            }
          }
          continue;
        }
        break;
      }
      robot.Update();
    }

    robot.Update();
    const double now = WallNow();

    if (camEnabled) {
      if (!VSOCK_OK(camSock) && now - lastCamConnect >= 0.25) {
        lastCamConnect = now;
        camSock = vicnet_tcp_client(kBrainHost, VIC_NET_PORT_CAM);
        if (VSOCK_OK(camSock)) {
          int sndbuf = 2 * 1024 * 1024;
          setsockopt(camSock, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf));
          camTxOff = sizeof(camTx);
        }
      }

      if (VSOCK_OK(camSock)) {
        for (;;) {
          VicCamParams p;
          const ssize_t r = recv(camSock, (char*)&p, sizeof(p), MSG_DONTWAIT);
          if (r != (ssize_t)sizeof(p)) {
            break;
          }
          if (p.magic == VIC_CAM_PARAMS_MAGIC) {
            camWantGain = p.gain;
            camWantExposure = (uint16_t)p.exposureMs;
          }
        }

        if (robot.IsReady() && now - lastCamParamSend >= kCamParamPeriod) {
          lastCamParamSend = now;
          robot.SetCameraParams(camWantGain, camWantExposure, false);
        }

        if (camTxOff >= sizeof(camTx) && camera.TakeFrame(camRGB)) {
          camTx.magic   = VIC_CAM_MAGIC;
          camTx.version = VIC_CAM_VERSION;
          camTx.seq     = camSeq++;
          camTx.width   = kCamOutW;
          camTx.height  = kCamOutH;
          RGBToYUV420sp(camRGB, camTx.yuv);
          camTxOff = 0;
          if (!camAnnounced) {
            camAnnounced = true;
            printf("camera streaming\n");
            fflush(stdout);
          }
        }
        while (camTxOff < sizeof(camTx)) {
          const ssize_t w = send(camSock, (const char*)&camTx + camTxOff,
                                 sizeof(camTx) - camTxOff, MSG_NOSIGNAL);
          if (w > 0) {
            camTxOff += (size_t)w;
            continue;
          }
          if (w < 0 && vicnet_would_block()) {
            break;
          }
          vicnet_close(camSock);
          camSock = VICNET_INVALID_SOCK;
          camTxOff = sizeof(camTx);
          break;
        }
      }
    }

    {
      if (!VSOCK_OK(faceSock) && now - lastFaceConnect >= 0.25) {
        lastFaceConnect = now;
        faceSock = vicnet_udp_client(kBrainHost, VIC_NET_PORT_FACE);
        if (VSOCK_OK(faceSock)) {
          int rcvbuf = 256 * 1024;
          setsockopt(faceSock, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));
        }
      }

      if (VSOCK_OK(faceSock)) {
        if (now - lastFaceHello >= kFaceHelloPeriod) {
          lastFaceHello = now;
          VicNetHello hello = { VIC_NET_HELLO_MAGIC, VIC_NET_PORT_FACE };
          send(faceSock, (const char*)&hello, sizeof(hello), MSG_NOSIGNAL);
        }

        static VicFaceFrame ff;
        bool haveFace = false;
        for (;;) {
          const ssize_t r = recv(faceSock, (char*)&ff, sizeof(ff), MSG_DONTWAIT);
          if (r == (ssize_t)sizeof(ff) && ff.magic == VIC_FACE_MAGIC &&
              ff.version == VIC_FACE_VERSION) {
            haveFace = true;
            continue;
          }
          break;
        }

        if (haveFace) {
          ++faceFrames;
          if (!faceAnnounced) {
            faceAnnounced = true;
            printf("face channel up\n");
            fflush(stdout);
          }
          uint8_t bits[kOledPixels];
          int usedAmp = faceDitherAmp;
          FaceToOled(ff.rgb565, VIC_FACE_WIDTH, VIC_FACE_HEIGHT, bits, faceThreshold,
                     faceDither, usedAmp);
          EncodeOled(bits, faceRle);
          while (faceDither && usedAmp > 0 && faceRle.size() > kMaxDisplayRleSize) {
            usedAmp /= 2;
            FaceToOled(ff.rgb565, VIC_FACE_WIDTH, VIC_FACE_HEIGHT, bits, faceThreshold,
                       faceDither, usedAmp);
            EncodeOled(bits, faceRle);
            ++faceDegrades;
          }
          if (faceRle.size() > kMaxDisplayRleSize) {
            FaceToOled(ff.rgb565, VIC_FACE_WIDTH, VIC_FACE_HEIGHT, bits, faceThreshold);
            EncodeOled(bits, faceRle);
          }
        }

      }
    }

    {
      if (robot.HaveAnimState()) {
        if (!faceClockBased) {
          faceClockBased = true;
          faceClockSent = robot.AnimFramesPlayed();
        }
      }

      if (robot.IsReady() &&
          (lastFaceSend < 0.0 || now - lastFaceSend >= facePeriod)) {
        const bool ahead = faceClockBased &&
                           (faceClockSent - robot.AnimFramesPlayed() > kFaceLead);
        const bool bufferFull = (robot.State().status & kStatusIsAnimBufferFull) != 0;
        const bool skip = (ahead || bufferFull) && (faceSkipRun < kFaceMaxSkips);
        if (skip) {
          ++faceSkipRun;
          ++faceSkips;
          lastFaceSend += facePeriod;
        } else {
          faceSkipRun = 0;
        }
        if (!skip) {
          {
            uint8_t mulaw[kCozmoAudioFrame];
            bool haveAudio = false;
            {
              std::lock_guard<std::mutex> lk(gRobotAudioMutex);
              if (gRobotAudioRing.size() >= kCozmoAudioFrame) {
                for (size_t i = 0; i < kCozmoAudioFrame; ++i) {
                  mulaw[i] = MuLawEncode(gRobotAudioRing.front());
                  gRobotAudioRing.pop_front();
                }
                haveAudio = true;
              }
            }
            if (haveAudio) {
              robot.OutputAudio(mulaw);
              ++audioFramesSent;
            } else {
              robot.OutputSilence();
            }
            ++faceClockSent;
          }
          if (!faceRle.empty()) {
            robot.DisplayImage(&faceRle[0], faceRle.size());
          }
          sentFaceRle = faceRle;
          if (lastFaceSend < 0.0 || now - lastFaceSend >= 4.0 * facePeriod) {
            lastFaceSend = now;
          } else {
            lastFaceSend += facePeriod;
          }
          ++faceSent;
        }
      }
    }

    if (!volumeSent && robot.IsReady()) {
      volumeSent = true;
      robot.SetRobotVolume(kRobotVolume);
      printf("robot volume set to %u\n", (unsigned)kRobotVolume);
      fflush(stdout);
    }

    if (brainAlive && now - lastCmdWall > kBrainTimeout) {
      brainAlive = false;
      memset(&cmd, 0, sizeof(cmd));
      printf("brain lost\n");
      fflush(stdout);
    }

    const RobotState& st = robot.State();
    bool freshState = false;
    if (robot.HaveState() && (!haveStateStamp || st.timestamp != lastStateStamp)) {
      lastStateStamp = st.timestamp;
      haveStateStamp = true;
      freshState = true;
    }

    if (freshState) {
      const double robotNow = st.timestamp / 1000.0;
      axis[MOTOR_LEFT].SetRate(st.lwheelSpeedMmps, SpeedQuantum(MOTOR_LEFT));
      axis[MOTOR_RIGHT].SetRate(st.rwheelSpeedMmps, SpeedQuantum(MOTOR_RIGHT));
      axis[MOTOR_LIFT].Measure(st.liftAngleRad - (double)kLiftMinAngleRad, robotNow,
                               MeasDeadband(MOTOR_LIFT));
      axis[MOTOR_HEAD].Measure(RemapHeadAngle(st.headAngleRad), robotNow,
                               MeasDeadband(MOTOR_HEAD));
    }

    axis[MOTOR_LEFT].Integrate(kTickSec);
    axis[MOTOR_RIGHT].Integrate(kTickSec);
    axis[MOTOR_HEAD].Advance(kTickSec);

    const bool ready = robot.IsReady();
    if (ready && !readyAnnounced) {
      readyAnnounced = true;
      printf("cozmo ready (firmware %u)\n", robot.FirmwareVersion());
      fflush(stdout);
    }

    if (ready) {
      const bool drive = brainAlive;
      float wl = 0.f, wr = 0.f;
      if (drive) {
        if ((cmd.setpointValid & VIC_SETPOINT_WHEELS) != 0) {
          wl = Clampf(cmd.desiredLeftWheelMmps, (float)kWheelMaxMmps);
          wr = Clampf(cmd.desiredRightWheelMmps, (float)kWheelMaxMmps);
        } else {
          const double pl = (cmd.h2b.motorPower[MOTOR_LEFT] / 32767.0) * kHalMotorDirection[MOTOR_LEFT];
          const double pr = (cmd.h2b.motorPower[MOTOR_RIGHT] / 32767.0) * kHalMotorDirection[MOTOR_RIGHT];
          wl = (float)(pl * kWheelMaxMmps);
          wr = (float)(pr * kWheelMaxMmps);
        }
      }

      const bool headPos = drive && (cmd.setpointValid & VIC_SETPOINT_HEAD) != 0;

      const bool liftPos = drive && (cmd.setpointValid & VIC_SETPOINT_LIFT) != 0;

      if (liftPos != liftWasPos) {
        liftWasPos = liftPos;
        sentLiftCmdSeq = 0;
        if (!liftPos) {
          robot.MoveLift(0.f);
        }
      }

      if (liftPos && cmd.liftCmdSeq != 0 && cmd.liftCmdSeq != sentLiftCmdSeq) {
        float speed = cmd.liftCmdSpeedRadPerSec;
        float accel = cmd.liftCmdAccelRadPerSec2;
        if (!(speed > 0.f) || speed > kLiftPosSpeed) {
          speed = kLiftPosSpeed;
        }
        if (!(accel > 0.f) || accel > kLiftPosAccel) {
          accel = kLiftPosAccel;
        }
        robot.SetLiftHeight(cmd.liftCmdHeightMm, speed, accel, cmd.liftCmdDurationSec,
                            ++liftActionId);
        sentLiftCmdSeq = cmd.liftCmdSeq;
      }

      if (headPos != headWasPos) {
        headWasPos = headPos;
        sentHeadCmdSeq = 0;
        if (!headPos) {
          robot.MoveHead(0.f);
        }
      }

      if (headPos && cmd.headCmdSeq != 0 && cmd.headCmdSeq != sentHeadCmdSeq) {
        const double vicSpan = kVicHeadMaxRad - kVicHeadMinRad;
        const double cozSpan = kCozHeadMaxRad - kCozHeadMinRad;
        const double rateScale = cozSpan / vicSpan;
        const float target = (float)UnmapHeadAngle(cmd.headCmdAngleRad);
        float speed = (float)(cmd.headCmdSpeedRadPerSec * rateScale);
        float accel = (float)(cmd.headCmdAccelRadPerSec2 * rateScale);
        if (!(speed > 0.f) || speed > kHeadPosSpeed) {
          speed = kHeadPosSpeed;
        }
        if (!(accel > 0.f) || accel > kHeadPosAccel) {
          accel = kHeadPosAccel;
        }
        robot.SetHeadAngle(target, speed, accel, cmd.headCmdDurationSec, ++headActionId);
        sentHeadCmdSeq = cmd.headCmdSeq;
      }

      const bool allZero = (wl == 0.f && wr == 0.f);

      if (allZero) {
        if (!motorsStopped) {
          if (headPos || liftPos) {
            robot.DriveWheels(0.f, 0.f, kWheelAccel, kWheelAccel);
          } else {
            robot.StopAllMotors();
          }
          motorsStopped = true;
          sentWheelL = sentWheelR = 0.f;
          lastWheelSend = now;
        }
      } else {
        motorsStopped = false;

        const bool wheelChanged =
            (fabsf(wl - sentWheelL) > kWheelStreamDeadband ||
             fabsf(wr - sentWheelR) > kWheelStreamDeadband ||
             ((wl == 0.f) != (sentWheelL == 0.f)) ||
             ((wr == 0.f) != (sentWheelR == 0.f)));
        if (wheelChanged ? (now - lastWheelSend >= kWheelStreamPeriod)
                         : (now - lastWheelSend >= kMotorKeepAlive)) {
          robot.DriveWheels(wl, wr, kWheelAccel, kWheelAccel);
          sentWheelL = wl;
          sentWheelR = wr;
          lastWheelSend = now;
        }
      }

      if (brainAlive && now - lastLedSend >= kLedPeriod) {
        uint16_t led[3];
        for (int i = 0; i < 3; ++i) {
          const uint8_t* c = &cmd.h2b.lightState.ledColors[i * LED_CHANEL_CT];
          led[i] = Rgb555(c[0], c[1], c[2]);
        }
        if (led[0] != sentLed[0] || led[1] != sentLed[1] || led[2] != sentLed[2]) {
          Writer w;
          for (int i = 0; i < 3; ++i) {
            w.U16(led[i]);
            w.U16(led[i]);
            w.U8(1);
            w.U8(0);
            w.U8(0);
            w.U8(0);
            w.I16(0);
          }
          w.U8(0);
          robot.GetLink().SendCommand(kCmdLightStateCenter, w);
          sentLed[0] = led[0]; sentLed[1] = led[1]; sentLed[2] = led[2];
        }
        lastLedSend = now;
      }
    }

    VicBridgeB2H out;
    memset(&out, 0, sizeof(out));
    out.magic = VIC_BRIDGE_MAGIC;
    out.version = VIC_BRIDGE_PROTO_VERSION;
    out.seq = frameCounter;

    BodyToHead& b = out.b2h;
    b.framecounter = frameCounter++;
    b.flags = RUNNING_FLAGS_SENSORS_VALID;
    b.tempAlarm = TEMP_ALARM_SAFE;
    b.failureCode = BOOT_FAIL_NONE;

    if (micDevOk) {
      std::lock_guard<std::mutex> lk(gMicMutex);
      for (int s = 0; s < AUDIO_SAMPLES_PER_FRAME; ++s) {
        int16_t v = 0;
        if (!gMicRing.empty()) {
          v = gMicRing.front();
          gMicRing.pop_front();
        }
        int16_t* dst = &b.audio[s * 4];
        dst[0] = dst[1] = dst[2] = dst[3] = v;
      }
    }

    for (int m = 0; m < MOTOR_COUNT; ++m) {
      FillMotorState(b.motor[m], axis[m].physical, axis[m].rate, kHalMotorScale[m]);
    }

    const bool sensed = robot.HaveState();

    if (sensed) {
      const float xzMag = hypotf(st.accel[0], st.accel[2]);
      const float robotAng = atan2f(st.accel[2], st.accel[0]) + (float)st.headAngleRad;
      const float rf[3] = { xzMag * cosf(robotAng), (float)st.accel[1], xzMag * sinf(robotAng) };
      const float mag = sqrtf(rf[0] * rf[0] + rf[1] * rf[1] + rf[2] * rf[2]);
      const bool motorsIdle = fabsf((float)axis[MOTOR_HEAD].rate) < kMotorIdleRate &&
                              fabsf((float)axis[MOTOR_LIFT].rate) < kMotorIdleRate;
      const bool disturbed = hypotf(rf[0], rf[1]) > kStableTiltMmps2 ||
                             fabsf(mag - kGravityMmps2) > kStableMagTolMmps2 ||
                             (motorsIdle && (fabsf((float)st.gyro[0]) > kStableGyroRadps ||
                                             fabsf((float)st.gyro[1]) > kStableGyroRadps));
      if (disturbed) {
        calmSince = -1.0;
        if (disturbedSince < 0.0) {
          disturbedSince = now;
        }
        if (now - disturbedSince > kUnstableConfirmSec) {
          onStableSurface = false;
        }
      } else {
        disturbedSince = -1.0;
        if (calmSince < 0.0) {
          calmSince = now;
        }
        if (now - calmSince > kStableConfirmSec) {
          onStableSurface = true;
        }
      }
    }

    const uint16_t frontCliff = sensed ? (uint16_t)(st.cliffRaw[0] * kCliffScale) : kRearCliffValue;
    if (sensed && onStableSurface && frontCliff > maxCliffSeen) {
      maxCliffSeen = frontCliff;
    }
    b.cliffSense[0] = frontCliff;
    b.cliffSense[1] = frontCliff;
    b.cliffSense[2] = (sensed && onStableSurface) ? maxCliffSeen : frontCliff;
    b.cliffSense[3] = b.cliffSense[2];

    {
      double volts = st.batteryVoltage;
      if (volts < 0.5 || volts > 6.0) {
        volts = 3.9;
      }

      const bool robotCharging = (st.status & (kStatusIsOnCharger | kStatusIsCharging)) != 0;

      battHist.push_back(std::make_pair(now, volts));
      while (battHist.size() > 1 && now - battHist.front().first > kChargeJumpWindow) {
        battHist.pop_front();
      }

      if (robotCharging) {
        chargeJumpUntil = -1.0;
      } else if (sensed && volts - battHist.front().second >= kChargeJumpVolts) {
        chargeJumpUntil = now + kChargeJumpHold;
        battHist.clear();
        battHist.push_back(std::make_pair(now, volts));
        printf("battery jumped to %.2fV, reporting charging early\n", volts);
        fflush(stdout);
      }

      b.battery.main_voltage = (int16_t)(volts / kBatteryScale);
      b.battery.charger = 0;
      b.battery.temperature = 25;
      b.battery.flags = 0;
      if (robotCharging || now < chargeJumpUntil) {
        b.battery.flags = (BatteryFlags)(POWER_ON_CHARGER | POWER_IS_CHARGING);
        b.battery.charger = (int16_t)(5.0 / kBatteryScale);
      }
    }

    b.proximity.rangeStatus = kProxStatusValid;
    b.proximity.spare1 = 0;
    b.proximity.rangeMM = FlipBytes(kProxFarMM);
    b.proximity.signalRate = 0;
    b.proximity.ambientRate = 0;
    b.proximity.spadCount = FlipBytes((uint16_t)(90.0 * 256.0));
    b.proximity.sampleCount = ++proxSampleCount;
    b.proximity.calibrationResult = 0;

    b.touchLevel[0] = 0;
    b.touchLevel[1] = buttonPressed ? 0xFFFF : 0x0000;
    b.touchHires[0] = 0;
    b.touchHires[1] = 0;

    const uint16_t micPattern = (frameCounter & 1) ? 0xFFFF : 0x0000;
    b.micError[0] = micPattern;
    b.micError[1] = micPattern;

    for (int i = 0; i < 3; ++i) {
      out.imu.gyro[i]  = sensed ? st.gyro[i] : 0.f;
      out.imu.accel[i] = sensed ? st.accel[i] : (i == 2 ? 9800.f : 0.f);
    }
    out.imu.temperature_degC = 25.f;

    if (VSOCK_OK(sock)) {
      if (send(sock, (const char*)&out, sizeof(out), MSG_NOSIGNAL) < 0 &&
          vicnet_conn_refused()) {
        vicnet_close(sock);
        sock = VICNET_INVALID_SOCK;
      }
    }

    if (!quiet && now - lastStats >= 10.0) {
      lastStats = now;
      size_t robotAudioQueued = 0;
      {
        std::lock_guard<std::mutex> lk(gRobotAudioMutex);
        robotAudioQueued = gRobotAudioRing.size();
      }
      Link& link = robot.GetLink();
      printf("stats: brain=%d ready=%d rx=%llu tx=%llu resends=%llu discards=%llu batt=%.2f "
             "rtt=%.0f/%.0f/%.0f rto=%llu@%.0f "
             "anim=%u face=%u rle=%zu lead=%d skip=%u degr=%u over=%llu spkr=%u aud=%u q=%zu\n",
             brainAlive ? 1 : 0, robot.IsReady() ? 1 : 0,
             (unsigned long long)link.RecvPackets(), (unsigned long long)link.SentPackets(),
             (unsigned long long)link.Resends(), (unsigned long long)link.DiscardedFrames(),
             st.batteryVoltage,
             link.RttMin() * 1000.0, link.RttAvg() * 1000.0, link.RttMax() * 1000.0,
             (unsigned long long)link.Timeouts(), link.Rto() * 1000.0,
             (unsigned)faceSent, (unsigned)faceFrames, sentFaceRle.size(),
             faceClockBased ? (int)(faceClockSent - robot.AnimFramesPlayed()) : -1,
             (unsigned)faceSkips, (unsigned)faceDegrades,
             (unsigned long long)link.OversizedPackets(), gSpkChunks.load(),
             (unsigned)audioFramesSent,
             robotAudioQueued);
      link.RttReset();
      fflush(stdout);
    }
  }

  if (micDevOk) {
    ma_device_uninit(&micDev);
  }
  if (spkRxThread.joinable()) {
    gSpkRxRun.store(false);
    spkRxThread.join();
  }
  if (VSOCK_OK(spkSock)) {
    vicnet_close(spkSock);
  }
  if (VSOCK_OK(sock)) {
    vicnet_close(sock);
  }
  robot.Close();
  return 0;
}
