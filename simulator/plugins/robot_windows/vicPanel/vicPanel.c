#include <webots/robot.h>
#if defined(__has_include) && __has_include(<webots/plugins/robot_window/robot_wwi.h>)
#include <webots/plugins/robot_window/robot_wwi.h>
#else
#include <webots/robot_wwi.h>
#endif

#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>

#define VIC_PANEL_SOCK_PATH "/tmp/vector_panel.sock"

static int sockFd = -1;

void wb_robot_window_init() {
  sockFd = socket(AF_UNIX, SOCK_DGRAM, 0);
}

void wb_robot_window_step(int time_step) {
  (void)time_step;
  const char *message;
  while ((message = wb_robot_wwi_receive_text()) != NULL) {
    if (sockFd < 0)
      continue;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VIC_PANEL_SOCK_PATH, sizeof(addr.sun_path) - 1);
    sendto(sockFd, message, strlen(message), 0, (struct sockaddr *)&addr, sizeof(addr));
  }
}

void wb_robot_window_cleanup() {
  if (sockFd >= 0) {
    close(sockFd);
    sockFd = -1;
  }
}
