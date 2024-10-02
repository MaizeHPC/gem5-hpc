#include "mem/MAA/MAA.hh"
#include "mem/MAA/ALU.hh"
#include "mem/MAA/IF.hh"
#include "mem/MAA/IndirectAccess.hh"
#include "mem/MAA/Invalidator.hh"
#include "mem/MAA/RangeFuser.hh"
#include "mem/MAA/SPD.hh"
#include "mem/MAA/StreamAccess.hh"

#include "base/addr_range.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "mem/packet.hh"
#include "params/MAA.hh"
#include "debug/MAA.hh"
#include "debug/MAACpuPort.hh"
#include "debug/MAACachePort.hh"
#include "debug/MAAMemPort.hh"
#include "debug/MAAController.hh"
#include <cassert>
#include <cstdint>

#ifndef TRACING_ON
#define TRACING_ON 1
#endif

namespace gem5 {

MAA::MAAResponsePort::MAAResponsePort(const std::string &_name, MAA &_maa, const std::string &_label)
    : QueuedResponsePort(_name, queue),
      maa{_maa},
      queue(_maa, *this, true, _label) {
}

MAA::MAA(const MAAParams &p)
    : ClockedObject(p),
      cpuSidePort(p.name + ".cpu_side_port", *this, "CpuSidePort"),
      memSidePort(p.name + ".mem_side_port", this, "MemSidePort"),
      cacheSidePort(p.name + ".cache_side_port", this, "CacheSidePort"),
      addrRanges(p.addr_ranges.begin(), p.addr_ranges.end()),
      num_tiles(p.num_tiles),
      num_tile_elements(p.num_tile_elements),
      num_regs(p.num_regs),
      num_instructions(p.num_instructions),
      num_stream_access_units(p.num_stream_access_units),
      num_indirect_access_units(p.num_indirect_access_units),
      num_range_units(p.num_range_units),
      num_alu_units(p.num_alu_units),
      num_row_table_rows(p.num_row_table_rows),
      num_row_table_entries_per_row(p.num_row_table_entries_per_row),
      rowtable_latency(p.rowtable_latency),
      cache_snoop_latency(p.cache_snoop_latency),
      system(p.system),
      mmu(p.mmu),
      my_instruction_pkt(nullptr),
      my_ready_pkt(nullptr),
      my_outstanding_instruction_pkt(false),
      my_outstanding_ready_pkt(false),
      issueInstructionEvent([this] { issueInstruction(); }, name()),
      dispatchInstructionEvent([this] { dispatchInstruction(); }, name()),
      stats(this,
            p.num_indirect_access_units,
            p.num_stream_access_units,
            p.num_range_units,
            p.num_alu_units) {

    requestorId = p.system->getRequestorId(this);
    spd = new SPD(this,
                  num_tiles,
                  num_tile_elements,
                  p.spd_read_latency,
                  p.spd_write_latency,
                  p.num_spd_read_ports,
                  p.num_spd_write_ports);
    rf = new RF(num_regs);
    ifile = new IF(num_instructions);
    streamAccessUnits = new StreamAccessUnit[num_stream_access_units];
    for (int i = 0; i < num_stream_access_units; i++) {
        streamAccessUnits[i].allocate(i, num_tile_elements, this);
    }
    indirectAccessUnits = new IndirectAccessUnit[num_indirect_access_units];
    cacheSidePort.allocate(p.max_outstanding_cache_side_packets);
    cpuSidePort.allocate(p.max_outstanding_cpu_side_packets);
    memSidePort.allocate();
    invalidator = new Invalidator();
    invalidator->allocate(
        num_tiles,
        num_tile_elements,
        addrRanges.front().start(),
        this);
    aluUnits = new ALUUnit[num_alu_units];
    for (int i = 0; i < num_alu_units; i++) {
        aluUnits[i].allocate(this, i, p.ALU_lane_latency, p.num_ALU_lanes);
    }
    rangeUnits = new RangeFuserUnit[num_range_units];
    for (int i = 0; i < num_range_units; i++) {
        rangeUnits[i].allocate(num_tile_elements, this, i);
    }
    current_instruction = new Instruction();
}

void MAA::init() {
    if (!cpuSidePort.isConnected())
        fatal("Cache ports on %s are not connected\n", name());
    cpuSidePort.sendRangeChange();
}

MAA::~MAA() {
}

Port &MAA::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "mem_side") {
        return memSidePort;
    } else if (if_name == "cpu_side") {
        return cpuSidePort;
    } else if (if_name == "cache_side") {
        return cacheSidePort;
    } else {
        return ClockedObject::getPort(if_name, idx);
    }
}
int MAA::inRange(Addr addr) const {
    int r_id = -1;
    for (const auto &r : addrRanges) {
        if (r.contains(addr)) {
            break;
        }
    }
    return r_id;
}
void MAA::addRamulator(memory::Ramulator2 *_ramulator2) {
    _ramulator2->getAddrMapData(m_org,
                                m_addr_bits,
                                m_num_levels,
                                m_tx_offset,
                                m_col_bits_idx,
                                m_row_bits_idx);
    DPRINTF(MAA, "DRAM organization [n_levels: %d] -- CH: %d, RA: %d, BG: %d, BA: %d, RO: %d, CO: %d\n",
            m_num_levels,
            m_org[ADDR_CHANNEL_LEVEL],
            m_org[ADDR_RANK_LEVEL],
            m_org[ADDR_BANKGROUP_LEVEL],
            m_org[ADDR_BANK_LEVEL],
            m_org[ADDR_ROW_LEVEL],
            m_org[ADDR_COLUMN_LEVEL]);
    DPRINTF(MAA, "DRAM addr_bit -- RO: %d, BA: %d, BG: %d, RA: %d, CO: %d, CH: %d, TX: %d\n",
            m_addr_bits[ADDR_ROW_LEVEL],
            m_addr_bits[ADDR_BANK_LEVEL],
            m_addr_bits[ADDR_BANKGROUP_LEVEL],
            m_addr_bits[ADDR_RANK_LEVEL],
            m_addr_bits[ADDR_COLUMN_LEVEL],
            m_addr_bits[ADDR_CHANNEL_LEVEL],
            m_tx_offset);
    assert(m_num_levels == 6);
    for (int i = 0; i < num_indirect_access_units; i++) {
        indirectAccessUnits[i].allocate(i,
                                        num_tile_elements,
                                        num_row_table_rows,
                                        num_row_table_entries_per_row,
                                        rowtable_latency,
                                        cache_snoop_latency,
                                        this);
    }
}
// RoBaRaCoCh address mapping taking from the Ramulator2
int slice_lower_bits(uint64_t &addr, int bits) {
    int lbits = addr & ((1 << bits) - 1);
    addr >>= bits;
    return lbits;
}
std::vector<int> MAA::map_addr(Addr addr) {
    std::vector<int> addr_vec(m_num_levels, -1);
    addr = addr >> m_tx_offset;
    addr_vec[0] = slice_lower_bits(addr, m_addr_bits[0]);
    addr_vec[m_addr_bits.size() - 1] = slice_lower_bits(addr, m_addr_bits[m_addr_bits.size() - 1]);
    for (int i = 1; i <= m_row_bits_idx; i++) {
        addr_vec[i] = slice_lower_bits(addr, m_addr_bits[i]);
    }
    return addr_vec;
}
Addr MAA::calc_Grow_addr(std::vector<int> addr_vec) {
    assert(addr_vec.size() == 6);
    Addr Grow_addr = (addr_vec[ADDR_BANKGROUP_LEVEL] >> 1) * m_org[ADDR_BANK_LEVEL];
    Grow_addr = (Grow_addr + addr_vec[ADDR_BANK_LEVEL]) * m_org[ADDR_ROW_LEVEL];
    Grow_addr += addr_vec[ADDR_ROW_LEVEL];
    return Grow_addr;
}
///////////////
//
// CpuSidePort
//
///////////////
void MAA::recvTimingSnoopResp(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());
    switch (pkt->cmd.toInt()) {
    case MemCmd::ReadExResp:
    case MemCmd::ReadResp: {
        assert(pkt->getSize() == 64);
        for (int i = 0; i < 64; i += 4) {
            panic_if(pkt->req->getByteEnable()[i] == false, "Byte enable [%d] is not set for the read response\n", i);
        }
        bool received = false;

        AddressRangeType address_range = AddressRangeType(pkt->getAddr(), addrRanges);
        if (address_range.isValid()) {
            // It's a dirty data for the invalidator in the SPD range
            assert(address_range.getType() == AddressRangeType::Type::SPD_DATA_CACHEABLE_RANGE);
            Addr offset = address_range.getOffset();
            int tile_id = offset / (num_tile_elements * sizeof(uint32_t));
            int element_id = offset % (num_tile_elements * sizeof(uint32_t));
            assert(element_id % sizeof(uint32_t) == 0);
            element_id /= sizeof(uint32_t);
            invalidator->recvData(tile_id, element_id, pkt->getPtr<uint8_t>());
        } else {
            // It's a data
            for (int i = 0; i < num_indirect_access_units; i++) {
                if (indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Request ||
                    indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Drain ||
                    indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Response) {
                    if (indirectAccessUnits[i].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>(), false)) {
                        panic_if(received, "Received multiple responses for the same request\n");
                    }
                }
            }
            for (int i = 0; i < num_stream_access_units; i++) {
                if (streamAccessUnits[i].getState() == StreamAccessUnit::Status::Request ||
                    streamAccessUnits[i].getState() == StreamAccessUnit::Status::Response) {
                    panic_if(streamAccessUnits[i].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>()),
                             "Received multiple responses for the same request\n");
                }
            }
        }
        break;
    }
    default:
        assert(false);
    }
}
bool MAA::CpuSidePort::recvTimingSnoopResp(PacketPtr pkt) {
    assert(pkt->isResponse());
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s, hasData %s, hasResponseData %s, size %u, isCached %s, satisfied: %d, be:\n",
            __func__,
            pkt->print(),
            pkt->hasData() ? "True" : "False",
            pkt->hasRespData() ? "True" : "False",
            pkt->getSize(),
            pkt->isBlockCached() ? "True" : "False",
            pkt->satisfied());
    for (int i = 0; i < pkt->getSize(); i++) {
        DPRINTF(MAACpuPort, "%02x %s\n", pkt->getPtr<uint8_t>()[i], pkt->req->getByteEnable()[i] ? "True" : "False");
    }
    // assert(false);
    // Express snoop responses from requestor to responder, e.g., from L1 to L2
    maa.recvTimingSnoopResp(pkt);
    outstandingCpuSidePackets--;
    if (blockReason == BlockReason::MAX_XBAR_PACKETS) {
        setUnblocked();
    }
    return true;
}

