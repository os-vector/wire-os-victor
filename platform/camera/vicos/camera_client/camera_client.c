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
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>

#include "camera_client.h"

#define VIDEO_DEVICE    "/dev/video0"
#define SUBDEV_DEVICE   "/dev/v4l-subdev8"

#define CAPTURE_WIDTH   1280
#define CAPTURE_HEIGHT  720
#define NUM_BUFFERS     4

struct v4l2_buffer_info {
    void*  start;
    size_t length;
    int    queued;
};

struct camera_context {
    int video_fd;
    int subdev_fd;
    anki_camera_status_t status;

    struct v4l2_buffer_info buffers[NUM_BUFFERS];
    uint32_t num_buffers;

    uint32_t frame_id;
    int current_buffer_index;

    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_row;
    anki_camera_pixel_format_t pixel_format;

    anki_camera_frame_t* frame_wrapper;
};

static struct camera_context g_ctx;

static int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static int v4l2_set_format(struct camera_context* ctx) {
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = CAPTURE_WIDTH;
    fmt.fmt.pix_mp.height = CAPTURE_HEIGHT;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_SBGGR10P;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 1;

    if (xioctl(ctx->video_fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "camera: VIDIOC_S_FMT failed: %s\n", strerror(errno));
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (xioctl(ctx->video_fd, VIDIOC_G_FMT, &fmt) < 0) {
            fprintf(stderr, "camera: VIDIOC_G_FMT failed: %s\n", strerror(errno));
            return -1;
        }
    }

    ctx->width = fmt.fmt.pix_mp.width;
    ctx->height = fmt.fmt.pix_mp.height;
    ctx->bytes_per_row = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;

    ctx->pixel_format = ANKI_CAM_FORMAT_RAW;

    return 0;
}

static int v4l2_request_buffers(struct camera_context* ctx)  {
    struct v4l2_requestbuffers req = {0};
    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(ctx->video_fd, VIDIOC_REQBUFS, &req) < 0) {
        fprintf(stderr, "camera: VIDIOC_REQBUFS failed: %s\n", strerror(errno));
        return -1;
    }

    ctx->num_buffers = req.count;

    for (uint32_t i = 0; i < ctx->num_buffers; i++) {
        struct v4l2_plane planes[1] = {{0}};
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1;

        if (xioctl(ctx->video_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "camera: VIDIOC_QUERYBUF failed: %s\n", strerror(errno));
            return -1;
        }

        ctx->buffers[i].length = planes[0].length;
        ctx->buffers[i].start = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->video_fd, planes[0].m.mem_offset);

        if (ctx->buffers[i].start == MAP_FAILED) {
            fprintf(stderr, "camera: mmap failed: %s\n", strerror(errno));
            return -1;
        }
        ctx->buffers[i].queued = 0;
    }

    return 0;
}

static int v4l2_queue_all_buffers(struct camera_context* ctx) {
    for (uint32_t i = 0; i < ctx->num_buffers; i++) {
        struct v4l2_plane planes[1] = {{0}};
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 1;

        if (xioctl(ctx->video_fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "camera: VIDIOC_QBUF failed: %s\n", strerror(errno));
            return -1;
        }
        ctx->buffers[i].queued = 1;
    }
    return 0;
}

static int v4l2_start_streaming(struct camera_context* ctx) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(ctx->video_fd, VIDIOC_STREAMON, &type) < 0) {
        fprintf(stderr, "camera: VIDIOC_STREAMON failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int v4l2_stop_streaming(struct camera_context* ctx) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(ctx->video_fd, VIDIOC_STREAMOFF, &type) < 0) {
        fprintf(stderr, "camera: VIDIOC_STREAMOFF failed: %s\n", strerror(errno));
        return -1;
    }

    for (uint32_t i = 0; i < ctx->num_buffers; i++) {
        ctx->buffers[i].queued = 0;
    }

    return 0;
}

static void v4l2_cleanup_buffers(struct camera_context* ctx)
{
    for (uint32_t i = 0; i < ctx->num_buffers; i++) {
        if (ctx->buffers[i].start != NULL && ctx->buffers[i].start != MAP_FAILED) {
            munmap(ctx->buffers[i].start, ctx->buffers[i].length);
            ctx->buffers[i].start = NULL;
        }
    }
    ctx->num_buffers = 0;
}

