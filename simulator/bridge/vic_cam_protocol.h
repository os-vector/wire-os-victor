#ifndef __VIC_CAM_PROTOCOL_H__
#define __VIC_CAM_PROTOCOL_H__

#include <stdint.h>

#define VIC_CAM_MAGIC            0x56494343u  // VICC
#define VIC_CAM_VERSION          2u

#define VIC_CAM_WIDTH    1280
#define VIC_CAM_HEIGHT   720
#define VIC_CAM_YSIZE    (VIC_CAM_WIDTH * VIC_CAM_HEIGHT)
#define VIC_CAM_UVSIZE   (VIC_CAM_YSIZE / 2)
#define VIC_CAM_FRAME_BYTES (VIC_CAM_YSIZE + VIC_CAM_UVSIZE)

typedef struct VicCamFrame
{
  uint32_t magic;
  uint32_t version;
  uint32_t seq;
  uint16_t width;
  uint16_t height;
  uint8_t  yuv[VIC_CAM_FRAME_BYTES];
} VicCamFrame;

#define VIC_CAM_PARAMS_MAGIC 0x56435041u  /* VCPA */

typedef struct VicCamParams
{
  uint32_t magic;
  uint32_t exposureMs;
  float    gain;
} VicCamParams;

#endif /* __VIC_CAM_PROTOCOL_H__ */
