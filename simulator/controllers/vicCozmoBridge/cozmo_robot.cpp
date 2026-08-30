#include "cozmo_robot.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stddef.h>

namespace cozmo {

float LiftAngleToHeightMm(float angleRad)
{
  return kLiftPivotHeightMm + kLiftArmLengthMm * sinf(angleRad);
}

float LiftHeightMmToAngle(float heightMm)
{
  float s = (heightMm - kLiftPivotHeightMm) / kLiftArmLengthMm;
  if (s > 1.f) {
    s = 1.f;
  } else if (s < -1.f) {
    s = -1.f;
  }
  return asinf(s);
}

bool ParseRobotState(const Packet& pkt, RobotState& out)
{
  if (pkt.payload.size() < kRobotStateSize) {
    return false;
  }
  Reader r(&pkt.payload[0], pkt.payload.size());
  out.timestamp = r.U32();
  out.poseFrameId = r.U32();
  out.poseOriginId = r.U32();
  out.poseX = r.F32();
  out.poseY = r.F32();
  out.poseZ = r.F32();
  out.poseAngleRad = r.F32();
  out.posePitchRad = r.F32();
  out.lwheelSpeedMmps = r.F32();
  out.rwheelSpeedMmps = r.F32();
  out.headAngleRad = r.F32();
  out.liftAngleRad = r.F32();
  for (int i = 0; i < 3; ++i) {
    out.accel[i] = r.F32();
  }
  for (int i = 0; i < 3; ++i) {
    out.gyro[i] = r.F32();
  }
  out.batteryVoltage = r.F32();
  out.status = r.U32();
  for (int i = 0; i < 4; ++i) {
    out.cliffRaw[i] = r.U16();
  }
  out.backpackTouchRaw = r.U16();
  out.currPathSegment = r.U8();
  return r.Ok();
}

std::string DescribeStatus(uint32_t status)
{
  static const struct { uint32_t bit; const char* name; } kFlags[] = {
    { kStatusIsMoving, "IS_MOVING" },
    { kStatusIsCarryingBlock, "IS_CARRYING_BLOCK" },
    { kStatusIsPickingOrPlacing, "IS_PICKING_OR_PLACING" },
    { kStatusIsPickedUp, "IS_PICKED_UP" },
    { kStatusIsBodyAccMode, "IS_BODY_ACC_MODE" },
    { kStatusIsFalling, "IS_FALLING" },
    { kStatusIsAnimating, "IS_ANIMATING" },
    { kStatusIsPathing, "IS_PATHING" },
    { kStatusLiftInPos, "LIFT_IN_POS" },
    { kStatusHeadInPos, "HEAD_IN_POS" },
    { kStatusIsAnimBufferFull, "IS_ANIM_BUFFER_FULL" },
    { kStatusIsAnimatingIdle, "IS_ANIMATING_IDLE" },
    { kStatusIsOnCharger, "IS_ON_CHARGER" },
    { kStatusIsCharging, "IS_CHARGING" },
    { kStatusCliffDetected, "CLIFF_DETECTED" },
    { kStatusAreWheelsMoving, "ARE_WHEELS_MOVING" },
    { kStatusIsChargerOos, "IS_CHARGER_OOS" }
  };

  std::string res;
  for (size_t i = 0; i < sizeof(kFlags) / sizeof(kFlags[0]); ++i) {
    if (status & kFlags[i].bit) {
      if (!res.empty()) {
        res += "|";
      }
      res += kFlags[i].name;
    }
  }
  return res.empty() ? std::string("-") : res;
}

Robot::Robot()
  : haveState_(false)
  , haveAnimState_(false)
  , animFramesPlayed_(0)
  , ready_(false)
  , verbose_(true)
  , wantAnimState_(false)
  , wantCamera_(false)
  , wantColor_(false)
  , sawSignature_(false)
  , enableSent_(false)
  , initSent_(false)
  , initAt_(0.0)
  , firmwareVersion_(0)
{
}

bool Robot::Open(const char* host, uint16_t port)
{
  if (!link_.Open(host, port, false)) {
    return false;
  }
  link_.SetHandler([this](const Packet& p) { OnPacket(p); });
  link_.Connect();
  return true;
}

void Robot::Close()
{
  if (link_.GetState() == Link::kConnected) {
    StopAllMotors();
    link_.Update();
    link_.Disconnect();
    link_.Update();
  }
  link_.Close();
}

void Robot::OnPacket(const Packet& pkt)
{
  if (observer_) {
    observer_(pkt);
  }

  if (pkt.type != kPacketCommand && pkt.type != kPacketEvent) {
    return;
  }

  if (pkt.id == kEvtRobotState) {
    if (ParseRobotState(pkt, state_)) {
      haveState_ = true;
    }
    return;
  }

  if (pkt.id == kEvtAnimationState) {
    if (pkt.payload.size() >= 12) {
      Reader r(&pkt.payload[0], pkt.payload.size());
      r.Skip(4);
      r.Skip(4);
      const int32_t played = (int32_t)r.U32();
      if (r.Ok()) {
        animFramesPlayed_ = played;
        haveAnimState_ = true;
      }
    }
    return;
  }

  if (pkt.id == kCmdFirmwareSignature && !sawSignature_) {
    sawSignature_ = true;
    if (pkt.payload.size() >= 4) {
      Reader r(&pkt.payload[0], pkt.payload.size());
      r.Skip(2);
      const uint16_t len = r.U16();
      for (uint16_t i = 0; i < len && r.Remaining() > 0; ++i) {
        firmwareSignature_.push_back((char)r.U8());
      }
      const size_t vpos = firmwareSignature_.find("\"version\":");
      if (vpos != std::string::npos) {
        firmwareVersion_ = (uint32_t)strtoul(firmwareSignature_.c_str() + vpos + 10, NULL, 10);
      }
    }
    if (verbose_) {
      printf("  firmware version %u\n", firmwareVersion_);
    }
  }
}

void Robot::Update()
{
  link_.Update();
  const double now = MonotonicSeconds();

  if (!enableSent_ && sawSignature_) {
    enableSent_ = true;
    initAt_ = now + 0.1;
    const Writer empty;
    link_.SendCommand(kCmdEnable, empty);
    link_.SendCommand(kCmdEnable, empty);
  }

  if (enableSent_ && !initSent_ && now >= initAt_) {
    initSent_ = true;

    Writer origin;
    origin.U32(0);
    origin.U32(0);
    origin.U32(1);
    origin.F32(0.f);
    origin.F32(0.f);
    origin.U32(0x80000000u);
    link_.SendCommand(kCmdSetOrigin, origin);

    Writer sync;
    sync.U32(0);
    sync.U32(0);
    link_.SendCommand(kCmdSyncTime, sync);

    if (wantAnimState_) {
      const Writer empty;
      link_.SendCommand(kCmdEnableAnimationState, empty);
    }

    if (wantCamera_) {
      Writer color;
      color.I8(wantColor_ ? 1 : 0);
      link_.SendCommand(kCmdEnableColorImages, color);

      Writer cam;
      cam.I8(1);
      cam.I8(4);
      link_.SendCommand(kCmdEnableCamera, cam);
    }
  }

  if (initSent_ && haveState_) {
    ready_ = true;
  }
}

void Robot::SetCameraParams(float gain, uint16_t exposureMs, bool requestDefaults)
{
  Writer w;
  w.F32(gain);
  w.U16(exposureMs);
  w.I8(requestDefaults ? 1 : 0);
  link_.SendCommand(kCmdSetCameraParams, w);
}

void Robot::DriveWheels(float lmmps, float rmmps, float laccel, float raccel)
{
  Writer w;
  w.F32(lmmps);
  w.F32(rmmps);
  w.F32(laccel);
  w.F32(raccel);
  link_.SendCommand(kCmdDriveWheels, w);
}

void Robot::MoveHead(float radPerSec)
{
  Writer w;
  w.F32(radPerSec);
  link_.SendCommand(kCmdMoveHead, w);
}

void Robot::DisplayImage(const uint8_t* rle, size_t len)
{
  if (len > kMaxDisplayRleSize) {
    return;
  }
  Writer w;
  w.U16((uint16_t)len);
  for (size_t i = 0; i < len; ++i) {
    w.U8(rle[i]);
  }
  link_.SendEvent(kCmdDisplayImage, w);
}

void Robot::OutputSilence()
{
  const Writer empty;
  link_.SendEvent(kCmdOutputSilence, empty);
}

void Robot::OutputAudio(const uint8_t* mulaw744)
{
  Writer w;
  for (size_t i = 0; i < 744; ++i) {
    w.U8(mulaw744[i]);
  }
  link_.SendEvent(kCmdOutputAudio, w);
}

void Robot::SetRobotVolume(uint16_t level)
{
  Writer w;
  w.U16(level);
  link_.SendCommand(kCmdSetRobotVolume, w);
}

void Robot::SetHeadAngle(float angleRad, float maxSpeedRadPerSec, float accelRadPerSec2,
                         float durationSec, uint8_t actionId)
{
  Writer w;
  w.F32(angleRad);
  w.F32(maxSpeedRadPerSec);
  w.F32(accelRadPerSec2);
  w.F32(durationSec);
  w.U8(actionId);
  link_.SendCommand(kCmdSetHeadAngle, w);
}

void Robot::SetLiftHeight(float heightMm, float maxSpeedRadPerSec, float accelRadPerSec2,
                          float durationSec, uint8_t actionId)
{
  Writer w;
  w.F32(heightMm);
  w.F32(maxSpeedRadPerSec);
  w.F32(accelRadPerSec2);
  w.F32(durationSec);
  w.U8(actionId);
  link_.SendCommand(kCmdSetLiftHeight, w);
}

void Robot::MoveLift(float radPerSec)
{
  Writer w;
  w.F32(radPerSec);
  link_.SendCommand(kCmdMoveLift, w);
}

void Robot::StopAllMotors()
{
  const Writer empty;
  link_.SendCommand(kCmdStopAllMotors, empty);
}

}  // namespace cozmo
