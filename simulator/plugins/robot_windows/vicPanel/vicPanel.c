#include <webots/robot.h>
#if defined(__has_include) && __has_include(<webots/plugins/robot_window/robot_wwi.h>)
#include <webots/plugins/robot_window/robot_wwi.h>
#else
#include <webots/robot_wwi.h>
#endif

#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET panel_sock_t;
#define PANEL_INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int panel_sock_t;
#define PANEL_INVALID_SOCK (-1)
#endif

#define VIC_PANEL_PORT 5804

static panel_sock_t sockFd = PANEL_INVALID_SOCK;

void wb_robot_window_init() {
#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
  sockFd = socket(AF_INET, SOCK_DGRAM, 0);
}

void wb_robot_window_step(int time_step) {
  (void)time_step;
  const char *message;
  while ((message = wb_robot_wwi_receive_text()) != NULL) {
    if (sockFd == PANEL_INVALID_SOCK)
      continue;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(VIC_PANEL_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    sendto(sockFd, message, (int)strlen(message), 0,
           (struct sockaddr *)&addr, sizeof(addr));
  }
}

void wb_robot_window_cleanup() {
  if (sockFd != PANEL_INVALID_SOCK) {
#ifdef _WIN32
    closesocket(sockFd);
#else
    close(sockFd);
#endif
    sockFd = PANEL_INVALID_SOCK;
  }
}
