#include "mem/MAA/ALU.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/IndirectAccess.hh"
#include "mem/MAA/Invalidator.hh"
#include "mem/MAA/RangeFuser.hh"
#include "mem/MAA/SPD.hh"
#include "mem/MAA/StreamAccess.hh"
#include "mem/MAA/MAA.hh"

#include "base/addr_range.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "mem/packet.hh"
#include "params/MAA.hh"
#include "debug/MAA.hh"
#include "debug/MAACpuPort.hh"
#include "debug/MAACachePort.hh"
#include "debug/MAAMemPort.hh"
#include "debug/MAAController.hh"
#include "sim/cur_tick.hh"
#include <cassert>
#include <cstdint>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {

void MAA::recvMemTimingResp(PacketPtr pkt) {
    DPRINTF(MAAMemPort, "%s: received %s, cmd: %s, size: %d\n", __func__, pkt->print(), pkt->cmdString(), pkt->getSize());
    switch (pkt->cmd.toInt()) {
    case MemCmd::ReadExResp:
    case MemCmd::ReadResp: {
        assert(pkt->getSize() == 64);
        std::vector<uint32_t> data;
        std::vector<uint16_t> wid;
        for (int i = 0; i < 64; i += 4) {
            if (pkt->req->getByteEnable()[i] == true) {
                data.push_back(*(pkt->getPtr<uint32_t>() + i / 4));
                wid.push_back(i / 4);
            }
        }
        bool received = false;
        for (int i = 0; i < num_maas; i++) {
            if (indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Fill ||
                indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Request) {
                if (indirectAccessUnits[i].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>(), false)) {
                    panic_if(received, "Received multiple responses for the same request\n");
                }
            }
        }
        for (int i = 0; i < num_maas; i++) {
            if (streamAccessUnits[i].getState() == StreamAccessUnit::Status::Request) {
                panic_if(streamAccessUnits[i].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>()),
                         "Received multiple responses for the same request\n");
            }
        }
        break;
    }
    default:
        assert(false);
    }
}
bool MAA::MemSidePort::recvTimingResp(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    maa->recvMemTimingResp(pkt);
    delete pkt;
    return true;
}

void MAA::recvMemTimingSnoopReq(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
}
// Express snooping requests to memside port
void MAA::MemSidePort::recvTimingSnoopReq(PacketPtr pkt) {
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    // handle snooping requests
    maa->recvMemTimingSnoopReq(pkt);
    assert(false);
}

Tick MAA::recvMemAtomicSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
    return 0;
}
Tick MAA::MemSidePort::recvAtomicSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    return maa->recvMemAtomicSnoop(pkt);
    assert(false);
}

void MAA::memFunctionalAccess(PacketPtr pkt, bool from_cpu_side) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
}
void MAA::MemSidePort::recvFunctionalSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    // functional snoop (note that in contrast to atomic we don't have
    // a specific functionalSnoop method, as they have the same
    // behaviour regardless)
    maa->memFunctionalAccess(pkt, false);
    assert(false);
}

void MAA::MemSidePort::recvReqRetry() {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: called!\n", __func__);
    maa->unblockMemChannel(channel_id);
}
bool MAA::sendPacketMem(PacketPtr pkt) {
    int pkt_channel_id = channel_addr(pkt->getAddr());
    return memSidePorts[pkt_channel_id]->sendTimingReq(pkt);
}
void MAA::MemSidePort::allocate(int _channel_id) {
    channel_id = _channel_id;
    DPRINTF(MAAMemPort, "%s channel %d\n", __func__, channel_id);
}

void MAA::MAAReqPacketQueue::sendDeferredPacket() {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: called!\n", __func__);
    assert(false);
}

MAA::MemSidePort::MemSidePort(const std::string &_name,
                              MAA *_maa,
                              const std::string &_label)
    : MAAMemRequestPort(_name, _reqQueue, _snoopRespQueue),
      _reqQueue(*_maa, *this, _snoopRespQueue, _label),
      _snoopRespQueue(*_maa, *this, true, _label), maa(_maa) {
}
} // namespace gem5