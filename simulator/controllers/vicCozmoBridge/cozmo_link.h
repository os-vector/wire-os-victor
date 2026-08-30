#ifndef __COZMO_LINK_H__
#define __COZMO_LINK_H__

#include <stdint.h>
#include <string.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "vic_net.h"

namespace cozmo {

enum FrameType {
  kFrameReset     = 1,
  kFrameResetAck  = 2,
  kFrameFin       = 3,
  kFrameEngineAct = 4,
  kFrameEngine    = 7,
  kFrameRobot     = 9,
  kFramePing      = 0x0b
};

enum PacketType {
  kPacketConnect    = 2,
  kPacketDisconnect = 3,
  kPacketCommand    = 4,
  kPacketEvent      = 5,
  kPacketKeyframe   = 0x0a,
  kPacketPing       = 0x0b
};

static const uint8_t  kFrameId[7]           = { 'C', 'O', 'Z', 0x03, 'R', 'E', 0x01 };
static const size_t   kMinFrameSize         = 14;
static const size_t   kMaxFrameSize         = 1051;
static const size_t   kMaxFramePayloadSize  = kMaxFrameSize - kMinFrameSize;
static const size_t   kMaxDisplayRleSize    = kMaxFramePayloadSize - 6;
static const uint16_t kMaxSeq               = 0xfffe;
static const uint16_t kOobSeq               = 0xffff;
static const uint16_t kWindowSize           = 62;
static const uint8_t  kFirstRobotPacketId   = 0xb0;
static const uint16_t kFirmwareVersion      = 2381;
static const uint16_t kRobotPort            = 5551;

struct Packet {
  uint8_t  type;
  uint8_t  id;
  uint16_t seq;
  std::vector<uint8_t> payload;

  Packet() : type(0), id(0), seq(0) {}
  bool IsOob() const { return type >= kPacketEvent; }
  size_t EncodedSize() const {
    const bool ided = (type == kPacketCommand || type == kPacketEvent);
    return 3 + (ided ? 1 : 0) + payload.size();
  }
};

class Writer {
public:
  void U8(uint8_t v) { buf_.push_back(v); }
  void I8(int8_t v) { Raw(&v, 1); }
  void U16(uint16_t v) { Raw(&v, 2); }
  void I16(int16_t v) { Raw(&v, 2); }
  void U32(uint32_t v) { Raw(&v, 4); }
  void I32(int32_t v) { Raw(&v, 4); }
  void F32(float v) { Raw(&v, 4); }
  void F64(double v) { Raw(&v, 8); }
  void Bytes(const void* p, size_t n) { Raw(p, n); }
  void Zero(size_t n) { buf_.insert(buf_.end(), n, 0); }

  const std::vector<uint8_t>& Data() const { return buf_; }
  size_t Size() const { return buf_.size(); }
  void Clear() { buf_.clear(); }

private:
  void Raw(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    buf_.insert(buf_.end(), b, b + n);
  }
  std::vector<uint8_t> buf_;
};

class Reader {
public:
  Reader(const uint8_t* data, size_t len) : data_(data), len_(len), pos_(0), ok_(true) {}

  uint8_t  U8()  { uint8_t v = 0; Raw(&v, 1); return v; }
  int8_t   I8()  { int8_t v = 0; Raw(&v, 1); return v; }
  uint16_t U16() { uint16_t v = 0; Raw(&v, 2); return v; }
  int16_t  I16() { int16_t v = 0; Raw(&v, 2); return v; }
  uint32_t U32() { uint32_t v = 0; Raw(&v, 4); return v; }
  int32_t  I32() { int32_t v = 0; Raw(&v, 4); return v; }
  float    F32() { float v = 0.f; Raw(&v, 4); return v; }
  double   F64() { double v = 0.0; Raw(&v, 8); return v; }
  void     Bytes(void* p, size_t n) { Raw(p, n); }
  void     Skip(size_t n) { if (pos_ + n > len_) { ok_ = false; return; } pos_ += n; }

  bool   Ok() const { return ok_; }
  size_t Tell() const { return pos_; }
  size_t Remaining() const { return pos_ <= len_ ? len_ - pos_ : 0; }
  void   SeekSet(size_t p) { if (p > len_) { ok_ = false; return; } pos_ = p; }
  const uint8_t* Ptr() const { return data_ + pos_; }

private:
  void Raw(void* p, size_t n) {
    if (pos_ + n > len_) { ok_ = false; memset(p, 0, n); return; }
    memcpy(p, data_ + pos_, n);
    pos_ += n;
  }
  const uint8_t* data_;
  size_t len_;
  size_t pos_;
  bool ok_;
};

class Link {
public:
  enum State { kIdle, kConnecting, kConnected };
  typedef std::function<void(const Packet&)> PacketHandler;

