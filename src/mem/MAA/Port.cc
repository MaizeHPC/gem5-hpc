#include "mem/MAA/ALU.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/IndirectAccess1.hh"
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
void MAA::sendPacket(FuncUnitType funcUnit, uint8_t maaID, PacketPtr pkt, Tick tick, bool force_cache) {
    Addr paddr = pkt->req->getPaddr();
    panic_if(pkt->getAddr() != paddr, "%s: paddr 0x%lx and addr 0x%lx do not match for packet %s\n", __func__, paddr, pkt->getAddr(), pkt->print());
    if (my_outstanding_pkt_map.find(paddr) != my_outstanding_pkt_map.end()) {
        DPRINTF(MAAPort, "%s: found %s in outstanding packets\n", __func__, pkt->print());
        if ((my_outstanding_pkt_map[paddr].cmd == MemCmd::WritebackDirty || my_outstanding_pkt_map[paddr].cmd == MemCmd::WriteReq)
                         && (pkt->cmd == MemCmd::ReadExReq || pkt->cmd == MemCmd::ReadReq)) {
            DPRINTF(MAAPort, "%s: store to load forwarding for outstanding write packet %s and new read packet %s\n", __func__, my_outstanding_pkt_map[paddr].packet->print(), pkt->print());
            panic_if(my_outstanding_pkt_map[paddr].maaIDs.size() != 1, "%s: multiple units on outstanding write packet %s\n", __func__, my_outstanding_pkt_map[paddr].packet->print());
            // taking this panic cond  // my_outstanding_pkt_map[paddr].funcUnits[0] != funcUnit ||
            panic_if(my_outstanding_pkt_map[paddr].maaIDs[0] != maaID, "%s: outstanding write maaID %d, funcUnit %s, packet %s do not match with new read maaID %d, funcUnit %s, packet %s\n", __func__, my_outstanding_pkt_map[paddr].maaIDs[0], func_unit_names[(uint8_t)my_outstanding_pkt_map[paddr].funcUnits[0]], my_outstanding_pkt_map[paddr].packet->print(), maaID, func_unit_names[(uint8_t)funcUnit], pkt->print());
            if (funcUnit == FuncUnitType::INDIRECT) {
                if (my_outstanding_pkt_map[paddr].cached) {
                    indirectAccessUnits[maaID].cacheReadPacketSent(paddr);
                } else {
                    indirectAccessUnits[maaID].memReadPacketSent(paddr);
                }
                panic_if(indirectAccessUnits[maaID].recvData(paddr, my_outstanding_pkt_map[paddr].packet->getPtr<uint8_t>(), my_outstanding_pkt_map[paddr].cached) == false, "%s: received %s but rejected from indirectAccessUnits[%d]\n", __func__, my_outstanding_pkt_map[paddr].packet->print(), maaID);
            } else if (funcUnit == FuncUnitType::STREAM) {
                streamAccessUnits[maaID].readPacketSent(paddr);
                panic_if(streamAccessUnits[maaID].recvData(paddr, my_outstanding_pkt_map[paddr].packet->getPtr<uint8_t>(), my_outstanding_pkt_map[paddr].cached) == false, "%s: received %s but rejected from streamAccessUnits[%d]\n", __func__, my_outstanding_pkt_map[paddr].packet->print(), maaID);
            } else if (funcUnit == FuncUnitType::ALU) {
                if (my_outstanding_pkt_map[paddr].cached) {
                    aluUnits[maaID].cacheReadPacketSent(paddr);
                } else {
                    aluUnits[maaID].memReadPacketSent(paddr);
                }
                panic_if(aluUnits[maaID].recvData(paddr, my_outstanding_pkt_map[paddr].packet->getPtr<uint8_t>(), my_outstanding_pkt_map[paddr].cached) == false, "%s: received %s but rejected from indirectAccessUnits[%d]\n", __func__, my_outstanding_pkt_map[paddr].packet->print(), maaID);
            }  else if (funcUnit == FuncUnitType::RANGE) {
                panic_if(rangeUnits[maaID].recvData(paddr, my_outstanding_pkt_map[paddr].packet->getPtr<uint8_t>(), my_outstanding_pkt_map[paddr].cached) == false, "%s: received %s but rejected from indirectAccessUnits[%d]\n", __func__, my_outstanding_pkt_map[paddr].packet->print(), maaID);
            } else {
                panic("Invalid func unit type\n");
            }
        } else if (my_outstanding_pkt_map[paddr].cmd == MemCmd::WritebackDirty && pkt->cmd == MemCmd::WritebackDirty) {
            DPRINTF(MAAPort, "%s: store to store replacement for outstanding write packet %s and new write packet %s\n", __func__, my_outstanding_pkt_map[paddr].packet->print(), pkt->print());
            panic_if(my_outstanding_pkt_map[paddr].maaIDs.size() != 1, "%s: multiple units on outstanding write packet %s\n", __func__, my_outstanding_pkt_map[paddr].packet->print());
            panic_if(my_outstanding_pkt_map[paddr].funcUnits[0] != funcUnit || my_outstanding_pkt_map[paddr].maaIDs[0] != maaID, "%s: outstanding write maaID %d, funcUnit %s, packet %s do not match with new write maaID %d, funcUnit %s, packet %s\n", __func__, my_outstanding_pkt_map[paddr].maaIDs[0], func_unit_names[(uint8_t)my_outstanding_pkt_map[paddr].funcUnits[0]], my_outstanding_pkt_map[paddr].packet->print(), maaID, func_unit_names[(uint8_t)funcUnit], pkt->print());
            my_outstanding_pkt_map[paddr].packet->setData(pkt->getPtr<uint8_t>());
            if (funcUnit == FuncUnitType::INDIRECT) {
                if (my_outstanding_pkt_map[paddr].cached) {
                    indirectAccessUnits[maaID].cacheWritePacketSent(paddr);
                } else {
                    indirectAccessUnits[maaID].memWritePacketSent(paddr);
                }
            } else if (funcUnit == FuncUnitType::STREAM) {
                streamAccessUnits[maaID].writePacketSent(paddr);
            } else {
                panic("Invalid func unit type\n");
            }
        } else {
            panic_if(my_outstanding_pkt_map[paddr].cmd != pkt->cmd, "%s Outstanding command %s from packet %s does not match with command %s from packet %s\n", __func__, my_outstanding_pkt_map[paddr].cmd.toString(), my_outstanding_pkt_map[paddr].packet->print(), pkt->cmdString(), pkt->print());
            panic_if(pkt->isWrite(), "%s cannot have duplicated writes %s and %s\n", __func__, my_outstanding_pkt_map[paddr].packet->print(), pkt->print());
            panic_if(pkt->isRead() == false, "%s: packet %s is not read!\n", __func__, pkt->print());
            for (int i = 0; i < my_outstanding_pkt_map[paddr].maaIDs.size(); i++) {
                panic_if(my_outstanding_pkt_map[paddr].maaIDs[i] == maaID && my_outstanding_pkt_map[paddr].funcUnits[i] == funcUnit, "%s: maaID %d and funcUnit %s already in the outstanding packet %s\n", __func__, maaID, func_unit_names[(uint8_t)funcUnit], pkt->print());
            }
            // overwriting if outstanding WriteReqs 
            if (my_outstanding_pkt_map[paddr].cmd == MemCmd::WriteReq  && pkt->cmd == MemCmd::WriteReq){
                my_outstanding_pkt_map[paddr].packet->setData(pkt->getPtr<uint8_t>());
            }
            my_outstanding_pkt_map[paddr].maaIDs.push_back(maaID);
            my_outstanding_pkt_map[paddr].funcUnits.push_back(funcUnit);
            if (my_outstanding_pkt_map[paddr].sent == false) {
                if (my_outstanding_pkt_map[paddr].tick < tick) {
                    my_outstanding_pkt_map[paddr].tick = tick;
                }
                if (funcUnit == FuncUnitType::INDIRECT) {
                    my_num_outstanding_indirect_pkts[maaID]++;
                } else if (funcUnit == FuncUnitType::STREAM) {
                    my_num_outstanding_stream_pkts[maaID]++;
                } else if(funcUnit == FuncUnitType::ALU){
                    my_num_outstanding_alu_pkts[maaID]++;
                } else if(funcUnit == FuncUnitType::RANGE){
                    my_num_outstanding_rangefuser_pkts[maaID]++;
                } else {
                    panic("Invalid func unit type\n");
                }
            } else {
                if (funcUnit == FuncUnitType::INDIRECT) {
                    if (my_outstanding_pkt_map[paddr].cached) {
                        indirectAccessUnits[maaID].cacheReadPacketSent(paddr);
                    } else {
                        indirectAccessUnits[maaID].memReadPacketSent(paddr);
                    }
                } else if (funcUnit == FuncUnitType::STREAM) {
                    streamAccessUnits[maaID].readPacketSent(paddr);
                } else {
                    panic("Invalid func unit type\n");
                }
            }
        }
    } else {
        my_outstanding_pkt_map[paddr] = OutstandingPacket(pkt, paddr, tick, pkt->cmd);
        my_outstanding_pkt_map[paddr].cached = force_cache;
        if (force_cache_access == false && force_cache == false) {
            RequestPtr snoop_req = std::make_shared<Request>(pkt->req->getPaddr(), pkt->req->getSize(), pkt->req->getFlags(), pkt->req->requestorId());
            PacketPtr snoop_pkt = new Packet(snoop_req, MemCmd::SnoopReq);
            snoop_pkt->setExpressSnoop();
            snoop_pkt->headerDelay = snoop_pkt->payloadDelay = 0;
            sendSnoopPacketCpu(snoop_pkt);
            my_outstanding_pkt_map[paddr].cached = snoop_pkt->isBlockCached();
            DPRINTF(MAAPort, "%s: force_cache is false, snoop request for %s determined %s\n", __func__, pkt->print(), my_outstanding_pkt_map[paddr].cached ? "cached" : "not cached");
            delete snoop_pkt;
        }
        my_outstanding_pkt_map[paddr].maaIDs.push_back(maaID);
        my_outstanding_pkt_map[paddr].funcUnits.push_back(funcUnit);
        int core_id = core_addr(paddr);
        int channel_id = channel_addr(paddr);
        bool send_cache = false;
        bool send_mem = false;
        if (funcUnit == FuncUnitType::INDIRECT) {
            my_num_outstanding_indirect_pkts[maaID]++;
            if (my_outstanding_pkt_map[paddr].cached) {
                send_cache = true;
                if (pkt->isRead()) {
                    my_outstanding_indirect_cache_read_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_indirect_cache_read_pkts[%s\n", __func__, core_id);
                } else if (pkt->isWrite()) {
                    my_outstanding_indirect_cache_write_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_indirect_cache_write_pkts[%s\n", __func__, core_id);
                } else {
                    panic("Invalid packet type\n");
                }
            } else {
                send_mem = true;
                if (pkt->isRead()) {
                    my_outstanding_indirect_mem_read_pkts[channel_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_indirect_mem_read_pkts[%s\n", __func__, channel_id);
                } else if (pkt->isWrite()) {
                    my_outstanding_indirect_mem_write_pkts[channel_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_indirect_mem_write_pkts[%s\n", __func__, channel_id);
                } else {
                    panic("Invalid packet type\n");
                }
            }
        } else if (funcUnit == FuncUnitType::STREAM) {
            my_num_outstanding_stream_pkts[maaID]++;
            
            if (my_outstanding_pkt_map[paddr].cached) {
                send_cache = true;
                if (pkt->isRead()) {
                    my_outstanding_stream_cache_read_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_stream_cache_read_pkts[%s\n", __func__, core_id);
                } else if (pkt->isWrite()) {
                    my_outstanding_stream_cache_write_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_stream_cache_write_pkts[%s\n", __func__, core_id);
                } else {
                    panic("Invalid packet type\n");
                }
            } else {
                panic("all stream packets should send through cache\n\n");
                // send_mem = true;
                // if (pkt->isRead()) {
                //     my_outstanding_stream_mem_read_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                //     DPRINTF(MAAPort, "%s: inserting my_outstanding_stream_mem_read_pkts[%s\n", __func__, core_id);
                // } else if (pkt->isWrite()) {
                //     my_outstanding_stream_mem_write_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                //     DPRINTF(MAAPort, "%s: inserting my_outstanding_stream_mem_write_pkts[%s\n", __func__, core_id);
                // } else {
                //     panic("Invalid packet type\n");
                // }
            }
        } else if (funcUnit == FuncUnitType::ALU) {
            my_num_outstanding_alu_pkts[maaID]++;
            if (my_outstanding_pkt_map[paddr].cached) {
                send_cache = true;
                if (pkt->isRead()) {
                    my_outstanding_alu_cache_read_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_alu_cache_read_pkts[%s\n", __func__, core_id);
                } else if (pkt->isWrite()) {
                    my_outstanding_alu_cache_write_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_alu_cache_write_pkts[%s\n", __func__, core_id);
                } else {
                    panic("Invalid packet type\n");
                }
            } else {
                panic("Tile access should always go through cache\n");
            }
        
        } else if (funcUnit == FuncUnitType::RANGE) {
            my_num_outstanding_rangefuser_pkts[maaID]++;
            if (my_outstanding_pkt_map[paddr].cached) {
                send_cache = true;
                if (pkt->isRead()) {
                    my_outstanding_rangefuser_cache_read_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_range_cache_read_pkts[%s\n", __func__, core_id);
                } else if (pkt->isWrite()) {
                    my_outstanding_rangefuser_cache_write_pkts[core_id].insert(my_outstanding_pkt_map[paddr]);
                    DPRINTF(MAAPort, "%s: inserting my_outstanding_range_cache_write_pkts[%s\n", __func__, core_id);
                } else {
                    panic("Invalid packet type\n");
                }
            } else {
                panic("Tile access should always go through cache\n");
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
}
bool MAA::scheduleNextSendMem() {
    bool return_val = false;
    Tick tick = 0;
    bool all_channels_blocked =true;
    for (int ch = 0; ch < num_channels; ch++) {
        if (mem_channels_blocked[ch]){
            continue;
        } else {
            all_channels_blocked = false;
        }
        if (my_outstanding_indirect_mem_read_pkts[ch].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_indirect_mem_read_pkts[ch].begin()->tick;
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_indirect_mem_read_pkts[ch].begin()->tick);
            }
        }
        if (my_outstanding_indirect_mem_write_pkts[ch].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_indirect_mem_write_pkts[ch].begin()->tick;
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_indirect_mem_write_pkts[ch].begin()->tick);
            }
        }
    }
    if (return_val) {
        Cycles latency = Cycles(0);
        if (tick > curTick()) {
            latency = getTicksToCycles(tick - curTick());
        }
        scheduleSendMemEvent(latency);
    }
    if(all_channels_blocked){
        DPRINTF(MAAPort, "%s: all channels are blocked, no mem event scheduled\n", __func__);
    }
    return return_val;
}
bool MAA::allIndirectEmpty() {
    for (int ch = 0; ch < num_channels; ch++) {
        if (my_outstanding_indirect_mem_read_pkts[ch].empty() == false)
            return false;
        if (my_outstanding_indirect_mem_write_pkts[ch].empty() == false)
            return false;
    }
    return true;
}
bool MAA::scheduleNextSendCache() {
    bool return_val = false;
    bool all_indirect_empty = allIndirectEmpty();
    Tick tick = 0;
    for (int core_id = 0; core_id < num_cores; core_id++) {
        if (cache_bus_blocked[core_id])
            continue;
        if (my_outstanding_indirect_cache_read_pkts[core_id].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_indirect_cache_read_pkts[core_id].begin()->tick;
                DPRINTF(MAAPort, "%s: my_outstanding_indirect_cache_read_pkts.size %d\n", __func__, my_outstanding_indirect_cache_read_pkts[core_id].size());
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_indirect_cache_read_pkts[core_id].begin()->tick);
            }
        }
        if (my_outstanding_indirect_cache_write_pkts[core_id].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_indirect_cache_write_pkts[core_id].begin()->tick;
                DPRINTF(MAAPort, "%s: my_outstanding_indirect_cache_write_pkts.size %d\n", __func__, my_outstanding_indirect_cache_write_pkts[core_id].size());
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_indirect_cache_write_pkts[core_id].begin()->tick);
            }
        }
        if (my_outstanding_stream_cache_read_pkts[core_id].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_stream_cache_read_pkts[core_id].begin()->tick;
                DPRINTF(MAAPort, "%s: my_outstanding_stream_cache_read_pkts.size %d\n", __func__, my_outstanding_stream_cache_read_pkts[core_id].size());
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_stream_cache_read_pkts[core_id].begin()->tick);
            }
        }
        if (my_outstanding_stream_cache_write_pkts[core_id].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_stream_cache_write_pkts[core_id].begin()->tick;
                DPRINTF(MAAPort, "%s: my_outstanding_stream_cache_write_pkts.size %d\n", __func__, my_outstanding_stream_cache_write_pkts[core_id].size());
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_stream_cache_write_pkts[core_id].begin()->tick);
            }
        }

        if (my_outstanding_alu_cache_read_pkts[core_id].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_alu_cache_read_pkts[core_id].begin()->tick;
                DPRINTF(MAAPort, "%s: my_outstanding_alu_cache_read_pkts.size %d\n", __func__, my_outstanding_alu_cache_read_pkts[core_id].size());
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_alu_cache_read_pkts[core_id].begin()->tick);
            }
        }

        if (my_outstanding_alu_cache_write_pkts[core_id].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_alu_cache_write_pkts[core_id].begin()->tick;
                DPRINTF(MAAPort, "%s: my_outstanding_alu_cache_write_pkts.size %d\n", __func__, my_outstanding_alu_cache_write_pkts[core_id].size());
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_alu_cache_write_pkts[core_id].begin()->tick);
            }
        }

        if (my_outstanding_rangefuser_cache_read_pkts[core_id].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_rangefuser_cache_read_pkts[core_id].begin()->tick;
                DPRINTF(MAAPort, "%s: my_outstanding_rangefuser_cache_read_pkts.size %d\n", __func__, my_outstanding_rangefuser_cache_read_pkts[core_id].size());
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_rangefuser_cache_read_pkts[core_id].begin()->tick);
            }
        }

        if (my_outstanding_rangefuser_cache_write_pkts[core_id].empty() == false) {
            if (return_val == false) {
                tick = my_outstanding_rangefuser_cache_write_pkts[core_id].begin()->tick;
                DPRINTF(MAAPort, "%s: my_outstanding_rangefuser_cache_write_pkts.size %d\n", __func__, my_outstanding_rangefuser_cache_write_pkts[core_id].size());
                return_val = true;
            } else {
                tick = std::min(tick, my_outstanding_rangefuser_cache_write_pkts[core_id].begin()->tick);
            }
        }


        if (all_indirect_empty) {
            if (my_outstanding_stream_mem_read_pkts[core_id].empty() == false) {
                if (return_val == false) {
                    tick = my_outstanding_stream_mem_read_pkts[core_id].begin()->tick;
                    DPRINTF(MAAPort, "%s: my_outstanding_stream_mem_read_pkts.size %d\n", __func__, my_outstanding_stream_mem_read_pkts[core_id].size());
                    return_val = true;
                } else {
                    tick = std::min(tick, my_outstanding_stream_mem_read_pkts[core_id].begin()->tick);
                }
            }
            if (my_outstanding_stream_mem_write_pkts[core_id].empty() == false) {
                if (return_val == false) {
                    tick = my_outstanding_stream_mem_write_pkts[core_id].begin()->tick;
                    DPRINTF(MAAPort, "%s: my_outstanding_stream_mem_write_pkts.size %d\n", __func__, my_outstanding_stream_mem_write_pkts[core_id].size());
                    return_val = true;
                } else {
                    tick = std::min(tick, my_outstanding_stream_mem_write_pkts[core_id].begin()->tick);
                }
            }
        }
    }
    if (return_val) {
        DPRINTF(MAAPort, "%s: found some outstanding transaction to send\n", __func__);
        
        Cycles latency = Cycles(1);
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
void MAA::unblockCache(int core_id) {
    panic_if(cache_bus_blocked[core_id] == false, "%s: cache %d is not blocked!\n", __func__, core_id);
    cache_bus_blocked[core_id] = false;
    scheduleNextSendCache();
}
bool MAA::allIndirectPacketsSent(uint8_t maaID) {
    return my_num_outstanding_indirect_pkts[maaID] == 0;
}
bool MAA::allStreamPacketsSent(uint8_t maaID) {
    return my_num_outstanding_stream_pkts[maaID] == 0;
}
bool MAA::sendOutstandingMemPacket() {
    bool packet_remaining = false;
    bool all_empty = true;
    bool all_channel_blocked = true;

    for (int ch = 0; ch < num_channels; ch++) {
        if (mem_channels_blocked[ch])
            continue;
        else 
            all_channel_blocked = false;

        for (auto it = my_outstanding_indirect_mem_write_pkts[ch].begin(); it != my_outstanding_indirect_mem_write_pkts[ch].end();) {
            if (it->tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to memory\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s to memory\n", __func__, it->packet->print());
            if (sendPacketMem(it->packet) == false) {
                DPRINTF(MAAPort, "%s: send failed for channel %d\n", __func__, ch);
                mem_channels_blocked[ch] = true;
                break;
            } else {
                Addr paddr = it->paddr;
                panic_if(it->packet->needsResponse(), "%s write packet %s needs response!\n", __func__, it->packet->print());
                OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                my_outstanding_pkt_map.erase(paddr);
                panic_if(tmp.maaIDs.size() != 1, "%s multiple write packes coalesced into one!\n", __func__);
                panic_if(tmp.funcUnits[0] != FuncUnitType::INDIRECT, "%s: func unit type %d does not match with %d\n", __func__, func_unit_names[(uint8_t)tmp.funcUnits[0]], func_unit_names[(uint8_t)FuncUnitType::INDIRECT]);
                my_num_outstanding_indirect_pkts[tmp.maaIDs[0]]--;
                indirectAccessUnits[tmp.maaIDs[0]].memWritePacketSent(it->paddr);
                it = my_outstanding_indirect_mem_write_pkts[ch].erase(it);
                stats.port_mem_WR_packets += 1;
            }
        }
        if (mem_channels_blocked[ch])
            continue;
        else 
            all_channel_blocked = false;

        for (auto it = my_outstanding_indirect_mem_read_pkts[ch].begin(); it != my_outstanding_indirect_mem_read_pkts[ch].end();) {
            if (it->tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to memory\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s to memory\n", __func__, it->packet->print());
            if (sendPacketMem(it->packet) == false) {
                DPRINTF(MAAPort, "%s: send failed for channel %d\n", __func__, ch);
                mem_channels_blocked[ch] = true;
                break;
            } else {
                Addr paddr = it->paddr;
                OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                for (int i = 0; i < tmp.maaIDs.size(); i++) {
                    if (tmp.funcUnits[i] == FuncUnitType::INDIRECT) {
                        my_num_outstanding_indirect_pkts[tmp.maaIDs[i]]--;
                        indirectAccessUnits[tmp.maaIDs[i]].memReadPacketSent(it->paddr);
                    } else if (tmp.funcUnits[i] == FuncUnitType::STREAM) {
                        my_num_outstanding_stream_pkts[tmp.maaIDs[i]]--;
                        DPRINTF(MAAPort, "%s: outstanding stream count %d\n", __func__, my_num_outstanding_stream_pkts[tmp.maaIDs[i]]);
                        streamAccessUnits[tmp.maaIDs[i]].readPacketSent(it->paddr);
                    } else {
                        panic("Invalid func unit type\n");
                    }
                }
                my_outstanding_pkt_map[paddr].sent = true;
                it = my_outstanding_indirect_mem_read_pkts[ch].erase(it);
                stats.port_mem_RD_packets += 1;
            }
        }
        if (my_outstanding_indirect_mem_read_pkts[ch].empty() == false || my_outstanding_indirect_mem_write_pkts[ch].empty() == false) {
            all_empty = false;
        }
    }
    if (packet_remaining) {
        scheduleNextSendMem();
    }
    if (all_empty) {
        scheduleNextSendCache();
    }

    // if(all_channel_blocked){
    //     DPRINTF(MAAPort, "%s: all memory channels are blocked, trying again in a clock cycles\n", __func__);
    //     scheduleSendMemEvent(Cycles(1));
    // }

    return true;
}


bool MAA::sendOutstandingCachePacket() {
    bool packet_remaining = false;
    bool all_indirect_empty = allIndirectEmpty();
    bool all_channel_blocked = true;
    bool all_packet_queue_empty = true;
    DPRINTF(MAAPort, "%s: Trying to send Outstanding cache packets\n", __func__);
    for (int core = 0; core < num_cores; core++) {
        if (cache_bus_blocked[core])
            continue;
        else 
            all_channel_blocked = false;

        // ------------------------------------------------------------
        // ----------------- INDIRECT ---------------------------------
        // -----------------------------------------------------------
        for (auto it = my_outstanding_indirect_cache_write_pkts[core].begin(); it != my_outstanding_indirect_cache_write_pkts[core].end();) {
            all_packet_queue_empty = false;
            if (it->tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, it->packet->print());
            if (sendPacketCache(it->packet) == false) {
                DPRINTF(MAAPort, "%s: send failed for bus %d\n", __func__, core);
                cache_bus_blocked[core] = true;
                break;
            } else {
                Addr paddr = it->paddr;
                // panic_if(it->packet->needsResponse(), "%s write packet %s needs response!\n", __func__, it->packet->print());
                OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                if(tmp.cmd != MemCmd::WriteReq){
                    my_outstanding_pkt_map.erase(paddr);
                } else {
                    my_outstanding_pkt_map[paddr].sent = true;
                }
                panic_if(tmp.maaIDs.size() != 1, "%s multiple write packes coalesced into one!\n", __func__);
                panic_if(tmp.funcUnits[0] != FuncUnitType::INDIRECT, "%s: func unit type %d does not match with %d\n", __func__, func_unit_names[(uint8_t)tmp.funcUnits[0]], func_unit_names[(uint8_t)FuncUnitType::INDIRECT]);
                my_num_outstanding_indirect_pkts[tmp.maaIDs[0]]--;
                indirectAccessUnits[tmp.maaIDs[0]].cacheWritePacketSent(it->paddr);
                it = my_outstanding_indirect_cache_write_pkts[core].erase(it);
                stats.port_cache_WR_packets += 1;
            }
        }
        if (cache_bus_blocked[core])
            continue;
        else 
            all_channel_blocked = false;

        for (auto it = my_outstanding_indirect_cache_read_pkts[core].begin(); it != my_outstanding_indirect_cache_read_pkts[core].end();) {
            all_packet_queue_empty = false;
            if (it->tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, it->packet->print());
            if (sendPacketCache(it->packet) == false) {
                DPRINTF(MAAPort, "%s: send failed for bus %d\n", __func__, core);
                cache_bus_blocked[core] = true;
                break;
            } else {
                Addr paddr = it->paddr;
                OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                for (int i = 0; i < tmp.maaIDs.size(); i++) {
                    if (tmp.funcUnits[i] == FuncUnitType::INDIRECT) {
                        my_num_outstanding_indirect_pkts[tmp.maaIDs[i]]--;
                        indirectAccessUnits[tmp.maaIDs[i]].cacheReadPacketSent(it->paddr);
                    } else if (tmp.funcUnits[i] == FuncUnitType::STREAM) {
                        my_num_outstanding_stream_pkts[tmp.maaIDs[i]]--;
                        DPRINTF(MAAPort, "%s: outstanding stream count %d\n", __func__, my_num_outstanding_stream_pkts[tmp.maaIDs[i]]);
                        streamAccessUnits[tmp.maaIDs[i]].readPacketSent(it->paddr);
                    } else {
                        panic("Invalid func unit type\n");
                    }
                }
                my_outstanding_pkt_map[paddr].sent = true;
                it = my_outstanding_indirect_cache_read_pkts[core].erase(it);
                stats.port_cache_RD_packets += 1;
            }
        }

        // -----------------------------------------------------------
        // ----------------- STREAM ---------------------------------
        // -----------------------------------------------------------

        if (cache_bus_blocked[core])
            continue;
        else 
            all_channel_blocked = false;

        for (auto it = my_outstanding_stream_cache_write_pkts[core].begin(); it != my_outstanding_stream_cache_write_pkts[core].end();) {
            all_packet_queue_empty = false;
            if (it->tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, it->packet->print());
            if (sendPacketCache(it->packet) == false) {
                DPRINTF(MAAPort, "%s: send failed for bus %d\n", __func__, core);
                cache_bus_blocked[core] = true;
                break;
            } else {
                Addr paddr = it->paddr;
                // panic_if(it->packet->needsResponse(), "%s write packet %s needs response!\n", __func__, it->packet->print());
                OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                if(tmp.cmd != MemCmd::WriteReq){
                    my_outstanding_pkt_map.erase(paddr);
                    
                } else {
                    my_outstanding_pkt_map[paddr].sent = true;
                }
                panic_if(tmp.maaIDs.size() != 1, "%s multiple write packes coalesced into one!\n", __func__);
                panic_if(tmp.funcUnits[0] != FuncUnitType::STREAM, "%s: func unit type %d does not match with %d\n", __func__, func_unit_names[(uint8_t)tmp.funcUnits[0]], func_unit_names[(uint8_t)FuncUnitType::STREAM]);
                my_num_outstanding_stream_pkts[tmp.maaIDs[0]]--;
                DPRINTF(MAAPort, "%s: outstanding stream count %d\n", __func__, my_num_outstanding_stream_pkts[tmp.maaIDs[0]]);
                if(tmp.cmd != MemCmd::WriteReq){
                    streamAccessUnits[tmp.maaIDs[0]].writePacketSent(it->paddr);
                }
                it = my_outstanding_stream_cache_write_pkts[core].erase(it);
                stats.port_cache_WR_packets += 1;
            }
        }
        if (cache_bus_blocked[core])
            continue;
        else 
            all_channel_blocked = false;

        for (auto it = my_outstanding_stream_cache_read_pkts[core].begin(); it != my_outstanding_stream_cache_read_pkts[core].end();) {
            all_packet_queue_empty = false;
            if (it->tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s to cache\n", __func__, it->packet->print());
            if (sendPacketCache(it->packet) == false) {
                DPRINTF(MAAPort, "%s: send failed for bus %d\n", __func__, core);
                cache_bus_blocked[core] = true;
                break;
            } else {
                Addr paddr = it->paddr;
                OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                for (int i = 0; i < tmp.maaIDs.size(); i++) {
                    if (tmp.funcUnits[i] == FuncUnitType::INDIRECT) {
                        my_num_outstanding_indirect_pkts[tmp.maaIDs[i]]--;
                        indirectAccessUnits[tmp.maaIDs[i]].cacheReadPacketSent(it->paddr);
                    } else if (tmp.funcUnits[i] == FuncUnitType::STREAM) {
                        my_num_outstanding_stream_pkts[tmp.maaIDs[i]]--;
                        DPRINTF(MAAPort, "%s: outstanding stream count %d\n", __func__, my_num_outstanding_stream_pkts[tmp.maaIDs[i]]);
                        streamAccessUnits[tmp.maaIDs[i]].readPacketSent(it->paddr);
                    } else {
                        panic("Invalid func unit type\n");
                    }
                }
                my_outstanding_pkt_map[paddr].sent = true;
                it = my_outstanding_stream_cache_read_pkts[core].erase(it);
                stats.port_cache_RD_packets += 1;
            }
        }

        // ---------------------------------------------------------
        // ------------------ ALU ----------------------------------
        // ---------------------------------------------------------
        if (cache_bus_blocked[core])
            continue;
        else 
            all_channel_blocked = false;


        for (auto it = my_outstanding_alu_cache_write_pkts[core].begin(); it != my_outstanding_alu_cache_write_pkts[core].end();) {
            all_packet_queue_empty = false;
            if (it->tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s (ALU packet) to cache\n", __func__, it->packet->print());
            if (sendPacketCache(it->packet) == false) {
                DPRINTF(MAAPort, "%s: send failed for bus %d\n", __func__, core);
                cache_bus_blocked[core] = true;
                break;
            } else {
                Addr paddr = it->paddr;
                // Write Req needs response 
                // panic_if(it->packet->needsResponse(), "%s write packet %s needs response!\n", __func__, it->packet->print());
                OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                my_outstanding_pkt_map[paddr].sent = true;
                panic_if(tmp.maaIDs.size() != 1, "%s multiple write packes coalesced into one!\n", __func__);
                panic_if(tmp.funcUnits[0] != FuncUnitType::ALU, "%s: func unit type %d does not match with %d\n", __func__, func_unit_names[(uint8_t)tmp.funcUnits[0]], func_unit_names[(uint8_t)FuncUnitType::STREAM]);
                my_num_outstanding_alu_pkts[tmp.maaIDs[0]]--;
                it = my_outstanding_alu_cache_write_pkts[core].erase(it);
                stats.port_cache_WR_packets += 1;
            }
        }
        if (cache_bus_blocked[core])
            continue;
        else 
            all_channel_blocked = false;

        for (auto it = my_outstanding_alu_cache_read_pkts[core].begin(); it != my_outstanding_alu_cache_read_pkts[core].end();) {
            all_packet_queue_empty = false;
            if (it->tick > curTick()) {
                DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                packet_remaining = true;
                break;
            }
            DPRINTF(MAAPort, "%s: trying sending %s (ALU packet) to cache\n", __func__, it->packet->print());
            if (sendPacketCache(it->packet) == false) {
                DPRINTF(MAAPort, "%s: send failed for bus %d\n", __func__, core);
                cache_bus_blocked[core] = true;
                break;
            } else {
                Addr paddr = it->paddr;
                OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                for (int i = 0; i < tmp.maaIDs.size(); i++) {
                    if (tmp.funcUnits[i] == FuncUnitType::INDIRECT) {
                        my_num_outstanding_indirect_pkts[tmp.maaIDs[i]]--;
                        indirectAccessUnits[tmp.maaIDs[i]].cacheReadPacketSent(it->paddr);
                    } else if (tmp.funcUnits[i] == FuncUnitType::STREAM) {
                        my_num_outstanding_stream_pkts[tmp.maaIDs[i]]--;
                        DPRINTF(MAAPort, "%s: outstanding stream count %d\n", __func__, my_num_outstanding_stream_pkts[tmp.maaIDs[i]]);
                        streamAccessUnits[tmp.maaIDs[i]].readPacketSent(it->paddr);
                    } else if (tmp.funcUnits[i] == FuncUnitType::ALU) {
                        my_num_outstanding_alu_pkts[tmp.maaIDs[i]]--;
                        DPRINTF(MAAPort, "%s: outstanding alu count %d\n", __func__, my_num_outstanding_alu_pkts[tmp.maaIDs[i]]);
                    } else if (tmp.funcUnits[i] == FuncUnitType::RANGE) {
                        my_num_outstanding_rangefuser_pkts[tmp.maaIDs[i]]--;
                    } else {
                        panic("Invalid func unit type\n");
                    }
                }
                my_outstanding_pkt_map[paddr].sent = true;
                it = my_outstanding_alu_cache_read_pkts[core].erase(it);
                stats.port_cache_RD_packets += 1;
            }
        }



        // ------------------------------------------------------
        // -------------------- RANGE ---------------------------
        // ------------------------------------------------------



        if (all_indirect_empty) {
            if (cache_bus_blocked[core])
                continue;
            else   
                all_channel_blocked = false;

            for (auto it = my_outstanding_rangefuser_cache_write_pkts[core].begin(); it != my_outstanding_rangefuser_cache_write_pkts[core].end();) {
                all_packet_queue_empty = false;
                if (it->tick > curTick()) {
                    DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                    packet_remaining = true;
                    break;
                }
                DPRINTF(MAAPort, "%s: trying sending %s (range packet) to cache\n", __func__, it->packet->print());
                if (sendPacketCache(it->packet) == false) {
                    DPRINTF(MAAPort, "%s: send failed for bus %d\n", __func__, core);
                    cache_bus_blocked[core] = true;
                    break;
                } else {
                    Addr paddr = it->paddr;
                    OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                    my_outstanding_pkt_map[paddr].sent = true;
                    panic_if(tmp.maaIDs.size() != 1, "%s multiple write packes coalesced into one!\n", __func__);
                    panic_if(tmp.funcUnits[0] != FuncUnitType::RANGE, "%s: func unit type %d does not match with %d\n", __func__, func_unit_names[(uint8_t)tmp.funcUnits[0]], func_unit_names[(uint8_t)FuncUnitType::STREAM]);
                    my_num_outstanding_rangefuser_pkts[tmp.maaIDs[0]]--;
                    it = my_outstanding_rangefuser_cache_write_pkts[core].erase(it);
                    stats.port_cache_WR_packets += 1;
                }
            }
            if (cache_bus_blocked[core])
                continue;
            else 
                all_channel_blocked = false;

            for (auto it = my_outstanding_rangefuser_cache_read_pkts[core].begin(); it != my_outstanding_rangefuser_cache_read_pkts[core].end();) {
                all_packet_queue_empty = false;
                if (it->tick > curTick()) {
                    DPRINTF(MAAPort, "%s: waiting for %d cycles to send %s to cache\n", __func__, getTicksToCycles(it->tick - curTick()), it->packet->print());
                    packet_remaining = true;
                    break;
                }
                DPRINTF(MAAPort, "%s: trying sending %s (range packet) to cache\n", __func__, it->packet->print());
                if (sendPacketCache(it->packet) == false) {
                    DPRINTF(MAAPort, "%s: send failed for bus %d\n", __func__, core);
                    cache_bus_blocked[core] = true;
                    break;
                } else {
                    Addr paddr = it->paddr;
                    OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
                    for (int i = 0; i < tmp.maaIDs.size(); i++) {
                        if (tmp.funcUnits[i] == FuncUnitType::INDIRECT) {
                            my_num_outstanding_indirect_pkts[tmp.maaIDs[i]]--;
                            indirectAccessUnits[tmp.maaIDs[i]].cacheReadPacketSent(it->paddr);
                        } else if (tmp.funcUnits[i] == FuncUnitType::STREAM) {
                            my_num_outstanding_stream_pkts[tmp.maaIDs[i]]--;
                            DPRINTF(MAAPort, "%s: outstanding stream count %d\n", __func__, my_num_outstanding_stream_pkts[tmp.maaIDs[i]]);
                            streamAccessUnits[tmp.maaIDs[i]].readPacketSent(it->paddr);
                        } else if(tmp.funcUnits[i] == FuncUnitType::ALU){
                            my_num_outstanding_alu_pkts[tmp.maaIDs[i]]--;
                        }else if(tmp.funcUnits[i] == FuncUnitType::RANGE){
                            my_num_outstanding_rangefuser_pkts[tmp.maaIDs[i]]--;
                        } else {
                            panic("Invalid func unit type\n");
                        }
                    }
                    my_outstanding_pkt_map[paddr].sent = true;
                    it = my_outstanding_rangefuser_cache_read_pkts[core].erase(it);
                    stats.port_cache_RD_packets += 1;
                }
            }
        }
    }

    if (packet_remaining) {
        scheduleNextSendCache();
    }

    if(all_channel_blocked){
        DPRINTF(MAAPort, "%s: all channels are blocked\n", __func__);
    }

    if(!all_channel_blocked && all_packet_queue_empty){
        DPRINTF(MAAPort, "%s: there is no packet to send\n", __func__);
        // scheduleSendCacheEvent(Cycles(1));
    }

    return true;
}

void MAA::recvTimingResp(PacketPtr pkt, bool cached) {
    DPRINTF(MAAPort, "%s: received %s, cmd: %s, size: %d\n", __func__, pkt->print(), pkt->cmdString(), pkt->getSize());
    panic_if(pkt->cmd.toInt() != MemCmd::ReadExResp && pkt->cmd.toInt() != MemCmd::ReadResp && pkt->cmd.toInt() != MemCmd::WriteResp, "%s received an unknown response: %s\n", __func__, pkt->print());
    assert(pkt->getSize() == 64);
    Addr paddr = pkt->req->getPaddr();
    panic_if(my_outstanding_pkt_map.find(paddr) == my_outstanding_pkt_map.end(), "%s: response for packet %s not found in my_outstanding_pkt_map\n", __func__, pkt->print());
    OutstandingPacket tmp = my_outstanding_pkt_map[paddr];
    panic_if(tmp.sent == false, "%s received response %s for an unsent packet!\n", pkt->cmdString(), pkt->getSize());
    my_outstanding_pkt_map.erase(paddr);
    for (int i = 0; i < tmp.maaIDs.size(); i++) {
        if (tmp.funcUnits[i] == FuncUnitType::INDIRECT) {
            panic_if(indirectAccessUnits[tmp.maaIDs[i]].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>(), tmp.cached) == false, "%s: received %s but rejected from indirectAccessUnits[%d]\n", __func__, pkt->print(), tmp.maaIDs[i]);
        } else if (tmp.funcUnits[i] == FuncUnitType::STREAM) {
            panic_if(streamAccessUnits[tmp.maaIDs[i]].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>(), tmp.cached) == false, "%s: received %s but rejected from streamAccessUnits[%d]\n", __func__, pkt->print(), tmp.maaIDs[i]);
        } else if (tmp.funcUnits[i] == FuncUnitType::ALU) {
            panic_if(aluUnits[tmp.maaIDs[i]].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>(), tmp.cached) == false, "%s: received %s but rejected from alu units[%d]\n", __func__, pkt->print(), tmp.maaIDs[i]);
        } else if (tmp.funcUnits[i] == FuncUnitType::RANGE) {
            panic_if(rangeUnits[tmp.maaIDs[i]].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>(), tmp.cached) == false, "%s: received %s but rejected from range units[%d]\n", __func__, pkt->print(), tmp.maaIDs[i]);
        }else {
            panic("Invalid func unit type\n");
        }

        // call the read tile units to progress 
        indirectAccessUnits[tmp.maaIDs[i]].tilereadunitIdx->createAndSendTileExReads(10);
        indirectAccessUnits[tmp.maaIDs[i]].tilereadunitSrc->createAndSendTileExReads(10);
        aluUnits[tmp.maaIDs[i]].tilereadunitSrc1->createAndSendTileExReads(10);
        aluUnits[tmp.maaIDs[i]].tilereadunitSrc2->createAndSendTileExReads(10);
        rangeUnits[tmp.maaIDs[i]].tilereadunitMin->createAndSendTileExReads(10);
        rangeUnits[tmp.maaIDs[i]].tilereadunitMax->createAndSendTileExReads(10);
        streamAccessUnits[tmp.maaIDs[i]].tilereadunitSrc->createAndSendTileExReads(10);
    }

    
    
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