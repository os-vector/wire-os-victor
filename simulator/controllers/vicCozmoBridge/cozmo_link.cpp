#include "cozmo_link.h"

#include <chrono>

namespace cozmo {

static const double kMinRto     = 0.05;
static const double kMaxRto     = 0.2;
static const double kPingPeriod  = 0.5;
static const double kMinAckWait = 0.02;
static const size_t kMaxResendFrames = 3;

double MonotonicSeconds()
{
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static uint16_t SeqAdd(uint16_t seq, uint16_t n)
{
  return (uint16_t)(((uint32_t)seq + n) % kMaxSeq);
}

Link::Link()
  : sock_(VICNET_INVALID_SOCK)
  , havePeer_(false)
  , server_(false)
  , state_(kIdle)
  , sendWindow_(kWindowSize)
  , sendExpected_(0)
  , sendNext_(0)
  , recvWindow_(kWindowSize)
  , recvExpected_(0)
  , recvLast_(kWindowSize - 1)
  , lastAck_(0)
  , ackedAck_(0)
  , ackPending_(false)
  , lastAckTime_(0.0)
  , lastPingTime_(0.0)
  , pingCounter_(0)
  , recvFrames_(0)
  , recvPackets_(0)
  , sentFrames_(0)
  , sentPackets_(0)
  , oversizedPackets_(0)
  , resends_(0)
  , timeouts_(0)
  , srtt_(0.0)
  , rttvar_(0.0)
  , rttInit_(false)
  , lastRecvTime_(0.0)
  , rttMin_(0.0)
  , rttMax_(0.0)
  , rttSum_(0.0)
  , rttCount_(0)
  , discardedFrames_(0)
{
  memset(&peer_, 0, sizeof(peer_));
}

Link::~Link()
{
  Close();
}

bool Link::Open(const char* host, uint16_t port, bool server)
{
  Close();
  vicnet_init();
  server_ = server;

  if (server) {
    sock_ = vicnet_udp_server(host, port);
    havePeer_ = false;
  } else {
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ == VICNET_INVALID_SOCK) {
      return false;
    }
    vicnet_set_nonblocking(sock_);
    vicnet_make_addr(&peer_, host, port);
    havePeer_ = true;
  }
  if (sock_ != VICNET_INVALID_SOCK) {
    const int sndbuf = 256 * 1024;
    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf));
  }
  return sock_ != VICNET_INVALID_SOCK;
}

void Link::Close()
{
  if (sock_ != VICNET_INVALID_SOCK) {
    vicnet_close(sock_);
    sock_ = VICNET_INVALID_SOCK;
  }
  state_ = kIdle;
  havePeer_ = false;
  outQueue_.clear();
  SendWindowReset();
  RecvWindowReset();
}

bool Link::SendWindowFull() const
{
  uint32_t used;
  if (sendExpected_ > sendNext_) {
    used = (uint32_t)sendNext_ + kMaxSeq - sendExpected_;
  } else {
    used = (uint32_t)sendNext_ - sendExpected_;
  }
  return used >= kWindowSize;
}

uint16_t Link::SendWindowPut(const Packet& pkt)
{
  const uint16_t seq = sendNext_;
  Slot& slot = sendWindow_[seq % kWindowSize];
  slot.used = true;
  slot.sampled = true;
  slot.sent = MonotonicSeconds();
  slot.pkt = pkt;
  slot.pkt.seq = seq;
  sendNext_ = SeqAdd(sendNext_, 1);
  return seq;
}