  Link();
  ~Link();

  bool Open(const char* host, uint16_t port, bool server);
  void Close();
  bool IsOpen() const { return sock_ != VICNET_INVALID_SOCK; }

  void SetHandler(const PacketHandler& h) { handler_ = h; }

  void Connect();
  void Disconnect();
  void Update();

  void SendCommand(uint8_t id, const Writer& w);
  void SendCommandRaw(uint8_t id, const uint8_t* data, size_t len);
  void SendUnreliable(uint8_t id, const Writer& w);
  void SendEvent(uint8_t id, const Writer& w);

  State GetState() const { return state_; }

  uint64_t RecvFrames() const { return recvFrames_; }
  uint64_t RecvPackets() const { return recvPackets_; }
  uint64_t SentFrames() const { return sentFrames_; }
  uint64_t SentPackets() const { return sentPackets_; }
  uint64_t OversizedPackets() const { return oversizedPackets_; }
  uint64_t Resends() const { return resends_; }
  uint64_t DiscardedFrames() const { return discardedFrames_; }
  uint64_t Timeouts() const { return timeouts_; }
  double Rto() const;
  double AckWait() const;
  double RttMin() const { return rttCount_ ? rttMin_ : 0.0; }
  double RttMax() const { return rttMax_; }
  double RttAvg() const { return rttCount_ ? rttSum_ / (double)rttCount_ : 0.0; }
  void RttReset() { rttMin_ = 0.0; rttMax_ = 0.0; rttSum_ = 0.0; rttCount_ = 0; }

private:
  struct Slot {
    bool used;
    bool sampled;
    double sent;
    Packet pkt;
    Slot() : used(false), sampled(false), sent(0.0) {}
  };

  bool SendWindowFull() const;
  uint16_t SendWindowPut(const Packet& pkt);
  bool SendWindowAck(uint16_t seq);
  void SendWindowPending(std::vector<std::pair<uint16_t, const Packet*> >& out);
  void SendWindowReset();

  bool RecvWindowOutOfOrder(uint16_t seq) const;
  void RecvWindowPut(uint16_t seq, const Packet& pkt);
  bool RecvWindowGet(Packet& out);
  void RecvWindowReset();

  void HandleDatagram(const uint8_t* data, size_t len, const struct sockaddr_in& from);
  void HandleFrame(uint8_t type, uint16_t firstSeq, uint16_t seq, uint16_t ack,
                   const uint8_t* body, size_t bodyLen);
  void HandleReset(const struct sockaddr_in& from);
  void HandlePacket(const Packet& pkt, bool frameOob);
  void Deliver(const Packet& pkt);
  void DeliverSequence();

  void EncodeFrame(uint8_t frameType, uint16_t firstSeq, uint16_t seq, uint16_t ack,
                   const std::vector<const Packet*>& pkts, Writer& out) const;
  void FlushOutgoing();
  void EmitPackets(const std::vector<std::pair<uint16_t, const Packet*> >& pkts);
  void EmitOob(const std::vector<Packet>& pkts);
  void SendAckOnly();
  void SendRawFrame(const Writer& w);
  void SendPing();
  void SendBare(uint8_t frameType);
  void SendControlPacket(uint8_t type);

  vicnet_sock_t sock_;
  struct sockaddr_in peer_;
  bool havePeer_;
  bool server_;
  State state_;
  PacketHandler handler_;

  std::vector<Slot> sendWindow_;
  uint16_t sendExpected_;
  uint16_t sendNext_;

  std::vector<Slot> recvWindow_;
  uint16_t recvExpected_;
  uint16_t recvLast_;

  std::deque<Packet> outQueue_;
  uint16_t lastAck_;
  uint16_t ackedAck_;
  bool ackPending_;
  double lastAckTime_;
  double lastPingTime_;
  uint32_t pingCounter_;

  uint64_t recvFrames_;
  uint64_t recvPackets_;
  uint64_t sentFrames_;
  uint64_t sentPackets_;
  uint64_t oversizedPackets_;
  uint64_t resends_;
  uint64_t timeouts_;
  double srtt_;
  double rttvar_;
  bool rttInit_;
  double lastRecvTime_;
  double rttMin_;
  double rttMax_;
  double rttSum_;
  uint64_t rttCount_;
  uint64_t discardedFrames_;
};

double MonotonicSeconds();

}  // namespace cozmo

#endif
