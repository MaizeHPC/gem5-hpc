#include "mem/MAA/IndirectAccess1.hh"
#include "mem/MAA/Tables.hh"
#include "base/logging.hh"
#include "mem/MAA/MAA.hh"
#include "mem/MAA/SPD.hh"
#include "mem/MAA/IF.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "debug/MAAIndirect.hh"
#include "debug/MAATrace.hh"
#include "mem/packet.hh"
#include "sim/cur_tick.hh"


#include <cassert>
#include <cstdint>
#include <string>


#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {

///////////////
//
// INDIRECT ACCESS UNIT
//
///////////////
IndirectAccessUnit::IndirectAccessUnit()
    : executeInstructionEvent([this] { executeInstruction(); }, name()) {
    // RT_slice_org = nullptr;
    // num_RT_slices = nullptr;
    // num_RT_rows_total = nullptr;
    // num_RT_possible_grows = nullptr;
    // num_RT_subslices = nullptr;
    // num_RT_slice_columns = nullptr;
    // RT_config_addr = nullptr;
    // RT_config_cache = nullptr;
    // RT_config_cache_tick = nullptr;
    // RT = nullptr;
    // offset_table = nullptr;
    my_RT_req_sent = nullptr;
    my_RT_slice_order = nullptr;
    my_instruction = nullptr;
}
IndirectAccessUnit::~IndirectAccessUnit() {

    assert(tilewriteunit != nullptr);
    delete [] tilewriteunit;
    assert(tilereadunitIdx != nullptr);
    delete [] tilereadunitIdx;
    assert(tilereadunitSrc != nullptr);
    delete [] tilereadunitSrc;

    // assert(RT_slice_org != nullptr);
    // for (int i = 0; i < num_RT_configs; i++) {
    //     assert(RT_slice_org[i] != nullptr);
    //     delete[] RT_slice_org[i];
    // }
    // delete[] RT_slice_org;
    // assert(num_RT_slices != nullptr);
    // delete[] num_RT_slices;
    // assert(num_RT_rows_total != nullptr);
    // delete[] num_RT_rows_total;
    // assert(num_RT_possible_grows != nullptr);
    // delete[] num_RT_possible_grows;
    // assert(num_RT_subslices != nullptr);
    // delete[] num_RT_subslices;
    // assert(num_RT_slice_columns != nullptr);
    // delete[] num_RT_slice_columns;
    // assert(RT_config_addr != nullptr);
    // delete[] RT_config_addr;
    // assert(RT_config_cache != nullptr);
    // delete[] RT_config_cache;
    // assert(RT_config_cache_tick != nullptr);
    // delete[] RT_config_cache_tick;
    // assert(RT != nullptr);
    // for (int i = 0; i < num_RT_configs; i++) {
    //     assert(RT[i] != nullptr);
    //     delete[] RT[i];
    // }
    // delete[] RT;
    // assert(offset_table != nullptr);
    // delete offset_table;
    // assert(my_RT_req_sent != nullptr);
    // for (int i = 0; i < num_RT_configs; i++) {
    //     assert(my_RT_req_sent[i] != nullptr);
    //     delete[] my_RT_req_sent[i];
    // }
    // delete[] my_RT_req_sent;
    // assert(my_RT_slice_order != nullptr);
    // delete[] my_RT_slice_order;
}
void IndirectAccessUnit::allocate(int _my_indirect_id,
                                  int _num_tile_elements,
                                  bool _reorder_row_table,
                                
                                  int _num_request_table_addresses, 
                                  int _num_request_table_entries_per_address,

                                  int _num_channels,
                                  int _num_cores,
                                  MAA *_maa) {
    my_indirect_id = _my_indirect_id;
    maa = _maa;
    num_tile_elements = _num_tile_elements;
    // num_RT_rows_per_slice = _num_row_table_rows_per_slice;
    // num_RT_entries_per_subslice_row = _num_row_table_entries_per_subslice_row;
    // num_RT_config_cache_entries = _num_row_table_config_cache_entries;
    // reconfigure_RT = _reconfigure_row_table;
    // reorder_RT = _reorder_row_table;
    // num_initial_RT_slices = _num_initial_row_table_slice;
    num_channels = _num_channels;
    num_cores = _num_cores;
    my_translation_done = false;
    state = Status::Idle;
    my_instruction = nullptr;
    dst_tile_id = -1;

    num_request_table_addresses = _num_request_table_addresses;
    num_request_table_entries_per_address = _num_request_table_entries_per_address;
    request_table = new RequestTable(maa, num_request_table_addresses, num_request_table_entries_per_address, my_indirect_id, true);


    tilewriteunit = new TileWrite(maa, TW_expected_responses, TW_received_responses, my_max, maa->spd->tile_write_counts, FuncUnitType::INDIRECT);
    tilereadunitIdx = new TileRead(maa, my_max, maa->spd->tile_write_counts, FuncUnitType::INDIRECT);
    tilereadunitSrc = new TileRead(maa, my_max, maa->spd->tile_write_counts, FuncUnitType::INDIRECT);
    // offset_table = new OffsetTable();
    // offset_table->allocate(my_indirect_id, num_tile_elements, maa, false);

    // Row Table initialization
    // int min_num_RT_slices = maa->m_org[ADDR_CHANNEL_LEVEL] * maa->m_org[ADDR_RANK_LEVEL] * 2;
    // Addr max_num_RT_possible_grows = 2 * maa->m_org[ADDR_BANK_LEVEL] * maa->m_org[ADDR_ROW_LEVEL];
    // total_num_RT_subslices = maa->m_org[ADDR_CHANNEL_LEVEL] * maa->m_org[ADDR_RANK_LEVEL] *
    //                          maa->m_org[ADDR_BANKGROUP_LEVEL] * maa->m_org[ADDR_BANK_LEVEL];
    // num_RT_configs = log2((double)total_num_RT_subslices / (double)min_num_RT_slices) + 1;

    // RT_config_addr = new Addr[num_RT_config_cache_entries];
    // RT_config_cache = new int[num_RT_config_cache_entries];
    // RT_config_cache_tick = new Tick[num_RT_config_cache_entries];
    // for (int i = 0; i < num_RT_config_cache_entries; i++) {
    //     RT_config_addr[i] = 0;
    //     RT_config_cache[i] = -1;
    //     RT_config_cache_tick[i] = 0;
    // }

    // RT = new RowTableSlice *[num_RT_configs];
    // my_RT_req_sent = new bool *[num_RT_configs];
    // my_RT_slice_order = new std::vector<int>[num_RT_configs];
    // RT_slice_org = new int *[num_RT_configs];
    // num_RT_slices = new int[num_RT_configs];
    // num_RT_rows_total = new int[num_RT_configs];
    // num_RT_subslices = new int[num_RT_configs];
    // num_RT_slice_columns = new int[num_RT_configs];
    // num_RT_possible_grows = new Addr[num_RT_configs];

    // int current_num_RT_slices = min_num_RT_slices;
    // int current_num_RT_rows_total = current_num_RT_slices * num_RT_rows_per_slice;
    // Addr current_num_RT_possible_grows = max_num_RT_possible_grows;
    // int current_num_RT_subslices = total_num_RT_subslices / min_num_RT_slices;
    // int current_num_RT_entries_per_row = num_RT_entries_per_subslice_row * current_num_RT_subslices;
    // initial_RT_config = -1;
//     for (int i = 0; i < num_RT_configs; i++) {
//         RT[i] = new RowTableSlice[current_num_RT_slices];
//         my_RT_req_sent[i] = new bool[current_num_RT_slices];
//         num_RT_slices[i] = current_num_RT_slices;
//         num_RT_rows_total[i] = current_num_RT_rows_total;
//         num_RT_subslices[i] = current_num_RT_subslices;
//         num_RT_slice_columns[i] = current_num_RT_entries_per_row;
//         num_RT_possible_grows[i] = current_num_RT_possible_grows;
//         if (reconfigure_RT == false && current_num_RT_slices == num_initial_RT_slices) {
//             initial_RT_config = i;
//         }
//         panic_if(current_num_RT_entries_per_row <= 0, "I[%d] TC[%d] %s: current_num_RT_entries_per_row is %d!\n",
//                  my_indirect_id, i, __func__, current_num_RT_entries_per_row);
//         for (int j = 0; j < current_num_RT_slices; j++) {
//             RT[i][j].allocate(my_indirect_id, j, num_RT_rows_per_slice, current_num_RT_entries_per_row, offset_table, maa, false);
//             my_RT_req_sent[i][j] = false;
//         }

//         // How many banks corresponding to which level exist in
//         // this configuration (RowTableSlice Bank Organization)
//         RT_slice_org[i] = new int[ADDR_MAX_LEVEL];
//         int remaining_banks = current_num_RT_slices;
//         for (int k = 0; k < ADDR_MAX_LEVEL; k++) {
//             if (remaining_banks > maa->m_org[k]) {
//                 RT_slice_org[i][k] = maa->m_org[k];
//                 assert(remaining_banks % maa->m_org[k] == 0);
//                 remaining_banks /= maa->m_org[k];
//             } else if (remaining_banks > 0) {
//                 RT_slice_org[i][k] = remaining_banks;
//                 remaining_banks = 0;
//             } else {
//                 RT_slice_org[i][k] = 1;
//             }
//         }
//         DPRINTF(MAAIndirect, "I[%d] TC[%d]: %d banks x %d subslices x %d rows x %d columns -- CH: %d, RA: %d, BG: %d, BA: %d, RO: %d, CO: %d\n",
//                 my_indirect_id, i, num_RT_slices[i], num_RT_subslices[i], num_RT_rows_per_slice, num_RT_slice_columns[i],
//                 RT_slice_org[i][ADDR_CHANNEL_LEVEL], RT_slice_org[i][ADDR_RANK_LEVEL],
//                 RT_slice_org[i][ADDR_BANKGROUP_LEVEL], RT_slice_org[i][ADDR_BANK_LEVEL],
//                 RT_slice_org[i][ADDR_ROW_LEVEL], RT_slice_org[i][ADDR_COLUMN_LEVEL]);

//         my_RT_slice_order[i].clear();
//         for (int bank = 0; bank < maa->m_org[ADDR_BANK_LEVEL]; bank++) {
//             for (int bankgroup = 0; bankgroup < maa->m_org[ADDR_BANKGROUP_LEVEL]; bankgroup++) {
//                 for (int rank = 0; rank < maa->m_org[ADDR_RANK_LEVEL]; rank++) {
//                     for (int channel = 0; channel < maa->m_org[ADDR_CHANNEL_LEVEL]; channel++) {
//                         int RT_index = getRowTableIdx(i, channel, rank, bankgroup, bank);
//                         if (std::find(my_RT_slice_order[i].begin(),
//                                       my_RT_slice_order[i].end(),
//                                       RT_index) == my_RT_slice_order[i].end()) {
//                             my_RT_slice_order[i].push_back(RT_index);
//                         }
//                     }
//                 }
//             }
//         }
//         panic_if(my_RT_slice_order[i].size() != num_RT_slices[i],
//                  "I[%d] TC[%d] %s: my_RT_slice_order(%d) != num_RT_slices(%d)!\n",
//                  my_indirect_id, i, __func__, my_RT_slice_order[i].size(), num_RT_slices[i]);
//         current_num_RT_slices *= 2;
//         current_num_RT_rows_total *= 2;
//         current_num_RT_subslices /= 2;
//         current_num_RT_entries_per_row /= 2;
//         current_num_RT_possible_grows /= 2;
//     }
//     if (reconfigure_RT)
//         initial_RT_config = num_RT_configs - 1;

//     request_table = new RequestTable(maa, num_request_table_addresses, num_request_table_entries_per_address, my_indirect_id, true);
//     DPRINTF(MAAIndirect, "I[%d] %s: initial_RT_config(%d)!\n", my_indirect_id, __func__, initial_RT_config);

}