int camera_init(struct anki_camera_handle** camera)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.video_fd = -1;
    g_ctx.subdev_fd = -1;
    g_ctx.current_buffer_index = -1;
    g_ctx.frame_id = 1;

    g_ctx.video_fd = open(VIDEO_DEVICE, O_RDWR);
    if (g_ctx.video_fd < 0) {
        fprintf(stderr, "camera: Failed to open %s: %s\n", VIDEO_DEVICE, strerror(errno));
        return -1;
    }

    g_ctx.subdev_fd = open(SUBDEV_DEVICE, O_RDWR);

    if (v4l2_set_format(&g_ctx) < 0) {
        close(g_ctx.video_fd);
        if (g_ctx.subdev_fd >= 0) {
            close(g_ctx.subdev_fd);
        }
        return -1;
    }

    g_ctx.status = ANKI_CAMERA_STATUS_IDLE;

    size_t wrapper_size = sizeof(anki_camera_frame_t);
    g_ctx.frame_wrapper = malloc(wrapper_size);
    if (!g_ctx.frame_wrapper) {
        close(g_ctx.video_fd);
        if (g_ctx.subdev_fd >= 0) {
            close(g_ctx.subdev_fd);
        }
        return -1;
    }

    *camera = (struct anki_camera_handle*)&g_ctx;
    return 0;
}

int camera_start(struct anki_camera_handle* camera) {
    (void)camera;

    if (g_ctx.status == ANKI_CAMERA_STATUS_RUNNING) {
        return 0;
    }

    g_ctx.status = ANKI_CAMERA_STATUS_STARTING;

    if (v4l2_request_buffers(&g_ctx) < 0) {
        g_ctx.status = ANKI_CAMERA_STATUS_IDLE;
        return -1;
    }

    if (v4l2_queue_all_buffers(&g_ctx) < 0) {
        v4l2_cleanup_buffers(&g_ctx);
        g_ctx.status = ANKI_CAMERA_STATUS_IDLE;
        return -1;
    }

    if (v4l2_start_streaming(&g_ctx) < 0) {
        v4l2_cleanup_buffers(&g_ctx);
        g_ctx.status = ANKI_CAMERA_STATUS_IDLE;
        return -1;
    }

    g_ctx.status = ANKI_CAMERA_STATUS_RUNNING;
    return 0;
}

int camera_stop(struct anki_camera_handle* camera)
{
    (void)camera;

    if (g_ctx.status != ANKI_CAMERA_STATUS_RUNNING) {
        return 0;
    }

    v4l2_stop_streaming(&g_ctx);
    v4l2_cleanup_buffers(&g_ctx);

    g_ctx.current_buffer_index = -1;
    g_ctx.status = ANKI_CAMERA_STATUS_IDLE;

    return 0;
}

void camera_pause(struct anki_camera_handle* camera, int pause)
{
    (void)camera;

    if (pause) {
        if (g_ctx.status == ANKI_CAMERA_STATUS_RUNNING) {
            v4l2_stop_streaming(&g_ctx);
            g_ctx.status = ANKI_CAMERA_STATUS_IDLE;
        }
    } else {
        if (g_ctx.status == ANKI_CAMERA_STATUS_IDLE && g_ctx.num_buffers > 0) {
            v4l2_queue_all_buffers(&g_ctx);
            v4l2_start_streaming(&g_ctx);
            g_ctx.status = ANKI_CAMERA_STATUS_RUNNING;
        }
    }
}

int camera_release(struct anki_camera_handle* camera) {
    (void)camera;

    if (g_ctx.status == ANKI_CAMERA_STATUS_RUNNING) {
        camera_stop(camera);
    }

    g_ctx.status = ANKI_CAMERA_STATUS_OFFLINE;
    return 0;
}

int camera_destroy(struct anki_camera_handle* camera)  {
    (void)camera;

    if (g_ctx.status != ANKI_CAMERA_STATUS_OFFLINE) {
        return 0;
    }

    v4l2_cleanup_buffers(&g_ctx);

    if (g_ctx.video_fd >= 0) {
        close(g_ctx.video_fd);
        g_ctx.video_fd = -1;
    }

    if (g_ctx.subdev_fd >= 0) {
        close(g_ctx.subdev_fd);
        g_ctx.subdev_fd = -1;
    }

    if (g_ctx.frame_wrapper) {
        free(g_ctx.frame_wrapper);
        g_ctx.frame_wrapper = NULL;
    }

    return 1;
}

