/*
  a whole lot of this is heavily inspired by the original controller code.
  some things directly copied
*/

#include <webots/Supervisor.hpp>
#include <webots/Node.hpp>
#include <webots/Motor.hpp>
#include <webots/PositionSensor.hpp>
#include <webots/Gyro.hpp>
#include <webots/Accelerometer.hpp>
#include <webots/DistanceSensor.hpp>
#include <webots/LED.hpp>
#include <webots/Connector.hpp>
#include <webots/Field.hpp>
#include <webots/Display.hpp>
#include <webots/Camera.hpp>
#include <webots/Keyboard.hpp>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <deque>
#include <mutex>
#include <atomic>

#include <alsa/asoundlib.h>

#include "vic_bridge_protocol.h"
#include "vic_face_protocol.h"
#include "vic_cam_protocol.h"
#include "vic_cube_protocol.h"

static const double PI = 3.14159265358979323846;

static const double HAL_MOTOR_POSITION_SCALE[MOTOR_COUNT] = {
  ((       0.96 * 29.0 * 0.25 * PI) / 172.3),
  ((-1.0 * 0.96 * 29.0 * 0.25 * PI) / 172.3),
  ((0.25 * PI) / 149.7),
  ((0.25 * PI) / 366.211),
};

static const double HAL_SEC_PER_TICK = (1.0 / 256.0) / 48000000.0;

static const double BATTERY_SCALE = 2.8 / 2048.0;

static const double WHEEL_MAX_SPEED_MPS = 0.25;

static const double WHEEL_DRIVE_SIGN[2]   = { -1.0, +1.0 };
static const double WHEEL_ENCODER_MM_SIGN = -1000.0;

static inline uint16_t FlipBytes(uint16_t v) {
  return (uint16_t)(((v & 0x00FF) << 8) | (v >> 8));
}

