#pragma once
#include <general.h>
#include <utility.h>
#include <riscv/register.h>
#include <interpreter/device.h>
#include <interpreter/memory.h>
#include <interpreter/register.h>
#include <interpreter/exception.h>
#include <interpreter/executable.h>
#include <bit>

namespace dark::interpreter {

namespace __details {

template <general::ArithOp op>
inline void arith_impl(target_size_t &rd, target_size_t rs1, target_size_t rs2, Device &dev) {
    static_assert(sizeof(target_size_t) == 4);
 
    using i32 = std::int32_t;
    using u32 = std::uint32_t;

    constexpr auto i64 = [](u32 x) { return static_cast<std::int64_t>(i32(x)); };
    constexpr auto u64 = [](u32 x) { return static_cast<std::uint64_t>(x); };

    using enum general::ArithOp;
    #define check_zero \
        rs2 == 0 ? throw FailToInterpret { Error::DivideByZero }

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
    }
    #undef check_zero

    runtime_assert(false);
}

template <typename _Derived>
struct crtp {
    constexpr auto to_data() const -> std::size_t;
};

} // namespace __details

struct ArithReg : __details::crtp <ArithReg> {
    Register rs1;
    Register rs2;
    Register rd;

    template <general::ArithOp op>
    static void fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &arith = exe.get_data <ArithReg>();
        auto &rd = rf[arith.rd];
        auto rs1 = rf[arith.rs1];
        auto rs2 = rf[arith.rs2];
        __details::arith_impl <op> (rd, rs1, rs2, dev);
    }
};

struct ArithImm : __details::crtp <ArithImm> {
    Register rs1;
    Register rd;
    target_size_t imm;

    template <general::ArithOp op>
    static void fn(Executable &exe, RegisterFile &rf, Memory &mem, Device &dev) {
        auto &arith = exe.get_data <ArithImm>();
        auto &rd = rf[arith.rd];
        auto rs1 = rf[arith.rs1];
        auto imm = arith.imm;
        __details::arith_impl <op> (rd, rs1, imm, dev);
    }
};

struct LoadStore : __details::crtp <LoadStore> {
    Register rs1;
    Register rd;    // or rs2
    target_size_t imm;

    template <general::MemoryOp op>
    static void fn(Executable &exe, RegisterFile &rf, Memory &mem, Device &dev) {
        auto &ls = exe.get_data <LoadStore>();
        auto &rd = rf[ls.rd]; // or rs2
        auto addr = rf[ls.rs1] + ls.imm;

        using enum general::MemoryOp;

        switch (op) {
            case LB:    rd = mem.load_i8(addr); dev.counter.lb++; return;
            case LH:    rd = mem.load_i16(addr); dev.counter.lh++; return;
            case LW:    rd = mem.load_i32(addr); dev.counter.lw++; return;
            case LBU:   rd = mem.load_u8(addr); dev.counter.lbu++; return;
            case LHU:   rd = mem.load_u16(addr); dev.counter.lhu++; return;
            case SB:    mem.store_u8(addr, rd); dev.counter.sb++; return;
            case SH:    mem.store_u16(addr, rd); dev.counter.sh++; return;
            case SW:    mem.store_u32(addr, rd); dev.counter.sw++; return;
        }

        runtime_assert(false);
    }
};

struct Branch : __details::crtp <Branch> {
    Register rs1;
    Register rs2;
    target_size_t imm;

    template <general::BranchOp op>
    static void fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &branch = exe.get_data <Branch>();
        auto rs1 = rf[branch.rs1];
        auto rs2 = rf[branch.rs2];
        auto imm = branch.imm;

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
            default:    runtime_assert(false);
        }

        dev.predict(rf.get_pc(), result);
        auto &pc = rf.get_pc();
        pc += (result ? imm : 4);
    }
};

struct Jump : __details::crtp <Jump> {
    Register rd;
    target_size_t imm;
    static void fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &jump = exe.get_data <Jump>();
        auto &rd = rf[jump.rd];
        auto imm = jump.imm;
        auto new_pc = rd + imm;
        rd = rf.get_pc() + 4;
        rf.get_pc() = new_pc;
        dev.counter.jal++;
    }
};

struct Jalr : __details::crtp <Jalr> {
    Register rs1;
    Register rd;
    target_size_t imm;
    static void fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &jalr = exe.get_data <Jalr>();
        auto &rd = rf[jalr.rd];
        auto rs1 = rf[jalr.rs1];
        auto imm = jalr.imm;
        auto new_pc = (rs1 + imm) & ~1;
        rd = rf.get_pc() + 4;
        rf.get_pc() = new_pc;
        dev.counter.jalr++;
    }
};

struct Lui : __details::crtp <Lui> {
    Register rd;
    target_size_t imm;
    static void fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &lui = exe.get_data <Lui>();
        auto &rd = rf[lui.rd];
        auto imm = lui.imm;
        rd = imm;
        dev.counter.lui++;
    }
};

struct Auipc : __details::crtp <Auipc> {
    Register rd;
    target_size_t imm;
    static void fn(Executable &exe, RegisterFile &rf, Memory &, Device &dev) {
        auto &auipc = exe.get_data <Auipc>();
        auto &rd = rf[auipc.rd];
        auto imm = auipc.imm;
        rd = rf.get_pc() + imm;
        dev.counter.auipc++;
    }
};

namespace __details {

template <typename _Derived>
constexpr auto crtp <_Derived>::to_data() const -> std::size_t {
    struct {
        alignas(std::size_t) _Derived data;
    } d = { .data = static_cast<const _Derived &>(*this) };
    return std::bit_cast<std::size_t>(d);
}

} // namespace __details

} // namespace dark::interpreter
