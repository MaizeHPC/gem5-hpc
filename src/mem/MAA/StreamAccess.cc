#include "mem/MAA/StreamAccess.hh"
#include "base/types.hh"
#include "mem/MAA/MAA.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/SPD.hh"
#include "base/trace.hh"
#include "debug/MAAStream.hh"
#include "sim/cur_tick.hh"
#include <cassert>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {

///////////////
// REQUEST TABLE
///////////////
RequestTable::RequestTable(StreamAccessUnit *_stream_access, unsigned int _num_addresses, unsigned int _num_entries_per_address, int _my_stream_id) {
    stream_access = _stream_access;
    num_addresses = _num_addresses;
    num_entries_per_address = _num_entries_per_address;
    my_stream_id = _my_stream_id;
    entries = new RequestTableEntry *[num_addresses];
    entries_valid = new bool *[num_addresses];
    for (int i = 0; i < num_addresses; i++) {
        entries[i] = new RequestTableEntry[num_entries_per_address];
        entries_valid[i] = new bool[num_entries_per_address];
        for (int j = 0; j < num_entries_per_address; j++) {
            entries_valid[i][j] = false;
        }
    }
    addresses = new Addr[num_addresses];
    addresses_valid = new bool[num_addresses];
    for (int i = 0; i < num_addresses; i++) {
        addresses_valid[i] = false;
    }
}
RequestTable::~RequestTable() {
    for (int i = 0; i < num_addresses; i++) {
        delete[] entries[i];
        delete[] entries_valid[i];
    }
    delete[] entries;
    delete[] entries_valid;
    delete[] addresses;
    delete[] addresses_valid;
}
std::vector<RequestTableEntry> RequestTable::get_entries(Addr base_addr) {
    std::vector<RequestTableEntry> result;
    for (int i = 0; i < num_addresses; i++) {
        if (addresses_valid[i] == true && addresses[i] == base_addr) {
            for (int j = 0; j < num_entries_per_address; j++) {
                if (entries_valid[i][j] == true) {
                    result.push_back(entries[i][j]);
                    entries_valid[i][j] = false;
                }
            }
            addresses_valid[i] = false;
            break;
        }
    }
    return result;
}
bool RequestTable::add_entry(int itr, Addr base_addr, uint16_t wid) {
    int address_itr = -1;
    int free_address_itr = -1;
    for (int i = 0; i < num_addresses; i++) {
        if (addresses_valid[i] == true) {
            if (addresses[i] == base_addr) {
                // Duplicate should not be allowed
                assert(address_itr == -1);
                address_itr = i;
            }
        } else if (free_address_itr == -1) {
            free_address_itr = i;
        }
    }
    if (address_itr == -1) {
        if (free_address_itr == -1) {
            return false;
        } else {
            addresses[free_address_itr] = base_addr;
            addresses_valid[free_address_itr] = true;
            address_itr = free_address_itr;
            (*stream_access->maa->stats.STR_NumCacheLineInserted[my_stream_id])++;
        }
    }
    int free_entry_itr = -1;
    for (int i = 0; i < num_entries_per_address; i++) {
        if (entries_valid[address_itr][i] == false) {
            free_entry_itr = i;
            break;
        }
    }
    assert(free_entry_itr != -1);
    entries[address_itr][free_entry_itr] = RequestTableEntry(itr, wid);
    entries_valid[address_itr][free_entry_itr] = true;
    (*stream_access->maa->stats.STR_NumWordsInserted[my_stream_id])++;
    return true;
}
void RequestTable::check_reset() {
    for (int i = 0; i < num_addresses; i++) {
        panic_if(addresses_valid[i], "Address %d is valid: 0x%lx!\n", i, addresses[i]);
        for (int j = 0; j < num_entries_per_address; j++) {
            panic_if(entries_valid[i][j], "Entry %d is valid: itr(%u) wid(%u)!\n", j, entries[i][j].itr, entries[i][j].wid);
        }
    }
}
void RequestTable::reset() {
    for (int i = 0; i < num_addresses; i++) {
        addresses_valid[i] = false;
        for (int j = 0; j < num_entries_per_address; j++) {
            entries_valid[i][j] = false;
        }
    }
}
bool RequestTable::is_full() {
    for (int i = 0; i < num_addresses; i++) {
        if (addresses_valid[i] == false) {
            return false;
        }
    }
    return true;
}

