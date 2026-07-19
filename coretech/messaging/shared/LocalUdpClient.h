#ifndef ANKI_MESSAGING_LOCAL_UDP_CLIENT_H
#define ANKI_MESSAGING_LOCAL_UDP_CLIENT_H

/**
 *
 * File: LocalUdpClient.h
 *
 * Description: Declaration of local-domain socket client class
 *
 * Copyright: Anki, inc. 2017
 *
 */
#include <string>

#include <sys/socket.h>
#include <sys/un.h>

class LocalUdpClient
{
public:

  LocalUdpClient(int sndbufsz, int rcvbufsz);
  LocalUdpClient();

  ~LocalUdpClient();

  bool Connect(const std::string& sockname, const std::string& peername);
  bool IsConnected() const { return _socket >= 0; }
  bool Disconnect();

  // Peer transport
  //
  // Send returns the number of bytes sent, or:
  //   0  - EITHER the send would block (EAGAIN/EWOULDBLOCK on this non-blocking
  //        socket): the datagram was dropped but the socket is KEPT; the caller
  //        may retry later and MUST NOT treat this as a broken channel —
  //        OR the socket is undefined (never connected / already disconnected),
  //        a pre-existing sentinel this class has always returned.
  //        The two are distinguishable at the call site: after a would-block 0,
  //        IsConnected() is true and a "LocalUdpClient.Send.WouldBlock" WARNING
  //        was logged; after an undefined-socket 0, IsConnected() is false and a
  //        "LocalUdpClient.Send" ERROR was logged. Log-readers must not attribute
  //        a dead-socket 0 to backpressure.
  //   -1 - hard socket error; the socket has ALREADY been Disconnect()ed
  //        internally. The channel is broken.
  //
  // Recv returns the number of bytes received, or:
  //   0  - nothing to read (would-block / zero-length datagram / undefined
  //        socket). Socket KEPT (when defined).
  //   -1 - hard socket error; the socket has ALREADY been Disconnect()ed
  //        internally.
  ssize_t Send(const char* data, size_t size);
  ssize_t Recv(char* data, size_t maxSize);

  // For use with select etc
  int GetSocket() const { return _socket; }

  // Return count of bytes queued for read or -1 on error
  ssize_t GetIncomingSize() const;

  // Return count of bytes queued for write or -1 on error
  ssize_t GetOutgoingSize() const;

private:
  // Socket parameters
  int _sndbufsz;
  int _rcvbufsz;

  // Socket descriptor
  int _socket;

  // Socket names
  std::string _sockname;
  std::string _peername;

  // Socket addresses
  struct sockaddr_un _sockaddr;
  socklen_t _sockaddr_len;

  struct sockaddr_un _peeraddr;
  socklen_t _peeraddr_len;

};

#endif