bool Link::SendWindowAck(uint16_t seq)
{
  if (seq >= kMaxSeq) {
    return false;
  }
  bool outOfOrder;
  if (sendExpected_ > sendNext_) {
    outOfOrder = (seq >= sendNext_ && seq < sendExpected_);
  } else {
    outOfOrder = (seq < sendExpected_ || seq >= sendNext_);
  }
  if (outOfOrder) {
    return false;
  }
  bool advanced = false;
  const uint16_t stop = SeqAdd(seq, 1);
  const double now = MonotonicSeconds();
  while (sendExpected_ != stop) {
    Slot& slot = sendWindow_[sendExpected_ % kWindowSize];
    if (slot.used && slot.sampled) {
      const double rtt = now - slot.sent;
      if (rttCount_ == 0 || rtt < rttMin_) { rttMin_ = rtt; }
      if (rtt > rttMax_) { rttMax_ = rtt; }
      rttSum_ += rtt;
      ++rttCount_;
      if (!rttInit_) {
        rttInit_ = true;
        srtt_ = rtt;
        rttvar_ = rtt * 0.5;
      } else {
        const double err = rtt > srtt_ ? (rtt - srtt_) : (srtt_ - rtt);
        rttvar_ = 0.75 * rttvar_ + 0.25 * err;
        srtt_ = 0.875 * srtt_ + 0.125 * rtt;
      }
    }
    slot.used = false;
    sendExpected_ = SeqAdd(sendExpected_, 1);
    advanced = true;
  }
  return advanced;
}

double Link::AckWait() const
{
  double wait = rttInit_ ? (srtt_ + 4.0 * rttvar_) : kMinAckWait;
  if (wait < kMinAckWait) { wait = kMinAckWait; }
  if (wait > kMaxRto) { wait = kMaxRto; }
  return wait;
}

double Link::Rto() const
{
  double rto = rttInit_ ? (srtt_ + 4.0 * rttvar_) : kMinRto;
  if (rto < kMinRto) { rto = kMinRto; }
  if (rto > kMaxRto) { rto = kMaxRto; }
  return rto;
}

void Link::SendWindowPending(std::vector<std::pair<uint16_t, const Packet*> >& out)
{
  uint16_t seq = sendExpected_;
  while (seq != sendNext_) {
    const Slot& slot = sendWindow_[seq % kWindowSize];
    if (slot.used) {
      out.push_back(std::make_pair(seq, &slot.pkt));
    }
    seq = SeqAdd(seq, 1);
  }
}

void Link::SendWindowReset()
{
  for (size_t i = 0; i < sendWindow_.size(); ++i) {
    sendWindow_[i].used = false;
  }
  sendExpected_ = 0;
  sendNext_ = 0;
  lastAck_ = 0;
  ackedAck_ = 0;
  ackPending_ = false;
  lastAckTime_ = 0.0;
}

bool Link::RecvWindowOutOfOrder(uint16_t seq) const
{
  if (recvExpected_ > recvLast_) {
    return seq < recvExpected_ && seq > recvLast_;
  }
  return seq < recvExpected_ || seq > recvLast_;
}

void Link::RecvWindowPut(uint16_t seq, const Packet& pkt)
{
  if (seq >= kMaxSeq || RecvWindowOutOfOrder(seq)) {
    return;
  }
  Slot& slot = recvWindow_[seq % kWindowSize];
  if (slot.used) {
    return;
  }
  slot.used = true;
  slot.pkt = pkt;
}

bool Link::RecvWindowGet(Packet& out)
{
  Slot& slot = recvWindow_[recvExpected_ % kWindowSize];
  if (!slot.used) {
    return false;
  }
  out = slot.pkt;
  slot.used = false;
  recvExpected_ = SeqAdd(recvExpected_, 1);
  recvLast_ = SeqAdd(recvExpected_, kWindowSize - 1);
  return true;
}

void Link::RecvWindowReset()
{
  for (size_t i = 0; i < recvWindow_.size(); ++i) {
    recvWindow_[i].used = false;
  }
  recvExpected_ = 0;
  recvLast_ = kWindowSize - 1;
}

void Link::Connect()
{
  if (server_ || !IsOpen()) {
    return;
  }
  state_ = kConnecting;
  outQueue_.clear();
  SendWindowReset();
  RecvWindowReset();
  SendBare(kFrameReset);
}