bool MAA::CpuSidePort::tryTiming(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());
    return true;
}

void MAA::recvTimingReq(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s, cmd: %s, isMaskedWrite: %d, size: %d\n",
            __func__,
            pkt->print(),
            pkt->cmdString(),
            pkt->isMaskedWrite(),
            pkt->getSize());
    AddressRangeType address_range = AddressRangeType(pkt->getAddr(), addrRanges);
    DPRINTF(MAACpuPort, "%s: address range type: %s\n", __func__, address_range.print());
    for (int i = 0; i < pkt->getSize(); i++) {
        DPRINTF(MAACpuPort, "%02x %s\n", pkt->getPtr<uint8_t>()[i], pkt->req->getByteEnable()[i] ? "True" : "False");
    }
    switch (pkt->cmd.toInt()) {
    case MemCmd::WriteReq: {
        bool respond_immediately = true;
        assert(pkt->isMaskedWrite() == false);
        switch (address_range.getType()) {
        case AddressRangeType::Type::SPD_DATA_NONCACHEABLE_RANGE: {
            Addr offset = address_range.getOffset();
            int tile_id = offset / (num_tile_elements * sizeof(uint32_t));
            panic_if(pkt->getSize() != 4 && pkt->getSize() != 8, "Invalid size for SPD data: %d\n", pkt->getSize());
            int element_id = offset % (num_tile_elements * sizeof(uint32_t));
            if (pkt->getSize() == 4) {
                assert(element_id % sizeof(uint32_t) == 0);
                element_id /= sizeof(uint32_t);
                uint32_t data_UINT32 = pkt->getPtr<uint32_t>()[0];
                int32_t data_INT32 = pkt->getPtr<int32_t>()[0];
                float data_FLOAT = pkt->getPtr<float>()[0];
                DPRINTF(MAACpuPort, "%s: TILE[%d][%d] = %u/%d/%f\n", __func__, tile_id, element_id, data_UINT32, data_INT32, data_FLOAT);
                spd->setData<uint32_t>(tile_id, element_id, data_UINT32);
            } else {
                assert(element_id % sizeof(uint64_t) == 0);
                element_id /= sizeof(uint64_t);
                uint64_t data_UINT64 = pkt->getPtr<uint64_t>()[0];
                int64_t data_INT64 = pkt->getPtr<int64_t>()[0];
                double data_DOUBLE = pkt->getPtr<double>()[0];
                DPRINTF(MAACpuPort, "%s: TILE[%d][%d] = %lu/%ld/%lf\n", __func__, tile_id, element_id, data_UINT64, data_INT64, data_DOUBLE);
                spd->setData<uint64_t>(tile_id, element_id, data_UINT64);
            }
            assert(pkt->needsResponse());
            pkt->makeTimingResponse();
            cpuSidePort.schedTimingResp(pkt, getClockEdge(spd->setDataLatency(1)));
            break;
        }
        case AddressRangeType::Type::SCALAR_RANGE: {
            Addr offset = address_range.getOffset();
            int element_id = offset % (num_regs * sizeof(uint32_t));
            assert(element_id % sizeof(uint32_t) == 0);
            element_id /= sizeof(uint32_t);
            panic_if(pkt->getSize() != 4 && pkt->getSize() != 8, "Invalid size for SPD data: %d\n", pkt->getSize());
            if (pkt->getSize() == 4) {
                uint32_t data_UINT32 = pkt->getPtr<uint32_t>()[0];
                int32_t data_INT32 = pkt->getPtr<int32_t>()[0];
                float data_FLOAT = pkt->getPtr<float>()[0];
                DPRINTF(MAACpuPort, "%s: REG[%d] = %u/%d/%f\n", __func__, element_id, data_UINT32, data_INT32, data_FLOAT);
                rf->setData<uint32_t>(element_id, data_UINT32);
            } else {
                uint64_t data_UINT64 = pkt->getPtr<uint64_t>()[0];
                int64_t data_INT64 = pkt->getPtr<int64_t>()[0];
                double data_DOUBLE = pkt->getPtr<double>()[0];
                DPRINTF(MAACpuPort, "%s: REG[%d] = %lu/%ld/%lf\n", __func__, element_id, data_UINT64, data_INT64, data_DOUBLE);
                rf->setData<uint64_t>(element_id, data_UINT64);
            }
            assert(pkt->needsResponse());
            pkt->makeTimingResponse();
            cpuSidePort.schedTimingResp(pkt, getClockEdge(Cycles(1)));
            break;
        }
        case AddressRangeType::Type::INSTRUCTION_RANGE: {
            Addr offset = address_range.getOffset();
            int element_id = offset % (num_instructions * sizeof(uint64_t));
            assert(element_id % sizeof(uint64_t) == 0);
            element_id /= sizeof(uint64_t);
            uint64_t data = pkt->getPtr<uint64_t>()[0];
            DPRINTF(MAACpuPort, "%s: IF[%d] = %ld\n", __func__, element_id, data);
#define NA_UINT8 0xFF
            switch (element_id) {
            case 0: {
                current_instruction->dst2SpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->dst1SpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->optype = (data & NA_UINT8) == NA_UINT8 ? Instruction::OPType::MAX : static_cast<Instruction::OPType>(data & NA_UINT8);
                data = data >> 8;
                current_instruction->datatype = (data & NA_UINT8) == NA_UINT8 ? Instruction::DataType::MAX : static_cast<Instruction::DataType>(data & NA_UINT8);
                assert(current_instruction->datatype != Instruction::DataType::MAX);
                data = data >> 8;
                current_instruction->opcode = (data & NA_UINT8) == NA_UINT8 ? Instruction::OpcodeType::MAX : static_cast<Instruction::OpcodeType>(data & NA_UINT8);
                assert(current_instruction->opcode != Instruction::OpcodeType::MAX);
                break;
            }
            case 1: {
                current_instruction->condSpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src3RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src2RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src1RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->dst2RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->dst1RegID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src2SpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                current_instruction->src1SpdID = (data & NA_UINT8) == NA_UINT8 ? -1 : (data & NA_UINT8);
                data = data >> 8;
                break;
            }
            case 2: {
                current_instruction->baseAddr = data;
                current_instruction->state = Instruction::Status::Idle;
                current_instruction->CID = pkt->req->contextId();
                current_instruction->PC = pkt->req->getPC();
                DPRINTF(MAAController, "%s: %s received!\n", __func__, current_instruction->print());
                respond_immediately = false;
                panic_if(my_outstanding_instruction_pkt, "Received multiple instruction packets\n");
                my_outstanding_instruction_pkt = true;
                my_instruction_pkt = pkt;
                scheduleDispatchInstructionEvent();
                break;
            }
            default:
                assert(false);
            }
            assert(pkt->needsResponse());
            if (respond_immediately) {
                pkt->makeTimingResponse();
                cpuSidePort.schedTimingResp(pkt, getClockEdge(Cycles(1)));
            }
            break;
        }
        default:
            // Write to SPD_DATA_CACHEABLE_RANGE not possible. All SPD writes must be to SPD_DATA_NONCACHEABLE_RANGE
            // Write to SPD_SIZE_RANGE not possible. Size is read-only.
            // Write to SPD_READY_RANGE not possible. Ready is read-only.
            DPRINTF(MAAController, "%s: Error: Range(%s) and cmd(%s) is illegal\n",
                    __func__, address_range.print(), pkt->cmdString());
            assert(false);
        }
        break;
    }
    case MemCmd::ReadReq: {
        // all read responses have a data payload
        assert(pkt->hasRespData());
        switch (address_range.getType()) {
        case AddressRangeType::Type::SPD_SIZE_RANGE: {
            assert(pkt->getSize() == sizeof(uint16_t));
            Addr offset = address_range.getOffset();
            assert(offset % sizeof(uint16_t) == 0);
            int element_id = offset / sizeof(uint16_t);
            uint16_t data = spd->getSize(element_id);
            uint8_t *dataPtr = (uint8_t *)(&data);
            pkt->setData(dataPtr);
            assert(pkt->needsResponse());
            pkt->makeTimingResponse();
            cpuSidePort.schedTimingResp(pkt, getClockEdge(Cycles(1)));
            break;
        }
        case AddressRangeType::Type::SPD_READY_RANGE: {
            assert(pkt->getSize() == sizeof(uint16_t));
            Addr offset = address_range.getOffset();
            assert(offset % sizeof(uint16_t) == 0);
            my_ready_tile_id = offset / sizeof(uint16_t);
            const uint16_t one = 1;
            pkt->setData((const uint8_t *)&one);
            assert(pkt->needsResponse());
            if (spd->getReady(my_ready_tile_id)) {
                pkt->makeTimingResponse();
                cpuSidePort.schedTimingResp(pkt, getClockEdge(Cycles(1)));
            } else {
                panic_if(my_outstanding_ready_pkt, "Received multiple ready read packets\n");
                my_outstanding_ready_pkt = true;
                my_ready_pkt = pkt;
            }
            break;
        }
        case AddressRangeType::Type::SCALAR_RANGE: {
            panic_if(pkt->getSize() != 4 && pkt->getSize() != 8, "Invalid size for SPD data: %d\n", pkt->getSize());
            Addr offset = address_range.getOffset();
            int element_id = offset % (num_regs * sizeof(uint32_t));
            assert(element_id % sizeof(uint32_t) == 0);
            element_id /= sizeof(uint32_t);
            uint8_t *dataPtr = rf->getDataPtr(element_id);
            pkt->setData(dataPtr);
            assert(pkt->needsResponse());
            pkt->makeTimingResponse();
            cpuSidePort.schedTimingResp(pkt, getClockEdge(Cycles(1)));
            break;
        }
        default: {
            // Read from SPD_DATA_CACHEABLE_RANGE uses ReadSharedReq command.
            // Read from SPD_DATA_NONCACHEABLE_RANGE not possible. All SPD reads must be from SPD_DATA_CACHEABLE_RANGE.
            DPRINTF(MAAController, "%s: Error: Range(%s) and cmd(%s) is illegal\n",
                    __func__, address_range.print(), pkt->cmdString());
            assert(false);
        }
        }
        break;
    }
    case MemCmd::ReadExReq:
    case MemCmd::ReadSharedReq: {
        // all read responses have a data payload
        assert(pkt->hasRespData());
        switch (address_range.getType()) {
        case AddressRangeType::Type::SPD_DATA_CACHEABLE_RANGE: {
            Addr offset = address_range.getOffset();
            int tile_id = offset / (num_tile_elements * sizeof(uint32_t));
            int element_id = offset % (num_tile_elements * sizeof(uint32_t));
            assert(element_id % sizeof(uint32_t) == 0);
            element_id /= sizeof(uint32_t);
            if (pkt->cmd == MemCmd::ReadSharedReq) {
                invalidator->read(tile_id, element_id);
            } else {
                invalidator->write(tile_id, element_id);
            }
            uint8_t *dataPtr = spd->getDataPtr(tile_id, element_id);
            pkt->setData(dataPtr);
            assert(pkt->needsResponse());
            pkt->makeTimingResponse();
            cpuSidePort.schedTimingResp(pkt, getClockEdge(spd->getDataLatency(1)));
            break;
        }
        default:
            DPRINTF(MAAController, "%s: Error: Range(%s) and cmd(%s) is illegal\n",
                    __func__, address_range.print(), pkt->cmdString());
            assert(false);
        }
        break;
    }
    default:
        assert(false);
    }
}
bool MAA::CpuSidePort::recvTimingReq(PacketPtr pkt) {
    assert(pkt->isRequest());
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());

    if (tryTiming(pkt)) {
        maa.recvTimingReq(pkt);
        return true;
    }
    return false;
}

