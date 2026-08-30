#ifndef __VIC_SPKR_PROTOCOL_H__
#define __VIC_SPKR_PROTOCOL_H__

#include <stdint.h>

#define VIC_SPKR_MAGIC        0x56494353u // VICS
#define VIC_SPKR_VERSION      1u
#define VIC_SPKR_SAMPLE_RATE  32000u
#define VIC_SPKR_CHUNK        1024u

typedef struct VicSpkrChunk
{
  uint32_t magic;
  uint32_t version;
  uint32_t seq;
  uint32_t sampleRate;
  uint32_t nsamples;
  int16_t  samples[VIC_SPKR_CHUNK];
} VicSpkrChunk;

#endif
