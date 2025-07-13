#include "mem/MAA/StreamAccess.hh"
#include "base/types.hh"
#include "mem/MAA/MAA.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/SPD.hh"
#include "base/trace.hh"
#include "debug/MAAStream.hh"
#include "debug/MAATrace.hh"
#include "sim/cur_tick.hh"
#include <cassert>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {

///////////////
//
// STREAM ACCESS UNIT
//
///////////////
StreamAccessUnit::StreamAccessUnit()
    : executeInstructionEvent([this] { executeInstruction(); }, name()) {
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
    request_table = new RequestTable(maa, num_request_table_addresses, num_request_table_entries_per_address, my_stream_id, true);
    my_translation_done = false;
    my_instruction = nullptr;

    tilewriteunit = new TileWrite(maa, TW_sent_requests, TW_received_responses, my_size, maa->spd->tile_write_counts, FuncUnitType::STREAM);
}
Cycles StreamAccessUnit::updateLatency(int num_spd_condread_accesses, int num_spd_srcread_accesses, int num_spd_write_accesses, int num_requesttable_accesses) {
    if (num_spd_condread_accesses != 0) {
        // 4Byte conditions -- 16 bytes per SPD access
        Cycles get_data_latency = maa->spd->getDataLatency(getCeiling(num_spd_condread_accesses, 16));
        my_SPD_read_finish_tick = maa->getClockEdge(get_data_latency);
        if (num_spd_srcread_accesses == 0) {
            (*maa->stats.STR_CyclesSPDReadAccess[my_stream_id]) += get_data_latency;
        }
    }
    if (num_spd_srcread_accesses != 0) {
        // XByte -- 64/X bytes per SPD access
        Cycles get_data_latency = maa->spd->getDataLatency(getCeiling(num_spd_srcread_accesses, my_words_per_cl));
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
    Tick finish_tick = my_RT_access_finish_tick;
    if (state == Status::Response) {
        finish_tick = std::max(std::max(my_SPD_read_finish_tick, my_SPD_write_finish_tick), finish_tick);
    }
    if (curTick() < finish_tick) {
        scheduleExecuteInstructionEvent(maa->getTicksToCycles(finish_tick - curTick()));
        return true;
    } else if (force) {
        scheduleExecuteInstructionEvent(Cycles(0));
        return true;
    }
    return false;
}
int StreamAccessUnit::getGBGAddr(int channel, int rank, int bankgroup) {
    return (channel * maa->m_org[ADDR_RANK_LEVEL] + rank) * maa->m_org[ADDR_BANKGROUP_LEVEL] + bankgroup;
}
StreamAccessUnit::PageInfo StreamAccessUnit::getPageInfo(int i, Addr base_addr, int word_size, int min, int stride) {
    Addr word_vaddr = base_addr + word_size * i;
    Addr block_vaddr = addrBlockAligner(word_vaddr, block_size);
    Addr block_paddr = translatePacket(block_vaddr);
    Addr word_paddr = block_paddr + (word_vaddr - block_vaddr);
    Addr page_paddr = addrBlockAligner(block_paddr, page_size);
    assert(word_paddr >= page_paddr);
    Addr diff_word_page_paddr = word_paddr - page_paddr;
    assert(diff_word_page_paddr % word_size == 0);
    int diff_word_page_words = diff_word_page_paddr / word_size;
    int min_itr = std::max(min, i - diff_word_page_words);
    // we use ceiling here to find the minimum idx in the page
    int min_idx = min_itr == min ? 0 : ((int)((min_itr - min - 1) / stride)) + 1;
    // We find the minimum itr based on the minimum idx which is stride aligned
    min_itr = min_idx * stride + min;
    std::vector<int> addr_vec = maa->map_addr(page_paddr);
    Addr gbg_addr = getGBGAddr(addr_vec[ADDR_CHANNEL_LEVEL], addr_vec[ADDR_RANK_LEVEL], addr_vec[ADDR_BANKGROUP_LEVEL]);
    DPRINTF(MAAStream, "S[%d] %s: word[%d] wordPaddr[0x%lx] blockPaddr[0x%lx] pagePaddr[0x%lx] minItr[%d] minIdx[%d] GBG[%d]\n", my_stream_id, __func__, i, word_paddr, block_paddr, page_paddr, min_itr, min_idx, gbg_addr);
    return StreamAccessUnit::PageInfo(min_itr, min_idx, gbg_addr);
}
bool StreamAccessUnit::fillCurrentPageInfos() {
    bool inserted = false;
    for (auto it = my_all_page_info.begin(); it != my_all_page_info.end();) {
        if (std::find_if(my_current_page_info.begin(), my_current_page_info.end(), [it](const PageInfo &page) {
                return page.bg_addr == it->bg_addr;
            }) == my_current_page_info.end()) {
            my_current_page_info.push_back(*it);
            DPRINTF(MAAStream, "S[%d] %s: %s added to current page info!\n", my_stream_id, __func__, it->print());
            it = my_all_page_info.erase(it);
            inserted = true;
        } else {
            ++it;
        }
    }
    return inserted;
}
void StreamAccessUnit::executeInstruction() {
    switch (state) {
    case Status::Idle: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAStream, "S[%d] %s: idling %s!\n", my_stream_id, __func__, my_instruction->print());
        DPRINTF(MAATrace, "S[%d] Start [%s]\n", my_stream_id, my_instruction->print());
        state = Status::Decode;
        [[fallthrough]];
    }
    case Status::Decode: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAStream, "S[%d] %s: decoding %s!\n", my_stream_id, __func__, my_instruction->print());

        // Decoding the instruction
        my_base_addr = my_instruction->baseAddr;
        my_dst_tile = my_instruction->dst1SpdID;
        my_src_tile = my_instruction->src1SpdID;
        my_cond_tile = my_instruction->condSpdID;
        my_min = maa->rf->getData<int>(my_instruction->src1RegID);
        my_max = maa->rf->getData<int>(my_instruction->src2RegID);
        my_stride = maa->rf->getData<int>(my_instruction->src3RegID);

        // if(my_dst_tile != -1){
        //     maa->spd->SPDQueues[my_dst_tile] = {};
        // }

        

        my_size = (my_max == my_min) ? 0 : std::min((int)(maa->num_tile_elements), ((int)((my_max - my_min - 1) / my_stride)) + 1);

        for(int i = 0; i < my_size; i++){
            sentmyIQueue.push(i);
        }


        DPRINTF(MAAStream, "S[%d] %s: min: %d, max: %d, stride: %d, size: %d!\n", my_stream_id, __func__, my_min, my_max, my_stride, my_size);
        if (my_instruction->opcode == Instruction::OpcodeType::STREAM_LD) {
            my_word_size = my_instruction->getWordSize(my_dst_tile);
        } else if (my_instruction->opcode == Instruction::OpcodeType::STREAM_ST) {
            my_word_size = my_instruction->getWordSize(my_src_tile);
        } else {
            assert(false);
        }
        my_words_per_cl = block_size / my_word_size;
        my_words_per_page = page_size / my_word_size;
        (*maa->stats.STR_NumInsts[my_stream_id])++;
        if (my_instruction->opcode == Instruction::OpcodeType::STREAM_LD) {
            my_is_load = true;
            maa->stats.numInst_STRRD++;
        } else if (my_instruction->opcode == Instruction::OpcodeType::STREAM_ST) {
            my_is_load = false;
            maa->stats.numInst_STRWR++;
        } else {
            assert(false);
        }
        maa->stats.numInst++;
        std::vector<PageInfo> all_page_info;
        for (int i = my_min; i < my_max; i += my_words_per_page) {
            StreamAccessUnit::PageInfo page_info = getPageInfo(i, my_base_addr, my_word_size, my_min, my_stride);
            if (page_info.curr_idx >= maa->num_tile_elements) {
                DPRINTF(MAAStream, "S[%d] %s: page %s is out of bounds, breaking...!\n", my_stream_id, __func__, page_info.print());
                break;
            } else {
                all_page_info.push_back(page_info);
            }
        }
        for (int i = 0; i < all_page_info.size() - 1; i++) {
            all_page_info[i].max_itr = all_page_info[i + 1].curr_itr;
            my_all_page_info.insert(all_page_info[i]);
        }
        all_page_info[all_page_info.size() - 1].max_itr = my_max;
        my_all_page_info.insert(all_page_info[all_page_info.size() - 1]);
        my_min_addr = my_instruction->minAddr;
        my_max_addr = my_instruction->maxAddr;
        my_addr_range_id = my_instruction->addrRangeID;

        // Initialization
        my_received_responses = 0;
        my_sent_requests = 0;
        TW_received_responses = 0;
        TW_sent_requests = 0;
        request_table->reset();
        my_SPD_read_finish_tick = curTick();
        my_SPD_write_finish_tick = curTick();
        my_RT_access_finish_tick = curTick();
        my_decode_start_tick = curTick();
        my_request_start_tick = 0;

        // Setting the state of the instruction and stream unit
        my_instruction->state = Instruction::Status::Service;
        state = Status::Request;
        scheduleExecuteInstructionEvent(Cycles(my_all_page_info.size() * 2));

        if(my_dst_tile != -1){
            tilewriteunit->set(my_dst_tile, my_word_size, my_instruction->CID, my_instruction->PC, block_size);
            int num_initial_reqs = 100;
            tilewriteunit->createAndSendTileExReads(num_initial_reqs);
        }

        my_received_itr_max = -1;
        my_set_fake_max = -1;
        my_itr_max = -1;
        my_itr_max_final = -1;


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
        int num_spd_condread_accesses = 0;
        int num_request_table_cacheline_accesses = 0;
        bool broken = false;
        bool any_broken = false;
        bool *channel_sent = new bool[maa->m_org[ADDR_CHANNEL_LEVEL]];
        while (my_current_page_info.empty() == false && request_table->is_full() == false) {
            for (auto page_it = my_current_page_info.begin(); page_it != my_current_page_info.end() && request_table->is_full() == false;) {
                DPRINTF(MAAStream, "S[%d] %s: operating on page %s!\n", my_stream_id, __func__, page_it->print());
                std::fill(channel_sent, channel_sent + maa->m_org[ADDR_CHANNEL_LEVEL], false);
                broken = false;
                for (; page_it->curr_itr < page_it->max_itr && page_it->curr_idx < maa->num_tile_elements; page_it->curr_itr += my_stride, page_it->curr_idx++) {
                    if (my_cond_tile != -1) {
                        if (maa->spd->getElementFinished(my_cond_tile, page_it->curr_idx, 4, (uint8_t)FuncUnitType::STREAM, my_stream_id) == false) {
                            DPRINTF(MAAStream, "%s: cond tile[%d] element[%d] not ready, moving page %s to all!\n", __func__, my_cond_tile, page_it->curr_idx, page_it->print());
                            my_all_page_info.insert(*page_it);
                            page_it = my_current_page_info.erase(page_it);
                            broken = true;
                            any_broken = true;
                            break;
                        }
                        num_spd_condread_accesses++;
                    }
                    if (my_src_tile != -1) {
                        if (maa->spd->getElementFinished(my_src_tile, page_it->curr_idx, my_word_size, (uint8_t)FuncUnitType::STREAM, my_stream_id) == false) {
                            DPRINTF(MAAStream, "%s: src tile[%d] element[%d] not ready, moving page %s to all!\n", __func__, my_src_tile, page_it->curr_idx, page_it->print());
                            my_all_page_info.insert(*page_it);
                            page_it = my_current_page_info.erase(page_it);
                            broken = true;
                            any_broken = true;
                            break;
                        }
                    }
                    if (my_cond_tile == -1 || maa->spd->getData<uint32_t>(my_cond_tile, page_it->curr_idx) != 0) {
                        Addr vaddr = my_base_addr + my_word_size * page_it->curr_itr;
                        panic_if(vaddr < my_min_addr || vaddr >= my_max_addr, "S[%d] %s: vaddr 0x%lx out of range [0x%lx, 0x%lx)!\n", my_stream_id, __func__, vaddr, my_min_addr, my_max_addr);
                        Addr block_vaddr = addrBlockAligner(vaddr, block_size);
                        if (block_vaddr != page_it->last_block_vaddr) {
                            if (page_it->last_block_vaddr != 0) {
                                Addr paddr = translatePacket(page_it->last_block_vaddr);
                                std::vector<int> addr_vec = maa->map_addr(paddr);
                                panic_if(channel_sent[addr_vec[ADDR_CHANNEL_LEVEL]], "S[%d] %s: channel %d already sent for page %s!\n", my_stream_id, __func__, addr_vec[ADDR_CHANNEL_LEVEL], page_it->print());
                                my_sent_requests++;
                                num_request_table_cacheline_accesses++;
                                createReadPacket(paddr, num_request_table_cacheline_accesses);
                                channel_sent[addr_vec[ADDR_CHANNEL_LEVEL]] = true;
                            }
                            page_it->last_block_vaddr = block_vaddr;
                        }
                        Addr paddr = translatePacket(block_vaddr);
                        std::vector<int> addr_vec = maa->map_addr(paddr);
                        if (channel_sent[addr_vec[ADDR_CHANNEL_LEVEL]]) {
                            DPRINTF(MAAStream, "S[%d] RequestTable: entry %d not added because channel already pushed! paddr=0x%lx\n", my_stream_id, page_it->curr_idx, paddr);
                            page_it++;
                            broken = true;
                            any_broken = true;
                            break;
                        }
                        uint16_t word_id = (vaddr - block_vaddr) / my_word_size;
                        if (request_table->add_entry(page_it->curr_idx, paddr, word_id) == false) {
                            DPRINTF(MAAStream, "S[%d] RequestTable: entry %d not added because request table is full! vaddr=0x%lx, paddr=0x%lx wid = %d\n", my_stream_id, page_it->curr_idx, block_vaddr, paddr, word_id);
                            (*maa->stats.STR_NumRTFull[my_stream_id])++;
                            page_it++;
                            broken = true;
                            any_broken = true;
                            break;
                        } else {
                            my_itr_max = std::max(my_itr_max, page_it->curr_idx);
                            DPRINTF(MAAStream, "S[%d] RequestTable: entry %d added! vaddr=0x%lx, paddr=0x%lx wid = %d\n",
                                    my_stream_id, page_it->curr_idx, block_vaddr, paddr, word_id);

                            // to keep the ids in order
                            


                        }
                    } else if (my_instruction->opcode == Instruction::OpcodeType::STREAM_LD) {
                        DPRINTF(MAAStream, "S[%d] %s: SPD[%d][%d] = %u (cond not taken)\n", my_stream_id, __func__, my_dst_tile, page_it->curr_idx, 0);
                        maa->spd->setFakeData(my_dst_tile, page_it->curr_idx, my_word_size);
                        if(my_word_size == 4){
                            tilewriteunit->setdata<uint32_t>(0, page_it->curr_idx);
                        } else if(my_word_size == 8){
                            tilewriteunit->setdata<uint64_t>(0, page_it->curr_idx);
                        }
                        my_set_fake_max = std::max(my_set_fake_max, page_it->curr_idx);
                    }
                }
                if (broken == false) {
                    if (page_it->last_block_vaddr != 0) {
                        my_sent_requests++;
                        Addr paddr = translatePacket(page_it->last_block_vaddr);
                        createReadPacket(paddr, num_request_table_cacheline_accesses);
                    }
                    DPRINTF(MAAStream, "S[%d] %s: page %s done, removing!\n", my_stream_id, __func__, page_it->print());
                    page_it = my_current_page_info.erase(page_it);
                    bool was_last_page = page_it == my_current_page_info.end();
                    // replacing with a new page and updating the iterator
                    if (fillCurrentPageInfos() && was_last_page) {
                        page_it = my_current_page_info.begin();
                    }
                }
            }
        }

        if(!any_broken && !request_table->is_full()){
            DPRINTF(MAAStream, "S[%d] %s: my_current_page_info is empty", my_stream_id, __func__);
            my_itr_max_final = std::max(my_set_fake_max, my_itr_max);
            tilewriteunit->set_max_element(my_itr_max_final+1); // adding one to get max count
            // if(((my_itr_max_final ==  my_received_itr_max) || (my_itr_max_final == my_set_fake_max)) && !tilewriteunit->is_last_element_reached()){
            //     tilewriteunit->mark_last_element_reached();
            // }
        }

        delete[] channel_sent;
        // assume parallelism = #Channels
        updateLatency(num_spd_condread_accesses, 0, 0, num_request_table_cacheline_accesses);
        if (request_table->is_full()) {
            scheduleNextExecution();
        }

        bool tileWriteUnitCheck;
        if(dst_tile_id == -1){
            tileWriteUnitCheck = true;
        } else {
            tileWriteUnitCheck = tilewriteunit->check_all_responses_received();
        }

        if ((my_received_responses != my_sent_requests) || !tileWriteUnitCheck || !maa->allStreamPacketsSent(my_stream_id)) {
            DPRINTF(MAAStream, "S[%d] %s: Waiting for responses, received (%d) != send (%d)...\n", my_stream_id, __func__, get_all_received(), get_all_sent());
        } else {
            if (my_cond_tile != -1 && maa->spd->getTileStatus(my_cond_tile,  (uint8_t)FuncUnitType::STREAM, my_stream_id) != SPD::TileStatus::Finished) {
                DPRINTF(MAAStream, "S[%d] %s: Waiting for cond tile %d to finish...\n", my_stream_id, __func__, my_cond_tile);
            } else if (my_src_tile != -1 && maa->spd->getTileStatus(my_src_tile,  (uint8_t)FuncUnitType::STREAM, my_stream_id) != SPD::TileStatus::Finished) {
                DPRINTF(MAAStream, "S[%d] %s: Waiting for src tile %d to finish...\n", my_stream_id, __func__, my_src_tile);
            } else {
                DPRINTF(MAAStream, "S[%d] %s: state set to respond for request %s!\n", my_stream_id, __func__, my_instruction->print());
                state = Status::Response;
                scheduleNextExecution(true);
            }
        }
        break;
    }
    case Status::Response: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAStream, "S[%d] %s: responding %s!\n", my_stream_id, __func__, my_instruction->print());
        DPRINTF(MAATrace, "S[%d] End [%s]\n", my_stream_id, my_instruction->print());
        panic_if(scheduleNextExecution(), "S[%d] %s: Execution is not completed!\n", my_stream_id, __func__);
        panic_if(maa->allStreamPacketsSent(my_stream_id) == false, "S[%d] %s: all stream packets are not sent!\n", my_stream_id, __func__);
        panic_if(get_all_received() != get_all_sent(), "S[%d] %s: received_responses(%d) != sent_requests(%d)!\n",
                 my_stream_id, __func__, get_all_received(), get_all_sent());
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
        if (my_instruction->opcode == Instruction::OpcodeType::STREAM_LD) {
            maa->spd->setSize(my_dst_tile, my_size);
        }
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
    real_req->setRegion(my_addr_range_id);
    PacketPtr my_pkt;
    if (my_instruction->opcode == Instruction::OpcodeType::STREAM_LD) {
        my_pkt = new Packet(real_req, MemCmd::ReadReq);
    } else {
        my_pkt = new Packet(real_req, MemCmd::ReadExReq);
    }
    my_pkt->allocate();
    maa->sendPacket(FuncUnitType::STREAM, my_stream_id, my_pkt, maa->getClockEdge(Cycles(latency)), true);
    DPRINTF(MAAStream, "S[%d] %s: created %s to send in %d cycles\n", my_stream_id, __func__, my_pkt->print(), latency);
    (*maa->stats.STR_LoadsCacheAccessing[my_stream_id])++;
}
void StreamAccessUnit::readPacketSent(Addr addr) {
    DPRINTF(MAAStream, "S[%d] %s: cache read packet 0x%lx sent!\n", my_stream_id, __func__, addr);
}
void StreamAccessUnit::writePacketSent(Addr addr) {
    DPRINTF(MAAStream, "S[%d] %s: cache write packet 0x%lx sent!\n", my_stream_id, __func__, addr);
    my_received_responses++;
    // if(my_received_responses == my_sent_requests){
    //     tilewriteunit->mark_last_element_reached();
    // }
    if (maa->allStreamPacketsSent(my_stream_id) && (get_all_received() == get_all_sent() )) {
        DPRINTF(MAAStream, "S[%d] %s: all responses received, calling execution again in state %s!\n", my_stream_id, __func__, status_names[(int)state]);
        scheduleNextExecution(true);
    } else {
        DPRINTF(MAAStream, "S[%d] %s: expected: %d, received: %d!\n", my_stream_id, __func__,  get_all_sent(), get_all_received());
    }
}
bool StreamAccessUnit::recvData(const Addr addr, uint8_t *dataptr, bool cached) {

    if(tilewriteunit->recv_data(addr, dataptr, cached)){
        DPRINTF(MAAStream, "S[%d] %s: expected: %d, received: %d!\n", my_stream_id, __func__, get_all_sent() , get_all_received());
        DPRINTF(MAAStream, "S[%d] %s: my_received_responses: %d, TW_received_responses: %d!\n", my_stream_id, __func__, my_received_responses , TW_received_responses);
        DPRINTF(MAAStream, "S[%d] %s: my_sent_requests: %d, TW_sent_requests: %d!\n", my_stream_id, __func__, my_sent_requests, TW_sent_requests);
        if (maa->allStreamPacketsSent(my_stream_id) && get_all_received() == get_all_sent()) {
            DPRINTF(MAAStream, "S[%d] %s: all responses received, calling execution again in state %s!\n", my_stream_id, __func__, status_names[(int)state]);
            scheduleNextExecution(true);
        }
        return true;
    }

    bool was_request_table_full = request_table->is_full();
    std::vector<RequestTableEntry> entries = request_table->get_entries(addr);
    if (entries.empty()) {
        DPRINTF(MAAStream, "S[%d] %s: no entries found for addr(0x%lx)\n", my_stream_id, __func__, addr);
        return false;
    }
    DPRINTF(MAAStream, "S[%d] %s: %d entry found for addr(0x%lx)\n", my_stream_id, __func__, entries.size(), addr);
    uint8_t new_data[block_size];
    uint32_t *dataptr_u32_typed = (uint32_t *)dataptr;
    uint64_t *dataptr_u64_typed = (uint64_t *)dataptr;
    std::memcpy(new_data, dataptr, block_size);
    for (auto entry : entries) {
        int itr = entry.itr;
        int wid = entry.wid;

        my_received_itr_max = std::max(my_received_itr_max,itr);


        switch (my_instruction->opcode) {
        case Instruction::OpcodeType::STREAM_LD: {

            /************ going to write data in order *************/

            if (my_word_size == 4) {
                DPRINTF(MAAStream, "S[%d] %s: SPD[%d][%d] = %u\n", my_stream_id, __func__, my_dst_tile, itr, dataptr_u32_typed[wid]);
                maa->spd->setData<uint32_t>(my_dst_tile, itr, dataptr_u32_typed[wid]);
                tilewriteunit->setdata<uint32_t>(dataptr_u32_typed[wid], itr);
            } else {
                DPRINTF(MAAStream, "S[%d] %s: SPD[%d][%d] = %lu\n", my_stream_id, __func__, my_dst_tile, itr, dataptr_u64_typed[wid]);
                maa->spd->setData<uint64_t>(my_dst_tile, itr, dataptr_u64_typed[wid]);
                tilewriteunit->setdata<uint64_t>(dataptr_u64_typed[wid], itr);
            }

            // last element by receiving 
            // if(dst_tile_id != -1){
            // if(my_itr_max_final != -1 && (my_itr_max_final ==  my_received_itr_max || my_itr_max_final == my_set_fake_max) && !tilewriteunit->is_last_element_reached()){
            //     tilewriteunit->mark_last_element_reached();
            // }
            // }

            // if (my_word_size == 4) {
            //     DPRINTF(MAAStream, "S[%d] %s: SPD[%d][%d] = %u\n", my_stream_id, __func__, my_dst_tile, itr, dataptr_u32_typed[wid]);
            //     writeBuffer[itr] = dataptr_u32_typed[wid];
            //     tilewriteunit->setdata(dataptr_u32_typed[wid], itr);
            // } else {
            //     DPRINTF(MAAStream, "S[%d] %s: SPD[%d][%d] = %lu\n", my_stream_id, __func__, my_dst_tile, itr, dataptr_u64_typed[wid]);
            //     writeBuffer[itr] = dataptr_u64_typed[wid];
            //     tilewriteunit->setdata(dataptr_u32_typed[wid], itr);
            // }

            // // rewriting the data in order
            // while(sentmyIQueue.size() > 0 && writeBuffer.find(sentmyIQueue.front()) != writeBuffer.end()){
            //     int my_i_queue = sentmyIQueue.front();
            //     if (my_word_size == 4) {
            //         uint32_t data_32 = writeBuffer[my_i_queue];
            //         maa->spd->setData<uint32_t>(my_dst_tile, my_i_queue, writeBuffer[my_i_queue]);
            //         maa->spd->SPDQueues[my_dst_tile].push(data_32);
            //         DPRINTF(MAAStream, "I[%d] %s: SPD[%d][%d] = %u/%d/%f!\n", my_stream_id, __func__, my_dst_tile, my_i_queue, ((uint32_t *)&data_32)[0], ((int32_t *)&data_32)[0], ((float *)&data_32)[0]);

            //     } else {
            //         uint64_t data_64 = writeBuffer[my_i_queue];
            //         maa->spd->setData<uint64_t>(my_dst_tile, my_i_queue, writeBuffer[my_i_queue]);
            //         maa->spd->SPDQueues[my_dst_tile].push(data_64);
            //         DPRINTF(MAAStream, "I[%d] %s: SPD[%d][%d] = %lu/%ld/%lf!\n", my_stream_id, __func__, my_dst_tile, my_i_queue, ((uint64_t *)&data_64)[0], ((int64_t *)&data_64)[0], ((double *)&data_64)[0]);

            //     }
            //     writeBuffer.erase(my_i_queue);
            //     sentmyIQueue.pop();

            // }


            /********************************/
            break;
        }
        case Instruction::OpcodeType::STREAM_ST: {
            if (my_word_size == 4) {
                ((uint32_t *)new_data)[wid] = maa->spd->getData<uint32_t>(my_src_tile, itr);
                DPRINTF(MAAStream, "S[%d] %s: new_data[%d] = SPD[%d][%d] = %f!\n", my_stream_id, __func__, wid, my_src_tile, itr, ((float *)new_data)[wid]);
            } else {
                ((uint64_t *)new_data)[wid] = maa->spd->getData<uint64_t>(my_src_tile, itr);
                DPRINTF(MAAStream, "S[%d] %s: new_data[%d] = SPD[%d][%d] = %f!\n", my_stream_id, __func__, wid, my_src_tile, itr, ((double *)new_data)[wid]);
            }
            break;
        }
        default:
            assert(false);
        }
    }

    Cycles total_latency;
    if (my_instruction->opcode == Instruction::OpcodeType::STREAM_LD) {
        my_received_responses++;
        updateLatency(0, 0, entries.size(), 1);
        if (maa->allStreamPacketsSent(my_stream_id) && get_all_sent() == get_all_received()) {
            DPRINTF(MAAStream, "S[%d] %s: all responses received, calling execution again in state %s!\n", my_stream_id, __func__, status_names[(int)state]);
            scheduleNextExecution(true);
        } else {
            DPRINTF(MAAStream, "S[%d] %s: expected: %d, received: %d!\n", my_stream_id, __func__,  get_all_sent(), get_all_received());
        }
    } else {
        RequestPtr real_req = std::make_shared<Request>(addr, block_size, flags, maa->requestorId);
        real_req->setRegion(my_addr_range_id);
        PacketPtr write_pkt = new Packet(real_req, MemCmd::WritebackDirty);
        write_pkt->allocate();
        write_pkt->setData(new_data);
        DPRINTF(MAAStream, "S[%d] %s: created %s to send in %d cycles\n", my_stream_id, __func__, write_pkt->print(), total_latency);
        maa->sendPacket(FuncUnitType::STREAM, my_stream_id, write_pkt, maa->getClockEdge(total_latency), true);
    }

    // if(my_received_responses == my_sent_requests){
    //     tilewriteunit->mark_last_element_reached();
    // }

    if (was_request_table_full) {
        scheduleNextExecution(true);
    }
    return true;
}
Addr StreamAccessUnit::translatePacket(Addr vaddr) {
    /**** Address translation ****/
    RequestPtr translation_req = std::make_shared<Request>(vaddr, block_size, flags, maa->requestorId, my_instruction->PC, my_instruction->CID);
    ThreadContext *tc = maa->system->threads[my_instruction->CID];
    maa->mmu->translateTiming(translation_req, tc, this, my_is_load ? BaseMMU::Read : BaseMMU::Write);
    // The above function immediately does the translation and calls the finish function
    assert(my_translation_done);
    my_translation_done = false;
    return my_translated_addr;
}
void StreamAccessUnit::finish(const Fault &fault, const RequestPtr &req, ThreadContext *tc, BaseMMU::Mode mode) {
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
} // namespace gem5