void MAA::CpuSidePort::recvFunctional(PacketPtr pkt) {
    assert(false);
}

Tick MAA::recvAtomic(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
    return 0;
}
Tick MAA::CpuSidePort::recvAtomic(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
    return maa.recvAtomic(pkt);
}

AddrRangeList MAA::CpuSidePort::getAddrRanges() const {
    return maa.getAddrRanges();
}

bool MAA::CpuSidePort::sendSnoopPacket(uint8_t func_unit_type,
                                       int func_unit_id,
                                       PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACpuPort, "%s: UNIT[%s][%d] %s\n",
            __func__,
            func_unit_names[func_unit_type],
            func_unit_id,
            pkt->print());
    panic_if(pkt->isExpressSnoop() == false, "Packet is not an express snoop packet\n");
    panic_if(func_unit_type == (int)FuncUnitType::STREAM, "Stream does not have any snoop requests\n");
    if (blockReason != BlockReason::NOT_BLOCKED) {
        DPRINTF(MAACpuPort, "%s Send snoop blocked because of MAX_XBAR_PACKETS...\n", __func__);
        funcBlockReasons[func_unit_type][func_unit_id] = blockReason;
        return false;
    }
    if (outstandingCpuSidePackets == maxOutstandingCpuSidePackets) {
        // XBAR is full
        DPRINTF(MAACpuPort, "%s Send failed because XBAR is full...\n", __func__);
        assert(blockReason == BlockReason::NOT_BLOCKED);
        blockReason = BlockReason::MAX_XBAR_PACKETS;
        funcBlockReasons[func_unit_type][func_unit_id] = BlockReason::MAX_XBAR_PACKETS;
        return false;
    }
    sendTimingSnoopReq(pkt);
    DPRINTF(MAACpuPort, "%s Send is successfull...\n", __func__);
    if (!pkt->cacheResponding())
        outstandingCpuSidePackets++;
    return true;
}

void MAA::CpuSidePort::setUnblocked() {
    blockReason = BlockReason::NOT_BLOCKED;
    if (funcBlockReasons[(int)FuncUnitType::INVALIDATOR][0] != BlockReason::NOT_BLOCKED) {
        assert(maa.invalidator->getState() == Invalidator::Status::Request);
        funcBlockReasons[(int)FuncUnitType::INVALIDATOR][0] = BlockReason::NOT_BLOCKED;
        DPRINTF(MAACpuPort, "%s unblocked Unit[invalidator]...\n", __func__);
        maa.invalidator->scheduleExecuteInstructionEvent();
    }
    for (int i = 0; i < maa.num_indirect_access_units; i++) {
        if (funcBlockReasons[(int)FuncUnitType::INDIRECT][i] != BlockReason::NOT_BLOCKED) {
            assert(maa.indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Request ||
                   maa.indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Drain);
            funcBlockReasons[(int)FuncUnitType::INDIRECT][i] = BlockReason::NOT_BLOCKED;
            DPRINTF(MAACpuPort, "%s unblocked Unit[indirect][%d]...\n", __func__, i);
            maa.indirectAccessUnits[i].scheduleSendReadPacketEvent();
        }
    }
}

void MAA::CpuSidePort::allocate(int _maxOutstandingCpuSidePackets) {
    maxOutstandingCpuSidePackets = _maxOutstandingCpuSidePackets - 16;
    funcBlockReasons[(int)FuncUnitType::INDIRECT] = new BlockReason[maa.num_indirect_access_units];
    for (int i = 0; i < maa.num_indirect_access_units; i++) {
        funcBlockReasons[(int)FuncUnitType::INDIRECT][i] = BlockReason::NOT_BLOCKED;
    }
    funcBlockReasons[(int)FuncUnitType::INVALIDATOR] = new BlockReason[1];
    funcBlockReasons[(int)FuncUnitType::INVALIDATOR][0] = BlockReason::NOT_BLOCKED;
    blockReason = BlockReason::NOT_BLOCKED;
}

MAA::CpuSidePort::CpuSidePort(const std::string &_name, MAA &_maa,
                              const std::string &_label)
    : MAAResponsePort(_name, _maa, _label) {
}

///////////////
//
// MemSidePort
//
///////////////
void MAA::recvMemTimingResp(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s, cmd: %s, size: %d\n",
            __func__,
            pkt->print(),
            pkt->cmdString(),
            pkt->getSize());
    for (int i = 0; i < pkt->getSize(); i++) {
        DPRINTF(MAAMemPort, "[%d] %02x %s\n", i, pkt->getPtr<uint8_t>()[i], pkt->req->getByteEnable()[i] ? "True" : "False");
    }
    switch (pkt->cmd.toInt()) {
    case MemCmd::ReadExResp:
    case MemCmd::ReadResp: {
        assert(pkt->getSize() == 64);
        std::vector<uint32_t> data;
        std::vector<uint16_t> wid;
        for (int i = 0; i < 64; i += 4) {
            if (pkt->req->getByteEnable()[i] == true) {
                data.push_back(*(pkt->getPtr<uint32_t>() + i / 4));
                wid.push_back(i / 4);
            }
        }
        bool received = false;
        for (int i = 0; i < num_indirect_access_units; i++) {
            if (indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Request ||
                indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Drain ||
                indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Response) {
                if (indirectAccessUnits[i].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>(), false)) {
                    panic_if(received, "Received multiple responses for the same request\n");
                }
            }
        }
        for (int i = 0; i < num_stream_access_units; i++) {
            if (streamAccessUnits[i].getState() == StreamAccessUnit::Status::Request ||
                streamAccessUnits[i].getState() == StreamAccessUnit::Status::Response) {
                panic_if(streamAccessUnits[i].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>()),
                         "Received multiple responses for the same request\n");
            }
        }
        break;
    }
    default:
        assert(false);
    }
}
bool MAA::MemSidePort::recvTimingResp(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    maa->recvMemTimingResp(pkt);
    return true;
}

void MAA::recvMemTimingSnoopReq(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
}
// Express snooping requests to memside port
void MAA::MemSidePort::recvTimingSnoopReq(PacketPtr pkt) {
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    // handle snooping requests
    maa->recvMemTimingSnoopReq(pkt);
    assert(false);
}

Tick MAA::recvMemAtomicSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
    return 0;
}
Tick MAA::MemSidePort::recvAtomicSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    return maa->recvMemAtomicSnoop(pkt);
    assert(false);
}

void MAA::memFunctionalAccess(PacketPtr pkt, bool from_cpu_side) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
}
void MAA::MemSidePort::recvFunctionalSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: received %s\n", __func__, pkt->print());
    // functional snoop (note that in contrast to atomic we don't have
    // a specific functionalSnoop method, as they have the same
    // behaviour regardless)
    maa->memFunctionalAccess(pkt, false);
    assert(false);
}

void MAA::MemSidePort::recvReqRetry() {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: called!\n", __func__);
    setUnblocked(BlockReason::MEM_FAILED);
}

