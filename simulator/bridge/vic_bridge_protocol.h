#ifndef __VIC_BRIDGE_PROTOCOL_H__
#define __VIC_BRIDGE_PROTOCOL_H__

#include <stdint.h>
#include "schema/messages.h"

#define VIC_BRIDGE_BODY_SOCK_PATH "/tmp/vector_body.sock"

#define VIC_BRIDGE_MAGIC          0x56494342u  // VICB
#define VIC_BRIDGE_PROTO_VERSION  1u

typedef struct VicBridgeImu
{
  float gyro[3];
  float accel[3];
  float temperature_degC;
} VicBridgeImu;

typedef struct VicBridgeH2B
{
  uint32_t magic;
  uint32_t version;
  uint32_t seq;
  uint32_t _pad;
  struct HeadToBody h2b;
  uint8_t  gripper;
  uint8_t  _pad2[3];
} VicBridgeH2B;

typedef struct VicBridgeB2H
{
  uint32_t magic;
  uint32_t version;
  uint32_t seq;
  uint32_t _pad;
  struct BodyToHead b2h;
  VicBridgeImu imu;
} VicBridgeB2H;

#endif /* __VIC_BRIDGE_PROTOCOL_H__ */