///////////////
//
// STREAM ACCESS UNIT
//
///////////////
StreamAccessUnit::StreamAccessUnit()
    : executeInstructionEvent([this] { executeInstruction(); }, name()),
      sendPacketEvent([this] { sendOutstandingReadPacket(); }, name()) {
    request_table = nullptr;
    my_instruction = nullptr;
}
void StreamAccessUnit::allocate(int _my_stream_id, unsigned int _num_request_table_addresses, unsigned int _num_request_table_entries_per_address, unsigned int _num_tile_elements, MAA *_maa) {
    my_stream_id = _my_stream_id;
    num_tile_elements = _num_tile_elements;
    num_request_table_addresses = _num_request_table_addresses;
    num_request_table_entries_per_address = _num_request_table_entries_per_address;
    state = Status::Idle;
    maa = _maa;
    dst_tile_id = -1;
    request_table = new RequestTable(this, num_request_table_addresses, num_request_table_entries_per_address, my_stream_id);
    my_translation_done = false;
    my_instruction = nullptr;
}
Cycles StreamAccessUnit::updateLatency(int num_spd_read_accesses,
                                       int num_spd_write_accesses,
                                       int num_requesttable_accesses) {
    if (num_spd_read_accesses != 0) {
        // 4Byte conditions -- 16 bytes per SPD access
        Cycles get_data_latency = maa->spd->getDataLatency(getCeiling(num_spd_read_accesses, 16));
        my_SPD_read_finish_tick = maa->getClockEdge(get_data_latency);
        (*maa->stats.STR_CyclesSPDReadAccess[my_stream_id]) += get_data_latency;
    }
    if (num_spd_write_accesses != 0) {
        // XByte -- 64/X bytes per SPD access
        Cycles set_data_latency = maa->spd->setDataLatency(my_dst_tile, getCeiling(num_spd_write_accesses, my_words_per_cl));
        my_SPD_write_finish_tick = maa->getClockEdge(set_data_latency);
        (*maa->stats.STR_CyclesSPDWriteAccess[my_stream_id]) += set_data_latency;
    }
    if (num_requesttable_accesses != 0) {
        Cycles access_requesttable_latency = Cycles(num_requesttable_accesses);
        if (my_RT_access_finish_tick < curTick())
            my_RT_access_finish_tick = maa->getClockEdge(access_requesttable_latency);
        else
            my_RT_access_finish_tick += maa->getCyclesToTicks(access_requesttable_latency);
        (*maa->stats.STR_CyclesRTAccess[my_stream_id]) += access_requesttable_latency;
    }
    Tick finish_tick = std::max(std::max(my_SPD_read_finish_tick, my_SPD_write_finish_tick), my_RT_access_finish_tick);
    return maa->getTicksToCycles(finish_tick - curTick());
}
bool StreamAccessUnit::scheduleNextExecution(bool force) {
    Tick finish_tick = std::max(std::max(my_SPD_read_finish_tick, my_SPD_write_finish_tick), my_RT_access_finish_tick);
    if (curTick() < finish_tick) {
        scheduleExecuteInstructionEvent(maa->getTicksToCycles(finish_tick - curTick()));
        return true;
    } else if (force) {
        scheduleExecuteInstructionEvent(Cycles(0));
        return true;
    }
    return false;
}
bool StreamAccessUnit::scheduleNextSend() {
    if (my_outstanding_read_pkts.size() > 0) {
        Cycles latency = Cycles(0);
        if (my_outstanding_read_pkts.begin()->tick > curTick()) {
            latency = maa->getTicksToCycles(my_outstanding_read_pkts.begin()->tick - curTick());
        }
        scheduleSendPacketEvent(latency);
        return true;
    } else if (my_outstanding_evict_pkts.size() > 0) {
        scheduleSendPacketEvent(Cycles(0));
        return true;
    }
    return false;
}
int StreamAccessUnit::getGBGAddr(int channel, int rank, int bankgroup) {
    return (channel * maa->m_org[ADDR_RANK_LEVEL] + rank) * maa->m_org[ADDR_BANKGROUP_LEVEL] + bankgroup;
}
StreamAccessUnit::PageInfo StreamAccessUnit::getPageInfo(int i, Addr base_addr, int word_size, int min, int stride) {
    Addr word_vaddr = base_addr + word_size * i;
    Addr block_vaddr = addrBlockAlign(word_vaddr, block_size);
    Addr block_paddr = translatePacket(block_vaddr);
    Addr word_paddr = block_paddr + (word_vaddr - block_vaddr);
    Addr page_paddr = addrBlockAlign(block_paddr, page_size);
    assert(word_paddr >= page_paddr);
    Addr diff_word_page_paddr = word_paddr - page_paddr;
    assert(diff_word_page_paddr % word_size == 0);
    int diff_word_page_words = diff_word_page_paddr / word_size;
    int min_itr = std::max(min, i - diff_word_page_words);
    // we use ceiling here to find the minimum idx in the page
    int min_idx = ((int)((min_itr - min - 1) / stride)) + 1;
    // We find the minimum itr based on the minimum idx which is stride aligned
    min_itr = min_idx * stride + min;
    std::vector<int> addr_vec = maa->map_addr(page_paddr);
    Addr gbg_addr = getGBGAddr(addr_vec[ADDR_CHANNEL_LEVEL], addr_vec[ADDR_RANK_LEVEL], addr_vec[ADDR_BANKGROUP_LEVEL]);
    DPRINTF(MAAStream, "S[%d] %s: word[%d] wordPaddr[0x%lx] blockPaddr[0x%lx] pagePaddr[0x%lx] minItr[%d] minIdx[%d] GBG[%d]\n", my_stream_id, __func__, i, word_paddr, block_paddr, page_paddr, min_itr, min_idx, gbg_addr);
    return StreamAccessUnit::PageInfo(min_itr, min_idx, gbg_addr);
}
void StreamAccessUnit::fillCurrentPageInfos() {
    for (auto it = my_all_page_info.begin(); it != my_all_page_info.end();) {
        if (std::find_if(my_current_page_info.begin(), my_current_page_info.end(), [it](const PageInfo &page) {
                return page.bg_addr == it->bg_addr;
            }) == my_current_page_info.end()) {
            my_current_page_info.push_back(*it);
            DPRINTF(MAAStream, "S[%d] %s: %s added to current page info!\n", my_stream_id, __func__, it->print());
            it = my_all_page_info.erase(it);
        } else {
            ++it;
        }
    }
}
void StreamAccessUnit::executeInstruction() {
    switch (state) {
    case Status::Idle: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAStream, "S[%d] %s: idling %s!\n", my_stream_id, __func__, my_instruction->print());
        state = Status::Decode;
        [[fallthrough]];
    }
    case Status::Decode: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAStream, "S[%d] %s: decoding %s!\n", my_stream_id, __func__, my_instruction->print());

        // Decoding the instruction
        my_base_addr = my_instruction->baseAddr;
        my_dst_tile = my_instruction->dst1SpdID;
        my_cond_tile = my_instruction->condSpdID;
        my_min = maa->rf->getData<int>(my_instruction->src1RegID);
        my_max = maa->rf->getData<int>(my_instruction->src2RegID);
        my_stride = maa->rf->getData<int>(my_instruction->src3RegID);
        my_size = (my_max == my_min) ? 0 : std::min((int)(maa->num_tile_elements), ((int)((my_max - my_min - 1) / my_stride)) + 1);
        DPRINTF(MAAStream, "S[%d] %s: min: %d, max: %d, stride: %d, size: %d!\n", my_stream_id, __func__, my_min, my_max, my_stride, my_size);
        my_word_size = my_instruction->getWordSize(my_dst_tile);
        my_words_per_cl = block_size / my_word_size;
        my_words_per_page = page_size / my_word_size;
        (*maa->stats.STR_NumInsts[my_stream_id])++;
        maa->stats.numInst_STRRD++;
        maa->stats.numInst++;
        for (int i = my_min; i < my_max; i += my_words_per_page) {
            StreamAccessUnit::PageInfo page_info = getPageInfo(i, my_base_addr, my_word_size, my_min, my_stride);
            if (page_info.curr_idx >= maa->num_tile_elements) {
                DPRINTF(MAAStream, "S[%d] %s: page %s is out of bounds, breaking...!\n", my_stream_id, __func__, page_info.print());
                break;
            } else {
                my_all_page_info.push_back(page_info);
            }
        }
        for (int i = 0; i < my_all_page_info.size() - 1; i++) {
            my_all_page_info[i].max_itr = my_all_page_info[i + 1].curr_itr;
        }
        my_all_page_info[my_all_page_info.size() - 1].max_itr = my_max;

        // Initialization
        my_received_responses = 0;
        my_sent_requests = 0;
        request_table->reset();
        my_SPD_read_finish_tick = curTick();
        my_SPD_write_finish_tick = curTick();
        my_RT_access_finish_tick = curTick();
        my_decode_start_tick = curTick();
        my_request_start_tick = 0;
        assert(my_outstanding_read_pkts.size() == 0);
        assert(my_outstanding_evict_pkts.size() == 0);

        // Setting the state of the instruction and stream unit
        my_instruction->state = Instruction::Status::Service;
        state = Status::Request;
        scheduleExecuteInstructionEvent(Cycles(my_all_page_info.size() * 2));
        break;
    }
    case Status::Request: {
        DPRINTF(MAAStream, "S[%d] %s: requesting %s!\n", my_stream_id, __func__, my_instruction->print());
        if (scheduleNextExecution() || request_table->is_full()) {
            break;
        }
        if (my_request_start_tick == 0) {
            my_request_start_tick = curTick();
        }
        fillCurrentPageInfos();
        int num_spd_read_accesses = 0;
        int num_request_table_cacheline_accesses = 0;
        bool broken = false;
        bool *channel_sent = new bool[maa->m_org[ADDR_CHANNEL_LEVEL]];
        while (my_current_page_info.empty() == false && request_table->is_full() == false) {
            for (auto page_it = my_current_page_info.begin(); page_it != my_current_page_info.end() && request_table->is_full() == false;) {
                DPRINTF(MAAStream, "S[%d] %s: operating on page %s!\n", my_stream_id, __func__, page_it->print());
                std::fill(channel_sent, channel_sent + maa->m_org[ADDR_CHANNEL_LEVEL], false);
                for (; page_it->curr_itr < page_it->max_itr && page_it->curr_idx < maa->num_tile_elements; page_it->curr_itr += my_stride, page_it->curr_idx++) {
                    if (my_cond_tile != -1) {
                        if (maa->spd->getElementFinished(my_cond_tile, page_it->curr_idx, 4, (uint8_t)FuncUnitType::STREAM, my_stream_id) == false) {
                            DPRINTF(MAAStream, "%s: cond tile[%d] element[%d] not ready, moving page %s to all!\n", __func__, my_cond_tile, page_it->curr_idx, page_it->print());
                            my_all_page_info.push_back(*page_it);
                            page_it = my_current_page_info.erase(page_it);
                            broken = true;
                            break;
                        }
                        num_spd_read_accesses++;
                    }
                    if (my_cond_tile == -1 || maa->spd->getData<uint32_t>(my_cond_tile, page_it->curr_idx) != 0) {
                        Addr vaddr = my_base_addr + my_word_size * page_it->curr_itr;
                        Addr block_vaddr = addrBlockAlign(vaddr, block_size);
                        if (block_vaddr != page_it->last_block_vaddr) {
                            if (page_it->last_block_vaddr != 0) {
                                Addr paddr = translatePacket(page_it->last_block_vaddr);
                                std::vector<int> addr_vec = maa->map_addr(paddr);
                                if (channel_sent[addr_vec[ADDR_CHANNEL_LEVEL]] == false) {
                                    my_sent_requests++;
                                    num_request_table_cacheline_accesses++;
                                    createReadPacket(paddr, num_request_table_cacheline_accesses);
                                    channel_sent[addr_vec[ADDR_CHANNEL_LEVEL]] = true;
                                } else {
                                    page_it++;
                                    broken = true;
                                    break;
                                }
                            }
                            page_it->last_block_vaddr = block_vaddr;
                        }
                        Addr paddr = translatePacket(block_vaddr);
                        uint16_t word_id = (vaddr - block_vaddr) / my_word_size;
                        if (request_table->add_entry(page_it->curr_idx, paddr, word_id) == false) {
                            DPRINTF(MAAStream, "S[%d] RequestTable: entry %d not added! vaddr=0x%lx, paddr=0x%lx wid = %d\n", my_stream_id, page_it->curr_idx, block_vaddr, paddr, word_id);
                            updateLatency(num_spd_read_accesses, 0, num_request_table_cacheline_accesses);
                            (*maa->stats.STR_NumRTFull[my_stream_id])++;
                            page_it++;
                            broken = true;
                            break;
                        } else {
                            DPRINTF(MAAStream, "S[%d] RequestTable: entry %d added! vaddr=0x%lx, paddr=0x%lx wid = %d\n",
                                    my_stream_id, page_it->curr_idx, block_vaddr, paddr, word_id);
                        }
                    } else {
                        DPRINTF(MAAStream, "S[%d] %s: SPD[%d][%d] = %u (cond not taken)\n", my_stream_id, __func__, my_dst_tile, page_it->curr_idx, 0);
                        switch (my_instruction->datatype) {
                        case Instruction::DataType::UINT32_TYPE: {
                            maa->spd->setData<uint32_t>(my_dst_tile, page_it->curr_idx, 0);
                            break;
                        }
                        case Instruction::DataType::INT32_TYPE: {
                            maa->spd->setData<int32_t>(my_dst_tile, page_it->curr_idx, 0);
                            break;
                        }
                        case Instruction::DataType::FLOAT32_TYPE: {
                            maa->spd->setData<float>(my_dst_tile, page_it->curr_idx, 0);
                            break;
                        }
                        case Instruction::DataType::UINT64_TYPE: {
                            maa->spd->setData<uint64_t>(my_dst_tile, page_it->curr_idx, 0);
                            break;
                        }
                        case Instruction::DataType::INT64_TYPE: {
                            maa->spd->setData<int64_t>(my_dst_tile, page_it->curr_idx, 0);
                            break;
                        }
                        case Instruction::DataType::FLOAT64_TYPE: {
                            maa->spd->setData<double>(my_dst_tile, page_it->curr_idx, 0);
                            break;
                        }
                        default:
                            assert(false);
                        }
                    }
                }
                if (broken == false && page_it->last_block_vaddr != 0) {
                    my_sent_requests++;
                    Addr paddr = translatePacket(page_it->last_block_vaddr);
                    createReadPacket(paddr, num_request_table_cacheline_accesses);
                    DPRINTF(MAAStream, "S[%d] %s: page %s done, removing!\n", my_stream_id, __func__, page_it->print());
                    page_it = my_current_page_info.erase(page_it);
                }
            }
        }

        delete[] channel_sent;
        updateLatency(num_spd_read_accesses, 0, num_request_table_cacheline_accesses);
        if (request_table->is_full()) {
            scheduleNextExecution();
        }
        scheduleNextSend();
        if (my_received_responses != my_sent_requests) {
            DPRINTF(MAAStream, "S[%d] %s: Waiting for responses, received (%d) != send (%d)...\n", my_stream_id, __func__, my_received_responses, my_sent_requests);
        } else {
            DPRINTF(MAAStream, "S[%d] %s: state set to respond for request %s!\n", my_stream_id, __func__, my_instruction->print());
            state = Status::Response;
            scheduleNextExecution(true);
        }
        break;
    }
    case Status::Response: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAStream, "S[%d] %s: responding %s!\n", my_stream_id, __func__, my_instruction->print());
        panic_if(scheduleNextExecution(), "S[%d] %s: Execution is not completed!\n", my_stream_id, __func__);
        panic_if(scheduleNextSend(), "S[%d] %s: Sending is not completed!\n", my_stream_id, __func__);
        panic_if(my_received_responses != my_sent_requests, "S[%d] %s: received_responses(%d) != sent_requests(%d)!\n",
                 my_stream_id, __func__, my_received_responses, my_sent_requests);
        DPRINTF(MAAStream, "S[%d] %s: state set to finish for request %s!\n", my_stream_id, __func__, my_instruction->print());
        my_instruction->state = Instruction::Status::Finish;
        if (my_request_start_tick != 0) {
            (*maa->stats.STR_CyclesRequest[my_stream_id]) += maa->getTicksToCycles(curTick() - my_request_start_tick);
            my_request_start_tick = 0;
        }
        Cycles total_cycles = maa->getTicksToCycles(curTick() - my_decode_start_tick);
        maa->stats.cycles += total_cycles;
        maa->stats.cycles_STRRD += total_cycles;
        my_decode_start_tick = 0;
        state = Status::Idle;
        maa->spd->setSize(my_dst_tile, my_size);
        maa->finishInstructionCompute(my_instruction);
        my_instruction = nullptr;
        request_table->check_reset();
        break;
    }
    default:
        assert(false);
    }
}
void StreamAccessUnit::createReadPacket(Addr addr, int latency) {
    /**** Packet generation ****/
    RequestPtr real_req = std::make_shared<Request>(addr, block_size, flags, maa->requestorId);
    PacketPtr my_pkt = new Packet(real_req, MemCmd::ReadSharedReq);
    my_pkt->allocate();
    my_outstanding_read_pkts.insert(StreamAccessUnit::StreamPacket(my_pkt, maa->getClockEdge(Cycles(latency))));
    DPRINTF(MAAStream, "S[%d] %s: created %s to send in %d cycles\n", my_stream_id, __func__, my_pkt->print(), latency);
    (*maa->stats.STR_LoadsCacheAccessing[my_stream_id])++;
}
void StreamAccessUnit::createReadPacketEvict(Addr addr) {
    /**** Packet generation ****/
    RequestPtr real_req = std::make_shared<Request>(addr, block_size, flags, maa->requestorId);
    PacketPtr my_pkt = new Packet(real_req, MemCmd::CleanEvict);
    my_outstanding_evict_pkts.insert(StreamAccessUnit::StreamPacket(my_pkt, maa->getClockEdge(Cycles(0))));
    DPRINTF(MAAStream, "S[%d] %s: created %s to send\n", my_stream_id, __func__, my_pkt->print());
    (*maa->stats.STR_Evicts[my_stream_id])++;
}
bool StreamAccessUnit::sendOutstandingReadPacket() {
    bool read_packet_blocked = false;
    bool read_packet_remaining = false;
    bool evict_packet_sent = false;

    DPRINTF(MAAStream, "S[%d] %s: sending %d outstanding read packets...\n", my_stream_id, __func__, my_outstanding_read_pkts.size());
    while (my_outstanding_read_pkts.empty() == false) {
        StreamAccessUnit::StreamPacket read_pkt = *my_outstanding_read_pkts.begin();
        DPRINTF(MAAStream, "S[%d] %s: trying sending %s to cache at time %u\n", my_stream_id, __func__, read_pkt.packet->print(), read_pkt.tick);
        if (read_pkt.tick > curTick()) {
            DPRINTF(MAAStream, "S[%d] %s: waiting for %d cycles\n", my_stream_id, __func__, maa->getTicksToCycles(read_pkt.tick - curTick()));
            read_packet_remaining = true;
            break;
        }
        if (maa->sendPacketCache((uint8_t)FuncUnitType::STREAM, my_stream_id, read_pkt.packet) == false) {
            DPRINTF(MAAStream, "S[%d] %s: send failed, leaving send packet...\n", my_stream_id, __func__);
            read_packet_blocked = true;
            break;
        } else {
            my_outstanding_read_pkts.erase(my_outstanding_read_pkts.begin());
        }
    }

    DPRINTF(MAAStream, "S[%d] %s: sending %d outstanding evict packets...\n", my_stream_id, __func__, my_outstanding_evict_pkts.size());
    while (my_outstanding_evict_pkts.empty() == false && read_packet_blocked == false) {
        StreamAccessUnit::StreamPacket evict_pkt = *my_outstanding_evict_pkts.begin();
        DPRINTF(MAAStream, "S[%d] %s: trying sending %s to cache at time %u\n", my_stream_id, __func__, evict_pkt.packet->print(), evict_pkt.tick);
        panic_if(evict_pkt.tick > curTick(), "S[%d] %s: waiting for %d cycles\n", my_stream_id, __func__, maa->getTicksToCycles(evict_pkt.tick - curTick()));
        if (maa->sendPacketCache((uint8_t)FuncUnitType::STREAM, my_stream_id, evict_pkt.packet) == false) {
            DPRINTF(MAAStream, "S[%d] %s: send failed, leaving send packet...\n", my_stream_id, __func__);
            break;
        } else {
            my_outstanding_evict_pkts.erase(my_outstanding_evict_pkts.begin());
            evict_packet_sent = true;
        }
    }

    if (read_packet_remaining) {
        scheduleNextSend();
    }
    if (evict_packet_sent) {
        if (my_received_responses == my_sent_requests) {
            DPRINTF(MAAStream, "S[%d] %s: all responses received, calling execution again in state %s!\n", my_stream_id, __func__, status_names[(int)state]);
            scheduleNextExecution(true);
        } else {
            DPRINTF(MAAStream, "S[%d] %s: expected: %d, received: %d!\n", my_stream_id, __func__, my_received_responses, my_received_responses);
        }
    }
    return true;
}
bool StreamAccessUnit::recvData(const Addr addr, uint8_t *dataptr) {
    bool was_request_table_full = request_table->is_full();
    std::vector<RequestTableEntry> entries = request_table->get_entries(addr);
    if (entries.empty()) {
        DPRINTF(MAAStream, "S[%d] %s: no entries found for addr(0x%lx)\n", my_stream_id, __func__, addr);
        return false;
    }
    DPRINTF(MAAStream, "S[%d] %s: %d entry found for addr(0x%lx)\n", my_stream_id, __func__, entries.size(), addr);
    uint32_t *dataptr_u32_typed = (uint32_t *)dataptr;
    uint64_t *dataptr_u64_typed = (uint64_t *)dataptr;
    for (auto entry : entries) {
        int itr = entry.itr;
        int wid = entry.wid;
        if (my_word_size == 4) {
            DPRINTF(MAAStream, "S[%d] %s: SPD[%d][%d] = %u\n", my_stream_id, __func__, my_dst_tile, itr, dataptr_u32_typed[wid]);
            maa->spd->setData<uint32_t>(my_dst_tile, itr, dataptr_u32_typed[wid]);
        } else {
            DPRINTF(MAAStream, "S[%d] %s: SPD[%d][%d] = %lu\n", my_stream_id, __func__, my_dst_tile, itr, dataptr_u64_typed[wid]);
            maa->spd->setData<uint64_t>(my_dst_tile, itr, dataptr_u64_typed[wid]);
        }
    }
    my_received_responses++;

    updateLatency(0, entries.size(), 1);
    createReadPacketEvict(addr);
    scheduleNextSend();
    if (was_request_table_full) {
        scheduleNextExecution(true);
    }
    return true;
}
Addr StreamAccessUnit::translatePacket(Addr vaddr) {
    /**** Address translation ****/
    RequestPtr translation_req = std::make_shared<Request>(vaddr,
                                                           block_size,
                                                           flags,
                                                           maa->requestorId,
                                                           my_instruction->PC,
                                                           my_instruction->CID);
    ThreadContext *tc = maa->system->threads[my_instruction->CID];
    maa->mmu->translateTiming(translation_req, tc, this, BaseMMU::Read);
    // The above function immediately does the translation and calls the finish function
    assert(my_translation_done);
    my_translation_done = false;
    return my_translated_addr;
}
void StreamAccessUnit::finish(const Fault &fault,
                              const RequestPtr &req, ThreadContext *tc, BaseMMU::Mode mode) {
    assert(fault == NoFault);
    assert(my_translation_done == false);
    my_translation_done = true;
    my_translated_addr = req->getPaddr();
}
void StreamAccessUnit::setInstruction(Instruction *_instruction) {
    assert(my_instruction == nullptr);
    my_instruction = _instruction;
}
void StreamAccessUnit::scheduleExecuteInstructionEvent(int latency) {
    DPRINTF(MAAStream, "S[%d] %s: scheduling execute for the Stream Unit in the next %d cycles!\n", my_stream_id, __func__, latency);
    panic_if(latency < 0, "Negative latency of %d!\n", latency);
    Tick new_when = maa->getClockEdge(Cycles(latency));
    if (!executeInstructionEvent.scheduled()) {
        maa->schedule(executeInstructionEvent, new_when);
    } else {
        Tick old_when = executeInstructionEvent.when();
        DPRINTF(MAAStream, "S[%d] %s: execution already scheduled for tick %d\n", my_stream_id, __func__, old_when);
        if (new_when < old_when) {
            DPRINTF(MAAStream, "S[%d] %s: rescheduling for tick %d!\n", my_stream_id, __func__, new_when);
            maa->reschedule(executeInstructionEvent, new_when);
        }
    }
}
void StreamAccessUnit::scheduleSendPacketEvent(int latency) {
    DPRINTF(MAAStream, "S[%d] %s: scheduling send packet for the Stream Unit in the next %d cycles!\n", my_stream_id, __func__, latency);
    panic_if(latency < 0, "Negative latency of %d!\n", latency);
    Tick new_when = maa->getClockEdge(Cycles(latency));
    if (!sendPacketEvent.scheduled()) {
        maa->schedule(sendPacketEvent, new_when);
    } else {
        Tick old_when = sendPacketEvent.when();
        DPRINTF(MAAStream, "S[%d] %s: send packet already scheduled for tick %d\n", my_stream_id, __func__, old_when);
        if (new_when < old_when) {
            DPRINTF(MAAStream, "S[%d] %s: rescheduling for tick %d!\n", my_stream_id, __func__, new_when);
            maa->reschedule(sendPacketEvent, new_when);
        }
    }
}
} // namespace gem5