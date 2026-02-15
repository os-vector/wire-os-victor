/**
 * File: camera_client.h
 *
 * Author: Brian Chapados
 * Created: 01/24/2018
 *
 * Description:
 *               API for remote IPC connection to anki camera system daemon
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

#include "camera_client.h"

#define FAKE_WIDTH  1280
#define FAKE_HEIGHT 720
#define FAKE_BPP    8
#define FAKE_GRAY   0x80

struct fake_camera {
  anki_camera_status_t status;
  uint32_t frame_id;
  anki_camera_frame_t *frame;
  uint8_t *frame_data;
};

static struct fake_camera g_cam;

static uint64_t now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

int camera_init(struct anki_camera_handle **camera)
{
  memset(&g_cam, 0, sizeof(g_cam));

  size_t image_size = FAKE_WIDTH * FAKE_HEIGHT;
  size_t frame_size = sizeof(anki_camera_frame_t) + image_size;

  g_cam.frame = malloc(frame_size);
  g_cam.frame_data = ((uint8_t *)g_cam.frame) + sizeof(anki_camera_frame_t);

  memset(g_cam.frame_data, FAKE_GRAY, image_size);

  g_cam.frame->timestamp = now_ns();
  g_cam.frame->frame_id = 1;
  g_cam.frame->width = FAKE_WIDTH;
  g_cam.frame->height = FAKE_HEIGHT;
  g_cam.frame->bytes_per_row = FAKE_WIDTH;
  g_cam.frame->bits_per_pixel = FAKE_BPP;
  g_cam.frame->format = ANKI_CAM_FORMAT_RAW;

  g_cam.status = ANKI_CAMERA_STATUS_IDLE;

  *camera = (struct anki_camera_handle *)&g_cam;
  return 0;
}

int camera_start(struct anki_camera_handle *camera)
{
  (void)camera;
  g_cam.status = ANKI_CAMERA_STATUS_RUNNING;
  return 0;
}

int camera_stop(struct anki_camera_handle *camera)
{
  (void)camera;
  g_cam.status = ANKI_CAMERA_STATUS_IDLE;
  return 0;
}

void camera_pause(struct anki_camera_handle *camera, int pause)
{
  (void)camera;
  (void)pause;
}

int camera_release(struct anki_camera_handle *camera)
{
  (void)camera;
  g_cam.status = ANKI_CAMERA_STATUS_OFFLINE;
  return 0;
}

int camera_destroy(struct anki_camera_handle *camera)
{
  (void)camera;
  if (g_cam.frame) {
    free(g_cam.frame);
    g_cam.frame = NULL;
  }
  g_cam.frame_data = NULL;
  return 1;
}

int camera_frame_acquire(struct anki_camera_handle *camera,
                         uint64_t frame_timestamp,
                         anki_camera_frame_t **out_frame)
{
  (void)camera;
  (void)frame_timestamp;

  if (g_cam.status != ANKI_CAMERA_STATUS_RUNNING) {
    return -1;
  }

  g_cam.frame_id++;
  g_cam.frame->frame_id = g_cam.frame_id;
  g_cam.frame->timestamp = now_ns();

  if (out_frame) {
    *out_frame = g_cam.frame;
  }

  return 0;
}

int camera_frame_release(struct anki_camera_handle *camera, uint32_t frame_id)
{
  (void)camera;
  (void)frame_id;
  return 0;
}

anki_camera_status_t camera_status(struct anki_camera_handle *camera)
{
  (void)camera;
  return g_cam.status;
}

int camera_set_exposure(struct anki_camera_handle* camera,
                        uint16_t exposure_ms,
                        float gain)
{
  (void)camera;
  (void)exposure_ms;
  (void)gain;
  return 0;
}

int camera_set_awb(struct anki_camera_handle* camera,
                   float r_gain,
                   float g_gain,
                   float b_gain)
{
  (void)camera;
  (void)r_gain;
  (void)g_gain;
  (void)b_gain;
  return 0;
}

int camera_set_capture_format(struct anki_camera_handle* camera,
                              anki_camera_pixel_format_t format)
{
  (void)camera;
  (void)format;
  return 0;
}

int camera_set_capture_snapshot(struct anki_camera_handle* camera,
                                uint8_t start)
{
  (void)camera;
  (void)start;
  return 0;
}