// int IndirectAccessUnit::getRowTableIdx(int RT_config, int channel, int rank, int bankgroup, int bank) {
//     int RT_index = 0;
//     RT_index += (channel % RT_slice_org[RT_config][ADDR_CHANNEL_LEVEL]);
//     RT_index *= (RT_slice_org[RT_config][ADDR_RANK_LEVEL]);
//     RT_index += (rank % RT_slice_org[RT_config][ADDR_RANK_LEVEL]);
//     RT_index *= (RT_slice_org[RT_config][ADDR_BANKGROUP_LEVEL]);
//     RT_index += (bankgroup % RT_slice_org[RT_config][ADDR_BANKGROUP_LEVEL]);
//     RT_index *= (RT_slice_org[RT_config][ADDR_BANK_LEVEL]);
//     RT_index += (bank % RT_slice_org[RT_config][ADDR_BANK_LEVEL]);
//     panic_if(RT_index >= num_RT_slices[RT_config],
//              "I[%d] TC[%d] %s: RT_index(%d) >= num_RT_slices(%d)!\n",
//              my_indirect_id, RT_config, __func__, RT_index, num_RT_slices[RT_config]);
//     return RT_index;
// }
// Addr IndirectAccessUnit::getGrowAddr(int RT_config, int bankgroup, int bank, int row) {
//     Addr grow_addr = 0;
//     grow_addr = (bankgroup / RT_slice_org[RT_config][ADDR_BANKGROUP_LEVEL]);
//     grow_addr *= maa->m_org[ADDR_BANK_LEVEL];
//     grow_addr += (bank / RT_slice_org[RT_config][ADDR_BANK_LEVEL]);
//     grow_addr *= maa->m_org[ADDR_ROW_LEVEL];
//     grow_addr += (row / RT_slice_org[RT_config][ADDR_ROW_LEVEL]);
//     assert(RT_slice_org[RT_config][ADDR_ROW_LEVEL] == 1);
//     panic_if(grow_addr >= num_RT_possible_grows[RT_config],
//              "I[%d] TC[%d] %s: grow_addr(%lu) >= num_RT_possible_grows(%lu)!\n",
//              my_indirect_id, RT_config, __func__, grow_addr, num_RT_possible_grows[RT_config]);
//     return grow_addr;
// }
// int IndirectAccessUnit::getRowTableConfig(Addr addr) {
//     if (reconfigure_RT == false)
//         return initial_RT_config;

//     int oldest_entry = -1;
//     Tick oldest_tick = 0;
//     Tick current_tick = curTick();
//     for (int i = 0; i < num_RT_config_cache_entries; i++) {
//         if (RT_config_addr[i] == addr) {
//             RT_config_cache_tick[i] = current_tick;
//             return RT_config_cache[i];
//         } else if (RT_config_cache_tick[i] <= oldest_tick) {
//             oldest_tick = RT_config_cache_tick[i];
//             oldest_entry = i;
//         }
//     }
//     assert(oldest_entry != -1);
//     RT_config_addr[oldest_entry] = addr;
//     RT_config_cache[oldest_entry] = initial_RT_config;
//     RT_config_cache_tick[oldest_entry] = current_tick;
//     return initial_RT_config;
// }
// void IndirectAccessUnit::setRowTableConfig(Addr addr, int num_CLs, int num_ROWs) {
//     if (reconfigure_RT == false)
//         return;

//     // This approach selects the configuration with as many ROWs as needed
//     int new_config = -1;
//     if (num_ROWs >= num_RT_rows_total[num_RT_configs - 1]) {
//         new_config = num_RT_configs - 1;
//     } else {
//         for (int i = 0; i < num_RT_configs; i++) {
//             if (num_ROWs < num_RT_rows_total[i]) {
//                 new_config = i;
//                 break;
//             }
//         }
//     }

// #if 0
//     // This approach selects the configuration with as many ROWs as needed
//     int new_config = -1;
//     if (num_ROWs >= num_RT_rows_total[num_RT_configs - 1]) {
//         new_config = num_RT_configs - 1;
//     } else {
//         for (int i = 0; i < num_RT_configs - 1; i++) {
//             if (num_ROWs < ((num_RT_rows_total[i] + num_RT_rows_total[i + 1]) / 2)) {
//                 new_config = i;
//                 break;
//             }
//         }
//     }
// #endif

// #if 0
//     // This approach does not work. If D is too large, there will be many unique CLs
//     // and many unique ROWs. The CL/ROW is still large, but since num_ROWs >> number
//     // of RT banks x RT subslices x rows/subslice, there will be a lot of drains.
//     int num_CLs_per_ROW = num_CLs / num_ROWs;
//     if (num_CLs_per_ROW >= num_RT_slice_columns[0]) {
//         new_config = 0;
//     } else if (num_CLs_per_ROW < num_RT_slice_columns[num_RT_configs - 1]) {
//         new_config = num_RT_configs - 1;
//     } else {
//         for (int i = 1; i < num_RT_configs; i++) {
//             if (num_CLs_per_ROW < num_RT_slice_columns[i - 1] &&
//                 num_CLs_per_ROW >= num_RT_slice_columns[i]) {
//                 new_config = i;
//             }
//         }
//     }
// #endif

//     assert(new_config != -1);
//     for (int i = 0; i < num_RT_config_cache_entries; i++) {
//         if (RT_config_addr[i] == addr) {
//             RT_config_cache[i] = new_config;
//             DPRINTF(MAATrace, "I[%d] %s: addr(0x%lx) set to config(%d) with (%d/%d) CLs, (%d/%d) ROWs, (%d/%d) CLs/ROW!\n",
//                     my_indirect_id, __func__, addr, new_config,
//                     num_CLs, num_RT_slice_columns[new_config] * num_RT_slices[new_config] * num_RT_rows_per_slice,
//                     num_ROWs, num_RT_rows_total[new_config],
//                     num_CLs / num_ROWs, num_RT_slice_columns[new_config]);
//             return;
//         }
//     }
//     panic_if(true, "I[%d] %s: addr(0x%lx) not found in the cache!\n", my_indirect_id, __func__, addr);
// }
void IndirectAccessUnit::check_reset() {
    // for (int i = 0; i < num_RT_configs; i++) {
    //     for (int j = 0; j < num_RT_slices[i]; j++) {
    //         RT[i][j].check_reset();
    //     }
    // }
    // offset_table->check_reset();
    panic_if(maa->allIndirectPacketsSent(my_indirect_id) == false, "All indirect packets are not sent!\n");
    panic_if(my_decode_start_tick != 0, "Decode start tick is not 0: %lu!\n", my_decode_start_tick);
    panic_if(my_fill_start_tick != 0, "Fill start tick is not 0: %lu!\n", my_fill_start_tick);
    panic_if(my_build_start_tick != 0, "Build start tick is not 0: %lu!\n", my_build_start_tick);
    panic_if(my_request_start_tick != 0, "Request start tick is not 0: %lu!\n", my_request_start_tick);
}
Cycles IndirectAccessUnit::updateLatency(int num_spd_read_data_accesses, int num_spd_read_condidx_accesses, int num_spd_write_accesses, int num_requesttable_accesses) {
    if (num_spd_read_data_accesses != 0) {
        // XByte -- 64/X bytes per SPD access
        Cycles get_data_latency = maa->spd->getDataLatency(getCeiling(num_spd_read_data_accesses, my_words_per_cl));
        my_SPD_read_finish_tick = maa->getClockEdge(get_data_latency);
        if (num_spd_read_condidx_accesses == 0) {
            (*maa->stats.IND_CyclesSPDReadAccess[my_indirect_id]) += get_data_latency;
        }
    }
    if (num_spd_read_condidx_accesses != 0) {
        // 4Byte conditions and indices -- 16 bytes per SPD access
        Cycles get_data_latency = maa->spd->getDataLatency(getCeiling(num_spd_read_condidx_accesses, 16));
        my_SPD_read_finish_tick = maa->getClockEdge(get_data_latency);
        (*maa->stats.IND_CyclesSPDReadAccess[my_indirect_id]) += get_data_latency;
    }
    if (num_spd_write_accesses != 0) {
        // XByte -- 64/X bytes per SPD access
        Cycles set_data_latency = maa->spd->setDataLatency(my_dst_tile, getCeiling(num_spd_write_accesses, my_words_per_cl));
        my_SPD_write_finish_tick = maa->getClockEdge(set_data_latency);
        (*maa->stats.IND_CyclesSPDWriteAccess[my_indirect_id]) += set_data_latency;
    }

    if (num_requesttable_accesses != 0) {
        Cycles access_requesttable_latency = Cycles(num_requesttable_accesses);
        if (my_RT_access_finish_tick < curTick())
            my_RT_access_finish_tick = maa->getClockEdge(access_requesttable_latency);
        else
            my_RT_access_finish_tick += maa->getCyclesToTicks(access_requesttable_latency);
        // (*maa->stats.STR_CyclesRTAccess[my_indirect_id]) += access_requesttable_latency;
    }
    Tick finish_tick = std::max(std::max(my_SPD_read_finish_tick, my_SPD_write_finish_tick), my_RT_access_finish_tick);
    return maa->getTicksToCycles(finish_tick - curTick());
}

