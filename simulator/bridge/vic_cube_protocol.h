#ifndef __VIC_CUBE_PROTOCOL_H__
#define __VIC_CUBE_PROTOCOL_H__

#include <stdint.h>

#define VIC_CUBE_MAGIC            0x56494343u  // VICC
#define VIC_CUBE_VERSION          1u
#define VIC_CUBE_FACTORYID_LEN    20           // ED:BB:02:DE:85:23 = 17 + null

enum {
  VIC_CUBE_MSG_ADVERTISE = 1,
  VIC_CUBE_MSG_ACCEL     = 2,
  VIC_CUBE_MSG_LEDS      = 3,  // down: resolved LED colors for display
};

typedef struct VicCubeUp {
  uint32_t magic;
  uint32_t version;
  uint8_t  type;
  char     factoryId[VIC_CUBE_FACTORYID_LEN];
  int16_t  accel[3];
  uint8_t  tapped;
  uint16_t railVoltageCnts;
} VicCubeUp;

typedef struct VicCubeDown {
  uint32_t magic;
  uint32_t version;
  uint8_t  type;
  uint8_t  rgb[4 * 3];
} VicCubeDown;

#endif /* __VIC_CUBE_PROTOCOL_H__ */
