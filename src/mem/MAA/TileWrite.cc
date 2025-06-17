#include "mem/MAA/TileWrite.hh"
#include "mem/MAA/IndirectAccess1.hh"
#include "mem/MAA/Tables.hh"
#include "base/logging.hh"
#include "mem/MAA/MAA.hh"
#include "mem/MAA/SPD.hh"
#include "mem/MAA/IF.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "debug/MAATileWrite.hh"
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

    TileWrite::TileWrite(MAA *_maa, int &expected_response, int &received_response, 
            int &my_max, std::vector<int>& tile_write_counter, FuncUnitType _funcUnit) : maa(_maa), 
            expected_response(expected_response), received_response(received_response), tile_write_counter(tile_write_counter), funcUnit(_funcUnit){
        block_size = 64;
        TileSize = 16384;
        my_translation_done = false;
    };

    void TileWrite::set(int _TileID, uint32_t _wordsize, ContextID _CID, Addr _PC, 
        uint32_t _block_size){
        TileID = _TileID;
        assert(TileID >= 0 && TileID<= 32);

        wordsize = _wordsize;
        CID = _CID;
        PC = _PC;
        block_size = _block_size;
        // TileSize = _TileSize;
        words_per_block = block_size/wordsize;
        DPRINTF(MAATileWrite, "TW[%d] %s %s: words_per_block is %x\n", my_indirect_id, __func__,func_unit_names[static_cast<int>(funcUnit)], words_per_block);
        DPRINTF(MAATileWrite, "TW[%d] %s %s: wordsize %x\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)],  wordsize);

        ReadEx_current = 0;
        write_current = 0;
        my_max = 0;
        tile_write_counter[TileID] = 0;
        last_elemet_set = false;
        CAM.clear();
    }

    Addr TileWrite::getVirtualAddress(int element_id){
        const int TileSize = 16384;
        assert(TileID >= 0 && TileID<= 32);
        return maa->CacheTiles_address + TileID*TileSize*4 + element_id * wordsize;
    }

    Addr TileWrite::translatePacket(Addr vaddr){
        RequestPtr translation_req = std::make_shared<Request>(vaddr, block_size, flags, maa->requestorId, PC, CID);
        ThreadContext *tc = maa->system->threads[CID];
        bool is_load = true;
        maa->mmu->translateTiming(translation_req, tc, this, is_load ? BaseMMU::Read : BaseMMU::Write);
        // The above function immediately does the translation and calls the finish function
        assert(my_translation_done);
        my_translation_done = false;
        return my_translated_addr;
    }

    void TileWrite::finish(const Fault &fault, const RequestPtr &req, ThreadContext *tc, BaseMMU::Mode mode) {
        panic_if(fault != NoFault, " %s: fault for request 0x%lx!\n", __func__, req->getVaddr());
        assert(my_translation_done == false);
        my_translation_done = true;
        my_translated_addr = req->getPaddr();
    }

    void TileWrite::mark_last_element_reached(){
        last_elemet_set =  true;
        write_tile_data();
    }

    void TileWrite::createAndSendTileExReads(int reqs_count){
        for(int i = ReadEx_current; i < TileSize && i < ReadEx_current + reqs_count*words_per_block; i += words_per_block){
            Addr v_block_addr = getVirtualAddress(i);
            DPRINTF(MAATileWrite, "TW[%d] %s %s: i=%d Virtual Cache Tile Address for write is %x\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], i ,  v_block_addr);
            Addr p_block_addr = translatePacket(v_block_addr);

            RequestPtr readex_req = std::make_shared<Request>(p_block_addr, block_size, flags, maa->requestorId);
            struct TileWriteReqMeta twrm;
            // if the entry is already there
            if(CAM.find(p_block_addr) != CAM.end()){
                twrm = CAM[p_block_addr];
            }
            twrm.ReadExSent = true;
            CAM[p_block_addr]  = twrm; // this will be replaced with an address range check
            readex_req->setRegion(maa->CacheTiles_rangeID);
            PacketPtr readex_pkt;
            readex_pkt = new Packet(readex_req, MemCmd::ReadExReq);
            readex_pkt->headerDelay = readex_pkt->payloadDelay = 0;
            readex_pkt->allocate();
            expected_response++;
            maa->sendPacket(funcUnit, my_indirect_id, readex_pkt, maa->getClockEdge(Cycles(i-ReadEx_current + 1)), true);
            DPRINTF(MAATileWrite, "TW[%d] %s %s: i=%d created %s for mem\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], i, readex_pkt->print());
        }
        ReadEx_current = ReadEx_current + reqs_count*words_per_block;
    }

    // void TileWrite::setdata(uint64_t data, int element_id){
    //     struct TileWriteReqMeta twrm;
    //     // check if the entry already exisits 
    //     uint32_t block_element_id = (element_id/words_per_block) * words_per_block;
    //     Addr v_block_addr_id = getVirtualAddress(block_element_id);
    //     Addr p_block_addr = translatePacket(v_block_addr_id);

    //     if(CAM.find(p_block_addr) != CAM.end()){
    //         twrm = CAM[p_block_addr];
    //     } else {
    //         // create an entry
    //         CAM[p_block_addr] = twrm;
    //     }

    //     // copy the data and update the count 
    //     uint8_t offset_wid = (element_id % words_per_block) * wordsize;
    //     // set the data 
    //     memcpy(&twrm.data[offset_wid], &data, wordsize);
    //     twrm.count++;

    //     // update entry 
    //     CAM[p_block_addr] = twrm;
    //     write_tile_data();
    //     my_max = std::max(my_max, element_id);

    // }

    bool TileWrite::recv_data(const Addr addr, uint8_t *dataptr, bool is_block_cached){
        bool ret = false;
        DPRINTF(MAATileWrite, "TW[%d] %s %s: received response for addr: %x \n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], addr);
        if(CAM.find(addr) != CAM.end()){
            received_response++;
            DPRINTF(MAATileWrite, "TW[%d] %s: found the entry on CAM for addr: %x \n", my_indirect_id, __func__, addr);
            if(CAM[addr].ReadExSent){
                DPRINTF(MAATileWrite, "TW[%d] %s: ReadEx response addr: %x \n", my_indirect_id, __func__, addr);
                struct TileWriteReqMeta twrm = CAM[addr];
                twrm.ReadExRecv = true;
                CAM[addr] = twrm;
                ret = true;
                write_tile_data();
                if(funcUnit == FuncUnitType::INDIRECT){
                    maa->indirectAccessUnits[0].recv_updateTimeHistory(addr, is_block_cached);
                }
                CAM[addr].ReadExSent = false;
                createAndSendTileExReads(1);
            } else if(CAM[addr].WriteReqSent){
                DPRINTF(MAATileWrite, "TW[%d] %s: WriteReq response addr: %x \n", my_indirect_id, __func__, addr);
                write_tile_data();
                ret = true;
                CAM.erase(addr);
            } else {
                panic("Unexpected packet has been received\n");
            }
        }
        return ret;
    }

    uint32_t TileWrite::write_tile_data(){
        int count = 0;
        DPRINTF(MAATileWrite, "TW[%d] %s %s: trying to write a tile data, my_max:%d\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], my_max);
        std::cout << "words_per_block: " << words_per_block << "\n" << std::flush;
        int bound_max = (my_max/words_per_block + 1) * words_per_block;
        bound_max = (bound_max > TileSize) ? TileSize : bound_max;
        std::cout << "I am here\n" << std::flush;
        for(int i = write_current; i < bound_max; i += words_per_block){
            Addr v_block_addr = getVirtualAddress(i);
            Addr p_block_addr = translatePacket(v_block_addr);
            struct TileWriteReqMeta twrm;
            // if the entry is already there

            // check for the last block
            int last_i = (my_max / words_per_block) * words_per_block;
            int last_word_count = my_max % words_per_block + 1;

            if(CAM.find(p_block_addr) != CAM.end()){
                twrm = CAM[p_block_addr];
                if((twrm.count == words_per_block || (last_elemet_set && i == last_i && twrm.count == last_word_count)) && twrm.ReadExRecv) { //  
                    // create the packet and write it 
                    RequestPtr TileWrite_req = std::make_shared<Request>(p_block_addr, block_size, flags, maa->requestorId);
                    TileWrite_req->setRegion(maa->CacheTiles_rangeID);
                    PacketPtr writeTile_pkt = new Packet(TileWrite_req, MemCmd::WriteReq);
                    writeTile_pkt->allocate();
                    writeTile_pkt->setData(&twrm.data[0]);
                    Cycles latency_Tilewrite = Cycles(i-write_current + 1);
                    expected_response++;
                    DPRINTF(MAATileWrite, "TW[%d] %s %s i:%d : Sending write back dirty packet is %s\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], i,  writeTile_pkt->print());
                    maa->sendPacket(funcUnit, my_indirect_id, writeTile_pkt, maa->getClockEdge(latency_Tilewrite), true);
                    count++;
                    twrm.WriteReqSent = true;
                    CAM[p_block_addr] = twrm;
                    tile_write_counter[TileID] += twrm.count;
                    // CAM.erase(p_block_addr);

                } else {
                    DPRINTF(MAATileWrite, "TW[%d] %s %s i:%d : entry for %d, twrm.count:%d, twrm.ReadExRecv: %d my_max:%d\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], i,  p_block_addr, twrm.count, twrm.ReadExRecv, my_max);
                    break;
                }
            } else {
                DPRINTF(MAATileWrite, "TW[%d] %s %s i:%d: entry for %d hasn't been created, my_max:%d\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], i, p_block_addr, my_max);
                break;
            }
        }
        write_current += count*words_per_block;
        return count;
    }


}