#ifndef __MEM_MAA_TILE_WRITE_HH__
#define __MEM_MAA_TILE_WRITE_HH__

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <queue>
#include <set>

#include "base/statistics.hh"
#include "base/types.hh"
#include "base/debug.hh"
#include "base/compiler.hh" 
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/system.hh"
#include "arch/generic/mmu.hh"
#include "mem/MAA/Tables.hh"
#include "mem/MAA/IF.hh"

namespace gem5 {

class MAA;
class IndirectAccessUnit;
class Instruction;

struct TileWriteReqMeta {
    bool ReadExSent = false;
    bool ReadExRecv = false;
    bool writeDataReady = false;
    bool WriteReqSent = false;
    uint8_t data[64];
    uint8_t count = 0;
}; 

// enum TileWriteMainUnit {
//     StreamUnit_enum,
//     IndirectUnit_enum,
//     ALUUnit_enum,
//     RangeFuserUnit_enum
// };

class TileWrite : public BaseMMU::Translation {
    MAA* maa;
    int TileID;
    uint32_t TileSize, wordsize;
    ContextID CID;
    Addr PC;
    uint32_t block_size;
    uint32_t words_per_block;

    
    int &expected_response, &received_response;
    int my_max;

    const int blockSizeReqs = 400;
    int my_indirect_id = 0;

    bool my_translation_done;
    Addr my_translated_addr;
    Request::Flags flags = 0;

    uint32_t ReadEx_current, write_current;

    std::map<Addr, struct TileWriteReqMeta> CAM;
    FuncUnitType funcUnit;

    bool last_elemet_set;




    public: 
        TileWrite(MAA *_maa, int &expected_response, int &received_response, int &my_max, 
            FuncUnitType funcUnit);

        void set(int _TileID, uint32_t _wordsize, ContextID _CID, Addr _PC, 
                uint32_t _block_size);

        Addr getVirtualAddress(int element_id);
        Addr translatePacket(Addr vaddr);

        void finish(const Fault &fault, const RequestPtr &req, ThreadContext *tc, BaseMMU::Mode mode) ;

        void createAndSendTileExReads(int reqs_count);
        bool recv_data(const Addr addr, uint8_t *dataptr, bool is_block_cached);

        uint32_t write_tile_data();
        void markDelayed() override {};
        void mark_last_element_reached();

        template <typename T>
        void setdata(T data, int element_id){
            struct TileWriteReqMeta twrm;
            // check if the entry already exisits 
            uint32_t block_element_id = (element_id/words_per_block) * words_per_block;
            Addr v_block_addr_id = getVirtualAddress(block_element_id);
            Addr p_block_addr = translatePacket(v_block_addr_id);

            if(CAM.find(p_block_addr) != CAM.end()){
                twrm = CAM[p_block_addr];
            } else {
                // create an entry
                CAM[p_block_addr] = twrm;
            }

            // copy the data and update the count 
            uint8_t offset_wid = (element_id % words_per_block) * wordsize;
            // set the data 
            memcpy(&twrm.data[offset_wid], &data, wordsize);
            twrm.count++;

            // update entry 
            CAM[p_block_addr] = twrm;
            write_tile_data();
            my_max = std::max(my_max, element_id);

        }

};



}


#endif //__MEM_MAA_TILE_WRITE_HH__