bool IndirectAccessUnit::scheduleNextExecution(bool force) {
    Tick finish_tick = my_RT_access_finish_tick;
    if (state == Status::Response) {
        finish_tick = std::max(std::max(my_SPD_read_finish_tick, my_SPD_write_finish_tick), my_RT_access_finish_tick);
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
void IndirectAccessUnit::checkTileReady() {
    // Check if any of the source tiles are ready
    // Set my_max to the size of the ready tile
    if (my_cond_tile != -1) {
        if (maa->spd->getTileStatus(my_cond_tile, (uint8_t)FuncUnitType::INDIRECT, my_indirect_id) == SPD::TileStatus::Finished) {
            my_cond_tile_ready = true;
            if (my_max == -1) {
                my_max = maa->spd->getSize(my_cond_tile);
                DPRINTF(MAAIndirect, "I[%d] %s: my_max = cond size (%d)!\n", my_indirect_id, __func__, my_max);
            }
            panic_if(maa->spd->getSize(my_cond_tile) != my_max, "I[%d] %s: cond size (%d) != max (%d)!\n", my_indirect_id, __func__, maa->spd->getSize(my_cond_tile), my_max);
        }
    }
    if (maa->spd->getTileStatus(my_idx_tile, (uint8_t)FuncUnitType::INDIRECT, my_indirect_id) == SPD::TileStatus::Finished) {
        my_idx_tile_ready = true;
        if (my_max == -1) {
            my_max = maa->spd->getSize(my_idx_tile);
            DPRINTF(MAAIndirect, "I[%d] %s: my_max = idx size (%d)!\n", my_indirect_id, __func__, my_max);
        }
        panic_if(maa->spd->getSize(my_idx_tile) != my_max, "I[%d] %s: idx size (%d) != max (%d)!\n", my_indirect_id, __func__, maa->spd->getSize(my_idx_tile), my_max);
    }
    if (my_instruction->opcode != Instruction::OpcodeType::INDIR_LD && 
            my_instruction->opcode != Instruction::OpcodeType::INDIR_ST_SCALAR && 
                my_instruction->opcode != Instruction::OpcodeType::INDIR_RMW_SCALAR && 
                maa->spd->getTileStatus(my_src_tile, (uint8_t)FuncUnitType::INDIRECT, my_indirect_id) == SPD::TileStatus::Finished) {
        my_src_tile_ready = true;
    }
}
bool IndirectAccessUnit::checkElementReady() {
    bool cond_ready = my_cond_tile == -1 || maa->spd->getElementFinished(my_cond_tile, my_i, 4, (uint8_t)FuncUnitType::INDIRECT, my_indirect_id);
    bool idx_ready = cond_ready && maa->spd->getElementFinished(my_idx_tile, my_i, 4, (uint8_t)FuncUnitType::INDIRECT, my_indirect_id);
    bool src_ready = idx_ready && (my_instruction->opcode == Instruction::OpcodeType::INDIR_LD || my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_SCALAR || my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_SCALAR || maa->spd->getElementFinished(my_src_tile, my_i, my_word_size, (uint8_t)FuncUnitType::INDIRECT, my_indirect_id));
    if (cond_ready == false) {
        DPRINTF(MAAIndirect, "I[%d] %s: cond tile[%d] element[%d] not ready, returning!\n", my_indirect_id, __func__, my_cond_tile, my_i);
    } else if (idx_ready == false) {
        DPRINTF(MAAIndirect, "I[%d] %s: idx tile[%d] element[%d] not ready, returning!\n", my_indirect_id, __func__, my_idx_tile, my_i);
    } else if (src_ready == false) {
        // TODO: this is too early to check src_ready, check it in other stages
        DPRINTF(MAAIndirect, "I[%d] %s: src tile[%d] element[%d] not ready, returning!\n", my_indirect_id, __func__, my_src_tile, my_i);
    }
    if (cond_ready == false || idx_ready == false || src_ready == false) {
        return false;
    }
    return true;
}
bool IndirectAccessUnit::checkReadyForFinish() {
    if (my_cond_tile_ready == false) {
        DPRINTF(MAAIndirect, "I[%d] %s: cond tile[%d] not ready, returning!\n", my_indirect_id, __func__, my_cond_tile);
        // Just a fake access to callback INDIRECT when the condition is ready
        maa->spd->getElementFinished(my_cond_tile, my_i, 4, (uint8_t)FuncUnitType::INDIRECT, my_indirect_id);
        return false;
    } else if (my_idx_tile_ready == false) {
        DPRINTF(MAAIndirect, "I[%d] %s: idx tile[%d] not ready, returning!\n", my_indirect_id, __func__, my_idx_tile);
        // Just a fake access to callback INDIRECT when the idx is ready
        maa->spd->getElementFinished(my_idx_tile, my_i, 4, (uint8_t)FuncUnitType::INDIRECT, my_indirect_id);
        return false;
    } else if (my_src_tile_ready == false) {
        DPRINTF(MAAIndirect, "I[%d] %s: src tile[%d] not ready, returning!\n", my_indirect_id, __func__, my_src_tile);
        // Just a fake access to callback INDIRECT when the src is ready
        maa->spd->getElementFinished(my_src_tile, my_i, my_word_size, (uint8_t)FuncUnitType::INDIRECT, my_indirect_id);
        return false;
    }
    return true;
}


void IndirectAccessUnit::fillRequestTable(bool &finished, bool &waitForFinish, bool &waitForElement, bool &needDrain, int &num_spd_read_condidx_accesses, int &num_rowtable_accesses) {
    finished = false;
    waitForFinish = false;
    waitForElement = false;
    needDrain = false;
    num_spd_read_condidx_accesses = 0;
    num_rowtable_accesses = 0;
    int num_request_table_cacheline_accesses = 0;
    checkTileReady();
    while (request_table->is_full() == false) {
        if (my_max != -1 && my_i >= my_max) {
            if (my_dst_tile != -1) {
                panic_if(my_max != -1 && my_i != my_max, "I[%d] %s: my_i(%d) != my_max(%d)!\n", my_indirect_id, __func__, my_i, my_max);
                maa->spd->setSize(my_dst_tile, my_i);
            }
            if (checkReadyForFinish()) {
                DPRINTF(MAAIndirect, "I[%d] %s: reached finished status!\n", my_indirect_id, __func__);
                finished = true;
                break;
            } else {
                waitForFinish = true;
                break;
            }
        }
        if (checkElementReady() == false) {
            // Row table parallelism = total #sub-banks. Each bank can be inserted once at a cycle
            // updateLatency(0, num_spd_read_condidx_accesses, 0, num_rowtable_accesses, total_num_RT_subslices);
            waitForElement = true;
            break;
        }
        
        // fill the src and index fifo 
        // maa->spd->fillQueue<uint32_t> (my_idx_tile); 
        if (my_cond_tile != -1) {
            num_spd_read_condidx_accesses++;
        }



        uint32_t idx ; //  = maa->spd->getData<uint32_t>(my_idx_tile, my_i);
        uint32_t data_32;
        uint64_t data_64;
        bool src_avail;
        
        bool idx_avail = tilereadunitIdx->getData<uint32_t>(idx, my_i, false);
        if(my_src_tile != -1){
            if(my_word_size ==4){
                src_avail = tilereadunitSrc->getData<uint32_t>(data_32, my_i, false);
            } else {
                src_avail = tilereadunitSrc->getData<uint64_t>(data_64, my_i, false);
            }
        }

        if(!idx_avail || !src_avail){
            waitForElement = true;
            break;
        }


        if (my_cond_tile == -1 || maa->spd->getData<uint32_t>(my_cond_tile, my_i) != 0) {

            num_spd_read_condidx_accesses++;
            Addr vaddr = my_base_addr + my_word_size * idx;
            panic_if(vaddr < my_min_addr || vaddr >= my_max_addr, "I[%d] %s: vaddr 0x%lx out of range [0x%lx, 0x%lx)!\n", my_indirect_id, __func__, vaddr, my_min_addr, my_max_addr);
            Addr block_vaddr = addrBlockAligner(vaddr, block_size);
            DPRINTF(MAAIndirect, "I[%d] %s: baseaddr = 0x%lx idx = %u wordsize = %d vaddr = 0x%lx!\n", my_indirect_id, __func__, my_base_addr, idx, my_word_size, vaddr);
            Addr paddr = translatePacket(block_vaddr, my_is_load);
            Addr block_paddr = addrBlockAligner(paddr, block_size);
            DPRINTF(MAAIndirect, "I[%d] %s: idx = %u, my_i = %u, addr = 0x%lx!\n", my_indirect_id, __func__, idx, my_i, block_paddr);
            uint16_t wid = (vaddr - block_vaddr) / my_word_size;

            // check the entry already exists in the request table 
            std::vector<RequestTableEntry> entries = request_table->get_entries_no_delete(block_paddr);
            bool firstCachelineAccess = (entries.size() == 0);
            // put the entries back
            // if(!firstCachelineAccess){
            //     for (auto entry : entries) {
            //         int itr = entry.itr;
            //         int wid = entry.wid;
            //         request_table->add_entry(itr, block_paddr, wid);
            //     } 
            // }
            bool inserted;
            if(my_src_tile != -1){
                if(my_word_size ==4){
                    // uint32_t data = maa->spd->getData<uint32_t>(my_src_tile, my_i);
                    inserted = request_table->add_entry(my_i, block_paddr, wid, data_32);
                } else {
                    // uint64_t data = maa->spd->getData<uint64_t>(my_src_tile, my_i);
                    inserted = request_table->add_entry(my_i, block_paddr, wid, data_64);
                }

            } else {
                inserted = request_table->add_entry(my_i, block_paddr, wid);
            }
             
            if(firstCachelineAccess && inserted){
                // num_spd_read_condidx_accesses/my_words_per_cl
                createReadPacket(block_paddr, num_spd_read_condidx_accesses);
                my_expected_responses++;
            }

        

            if(inserted){
                this->sentmyIQueue.push(my_i);
            }
            
            // if(my_i % my_words_per_cl == 0 && inserted){
            //     Addr TileOffset = my_dst_tile * num_tile_elements * 4  + my_word_size * my_i;
            //     Addr vaddr_tile_my_i = maa->CacheTiles_address + TileOffset;
            //     Addr block_vaddr_tile_my_i = addrBlockAligner(vaddr_tile_my_i, block_size);
            //     assert(vaddr_tile_my_i == block_vaddr_tile_my_i);
            //     Addr paddr_tile_my_i = translatePacket(block_vaddr_tile_my_i, true);

            //     // creating read exclusive request
            //     RequestPtr readex_req = std::make_shared<Request>(paddr_tile_my_i, block_size, flags, maa->requestorId);
            //     ReadExTile_CAM[paddr_tile_my_i]  = true; // this will be replaced with an address range check
            //     readex_req->setRegion(my_addr_range_id);
            //     readex_req->setRegion(maa->CacheTiles_rangeID);
            //     PacketPtr readex_pkt;
            //     readex_pkt = new Packet(readex_req, MemCmd::ReadExReq);
            //     readex_pkt->headerDelay = readex_pkt->payloadDelay = 0;
            //     readex_pkt->allocate();
            //     maa->sendPacket(FuncUnitType::INDIRECT, my_indirect_id, readex_pkt, maa->getClockEdge(Cycles(num_spd_read_condidx_accesses)), true);
            //     DPRINTF(MAAIndirect, "I[%d] %s: created %s for mem\n", my_indirect_id, __func__, readex_pkt->print());
            // }
            

            num_request_table_cacheline_accesses++;
            
            // if request table is full, break 
            if(!inserted){
                needDrain = true;
                (*maa->stats.IND_NumRTFull[my_indirect_id])++;
                break;
            }
            // updateLatency(num_spd_read_condidx_accesses, num_spd_read_condidx_accesses, 0, num_request_table_cacheline_accesses);





            // std::vector<int> addr_vec = maa->map_addr(block_paddr);
            // my_RT_idx = getRowTableIdx(my_RT_config, addr_vec[ADDR_CHANNEL_LEVEL], addr_vec[ADDR_RANK_LEVEL], addr_vec[ADDR_BANKGROUP_LEVEL], addr_vec[ADDR_BANK_LEVEL]);
            // Addr grow_addr = getGrowAddr(my_RT_config, addr_vec[ADDR_BANKGROUP_LEVEL], addr_vec[ADDR_BANK_LEVEL], addr_vec[ADDR_ROW_LEVEL]);
            // DPRINTF(MAAIndirect, "I[%d] %s: inserting vaddr(0x%lx), paddr(0x%lx), MAP(RO: %d, BA: %d, BG: %d, RA: %d, CO: %d, CH: %d), grow(0x%lx), itr(%d), idx(%d), wid(%d) to T[%d]\n", my_indirect_id, __func__, block_vaddr, block_paddr, addr_vec[ADDR_ROW_LEVEL], addr_vec[ADDR_BANK_LEVEL], addr_vec[ADDR_BANKGROUP_LEVEL], addr_vec[ADDR_RANK_LEVEL], addr_vec[ADDR_COLUMN_LEVEL], addr_vec[ADDR_CHANNEL_LEVEL], grow_addr, my_i, idx, wid, my_RT_idx);
            // bool first_CL_access;
            // bool inserted = RT[my_RT_config][my_RT_idx].insert(grow_addr, block_paddr, my_i, wid, first_CL_access);
            // num_rowtable_accesses++;
            // if (inserted == false) {
            //     needDrain = true;
            //     (*maa->stats.IND_NumRTFull[my_indirect_id])++;
            //     break;
            // } else {
            //     my_unique_WORD_addrs.insert(vaddr);
            //     my_unique_CL_addrs.insert(block_paddr);
            //     my_unique_ROW_addrs.insert(grow_addr + my_RT_idx * num_RT_possible_grows[my_RT_config]);
            //     if (reorder_RT == false && first_CL_access == true) {
            //         DPRINTF(MAAIndirect, "I[%d] %s: Creating packet for bank[%d], addr[0x%lx]!\n", my_indirect_id, __func__, my_RT_idx, block_paddr);
            //         my_expected_responses++;
            //         createReadPacket(block_paddr, getCeiling(num_rowtable_accesses, total_num_RT_subslices) * rowtable_latency);
            //     }
            // }
        } else if (my_dst_tile != -1) {
            DPRINTF(MAAIndirect, "I[%d] %s: SPD[%d][%d] = %u (cond not taken)\n", my_indirect_id, __func__, my_dst_tile, my_i, 0);
            set_fake_max = std::max(set_fake_max, my_i);
            maa->spd->setFakeData(my_dst_tile, my_i, my_word_size);
            if(my_word_size == 4){
                tilewriteunit->setdata<uint32_t>(0, my_i);
            } else if(my_word_size == 8){
                tilewriteunit->setdata<uint64_t>(0, my_i);
            }
        }

        // remove the elements 
        tilereadunitIdx->getData<uint32_t>(idx, my_i, true);
        if(my_src_tile != -1){
            if(my_word_size ==4){
                tilereadunitSrc->getData<uint32_t>(data_32, my_i, true);
            } else {
                tilereadunitSrc->getData<uint64_t>(data_64, my_i, true);
            }
        }
        // if(my_idx_tile != -1){
        //     DPRINTF(MAAIndirect, "poped value =%d,  my_i=%d\n",maa->spd->SPDQueues[my_idx_tile].front(), my_i);
        //     maa->spd->SPDQueues[my_idx_tile].pop();
        // }
        my_i++;
    }
}
void IndirectAccessUnit::executeInstruction() {
    switch (state) {
    case Status::Idle: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAIndirect, "I[%d] %s: idling %s!\n", my_indirect_id, __func__, my_instruction->print());
        DPRINTF(MAATrace, "I[%d] Start [%s]\n", my_indirect_id, my_instruction->print());
        state = Status::Decode;
        [[fallthrough]];
    }
    case Status::Decode: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAIndirect, "I[%d] %s: decoding %s!\n", my_indirect_id, __func__, my_instruction->print());

        // Decoding the instruction
        my_base_addr = my_instruction->baseAddr;
        my_idx_tile = my_instruction->src1SpdID;
        my_src_tile = my_instruction->src2SpdID;
        my_src_reg = my_instruction->src1RegID;
        my_dst_tile = my_instruction->dst1SpdID;
        my_cond_tile = my_instruction->condSpdID;
        if (my_instruction->opcode == Instruction::OpcodeType::INDIR_LD ||
            my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_VECTOR ||
            my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_SCALAR) {
            my_is_load = true;
        } else if (my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_VECTOR ||
                   my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_SCALAR) {
            my_is_load = false;
        } else {
            assert(false);
        }
        if (my_instruction->opcode == Instruction::OpcodeType::INDIR_LD) {
            my_word_size = my_instruction->getWordSize(my_dst_tile);
        } else if (my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_VECTOR ||
                   my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_VECTOR) {
            my_word_size = my_instruction->getWordSize(my_src_tile);
        } else if (my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_SCALAR ||
                   my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_SCALAR) {
            my_word_size = my_instruction->WordSize();
        } else {
            assert(false);
        }
        my_words_per_cl = 64 / my_word_size;
        maa->stats.numInst++;
        (*maa->stats.IND_NumInsts[my_indirect_id])++;
        if (my_instruction->opcode == Instruction::OpcodeType::INDIR_LD) {
            maa->stats.numInst_INDRD++;
        } else if (my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_SCALAR ||
                   my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_VECTOR) {
            maa->stats.numInst_INDWR++;
        } else if (my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_SCALAR ||
                   my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_VECTOR) {
            maa->stats.numInst_INDRMW++;
        } else {
            assert(false);
        }
        my_cond_tile_ready = (my_cond_tile == -1) ? true : false;
        my_idx_tile_ready = false;
        my_src_tile_ready = (my_instruction->opcode == Instruction::OpcodeType::INDIR_LD || my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_SCALAR || my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_SCALAR) ? true : false;
        // my_RT_config = getRowTableConfig(my_base_addr);

        // Initialization
        my_virtual_addr = 0;
        my_received_responses = my_expected_responses = 0; my_processed_count = 0;
        TW_received_responses = TW_expected_responses = 0; 
        // offset_table->reset();
        // for (int i = 0; i < num_RT_slices[my_RT_config]; i++) {
        //     RT[my_RT_config][i].reset();
        //     my_RT_req_sent[my_RT_config][i] = false;
        // }
        my_i = 0;
        last_blk_tileWrite = 0;
        last_blk_tileWrite = -1;
        
        my_i_count = 0;
        my_max = -1;
        my_SPD_read_finish_tick = curTick();
        my_SPD_write_finish_tick = curTick();
        my_RT_access_finish_tick = curTick();
        // my_RT_read_access_finish_tick = curTick();
        // my_RT_write_access_finish_tick = curTick();
        my_decode_start_tick = curTick();
        my_fill_start_tick = 0;
        my_build_start_tick = 0;
        my_request_start_tick = 0;
        my_fill_finished = false;
        my_force_cache_determined = false;
        my_force_cache = false;
        my_min_addr = my_instruction->minAddr;
        my_max_addr = my_instruction->maxAddr;
        my_addr_range_id = my_instruction->addrRangeID;

        // clear the queue
        this->sentmyIQueue = {};
        CacheTileWrite = true;
        CacheTileWriteCount = 0;

        set_fake_max = -1;
        my_received_itr_max = -1;
  

        // Setting the state of the instruction and stream unit
        my_instruction->state = Instruction::Status::Service;
        DPRINTF(MAAIndirect, "I[%d] %s: state set to Fill for request %s!\n", my_indirect_id, __func__, my_instruction->print());

        // 
        const int num_initial_reqs = 100;
        if(my_dst_tile != -1){
            tilewriteunit->set(my_dst_tile, my_word_size, my_instruction->CID, my_instruction->PC, block_size);
            tilewriteunit->createAndSendTileExReads(num_initial_reqs);
        }

        if(my_idx_tile != -1){
            tilereadunitIdx->set(my_idx_tile, 4, my_instruction->CID, my_instruction->PC, block_size); // IDX tile is always 4 bytes
            tilereadunitIdx->createAndSendTileExReads(num_initial_reqs);
        }

        if(my_src_tile != -1){
            tilereadunitSrc->set(my_src_tile, my_word_size, my_instruction->CID, my_instruction->PC, block_size); // IDX tile is always 4 bytes
            tilereadunitSrc->createAndSendTileExReads(num_initial_reqs);
        }

        state = Status::Fill;
        [[fallthrough]];
    }
    case Status::Fill: {
        // Reordering the indices
        DPRINTF(MAAIndirect, "I[%d] %s: filling %s!\n", my_indirect_id, __func__, my_instruction->print());
        if (scheduleNextExecution()) {
            break;
        }
        if (my_fill_start_tick == 0) {
            my_fill_start_tick = curTick();
        }
        if (my_request_start_tick != 0) {
            (*maa->stats.IND_CyclesRequest[my_indirect_id]) += maa->getTicksToCycles(curTick() - my_request_start_tick);
            my_request_start_tick = 0;
        }
        bool finished, waitForFinish, waitForElement, needDrain;
        int num_spd_read_condidx_accesses, num_rowtable_accesses;
        fillRequestTable(finished, waitForFinish, waitForElement, needDrain, num_spd_read_condidx_accesses, num_rowtable_accesses);
        bool buildReady = false;
        if (waitForFinish) {
            DPRINTF(MAAIndirect, "I[%d] %s: waiting for fill finish %s!\n", my_indirect_id, __func__, my_instruction->print());
        } else if (finished) {
            DPRINTF(MAAIndirect, "I[%d] %s: fill finished %s!\n", my_indirect_id, __func__, my_instruction->print());
            my_fill_finished = true;
            buildReady = true;
        } else if (waitForElement) {
            DPRINTF(MAAIndirect, "I[%d] %s: waiting for fill element %s!\n", my_indirect_id, __func__, my_instruction->print());
        } else if (needDrain) {
            DPRINTF(MAAIndirect, "I[%d] %s: fill needs to drain %s!\n", my_indirect_id, __func__, my_instruction->print());
            my_fill_finished = false;
            buildReady = true;
        } else {
            panic_if(false, "I[%d] %s: unknown state!\n", my_indirect_id, __func__);
        }
        // Row table parallelism = total #sub-banks. Each bank can be inserted once at a cycle
        updateLatency(0, num_spd_read_condidx_accesses, 0, num_rowtable_accesses);
        if (buildReady) {
            // if (reorder_RT) {
            //     DPRINTF(MAAIndirect, "I[%d] %s: state set to Build for %s!\n", my_indirect_id, __func__, my_instruction->print());
            //     state = Status::Build;
            //     scheduleNextExecution(true);
            // } else {
            DPRINTF(MAAIndirect, "I[%d] %s: state set to Request for %s!\n", my_indirect_id, __func__, my_instruction->print());
            state = Status::Request;
            scheduleNextExecution(true);
            // }
        }
        return;
    }
    // case Status::Build: {
    //     assert(my_instruction != nullptr);
    //     DPRINTF(MAAIndirect, "I[%d] %s: Building %s requests, fill finished: %s!\n",
    //             my_indirect_id, __func__, my_instruction->print(), my_fill_finished ? "true" : "false");
    //     if (scheduleNextExecution()) {
    //         break;
    //     }
    //     if (my_build_start_tick == 0) {
    //         my_build_start_tick = curTick();
    //     }
    //     if (my_fill_start_tick != 0) {
    //         (*maa->stats.IND_CyclesFill[my_indirect_id]) += maa->getTicksToCycles(curTick() - my_fill_start_tick);
    //         my_fill_start_tick = 0;
    //     }
    //     int last_RT_sent = 0;
    //     int num_rowtable_accesses = 0;
    //     Addr addr;
    //     if (my_force_cache_determined == false) {
    //         my_force_cache_determined = true;
    //         if (my_unique_WORD_addrs.size() > my_words_per_cl * my_unique_CL_addrs.size()) {
    //             DPRINTF(MAAIndirect, "I[%d] %s: Direct cache access is needed!\n", my_indirect_id, __func__);
    //             my_force_cache = true;
    //         } else {
    //             DPRINTF(MAAIndirect, "I[%d] %s: Direct cache access is not needed!\n", my_indirect_id, __func__);
    //             my_force_cache = false;
    //         }
    //     }
    //     while (true) {
    //         if (checkAndResetAllRowTablesSent())
    //             break;
    //         for (; last_RT_sent < num_RT_slices[my_RT_config]; last_RT_sent++) {
    //             int RT_idx = my_RT_slice_order[my_RT_config][last_RT_sent];
    //             assert(RT_idx < num_RT_slices[my_RT_config]);
    //             DPRINTF(MAAIndirect, "I[%d] %s: Checking row table bank[%d]!\n", my_indirect_id, __func__, RT_idx);
    //             if (my_RT_req_sent[my_RT_config][RT_idx] == false) {
    //                 if (RT[my_RT_config][RT_idx].get_entry_send(addr, my_fill_finished)) {
    //                     DPRINTF(MAAIndirect, "I[%d] %s: Creating packet for bank[%d], addr[0x%lx]!\n", my_indirect_id, __func__, RT_idx, addr);
    //                     my_expected_responses++;
    //                     num_rowtable_accesses++;
    //                     createReadPacket(addr, getCeiling(num_rowtable_accesses, total_num_RT_subslices) * rowtable_latency);
    //                 } else {
    //                     DPRINTF(MAAIndirect, "I[%d] %s: T[%d] has nothing, setting sent to true!\n", my_indirect_id, __func__, RT_idx);
    //                     my_RT_req_sent[my_RT_config][RT_idx] = true;
    //                 }
    //             } else {
    //                 DPRINTF(MAAIndirect, "I[%d] %s: T[%d] has already sent the requests!\n", my_indirect_id, __func__, RT_idx);
    //             }
    //         }
    //         last_RT_sent = (last_RT_sent >= num_RT_slices[my_RT_config]) ? 0 : last_RT_sent;
    //     }
    //     DPRINTF(MAAIndirect, "I[%d] %s: state set to Request for %s!\n", my_indirect_id, __func__, my_instruction->print());
    //     // Row table parallelism = total #banks. Each bank can give us a address in a cycle.
    //     updateLatency(0, 0, 0, num_rowtable_accesses, 0, total_num_RT_subslices);
    //     state = Status::Request;
    //     scheduleNextExecution(true);
    //     break;
    // }
    case Status::Request: {
        assert(my_instruction != nullptr);
        DPRINTF(MAAIndirect, "I[%d] %s: requesting %s!\n", my_indirect_id, __func__, my_instruction->print());
        if (my_request_start_tick == 0) {
            my_request_start_tick = curTick();
        }
        // if (reorder_RT) {
        //     if (my_build_start_tick != 0) {
        //         (*maa->stats.IND_CyclesBuild[my_indirect_id]) += maa->getTicksToCycles(curTick() - my_build_start_tick);
        //         my_build_start_tick = 0;
        //     }
        // } else {
        if (my_fill_start_tick != 0) {
            (*maa->stats.IND_CyclesFill[my_indirect_id]) += maa->getTicksToCycles(curTick() - my_fill_start_tick);
            my_fill_start_tick = 0;
        }


        if(my_dst_tile != -1){
            if(((my_max != -1 && my_max == my_received_itr_max+1) || my_max == set_fake_max + 1) && !tilewriteunit->is_last_element_reached()){
                DPRINTF(MAAIndirect, "I[%d] %s itr:%d: marking the last element\n", my_indirect_id, __func__, my_received_itr_max);
                tilewriteunit->mark_last_element_reached();
            }
        }
        

        int offset_cache_tile_write = CacheTileWrite ? CacheTileWriteCount : 0;
        // if (maa->allIndirectPacketsSent(my_indirect_id) && get_all_received() == get_all_expected() + offset_cache_tile_write) {
        if (maa->allIndirectPacketsSent(my_indirect_id) && (my_expected_responses == my_received_responses)  && tilewriteunit->check_all_responses_received()) {
            if (scheduleNextExecution()) {
                DPRINTF(MAAIndirect, "I[%d] %s: requesting is still not ready, returning!\n", my_indirect_id, __func__);
                break;
            }
            if (my_fill_finished) {
                state = Status::Response;
                my_fill_finished = false;
            } else {
                state = Status::Fill;
            }
            DPRINTF(MAAIndirect, "I[%d] %s: all responses received, calling execution again in state %s!\n", my_indirect_id, __func__, status_names[(int)state]);
            scheduleNextExecution(true);
            break;
        }
        if (my_fill_finished == false) {
            bool finished, waitForFinish, waitForElement, needDrain;
            int num_spd_read_condidx_accesses, num_rowtable_accesses;
            fillRequestTable(finished, waitForFinish, waitForElement, needDrain, num_spd_read_condidx_accesses, num_rowtable_accesses);
            // Row table parallelism = total #sub-banks. Each bank can be inserted once at a cycle
            updateLatency(0, num_spd_read_condidx_accesses, 0, 0);
        }
        break;
    }
    case Status::Response: {

        assert(my_instruction != nullptr);
        DPRINTF(MAAIndirect, "I[%d] %s: responding %s!\n", my_indirect_id, __func__, my_instruction->print());
        DPRINTF(MAATrace, "I[%d] End [%s]\n", my_indirect_id, my_instruction->print());
        panic_if(scheduleNextExecution(), "I[%d] %s: Execution is not completed!\n", my_indirect_id, __func__);
        panic_if(maa->allIndirectPacketsSent(my_indirect_id) == false, "All indirect packets are not sent!\n");
        panic_if(my_cond_tile_ready == false, "I[%d] %s: cond tile[%d] is not ready!\n", my_indirect_id, __func__, my_cond_tile);
        panic_if(my_idx_tile_ready == false, "I[%d] %s: idx tile[%d] is not ready!\n", my_indirect_id, __func__, my_idx_tile);
        panic_if(my_src_tile_ready == false, "I[%d] %s: src tile[%d] is not ready!\n", my_indirect_id, __func__, my_src_tile);
        panic_if(LoadsCacheHitRespondingTimeHistory.size() != 0, "I[%d] %s: LoadsCacheHitRespondingTimeHistory is not empty!\n", my_indirect_id, __func__);
        panic_if(LoadsCacheHitAccessingTimeHistory.size() != 0, "I[%d] %s: LoadsCacheHitAccessingTimeHistory is not empty!\n", my_indirect_id, __func__);
        panic_if(LoadsMemAccessingTimeHistory.size() != 0, "I[%d] %s: LoadsMemAccessingTimeHistory is not empty!\n", my_indirect_id, __func__);
        DPRINTF(MAAIndirect, "I[%d] %s: state set to finish for request %s!\n", my_indirect_id, __func__, my_instruction->print());
        my_instruction->state = Instruction::Status::Finish;
        tilereadunitIdx->unset_ready_to_req();
        tilereadunitSrc->unset_ready_to_req();


        if (my_request_start_tick != 0) {
            (*maa->stats.IND_CyclesRequest[my_indirect_id]) += maa->getTicksToCycles(curTick() - my_request_start_tick);
            my_request_start_tick = 0;
        }
        Cycles total_cycles = maa->getTicksToCycles(curTick() - my_decode_start_tick);
        maa->stats.cycles += total_cycles;
        my_decode_start_tick = 0;
        state = Status::Idle;
        request_table->check_reset();
        maa->finishInstructionCompute(my_instruction);
        if (my_instruction->opcode == Instruction::OpcodeType::INDIR_LD) {
            maa->stats.cycles_INDRD += total_cycles;
        } else if (my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_SCALAR ||
                   my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_VECTOR) {
            maa->stats.cycles_INDWR += total_cycles;
        } else {
            maa->stats.cycles_INDRMW += total_cycles;
        }
        // setRowTableConfig(my_base_addr, my_unique_CL_addrs.size(), my_unique_ROW_addrs.size());
        (*maa->stats.IND_NumUniqueWordsInserted[my_indirect_id]) += my_unique_WORD_addrs.size();
        (*maa->stats.IND_NumUniqueCacheLineInserted[my_indirect_id]) += my_unique_CL_addrs.size();
        (*maa->stats.IND_NumUniqueRowsInserted[my_indirect_id]) += my_unique_ROW_addrs.size();
        my_unique_WORD_addrs.clear();
        my_unique_CL_addrs.clear();
        my_unique_ROW_addrs.clear();
        my_instruction = nullptr;
        break;
    }
    
    default:
        assert(false);
    }
}

