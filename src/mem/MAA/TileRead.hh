#ifndef __MEM_MAA_TILE_READ_HH__
#define __MEM_MAA_TILE_READ_HH__

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
#include "debug/MAATileRead.hh"

namespace gem5 {

class MAA;
class IndirectAccessUnit;
class Instruction;


// enum TileWriteMainUnit {
//     StreamUnit_enum,
//     IndirectUnit_enum,
//     ALUUnit_enum,
//     RangeFuserUnit_enum
// };

struct TileReadReqMeta {
    bool ReadExSent = false;
    bool ReadExRecv = false;
    uint8_t data[64];
    uint8_t count = 0;
}; 

class TileRead : public BaseMMU::Translation {
    MAA* maa;
    int TileID;
    uint32_t TileSize, wordsize;
    ContextID CID;
    Addr PC;
    uint32_t block_size;
    uint32_t words_per_block;

    
    int expected_response, received_response;
    int my_max;

    const int blockSizeReqs = 400;
    int my_indirect_id = 0;

    bool my_translation_done;
    Addr my_translated_addr;
    Request::Flags flags = 0;

    uint32_t ReadEx_current, write_current;

    std::map<Addr, struct TileReadReqMeta> CAM;
    std::vector<int>& tile_write_counter;
    FuncUnitType funcUnit;

    bool last_elemet_set;
    int pop_data_counter;
    bool ready_to_req;
 
    // int &src_data_counter;




    public: 
        TileRead(MAA *_maa, int &my_max, std::vector<int>& tile_write_counter, FuncUnitType funcUnit);

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

        template<typename T> bool getData(T& data, bool remove){

                int blk_counter_id = pop_data_counter/words_per_block *words_per_block;
                Addr v_block_addr = getVirtualAddress(blk_counter_id);
                Addr p_block_addr = translatePacket(v_block_addr);
                struct TileReadReqMeta twrm;
                // if the entry is already there

                // check for the last block
                // int last_i = (my_max / words_per_block) * words_per_block;
                // int last_word_count = my_max % words_per_block + 1;
                if(CAM.find(p_block_addr) != CAM.end()){
                    twrm = CAM[p_block_addr];
                    if(twrm.ReadExRecv) { //  
                        int offset = pop_data_counter % words_per_block;
                        data = *(T*)(&twrm.data[offset*wordsize]);
                        // memcpy((void*) data, &twrm.data[0], wordsize);
                        if((offset == words_per_block-1 || pop_data_counter >= tile_write_counter[TileID]) && remove){
                            CAM.erase(p_block_addr);
                        }
                        bool ret;
                        if(pop_data_counter < tile_write_counter[TileID]){
                            ret = true;
                        } else {
                            ret = false;
                        }
                        if(remove){
                            pop_data_counter += 1;
                        }
                        DPRINTF(MAATileRead, "TR[%d] %s %s TileId:%d i:%d : data was there my_max:%d\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], TileID, pop_data_counter, my_max);
                        return ret;
                       
                    } else {
                        DPRINTF(MAATileRead, "TR[%d] %s %s TileId:%d i:%d : Data has not been received my_max:%d\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], TileID, pop_data_counter, my_max);
                        return false;
                    }
                } else {
                    DPRINTF(MAATileRead, "TR[%d] %s %s TileID:%d i:%d : No entries in the table my_max:%d\n", my_indirect_id, __func__, func_unit_names[static_cast<int>(funcUnit)], TileID, pop_data_counter, my_max);
                    return false;
                }
            };

            void unset_ready_to_req();
    };

}






#endif //__MEM_MAA_TILE_READ_HH__