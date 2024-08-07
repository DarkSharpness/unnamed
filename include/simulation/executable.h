// Should only be included in interpreter/executable.cpp
#include <general.h>
#include <utility.h>
#include <riscv/register.h>
#include <interpreter/device.h>
#include <interpreter/memory.h>
#include <interpreter/register.h>
#include <interpreter/exception.h>
#include <interpreter/executable.h>

namespace dark {

struct Executable::MetaData::PackData  {
    target_size_t &rd;
    target_size_t  rs1;
    target_size_t  rs2;
    target_size_t  imm;
};

inline auto Executable::MetaData::parse(RegisterFile &rf) const -> PackData {
    auto &rd = rf[this->rd];
    auto rs1 = rf[this->rs1];
    auto rs2 = rf[this->rs2];
    auto imm = this->imm;
    return PackData { .rd = rd, .rs1 = rs1, .rs2 = rs2, .imm = imm };
}

} // namespace dark

namespace dark::interpreter {

namespace __details {

template <general::ArithOp op>
static void arith_impl(target_size_t &rd, target_size_t rs1, target_size_t rs2, Device &dev) {
    static_assert(sizeof(target_size_t) == 4);
 
    using i32 = std::int32_t;
    using u32 = std::uint32_t;

    constexpr auto i64 = [](u32 x) { return static_cast<std::int64_t>(i32(x)); };
    constexpr auto u64 = [](u32 x) { return static_cast<std::uint64_t>(x); };

    using enum general::ArithOp;
    #define check_zero \
        rs2 == 0 ? throw FailToInterpret { \
            .error = Error::DivideByZero, \
            .message = {} \
        }

    switch (op) {
        case ADD:   rd = rs1 + rs2; dev.counter.add++; return;
        case SUB:   rd = rs1 - rs2; dev.counter.sub++; return;
        case AND:   rd = rs1 & rs2; dev.counter.and_++; return;
        case OR:    rd = rs1 | rs2; dev.counter.or_++; return;
        case XOR:   rd = rs1 ^ rs2; dev.counter.xor_++; return;
        case SLL:   rd = rs1 << rs2; dev.counter.sll++; return;
        case SRL:   rd = u32(rs1) >> rs2; dev.counter.srl++; return;
        case SRA:   rd = i32(rs1) >> rs2; dev.counter.sra++; return;
        case SLT:   rd = i32(rs1) < i32(rs2); dev.counter.slt++; return;
        case SLTU:  rd = u32(rs1) < u32(rs2); dev.counter.sltu++; return;
        case MUL:   rd = rs1 * rs2; dev.counter.mul++; return;
        case MULH:  rd = (i64(rs1) * i64(rs2)) >> 32; dev.counter.mulh++; return;
        case MULHSU:rd = (i64(rs1) * u64(rs2)) >> 32; dev.counter.mulhsu++; return;
        case MULHU: rd = (u64(rs1) * u64(rs2)) >> 32; dev.counter.mulhu++; return;
        case DIV:   rd = check_zero : i32(rs1) / i32(rs2); dev.counter.div++; return;
        case DIVU:  rd = check_zero : u32(rs1) / u32(rs2); dev.counter.divu++; return;
        case REM:   rd = check_zero : i32(rs1) % i32(rs2); dev.counter.rem++; return;
        case REMU:  rd = check_zero : u32(rs1) % u32(rs2); dev.counter.remu++; return;
        default:    unreachable();
    }
    #undef check_zero
}

} // namespace __details

namespace ArithReg {
    template <general::ArithOp op>
    static auto fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &&[rd, rs1, rs2, imm] = exe.get_meta().parse(rf);
        __details::arith_impl <op> (rd, rs1, rs2, dev);
        return exe.next();
    }
} // namespace ArithReg

namespace ArithImm {
    template <general::ArithOp op>
    static auto fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &&[rd, rs1, rs2, imm] = exe.get_meta().parse(rf);
        __details::arith_impl <op> (rd, rs1, imm, dev);
        return exe.next();
    }
} // namespace ArithImm

namespace LoadStore {
    template <general::MemoryOp op>
    static auto fn(Executable &exe, RegisterFile &rf, Memory &mem, Device &dev) {
        auto &&[rd, rs1, rs2, imm] = exe.get_meta().parse(rf);
        auto addr = rs1 + imm;

        using enum general::MemoryOp;
        switch (op) {
            case LB:    rd = mem.load_i8(addr); dev.counter.lb++; break;
            case LH:    rd = mem.load_i16(addr); dev.counter.lh++; break;
            case LW:    rd = mem.load_i32(addr); dev.counter.lw++; break;
            case LBU:   rd = mem.load_u8(addr); dev.counter.lbu++; break;
            case LHU:   rd = mem.load_u16(addr); dev.counter.lhu++; break;
            case SB:    mem.store_u8(addr, rs2); dev.counter.sb++; break;
            case SH:    mem.store_u16(addr, rs2); dev.counter.sh++; break;
            case SW:    mem.store_u32(addr, rs2); dev.counter.sw++; break;
            default:    unreachable();
        }

        return exe.next();
    }
} // namespace LoadStore

namespace Branch {
    template <general::BranchOp op>
    static auto fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &&[rd, rs1, rs2, imm] = exe.get_meta().parse(rf);
        static_assert(sizeof(target_size_t) == 4);

        using i32 = std::int32_t;
        using u32 = std::uint32_t;

        using enum general::BranchOp;

        bool result {};
        switch (op) {
            case BEQ:   result = (rs1 == rs2); dev.counter.beq++; break;
            case BNE:   result = (rs1 != rs2); dev.counter.bne++; break;
            case BLT:   result = (i32(rs1) <  i32(rs2)); dev.counter.blt++; break;
            case BGE:   result = (i32(rs1) >= i32(rs2)); dev.counter.bge++; break;
            case BLTU:  result = (u32(rs1) <  u32(rs2)); dev.counter.bltu++; break;
            case BGEU:  result = (u32(rs1) >= u32(rs2)); dev.counter.bgeu++; break;
            default:    unreachable();
        }

        dev.predict(rf.get_pc(), result);
        if (result) {
            rf.set_pc(rf.get_pc() + imm);
            return exe.next(imm);
        } else {
            return exe.next();
        }
    }
} // namespace Branch

namespace Jump {
    [[maybe_unused]]
    static auto fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &&[rd, rs1, rs2, imm] = exe.get_meta().parse(rf);

        rd = rf.get_pc() + 4;
        rf.set_pc(rf.get_pc() + imm);
        dev.counter.jal++;

        return exe.next(imm);
    }
} // namespace Jump

namespace Jalr {
    [[maybe_unused]]
    static auto fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &&[rd, rs1, rs2, imm] = exe.get_meta().parse(rf);

        auto target = (rs1 + imm) & ~1;
        auto offset = target - rf.get_pc();

        rd = rf.get_pc() + 4;
        rf.set_pc(target);
        dev.counter.jalr++;

        return exe.next(offset);
    }
} // namespace Jalr

namespace Lui {
    [[maybe_unused]]
    static auto fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &&[rd, rs1, rs2, imm] = exe.get_meta().parse(rf);
        rd = imm;
        dev.counter.lui++;
        return exe.next();
    }
} // namespace Lui

namespace Auipc {
    [[maybe_unused]]
    static auto fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &&[rd, rs1, rs2, imm] = exe.get_meta().parse(rf);
        rd = rf.get_pc() + imm;
        dev.counter.auipc++;
        return exe.next();
    }
} // namespace Auipc

} // namespace dark::interpreter