void IndirectAccessUnit::createReadPacket(Addr addr, int latency) {
    /**** Packet generation ****/
    RequestPtr real_req = std::make_shared<Request>(addr, block_size, flags, maa->requestorId);
    real_req->setRegion(my_addr_range_id);
    PacketPtr read_pkt;
    if (my_instruction->opcode == Instruction::OpcodeType::INDIR_LD) {
        read_pkt = new Packet(real_req, MemCmd::ReadReq);
    } else {
        read_pkt = new Packet(real_req, MemCmd::ReadExReq);
    }
    read_pkt->headerDelay = read_pkt->payloadDelay = 0;
    read_pkt->allocate();
    // forcing to send through cache
    maa->sendPacket(FuncUnitType::INDIRECT, my_indirect_id, read_pkt, maa->getClockEdge(Cycles(latency)), true);
    DPRINTF(MAAIndirect, "I[%d] %s: created %s for mem\n", my_indirect_id, __func__, read_pkt->print());
}

void IndirectAccessUnit::memReadPacketSent(Addr addr) {
    DPRINTF(MAAIndirect, "I[%d] %s: mem read packet 0x%lx sent\n", my_indirect_id, __func__, addr);
    (*maa->stats.IND_LoadsMemAccessing[my_indirect_id])++;
    LoadsMemAccessingTimeHistory[addr] = curTick();
}
void IndirectAccessUnit::memWritePacketSent(Addr addr) {
    DPRINTF(MAAIndirect, "I[%d] %s: mem write packet 0x%lx sent\n", my_indirect_id, __func__, addr);
    // my_received_responses++;
    int offset_cache_tile_write = CacheTileWrite ? CacheTileWriteCount : 0;
    if (maa->allIndirectPacketsSent(my_indirect_id) && (get_all_received() == get_all_expected() + offset_cache_tile_write)) {
        DPRINTF(MAAIndirect, "I[%d] %s: all responses received, calling execution again in state %s!\n", my_indirect_id, __func__, status_names[(int)state]);
        scheduleNextExecution(true);
    } else {
        DPRINTF(MAAIndirect, "I[%d] %s: expected: %d, received: %d!\n", my_indirect_id, __func__, get_all_expected(), get_all_received());
    }
}
void IndirectAccessUnit::cacheReadPacketSent(Addr addr) {
    DPRINTF(MAAIndirect, "I[%d] %s: cache read packet 0x%lx sent\n", my_indirect_id, __func__, addr);
    LoadsCacheHitAccessingTimeHistory[addr] = curTick();
    (*maa->stats.IND_LoadsCacheHitAccessing[my_indirect_id])++;
}
void IndirectAccessUnit::cacheWritePacketSent(Addr addr) {
    DPRINTF(MAAIndirect, "I[%d] %s: cache write packet 0x%lx sent\n", my_indirect_id, __func__, addr);
    // my_received_responses++;
    int offset_cache_tile_write = CacheTileWrite ? CacheTileWriteCount : 0;
    if (maa->allIndirectPacketsSent(my_indirect_id) && (get_all_received() == get_all_expected() + offset_cache_tile_write)) {
        DPRINTF(MAAIndirect, "I[%d] %s: all responses received, calling execution again in state %s!\n", my_indirect_id, __func__, status_names[(int)state]);
        scheduleNextExecution(true);
    } else {
        DPRINTF(MAAIndirect, "I[%d] %s: expected: %d, received: %d!\n", my_indirect_id, __func__, get_all_expected(), get_all_received());
    }
}


