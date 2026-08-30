#ifndef __COZMO_BODY_H__
#define __COZMO_BODY_H__

#include <stdint.h>

#include "schema/messages.h"

namespace cozmo {

extern const double kHalSecPerTick;
extern const double kHalMotorScale[MOTOR_COUNT];
extern const double kHalMotorDirection[MOTOR_COUNT];
extern const double kBatteryScale;
extern const double kSpeedWindowSec;

extern const double kVicHeadMinRad;
extern const double kVicHeadMaxRad;
extern const double kCozHeadMinRad;
extern const double kCozHeadMaxRad;
extern const double kCozHeadCmdMargin;

extern double kMeasDeadbandCounts;
extern double kMaxExtrapolateSec;
extern const double kMaxMeasGapSec;

double MeasDeadband(int motor);
double SpeedQuantum(int motor);

double RemapHeadAngle(double cozRad);
double UnmapHeadAngle(double vicRad);
uint16_t Rgb555(uint8_t r, uint8_t g, uint8_t b);

void FillMotorState(MotorState& ms, double physical, double rate, double scale);

double HalPosition(const MotorState& ms, int motor, int32_t offset);
double HalSpeed(const MotorState& ms, int motor);

struct AxisEstimate {
  double physical;
  double rate;
  double lastMeas;
  double lastMeasTime;
  double ahead;
  bool   haveMeas;

  AxisEstimate();
  void Measure(double meas, double now, double deadband);
  void SetRate(double r, double deadband);
  void Advance(double dt);
  void Integrate(double dt);
};

}  // namespace cozmo

#endif
