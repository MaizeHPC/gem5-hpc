#include "mem/MAA/TileRead.hh"
#include "mem/MAA/IndirectAccess1.hh"
#include "mem/MAA/Tables.hh"
#include "base/logging.hh"
#include "mem/MAA/MAA.hh"
#include "mem/MAA/SPD.hh"
#include "mem/MAA/IF.hh"
#include "base/trace.hh"
#include "base/types.hh"
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

    TileRead::TileRead(MAA *_maa, int &my_max, std::vector<int>& _tile_write_counter,  FuncUnitType _funcUnit) : maa(_maa), 
                tile_write_counter(_tile_write_counter), funcUnit(_funcUnit){
        block_size = 64;
        TileSize = 16384;
        my_translation_done = false;
        ready_to_req = false;
    };

    void TileRead::set(int _TileID, uint32_t _wordsize,  ContextID _CID, Addr _PC, 
        uint32_t _block_size){
        TileID = _TileID;
        assert(TileID >= 0 && TileID<= 32);

        wordsize = _wordsize;
        CID = _CID;
        PC = _PC;
        block_size = _block_size;
        // TileSize = _TileSize;
        words_per_block = block_size/wordsize;
        DPRINTF(MAATileRead, "TR[%d] %s %s: words_per_block is %x\n", my_indirect_id, __func__,func_unit_names[static_cast<int>(funcUnit)], words_per_block);
        DPRINTF(MAATileRead, "TR[%d] %s %s: wordsize %x\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)],  wordsize);

        ReadEx_current = 0;
        write_current = 0;
        my_max = 0;
        pop_data_counter = 0;
        last_elemet_set = false;
        ready_to_req = true;
        CAM.clear();
    }

    Addr TileRead::getVirtualAddress(int element_id){
        const int TileSize = 16384;
        assert(TileID >= 0 && TileID<= 32);
        return maa->CacheTiles_address + TileID*TileSize*4 + element_id * wordsize;
    }

    Addr TileRead::translatePacket(Addr vaddr){
        RequestPtr translation_req = std::make_shared<Request>(vaddr, block_size, flags, maa->requestorId, PC, CID);
        ThreadContext *tc = maa->system->threads[CID];
        bool is_load = true;
        maa->mmu->translateTiming(translation_req, tc, this, is_load ? BaseMMU::Read : BaseMMU::Write);
        // The above function immediately does the translation and calls the finish function
        assert(my_translation_done);
        my_translation_done = false;
        return my_translated_addr;
    }

    void TileRead::finish(const Fault &fault, const RequestPtr &req, ThreadContext *tc, BaseMMU::Mode mode) {
        panic_if(fault != NoFault, " %s: fault for request 0x%lx!\n", __func__, req->getVaddr());
        assert(my_translation_done == false);
        my_translation_done = true;
        my_translated_addr = req->getPaddr();
    }

    void TileRead::mark_last_element_reached(){
        last_elemet_set =  true;
    }

    void TileRead::createAndSendTileExReads(int reqs_count){
        // tile_write_counter[TileID]
        if(ready_to_req){
            DPRINTF(MAATileRead, "TR[%d] %s %s: i=%d Write count is %d\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)],  ReadEx_current, tile_write_counter[TileID]);
        }
        for(int i = ReadEx_current; (i < TileSize) && (i < ReadEx_current + reqs_count*words_per_block) && (i <  tile_write_counter[TileID]) && ready_to_req; i += words_per_block){ // && i < target_tile_ready_counter
            Addr v_block_addr = getVirtualAddress(i);
            DPRINTF(MAATileRead, "TR[%d] %s %s: Virtual Cache Tile Address for write is %x\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)],  v_block_addr);
            Addr p_block_addr = translatePacket(v_block_addr);

            RequestPtr readex_req = std::make_shared<Request>(p_block_addr, block_size, flags, maa->requestorId);
            struct TileReadReqMeta twrm;
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
            DPRINTF(MAATileRead, "TR[%d] %s: created %s for mem\n", my_indirect_id, __func__, readex_pkt->print());
            maa->sendPacket(funcUnit, my_indirect_id, readex_pkt, maa->getClockEdge(Cycles(i-ReadEx_current + 1)), true);

            ReadEx_current = ReadEx_current + words_per_block;
        }
        
    }


    bool TileRead::recv_data(const Addr addr, uint8_t *dataptr, bool is_block_cached){
        bool ret = false;
        DPRINTF(MAATileRead, "TR[%d] %s %s: received response for addr: %x \n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], addr);
        if(CAM.find(addr) != CAM.end()){
            received_response++;
            DPRINTF(MAATileRead, "TR[%d] %s: found the entry on CAM for addr: %x \n", my_indirect_id, __func__, addr);
            if(CAM[addr].ReadExSent){
                DPRINTF(MAATileRead, "TR[%d] %s: ReadEx response addr: %x \n", my_indirect_id, __func__, addr);
                struct TileReadReqMeta twrm = CAM[addr];
                twrm.ReadExRecv = true;
                memcpy(&twrm.data[0], dataptr, 64);
                CAM[addr] = twrm;
                ret = true;
                if(funcUnit == FuncUnitType::INDIRECT){
                    maa->indirectAccessUnits[0].recv_updateTimeHistory(addr, is_block_cached);
                }
                CAM[addr].ReadExSent = false;
                // createAndSendTileExReads(1);
            } else {
                panic("Unexpected packet has been received\n");
            }
        }
        return ret;
    }

    void TileRead::unset_ready_to_req(){
        ready_to_req = false;
    }

}