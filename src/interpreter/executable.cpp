#include <utility.h>
#include <riscv/command.h>
#include <interpreter/exception.h>
#include <interpreter/executable.h>
#include <interpreter/device.h>
#include <interpreter/memory.h>
#include <interpreter/register.h>
#include <interpreter/implement.h>

namespace dark {

using _Pair_t = std::pair <Executable::_Func_t *, std::size_t>;

static auto parse_cmd(std::uint32_t cmd) -> _Pair_t;

[[noreturn]]
static void handle_unknown_instruction(std::uint32_t cmd) {
    throw FailToInterpret { Error::InsUnknown, cmd };
}

/**
 * A function which will parse the command at runtime,
 * and reset the executable function pointer.
 * 
 * This can greatly improve the performance of the interpreter.
 * Since each command will only be parsed once (on the first execution).
 * Subsequent executions will not require parsing.
 */
void Executable::fn(Executable &exe, RegisterFile &rf, Memory &mem, Device &dev) {
    const auto pc = rf.get_pc();
    dev.counter.iparse += 1;

    const auto cmd = mem.load_cmd(pc);

    const auto [func, data] = parse_cmd(cmd);
    exe.set_handle(func, data);

    return exe(rf, mem, dev);
}

static auto parse_r_type(std::uint32_t cmd) -> _Pair_t {
    auto r_type = command::r_type::from_integer(cmd);

    constexpr auto join = // A simple function to join two integers.
        [](std::uint32_t a, std::uint32_t b) -> std::uint32_t {
            return (a << 3) | b;
        };

    auto rs1 = int_to_reg(r_type.rs1);
    auto rs2 = int_to_reg(r_type.rs2);
    auto rd  = int_to_reg(r_type.rd);
    auto arg = interpreter::ArithReg {
        .rs1 = rs1, .rs2 = rs2, .rd = rd
    }.to_data();

    #define match_and_return(a) \
        case join(command::r_type::Funct7::a, command::r_type::Funct3::a):  \
            return { interpreter::ArithReg::fn <general::ArithOp::a>, arg }

    switch (join(r_type.funct7, r_type.funct3)) {
        match_and_return(ADD);
        match_and_return(SUB);
        match_and_return(SLL);
        match_and_return(SLT);
        match_and_return(SLTU);
        match_and_return(XOR);
        match_and_return(SRL);
        match_and_return(SRA);
        match_and_return(OR);
        match_and_return(AND);

        match_and_return(MUL);
        match_and_return(MULH);
        match_and_return(MULHSU);
        match_and_return(MULHU);
        match_and_return(DIV);
        match_and_return(DIVU);
        match_and_return(REM);
        match_and_return(REMU);
    }
    #undef match_and_return

    handle_unknown_instruction(cmd);
}

static auto parse_i_type(std::uint32_t cmd) -> _Pair_t {
    auto i_type = command::i_type::from_integer(cmd);
    auto rs1 = int_to_reg(i_type.rs1);
    auto rd  = int_to_reg(i_type.rd);
    auto arg = interpreter::ArithImm {
        .rs1 = rs1, .rd = rd, .imm = i_type.get_imm()
    }.to_data();

    #define match_and_return(a) \
        case command::i_type::Funct3::a: \
            return { interpreter::ArithImm::fn <general::ArithOp::a>, arg }

    switch (i_type.funct3) {
        match_and_return(ADD);
        match_and_return(SLT);
        match_and_return(SLTU);
        match_and_return(XOR);
        match_and_return(OR);
        match_and_return(AND);

        case command::i_type::Funct3::SLL:
            if (command::get_funct7(cmd) != command::i_type::Funct7::SLL) break;
            return { interpreter::ArithImm::fn <general::ArithOp::SLL>, arg };

        case command::i_type::Funct3::SRL:
            if (command::get_funct7(cmd) == command::i_type::Funct7::SRL)
                return { interpreter::ArithImm::fn <general::ArithOp::SRL>, arg };
            if (command::get_funct7(cmd) == command::i_type::Funct7::SRA)
                return { interpreter::ArithImm::fn <general::ArithOp::SRA>, arg };
            break; // Invalid shift operation.
    }

    #undef match_and_return

    handle_unknown_instruction(cmd);
}

static auto parse_s_type(std::uint32_t cmd) -> _Pair_t {
    auto s_type = command::s_type::from_integer(cmd);
    auto rs1 = int_to_reg(s_type.rs1);
    auto rs2 = int_to_reg(s_type.rs2);
    auto arg = interpreter::LoadStore {
        .rs1 = rs1, .rd = rs2, .imm = s_type.get_imm()
    }.to_data();

    #define match_and_return(a) \
        case command::s_type::Funct3::a: \
            return { interpreter::LoadStore::fn <general::MemoryOp::a>, arg }

    switch (s_type.funct3) {
        match_and_return(SB);
        match_and_return(SH);
        match_and_return(SW);
    }

    #undef match_and_return
    handle_unknown_instruction(cmd);
}

static auto parse_l_type(std::uint32_t cmd) -> _Pair_t {
    auto l_type = command::l_type::from_integer(cmd);
    auto rs1 = int_to_reg(l_type.rs1);
    auto rd  = int_to_reg(l_type.rd);
    auto arg = interpreter::LoadStore {
        .rs1 = rs1, .rd = rd, .imm = l_type.get_imm()
    }.to_data();

    #define match_and_return(a) \
        case command::l_type::Funct3::a: \
            return { interpreter::LoadStore::fn <general::MemoryOp::a>, arg }

    switch (l_type.funct3) {
        match_and_return(LB);
        match_and_return(LH);
        match_and_return(LW);
        match_and_return(LBU);
        match_and_return(LHU);
    }

    #undef match_and_return
    handle_unknown_instruction(cmd);
}

static auto parse_b_type(std::uint32_t cmd) -> _Pair_t {
    auto b_type = command::b_type::from_integer(cmd);
    auto rs1 = int_to_reg(b_type.rs1);
    auto rs2 = int_to_reg(b_type.rs2);
    auto arg = interpreter::Branch {
        .rs1 = rs1, .rs2 = rs2, .imm = b_type.get_imm()
    }.to_data();

    #define match_and_return(a) \
        case command::b_type::Funct3::a: \
            return { interpreter::Branch::fn <general::BranchOp::a>, arg }

    switch (b_type.funct3) {
        match_and_return(BEQ);
        match_and_return(BNE);
        match_and_return(BLT);
        match_and_return(BGE);
        match_and_return(BLTU);
        match_and_return(BGEU);
    }

    #undef match_and_return
    handle_unknown_instruction(cmd);
}

static auto parse_auipc(std::uint32_t cmd) -> _Pair_t {
    auto auipc = command::auipc::from_integer(cmd);
    auto rd = int_to_reg(auipc.rd);
    auto arg = interpreter::Auipc {
        .rd = rd, .imm = auipc.get_imm()
    }.to_data();

    return { interpreter::Auipc::fn, arg };
}

static auto parse_lui(std::uint32_t cmd) -> _Pair_t {
    auto lui = command::lui::from_integer(cmd);
    auto rd = int_to_reg(lui.rd);
    auto arg = interpreter::Lui {
        .rd = rd, .imm = lui.get_imm()
    }.to_data();

    return { interpreter::Lui::fn, arg };
}

static auto parse_jal(std::uint32_t cmd) -> _Pair_t {
    auto jal = command::jal::from_integer(cmd);
    auto rd = int_to_reg(jal.rd);
    auto arg = interpreter::Jump {
        .rd = rd, .imm = jal.get_imm()
    }.to_data();

    return { interpreter::Jump::fn, arg };
}

static auto parse_jalr(std::uint32_t cmd) -> _Pair_t {
    auto jalr = command::jalr::from_integer(cmd);
    auto rs1 = int_to_reg(jalr.rs1);
    auto rd  = int_to_reg(jalr.rd);
    auto arg = interpreter::Jalr {
        .rs1 = rs1, .rd = rd, .imm = jalr.get_imm()
    }.to_data();

    return { interpreter::Jalr::fn, arg };
}

auto parse_cmd(std::uint32_t cmd) -> _Pair_t {
    switch (command::get_opcode(cmd)) {
        case command::r_type::opcode:
            return parse_r_type(cmd);
        case command::i_type::opcode:
            return parse_i_type(cmd);
        case command::s_type::opcode:
            return parse_s_type(cmd);
        case command::l_type::opcode:
            return parse_l_type(cmd);
        case command::b_type::opcode:
            return parse_b_type(cmd);
        case command::auipc::opcode:
            return parse_auipc(cmd);
        case command::lui::opcode:
            return parse_lui(cmd);
        case command::jal::opcode:
            return parse_jal(cmd);
        case command::jalr::opcode:
            return parse_jalr(cmd);
    }

    throw FailToInterpret {};
}

} // namespace dark::interpreter