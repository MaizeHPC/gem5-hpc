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
#include "debug/MAAPort.hh"
#include "debug/MAACpuPort.hh"
#include "debug/MAACachePort.hh"
#include "debug/MAAMemPort.hh"
#include "debug/MAAController.hh"
#include "sim/cur_tick.hh"
#include <cassert>
#include <cstdint>
#include <string>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif
namespace gem5 {
void MAA::sendPacket(FuncUnitType funcUnit, PacketPtr pkt, Tick tick, bool force_cache) {
    RequestPtr snoop_req = std::make_shared<Request>(pkt->req->getPaddr(),
                                                     pkt->req->getSize(),
                                                     pkt->req->getFlags(),
                                                     pkt->req->requestorId());
    bool isBlockCached = true;
    if (force_cache_access == false && force_cache == false) {
        PacketPtr snoop_pkt = new Packet(snoop_req, MemCmd::SnoopReq);
        snoop_pkt->setExpressSnoop();
        snoop_pkt->headerDelay = snoop_pkt->payloadDelay = 0;
        sendSnoopPacketCpu(snoop_pkt);
        isBlockCached = snoop_pkt->isBlockCached();
        DPRINTF(MAAPort, "%s: force_cache is false, snoop request for %s determined %s\n", __func__, pkt->print(), isBlockCached ? "cached" : "not cached");
        delete snoop_pkt;
    }
    OutstandingPacket outstanding_pkt(pkt, tick);
    bool send_cache = false;
    bool send_mem = false;
    if (funcUnit == FuncUnitType::INDIRECT) {
        if (isBlockCached) {
            send_cache = true;
            if (pkt->isRead()) {
                my_outstanding_indirect_cache_read_pkts.insert(outstanding_pkt);
                DPRINTF(MAAPort, "%s: inserting my_outstanding_indirect_cache_read_pkts\n", __func__);
            } else if (pkt->isWrite()) {
                my_outstanding_indirect_cache_write_pkts.insert(outstanding_pkt);
                DPRINTF(MAAPort, "%s: inserting my_outstanding_indirect_cache_write_pkts\n", __func__);
            } else {
                panic("Invalid packet type\n");
            }
        } else {
            send_mem = true;
            if (pkt->isRead()) {
                my_outstanding_indirect_mem_read_pkts.insert(outstanding_pkt);
                DPRINTF(MAAPort, "%s: inserting my_outstanding_indirect_mem_read_pkts\n", __func__);
            } else if (pkt->isWrite()) {
                my_outstanding_indirect_mem_write_pkts.insert(outstanding_pkt);
                DPRINTF(MAAPort, "%s: inserting my_outstanding_indirect_mem_write_pkts\n", __func__);
            } else {
                panic("Invalid packet type\n");
            }
        }
    } else if (funcUnit == FuncUnitType::STREAM) {
        send_cache = true;
        if (isBlockCached) {
            if (pkt->isRead()) {
                my_outstanding_stream_cache_read_pkts.insert(outstanding_pkt);
                DPRINTF(MAAPort, "%s: inserting my_outstanding_stream_cache_read_pkts\n", __func__);
            } else if (pkt->isWrite()) {
                my_outstanding_stream_cache_write_pkts.insert(outstanding_pkt);
                DPRINTF(MAAPort, "%s: inserting my_outstanding_stream_cache_write_pkts\n", __func__);
            } else {
                panic("Invalid packet type\n");
            }
        } else {
            if (pkt->isRead()) {
                my_outstanding_stream_mem_read_pkts.insert(outstanding_pkt);
                DPRINTF(MAAPort, "%s: inserting my_outstanding_stream_mem_read_pkts\n", __func__);
            } else if (pkt->isWrite()) {
                my_outstanding_stream_mem_write_pkts.insert(outstanding_pkt);
                DPRINTF(MAAPort, "%s: inserting my_outstanding_stream_mem_write_pkts\n", __func__);
            } else {
                panic("Invalid packet type\n");
            }
        }
    } else {
        panic("Invalid func unit type\n");
    }
    if (send_cache) {
        scheduleNextSendCache();
    } else if (send_mem) {
        scheduleNextSendMem();
    } else {
        panic("Invalid send type\n");
    }
}
bool MAA::scheduleNextSendMem() {
    bool return_val = false;
    Tick tick = 0;
    for (auto it = my_outstanding_indirect_mem_read_pkts.begin(); it != my_outstanding_indirect_mem_read_pkts.end();) {
        if (mem_channels_blocked[channel_addr(it->packet->getAddr())]) {
            ++it;
            continue;
        }
        if (return_val == false) {
            tick = it->tick;
            return_val = true;
        } else {
            tick = std::min(tick, it->tick);
        }
        break;
    }
    for (auto it = my_outstanding_indirect_mem_write_pkts.begin(); it != my_outstanding_indirect_mem_write_pkts.end();) {
        if (mem_channels_blocked[channel_addr(it->packet->getAddr())]) {
            ++it;
            continue;
        }
        if (return_val == false) {
            tick = it->tick;
            return_val = true;
        } else {
            tick = std::min(tick, it->tick);
        }
        break;
    }
    if (return_val) {
        Cycles latency = Cycles(0);
        if (tick > curTick()) {
            latency = getTicksToCycles(tick - curTick());
        }
        scheduleSendMemEvent(latency);
    }
    return return_val;
}
bool MAA::scheduleNextSendCache() {
    if (cache_blocked) {
        return false;
    }
    bool return_val = false;
    Tick tick = 0;
    for (auto it = my_outstanding_indirect_cache_read_pkts.begin(); it != my_outstanding_indirect_cache_read_pkts.end();) {
        if (return_val == false) {
            tick = it->tick;
            return_val = true;
        } else {
            tick = std::min(tick, it->tick);
        }
        break;
    }
    for (auto it = my_outstanding_indirect_cache_write_pkts.begin(); it != my_outstanding_indirect_cache_write_pkts.end();) {
        if (return_val == false) {
            tick = it->tick;
            return_val = true;
        } else {
            tick = std::min(tick, it->tick);
        }
        break;
    }
    for (auto it = my_outstanding_stream_cache_read_pkts.begin(); it != my_outstanding_stream_cache_read_pkts.end();) {
        if (return_val == false) {
            tick = it->tick;
            return_val = true;
        } else {
            tick = std::min(tick, it->tick);
        }
        break;
    }
    for (auto it = my_outstanding_stream_cache_write_pkts.begin(); it != my_outstanding_stream_cache_write_pkts.end();) {
        if (return_val == false) {
            tick = it->tick;
            return_val = true;
        } else {
            tick = std::min(tick, it->tick);
        }
        break;
    }
    if (my_outstanding_indirect_mem_read_pkts.empty() && my_outstanding_indirect_mem_write_pkts.empty()) {
        for (auto it = my_outstanding_stream_mem_read_pkts.begin(); it != my_outstanding_stream_mem_read_pkts.end();) {
            if (return_val == false) {
                tick = it->tick;
                return_val = true;
            } else {
                tick = std::min(tick, it->tick);
            }
            break;
        }
        for (auto it = my_outstanding_stream_mem_write_pkts.begin(); it != my_outstanding_stream_mem_write_pkts.end();) {
            if (return_val == false) {
                tick = it->tick;
                return_val = true;
            } else {
                tick = std::min(tick, it->tick);
            }
            break;
        }
    }
    if (return_val) {
        Cycles latency = Cycles(0);
        if (tick > curTick()) {
            latency = getTicksToCycles(tick - curTick());
        }
        scheduleSendCacheEvent(latency);
    }
    return return_val;
}
void MAA::unblockMemChannel(int channel_id) {
    panic_if(mem_channels_blocked[channel_id] == false, "%s: channel %d is not blocked!\n", __func__, channel_id);
    mem_channels_blocked[channel_id] = false;
    scheduleNextSendMem();
}
void MAA::unblockCache() {
    panic_if(cache_blocked == false, "%s: cache is not blocked!\n", __func__);
    cache_blocked = false;
    scheduleNextSendCache();
}
bool MAA::allIndirectPacketsSent() {
    return my_outstanding_indirect_cache_read_pkts.empty() && my_outstanding_indirect_cache_write_pkts.empty() && my_outstanding_indirect_mem_write_pkts.empty() && my_outstanding_indirect_mem_read_pkts.empty();
}
bool MAA::allStreamPacketsSent() {
    return my_outstanding_stream_cache_read_pkts.empty() && my_outstanding_stream_cache_write_pkts.empty() && my_outstanding_stream_mem_write_pkts.empty() && my_outstanding_stream_mem_read_pkts.empty();
}
bool MAA::sendOutstandingMemPacket() {
    bool packet_remaining = false;
    for (auto it = my_outstanding_indirect_mem_write_pkts.begin(); it != my_outstanding_indirect_mem_write_pkts.end();) {
        MAA::OutstandingPacket write_pkt = *it;
        if (write_pkt.tick > curTick()) {
            DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to memory\n", __func__, getTicksToCycles(write_pkt.tick - curTick()), write_pkt.packet->print());
            packet_remaining = true;
            break;
        }
        int ch = channel_addr(write_pkt.packet->getAddr());
        if (mem_channels_blocked[ch]) {
            ++it;
            continue;
        }
        DPRINTF(MAAPort, "%s: trying sending %s to memory\n", __func__, write_pkt.packet->print());
        if (sendPacketMem(write_pkt.packet) == false) {
            DPRINTF(MAAPort, "%s: send failed for channel %d\n", __func__, ch);
            mem_channels_blocked[ch] = true;
            ++it;
            continue;
        } else {
            it = my_outstanding_indirect_mem_write_pkts.erase(it);
            indirectAccessUnits[0].memWritePacketSent(write_pkt.packet);
            stats.port_mem_WR_packets += 1;
            continue;
        }
    }
    for (auto it = my_outstanding_indirect_mem_read_pkts.begin(); it != my_outstanding_indirect_mem_read_pkts.end();) {
        MAA::OutstandingPacket read_pkt = *it;
        if (read_pkt.tick > curTick()) {
            DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to memory\n", __func__, getTicksToCycles(read_pkt.tick - curTick()), read_pkt.packet->print());
            packet_remaining = true;
            break;
        }
        int ch = channel_addr(read_pkt.packet->getAddr());
        if (mem_channels_blocked[ch]) {
            ++it;
            continue;
        }
        DPRINTF(MAAPort, "%s: trying sending %s to memory\n", __func__, read_pkt.packet->print());
        if (sendPacketMem(read_pkt.packet) == false) {
            DPRINTF(MAAPort, "%s: send failed for channel %d\n", __func__, ch);
            mem_channels_blocked[ch] = true;
            ++it;
            continue;
        } else {
            it = my_outstanding_indirect_mem_read_pkts.erase(it);
            indirectAccessUnits[0].memReadPacketSent(read_pkt.packet);
            stats.port_mem_RD_packets += 1;
            continue;
        }
    }
    if (packet_remaining) {
        scheduleNextSendMem();
    }
    if (my_outstanding_indirect_mem_read_pkts.empty() && my_outstanding_indirect_mem_write_pkts.empty()) {
        scheduleNextSendCache();
    }
    return true;
}
bool MAA::sendOutstandingCachePacket() {
    bool packet_remaining = false;
    while (my_outstanding_indirect_cache_write_pkts.empty() == false && cache_blocked == false) {
        MAA::OutstandingPacket write_pkt = *my_outstanding_indirect_cache_write_pkts.begin();
        if (write_pkt.tick > curTick()) {
            DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(write_pkt.tick - curTick()), write_pkt.packet->print());
            packet_remaining = true;
            break;
        }
        DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, write_pkt.packet->print());
        if (sendPacketCache(write_pkt.packet) == false) {
            DPRINTF(MAAPort, "%s: send failed\n", __func__);
            cache_blocked = true;
            break;
        } else {
            my_outstanding_indirect_cache_write_pkts.erase(my_outstanding_indirect_cache_write_pkts.begin());
            indirectAccessUnits[0].cacheWritePacketSent(write_pkt.packet);
            stats.port_cache_WR_packets += 1;
            continue;
        }
    }
    while (my_outstanding_indirect_cache_read_pkts.empty() == false && cache_blocked == false) {
        MAA::OutstandingPacket read_pkt = *my_outstanding_indirect_cache_read_pkts.begin();
        if (read_pkt.tick > curTick()) {
            DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(read_pkt.tick - curTick()), read_pkt.packet->print());
            packet_remaining = true;
            break;
        }
        DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, read_pkt.packet->print());
        if (sendPacketCache(read_pkt.packet) == false) {
            DPRINTF(MAAPort, "%s: send failed\n", __func__);
            cache_blocked = true;
            break;
        } else {
            my_outstanding_indirect_cache_read_pkts.erase(my_outstanding_indirect_cache_read_pkts.begin());
            indirectAccessUnits[0].cacheReadPacketSent(read_pkt.packet);
            stats.port_cache_RD_packets += 1;
            continue;
        }
    }
    while (my_outstanding_stream_cache_write_pkts.empty() == false && cache_blocked == false) {
        MAA::OutstandingPacket write_pkt = *my_outstanding_stream_cache_write_pkts.begin();
        if (write_pkt.tick > curTick()) {
            DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(write_pkt.tick - curTick()), write_pkt.packet->print());
            packet_remaining = true;
            break;
        }
        DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, write_pkt.packet->print());
        if (sendPacketCache(write_pkt.packet) == false) {
            DPRINTF(MAAPort, "%s: send failed\n", __func__);
            cache_blocked = true;
            break;
        } else {
            my_outstanding_stream_cache_write_pkts.erase(my_outstanding_stream_cache_write_pkts.begin());
            streamAccessUnits[0].writePacketSent(write_pkt.packet);
            stats.port_cache_WR_packets += 1;
            continue;
        }
    }
    while (my_outstanding_stream_cache_read_pkts.empty() == false && cache_blocked == false) {
        MAA::OutstandingPacket read_pkt = *my_outstanding_stream_cache_read_pkts.begin();
        if (read_pkt.tick > curTick()) {
            DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(read_pkt.tick - curTick()), read_pkt.packet->print());
            packet_remaining = true;
            break;
        }
        DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, read_pkt.packet->print());
        if (sendPacketCache(read_pkt.packet) == false) {
            DPRINTF(MAAPort, "%s: send failed\n", __func__);
            cache_blocked = true;
            break;
        } else {
            my_outstanding_stream_cache_read_pkts.erase(my_outstanding_stream_cache_read_pkts.begin());
            streamAccessUnits[0].readPacketSent(read_pkt.packet);
            stats.port_cache_RD_packets += 1;
            continue;
        }
    }
    if (my_outstanding_indirect_mem_read_pkts.empty() && my_outstanding_indirect_mem_write_pkts.empty()) {
        while (my_outstanding_stream_mem_write_pkts.empty() == false && cache_blocked == false) {
            MAA::OutstandingPacket write_pkt = *my_outstanding_stream_mem_write_pkts.begin();
            if (write_pkt.tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(write_pkt.tick - curTick()), write_pkt.packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, write_pkt.packet->print());
            if (sendPacketCache(write_pkt.packet) == false) {
                DPRINTF(MAAPort, "%s: send failed\n", __func__);
                cache_blocked = true;
                break;
            } else {
                my_outstanding_stream_mem_write_pkts.erase(my_outstanding_stream_mem_write_pkts.begin());
                streamAccessUnits[0].writePacketSent(write_pkt.packet);
                stats.port_cache_WR_packets += 1;
                continue;
            }
        }
        while (my_outstanding_stream_mem_read_pkts.empty() == false && cache_blocked == false) {
            MAA::OutstandingPacket read_pkt = *my_outstanding_stream_mem_read_pkts.begin();
            if (read_pkt.tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(read_pkt.tick - curTick()), read_pkt.packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, read_pkt.packet->print());
            if (sendPacketCache(read_pkt.packet) == false) {
                DPRINTF(MAAPort, "%s: send failed\n", __func__);
                cache_blocked = true;
                break;
            } else {
                my_outstanding_stream_mem_read_pkts.erase(my_outstanding_stream_mem_read_pkts.begin());
                streamAccessUnits[0].readPacketSent(read_pkt.packet);
                stats.port_cache_RD_packets += 1;
                continue;
            }
        }
    }
    if (packet_remaining) {
        scheduleNextSendCache();
    }
    return true;
}
void MAA::scheduleSendCacheEvent(int latency) {
    DPRINTF(MAAPort, "%s: scheduling send cache packet in the next %d cycles!\n", __func__, latency);
    panic_if(latency < 0, "Negative latency of %d!\n", latency);
    Tick new_when = getClockEdge(Cycles(latency));
    if (!sendCacheEvent.scheduled()) {
        schedule(sendCacheEvent, new_when);
    } else {
        Tick old_when = sendCacheEvent.when();
        DPRINTF(MAAPort, "%s: send cache packet already scheduled for tick %d\n", __func__, old_when);
        if (new_when < old_when) {
            DPRINTF(MAAPort, "%s: rescheduling for tick %d!\n", __func__, new_when);
            reschedule(sendCacheEvent, new_when);
        }
    }
}
void MAA::scheduleSendMemEvent(int latency) {
    DPRINTF(MAAPort, "%s: scheduling send mem packet in the next %d cycles!\n", __func__, latency);
    panic_if(latency < 0, "Negative latency of %d!\n", latency);
    Tick new_when = getClockEdge(Cycles(latency));
    if (!sendMemEvent.scheduled()) {
        schedule(sendMemEvent, new_when);
    } else {
        Tick old_when = sendMemEvent.when();
        DPRINTF(MAAPort, "%s: send mem packet already scheduled for tick %d\n", __func__, old_when);
        if (new_when < old_when) {
            DPRINTF(MAAPort, "%s: rescheduling for tick %d!\n", __func__, new_when);
            reschedule(sendMemEvent, new_when);
        }
    }
}
} // namespace gem5