int camera_frame_acquire(struct anki_camera_handle* camera, uint64_t frame_timestamp, anki_camera_frame_t** out_frame) {
    (void)camera;
    (void)frame_timestamp;

    if (g_ctx.status != ANKI_CAMERA_STATUS_RUNNING) {
        return -1;
    }

    if (g_ctx.current_buffer_index >= 0) {
        camera_frame_release(camera, g_ctx.frame_id - 1);
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(g_ctx.video_fd, &fds);
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

    int r = select(g_ctx.video_fd + 1, &fds, NULL, NULL, &tv);
    if (r < 0) {
        if (errno == EINTR) return -1;
        fprintf(stderr, "camera: select failed: %s\n", strerror(errno));
        return -1;
    }
    if (r == 0) {
        return -1;
    }

    struct v4l2_plane planes[1] = {{0}};
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length = 1;

    if (xioctl(g_ctx.video_fd, VIDIOC_DQBUF, &buf) < 0) {
        fprintf(stderr, "camera: VIDIOC_DQBUF failed: %s\n", strerror(errno));
        return -1;
    }

    g_ctx.buffers[buf.index].queued = 0;
    g_ctx.current_buffer_index = buf.index;

    uint64_t timestamp_ns = (uint64_t)buf.timestamp.tv_sec * 1000000000ULL +
                            (uint64_t)buf.timestamp.tv_usec * 1000ULL;

    g_ctx.frame_wrapper->timestamp = timestamp_ns;
    g_ctx.frame_wrapper->frame_id = g_ctx.frame_id++;
    g_ctx.frame_wrapper->width = g_ctx.width;
    g_ctx.frame_wrapper->height = g_ctx.height;
    g_ctx.frame_wrapper->bytes_per_row = g_ctx.bytes_per_row;
    g_ctx.frame_wrapper->bits_per_pixel = 10;  // SBGGR10P
    g_ctx.frame_wrapper->format = g_ctx.pixel_format;

    void* buffer_start = g_ctx.buffers[buf.index].start;

    memcpy(buffer_start, g_ctx.frame_wrapper, sizeof(anki_camera_frame_t));

    *out_frame = (anki_camera_frame_t*)buffer_start;

    return 0;
}

int camera_frame_release(struct anki_camera_handle* camera, uint32_t frame_id) {
    (void)camera;
    (void)frame_id;

    if (g_ctx.current_buffer_index < 0) {
        return 0;
    }

    struct v4l2_plane planes[1] = {{0}};
    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = g_ctx.current_buffer_index;
    buf.m.planes = planes;
    buf.length = 1;

    if (xioctl(g_ctx.video_fd, VIDIOC_QBUF, &buf) < 0) {
        fprintf(stderr, "camera: VIDIOC_QBUF failed: %s\n", strerror(errno));
        return -1;
    }

    g_ctx.buffers[g_ctx.current_buffer_index].queued = 1;
    g_ctx.current_buffer_index = -1;

    return 0;
}

int camera_set_exposure(struct anki_camera_handle* camera, uint16_t exposure_ms, float gain) {
    (void)camera;

    if (g_ctx.subdev_fd < 0) {
        return -1;
    }

    // our range: 1-66 ms
    // v4l2 range: 0-65535
    struct v4l2_control ctrl = {0};
    ctrl.id = V4L2_CID_EXPOSURE;

    ctrl.value = exposure_ms * 1000;
    if (ctrl.value > 65535) ctrl.value = 65535;

    if (xioctl(g_ctx.subdev_fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        // exposure not properly implemented
    }

    // our range: 0.25 - 3.8
    // v4l2 range: 0-255
    ctrl.id = V4L2_CID_GAIN;
    ctrl.value = (int)(gain * 72.0f);

    if (ctrl.value > 255) ctrl.value = 255;
    if (ctrl.value < 0) ctrl.value = 0;

    if (xioctl(g_ctx.subdev_fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        fprintf(stderr, "camera: Failed to set gain: %s\n", strerror(errno));
    }

    return 0;
}

int camera_set_awb(struct anki_camera_handle* camera, float r_gain, float g_gain, float b_gain)
{
    (void)camera;
    (void)r_gain;
    (void)g_gain;
    (void)b_gain;

    return 0;
}

int camera_set_capture_format(struct anki_camera_handle* camera, anki_camera_pixel_format_t format)
{
    (void)camera;

    if (format != ANKI_CAM_FORMAT_RAW &&
        format != ANKI_CAM_FORMAT_BAYER_MIPI_BGGR10) {
        fprintf(stderr, "camera: Only RAW format currently supported\n");
        return -1;
    }

    g_ctx.pixel_format = format;
    return 0;
}

int camera_set_capture_snapshot(struct anki_camera_handle* camera, uint8_t start)
{
    (void)camera;
    (void)start;

    return 0;
}

anki_camera_status_t camera_status(struct anki_camera_handle* camera)
{
    (void)camera;
    return g_ctx.status;
}