void IndirectAccessUnit::recv_updateTimeHistory(const Addr addr, bool is_block_cached){
    if (is_block_cached) {
        if (LoadsCacheHitRespondingTimeHistory.find(addr) != LoadsCacheHitRespondingTimeHistory.end()) {
            (*maa->stats.IND_LoadsCacheHitRespondingLatency[my_indirect_id]) += maa->getTicksToCycles(curTick() - LoadsCacheHitRespondingTimeHistory[addr]);
            LoadsCacheHitRespondingTimeHistory.erase(addr);
        } else if (LoadsCacheHitAccessingTimeHistory.find(addr) != LoadsCacheHitAccessingTimeHistory.end()) {
            (*maa->stats.IND_LoadsCacheHitAccessingLatency[my_indirect_id]) += maa->getTicksToCycles(curTick() - LoadsCacheHitAccessingTimeHistory[addr]);
            LoadsCacheHitAccessingTimeHistory.erase(addr);
        } else {
            panic("I[%d] %s: addr(0x%lx) is not in the cache hit history!\n", my_indirect_id, __func__, addr);
        }
    } else {
        panic_if(LoadsMemAccessingTimeHistory.find(addr) == LoadsMemAccessingTimeHistory.end(), "I[%d] %s: addr(0x%lx) is not in the memory accessing history!\n", my_indirect_id, __func__, addr);
        (*maa->stats.IND_LoadsMemAccessingLatency[my_indirect_id]) += maa->getTicksToCycles(curTick() - LoadsMemAccessingTimeHistory[addr]);
        LoadsMemAccessingTimeHistory.erase(addr);
    }
}

