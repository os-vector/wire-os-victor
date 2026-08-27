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

bool Exchange(const HeadToBody& headData);

const BodyToHead* LatestBody();

bool LatestImu(HAL::IMU_DataStructure& imu);

bool IsConnected();

void Shutdown();

} // namespace SimBodyBridge
} // namespace Vector
} // namespace Anki

#endif // STANDALONE_SIM
#endif // __ANKI_COZMO_ROBOT_SIM_BODY_BRIDGE_H__
