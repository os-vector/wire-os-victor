#include "cozmo_body.h"

#include <math.h>

namespace cozmo {

static const double kPi = 3.14159265358979323846;

const double kHalSecPerTick = (1.0 / 256.0) / 48000000.0;
const double kSpeedWindowSec = 0.25;

const double kHalMotorScale[MOTOR_COUNT] = {
  ((       0.96 * 29.0 * 0.25 * kPi) / 172.3),
  ((-1.0 * 0.96 * 29.0 * 0.25 * kPi) / 172.3),
  ((0.25 * kPi) / 149.7),
  ((0.25 * kPi) / 366.211),
};

const double kHalMotorDirection[MOTOR_COUNT] = { 1.0, -1.0, 1.0, 1.0 };

const double kBatteryScale = 2.8 / 2048.0;

const double kVicHeadMinRad = -22.0 * kPi / 180.0;
const double kVicHeadMaxRad =  45.0 * kPi / 180.0;
const double kCozHeadMinRad = -0.4363;
const double kCozHeadMaxRad =  0.7767;
const double kCozHeadCmdMargin = 0.010;

double RemapHeadAngle(double cozRad)
{
  const double cozSpan = kCozHeadMaxRad - kCozHeadMinRad;
  const double vicSpan = kVicHeadMaxRad - kVicHeadMinRad;
  return kVicHeadMinRad + (cozRad - kCozHeadMinRad) * (vicSpan / cozSpan);
}

double UnmapHeadAngle(double vicRad)
{
  const double cozSpan = kCozHeadMaxRad - kCozHeadMinRad;
  const double vicSpan = kVicHeadMaxRad - kVicHeadMinRad;
  double coz = kCozHeadMinRad + (vicRad - kVicHeadMinRad) * (cozSpan / vicSpan);
  const double lo = kCozHeadMinRad + kCozHeadCmdMargin;
  const double hi = kCozHeadMaxRad - kCozHeadCmdMargin;
  if (coz < lo) {
    coz = lo;
  } else if (coz > hi) {
    coz = hi;
  }
  return coz;
}

uint16_t Rgb555(uint8_t r, uint8_t g, uint8_t b)
{
  const uint16_t r5 = (uint16_t)(((uint32_t)r * 31u) / 255u);
  const uint16_t g5 = (uint16_t)(((uint32_t)g * 31u) / 255u);
  const uint16_t b5 = (uint16_t)(((uint32_t)b * 31u) / 255u);
  return (uint16_t)((r5 << 10) | (g5 << 5) | b5);
}

void FillMotorState(MotorState& ms, double physical, double rate, double scale)
{
  ms.position = (int32_t)llround(physical / scale);
  const double countsPerSec = rate / scale;
  const int32_t delta = (int32_t)llround(countsPerSec * kSpeedWindowSec);
  if (delta == 0) {
    ms.delta = 0;
    ms.time = 0;
  } else {
    ms.delta = delta;
    ms.time = (uint32_t)llround(kSpeedWindowSec / kHalSecPerTick);
  }
}

double HalPosition(const MotorState& ms, int motor, int32_t offset)
{
  return (ms.position - offset) * kHalMotorScale[motor];
}

double HalSpeed(const MotorState& ms, int motor)
{
  if (ms.time == 0) {
    return 0.0;
  }
  const double countsPerTick = (double)ms.delta / (double)ms.time;
  return (countsPerTick / kHalSecPerTick) * kHalMotorScale[motor];
}

double kMeasDeadbandCounts = 0.5;
double kMaxExtrapolateSec = 0.045;
const double kMaxMeasGapSec = 0.5;

double MeasDeadband(int motor)
{
  return kMeasDeadbandCounts * fabs(kHalMotorScale[motor]);
}

double SpeedQuantum(int motor)
{
  return 0.5 * fabs(kHalMotorScale[motor]) / kSpeedWindowSec;
}

AxisEstimate::AxisEstimate()
  : physical(0.0), rate(0.0), lastMeas(0.0), lastMeasTime(0.0), ahead(0.0), haveMeas(false)
{
}

void AxisEstimate::Measure(double meas, double now, double deadband)
{
  if (!haveMeas) {
    lastMeas = meas;
    lastMeasTime = now;
    physical = meas;
    haveMeas = true;
    return;
  }

  if (fabs(meas - lastMeas) <= deadband) {
    rate = 0.0;
    ahead = 0.0;
    physical = lastMeas;
    return;
  }

  const double dt = now - lastMeasTime;
  if (dt > 1e-4 && dt < kMaxMeasGapSec) {
    rate = (meas - lastMeas) / dt;
  }
  lastMeas = meas;
  lastMeasTime = now;
  physical = meas;
  ahead = 0.0;
}

void AxisEstimate::Advance(double dt)
{
  if (rate == 0.0) {
    return;
  }
  const double step = rate * dt;
  const double cap = fabs(rate) * kMaxExtrapolateSec;
  if (fabs(ahead + step) > cap) {
    return;
  }
  ahead += step;
  physical += step;
}

void AxisEstimate::SetRate(double r, double deadband)
{
  rate = (fabs(r) <= deadband) ? 0.0 : r;
}

void AxisEstimate::Integrate(double dt)
{
  physical += rate * dt;
}

}  // namespace cozmo