void Link::Disconnect()
{
  if (!IsOpen() || state_ != kConnected) {
    return;
  }
  SendControlPacket(kPacketDisconnect);
  FlushOutgoing();
  state_ = kIdle;
}

void Link::SendControlPacket(uint8_t type)
{
  Packet pkt;
  pkt.type = type;
  pkt.id = 0;
  outQueue_.push_back(pkt);
}

void Link::SendCommandRaw(uint8_t id, const uint8_t* data, size_t len)
{
  if (len + 4 > kMaxFramePayloadSize) {
    ++oversizedPackets_;
    return;
  }
  Packet pkt;
  pkt.type = kPacketCommand;
  pkt.id = id;
  if (len > 0) {
    pkt.payload.assign(data, data + len);
  }
  outQueue_.push_back(pkt);
}

void Link::SendCommand(uint8_t id, const Writer& w)
{
  SendCommandRaw(id, w.Size() ? &w.Data()[0] : NULL, w.Size());
}

void Link::SendEvent(uint8_t id, const Writer& w)
{
  Packet pkt;
  pkt.type = kPacketEvent;
  pkt.id = id;
  if (w.Size() > 0) {
    pkt.payload.assign(w.Data().begin(), w.Data().end());
  }
  outQueue_.push_back(pkt);
}

void Link::SendUnreliable(uint8_t id, const Writer& w)
{
  if (!IsOpen() || !havePeer_) {
    return;
  }
  Packet pkt;
  pkt.type = kPacketCommand;
  pkt.id = id;
  if (w.Size() > 0) {
    pkt.payload.assign(w.Data().begin(), w.Data().end());
  }
  std::vector<const Packet*> one(1, &pkt);
  Writer frame;
  EncodeFrame(server_ ? kFrameRobot : kFrameEngine, kOobSeq, kOobSeq, lastAck_, one, frame);
  ++sentPackets_;
  SendRawFrame(frame);
}

void Link::EncodeFrame(uint8_t frameType, uint16_t firstSeq, uint16_t seq, uint16_t ack,
                       const std::vector<const Packet*>& pkts, Writer& out) const
{
  out.Bytes(kFrameId, sizeof(kFrameId));
  out.U8(frameType);
  out.U16((uint16_t)((firstSeq + 1) & 0xffff));
  out.U16((uint16_t)((seq + 1) & 0xffff));
  out.U16((uint16_t)((ack + 1) & 0xffff));

  if (frameType == kFramePing) {
    const Packet* pkt = pkts[0];
    out.Bytes(pkt->payload.empty() ? NULL : &pkt->payload[0], pkt->payload.size());
    return;
  }

  for (size_t i = 0; i < pkts.size(); ++i) {
    const Packet* pkt = pkts[i];
    const bool ided = (pkt->type == kPacketCommand || pkt->type == kPacketEvent);
    out.U8(pkt->type);
    out.U16((uint16_t)(pkt->payload.size() + (ided ? 1 : 0)));
    if (ided) {
      out.U8(pkt->id);
    }
    if (!pkt->payload.empty()) {
      out.Bytes(&pkt->payload[0], pkt->payload.size());
    }
  }
}

void Link::SendRawFrame(const Writer& w)
{
  if (!IsOpen() || !havePeer_ || w.Size() == 0) {
    return;
  }
  const ssize_t n = sendto(sock_, (const char*)&w.Data()[0], (int)w.Size(), 0,
                           (const struct sockaddr*)&peer_, sizeof(peer_));
  if (n < 0) {
    ++discardedFrames_;
    return;
  }
  ++sentFrames_;
}

void Link::SendBare(uint8_t frameType)
{
  Writer w;
  const std::vector<const Packet*> none;
  EncodeFrame(frameType, 0, 0, kOobSeq, none, w);
  SendRawFrame(w);
}