static inline float Median3(float a, float b, float c) {
  return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

static void BGRAtoYUV420sp(const unsigned char* bgra, int w, int h, uint8_t* yuv, bool rot180)
{
  uint8_t* yPlane  = yuv;
  uint8_t* uvPlane = yuv + (size_t)w * h;

  for (int j = 0; j < h; ++j) {
    const int sj = rot180 ? (h - 1 - j) : j;
    const unsigned char* row = bgra + (size_t)sj * w * 4;
    uint8_t* yRow = yPlane + (size_t)j * w;
    for (int i = 0; i < w; ++i) {
      const int si = (rot180 ? (w - 1 - i) : i) * 4;
      const int b = row[si + 0];
      const int g = row[si + 1];
      const int r = row[si + 2];
      yRow[i] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    }
  }

  for (int j = 0; j < h; j += 2) {
    const int sj = rot180 ? (h - 2 - j) : j;
    const unsigned char* row = bgra + (size_t)sj * w * 4;
    uint8_t* uvRow = uvPlane + (size_t)(j / 2) * w;
    for (int i = 0; i < w; i += 2) {
      const int si = (rot180 ? (w - 2 - i) : i) * 4;
      const int b = row[si + 0];
      const int g = row[si + 1];
      const int r = row[si + 2];
      int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
      int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
      uvRow[i]     = (uint8_t)std::max(0, std::min(255, u));
      uvRow[i + 1] = (uint8_t)std::max(0, std::min(255, v));
    }
  }
}

static webots::Node* FindLightCubeNode(webots::Supervisor& robot)
{
  webots::Node* root = robot.getRoot();
  if (root == nullptr) return nullptr;
  webots::Field* children = root->getField("children");
  if (children == nullptr) return nullptr;
  const int n = children->getCount();
  for (int i = 0; i < n; ++i) {
    webots::Node* node = children->getMFNode(i);
    if (node != nullptr && node->getField("objectType") && node->getField("ledColors")) {
      return node;
    }
  }
  return nullptr;
}

static bool CubeCheckForTap(float ax, float ay, float az, int stepMs)
{
  static const int MAXB = 30;
  static float buf[3][MAXB] = {{0}};
  static int start = 0, size = 0;
  const float cutoff = 50.f;
  const float rc = 1.0f / (cutoff * 2.f * 3.14159f);
  const float dt = 0.001f * stepMs;
  const float alpha = rc / (rc + dt);
  const float thresh = 9.f;

  int newIdx = start;
  if (size < MAXB - 1) { newIdx += size; ++size; }
  else { start = (start + 1) % MAXB; size = MAXB; }
  newIdx %= MAXB;
  buf[0][newIdx] = ax; buf[1][newIdx] = ay; buf[2][newIdx] = az;

  bool tap = false;
  if (size == MAXB) {
    for (int axis = 0; axis < 3 && !tap; ++axis) {
      float prev = buf[axis][start], prevF = prev;
      for (int i = 1; i < MAXB; ++i) {
        const int idx = (start + i) % MAXB;
        const float f = alpha * (prevF + buf[axis][idx] - prev);
        prev = buf[axis][idx]; prevF = f;
        if (f > thresh) {
          tap = true;
          const int off = i + (100 / stepMs);
          start = (start + off) % MAXB;
          size = (size > off) ? size - off : 0;
          break;
        }
      }
    }
  }
  return tap;
}

int main(int, char**)
{
  webots::Supervisor robot;
  const int timeStepMs = (int)robot.getBasicTimeStep();
  const double dtSec = timeStepMs / 1000.0;
  const uint32_t frameTimeTicks = (uint32_t)(dtSec / HAL_SEC_PER_TICK);

  webots::Motor* motors[MOTOR_COUNT];
  motors[MOTOR_LEFT]  = robot.getMotor("LeftWheelMotor");
  motors[MOTOR_RIGHT] = robot.getMotor("RightWheelMotor");
  motors[MOTOR_LIFT]  = robot.getMotor("LiftMotor");
  motors[MOTOR_HEAD]  = robot.getMotor("HeadMotor");

  webots::PositionSensor* pos[MOTOR_COUNT];
  pos[MOTOR_LEFT]  = robot.getPositionSensor("LeftWheelMotorPosSensor");
  pos[MOTOR_RIGHT] = robot.getPositionSensor("RightWheelMotorPosSensor");
  pos[MOTOR_LIFT]  = robot.getPositionSensor("LiftMotorPosSensor");
  pos[MOTOR_HEAD]  = robot.getPositionSensor("HeadMotorPosSensor");

  const double INF = std::numeric_limits<double>::infinity();
  for (int m = 0; m < MOTOR_COUNT; ++m) {
    if (motors[m]) { motors[m]->setPosition(INF); motors[m]->setVelocity(0.0); }
    if (pos[m])    { pos[m]->enable(timeStepMs); }
  }

  webots::Gyro* gyro = robot.getGyro("gyro");
  webots::Accelerometer* accel = robot.getAccelerometer("accel");
  if (gyro)  gyro->enable(timeStepMs);
  if (accel) accel->enable(timeStepMs);

  const char* cliffNames[DROP_SENSOR_COUNT] =
    { "cliffSensorFL", "cliffSensorFR", "cliffSensorBL", "cliffSensorBR" };
  webots::DistanceSensor* cliff[DROP_SENSOR_COUNT];
  for (int i = 0; i < DROP_SENSOR_COUNT; ++i) {
    cliff[i] = robot.getDistanceSensor(cliffNames[i]);
    if (cliff[i]) cliff[i]->enable(timeStepMs);
  }

  webots::DistanceSensor* prox = robot.getDistanceSensor("forwardProxSensor");
  if (prox) prox->enable(timeStepMs);

  webots::LED* leds[4];
  leds[0] = robot.getLED("backpackLED0");
  leds[1] = robot.getLED("backpackLED1");
  leds[2] = robot.getLED("backpackLED2");
  leds[3] = robot.getLED("backpackLED3");

  webots::Connector* chargeContact = robot.getConnector("ChargeContact");
  if (chargeContact) chargeContact->enablePresence(timeStepMs);

  webots::Connector* gripperConnector = robot.getConnector("gripperConnector");
  bool gripperWasLocked = false;

  int panelSock = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (panelSock >= 0) {
    struct sockaddr_un paddr;
    memset(&paddr, 0, sizeof(paddr));
    paddr.sun_family = AF_UNIX;
    strncpy(paddr.sun_path, "/tmp/vector_panel.sock", sizeof(paddr.sun_path) - 1);
    unlink(paddr.sun_path);
    if (bind(panelSock, (struct sockaddr*)&paddr, sizeof(paddr)) < 0) {
      close(panelSock);
      panelSock = -1;
    } else {
      chmod(paddr.sun_path, 0777);
      int pfl = fcntl(panelSock, F_GETFL, 0);
      if (pfl >= 0) fcntl(panelSock, F_SETFL, pfl | O_NONBLOCK);
    }
  }
  struct PanelAxis {
    int val = 0;
    int lastNonzero = 0;
    double minUntil = -1.0;
    double alive = -1.0;
  };
  enum { PANEL_HEAD, PANEL_LIFT, PANEL_LW, PANEL_RW, PANEL_BTN, PANEL_PET, PANEL_CHG, PANEL_COUNT };
  PanelAxis panelAxes[PANEL_COUNT];
  const char* panelKeys[PANEL_COUNT] = { "head", "lift", "lw", "rw", "btn", "pet", "chg" };
  auto panelEffective = [&](int axis, double now) -> int {
    PanelAxis& a = panelAxes[axis];
    if (now < a.minUntil) return a.lastNonzero;
    if (a.val != 0 && now < a.alive) return a.val;
    return 0;
  };

  webots::Field* batteryVoltsField     = robot.getSelf()->getField("batteryVolts");
  webots::Field* backpackButtonField   = robot.getSelf()->getField("backpackButtonPressed");
  webots::Field* touchSensorField      = robot.getSelf()->getField("touchSensorTouched");

  webots::Display* faceDisplay = robot.getDisplay("face_display");
  webots::Camera* headCam = robot.getCamera("HeadCamera");
  int camStepMult = 1;
  while (camStepMult * timeStepMs < 66) ++camStepMult;
  const int camPeriodMs = camStepMult * timeStepMs;
  if (headCam) {
    headCam->enable(camPeriodMs);
    if (headCam->getWidth() != VIC_CAM_WIDTH || headCam->getHeight() != VIC_CAM_HEIGHT) {
      headCam = nullptr;
    }
  }

  const bool micLive = true;

  std::mutex micMutex;
  std::deque<int16_t> micRing;
  const size_t kMicRingMax = 16000 / 5; // ~200ms
  std::atomic<bool> micCaptureRun{false};
  std::thread micThread;

  webots::Keyboard* keyboard = robot.getKeyboard();
  if (keyboard) keyboard->enable(timeStepMs);

  if (micLive) {
    micCaptureRun.store(true);
    micThread = std::thread([&]() {
      const char* dev = getenv("VIC_MIC_DEV");
      if (!dev) dev = "default";
      snd_pcm_t* pcm = nullptr;
      int err = snd_pcm_open(&pcm, dev, SND_PCM_STREAM_CAPTURE, 0);
      if (err < 0) {
        return;
      }
      snd_pcm_hw_params_t* hw;
      snd_pcm_hw_params_alloca(&hw);
      snd_pcm_hw_params_any(pcm, hw);
      snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
      snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
      snd_pcm_hw_params_set_channels(pcm, hw, 1);
      unsigned int rate = 16000;
      snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0);
      snd_pcm_uframes_t period = 160;
      snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, 0);
      err = snd_pcm_hw_params(pcm, hw);
      if (err < 0) {
        snd_pcm_close(pcm);
        return;
      }
      std::vector<int16_t> buf(period);
      while (micCaptureRun.load()) {
        snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf.data(), period);
        if (n < 0) { snd_pcm_recover(pcm, (int)n, 1); continue; }
        if (n > 0) {
          std::lock_guard<std::mutex> lk(micMutex);
          for (snd_pcm_sframes_t i = 0; i < n; ++i) micRing.push_back(buf[(size_t)i]);
          while (micRing.size() > kMicRingMax) micRing.pop_front();
        }
      }
      snd_pcm_close(pcm);
    });
  }

  int sock = -1;
  int lastConnectErrno = 0;
  auto tryConnect = [&]() {
    if (sock >= 0) return;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
      return;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VIC_BRIDGE_BODY_SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
      int flags = fcntl(fd, F_GETFL, 0);
      if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
      sock = fd;
      lastConnectErrno = 0;
    } else {
      close(fd);
    }
  };

  int32_t prevCount[MOTOR_COUNT] = {0};
  uint32_t ticksSinceChange[MOTOR_COUNT] = {0};
  int32_t reportedDelta[MOTOR_COUNT] = {0};
  uint32_t reportedTime[MOTOR_COUNT] = {0};
  bool havePrev = false;
  const uint32_t maxStillTicks = (uint32_t)(0.125 / HAL_SEC_PER_TICK);
  uint32_t frameCounter = 0;
  uint16_t proxSampleCount = 0;
  double lastConnectAttempt = -1.0;

  float gyroHist[3][3] = {{0}};
  float accelHist[3][3] = {{0, 0, 0}, {0, 0, 0}, {9800, 9800, 9800}};
  int imuHistIdx = 0;
  bool imuHistFull = false;

  int faceSock = -1;
  double lastFaceConnect = -1.0;

  int camSock = -1;
  double lastCamConnect = -1.0;
  static VicCamFrame camTx;
  size_t camTxOff = sizeof(camTx);
  uint32_t camSeq = 0;
  long stepIndex = 0;

  int cubeSock = -1;
  double lastCubeConnect = -1.0;
  double lastCubeScan = -1.0;
  webots::Node* cubeNode = nullptr;
  std::string cubeFactoryId;
  double cubePrevVel[3] = {0.0, 0.0, 0.0};
  bool cubeHaveVel = false;
  long cubeAdvCtr = 0;
  uint8_t cubeLedRgb[12] = {0};
  bool cubeLedDirty = false;
  bool cubeTapPending = false;

  auto wallStart = std::chrono::steady_clock::now();
  const double simStart = robot.getTime();

  while (true) {
    if (sock < 0) {
      double now = robot.getTime();
      if (now - lastConnectAttempt >= 0.25) {
        lastConnectAttempt = now;
        tryConnect();
      }
    }

    VicBridgeH2B cmd;
    bool haveCmd = false;
    while (sock >= 0 && !haveCmd) {
      struct pollfd pfd;
      pfd.fd = sock; pfd.events = POLLIN; pfd.revents = 0;
      int pr = poll(&pfd, 1, 500);
      if (pr == 0) continue;
      if (pr < 0) { if (errno == EINTR) continue; close(sock); sock = -1; break; }
      if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
        close(sock); sock = -1; break;
      }
      ssize_t r = recv(sock, &cmd, sizeof(cmd), 0);
      if (r == (ssize_t)sizeof(cmd)) {
        if (cmd.magic == VIC_BRIDGE_MAGIC && cmd.version == VIC_BRIDGE_PROTO_VERSION) {
          haveCmd = true;
        }
        continue;
      }
      if (r == 0) {
        printf("brain disconnected\n");
        fflush(stdout);
        close(sock); sock = -1; break;
      }
      if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
      if (r < 0) { close(sock); sock = -1; break; }
    }

    while (haveCmd && sock >= 0) {
      VicBridgeH2B next;
      const ssize_t r = recv(sock, &next, sizeof(next), MSG_DONTWAIT);
      if (r == (ssize_t)sizeof(next)) {
        if (next.magic == VIC_BRIDGE_MAGIC && next.version == VIC_BRIDGE_PROTO_VERSION) {
          cmd = next;
        }
        continue;
      }
      if (r == 0) {
        printf("brain disconnected\n");
        fflush(stdout);
        close(sock); sock = -1;
      }
      break;
    }

    if (robot.step(timeStepMs) == -1) break;

    if (haveCmd) {
      for (int m = 0; m < MOTOR_COUNT; ++m) {
        if (!motors[m]) continue;
        const double p = cmd.h2b.motorPower[m] / 32767.0;
        double vel;
        if (m == MOTOR_LEFT || m == MOTOR_RIGHT) {
          vel = WHEEL_DRIVE_SIGN[m] * p * WHEEL_MAX_SPEED_MPS;
        } else if (m == MOTOR_LIFT) {
          vel = p * 20.0;
        } else {
          vel = p * 2.0 * PI;
        }
        const double now = robot.getTime();
        int nudge = 0;
        if (m == MOTOR_HEAD)       nudge = panelEffective(PANEL_HEAD, now);
        else if (m == MOTOR_LIFT)  nudge = panelEffective(PANEL_LIFT, now);
        else if (m == MOTOR_LEFT)  nudge = panelEffective(PANEL_LW, now);
        else if (m == MOTOR_RIGHT) nudge = panelEffective(PANEL_RW, now);
        if (nudge != 0) {
          if (m == MOTOR_LEFT || m == MOTOR_RIGHT) {
            vel += WHEEL_DRIVE_SIGN[m] * nudge * 0.4 * WHEEL_MAX_SPEED_MPS;
          } else if (m == MOTOR_LIFT) {
            vel += nudge * 4.0;
          } else {
            vel += nudge * 1.2;
          }
        }
        const double vmax = motors[m]->getMaxVelocity();
        if (vmax > 0.0) vel = std::max(-vmax, std::min(vmax, vel));
        motors[m]->setVelocity(vel);
      }

      auto rgb = [&](int base) -> unsigned int {
        uint8_t r = cmd.h2b.lightState.ledColors[base + 0];
        uint8_t g = cmd.h2b.lightState.ledColors[base + 1];
        uint8_t b = cmd.h2b.lightState.ledColors[base + 2];
        return ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
      };
      static const int kLedSlotForNode[4] = { 3, 0, 1, 2 };
      for (int i = 0; i < 4; ++i) {
        if (leds[i]) leds[i]->set(rgb(kLedSlotForNode[i] * LED_CHANEL_CT));
      }

      if (gripperConnector) {
        const bool lock = (cmd.gripper != 0);
        if (lock && !gripperWasLocked) {
          gripperConnector->lock();
          gripperConnector->enablePresence(timeStepMs);
        } else if (!lock && gripperWasLocked) {
          gripperConnector->unlock();
        }
        gripperWasLocked = lock;
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

    if (micLive) {
      std::lock_guard<std::mutex> lk(micMutex);
      for (int s = 0; s < AUDIO_SAMPLES_PER_FRAME; ++s) {
        int16_t v = 0;
        if (!micRing.empty()) { v = micRing.front(); micRing.pop_front(); }
        int16_t* dst = &b.audio[s * 4];
        dst[0] = dst[1] = dst[2] = dst[3] = v;
      }
    }

    for (int m = 0; m < MOTOR_COUNT; ++m) {
      double physical = 0.0;
      if (pos[m]) {
        if (m == MOTOR_LEFT || m == MOTOR_RIGHT) {
          physical = pos[m]->getValue() * WHEEL_ENCODER_MM_SIGN;
        } else {
          physical = pos[m]->getValue();
        }
      }
      const int32_t count = (int32_t)llround(physical / HAL_MOTOR_POSITION_SCALE[m]);
      b.motor[m].position = count;

      if (!havePrev) {
        prevCount[m] = count;
        b.motor[m].delta = 0;
        b.motor[m].time = 0;
        continue;
      }

      ticksSinceChange[m] += frameTimeTicks;
      if (count != prevCount[m]) {
        reportedDelta[m] = count - prevCount[m];
        reportedTime[m]  = ticksSinceChange[m];
        prevCount[m] = count;
        ticksSinceChange[m] = 0;
      } else if (ticksSinceChange[m] >= maxStillTicks) {
        reportedDelta[m] = 0;
        reportedTime[m]  = 0;
      }

      b.motor[m].delta = reportedDelta[m];
      b.motor[m].time  = reportedTime[m];
    }
    havePrev = true;

    for (int i = 0; i < DROP_SENSOR_COUNT; ++i) {
      double v = cliff[i] ? cliff[i]->getValue() : 800.0;
      if (v < 0) v = 0;
      if (v > 65535.0) v = 65535.0;
      b.cliffSense[i] = (uint16_t)v;
    }

    const double panelNow = robot.getTime();
    double volts = batteryVoltsField ? batteryVoltsField->getSFFloat() : 4.1;
    b.battery.main_voltage = (int16_t)(volts / BATTERY_SCALE);
    b.battery.charger = 0;
    b.battery.temperature = 25;
    b.battery.flags = 0;
    if ((chargeContact && chargeContact->getPresence() == 1) ||
        panelEffective(PANEL_CHG, panelNow) != 0) {
      b.battery.flags = (BatteryFlags)(POWER_ON_CHARGER | POWER_IS_CHARGING);
      b.battery.charger = (int16_t)(5.0 / BATTERY_SCALE);
    }

    {
      double mm = prox ? prox->getValue() : 100.0;
      if (mm < 0) mm = 0;
      if (mm > 65535.0) mm = 65535.0;
      b.proximity.rangeStatus = 0x58;
      b.proximity.spare1 = 0;
      b.proximity.rangeMM = FlipBytes((uint16_t)mm);
      b.proximity.signalRate = FlipBytes((uint16_t)(25.0 * 128.0));
      b.proximity.ambientRate = 0;
      b.proximity.spadCount = FlipBytes((uint16_t)(90.0 * 256.0));
      b.proximity.sampleCount = ++proxSampleCount;
      b.proximity.calibrationResult = 0;
    }

    const bool petting = (panelEffective(PANEL_PET, panelNow) != 0);
    const bool touched = (touchSensorField && touchSensorField->getSFBool()) || petting;
    const bool buttonPressed = (backpackButtonField && backpackButtonField->getSFBool()) ||
                               (panelEffective(PANEL_BTN, panelNow) != 0);
    const uint16_t kTouchBaseline = 0;
    const uint16_t kTouchTouched  = 300;
    uint16_t touchVal = touched ? kTouchTouched : kTouchBaseline;
    if (petting) {
      touchVal = (uint16_t)(kTouchTouched +
                            60.0 * sin(2.0 * PI * 1.3 * panelNow) +
                            (double)((frameCounter * 2654435761u >> 20) & 0x1F));
    }
    b.touchLevel[0] = 0;
    b.touchLevel[1] = buttonPressed ? 0xFFFF : 0x0000;
    b.touchHires[0] = touchVal;
    b.touchHires[1] = 0;

    const uint16_t micPattern = (frameCounter & 1) ? 0xFFFF : 0x0000;
    b.micError[0] = micPattern;
    b.micError[1] = micPattern;

    float rawGyro[3] = {0, 0, 0};
    float rawAccel[3] = {0, 0, 9800.f};
    if (gyro) {
      const double* g = gyro->getValues();
      for (int i = 0; i < 3; ++i) rawGyro[i] = (float)g[i];
    }
    if (accel) {
      const double* a = accel->getValues();
      for (int i = 0; i < 3; ++i) rawAccel[i] = (float)(a[i] * 1000.0);
    }
    for (int i = 0; i < 3; ++i) {
      gyroHist[i][imuHistIdx]  = rawGyro[i];
      accelHist[i][imuHistIdx] = rawAccel[i];
    }
    imuHistIdx = (imuHistIdx + 1) % 3;
    if (imuHistIdx == 0) imuHistFull = true;
    for (int i = 0; i < 3; ++i) {
      out.imu.gyro[i]  = imuHistFull ? Median3(gyroHist[i][0],  gyroHist[i][1],  gyroHist[i][2])  : rawGyro[i];
      out.imu.accel[i] = imuHistFull ? Median3(accelHist[i][0], accelHist[i][1], accelHist[i][2]) : rawAccel[i];
    }
    {
      const double t = robot.getTime();
      out.imu.temperature_degC = (float)(70.0 - (70.0 - 20.0) * exp(-0.0032 * t));
    }

    if (sock >= 0) {
      ssize_t w = send(sock, &out, sizeof(out), MSG_NOSIGNAL);
      if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        close(sock); sock = -1;
      }
    }

    if (faceDisplay != nullptr) {
      if (faceSock < 0) {
        double now = robot.getTime();
        if (now - lastFaceConnect >= 0.25) {
          lastFaceConnect = now;
          int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
          if (fd >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, VIC_BRIDGE_FACE_SOCK_PATH, sizeof(addr.sun_path) - 1);
            if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
              int rcvbuf = 256 * 1024;
              setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
              int fl = fcntl(fd, F_GETFL, 0);
              if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
              faceSock = fd;
              printf("connected to %s\n", VIC_BRIDGE_FACE_SOCK_PATH);
              fflush(stdout);
            } else {
              close(fd);
            }
          }
        }
      }
      if (faceSock >= 0) {
        static VicFaceFrame ff;
        bool haveFace = false;
        for (;;) {
          ssize_t r = recv(faceSock, &ff, sizeof(ff), 0);
          if (r == (ssize_t)sizeof(ff) &&
              ff.magic == VIC_FACE_MAGIC && ff.version == VIC_FACE_VERSION) {
            haveFace = true;
            continue;
          }
          if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
          if (r == 0 || r < 0) { close(faceSock); faceSock = -1; break; }
          break;
        }
        if (haveFace) {
          static unsigned int bgra[VIC_FACE_NPIX];
          for (int i = 0; i < VIC_FACE_NPIX; ++i) {
            const uint16_t px = ff.rgb565[i];
            const unsigned int r = (((px >> 11) & 0x1F) * 255) / 31;
            const unsigned int g = (((px >>  5) & 0x3F) * 255) / 63;
            const unsigned int b = ( (px        & 0x1F) * 255) / 31;
            bgra[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
          }
          webots::ImageRef* ref =
            faceDisplay->imageNew(VIC_FACE_WIDTH, VIC_FACE_HEIGHT, bgra, webots::Display::BGRA);
          faceDisplay->imagePaste(ref, 0, 0);
          faceDisplay->imageDelete(ref);
        }
      }
    }

    if (headCam != nullptr) {
      if (camSock < 0) {
        double now = robot.getTime();
        if (now - lastCamConnect >= 0.25) {
          lastCamConnect = now;
          int fd = socket(AF_UNIX, SOCK_STREAM, 0);
          if (fd >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, VIC_BRIDGE_CAM_SOCK_PATH, sizeof(addr.sun_path) - 1);
            if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
              int sndbuf = 2 * 1024 * 1024;
              setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
              int fl = fcntl(fd, F_GETFL, 0);
              if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
              camSock = fd;
              camTxOff = sizeof(camTx);
              printf("connected to %s\n", VIC_BRIDGE_CAM_SOCK_PATH);
              fflush(stdout);
            } else {
              close(fd);
            }
          }
        }
      }

      if (camSock >= 0) {
        if (camTxOff >= sizeof(camTx) && (stepIndex % camStepMult) == 0) {
          const unsigned char* img = headCam->getImage();
          if (img != nullptr) {
            camTx.magic   = VIC_CAM_MAGIC;
            camTx.version = VIC_CAM_VERSION;
            camTx.seq     = camSeq++;
            camTx.width   = VIC_CAM_WIDTH;
            camTx.height  = VIC_CAM_HEIGHT;
            static const bool kCamRot180 = (getenv("VIC_CAM_ROT180") != nullptr);
            BGRAtoYUV420sp(img, VIC_CAM_WIDTH, VIC_CAM_HEIGHT, camTx.yuv, kCamRot180);
            camTxOff = 0;
          }
        }
        if (camTxOff < sizeof(camTx)) {
          for (;;) {
            ssize_t w = send(camSock, (uint8_t*)&camTx + camTxOff,
                             sizeof(camTx) - camTxOff, MSG_NOSIGNAL);
            if (w > 0) {
              camTxOff += (size_t)w;
              if (camTxOff >= sizeof(camTx)) break;
              continue;
            }
            if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            close(camSock);
            camSock = -1;
            camTxOff = sizeof(camTx);
            break;
          }
        }
      }
    }
    ++stepIndex;

    if (panelSock >= 0) {
      char pbuf[64];
      ssize_t pr;
      while ((pr = recv(panelSock, pbuf, sizeof(pbuf) - 1, MSG_DONTWAIT)) > 0) {
        pbuf[pr] = '\0';
        const char* colon = strchr(pbuf, ':');
        if (colon == nullptr) continue;
        const int v = atoi(colon + 1);
        const double now = robot.getTime();
        if (strncmp(pbuf, "tap", (size_t)(colon - pbuf)) == 0 && (size_t)(colon - pbuf) == 3) {
          if (v != 0) cubeTapPending = true;
          continue;
        }
        for (int a = 0; a < PANEL_COUNT; ++a) {
          const size_t keyLen = (size_t)(colon - pbuf);
          if (strlen(panelKeys[a]) == keyLen && strncmp(pbuf, panelKeys[a], keyLen) == 0) {
            PanelAxis& ax = panelAxes[a];
            if (v != 0) {
              if (ax.val == 0) ax.minUntil = now + 0.12;
              ax.val = v;
              ax.lastNonzero = v;
              ax.alive = now + 0.5;
            } else {
              ax.val = 0;
            }
            break;
          }
        }
      }
    }

    if (cubeSock < 0) {
      const double now = robot.getTime();
      if (now - lastCubeConnect >= 0.25) {
        lastCubeConnect = now;
        int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (fd >= 0) {
          struct sockaddr_un caddr;
          memset(&caddr, 0, sizeof(caddr));
          caddr.sun_family = AF_UNIX;
          strncpy(caddr.sun_path, VIC_BRIDGE_CUBE_SOCK_PATH, sizeof(caddr.sun_path) - 1);
          if (connect(fd, (struct sockaddr*)&caddr, sizeof(caddr)) == 0) {
            int fl = fcntl(fd, F_GETFL, 0);
            if (fl >= 0) { fcntl(fd, F_SETFL, fl | O_NONBLOCK); }
            cubeSock = fd;
          } else {
            close(fd);
          }
        }
      }
    }
    if (cubeSock >= 0) {
      for (;;) {
        VicCubeDown down;
        const ssize_t r = recv(cubeSock, &down, sizeof(down), MSG_DONTWAIT);
        if (r == (ssize_t)sizeof(down)) {
          if (down.magic == VIC_CUBE_MAGIC && down.version == VIC_CUBE_VERSION &&
              down.type == VIC_CUBE_MSG_LEDS) {
            memcpy(cubeLedRgb, down.rgb, sizeof(cubeLedRgb));
            cubeLedDirty = true;
          }
          continue;
        }
        if (r == 0) {
          close(cubeSock); cubeSock = -1; cubeNode = nullptr; cubeHaveVel = false;
        }
        break;
      }
    }
    if (cubeSock >= 0) {
      if (cubeNode == nullptr) {
        const double now = robot.getTime();
        if (now - lastCubeScan >= 0.5) {
          lastCubeScan = now;
          cubeNode = FindLightCubeNode(robot);
          if (cubeNode != nullptr) {
            webots::Field* fid = cubeNode->getField("factoryID");
            cubeFactoryId = (fid && !fid->getSFString().empty())
                              ? fid->getSFString() : std::string("AA:BB:CC:DD:EE:01");
            cubeHaveVel = false;
          }
        }
      }
      if (cubeNode != nullptr && cubeLedDirty) {
        for (int i = 0; i < 4; ++i) {
          webots::Field* f = cubeNode->getField("led" + std::to_string(i) + "Color");
          if (f != nullptr) {
            const double c[3] = { cubeLedRgb[3*i + 0] / 255.0,
                                  cubeLedRgb[3*i + 1] / 255.0,
                                  cubeLedRgb[3*i + 2] / 255.0 };
            f->setSFColor(c);
          }
        }
        cubeLedDirty = false;
      }
      if (cubeNode != nullptr) {
        const double* R   = cubeNode->getOrientation();
        const double* vel = cubeNode->getVelocity();
        if (R != nullptr && vel != nullptr) {
          double aWorld[3] = {0.0, 0.0, 0.0};
          if (cubeHaveVel) {
            for (int i = 0; i < 3; ++i) aWorld[i] = (vel[i] - cubePrevVel[i]) / dtSec;
          }
          for (int i = 0; i < 3; ++i) cubePrevVel[i] = vel[i];
          cubeHaveVel = true;

          const double sfWorld[3] = { aWorld[0], aWorld[1], aWorld[2] + 9.81 };
          double sfBody[3];
          for (int i = 0; i < 3; ++i) {
            double s = 0.0;
            for (int j = 0; j < 3; ++j) s += R[j*3 + i] * sfWorld[j];
            sfBody[i] = s;
          }

          VicCubeUp up;
          memset(&up, 0, sizeof(up));
          up.magic = VIC_CUBE_MAGIC;
          up.version = VIC_CUBE_VERSION;
          strncpy(up.factoryId, cubeFactoryId.c_str(), VIC_CUBE_FACTORYID_LEN - 1);

          if ((cubeAdvCtr++ % 200) == 0) {
            up.type = VIC_CUBE_MSG_ADVERTISE;
            send(cubeSock, &up, sizeof(up), MSG_NOSIGNAL | MSG_DONTWAIT);
          }

          up.type = VIC_CUBE_MSG_ACCEL;
          for (int i = 0; i < 3; ++i) {
            double sc = (sfBody[i] / 9.81) * 32767.0 / 4.0;
            sc = std::max(-32768.0, std::min(32767.0, sc));
            up.accel[i] = (int16_t)lrint(sc);
          }
          up.tapped = CubeCheckForTap((float)sfBody[0], (float)sfBody[1], (float)sfBody[2], timeStepMs) ? 1 : 0;
          if (cubeTapPending) {
            up.tapped = 1;
            cubeTapPending = false;
          }
          webots::Field* bv = cubeNode->getField("batteryVolts");
          const double volts = bv ? bv->getSFFloat() : 1.5;
          up.railVoltageCnts = (uint16_t)(volts * 1024.0 / 3.6);

          const ssize_t w = send(cubeSock, &up, sizeof(up), MSG_NOSIGNAL | MSG_DONTWAIT);
          if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            close(cubeSock); cubeSock = -1; cubeNode = nullptr; cubeHaveVel = false;
          }
        }
      }
    }

    const double simElapsed = robot.getTime() - simStart;
    const double wallElapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
    const double ahead = simElapsed - wallElapsed;
    if (ahead > 0.0) {
      std::this_thread::sleep_for(std::chrono::duration<double>(ahead));
    } else if (ahead < -0.5) {
      wallStart = std::chrono::steady_clock::now() -
                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(simElapsed));
    }
  }

  if (micThread.joinable()) {
    micCaptureRun.store(false);
    micThread.join();
  }
  if (sock >= 0) close(sock);
  return 0;
}
