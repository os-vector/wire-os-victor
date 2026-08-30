#ifndef __VIC_FACE_PROTOCOL_H__
#define __VIC_FACE_PROTOCOL_H__

#include <stdint.h>

#define VIC_FACE_MAGIC            0x56494346u  // VICF
#define VIC_FACE_VERSION          1u

#define VIC_FACE_WIDTH   184
#define VIC_FACE_HEIGHT  96
#define VIC_FACE_NPIX    (VIC_FACE_WIDTH * VIC_FACE_HEIGHT)

typedef struct VicFaceFrame
{
  uint32_t magic;
  uint32_t version;
  uint32_t seq;
  uint16_t width;
  uint16_t height;
  uint16_t rgb565[VIC_FACE_NPIX];
} VicFaceFrame;

#endif /* __VIC_FACE_PROTOCOL_H__ */