void Link::SendPing()
{
  Packet pkt;
  pkt.type = kPacketPing;
  Writer body;
  body.F64(MonotonicSeconds() * 1000.0);
  body.U32(pingCounter_++);
  body.U32(0);
  body.U8(0);
  pkt.payload.assign(body.Data().begin(), body.Data().end());

  std::vector<const Packet*> one(1, &pkt);
  Writer frame;
  EncodeFrame(kFramePing, kOobSeq, kOobSeq, lastAck_, one, frame);
  ++sentPackets_;
  SendRawFrame(frame);
}

void Link::EmitPackets(const std::vector<std::pair<uint16_t, const Packet*> >& pkts)
{
  if (pkts.empty()) {
    return;
  }
  const uint8_t frameType = server_ ? kFrameRobot : kFrameEngine;

  std::vector<const Packet*> batch;
  size_t batchLen = 0;
  uint16_t firstSeq = 0;
  uint16_t lastSeq = 0;

  for (size_t i = 0; i < pkts.size(); ++i) {
    const uint16_t seq = pkts[i].first;
    const Packet* pkt = pkts[i].second;
    const size_t pktLen = pkt->EncodedSize();

    if (!batch.empty() && batchLen + pktLen > kMaxFramePayloadSize) {
      Writer w;
      EncodeFrame(frameType, firstSeq, lastSeq, lastAck_, batch, w);
      sentPackets_ += batch.size();
      SendRawFrame(w);
      batch.clear();
      batchLen = 0;
    }
    if (batch.empty()) {
      firstSeq = seq;
    }
    batch.push_back(pkt);
    batchLen += pktLen;
    lastSeq = seq;
  }

  if (!batch.empty()) {
    Writer w;
    EncodeFrame(frameType, firstSeq, lastSeq, lastAck_, batch, w);
    sentPackets_ += batch.size();
    SendRawFrame(w);
  }
}

void Link::EmitOob(const std::vector<Packet>& pkts)
{
  if (pkts.empty()) {
    return;
  }
  const uint8_t frameType = server_ ? kFrameRobot : kFrameEngine;

  std::vector<const Packet*> batch;
  size_t batchLen = 0;
  for (size_t i = 0; i < pkts.size(); ++i) {
    const size_t pktLen = pkts[i].EncodedSize();
    if (pktLen > kMaxFramePayloadSize) {
      ++oversizedPackets_;
      continue;
    }
    if (!batch.empty() && batchLen + pktLen > kMaxFramePayloadSize) {
      Writer w;
      EncodeFrame(frameType, kOobSeq, kOobSeq, lastAck_, batch, w);
      sentPackets_ += batch.size();
      SendRawFrame(w);
      batch.clear();
      batchLen = 0;
    }
    batch.push_back(&pkts[i]);
    batchLen += pktLen;
  }
  if (!batch.empty()) {
    Writer w;
    EncodeFrame(frameType, kOobSeq, kOobSeq, lastAck_, batch, w);
    sentPackets_ += batch.size();
    SendRawFrame(w);
  }
}

void Link::SendAckOnly()
{
  const std::vector<const Packet*> none;
  Writer w;
  EncodeFrame(server_ ? kFrameRobot : kFrameEngine, kOobSeq, kOobSeq, lastAck_, none, w);
  SendRawFrame(w);
}

