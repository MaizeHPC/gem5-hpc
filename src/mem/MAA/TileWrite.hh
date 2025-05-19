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
    int &my_max;

    const int blockSizeReqs = 400;
    int my_indirect_id = 0;

    bool my_translation_done;
    Addr my_translated_addr;
    Request::Flags flags = 0;

    uint32_t ReadEx_current, write_current;

    std::map<Addr, struct TileWriteReqMeta> CAM;
    FuncUnitType funcUnit;




    public: 
        TileWrite(MAA *_maa, int &expected_response, int &received_response, int &my_max, 
            FuncUnitType funcUnit);

        void set(int _TileID, uint32_t _wordsize, ContextID _CID, Addr _PC, 
                uint32_t _block_size, uint32_t _TileSize);

        Addr getVirtualAddress(int element_id);
        Addr translatePacket(Addr vaddr);

        void finish(const Fault &fault, const RequestPtr &req, ThreadContext *tc, BaseMMU::Mode mode) ;

        void createAndSendTileExReads(int reqs_count);
        bool recv_data(const Addr addr, uint8_t *dataptr, bool is_block_cached);
        void setdata(uint64_t data, int element_id);
        uint32_t write_tile_data();
        void markDelayed() override {};

};



}


#endif //__MEM_MAA_TILE_WRITE_HH__