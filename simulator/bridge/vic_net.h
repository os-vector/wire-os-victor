#ifndef __VIC_NET_H__
#define __VIC_NET_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET vicnet_sock_t;
  #define VICNET_INVALID_SOCK INVALID_SOCKET
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <poll.h>
  #include <signal.h>
  typedef int vicnet_sock_t;
  #define VICNET_INVALID_SOCK (-1)
#endif

#ifndef MSG_NOSIGNAL
  #define MSG_NOSIGNAL 0
#endif
#ifndef MSG_DONTWAIT
  #define MSG_DONTWAIT 0
#endif

#define VIC_NET_PORT_BODY  5800
#define VIC_NET_PORT_FACE  5801
#define VIC_NET_PORT_CAM   5802
#define VIC_NET_PORT_CUBE  5803
#define VIC_NET_PORT_PANEL 5804
#define VIC_NET_PORT_SPKR  5805

#define VIC_NET_HELLO_MAGIC 0x56484C4Fu
typedef struct VicNetHello {
  uint32_t magic;
  uint32_t channel;
} VicNetHello;

static inline void vicnet_init(void)
{
#ifdef _WIN32
  static int done = 0;
  if (!done) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    done = 1;
  }
#else
  signal(SIGPIPE, SIG_IGN);
#endif
}

static inline void vicnet_close(vicnet_sock_t s)
{
#ifdef _WIN32
  closesocket(s);
#else
  close(s);
#endif
}

static inline void vicnet_set_nonblocking(vicnet_sock_t s)
{
#ifdef _WIN32
  u_long on = 1;
  ioctlsocket(s, FIONBIO, &on);
#else
  int flags = fcntl(s, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
  }
#endif
}

static inline int vicnet_would_block(void)
{
#ifdef _WIN32
  const int e = WSAGetLastError();
  return e == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static inline int vicnet_conn_refused(void)
{
#ifdef _WIN32
  return WSAGetLastError() == WSAECONNRESET;
#else
  return errno == ECONNREFUSED;
#endif
}

static inline int vicnet_poll_in(vicnet_sock_t s, int timeoutMs)
{
#ifdef _WIN32
  WSAPOLLFD pfd;
  pfd.fd = s;
  pfd.events = POLLRDNORM;
  pfd.revents = 0;
  return WSAPoll(&pfd, 1, timeoutMs);
#else
  struct pollfd pfd;
  pfd.fd = s;
  pfd.events = POLLIN;
  pfd.revents = 0;
  return poll(&pfd, 1, timeoutMs);
#endif
}

static inline void vicnet_make_addr(struct sockaddr_in* a, const char* host, uint16_t port)
{
  memset(a, 0, sizeof(*a));
  a->sin_family = AF_INET;
  a->sin_port = htons(port);
  if (host == NULL || host[0] == '\0') {
    a->sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    a->sin_addr.s_addr = inet_addr(host);
  }
}

static inline vicnet_sock_t vicnet_udp_server(const char* host, uint16_t port)
{
  vicnet_sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == VICNET_INVALID_SOCK) {
    return VICNET_INVALID_SOCK;
  }
  int reuse = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
  struct sockaddr_in a;
  vicnet_make_addr(&a, host, port);
  if (bind(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
    vicnet_close(s);
    return VICNET_INVALID_SOCK;
  }
  vicnet_set_nonblocking(s);
  return s;
}

static inline vicnet_sock_t vicnet_udp_client(const char* host, uint16_t port)
{
  vicnet_sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == VICNET_INVALID_SOCK) {
    return VICNET_INVALID_SOCK;
  }
  struct sockaddr_in a;
  vicnet_make_addr(&a, host, port);
  if (connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
    vicnet_close(s);
    return VICNET_INVALID_SOCK;
  }
  vicnet_set_nonblocking(s);
  return s;
}

static inline vicnet_sock_t vicnet_tcp_server(const char* host, uint16_t port)
{
  vicnet_sock_t s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == VICNET_INVALID_SOCK) {
    return VICNET_INVALID_SOCK;
  }
  int reuse = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
  struct sockaddr_in a;
  vicnet_make_addr(&a, host, port);
  if (bind(s, (struct sockaddr*)&a, sizeof(a)) != 0 || listen(s, 1) != 0) {
    vicnet_close(s);
    return VICNET_INVALID_SOCK;
  }
  vicnet_set_nonblocking(s);
  return s;
}

static inline vicnet_sock_t vicnet_tcp_client(const char* host, uint16_t port)
{
  vicnet_sock_t s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == VICNET_INVALID_SOCK) {
    return VICNET_INVALID_SOCK;
  }
  struct sockaddr_in a;
  vicnet_make_addr(&a, host, port);
  if (connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
    vicnet_close(s);
    return VICNET_INVALID_SOCK;
  }
  int nodelay = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
  vicnet_set_nonblocking(s);
  return s;
}

#endif