void Link::FlushOutgoing()
{
  if (!havePeer_) {
    return;
  }

  std::vector<std::pair<uint16_t, const Packet*> > pending;
  std::vector<std::pair<uint16_t, const Packet*> > resend;
  std::vector<Packet> oob;

  const double now = MonotonicSeconds();
  std::vector<std::pair<uint16_t, const Packet*> > stale;
  SendWindowPending(stale);
  const bool windowWasEmpty = stale.empty();
  if (!stale.empty() && lastAckTime_ > 0.0) {
    const double oldestSent = sendWindow_[stale[0].first % kWindowSize].sent;
    const bool shouldHaveBeenAcked = lastRecvTime_ > 0.0 && oldestSent < lastRecvTime_ - AckWait();
    if (shouldHaveBeenAcked || now - lastAckTime_ > Rto()) {
      lastAckTime_ = now;
      ++timeouts_;
      size_t frames = 0;
      size_t bytes = 0;
      for (size_t i = 0; i < stale.size(); ++i) {
        const size_t len = stale[i].second->EncodedSize();
        if (bytes > 0 && bytes + len > kMaxFramePayloadSize) {
          if (++frames >= kMaxResendFrames) {
            break;
          }
          bytes = 0;
        }
        bytes += len;
        ++resends_;
        Slot& slot = sendWindow_[stale[i].first % kWindowSize];
        slot.sampled = false;
        slot.sent = now;
        resend.push_back(stale[i]);
      }
    }
  }

  while (!outQueue_.empty()) {
    if (outQueue_.front().IsOob()) {
      oob.push_back(outQueue_.front());
      outQueue_.pop_front();
      continue;
    }
    if (SendWindowFull()) {
      break;
    }
    const uint16_t seq = SendWindowPut(outQueue_.front());
    outQueue_.pop_front();
    pending.push_back(std::make_pair(seq, &sendWindow_[seq % kWindowSize].pkt));
  }

  if (windowWasEmpty && !pending.empty()) {
    lastAckTime_ = now;
  }

  EmitPackets(resend);
  EmitPackets(pending);
  EmitOob(oob);

  if (ackPending_ && resend.empty() && pending.empty() && oob.empty()) {
    SendAckOnly();
  }
  if (!resend.empty() || !pending.empty() || !oob.empty() || ackPending_) {
    ackedAck_ = lastAck_;
    ackPending_ = false;
  }
}

void Link::Update()
{
  if (!IsOpen()) {
    return;
  }

  uint8_t buf[2048];
  for (;;) {
    struct sockaddr_in from;
    socklen_t fromLen = sizeof(from);
    memset(&from, 0, sizeof(from));
    const ssize_t n = recvfrom(sock_, (char*)buf, sizeof(buf), 0,
                               (struct sockaddr*)&from, &fromLen);
    if (n <= 0) {
      break;
    }
    HandleDatagram(buf, (size_t)n, from);
  }

  const double now = MonotonicSeconds();
  if (!server_ && state_ == kConnected && now - lastPingTime_ > kPingPeriod) {
    lastPingTime_ = now;
    SendPing();
  }

  FlushOutgoing();
}

void Link::HandleDatagram(const uint8_t* data, size_t len, const struct sockaddr_in& from)
{
  if (len < kMinFrameSize || memcmp(data, kFrameId, sizeof(kFrameId)) != 0) {
    ++discardedFrames_;
    return;
  }

  Reader r(data, len);
  r.SeekSet(sizeof(kFrameId));
  const uint8_t frameType = r.U8();
  const uint16_t firstSeq = (uint16_t)((r.U16() - 1) & 0xffff);
  const uint16_t seq = (uint16_t)((r.U16() - 1) & 0xffff);
  const uint16_t ack = (uint16_t)((r.U16() - 1) & 0xffff);
  if (!r.Ok()) {
    ++discardedFrames_;
    return;
  }

  if (frameType == kFrameReset) {
    HandleReset(from);
    return;
  }

  if (!havePeer_ ||
      from.sin_addr.s_addr != peer_.sin_addr.s_addr ||
      from.sin_port != peer_.sin_port) {
    ++discardedFrames_;
    return;
  }

  if (frameType == kFrameFin) {
    if (server_) {
      havePeer_ = false;
      state_ = kIdle;
      SendWindowReset();
      RecvWindowReset();
    }
    return;
  }

  HandleFrame(frameType, firstSeq, seq, ack, data + r.Tell(), len - r.Tell());
}