bool IndirectAccessUnit::recvData(const Addr addr, uint8_t *dataptr, bool is_block_cached) {

    bool was_request_table_full = request_table->is_full();
    std::vector addr_vec = maa->map_addr(addr);
    // int RT_idx = getRowTableIdx(my_RT_config, addr_vec[ADDR_CHANNEL_LEVEL], addr_vec[ADDR_RANK_LEVEL], addr_vec[ADDR_BANKGROUP_LEVEL], addr_vec[ADDR_BANK_LEVEL]);
    // Addr grow_addr = getGrowAddr(my_RT_config, addr_vec[ADDR_BANKGROUP_LEVEL], addr_vec[ADDR_BANK_LEVEL], addr_vec[ADDR_ROW_LEVEL]);
    // bool was_full = false;
    // if (RT_idx == my_RT_idx)
    //     was_full = RT[my_RT_config][RT_idx].is_full();
    std::vector<RequestTableEntry> entries = request_table->get_entries(addr);

    if(my_dst_tile != -1) {
        if(tilewriteunit->recv_data(addr, dataptr, is_block_cached)){
            scheduleNextExecution(true);
            return true;
        }
    }

    if(my_idx_tile != -1) {
        if(tilereadunitIdx->recv_data(addr, dataptr, is_block_cached)){
            scheduleNextExecution(true);
            return true;
        }
    }

    if(my_src_tile != -1) {
        if(tilereadunitSrc->recv_data(addr, dataptr, is_block_cached)){
            scheduleNextExecution(true);
            return true;
        }
    }
    // bool is_full = false;
    // if (RT_idx == my_RT_idx)
    //     is_full = RT[my_RT_config][RT_idx].is_full();
    // DPRINTF(MAAIndirect, "I[%d] %s: %d entries received for addr(0x%lx), grow(x%lx) from T[%d]!\n", my_indirect_id, __func__, entries.size(), addr, grow_addr, RT_idx);
   
    recv_updateTimeHistory(addr, is_block_cached);


    // if(ReadExTile_CAM[addr] == true){
    //     // this is for tile read exclusive
    //     ReadExTile_CAM.erase(addr);
    //     // my_received_responses++;
    //     // if (maa->allIndirectPacketsSent(my_indirect_id) && my_received_responses == my_expected_responses ) {
    //     //     DPRINTF(MAAIndirect, "I[%d] %s: all responses received, calling execution again!\n", my_indirect_id, __func__);
    //     //     scheduleNextExecution(true);
    //     // }
    //     return true;
    // }

    if (entries.size() == 0) {
        panic("Addr:%x Somthing went wrong, at least one entry should be there\n", addr);
        return false;
    }


    uint8_t new_data[block_size];
    uint32_t *dataptr_u32_typed = (uint32_t *)new_data;
    uint64_t *dataptr_u64_typed = (uint64_t *)new_data;
    std::memcpy(new_data, dataptr, block_size);
    int num_recv_spd_read_accesses = 0;
    int num_recv_spd_write_accesses = 0;
    int num_recv_rt_accesses = entries.size();
    for (auto entry : entries) {
        int itr = entry.itr;
        int wid = entry.wid;
        uint64_t srcData = entry.data;
        DPRINTF(MAAIndirect, "I[%d] %s: itr (%d) wid (%d) matched!\n", my_indirect_id, __func__, itr, wid);

        //****************** going to be wrriten in order *****************************
        // if (my_dst_tile != -1) {
        //     if (my_word_size == 4) {
        //         maa->spd->setData<uint32_t>(my_dst_tile, itr, dataptr_u32_typed[wid]);
        //         DPRINTF(MAAIndirect, "I[%d] %s: SPD[%d][%d] = %u/%d/%f!\n", my_indirect_id, __func__, my_dst_tile, itr, ((uint32_t *)new_data)[wid], ((int32_t *)new_data)[wid], ((float *)new_data)[wid]);
        //     } else {
        //         maa->spd->setData<uint64_t>(my_dst_tile, itr, dataptr_u64_typed[wid]);
        //         DPRINTF(MAAIndirect, "I[%d] %s: SPD[%d][%d] = %lu/%ld/%lf!\n", my_indirect_id, __func__, my_dst_tile, itr, ((uint64_t *)new_data)[wid], ((int64_t *)new_data)[wid], ((double *)new_data)[wid]);
        //     }
        //     num_recv_spd_write_accesses++;
        // }

        if (my_dst_tile != -1) {
            if (my_word_size == 4) {
                writeBuffer[itr] = dataptr_u32_typed[wid];
                tilewriteunit->setdata<uint32_t>(dataptr_u32_typed[wid], itr);
                maa->spd->setData<uint32_t>(my_dst_tile, itr, dataptr_u32_typed[wid]);
                DPRINTF(MAAIndirect, "I[%d] %s: SPD_noWrite[%d][%d] = %u/%d/%f!\n", my_indirect_id, __func__, my_dst_tile, itr, ((uint32_t *)new_data)[wid], ((int32_t *)new_data)[wid], ((float *)new_data)[wid]);
            } else {
                writeBuffer[itr] = dataptr_u64_typed[wid];
                tilewriteunit->setdata<uint64_t>(dataptr_u64_typed[wid], itr);
                maa->spd->setData<uint64_t>(my_dst_tile, itr, dataptr_u64_typed[wid]);
                DPRINTF(MAAIndirect, "I[%d] %s: SPD_noWrite[%d][%d] = %lu/%ld/%lf!\n", my_indirect_id, __func__, my_dst_tile, itr, ((uint64_t *)new_data)[wid], ((int64_t *)new_data)[wid], ((double *)new_data)[wid]);
            }
        }

        // sending the signal to write last cache line
        my_received_itr_max = std::max(itr, my_received_itr_max);
        if(my_dst_tile != -1){
            if(((my_max != -1 && my_max == itr+1) || my_max == set_fake_max + 1) && !tilewriteunit->is_last_element_reached()){
                DPRINTF(MAAIndirect, "I[%d] %s itr:%d: marking the last element\n", my_indirect_id, __func__, itr);
                tilewriteunit->mark_last_element_reached();
            }
        }
        

        // // sent the data in order
        // const int words_per_cacheline = block_size/my_word_size;
        // int BufferAccessCount = 0;
        // if (my_dst_tile != -1) {
        //     while(sentmyIQueue.size() > 0 && writeBuffer.find(sentmyIQueue.front()) != writeBuffer.end()){
        //         int my_i_queue = sentmyIQueue.front();
        //         std::cout << "my_i_queue: " << my_i_queue << "\n";
        //         // DPRINTF(MAAIndirect, "I[%d] %s: SPD[%d][%d] = %u/%d/%f!\n", my_indirect_id, __func__, my_dst_tile, my_i_queue, ((uint32_t *)&data_32)[0], ((int32_t *)&data_32)[0], ((float *)&data_32)[0]);
        //         assert(my_i_queue >=0 && my_i_queue < num_tile_elements);
        //         uint32_t data_32 = 0;
        //         uint64_t data_64 = 0;
        //         BufferAccessCount++;
        //         if (my_word_size == 4) {
        //             data_32 = writeBuffer[my_i_queue];
        //             maa->spd->setData<uint32_t>(my_dst_tile, my_i_queue, writeBuffer[my_i_queue]);

        //             DPRINTF(MAAIndirect, "I[%d] %s: SPD[%d][%d] = %u/%d/%f!\n", my_indirect_id, __func__, my_dst_tile, my_i_queue, ((uint32_t *)&data_32)[0], ((int32_t *)&data_32)[0], ((float *)&data_32)[0]);

        //         } else {
        //             data_64 = writeBuffer[my_i_queue];
        //             maa->spd->setData<uint64_t>(my_dst_tile, my_i_queue, writeBuffer[my_i_queue]);

        //             DPRINTF(MAAIndirect, "I[%d] %s: SPD[%d][%d] = %lu/%ld/%lf!\n", my_indirect_id, __func__, my_dst_tile, my_i_queue, ((uint64_t *)&data_64)[0], ((int64_t *)&data_64)[0], ((double *)&data_64)[0]);

        //         }

        //         num_recv_spd_write_accesses++;

        //         int curr_blk_tileWrite = my_i_queue / words_per_cacheline; 
        //         bool movedToNextBlk = (curr_blk_tileWrite != last_blk_tileWrite && last_blk_tileWrite != -1);
        //         if(movedToNextBlk){
        //             // update the TileWriteData after sending the write request 
        //         } else {
        //             // copy the data to the TileWriteData
        //             int offset_cl = my_i_queue % words_per_cacheline;
        //             if(my_word_size == 4){
        //                 memcpy(&TileWriteData[offset_cl*my_word_size], &data_32, sizeof(uint32_t));
        //             } else if(my_word_size == 8){
        //                 memcpy(&TileWriteData[offset_cl*my_word_size], &data_64, sizeof(uint64_t));
        //             } 
        //         }

        //         // handle the last element in the queue
        //         if(!(movedToNextBlk && (sentmyIQueue.size() == 1 && my_fill_finished))){
        //             writeBuffer.erase(my_i_queue);
        //             sentmyIQueue.pop();
        //         }

        //         if(movedToNextBlk || (sentmyIQueue.empty() && my_fill_finished)){
        //             Addr TileOffset = 0;
        //             if(sentmyIQueue.empty() && my_fill_finished){
        //                 TileOffset = my_dst_tile * num_tile_elements * 4 + curr_blk_tileWrite*words_per_cacheline*my_word_size;
        //             } else {
        //                 TileOffset = my_dst_tile * num_tile_elements * 4 + last_blk_tileWrite*words_per_cacheline*my_word_size;
        //             }
        //             Addr vaddr_tileWrite = maa->CacheTiles_address + TileOffset;
        //             assert(vaddr_tileWrite >= maa->CacheTiles_address);
        //             assert(vaddr_tileWrite == addrBlockAligner(vaddr_tileWrite, block_size));
        //             DPRINTF(MAAIndirect, "I[%d] %s: Virtual CacheTileAddress for write is %x\n", my_indirect_id, __func__, vaddr_tileWrite);
        //             Addr addrTileWrite = translatePacket(vaddr_tileWrite, false);
        //             DPRINTF(MAAIndirect, "I[%d] %s: Translated CacheTileAddress is %x\n", my_indirect_id, __func__, addrTileWrite);

        //             RequestPtr TileWrite_req = std::make_shared<Request>(addrTileWrite, block_size, flags, maa->requestorId);
        //             TileWrite_req->setRegion(maa->CacheTiles_rangeID);
        //             PacketPtr writeTile_pkt = new Packet(TileWrite_req, MemCmd::WritebackDirty);
        //             writeTile_pkt->allocate();
        //             writeTile_pkt->setData(TileWriteData);
        //             for (int i = 0; i < block_size / my_word_size; i++) {
        //                 if (my_word_size == 4)
        //                     DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] = %f!\n", my_indirect_id, __func__, i, writeTile_pkt->getPtr<float>()[i]);
        //                 else
        //                     DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] = %f!\n", my_indirect_id, __func__, i, writeTile_pkt->getPtr<double>()[i]);
        //             }
        //             Cycles latency_Tilewrite = Cycles(BufferAccessCount/words_per_cacheline);
        //             DPRINTF(MAAIndirect, "I[%d] %s: Cache Tile created %s to send in %d cycles, my_i:%d\n", my_indirect_id, __func__, writeTile_pkt->print(), latency_Tilewrite, my_i_queue);
        //             CacheTileWriteCount++;
        //             // ReadExTile_CAM[addrTileWrite]  = true;
        //             // my_expected_responses++;
        //             maa->sendPacket(FuncUnitType::INDIRECT, my_indirect_id, writeTile_pkt, maa->getClockEdge(latency_Tilewrite), true);
        //             DPRINTF(MAAIndirect, "I[%d] %s: Cache Tile Returned %s to send in %d cycles, my_i:%d\n", my_indirect_id, __func__, writeTile_pkt->print(), latency_Tilewrite, my_i_queue);
                    

        //         }

        //         if(movedToNextBlk){
        //             int offset_cl = my_i_queue % words_per_cacheline;
        //             if(my_word_size == 4){
        //                 memcpy(&TileWriteData[offset_cl*words_per_cacheline], &data_32, sizeof(uint32_t));
        //             } else if(my_word_size == 8){
        //                 memcpy(&TileWriteData[offset_cl*words_per_cacheline], &data_64, sizeof(uint64_t));
        //             } 
        //         }
        //         last_blk_tileWrite = curr_blk_tileWrite;

        //         // create the packet and send it 

        //     }
        // }

        

        // ******************************************************
        switch (my_instruction->opcode) {
        case Instruction::OpcodeType::INDIR_LD: {
            assert(my_dst_tile != -1);
            break;
        }
        case Instruction::OpcodeType::INDIR_ST_VECTOR: {
            if (my_word_size == 4) {
                ((uint32_t *)new_data)[wid] = castuint64_t<uint32_t> (srcData); //maa->spd->getData<uint32_t>(my_src_tile, itr);
                DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] = SPD[%d][%d] = %u/%d/%f!\n", my_indirect_id, __func__, wid, my_src_tile, itr, ((uint32_t *)new_data)[wid], ((int32_t *)new_data)[wid], ((float *)new_data)[wid]);
            } else {
                ((uint64_t *)new_data)[wid] = srcData; //maa->spd->getData<uint64_t>(my_src_tile, itr);
                DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] = SPD[%d][%d] = %lu/%ld/%lf!\n", my_indirect_id, __func__, wid, my_src_tile, itr, ((uint64_t *)new_data)[wid], ((int64_t *)new_data)[wid], ((double *)new_data)[wid]);
            }
            num_recv_spd_read_accesses++;
            break;
        }
        case Instruction::OpcodeType::INDIR_ST_SCALAR: {
            if (my_word_size == 4) {
                ((uint32_t *)new_data)[wid] = maa->rf->getData<uint32_t>(my_src_reg);
            } else {
                ((uint64_t *)new_data)[wid] = maa->rf->getData<uint64_t>(my_src_reg);
            }
            break;
        }
        case Instruction::OpcodeType::INDIR_RMW_VECTOR: {
            switch (my_instruction->datatype) {
            case Instruction::DataType::UINT32_TYPE: {
                uint32_t word_data = castuint64_t<uint32_t> (srcData); //maa->spd->getData<uint32_t>( my_src_tile, itr);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%u) += SPD[%d][%d] (%u) = %u!\n",
                            my_indirect_id, __func__, wid, ((uint32_t *)new_data)[wid], my_src_tile, itr, word_data, ((uint32_t *)new_data)[wid] + word_data);
                    ((uint32_t *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((uint32_t *)new_data)[wid] = ((uint32_t *)new_data)[wid] < word_data ? ((uint32_t *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((uint32_t *)new_data)[wid] = ((uint32_t *)new_data)[wid] > word_data ? ((uint32_t *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::INT32_TYPE: {
                int32_t word_data = castuint64_t<int32_t> (srcData); //maa->spd->getData<int32_t>(my_src_tile, itr);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%d) += SPD[%d][%d] (%d) = %d!\n",
                            my_indirect_id, __func__, wid, ((int32_t *)new_data)[wid], my_src_tile, itr, word_data, ((int32_t *)new_data)[wid] + word_data);
                    ((int32_t *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((int32_t *)new_data)[wid] = ((int32_t *)new_data)[wid] < word_data ? ((int32_t *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((int32_t *)new_data)[wid] = ((int32_t *)new_data)[wid] > word_data ? ((int32_t *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::FLOAT32_TYPE: {
                float word_data = castuint64_t<float> (srcData); //maa->spd->getData<float>(my_src_tile, itr);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%f) += SPD[%d][%d] (%f) = %f!\n",
                            my_indirect_id, __func__, wid, ((float *)new_data)[wid], my_src_tile, itr, word_data, ((float *)new_data)[wid] + word_data);
                    ((float *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((float *)new_data)[wid] = ((float *)new_data)[wid] < word_data ? ((float *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((float *)new_data)[wid] = ((float *)new_data)[wid] > word_data ? ((float *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::UINT64_TYPE: {
                uint64_t word_data = castuint64_t<uint64_t> (srcData);
                // uint64_t word_data = maa->spd->getData<uint64_t>(my_src_tile, itr);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%lu) += SPD[%d][%d] (%lu) = %lu!\n",
                            my_indirect_id, __func__, wid, ((uint64_t *)new_data)[wid], my_src_tile, itr, word_data, ((uint64_t *)new_data)[wid] + word_data);
                    ((uint64_t *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((uint64_t *)new_data)[wid] = ((uint64_t *)new_data)[wid] < word_data ? ((uint64_t *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((uint64_t *)new_data)[wid] = ((uint64_t *)new_data)[wid] > word_data ? ((uint64_t *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::INT64_TYPE: {
                int64_t word_data = castuint64_t<int64_t> (srcData);
                // int64_t word_data = maa->spd->getData<int64_t>(my_src_tile, itr);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%ld) += SPD[%d][%d] (%ld) = %ld!\n",
                            my_indirect_id, __func__, wid, ((int64_t *)new_data)[wid], my_src_tile, itr, word_data, ((int64_t *)new_data)[wid] + word_data);
                    ((int64_t *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((int64_t *)new_data)[wid] = ((int64_t *)new_data)[wid] < word_data ? ((int64_t *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((int64_t *)new_data)[wid] = ((int64_t *)new_data)[wid] > word_data ? ((int64_t *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::FLOAT64_TYPE: {
                double word_data = castuint64_t<double> (srcData);
                // double word_data = maa->spd->getData<double>(my_src_tile, itr);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%lf) += SPD[%d][%d] (%lf) = %lf!\n",
                            my_indirect_id, __func__, wid, ((double *)new_data)[wid], my_src_tile, itr, word_data, ((double *)new_data)[wid] + word_data);
                    ((double *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((double *)new_data)[wid] = ((double *)new_data)[wid] < word_data ? ((double *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((double *)new_data)[wid] = ((double *)new_data)[wid] > word_data ? ((double *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            default:
                assert(false);
            }
            break;
        }
        case Instruction::OpcodeType::INDIR_RMW_SCALAR: {
            switch (my_instruction->datatype) {
            case Instruction::DataType::UINT32_TYPE: {
                uint32_t word_data = maa->rf->getData<uint32_t>(my_src_reg);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%u) += RF[%d] (%u) = %u!\n",
                            my_indirect_id, __func__, wid, ((uint32_t *)new_data)[wid], my_src_reg, word_data, ((uint32_t *)new_data)[wid] + word_data);
                    ((uint32_t *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((uint32_t *)new_data)[wid] = ((uint32_t *)new_data)[wid] < word_data ? ((uint32_t *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((uint32_t *)new_data)[wid] = ((uint32_t *)new_data)[wid] > word_data ? ((uint32_t *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::INT32_TYPE: {
                int32_t word_data = maa->rf->getData<int32_t>(my_src_reg);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%d) += RF[%d] (%d) = %d!\n",
                            my_indirect_id, __func__, wid, ((int32_t *)new_data)[wid], my_src_reg, word_data, ((int32_t *)new_data)[wid] + word_data);
                    ((int32_t *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((int32_t *)new_data)[wid] = ((int32_t *)new_data)[wid] < word_data ? ((int32_t *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((int32_t *)new_data)[wid] = ((int32_t *)new_data)[wid] > word_data ? ((int32_t *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::FLOAT32_TYPE: {
                float word_data = maa->rf->getData<float>(my_src_reg);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%f) += RF[%d] (%f) = %f!\n",
                            my_indirect_id, __func__, wid, ((float *)new_data)[wid], my_src_reg, word_data, ((float *)new_data)[wid] + word_data);
                    ((float *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((float *)new_data)[wid] = ((float *)new_data)[wid] < word_data ? ((float *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((float *)new_data)[wid] = ((float *)new_data)[wid] > word_data ? ((float *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::UINT64_TYPE: {
                uint64_t word_data = maa->rf->getData<uint64_t>(my_src_reg);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%lu) += RF[%d] (%lu) = %lu!\n",
                            my_indirect_id, __func__, wid, ((uint64_t *)new_data)[wid], my_src_reg, word_data, ((uint64_t *)new_data)[wid] + word_data);
                    ((uint64_t *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((uint64_t *)new_data)[wid] = ((uint64_t *)new_data)[wid] < word_data ? ((uint64_t *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((uint64_t *)new_data)[wid] = ((uint64_t *)new_data)[wid] > word_data ? ((uint64_t *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::INT64_TYPE: {
                int64_t word_data = maa->rf->getData<int64_t>(my_src_reg);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%ld) += RF[%d] (%ld) = %ld!\n",
                            my_indirect_id, __func__, wid, ((int64_t *)new_data)[wid], my_src_reg, word_data, ((int64_t *)new_data)[wid] + word_data);
                    ((int64_t *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((int64_t *)new_data)[wid] = ((int64_t *)new_data)[wid] < word_data ? ((int64_t *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((int64_t *)new_data)[wid] = ((int64_t *)new_data)[wid] > word_data ? ((int64_t *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            case Instruction::DataType::FLOAT64_TYPE: {
                double word_data = maa->rf->getData<double>(my_src_reg);
                if (my_instruction->optype == Instruction::OPType::ADD_OP) {
                    DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] (%lf) += RF[%d] (%lf) = %lf!\n",
                            my_indirect_id, __func__, wid, ((double *)new_data)[wid], my_src_reg, word_data, ((double *)new_data)[wid] + word_data);
                    ((double *)new_data)[wid] += word_data;
                } else if (my_instruction->optype == Instruction::OPType::MIN_OP) {
                    ((double *)new_data)[wid] = ((double *)new_data)[wid] < word_data ? ((double *)new_data)[wid] : word_data;
                } else if (my_instruction->optype == Instruction::OPType::MAX_OP) {
                    ((double *)new_data)[wid] = ((double *)new_data)[wid] > word_data ? ((double *)new_data)[wid] : word_data;
                } else {
                    panic_if(true, "I[%d] %s: unknown optype %s!\n", my_indirect_id, __func__, my_instruction->print());
                }
                break;
            }
            default:
                assert(false);
            }
            break;
        }
        default:
            assert(false);
        }
    }

    // Row table parallelism = total #banks.
    // We will have total #banks offset table walkers.
    Cycles total_latency = updateLatency(num_recv_spd_read_accesses, 0, num_recv_spd_write_accesses, num_recv_rt_accesses);
    if (my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_VECTOR ||
         my_instruction->opcode == Instruction::OpcodeType::INDIR_ST_SCALAR || 
         my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_VECTOR || 
         my_instruction->opcode == Instruction::OpcodeType::INDIR_RMW_SCALAR
         ) {
        my_received_responses++;
        RequestPtr real_req = std::make_shared<Request>(addr, block_size, flags, maa->requestorId);
        real_req->setRegion(my_addr_range_id);
        PacketPtr write_pkt = new Packet(real_req, MemCmd::WritebackDirty);
        write_pkt->allocate();
        write_pkt->setData(new_data);
        for (int i = 0; i < block_size / my_word_size; i++) {
            if (my_word_size == 4)
                DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] = %f!\n", my_indirect_id, __func__, i, write_pkt->getPtr<float>()[i]);
            else
                DPRINTF(MAAIndirect, "I[%d] %s: new_data[%d] = %f!\n", my_indirect_id, __func__, i, write_pkt->getPtr<double>()[i]);
        }
        DPRINTF(MAAIndirect, "I[%d] %s: created %s to send in %d cycles\n", my_indirect_id, __func__, write_pkt->print(), total_latency);
        // forcing through cache 
        maa->sendPacket(FuncUnitType::INDIRECT, my_indirect_id, write_pkt, maa->getClockEdge(total_latency), true);
        (*maa->stats.IND_StoresMemAccessing[my_indirect_id])++;
    } else  {
        my_received_responses++;
        int offset_cache_tile_write = CacheTileWrite ? CacheTileWriteCount : 0;
        if (maa->allIndirectPacketsSent(my_indirect_id) && get_all_received() == get_all_expected() + offset_cache_tile_write) {
            DPRINTF(MAAIndirect, "I[%d] %s: all responses received, calling execution again!\n", my_indirect_id, __func__);
            scheduleNextExecution(true);
        } else {
            DPRINTF(MAAIndirect, "I[%d] %s: expected: %d, received: %d responses!\n", my_indirect_id, __func__, get_all_expected(), get_all_received());
        }
    }
    if (was_request_table_full) {
        DPRINTF(MAAIndirect, "I[%d] %s: was full, now not full, calling execution again!\n", my_indirect_id, __func__);
        panic_if(state != Status::Request && state != Status::Fill, "I[%d] %s: state is %s!\n", my_indirect_id, __func__, status_names[(int)state]);
        scheduleNextExecution(true);
    }

    return true;
}


Addr IndirectAccessUnit::translatePacket(Addr vaddr, bool is_load) {
    /**** Address translation ****/
    RequestPtr translation_req = std::make_shared<Request>(vaddr, block_size, flags, maa->requestorId, my_instruction->PC, my_instruction->CID);
    ThreadContext *tc = maa->system->threads[my_instruction->CID];
    maa->mmu->translateTiming(translation_req, tc, this, is_load ? BaseMMU::Read : BaseMMU::Write);
    // The above function immediately does the translation and calls the finish function
    assert(my_translation_done);
    my_translation_done = false;
    return my_translated_addr;
}
void IndirectAccessUnit::finish(const Fault &fault, const RequestPtr &req, ThreadContext *tc, BaseMMU::Mode mode) {
    panic_if(fault != NoFault, "I[%d] %s: fault for request 0x%lx!\n", my_indirect_id, __func__, req->getVaddr());
    assert(my_translation_done == false);
    my_translation_done = true;
    my_translated_addr = req->getPaddr();
}
void IndirectAccessUnit::setInstruction(Instruction *_instruction) {
    assert(my_instruction == nullptr);
    my_instruction = _instruction;
}
void IndirectAccessUnit::scheduleExecuteInstructionEvent(int latency) {
    DPRINTF(MAAIndirect, "I[%d] %s: scheduling execute for the IndirectAccess Unit in the next %d cycles!\n", my_indirect_id, __func__, latency);
    panic_if(latency < 0, "Negative latency of %d!\n", latency);
    Tick new_when = maa->getClockEdge(Cycles(latency));
    if (!executeInstructionEvent.scheduled()) {
        maa->schedule(executeInstructionEvent, new_when);
    } else {
        Tick old_when = executeInstructionEvent.when();
        DPRINTF(MAAIndirect, "I[%d] %s: execution already scheduled for tick %d\n", my_indirect_id, __func__, old_when);
        if (new_when < old_when) {
            DPRINTF(MAAIndirect, "I[%d] %s: rescheduling for tick %d!\n", my_indirect_id, __func__, new_when);
            maa->reschedule(executeInstructionEvent, new_when);
        }
    }
}
} // namespace gem5