void MAA::MemSidePort::setUnblocked(BlockReason reason) {
    assert(blockReason == reason);
    blockReason = BlockReason::NOT_BLOCKED;
    for (int i = 0; i < maa->num_indirect_access_units; i++) {
        if (funcBlockReasons[i] != BlockReason::NOT_BLOCKED) {
            assert(funcBlockReasons[i] == reason);
            assert(maa->indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Request ||
                   maa->indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Drain ||
                   maa->indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Response);
            funcBlockReasons[i] = BlockReason::NOT_BLOCKED;
            DPRINTF(MAAMemPort, "%s unblocked Unit[indirect][%d]...\n", __func__, i);
            maa->indirectAccessUnits[i].scheduleSendReadPacketEvent();
            maa->indirectAccessUnits[i].scheduleSendWritePacketEvent();
        }
    }
}
bool MAA::MemSidePort::sendPacket(int func_unit_id,
                                  PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: UNIT[INDIRECT][%d] %s\n",
            __func__,
            func_unit_id,
            pkt->print());
    if (blockReason != BlockReason::NOT_BLOCKED) {
        DPRINTF(MAAMemPort, "%s Send blocked because of MEM_FAILED...\n", __func__);
        funcBlockReasons[func_unit_id] = blockReason;
        return false;
    }
    if (maa->memSidePort.sendTimingReq(pkt) == false) {
        // Cache cannot receive a new request
        DPRINTF(MAAMemPort, "%s Send failed because cache returned false...\n", __func__);
        blockReason = BlockReason::MEM_FAILED;
        funcBlockReasons[func_unit_id] = BlockReason::MEM_FAILED;
        return false;
    }
    DPRINTF(MAAMemPort, "%s Send is successfull...\n", __func__);
    return true;
}
void MAA::MemSidePort::allocate() {
    funcBlockReasons = new BlockReason[maa->num_indirect_access_units];
    for (int i = 0; i < maa->num_indirect_access_units; i++) {
        funcBlockReasons[i] = BlockReason::NOT_BLOCKED;
    }
    blockReason = BlockReason::NOT_BLOCKED;
}

void MAA::MAAReqPacketQueue::sendDeferredPacket() {
    /// print the packet
    DPRINTF(MAAMemPort, "%s: called!\n", __func__);
    assert(false);
}

MAA::MemSidePort::MemSidePort(const std::string &_name,
                              MAA *_maa,
                              const std::string &_label)
    : MAAMemRequestPort(_name, _reqQueue, _snoopRespQueue),
      _reqQueue(*_maa, *this, _snoopRespQueue, _label),
      _snoopRespQueue(*_maa, *this, true, _label), maa(_maa) {
    blockReason = BlockReason::NOT_BLOCKED;
}

///////////////
//
// CacheSidePort
//
///////////////
void MAA::recvCacheTimingResp(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s, cmd: %s, size: %d\n",
            __func__,
            pkt->print(),
            pkt->cmdString(),
            pkt->getSize());
    // for (int i = 0; i < pkt->getSize(); i++) {
    //     DPRINTF(MAACachePort, "%02x %s\n", pkt->getPtr<uint8_t>()[i], pkt->req->getByteEnable()[i] ? "True" : "False");
    // }
    switch (pkt->cmd.toInt()) {
    case MemCmd::ReadExResp:
    case MemCmd::ReadResp: {
        assert(pkt->getSize() == 64);
        std::vector<uint32_t> data;
        std::vector<uint16_t> wid;
        for (int i = 0; i < 64; i += 4) {
            if (pkt->req->getByteEnable()[i] == true) {
                data.push_back(*(pkt->getPtr<uint32_t>() + i / 4));
                wid.push_back(i / 4);
            }
        }
        bool received = false;
        if (pkt->cmd == MemCmd::ReadResp) {
            for (int i = 0; i < num_stream_access_units; i++) {
                if (streamAccessUnits[i].getState() == StreamAccessUnit::Status::Request ||
                    streamAccessUnits[i].getState() == StreamAccessUnit::Status::Response) {
                    if (streamAccessUnits[i].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>())) {
                        panic_if(received, "Received multiple responses for the same request\n");
                        received = true;
                    }
                }
            }
        }
        if (received == false) {
            for (int i = 0; i < num_indirect_access_units; i++) {
                if (indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Request ||
                    indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Drain ||
                    indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Response) {
                    if (indirectAccessUnits[i].recvData(pkt->getAddr(), pkt->getPtr<uint8_t>(), true)) {
                        panic_if(received, "Received multiple responses for the same request\n");
                    }
                }
            }
        }
        break;
    }
    case MemCmd::InvalidateResp: {
        assert(false);
        // assert(pkt->getSize() == 64);
        // AddressRangeType address_range = AddressRangeType(pkt->getAddr(), addrRanges);
        // assert(address_range.getType() == AddressRangeType::Type::SPD_DATA_CACHEABLE_RANGE);
        // Addr offset = address_range.getOffset();
        // int tile_id = offset / (num_tile_elements * sizeof(uint32_t));
        // int element_id = offset % (num_tile_elements * sizeof(uint32_t));
        // assert(element_id % sizeof(uint32_t) == 0);
        // element_id /= sizeof(uint32_t);
        // invalidator->recvData(tile_id, element_id);
        break;
    }
    default:
        assert(false);
    }
}
bool MAA::CacheSidePort::recvTimingResp(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    maa->recvCacheTimingResp(pkt);
    outstandingCacheSidePackets--;
    if (blockReason == BlockReason::MAX_XBAR_PACKETS) {
        setUnblocked(BlockReason::MAX_XBAR_PACKETS);
    }
    return true;
}

void MAA::recvCacheTimingSnoopReq(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
}
// Express snooping requests to memside port
void MAA::CacheSidePort::recvTimingSnoopReq(PacketPtr pkt) {
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    // handle snooping requests
    maa->recvCacheTimingSnoopReq(pkt);
    assert(false);
}

Tick MAA::recvCacheAtomicSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
    return 0;
}
Tick MAA::CacheSidePort::recvAtomicSnoop(PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    return maa->recvCacheAtomicSnoop(pkt);
    assert(false);
}

void MAA::cacheFunctionalAccess(PacketPtr pkt, bool from_cpu_side) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: received %s\n", __func__, pkt->print());
    assert(false);
}
void MAA::CacheSidePort::recvFunctionalSnoop(PacketPtr pkt) {
    /// print the packet
    // DPRINTF(MAACachePort, "%s: received %s, doing nothing\n", __func__, pkt->print());
    // // functional snoop (note that in contrast to atomic we don't have
    // // a specific functionalSnoop method, as they have the same
    // // behaviour regardless)
    // maa->cacheFunctionalAccess(pkt, false);
    // assert(false);
}

void MAA::CacheSidePort::recvReqRetry() {
    /// print the packet
    DPRINTF(MAACachePort, "%s: called!\n", __func__);
    setUnblocked(BlockReason::CACHE_FAILED);
}

bool MAA::CacheSidePort::sendPacket(uint8_t func_unit_type,
                                    int func_unit_id,
                                    PacketPtr pkt) {
    /// print the packet
    DPRINTF(MAACachePort, "%s: UNIT[%s][%d] %s\n",
            __func__,
            func_unit_names[func_unit_type],
            func_unit_id,
            pkt->print());
    if (blockReason != BlockReason::NOT_BLOCKED) {
        DPRINTF(MAACachePort, "%s Send blocked because of %s...\n", __func__,
                blockReason == BlockReason::MAX_XBAR_PACKETS ? "MAX_XBAR_PACKETS" : "CACHE_FAILED");
        funcBlockReasons[func_unit_type][func_unit_id] = blockReason;
        return false;
    }
    if (outstandingCacheSidePackets == maxOutstandingCacheSidePackets) {
        // XBAR is full
        DPRINTF(MAACachePort, "%s Send failed because XBAR is full...\n", __func__);
        assert(blockReason == BlockReason::NOT_BLOCKED);
        blockReason = BlockReason::MAX_XBAR_PACKETS;
        funcBlockReasons[func_unit_type][func_unit_id] = BlockReason::MAX_XBAR_PACKETS;
        return false;
    }
    if (sendTimingReq(pkt) == false) {
        // Cache cannot receive a new request
        DPRINTF(MAACachePort, "%s Send failed because cache returned false...\n", __func__);
        blockReason = BlockReason::CACHE_FAILED;
        funcBlockReasons[func_unit_type][func_unit_id] = BlockReason::CACHE_FAILED;
        return false;
    }
    DPRINTF(MAACachePort, "%s Send is successfull...\n", __func__);
    if (pkt->needsResponse() && !pkt->cacheResponding())
        outstandingCacheSidePackets++;
    return true;
}