void Link::HandleReset(const struct sockaddr_in& from)
{
  if (!server_) {
    return;
  }
  peer_ = from;
  havePeer_ = true;
  outQueue_.clear();
  SendWindowReset();
  RecvWindowReset();
  state_ = kConnected;
  SendControlPacket(kPacketConnect);

  Packet pkt;
  pkt.type = kPacketConnect;
  Deliver(pkt);
}

void Link::HandleFrame(uint8_t frameType, uint16_t firstSeq, uint16_t seq, uint16_t ack,
                       const uint8_t* body, size_t bodyLen)
{
  ++recvFrames_;

  lastRecvTime_ = MonotonicSeconds();
  if (SendWindowAck(ack)) {
    lastAckTime_ = lastRecvTime_;
  }
  if (seq != kOobSeq) {
    lastAck_ = seq;
    if (server_ && lastAck_ != ackedAck_) {
      ackPending_ = true;
    }
  }

  if (frameType == kFramePing) {
    Packet pkt;
    pkt.type = kPacketPing;
    pkt.payload.assign(body, body + bodyLen);
    ++recvPackets_;
    if (server_) {
      outQueue_.push_back(pkt);
    }
    Deliver(pkt);
    return;
  }

  if (frameType != kFrameEngine && frameType != kFrameRobot && frameType != kFrameEngineAct) {
    return;
  }

  Reader r(body, bodyLen);
  const bool frameOob = (seq == kOobSeq || firstSeq == kOobSeq);
  uint16_t pktSeq = firstSeq;

  if (frameType == kFrameEngineAct) {
    if (bodyLen < 1) {
      return;
    }
    Packet pkt;
    pkt.type = kPacketCommand;
    pkt.id = body[0];
    pkt.payload.assign(body + 1, body + bodyLen);
    pkt.seq = pktSeq;
    ++recvPackets_;
    HandlePacket(pkt, frameOob);
    DeliverSequence();
    return;
  }

  while (r.Remaining() > 0) {
    const uint8_t pktType = r.U8();
    const uint16_t pktLen = r.U16();
    if (!r.Ok() || pktLen > r.Remaining()) {
      ++discardedFrames_;
      break;
    }
    const size_t end = r.Tell() + pktLen;

    Packet pkt;
    pkt.type = pktType;
    pkt.seq = pktSeq;

    const bool ided = (pktType == kPacketCommand || pktType == kPacketEvent);
    if (ided) {
      pkt.id = r.U8();
      pkt.payload.assign(r.Ptr(), r.Ptr() + (pktLen - 1));
    } else if (pktLen > 0) {
      pkt.payload.assign(r.Ptr(), r.Ptr() + pktLen);
    }
    r.SeekSet(end);

    ++recvPackets_;
    if (!pkt.IsOob()) {
      pktSeq = SeqAdd(pktSeq, 1);
    }

    if (pkt.type == kPacketDisconnect) {
      if (server_) {
        havePeer_ = false;
        SendWindowReset();
        RecvWindowReset();
      }
      state_ = kIdle;
      Deliver(pkt);
      return;
    }
    HandlePacket(pkt, frameOob);
  }

  DeliverSequence();
}

void Link::HandlePacket(const Packet& pkt, bool frameOob)
{
  if (frameOob || pkt.IsOob()) {
    Deliver(pkt);
  } else {
    RecvWindowPut(pkt.seq, pkt);
  }
}

void Link::DeliverSequence()
{
  Packet pkt;
  while (RecvWindowGet(pkt)) {
    Deliver(pkt);
  }
}

void Link::Deliver(const Packet& pkt)
{
  if (!server_ && pkt.type == kPacketConnect && state_ != kConnected) {
    state_ = kConnected;
    pingCounter_ = 0;
    lastPingTime_ = 0.0;
  }
  if (handler_) {
    handler_(pkt);
  }
}

}  // namespace cozmo
