/**
 * File: testLocalUdpClient.cpp
 *
 * Description: gtest cases for LocalUdpClient transport semantics over real
 * local-domain DGRAM sockets, plus contract-mirror tests for the (non
 * host-buildable) callers whose disconnect-on-EAGAIN behavior this change
 * fixes. The focus is the Send()/Recv() return contract — the client-side
 * mirror of the LocalUdpServer contract fixed for orchestrator #4689 (fork
 * #26):
 *
 *     >0  datagram sent / received
 *      0  the send would block (EAGAIN/EWOULDBLOCK) or there is nothing to
 *         read — datagram dropped, socket KEPT. Also the pre-existing sentinel
 *         for an undefined socket (distinguishable: IsConnected() is false).
 *     -1  hard socket error — the client has ALREADY Disconnect()ed itself.
 *
 * Pre-fix, LocalUdpClient::Send() called Disconnect() on ANY incomplete send,
 * including a transient EAGAIN when the peer's kernel receive queue was
 * momentarily full. Every caller of the client (engine->anim
 * RobotConnectionManager, anim->robot AnimComms::SendPacketToRobot, jdocs, log
 * uploader, switchboard engine/token clients) therefore lost its channel to one
 * burst of backpressure — the reverse direction of the engine->gateway jam
 * (orchestrator #4672). These tests assert the socket survives the EAGAIN and
 * carries traffic again once the peer drains (orchestrator #4715).
 *
 * Copyright: Anki, inc. 2026
 */

#include "coretech/messaging/shared/LocalUdpClient.h"
#include "coretech/messaging/shared/LocalUdpServer.h"

#include "gtest/gtest.h"

#include <cstdint>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

std::string SocketPath(const char* tag)
{
  return "/tmp/testLocalUdpClient_" + std::string(tag) + "_" + std::to_string(getpid());
}

// Small kernel buffers so a short unread burst reaches backpressure. The
// kernel rounds these up to its floor (and doubles them), so the tests flood
// generously rather than assuming an exact capacity.
constexpr int kSmallBufSz = 4096;
constexpr size_t kDatagramSize = 1024;
constexpr int kMaxFloodDatagrams = 4096;

// A client connected to a server that does NOT drain unless told to — the
// client->server mirror of the ServerClientPair in testLocalUdpServer.cpp.
struct ClientServerPair {
  LocalUdpServer server{kSmallBufSz, kSmallBufSz};
  LocalUdpClient client{kSmallBufSz, kSmallBufSz};
  std::string serverPath;
  std::string clientPath;

  explicit ClientServerPair(const char* tag)
  : serverPath(SocketPath((std::string(tag) + "_server").c_str()))
  , clientPath(SocketPath((std::string(tag) + "_client").c_str()))
  {
  }

  ~ClientServerPair()
  {
    client.Disconnect();
    server.StopListening();
    unlink(serverPath.c_str());
    unlink(clientPath.c_str());
  }

  // Start the server, connect the client, and consume the connection packet the
  // client sends on Connect() (server registers the client and returns 0 for
  // it). After this, the server's recv queue holds only real payload datagrams,
  // so DrainServer() can loop on the Recv() return value alone.
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

  // Client sends datagrams without the server draining until Send() reports
  // something other than a full send. Returns that first non-full-size result,
  // or the last full-size result if backpressure was never reached.
  ssize_t FloodUntilNotSent()
  {
    const std::vector<char> datagram(kDatagramSize, 'x');
    ssize_t res = -1;
    for (int i = 0; i < kMaxFloodDatagrams; ++i) {
      res = client.Send(datagram.data(), datagram.size());
      if (res != (ssize_t)datagram.size()) {
        return res;
      }
    }
    return res;
  }

  // Drain everything queued on the server side. The connection packet was
  // already consumed in Establish(), so the queue holds only full-size payload
  // datagrams and Recv() returns >0 until the queue empties (then 0).
  void DrainServer()
  {
    std::vector<char> rbuf(kDatagramSize * 2);
    while (server.Recv(rbuf.data(), rbuf.size()) > 0) {
    }
  }
};

} // anonymous namespace

// Pre-existing sentinel: an undefined socket returns 0 from Send. The
// disambiguation from a would-block 0 is IsConnected() — pin it with a test so
// the collision named in the header contract stays true.
TEST(LocalUdpClient, SendWithUndefinedSocketReturnsZeroAndNotConnected)
{
  LocalUdpClient client;
  ASSERT_FALSE(client.IsConnected());

  const char data[] = "hello";
  EXPECT_EQ(client.Send(data, sizeof(data)), 0);
  EXPECT_FALSE(client.IsConnected());
}

TEST(LocalUdpClient, RoundTripDelivery)
{
  ClientServerPair pair("roundtrip");
  ASSERT_TRUE(pair.Establish());

  const char data[] = "ping";
  ASSERT_EQ(pair.client.Send(data, sizeof(data)), (ssize_t)sizeof(data));

  char buf[64];
  EXPECT_EQ(pair.server.Recv(buf, sizeof(buf)), (ssize_t)sizeof(data));
  EXPECT_STREQ(buf, "ping");
}

