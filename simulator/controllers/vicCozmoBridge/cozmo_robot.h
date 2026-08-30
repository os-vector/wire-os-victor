#ifndef __COZMO_ROBOT_H__
#define __COZMO_ROBOT_H__

#include <stdint.h>

#include <string>

#include <stddef.h>

#include "cozmo_link.h"

namespace cozmo {

enum CommandId {
  kCmdLightStateCenter     = 0x03,
  kCmdCubeLights           = 0x04,
  kCmdObjectConnect        = 0x05,
  kCmdStreamObjectAccel    = 0x08,
  kCmdSetAccessoryDiscovery = 0x0a,
  kCmdSetHeadLight         = 0x0b,
  kCmdLightStateSide       = 0x11,
  kCmdEnable               = 0x25,
  kCmdDriveWheels          = 0x32,
  kCmdMoveLift             = 0x34,
  kCmdMoveHead             = 0x35,
  kCmdSetLiftHeight        = 0x36,
  kCmdSetHeadAngle         = 0x37,
  kCmdStopAllMotors        = 0x3b,
  kCmdSetOrigin            = 0x45,
  kCmdSyncTime             = 0x4b,
  kCmdEnableCamera         = 0x4c,
  kCmdSetCameraParams      = 0x57,
  kCmdSetRobotVolume       = 0x64,
  kCmdEnableColorImages    = 0x66,
  kCmdOutputAudio          = 0x8e,
  kCmdOutputSilence        = 0x8f,
  kCmdDisplayImage         = 0x97,
  kCmdEnableAnimationState = 0x9f,
  kCmdDebugData            = 0xb0,
  kCmdObjectTapped         = 0xb6,
  kCmdRobotDelocalized     = 0xc2,
  kCmdHardwareInfo         = 0xc9,
  kCmdMotorCalibration     = 0xd1,
  kCmdButtonPressed        = 0xdb,
  kCmdBodyInfo             = 0xed,
  kCmdFirmwareSignature    = 0xee,
  kEvtRobotState           = 0xf0,
  kEvtAnimationState       = 0xf1,
  kEvtImageChunk           = 0xf2,
  kEvtObjectAvailable      = 0xf3
};

enum RobotStatusFlag {
  kStatusIsMoving          = 0x00001,
  kStatusIsCarryingBlock   = 0x00002,
  kStatusIsPickingOrPlacing = 0x00004,
  kStatusIsPickedUp        = 0x00008,
  kStatusIsBodyAccMode     = 0x00010,
  kStatusIsFalling         = 0x00020,
  kStatusIsAnimating       = 0x00040,
  kStatusIsPathing         = 0x00080,
  kStatusLiftInPos         = 0x00100,
  kStatusHeadInPos         = 0x00200,
  kStatusIsAnimBufferFull  = 0x00400,
  kStatusIsAnimatingIdle   = 0x00800,
  kStatusIsOnCharger       = 0x01000,
  kStatusIsCharging        = 0x02000,
  kStatusCliffDetected     = 0x04000,
  kStatusAreWheelsMoving   = 0x08000,
  kStatusIsChargerOos      = 0x10000
};

static const size_t kRobotStateSize = 91;

static const float kLiftArmLengthMm    = 66.0f;
static const float kLiftPivotHeightMm  = 45.0f;
static const float kLiftMinHeightMm    = 32.0f;
static const float kLiftMaxHeightMm    = 92.0f;
static const float kHeadMinAngleRad    = -0.4363f;
static const float kHeadMaxAngleRad    = 0.7767f;
static const float kLiftMinAngleRad    = -0.19827f;
static const float kLiftMaxAngleRad    = 0.79594f;
static const float kMaxWheelSpeedMmps  = 200.0f;

float LiftAngleToHeightMm(float angleRad);
float LiftHeightMmToAngle(float heightMm);

struct RobotState {
  uint32_t timestamp;
  uint32_t poseFrameId;
  uint32_t poseOriginId;
  float    poseX;
  float    poseY;
  float    poseZ;
  float    poseAngleRad;
  float    posePitchRad;
  float    lwheelSpeedMmps;
  float    rwheelSpeedMmps;
  float    headAngleRad;
  float    liftAngleRad;
  float    accel[3];
  float    gyro[3];
  float    batteryVoltage;
  uint32_t status;
  uint16_t cliffRaw[4];
  uint16_t backpackTouchRaw;
  uint8_t  currPathSegment;

  RobotState() { memset(this, 0, sizeof(*this)); }
};

bool ParseRobotState(const Packet& pkt, RobotState& out);
std::string DescribeStatus(uint32_t status);

class Robot {
public:
  Robot();

  bool Open(const char* host, uint16_t port);
  void Close();

  void Update();
  bool IsReady() const { return ready_; }
  bool HaveState() const { return haveState_; }

  const RobotState& State() const { return state_; }
  int32_t AnimFramesPlayed() const { return animFramesPlayed_; }
  bool HaveAnimState() const { return haveAnimState_; }
  const std::string& FirmwareSignature() const { return firmwareSignature_; }
  uint32_t FirmwareVersion() const { return firmwareVersion_; }
  Link& GetLink() { return link_; }

  void SetVerbose(bool v) { verbose_ = v; }
  void SetEnableAnimationState(bool v) { wantAnimState_ = v; }
  void SetEnableCamera(bool v) { wantCamera_ = v; }
  void SetEnableColor(bool v) { wantColor_ = v; }
  void SetObserver(const Link::PacketHandler& h) { observer_ = h; }

  void DriveWheels(float lmmps, float rmmps, float laccel, float raccel);
  void MoveHead(float radPerSec);
  void SetHeadAngle(float angleRad, float maxSpeedRadPerSec, float accelRadPerSec2, float durationSec, uint8_t actionId);
  void MoveLift(float radPerSec);
  void SetLiftHeight(float heightMm, float maxSpeedRadPerSec, float accelRadPerSec2, float durationSec, uint8_t actionId);
  void StopAllMotors();
  void DisplayImage(const uint8_t* rle, size_t len);
  void OutputSilence();
  void OutputAudio(const uint8_t* mulaw744);
  void SetRobotVolume(uint16_t level);
  void SetCameraParams(float gain, uint16_t exposureMs, bool requestDefaults);

private:
  void OnPacket(const Packet& pkt);

  Link link_;
  RobotState state_;
  bool haveState_;
  bool haveAnimState_;
  int32_t animFramesPlayed_;
  bool ready_;
  bool verbose_;
  bool wantAnimState_;
  bool wantCamera_;
  bool wantColor_;
  bool sawSignature_;
  bool enableSent_;
  bool initSent_;
  double initAt_;
  std::string firmwareSignature_;
  uint32_t firmwareVersion_;
  Link::PacketHandler observer_;
};

}  // namespace cozmo

#endif
