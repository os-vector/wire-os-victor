/**
 * File: testLocalUdpServer.cpp
 *
 * Description: gtest cases for LocalUdpServer client-transport semantics over
 * real local-domain DGRAM sockets. The focus is the Send() return contract:
 *
 *     >0  datagram sent
 *      0  send would block (EAGAIN/EWOULDBLOCK) — datagram dropped, client KEPT
 *     -1  hard socket error or no client — channel broken
 *
 * The would-block case reproduces the engine→gateway response jam: a burst of
 * unread datagrams fills the peer's kernel receive queue, the server's
 * non-blocking send() returns EAGAIN ("Resource temporarily unavailable"),
 * and — because LocalUdpServer is one-client-per-server — disconnecting on
 * that transient condition used to kill every subsequent response until the
 * next boot. These tests assert the channel survives the EAGAIN and carries
 * traffic again once the peer drains.
 *
 * Copyright: Anki, inc. 2026
 */

#include "coretech/messaging/shared/LocalUdpServer.h"
#include "coretech/messaging/shared/LocalUdpClient.h"

#include "gtest/gtest.h"

#include <string>
#include <vector>

#include <unistd.h>

namespace {

std::string SocketPath(const char* tag)
{
  return "/tmp/testLocalUdpServer_" + std::string(tag) + "_" + std::to_string(getpid());
}

// Small kernel buffers so a short unread burst reaches backpressure. The
// kernel rounds these up to its floor (and doubles them), so the test floods
// generously rather than assuming an exact capacity.
constexpr int kSmallBufSz = 4096;
constexpr size_t kDatagramSize = 1024;
constexpr int kMaxFloodDatagrams = 4096;

struct ServerClientPair {
  LocalUdpServer server;
  LocalUdpClient client{kSmallBufSz, kSmallBufSz};
  std::string serverPath;
  std::string clientPath;

  explicit ServerClientPair(const char* tag)
  : serverPath(SocketPath((std::string(tag) + "_server").c_str()))
  , clientPath(SocketPath((std::string(tag) + "_client").c_str()))
  {
  }

  ~ServerClientPair()
  {
    client.Disconnect();
    server.StopListening();
    unlink(serverPath.c_str());
    unlink(clientPath.c_str());
  }

  // Start the server, connect the client, and let the server register the
  // client from the connection packet the client sends on Connect().
  ::testing::AssertionResult Establish()
  {
    if (!server.StartListening(serverPath)) {
      return ::testing::AssertionFailure() << "server failed to listen on " << serverPath;
    }
    if (!client.Connect(clientPath, serverPath)) {
      return ::testing::AssertionFailure() << "client failed to connect to " << serverPath;
    }
    char buf[64];
    if (server.Recv(buf, sizeof(buf)) < 0) {
      return ::testing::AssertionFailure() << "server Recv failed on connection packet";
    }
    if (!server.HasClient()) {
      return ::testing::AssertionFailure() << "server did not register the client";
    }
    return ::testing::AssertionSuccess();
  }

  // Send datagrams without the client draining until Send() reports
  // backpressure. Returns the first non-full-size Send() result, or the last
  // full-size result if backpressure was never reached.
  ssize_t FloodUntilNotSent()
  {
    const std::vector<char> datagram(kDatagramSize, 'x');
    ssize_t res = -1;
    for (int i = 0; i < kMaxFloodDatagrams; ++i) {
      res = server.Send(datagram.data(), datagram.size());
      if (res != (ssize_t)datagram.size()) {
        return res;
      }
    }
    return res;
  }
};

} // anonymous namespace

TEST(LocalUdpServer, SendWithNoClientReturnsMinusOne)
{
  const std::string serverPath = SocketPath("noclient");
  LocalUdpServer server;
  ASSERT_TRUE(server.StartListening(serverPath));

  const char data[] = "hello";
  EXPECT_EQ(server.Send(data, sizeof(data)), -1);

  server.StopListening();
  unlink(serverPath.c_str());
}

TEST(LocalUdpServer, RoundTripDelivery)
{
  ServerClientPair pair("roundtrip");
  ASSERT_TRUE(pair.Establish());

  const char data[] = "ping";
  ASSERT_EQ(pair.server.Send(data, sizeof(data)), (ssize_t)sizeof(data));

  char buf[64];
  EXPECT_EQ(pair.client.Recv(buf, sizeof(buf)), (ssize_t)sizeof(data));
  EXPECT_STREQ(buf, "ping");
}

// The engine→gateway jam scenario: backpressure must surface as 0 (drop, keep
// client), never as the -1 that callers treat as a broken channel.
TEST(LocalUdpServer, WouldBlockReturnsZeroAndKeepsClient)
{
  ServerClientPair pair("wouldblock");
  ASSERT_TRUE(pair.Establish());

  const ssize_t res = pair.FloodUntilNotSent();
  ASSERT_EQ(res, 0) << "expected would-block sentinel 0 (got " << res
                    << "); -1 means EAGAIN is still reported as a hard error";
  EXPECT_TRUE(pair.server.HasClient());
}

// After the peer drains its queue, the SAME client connection must carry
// traffic again. Pre-fix, the EAGAIN-triggered Disconnect() left the
// one-client server clientless for the rest of the boot.
TEST(LocalUdpServer, ChannelRecoversAfterWouldBlock)
{
  ServerClientPair pair("recovery");
  ASSERT_TRUE(pair.Establish());

  ASSERT_EQ(pair.FloodUntilNotSent(), 0);
  ASSERT_TRUE(pair.server.HasClient());

  // Drain everything queued on the client side.
  std::vector<char> rbuf(kDatagramSize * 2);
  while (pair.client.Recv(rbuf.data(), rbuf.size()) > 0) {
  }

  const std::vector<char> datagram(kDatagramSize, 'y');
  EXPECT_EQ(pair.server.Send(datagram.data(), datagram.size()), (ssize_t)datagram.size());
  EXPECT_EQ(pair.client.Recv(rbuf.data(), rbuf.size()), (ssize_t)datagram.size());
}

// A genuinely broken channel must still be reported as -1 so callers keep
// their disconnect-on-hard-error behavior.
TEST(LocalUdpServer, HardErrorStillReturnsMinusOne)
{
  ServerClientPair pair("harderror");
  ASSERT_TRUE(pair.Establish());

  // Closing the client socket makes subsequent connected sends fail with
  // ECONNREFUSED — a hard error, not backpressure.
  pair.client.Disconnect();

  const char data[] = "into the void";
  EXPECT_EQ(pair.server.Send(data, sizeof(data)), -1);
}