// The client-side jam scenario: backpressure must surface as 0 (drop, keep
// socket), never as the -1-with-internal-Disconnect that killed the channel.
TEST(LocalUdpClient, WouldBlockReturnsZeroAndKeepsSocket)
{
  ClientServerPair pair("wouldblock");
  ASSERT_TRUE(pair.Establish());

  const ssize_t res = pair.FloodUntilNotSent();
  ASSERT_EQ(res, 0) << "expected would-block sentinel 0 (got " << res
                    << "); -1 means EAGAIN is still a hard error that Disconnect()ed the socket";
  EXPECT_TRUE(pair.client.IsConnected());
}

// After the peer drains its queue, the SAME socket must carry traffic again.
// Pre-fix, the EAGAIN-triggered internal Disconnect() left the client dead
// until its owner ran a full reconnect (which several owners never do).
TEST(LocalUdpClient, ChannelRecoversAfterWouldBlock)
{
  ClientServerPair pair("recovery");
  ASSERT_TRUE(pair.Establish());

  ASSERT_EQ(pair.FloodUntilNotSent(), 0);
  ASSERT_TRUE(pair.client.IsConnected());

  pair.DrainServer();

  const std::vector<char> datagram(kDatagramSize, 'y');
  EXPECT_EQ(pair.client.Send(datagram.data(), datagram.size()), (ssize_t)datagram.size());

  std::vector<char> rbuf(kDatagramSize * 2);
  EXPECT_EQ(pair.server.Recv(rbuf.data(), rbuf.size()), (ssize_t)datagram.size());
}

// A genuinely broken channel must still be reported as -1, and the client still
// tears itself down (hard-error behavior unchanged).
TEST(LocalUdpClient, HardErrorReturnsMinusOneAndDisconnects)
{
  ClientServerPair pair("harderror");
  ASSERT_TRUE(pair.Establish());

  // Closing the server socket makes subsequent connected sends fail with
  // ECONNREFUSED — a hard error, not backpressure.
  pair.server.StopListening();

  const char data[] = "into the void";
  EXPECT_EQ(pair.client.Send(data, sizeof(data)), -1);
  EXPECT_FALSE(pair.client.IsConnected());
}

