#ifndef __MEM_MAA_STREAMACCESS_HH__
#define __MEM_MAA_STREAMACCESS_HH__

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

#include "base/types.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/system.hh"
#include "arch/generic/mmu.hh"
#include "mem/MAA/IF.hh"

namespace gem5 {

class MAA;

struct RequestTableEntry {
    RequestTableEntry() : itr(0), wid(0) {}
    RequestTableEntry(int _itr, uint16_t _wid) : itr(_itr), wid(_wid) {}
    uint32_t itr;
    uint16_t wid;
};

class RequestTable {
public:
    RequestTable();
    ~RequestTable();

    bool add_entry(int itr, Addr base_addr, uint16_t wid);
    bool is_full();
    std::vector<RequestTableEntry> get_entries(Addr base_addr);
    void check_reset();
    void reset();

protected:
    const int num_addresses = 32;
    const int num_entries_per_address = 16;
    RequestTableEntry **entries;
    bool **entries_valid;
    Addr *addresses;
    bool *addresses_valid;
};

class StreamAccessUnit : public BaseMMU::Translation {
public:
    enum class Status : uint8_t {
        Idle = 0,
        Decode = 1,
        Request = 2,
        Response = 3,
        max
    };

protected:
    std::string status_names[5] = {
        "Idle",
        "Decode",
        "Request",
        "Response",
        "max"};
    class StreamPacket {
    public:
        PacketPtr packet;
        Tick tick;
        StreamPacket(PacketPtr _packet, Tick _tick)
            : packet(_packet), tick(_tick) {}
        StreamPacket(const StreamPacket &other) {
            packet = other.packet;
            tick = other.tick;
        }
        bool operator<(const StreamPacket &rhs) const {
            return tick < rhs.tick;
        }
    };
    struct CompareByTick {
        bool operator()(const StreamPacket &lhs, const StreamPacket &rhs) const {
            return lhs.tick < rhs.tick;
        }
    };
    std::multiset<StreamPacket, CompareByTick> my_outstanding_read_pkts;
    unsigned int num_tile_elements;
    Status state;
    MAA *maa;
    RequestTable *request_table;
    int dst_tile_id;

public:
    StreamAccessUnit();
    ~StreamAccessUnit() {
        if (request_table != nullptr) {
            delete request_table;
        }
    }
    void allocate(int _my_stream_id, unsigned int _num_tile_elements, MAA *_maa);

    Status getState() const { return state; }

    void setInstruction(Instruction *_instruction);

    void scheduleExecuteInstructionEvent(int latency = 0);
    void scheduleSendPacketEvent(int latency = 0);
    bool recvData(const Addr addr,
                  std::vector<uint32_t> data,
                  std::vector<uint16_t> wids);

    /* Related to BaseMMU::Translation Inheretance */
    void markDelayed() override {}
    void finish(const Fault &fault, const RequestPtr &req,
                ThreadContext *tc, BaseMMU::Mode mode) override;

protected:
    Instruction *my_instruction;
    Request::Flags flags = 0;
    const Addr block_size = 64;
    const Addr word_size = sizeof(uint32_t);
    int my_i;
    int my_idx;
    Addr my_base_addr, my_last_block_vaddr;
    int my_dst_tile, my_cond_tile, my_min, my_max, my_stride;
    int my_received_responses, my_sent_requests;
    int my_stream_id;
    Tick my_SPD_read_finish_tick;
    Tick my_SPD_write_finish_tick;
    Tick my_RT_access_finish_tick;

    Addr my_translated_addr;
    bool my_translation_done;

    void createReadPacket(Addr addr, int latency);
    bool sendOutstandingReadPacket();
    Addr translatePacket(Addr vaddr);
    void executeInstruction();
    EventFunctionWrapper executeInstructionEvent;
    EventFunctionWrapper sendPacketEvent;
    bool scheduleNextExecution();
    bool scheduleNextSend();
};
} // namespace gem5

#endif // __MEM_MAA_STREAMACCESS_HH__