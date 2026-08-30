#ifndef __ANKI_COZMO_ROBOT_SIM_BODY_BRIDGE_H__
#define __ANKI_COZMO_ROBOT_SIM_BODY_BRIDGE_H__

#ifdef STANDALONE_SIM

#include "schema/messages.h"
#include "anki/cozmo/robot/hal.h"

namespace Anki {
namespace Vector {
namespace SimBodyBridge {

void Init();

void SetGripper(bool on);

void SetMotorSetpoints(uint8_t valid);

void SetHeadCommand(uint32_t seq, float angleRad, float speedRadPerSec,
                    float accelRadPerSec2, float durationSec);

void SetLiftCommand(uint32_t seq, float heightMm, float speedRadPerSec,
                    float accelRadPerSec2, float durationSec);

void SetWheelSetpoints(float leftMmps, float rightMmps);

int Exchange(const HeadToBody& headData);

const BodyToHead* LatestBody();

bool PopImu(HAL::IMU_DataStructure& imu);

bool IsConnected();

void Shutdown();

} // namespace SimBodyBridge
} // namespace Vector
} // namespace Anki

#endif // STANDALONE_SIM
#endif // __ANKI_COZMO_ROBOT_SIM_BODY_BRIDGE_H__