void MAA::CacheSidePort::setUnblocked(BlockReason reason) {
    assert(blockReason == reason);
    blockReason = BlockReason::NOT_BLOCKED;
    if (funcBlockReasons[(int)FuncUnitType::INVALIDATOR][0] != BlockReason::NOT_BLOCKED) {
        assert(funcBlockReasons[(int)FuncUnitType::INVALIDATOR][0] == reason);
        assert(maa->invalidator->getState() == Invalidator::Status::Request);
        funcBlockReasons[(int)FuncUnitType::INVALIDATOR][0] = BlockReason::NOT_BLOCKED;
        DPRINTF(MAACachePort, "%s unblocked Unit[invalidator]...\n", __func__);
        maa->invalidator->scheduleExecuteInstructionEvent();
    }
    for (int i = 0; i < maa->num_stream_access_units; i++) {
        if (funcBlockReasons[(int)FuncUnitType::STREAM][i] != BlockReason::NOT_BLOCKED) {
            assert(funcBlockReasons[(int)FuncUnitType::STREAM][i] == reason);
            assert(maa->streamAccessUnits[i].getState() == StreamAccessUnit::Status::Request);
            funcBlockReasons[(int)FuncUnitType::STREAM][i] = BlockReason::NOT_BLOCKED;
            DPRINTF(MAACachePort, "%s unblocked Unit[stream][%d]...\n", __func__, i);
            maa->streamAccessUnits[i].scheduleSendPacketEvent();
        }
    }
    for (int i = 0; i < maa->num_indirect_access_units; i++) {
        if (funcBlockReasons[(int)FuncUnitType::INDIRECT][i] != BlockReason::NOT_BLOCKED) {
            assert(funcBlockReasons[(int)FuncUnitType::INDIRECT][i] == reason);
            assert(maa->indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Request ||
                   maa->indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Drain);
            funcBlockReasons[(int)FuncUnitType::INDIRECT][i] = BlockReason::NOT_BLOCKED;
            DPRINTF(MAACachePort, "%s unblocked Unit[indirect][%d]...\n", __func__, i);
            maa->indirectAccessUnits[i].scheduleSendReadPacketEvent();
            maa->indirectAccessUnits[i].scheduleSendWritePacketEvent();
        }
    }
}

void MAA::CacheSidePort::allocate(int _maxOutstandingCacheSidePackets) {
    maxOutstandingCacheSidePackets = _maxOutstandingCacheSidePackets;
    // 16384 is maximum transmitList of PacketQueue (CPU side port of LLC)
    // Taken from gem5-hpc/src/mem/packet_queue.cc (changed from 1024 to 16384)
    maxOutstandingCacheSidePackets = std::min(maxOutstandingCacheSidePackets, 16384);
    // We let it to be 32 less than the maximum
    maxOutstandingCacheSidePackets -= 32;
    funcBlockReasons[(int)FuncUnitType::STREAM] = new BlockReason[maa->num_stream_access_units];
    for (int i = 0; i < maa->num_stream_access_units; i++) {
        funcBlockReasons[(int)FuncUnitType::STREAM][i] = BlockReason::NOT_BLOCKED;
    }
    funcBlockReasons[(int)FuncUnitType::INDIRECT] = new BlockReason[maa->num_indirect_access_units];
    for (int i = 0; i < maa->num_indirect_access_units; i++) {
        funcBlockReasons[(int)FuncUnitType::INDIRECT][i] = BlockReason::NOT_BLOCKED;
    }
    funcBlockReasons[(int)FuncUnitType::INVALIDATOR] = new BlockReason[1];
    funcBlockReasons[(int)FuncUnitType::INVALIDATOR][0] = BlockReason::NOT_BLOCKED;
    blockReason = BlockReason::NOT_BLOCKED;
}

MAA::CacheSidePort::CacheSidePort(const std::string &_name,
                                  MAA *_maa,
                                  const std::string &_label)
    : MAACacheRequestPort(_name, _reqQueue, _snoopRespQueue),
      _reqQueue(*_maa, *this, _snoopRespQueue, _label),
      _snoopRespQueue(*_maa, *this, true, _label), maa(_maa) {
    outstandingCacheSidePackets = 0;
    blockReason = BlockReason::NOT_BLOCKED;
}

///////////////
//
// MAA
//
///////////////

void MAA::issueInstruction() {
    if (invalidator->getState() == Invalidator::Status::Idle) {
        Instruction *inst = ifile->getReady(FuncUnitType::INVALIDATOR);
        if (inst != nullptr) {
            invalidator->setInstruction(inst);
            invalidator->scheduleExecuteInstructionEvent(1);
        }
    }
    for (int i = 0; i < num_stream_access_units; i++) {
        if (streamAccessUnits[i].getState() == StreamAccessUnit::Status::Idle) {
            Instruction *inst = ifile->getReady(FuncUnitType::STREAM);
            if (inst != nullptr) {
                streamAccessUnits[i].setInstruction(inst);
                streamAccessUnits[i].scheduleExecuteInstructionEvent(1);
            } else {
                break;
            }
        }
    }
    for (int i = 0; i < num_indirect_access_units; i++) {
        if (indirectAccessUnits[i].getState() == IndirectAccessUnit::Status::Idle) {
            Instruction *inst = ifile->getReady(FuncUnitType::INDIRECT);
            if (inst != nullptr) {
                indirectAccessUnits[i].setInstruction(inst);
                indirectAccessUnits[i].scheduleExecuteInstructionEvent(1);
            } else {
                break;
            }
        }
    }
    for (int i = 0; i < num_alu_units; i++) {
        if (aluUnits[i].getState() == ALUUnit::Status::Idle) {
            Instruction *inst = ifile->getReady(FuncUnitType::ALU);
            if (inst != nullptr) {
                aluUnits[i].setInstruction(inst);
                aluUnits[i].scheduleExecuteInstructionEvent(1);
            } else {
                break;
            }
        }
    }
    for (int i = 0; i < num_range_units; i++) {
        if (rangeUnits[i].getState() == RangeFuserUnit::Status::Idle) {
            Instruction *inst = ifile->getReady(FuncUnitType::RANGE);
            if (inst != nullptr) {
                rangeUnits[i].setInstruction(inst);
                rangeUnits[i].scheduleExecuteInstructionEvent(1);
            } else {
                break;
            }
        }
    }
}
void MAA::dispatchInstruction() {
    DPRINTF(MAAController, "%s: dispatching...!\n", __func__);
    if (my_outstanding_instruction_pkt) {
        assert(my_instruction_pkt != nullptr);
        current_instruction->src1Invalidated = (current_instruction->src1SpdID == -1);
        current_instruction->src2Invalidated = (current_instruction->src2SpdID == -1);
        current_instruction->condInvalidated = (current_instruction->condSpdID == -1);
        current_instruction->src1Ready = current_instruction->src1SpdID == -1 ? true : spd->getReady(current_instruction->src1SpdID);
        current_instruction->src2Ready = current_instruction->src2SpdID == -1 ? true : spd->getReady(current_instruction->src2SpdID);
        current_instruction->condReady = current_instruction->condSpdID == -1 ? true : spd->getReady(current_instruction->condSpdID);
        // assume that we can read from any tile, so invalidate all destinations
        // Instructions with DST1: stream and indirect load, range loop, ALU
        current_instruction->dst1Invalidated = (current_instruction->dst1SpdID == -1);
        // Instructions with DST2: range loop
        current_instruction->dst2Invalidated = (current_instruction->dst2SpdID == -1);
        if (ifile->pushInstruction(*current_instruction)) {
            DPRINTF(MAAController, "%s: %s dispatched!\n", __func__, current_instruction->print());
            if (current_instruction->dst1SpdID != -1) {
                assert(current_instruction->dst1SpdID != current_instruction->src1SpdID);
                assert(current_instruction->dst1SpdID != current_instruction->src2SpdID);
                spd->unsetReady(current_instruction->dst1SpdID);
            }
            if (current_instruction->dst2SpdID != -1) {
                assert(current_instruction->dst2SpdID != current_instruction->src1SpdID);
                assert(current_instruction->dst2SpdID != current_instruction->src2SpdID);
                spd->unsetReady(current_instruction->dst2SpdID);
            }
            if (current_instruction->opcode == Instruction::OpcodeType::INDIR_ST ||
                current_instruction->opcode == Instruction::OpcodeType::INDIR_RMW) {
                spd->unsetReady(current_instruction->src2SpdID);
            }
            my_instruction_pkt->makeTimingResponse();
            cpuSidePort.schedTimingResp(my_instruction_pkt, getClockEdge(Cycles(1)));
            scheduleIssueInstructionEvent(1);
            my_outstanding_instruction_pkt = false;
        } else {
            DPRINTF(MAAController, "%s: %s failed to dipatch!\n", __func__, current_instruction->print());
        }
    }
}
void MAA::finishInstruction(Instruction *instruction,
                            int dst1SpdID,
                            int dst2SpdID) {
    DPRINTF(MAAController, "%s: %s finishing!\n", __func__, instruction->print());
    ifile->finishInstruction(instruction, dst1SpdID, dst2SpdID);
    scheduleIssueInstructionEvent();
    scheduleDispatchInstructionEvent();
}
void MAA::setTileInvalidated(Instruction *instruction, int tileID) {
    if (tileID == instruction->src1SpdID) {
        assert(instruction->src1Invalidated == false);
        instruction->src1Invalidated = true;
    } else if (tileID == instruction->src2SpdID) {
        assert(instruction->src2Invalidated == false);
        instruction->src2Invalidated = true;
    } else if (tileID == instruction->condSpdID) {
        assert(instruction->condInvalidated == false);
        instruction->condInvalidated = true;
    } else if (tileID == instruction->dst1SpdID) {
        assert(instruction->dst1Invalidated == false);
        instruction->dst1Invalidated = true;
    } else if (tileID == instruction->dst2SpdID) {
        assert(instruction->dst2Invalidated == false);
        instruction->dst2Invalidated = true;
    } else {
        assert(false);
    }
    scheduleIssueInstructionEvent();
}
void MAA::scheduleIssueInstructionEvent(int latency) {
    DPRINTF(MAAController, "%s: scheduling issue for the next %d cycles!\n", __func__, latency);
    Tick new_when = curTick() + latency;
    if (!issueInstructionEvent.scheduled()) {
        schedule(issueInstructionEvent, new_when);
    } else {
        Tick old_when = issueInstructionEvent.when();
        if (new_when < old_when)
            reschedule(issueInstructionEvent, new_when);
    }
}
void MAA::scheduleDispatchInstructionEvent(int latency) {
    DPRINTF(MAAController, "%s: scheduling dispatch for the next %d cycles!\n", __func__, latency);
    Tick new_when = curTick() + latency;
    if (!dispatchInstructionEvent.scheduled()) {
        schedule(dispatchInstructionEvent, new_when);
    } else {
        Tick old_when = dispatchInstructionEvent.when();
        if (new_when < old_when)
            reschedule(dispatchInstructionEvent, new_when);
    }
}
void MAA::setTileReady(int tileID) {
    DPRINTF(MAAController, "%s: tile[%d] is ready!\n", __func__, tileID);
    if (my_outstanding_ready_pkt && (my_ready_tile_id == tileID)) {
        DPRINTF(MAAController, "%s: responding to outstanding ready packet!\n", __func__);
        my_ready_pkt->makeTimingResponse();
        cpuSidePort.schedTimingResp(my_ready_pkt, getClockEdge(Cycles(1)));
        my_outstanding_ready_pkt = false;
    }
    spd->setReady(tileID);
}
Tick MAA::getClockEdge(Cycles cycles) const {
    return clockEdge(cycles);
}
Cycles MAA::getTicksToCycles(Tick t) const {
    return ticksToCycles(t);
}
Tick MAA::getCyclesToTicks(Cycles c) const {
    return cyclesToTicks(c);
}