// Recv with nothing pending is transient: 0, socket kept.
TEST(LocalUdpClient, RecvWouldBlockReturnsZeroAndKeepsSocket)
{
  ClientServerPair pair("recvwouldblock");
  ASSERT_TRUE(pair.Establish());

  char buf[64];
  EXPECT_EQ(pair.client.Recv(buf, sizeof(buf)), 0);
  EXPECT_TRUE(pair.client.IsConnected());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Caller-contract mirrors.
//
// RobotConnectionManager / AnimComms / HAL radio are not host-buildable (they
// drag the engine/anim/robot CLAD build graphs). Their exact decision trees are
// replicated verbatim here against the real primitives so the branch this fix
// adds is regression-tested; the 5-line class branches themselves are validated
// live in the supervised deploy window.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

namespace {

// Verbatim mirror of RobotConnectionManager::SendData()'s NEW decision tree
// (engine/comms/robotConnectionManager.cpp), a LocalUdpClient caller
// (engine->anim). Would-block 0 -> return false, connection KEPT; hard error ->
// disconnect.
bool SendDataPerRobotConnectionManagerContract(LocalUdpClient& udpClient,
                                               const uint8_t* buffer,
                                               unsigned int size,
                                               bool& disconnectedOut)
{
  disconnectedOut = false;

  const ssize_t sent = udpClient.Send((const char *) buffer, size);
  if (sent == 0) {
    // Would-block: drop and keep (the fix). Pre-fix this fell into the
    // `sent != size` teardown below and tore down the engine->anim connection.
    return false;
  }
  if (sent != size) {
    // DisconnectCurrent() equivalent.
    if (udpClient.IsConnected()) {
      udpClient.Disconnect();
    }
    disconnectedOut = true;
    return false;
  }
  return true;
}

// Verbatim mirror of the LocalUdpServer-caller decision tree shared by
// HAL::RadioSendPacket (robot/hal/src/radio.cpp, robot->anim) and
// AnimComms::SendPacketToEngine (animProcess/.../animComms.cpp, anim->engine).
// Would-block 0 -> return false, client KEPT (NO Disconnect); hard error ->
// caller Disconnect()s (the server does not self-tear-down).
bool SendPacketPerServerCallerContract(LocalUdpServer& server,
                                       const char* buffer,
                                       size_t length,
                                       bool& disconnectedOut)
{
  disconnectedOut = false;
  if (!server.HasClient()) {
    return false;
  }
  const ssize_t bytesSent = server.Send(buffer, length);
  if (bytesSent == 0) {
    // Would-block: drop and keep (the fix). Pre-fix this fell into the
    // `bytesSent < length` teardown below and DisconnectRadio()/DisconnectEngine().
    return false;
  }
  if (bytesSent < (ssize_t) length) {
    server.Disconnect();
    disconnectedOut = true;
    return false;
  }
  return true;
}

} // anonymous namespace

// Orchestrator #4715 item 2a: one transient EAGAIN on the engine->anim socket
// must NOT make the engine treat the robot as gone. Would-block send ->
// connection retained -> subsequent send succeeds.
TEST(RobotConnectionManagerContract, WouldBlockSendKeepsConnection)
{
  ClientServerPair pair("rcmcontract");
  ASSERT_TRUE(pair.Establish());

  const std::vector<uint8_t> message(kDatagramSize, 'm');
  bool disconnected = false;
  bool sendResult = true;
  for (int i = 0; i < kMaxFloodDatagrams && sendResult; ++i) {
    sendResult = SendDataPerRobotConnectionManagerContract(
        pair.client, message.data(), (unsigned int)message.size(), disconnected);
  }
  ASSERT_FALSE(sendResult) << "flood never reached backpressure";

  EXPECT_FALSE(disconnected)
      << "would-block send tore down the engine->anim connection "
         "(RobotConnectionManager::Update() would return RESULT_FAIL_IO_CONNECTION_CLOSED)";
  ASSERT_TRUE(pair.client.IsConnected());

  pair.DrainServer();
  EXPECT_TRUE(SendDataPerRobotConnectionManagerContract(
      pair.client, message.data(), (unsigned int)message.size(), disconnected));
  EXPECT_FALSE(disconnected);
}

// Hard errors must still tear down through the same contract.
TEST(RobotConnectionManagerContract, HardErrorStillDisconnects)
{
  ClientServerPair pair("rcmharderror");
  ASSERT_TRUE(pair.Establish());

  pair.server.StopListening();

  const std::vector<uint8_t> message(kDatagramSize, 'm');
  bool disconnected = false;
  EXPECT_FALSE(SendDataPerRobotConnectionManagerContract(
      pair.client, message.data(), (unsigned int)message.size(), disconnected));
  EXPECT_TRUE(disconnected);
  EXPECT_FALSE(pair.client.IsConnected());
}

// Orchestrator #4715 items 2b/2c: one transient EAGAIN on the robot->anim
// (HAL::RadioSendPacket) or anim->engine (AnimComms::SendPacketToEngine) socket
// must NOT DisconnectRadio()/DisconnectEngine() — which, because InitComms()/
// InitRadio() run once per boot, would sever the channel until reboot.
TEST(ServerCallerContract, WouldBlockSendKeepsClient)
{
  // Server floods its sole client without the client draining; Send() surfaces
  // backpressure as 0 and the caller must keep the client.
  const std::string serverPath = SocketPath("servercaller_server");
  const std::string clientPath = SocketPath("servercaller_client");
  LocalUdpServer server{kSmallBufSz, kSmallBufSz};
  LocalUdpClient client{kSmallBufSz, kSmallBufSz};
  ASSERT_TRUE(server.StartListening(serverPath));
  ASSERT_TRUE(client.Connect(clientPath, serverPath));
  char cbuf[64];
  ASSERT_GE(server.Recv(cbuf, sizeof(cbuf)), 0);
  ASSERT_TRUE(server.HasClient());

  const std::vector<char> datagram(kDatagramSize, 'x');
  bool disconnected = false;
  bool sendResult = true;
  for (int i = 0; i < kMaxFloodDatagrams && sendResult; ++i) {
    sendResult = SendPacketPerServerCallerContract(
        server, datagram.data(), datagram.size(), disconnected);
  }
  ASSERT_FALSE(sendResult) << "flood never reached backpressure";
  EXPECT_FALSE(disconnected)
      << "would-block send tore down the sole client (DisconnectRadio/DisconnectEngine)";
  EXPECT_TRUE(server.HasClient());

  client.Disconnect();
  server.StopListening();
  unlink(serverPath.c_str());
  unlink(clientPath.c_str());
}

// Hard errors must still tear the client down through the same caller contract.
TEST(ServerCallerContract, HardErrorDisconnectsClient)
{
  const std::string serverPath = SocketPath("servercaller_hard_server");
  const std::string clientPath = SocketPath("servercaller_hard_client");
  LocalUdpServer server{kSmallBufSz, kSmallBufSz};
  LocalUdpClient client{kSmallBufSz, kSmallBufSz};
  ASSERT_TRUE(server.StartListening(serverPath));
  ASSERT_TRUE(client.Connect(clientPath, serverPath));
  char cbuf[64];
  ASSERT_GE(server.Recv(cbuf, sizeof(cbuf)), 0);
  ASSERT_TRUE(server.HasClient());

  // Client gone -> connected send fails with ECONNREFUSED (hard error).
  client.Disconnect();

  const std::vector<char> datagram(kDatagramSize, 'x');
  bool disconnected = false;
  EXPECT_FALSE(SendPacketPerServerCallerContract(
      server, datagram.data(), datagram.size(), disconnected));
  EXPECT_TRUE(disconnected);
  EXPECT_FALSE(server.HasClient());

  server.StopListening();
  unlink(serverPath.c_str());
  unlink(clientPath.c_str());
}