#define MAKE_INDIRECT_STAT_NAME(name) \
    (std::string("I") + std::to_string(indirect_id) + "_" + std::string(name)).c_str()

#define MAKE_STREAM_STAT_NAME(name) \
    (std::string("S") + std::to_string(stream_id) + "_" + std::string(name)).c_str()

#define MAKE_RANGE_STAT_NAME(name) \
    (std::string("A") + std::to_string(range_id) + "_" + std::string(name)).c_str()

#define MAKE_ALU_STAT_NAME(name) \
    (std::string("A") + std::to_string(alu_id) + "_" + std::string(name)).c_str()

#define MAKE_INVALIDATOR_STAT_NAME(name) \
    (std::string("INV_") + std::string(name)).c_str()

MAA::MAAStats::MAAStats(statistics::Group *parent,
                        int num_indirect_access_units,
                        int num_stream_access_units,
                        int num_range_units,
                        int num_alu_units)
    : statistics::Group(parent),
      ADD_STAT(numInst_INDRD, statistics::units::Count::get(), "number of indirect read instructions"),
      ADD_STAT(numInst_INDWR, statistics::units::Count::get(), "number of indirect write instructions"),
      ADD_STAT(numInst_INDRMW, statistics::units::Count::get(), "number of indirect read-modify-write instructions"),
      ADD_STAT(numInst_STRRD, statistics::units::Count::get(), "number of stream read instructions"),
      ADD_STAT(numInst_RANGE, statistics::units::Count::get(), "number of range loop instructions"),
      ADD_STAT(numInst_ALUS, statistics::units::Count::get(), "number of ALU Scalar instructions"),
      ADD_STAT(numInst_ALUV, statistics::units::Count::get(), "number of ALU Vector instructions"),
      ADD_STAT(numInst_INV, statistics::units::Count::get(), "number of Invalidation for instructions"),
      ADD_STAT(numInst, statistics::units::Count::get(), "total number of instructions"),
      ADD_STAT(cycles_INDRD, statistics::units::Count::get(), "number of indirect read instruction cycles"),
      ADD_STAT(cycles_INDWR, statistics::units::Count::get(), "number of indirect write instruction cycles"),
      ADD_STAT(cycles_INDRMW, statistics::units::Count::get(), "number of indirect read-modify-write instruction cycles"),
      ADD_STAT(cycles_STRRD, statistics::units::Count::get(), "number of stream read instruction cycles"),
      ADD_STAT(cycles_RANGE, statistics::units::Count::get(), "number of range loop instruction cycles"),
      ADD_STAT(cycles_ALUS, statistics::units::Count::get(), "number of ALU Scalar instruction cycles"),
      ADD_STAT(cycles_ALUV, statistics::units::Count::get(), "number of ALU Vector instruction cycles"),
      ADD_STAT(cycles_INV, statistics::units::Count::get(), "number of Invalidation for instruction cycles"),
      ADD_STAT(cycles, statistics::units::Count::get(), "total number of instruction cycles"),
      ADD_STAT(avgCPI_INDRD, statistics::units::Count::get(), "average CPI for indirect read instructions"),
      ADD_STAT(avgCPI_INDWR, statistics::units::Count::get(), "average CPI for indirect write instructions"),
      ADD_STAT(avgCPI_INDRMW, statistics::units::Count::get(), "average CPI for indirect read-modify-write instructions"),
      ADD_STAT(avgCPI_STRRD, statistics::units::Count::get(), "average CPI for stream read instructions"),
      ADD_STAT(avgCPI_RANGE, statistics::units::Count::get(), "average CPI for range loop instructions"),
      ADD_STAT(avgCPI_ALUS, statistics::units::Count::get(), "average CPI for ALU Scalar instructions"),
      ADD_STAT(avgCPI_ALUV, statistics::units::Count::get(), "average CPI for ALU Vector instructions"),
      ADD_STAT(avgCPI_INV, statistics::units::Count::get(), "average CPI for Invalidation for instructions"),
      ADD_STAT(avgCPI, statistics::units::Count::get(), "average CPI for all instructions") {

    numInst_INDRD.flags(statistics::nozero);
    numInst_INDWR.flags(statistics::nozero);
    numInst_INDRMW.flags(statistics::nozero);
    numInst_STRRD.flags(statistics::nozero);
    numInst_RANGE.flags(statistics::nozero);
    numInst_ALUS.flags(statistics::nozero);
    numInst_ALUV.flags(statistics::nozero);
    numInst_INV.flags(statistics::nozero);
    numInst.flags(statistics::nozero);
    cycles_INDRD.flags(statistics::nozero);
    cycles_INDWR.flags(statistics::nozero);
    cycles_INDRMW.flags(statistics::nozero);
    cycles_STRRD.flags(statistics::nozero);
    cycles_RANGE.flags(statistics::nozero);
    cycles_ALUS.flags(statistics::nozero);
    cycles_ALUV.flags(statistics::nozero);
    cycles_INV.flags(statistics::nozero);
    cycles.flags(statistics::nozero);

    avgCPI_INDRD = cycles_INDRD / numInst_INDRD;
    avgCPI_INDWR = cycles_INDWR / numInst_INDWR;
    avgCPI_INDRMW = cycles_INDRMW / numInst_INDRMW;
    avgCPI_STRRD = cycles_STRRD / numInst_STRRD;
    avgCPI_RANGE = cycles_RANGE / numInst_RANGE;
    avgCPI_ALUS = cycles_ALUS / numInst_ALUS;
    avgCPI_ALUV = cycles_ALUV / numInst_ALUV;
    avgCPI_INV = cycles_INV / numInst_INV;
    avgCPI = cycles / numInst;

    avgCPI_INDRD.flags(statistics::nonan | statistics::nozero);
    avgCPI_INDWR.flags(statistics::nonan | statistics::nozero);
    avgCPI_INDRMW.flags(statistics::nonan | statistics::nozero);
    avgCPI_STRRD.flags(statistics::nonan | statistics::nozero);
    avgCPI_RANGE.flags(statistics::nonan | statistics::nozero);
    avgCPI_ALUS.flags(statistics::nonan | statistics::nozero);
    avgCPI_ALUV.flags(statistics::nonan | statistics::nozero);
    avgCPI_INV.flags(statistics::nonan | statistics::nozero);
    avgCPI.flags(statistics::nonan | statistics::nozero);

    for (int indirect_id = 0; indirect_id < num_indirect_access_units; indirect_id++) {
        IND_NumInsts.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumInsts"), statistics::units::Count::get(), "number of instructions"));
        IND_NumWordsInserted.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumWordsInserted"), statistics::units::Count::get(), "number of words inserted to the row table"));
        IND_NumCacheLineInserted.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumCacheLineInserted"), statistics::units::Count::get(), "number of cachelines inserted to the row table"));
        IND_NumRowsInserted.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumRowsInserted"), statistics::units::Count::get(), "number of rows inserted to the row table"));
        IND_NumDrains.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_NumDrains"), statistics::units::Count::get(), "number of drains due to row table full"));
        IND_AvgWordsPerCacheLine.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgWordsPerCacheLine"), statistics::units::Count::get(), "average number of words per cacheline"));
        IND_AvgCacheLinesPerRow.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCacheLinesPerRow"), statistics::units::Count::get(), "average number of cachelines per row"));
        IND_AvgRowsPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgRowsPerInst"), statistics::units::Count::get(), "average number of rows per indirect instruction"));
        IND_AvgDrainsPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgDrainsPerInst"), statistics::units::Count::get(), "average number of drains per indirect instruction"));
        IND_CyclesFill.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesFill"), statistics::units::Count::get(), "number of cycles in the FILL stage"));
        IND_CyclesDrain.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesDrain"), statistics::units::Count::get(), "number of cycles in the DRAIN stage"));
        IND_CyclesBuild.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesBuild"), statistics::units::Count::get(), "number of cycles in the BUILD stage"));
        IND_CyclesRequest.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesRequest"), statistics::units::Count::get(), "number of cycles in the REQUEST stage"));
        IND_CyclesRTAccess.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesRTAccess"), statistics::units::Count::get(), "number of cycles spent on row table access"));
        IND_CyclesSPDReadAccess.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesSPDReadAccess"), statistics::units::Count::get(), "number of cycles spent on SPD read access"));
        IND_CyclesSPDWriteAccess.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_CyclesSPDWriteAccess"), statistics::units::Count::get(), "number of cycles spent on SPD write access"));
        IND_AvgCyclesFillPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesFillPerInst"), statistics::units::Count::get(), "average number of cycles in the FILL stage per indirect instruction"));
        IND_AvgCyclesDrainPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesDrainPerInst"), statistics::units::Count::get(), "average number of cycles in the DRAIN stage per indirect instruction"));
        IND_AvgCyclesBuildPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesBuildPerInst"), statistics::units::Count::get(), "average number of cycles in the BUILD stage per indirect instruction"));
        IND_AvgCyclesRequestPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesRequestPerInst"), statistics::units::Count::get(), "average number of cycles in the REQUEST stage per indirect instruction"));
        IND_AvgCyclesRTAccessPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesRTAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on row table access per indirect instruction"));
        IND_AvgCyclesSPDReadAccessPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesSPDReadAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on SPD read access per indirect instruction"));
        IND_AvgCyclesSPDWriteAccessPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgCyclesSPDWriteAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on SPD write access per indirect instruction"));
        IND_LoadsCacheHitResponding.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_LoadsCacheHitResponding"), statistics::units::Count::get(), "number of loads hit in cache in the M/O state, responding back"));
        IND_LoadsCacheHitAccessing.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_LoadsCacheHitAccessing"), statistics::units::Count::get(), "number of loads hit in cache in the E/S state, reaccessed cache"));
        IND_LoadsMemAccessing.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_LoadsMemAccessing"), statistics::units::Count::get(), "number of loads miss in cache, accessed from memory"));
        IND_AvgLoadsCacheHitRespondingPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgLoadsCacheHitRespondingPerInst"), statistics::units::Count::get(), "average number of loads hit in cache in the M/O state per indirect instruction"));
        IND_AvgLoadsCacheHitAccessingPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgLoadsCacheHitAccessingPerInst"), statistics::units::Count::get(), "average number of loads hit in cache in the E/S state per indirect instruction"));
        IND_AvgLoadsMemAccessingPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgLoadsMemAccessingPerInst"), statistics::units::Count::get(), "average number of loads miss in cache per indirect instruction"));
        IND_StoresMemAccessing.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_StoresMemAccessing"), statistics::units::Count::get(), "number of writes accessed from memory"));
        IND_AvgStoresMemAccessingPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgStoresMemAccessingPerInst"), statistics::units::Count::get(), "average number of writes accessed from memory per indirect instruction"));
        IND_Evicts.push_back(new statistics::Scalar(this, MAKE_INDIRECT_STAT_NAME("IND_Evicts"), statistics::units::Count::get(), "number of evict accesses to the cache side port"));
        IND_AvgEvictssPerInst.push_back(new statistics::Formula(this, MAKE_INDIRECT_STAT_NAME("IND_AvgEvictssPerInst"), statistics::units::Count::get(), "average number of evict accesses to the cache side port per indirect instruction"));

        (*IND_NumInsts[indirect_id]).flags(statistics::nozero);
        (*IND_NumWordsInserted[indirect_id]).flags(statistics::nozero);
        (*IND_NumCacheLineInserted[indirect_id]).flags(statistics::nozero);
        (*IND_NumRowsInserted[indirect_id]).flags(statistics::nozero);
        (*IND_NumDrains[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesFill[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesDrain[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesBuild[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesRequest[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesRTAccess[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesSPDReadAccess[indirect_id]).flags(statistics::nozero);
        (*IND_CyclesSPDWriteAccess[indirect_id]).flags(statistics::nozero);
        (*IND_LoadsCacheHitResponding[indirect_id]).flags(statistics::nozero);
        (*IND_LoadsCacheHitAccessing[indirect_id]).flags(statistics::nozero);
        (*IND_LoadsMemAccessing[indirect_id]).flags(statistics::nozero);
        (*IND_StoresMemAccessing[indirect_id]).flags(statistics::nozero);
        (*IND_Evicts[indirect_id]).flags(statistics::nozero);

        (*IND_AvgWordsPerCacheLine[indirect_id]) = (*IND_NumWordsInserted[indirect_id]) / (*IND_NumCacheLineInserted[indirect_id]);
        (*IND_AvgCacheLinesPerRow[indirect_id]) = (*IND_NumCacheLineInserted[indirect_id]) / (*IND_NumRowsInserted[indirect_id]);
        (*IND_AvgRowsPerInst[indirect_id]) = (*IND_NumRowsInserted[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgDrainsPerInst[indirect_id]) = (*IND_NumDrains[indirect_id]) / (*IND_NumInsts[indirect_id]);

        (*IND_AvgCyclesFillPerInst[indirect_id]) = (*IND_CyclesFill[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgCyclesDrainPerInst[indirect_id]) = (*IND_CyclesDrain[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgCyclesBuildPerInst[indirect_id]) = (*IND_CyclesBuild[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgCyclesRequestPerInst[indirect_id]) = (*IND_CyclesRequest[indirect_id]) / (*IND_NumInsts[indirect_id]);

        (*IND_AvgCyclesRTAccessPerInst[indirect_id]) = (*IND_CyclesRTAccess[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgCyclesSPDReadAccessPerInst[indirect_id]) = (*IND_CyclesSPDReadAccess[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgCyclesSPDWriteAccessPerInst[indirect_id]) = (*IND_CyclesSPDWriteAccess[indirect_id]) / (*IND_NumInsts[indirect_id]);

        (*IND_AvgLoadsCacheHitRespondingPerInst[indirect_id]) = (*IND_LoadsCacheHitResponding[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgLoadsCacheHitAccessingPerInst[indirect_id]) = (*IND_LoadsCacheHitAccessing[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgLoadsMemAccessingPerInst[indirect_id]) = (*IND_LoadsMemAccessing[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgStoresMemAccessingPerInst[indirect_id]) = (*IND_StoresMemAccessing[indirect_id]) / (*IND_NumInsts[indirect_id]);
        (*IND_AvgEvictssPerInst[indirect_id]) = (*IND_Evicts[indirect_id]) / (*IND_NumInsts[indirect_id]);

        (*IND_AvgWordsPerCacheLine[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCacheLinesPerRow[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgRowsPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgDrainsPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesFillPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesDrainPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesBuildPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesRequestPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesRTAccessPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesSPDReadAccessPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgCyclesSPDWriteAccessPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgLoadsCacheHitRespondingPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgLoadsCacheHitAccessingPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgLoadsMemAccessingPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgStoresMemAccessingPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
        (*IND_AvgEvictssPerInst[indirect_id]).flags(statistics::nozero | statistics::nonan);
    }
    for (int stream_id = 0; stream_id < num_stream_access_units; stream_id++) {
        STR_NumInsts.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_NumInsts"), statistics::units::Count::get(), "number of instructions"));
        STR_NumWordsInserted.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_NumWordsInserted"), statistics::units::Count::get(), "number of words inserted to the request table"));
        STR_NumCacheLineInserted.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_NumCacheLineInserted"), statistics::units::Count::get(), "number of cachelines inserted to the request table"));
        STR_NumDrains.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_NumDrains"), statistics::units::Count::get(), "number of drains due to request table full"));
        STR_AvgWordsPerCacheLine.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgWordsPerCacheLine"), statistics::units::Count::get(), "average number of words per cacheline"));
        STR_AvgCacheLinesPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCacheLinesPerInst"), statistics::units::Count::get(), "average number of cachelines per stream instruction"));
        STR_AvgDrainsPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgDrainsPerInst"), statistics::units::Count::get(), "average number of drains per stream instruction"));
        STR_CyclesRequest.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_CyclesRequest"), statistics::units::Count::get(), "number of cycles in the REQUEST stage"));
        STR_CyclesRTAccess.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_CyclesRTAccess"), statistics::units::Count::get(), "number of cycles for request table access"));
        STR_CyclesSPDReadAccess.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_CyclesSPDReadAccess"), statistics::units::Count::get(), "number of cycles for SPD read access"));
        STR_CyclesSPDWriteAccess.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_CyclesSPDWriteAccess"), statistics::units::Count::get(), "number of cycles for SPD write access"));
        STR_AvgCyclesRequestPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCyclesRequestPerInst"), statistics::units::Count::get(), "average number of cycles in the REQUEST stage per stream instruction"));
        STR_AvgCyclesRTAccessPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCyclesRTAccessPerInst"), statistics::units::Count::get(), "average number of cycles for request table access per stream instruction"));
        STR_AvgCyclesSPDReadAccessPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCyclesSPDReadAccessPerInst"), statistics::units::Count::get(), "average number of cycles for SPD read access per stream instruction"));
        STR_AvgCyclesSPDWriteAccessPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgCyclesSPDWriteAccessPerInst"), statistics::units::Count::get(), "average number of cycles for SPD write access per stream instruction"));
        STR_LoadsCacheAccessing.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_LoadsCacheAccessing"), statistics::units::Count::get(), "number of loads accessed from cache"));
        STR_AvgLoadsCacheAccessingPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgLoadsCacheAccessingPerInst"), statistics::units::Count::get(), "average number of loads accessed from cache per stream instruction"));
        STR_Evicts.push_back(new statistics::Scalar(this, MAKE_STREAM_STAT_NAME("STR_Evicts"), statistics::units::Count::get(), "number of evict accesses to the cache side port"));
        STR_AvgEvictssPerInst.push_back(new statistics::Formula(this, MAKE_STREAM_STAT_NAME("STR_AvgEvictssPerInst"), statistics::units::Count::get(), "average number of evict accesses to the cache side port per stream instruction"));

        (*STR_NumInsts[stream_id]).flags(statistics::nozero);
        (*STR_NumWordsInserted[stream_id]).flags(statistics::nozero);
        (*STR_NumCacheLineInserted[stream_id]).flags(statistics::nozero);
        (*STR_NumDrains[stream_id]).flags(statistics::nozero);
        (*STR_CyclesRequest[stream_id]).flags(statistics::nozero);
        (*STR_CyclesRTAccess[stream_id]).flags(statistics::nozero);
        (*STR_CyclesSPDReadAccess[stream_id]).flags(statistics::nozero);
        (*STR_CyclesSPDWriteAccess[stream_id]).flags(statistics::nozero);
        (*STR_LoadsCacheAccessing[stream_id]).flags(statistics::nozero);
        (*STR_Evicts[stream_id]).flags(statistics::nozero);

        (*STR_AvgWordsPerCacheLine[stream_id]) = (*STR_NumWordsInserted[stream_id]) / (*STR_NumCacheLineInserted[stream_id]);
        (*STR_AvgCacheLinesPerInst[stream_id]) = (*STR_NumCacheLineInserted[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgDrainsPerInst[stream_id]) = (*STR_NumDrains[stream_id]) / (*STR_NumInsts[stream_id]);

        (*STR_AvgCyclesRequestPerInst[stream_id]) = (*STR_CyclesRequest[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgCyclesRTAccessPerInst[stream_id]) = (*STR_CyclesRTAccess[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgCyclesSPDReadAccessPerInst[stream_id]) = (*STR_CyclesSPDReadAccess[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgCyclesSPDWriteAccessPerInst[stream_id]) = (*STR_CyclesSPDWriteAccess[stream_id]) / (*STR_NumInsts[stream_id]);

        (*STR_AvgLoadsCacheAccessingPerInst[stream_id]) = (*STR_LoadsCacheAccessing[stream_id]) / (*STR_NumInsts[stream_id]);
        (*STR_AvgEvictssPerInst[stream_id]) = (*STR_Evicts[stream_id]) / (*STR_NumInsts[stream_id]);

        (*STR_AvgWordsPerCacheLine[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCacheLinesPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgDrainsPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCyclesRequestPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCyclesRTAccessPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCyclesSPDReadAccessPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgCyclesSPDWriteAccessPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgLoadsCacheAccessingPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
        (*STR_AvgEvictssPerInst[stream_id]).flags(statistics::nozero | statistics::nonan);
    }
    for (int range_id = 0; range_id < num_range_units; range_id++) {
        RNG_NumInsts.push_back(new statistics::Scalar(this, MAKE_RANGE_STAT_NAME("RNG_NumInsts"), statistics::units::Count::get(), "number of instructions"));
        RNG_CyclesCompute.push_back(new statistics::Scalar(this, MAKE_RANGE_STAT_NAME("RNG_CyclesCompute"), statistics::units::Count::get(), "number of compute cycles in range loop"));
        RNG_CyclesSPDReadAccess.push_back(new statistics::Scalar(this, MAKE_RANGE_STAT_NAME("RNG_CyclesSPDReadAccess"), statistics::units::Count::get(), "number of cycles spent on SPD read access in range loop"));
        RNG_CyclesSPDWriteAccess.push_back(new statistics::Scalar(this, MAKE_RANGE_STAT_NAME("RNG_CyclesSPDWriteAccess"), statistics::units::Count::get(), "number of cycles spent on SPD write access in range loop"));
        RNG_AvgCyclesComputePerInst.push_back(new statistics::Formula(this, MAKE_RANGE_STAT_NAME("RNG_AvgCyclesComputePerInst"), statistics::units::Count::get(), "average number of compute cycles per range loop instruction"));
        RNG_AvgCyclesSPDReadAccessPerInst.push_back(new statistics::Formula(this, MAKE_RANGE_STAT_NAME("RNG_AvgCyclesSPDReadAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on SPD read access per range loop instruction"));
        RNG_AvgCyclesSPDWriteAccessPerInst.push_back(new statistics::Formula(this, MAKE_RANGE_STAT_NAME("RNG_AvgCyclesSPDWriteAccessPerInst"), statistics::units::Count::get(), "average number of cycles spent on SPD write access per range loop instruction"));

        (*RNG_NumInsts[range_id]).flags(statistics::nozero);
        (*RNG_CyclesCompute[range_id]).flags(statistics::nozero);
        (*RNG_CyclesSPDReadAccess[range_id]).flags(statistics::nozero);
        (*RNG_CyclesSPDWriteAccess[range_id]).flags(statistics::nozero);

        (*RNG_AvgCyclesComputePerInst[range_id]) = (*RNG_CyclesCompute[range_id]) / (*RNG_NumInsts[range_id]);
        (*RNG_AvgCyclesSPDReadAccessPerInst[range_id]) = (*RNG_CyclesSPDReadAccess[range_id]) / (*RNG_NumInsts[range_id]);
        (*RNG_AvgCyclesSPDWriteAccessPerInst[range_id]) = (*RNG_CyclesSPDWriteAccess[range_id]) / (*RNG_NumInsts[range_id]);

        (*RNG_AvgCyclesComputePerInst[range_id]).flags(statistics::nozero | statistics::nonan);
        (*RNG_AvgCyclesSPDReadAccessPerInst[range_id]).flags(statistics::nozero | statistics::nonan);
        (*RNG_AvgCyclesSPDWriteAccessPerInst[range_id]).flags(statistics::nozero | statistics::nonan);
    }
    for (int alu_id = 0; alu_id < num_alu_units; alu_id++) {
        ALU_NumInsts.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumInsts"), statistics::units::Count::get(), "number of instructions"));
        ALU_NumInstsCompare.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumInstsCompare"), statistics::units::Count::get(), "number of compare instructions"));
        ALU_NumInstsCompute.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumInstsCompute"), statistics::units::Count::get(), "number of compute instructions"));
        ALU_CyclesCompute.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_CyclesCompute"), statistics::units::Count::get(), "number of cycles spent on compute"));
        ALU_CyclesSPDReadAccess.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_CyclesSPDReadAccess"), statistics::units::Count::get(), "number of cycles for SPD read access"));
        ALU_CyclesSPDWriteAccess.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_CyclesSPDWriteAccess"), statistics::units::Count::get(), "number of cycles for SPD write access"));
        ALU_AvgCyclesComputePerInst.push_back(new statistics::Formula(this, MAKE_ALU_STAT_NAME("ALU_AvgCyclesComputePerInst"), statistics::units::Count::get(), "average number of cycles spent on compute per ALU instruction"));
        ALU_AvgCyclesSPDReadAccessPerInst.push_back(new statistics::Formula(this, MAKE_ALU_STAT_NAME("ALU_AvgCyclesSPDReadAccessPerInst"), statistics::units::Count::get(), "average number of cycles for SPD read access per ALU instruction"));
        ALU_AvgCyclesSPDWriteAccessPerInst.push_back(new statistics::Formula(this, MAKE_ALU_STAT_NAME("ALU_AvgCyclesSPDWriteAccessPerInst"), statistics::units::Count::get(), "average number of cycles for SPD write access per ALU instruction"));
        ALU_NumComparedWords.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumComparedWords"), statistics::units::Count::get(), "number of words compared"));
        ALU_NumTakenWords.push_back(new statistics::Scalar(this, MAKE_ALU_STAT_NAME("ALU_NumTakenWords"), statistics::units::Count::get(), "number of words which comparison was taken"));
        ALU_AvgNumTakenWordsPerComparedWords.push_back(new statistics::Formula(this, MAKE_ALU_STAT_NAME("ALU_AvgNumTakenWordsPerComparedWords"), statistics::units::Count::get(), "portion of words which comparison was taken"));

        (*ALU_NumInsts[alu_id]).flags(statistics::nozero);
        (*ALU_NumInstsCompare[alu_id]).flags(statistics::nozero);
        (*ALU_NumInstsCompute[alu_id]).flags(statistics::nozero);
        (*ALU_CyclesCompute[alu_id]).flags(statistics::nozero);
        (*ALU_CyclesSPDReadAccess[alu_id]).flags(statistics::nozero);
        (*ALU_CyclesSPDWriteAccess[alu_id]).flags(statistics::nozero);
        (*ALU_NumComparedWords[alu_id]).flags(statistics::nozero);
        (*ALU_NumTakenWords[alu_id]).flags(statistics::nozero);

        (*ALU_AvgCyclesComputePerInst[alu_id]) = (*ALU_CyclesCompute[alu_id]) / (*ALU_NumInsts[alu_id]);
        (*ALU_AvgCyclesSPDReadAccessPerInst[alu_id]) = (*ALU_CyclesSPDReadAccess[alu_id]) / (*ALU_NumInsts[alu_id]);
        (*ALU_AvgCyclesSPDWriteAccessPerInst[alu_id]) = (*ALU_CyclesSPDWriteAccess[alu_id]) / (*ALU_NumInsts[alu_id]);
        (*ALU_AvgNumTakenWordsPerComparedWords[alu_id]) = (*ALU_NumTakenWords[alu_id]) / (*ALU_NumComparedWords[alu_id]);

        (*ALU_AvgCyclesComputePerInst[alu_id]).flags(statistics::nozero | statistics::nonan);
        (*ALU_AvgCyclesSPDReadAccessPerInst[alu_id]).flags(statistics::nozero | statistics::nonan);
        (*ALU_AvgCyclesSPDWriteAccessPerInst[alu_id]).flags(statistics::nozero | statistics::nonan);
        (*ALU_AvgNumTakenWordsPerComparedWords[alu_id]).flags(statistics::nozero | statistics::nonan);
    }
    INV_NumInvalidatedCachelines = new statistics::Scalar(this, MAKE_INVALIDATOR_STAT_NAME("INV_NumInvalidatedCachelines"), statistics::units::Count::get(), "number of invalidated cachelines");
    INV_AvgInvalidatedCachelinesPerInst = new statistics::Formula(this, MAKE_INVALIDATOR_STAT_NAME("INV_AvgInvalidatedCachelinesPerInst"), statistics::units::Count::get(), "average number of invalidated cachelines per instruction");

    (*INV_NumInvalidatedCachelines).flags(statistics::nozero);
    (*INV_AvgInvalidatedCachelinesPerInst) = (*INV_NumInvalidatedCachelines) / numInst_INV;
    (*INV_AvgInvalidatedCachelinesPerInst).flags(statistics::nozero | statistics::nonan);
}
} // namespace gem5
