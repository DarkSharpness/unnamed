#include <utility.h>
#include <assembly/assembly.h>
#include <assembly/helper.h>
#include <default_config.h>
#include <fstream>
#include <algorithm>

namespace dark {

Assembler::Assembler(std::string_view file_name) {
    using enum __console::Color;
    this->file_info = std::format("{}file: {}{}",
        __console::color_code <YELLOW>, file_name, __console::color_code <RESET>);

    std::ifstream file { std::string(file_name) };
    panic_if(!file.is_open(), "Failed to open {}", this->file_info);

    this->line_number = 0;

    std::string last;   // Last line
    std::string line;   // Current line

    while (std::getline(file, line)) {
        ++this->line_number;
        try {
            this->parse_line(line);
        } catch (FailToParse &e) {
            if (!e.inner.empty() && e.inner.back() != '\n')
                e.inner += "\n";

            constexpr auto make_line = [] (std::string_view line, std::size_t line_number) {
                return std::format("{: >4}  |  {}", line_number, line);
            };

            std::string line_fmt = make_line(line, this->line_number);
            if (this->line_number != 1)
                line_fmt = std::format("{}\n{}",
                    make_line(last, this->line_number - 1), line_fmt);
            if (std::string temp; std::getline(file, temp))
                line_fmt = std::format("{}\n{}",
                    line_fmt, make_line(temp, this->line_number + 1));

            panic("{:}Failed to parse {}:{}\n{}",
                e.inner, this->file_info, this->line_number, line_fmt);
        } catch(std::exception &e) {
            std::cerr << std::format("Unexpected error: {}\n", e.what());
            runtime_assert(false);
        } catch(...) {
            std::cerr << "Unexpected error\n";
            runtime_assert(false);
        }
        last = std::move(line);
    }

    // this->debug(std::cout);
}

void Assembler::set_section(Assembler::Section section) {
    this->current_section = section;
    this->sections.emplace_back(this->storages.size(), section);
}

void Assembler::debug(std::ostream &os) const {
    if (this->sections.empty()) return;
    using _Pair_t = std::pair <std::size_t, const decltype(this->labels)::value_type *>;
    std::vector <_Pair_t> label_list;
    for (auto &pair : this->labels)
        label_list.emplace_back(pair.second.data_index, &pair);

    std::ranges::sort(label_list, [](auto &lhs, auto &rhs) {
        return lhs.first < rhs.first;
    });

    constexpr auto __print_section =
    [](std::ostream &os, auto *ptr, StorageSlice data, std::span <const _Pair_t> &view) {
        os << "    .section .";
        auto [slice, section] = data;
        switch (section) {
            case Assembler::Section::TEXT:   os << "text\n"; break;
            case Assembler::Section::DATA:   os << "data\n"; break;
            case Assembler::Section::BSS:    os << "bss\n"; break;
            case Assembler::Section::RODATA: os << "rodata\n"; break;
            default: os << "unknown\n";
        }
        for (auto &storage : slice) {
            std::size_t which = &storage - ptr;
            while (!view.empty() && view.front().first == which) {
                auto [_, label_ptr] = view.front();
                view = view.subspan(1);
                auto &[name, info] = *label_ptr;
                if (info.global) os << "    .globl " << name << '\n';
                os << name << ":\n";
            }
            storage->debug(os); os << '\n';
        }
    };

    std::span <const _Pair_t> label_view { label_list };

    for (auto part : this->split_by_section())
        __print_section(os, this->storages.data(), part, label_view);

}

/**
 * @brief Parse a line of the assembly.
 * @param line Line of the assembly.
*/
void Assembler::parse_line(std::string_view line) {
    line = remove_front_whitespace(line);

    // Whether the line starts with a label
    if (auto label = start_with_label(line); label.has_value())
        return this->add_label(*label);

    // Not a label, find the first token
    auto [token, rest] = find_first_token(line);

    // Empty line case, nothing happens
    if (token.empty()) return;

    if (token[0] != '.') {
        return this->parse_command(token, rest);
    } else {
        token.remove_prefix(1);
        return this->parse_storage(token, rest);
    }
}

/**
 * @brief Try to add a label to the assembly.
 * @param label Label name.
 * @return Whether the label is successfully added.
 */
void Assembler::add_label(std::string_view label) {
    auto [iter, success] = this->labels.try_emplace(std::string(label));

    throw_if <FailToParse> (success == false && iter->second.line_number != 0,
        "Label \"{}\" already exists\n"
        "First appearance at line {} in {}",
        label, iter->second.line_number, this->file_info);

    throw_if <FailToParse> (this->current_section == Assembler::Section::UNKNOWN,
        "Label must be defined in a section");

    auto &label_info = iter->second;
    label_info = Assembler::LabelData {
        .line_number = this->line_number,
        .data_index = this->storages.size(),
        .label_name = iter->first,
        .global     = label_info.global,
        .section    = this->current_section
    };
}

/**
 * @brief Parse the first token in the line.
 * @param token Initial token.
 * @param rest Rest of the line.
 * @return Whether the parsing is successful.
 */
void Assembler::parse_storage(std::string_view token, std::string_view rest) {
    constexpr auto __parse_section = [](std::string_view rest) -> std::optional <std::string_view> {
        rest = remove_front_whitespace(rest);
        if (!rest.empty() && rest.front() == '.') {
            rest.remove_prefix(1);
            if (match_prefix(rest, { "text" }))
                return "text";
            if (match_prefix(rest, { "data", "sdata" }))
                return "data";
            if (match_prefix(rest, { "bss", "sbss" }))
                return "bss";
            if (match_prefix(rest, { "rodata" }))
                return "rodata";
        }
        warning("Unknown section: {}", rest);
        return std::nullopt;
    };

    if (match_string(token, { "section" })) {
        if (auto result = __parse_section(rest); !result.has_value()) {
            return this->set_section(Assembler::Section::UNKNOWN);
        } else {
            token = *result;
            rest = std::string_view {};
        }
    }

    auto new_rest = this->parse_storage_impl(token, rest);

    // Fail to parse the token
    if (!contain_no_token(new_rest)) throw FailToParse {};
}

auto Assembler::parse_storage_impl(std::string_view token, std::string_view rest) -> std::string_view {
    constexpr auto __set_section = [](Assembler *ptr, std::string_view rest, Assembler::Section section) {
        ptr->set_section(section);
        return rest;
    };
    constexpr auto __set_globl = [](Assembler *ptr, std::string_view rest) {
        rest = remove_comments_when_no_string(rest);
        rest = remove_front_whitespace(rest);
        rest = remove_back_whitespace(rest);
        ptr->labels[std::string(rest)].global = true;
        return std::string_view {};
    };
    constexpr auto __set_align = [](Assembler *ptr, std::string_view rest) {
        using __config::kMaxAlign;
        auto [num_str, new_rest] = find_first_token(rest);
        auto num = sv_to_integer <std::size_t> (num_str).value_or(kMaxAlign);
        throw_if <FailToParse> (num >= kMaxAlign, "Invalid alignment value: \"{}\"", rest);
        ptr->storages.push_back(std::make_unique <Alignment> (std::size_t{1} << num));
        return new_rest;
    };
    constexpr auto __set_bytes = [](Assembler *ptr, std::string_view rest, IntegerData::Type n) {
        auto [first, new_rest] = find_first_token(rest);
        ptr->storages.push_back(std::make_unique <IntegerData> (first, n));
        return new_rest;
    };
    constexpr auto __set_asciz = [](Assembler *ptr, std::string_view rest) {
        auto [str, new_rest] = find_first_asciz(rest);
        ptr->storages.push_back(std::make_unique <ASCIZ> (std::move(str)));
        return new_rest;
    };
    constexpr auto __set_zeros = [](Assembler *ptr, std::string_view rest) {
        auto [num_str, new_rest] = find_first_token(rest);
        using __config::kMaxZeros;
        auto num = sv_to_integer <std::size_t> (num_str).value_or(kMaxZeros);
        throw_if <FailToParse> (num >= kMaxZeros, "Invalid zero count: \"{}\"", num_str);
        ptr->storages.push_back(std::make_unique <ZeroBytes> (num));
        return new_rest;
    };
    // Warn once for those known yet ignored attributes.
    constexpr auto __warn_once = [](std::string_view str) {
        static std::unordered_set <std::string> ignored_attributes;
        if (ignored_attributes.insert(std::string(str)).second)
            warning("attribute ignored: .{}", str);
        return std::string_view {};
    };

    using namespace ::dark::__hash;

    #define match_or_break(str, expr, ...) case switch_hash_impl(str):\
        if (token == str) { return expr(this, rest, ##__VA_ARGS__); } break

    switch (switch_hash_impl(token)) {
        // Data section
        match_or_break("data",      __set_section, Assembler::Section::DATA);
        match_or_break("sdata",     __set_section, Assembler::Section::DATA);
        match_or_break("bss",       __set_section, Assembler::Section::BSS);
        match_or_break("sbss",      __set_section, Assembler::Section::BSS);
        match_or_break("rodata",    __set_section, Assembler::Section::RODATA);
        match_or_break("text",      __set_section, Assembler::Section::TEXT);

        // Real storage
        match_or_break("align",     __set_align);
        match_or_break("p2align",   __set_align);
        match_or_break("byte",      __set_bytes, IntegerData::Type::BYTE);
        match_or_break("short",     __set_bytes, IntegerData::Type::SHORT);
        match_or_break("half",      __set_bytes, IntegerData::Type::SHORT);
        match_or_break("2byte",     __set_bytes, IntegerData::Type::SHORT);
        match_or_break("long",      __set_bytes, IntegerData::Type::LONG);
        match_or_break("word",      __set_bytes, IntegerData::Type::LONG);
        match_or_break("4byte",     __set_bytes, IntegerData::Type::LONG);
        match_or_break("string",    __set_asciz);
        match_or_break("asciz",     __set_asciz);
        match_or_break("zero",      __set_zeros);
        match_or_break("globl",     __set_globl);
    }
    #undef match_or_break

    return  __warn_once(token);
    // throw FailToParse { std::format("Unknown storage type: .{}", token) };
}

void Assembler::parse_command(std::string_view token, std::string_view rest) {
    return this->parse_command_impl(token, rest);
}

void Assembler::parse_command_impl(std::string_view token, std::string_view rest) {
    using Aop = ArithmeticReg::Opcode;
    using Mop = LoadStore::Opcode;
    using Bop = Branch::Opcode;

    constexpr auto __insert_arith_reg = [](Assembler *ptr, std::string_view rest, Aop opcode) {
        auto [rd, rs1, rs2] = split_command <3> (rest);
        ptr->storages.push_back(std::make_unique <ArithmeticReg> (opcode, rd, rs1, rs2));
    };
    constexpr auto __insert_arith_imm = [](Assembler *ptr, std::string_view rest, Aop opcode) {
        auto [rd, rs1, imm] = split_command <3> (rest);
        ptr->storages.push_back(std::make_unique <ArithmeticImm> (opcode, rd, rs1, imm));
    };
    constexpr auto __insert_load_store = [](Assembler *ptr, std::string_view rest, Mop opcode) {
        auto [rd, off_rs1] = split_command <2> (rest);
        auto [off, rs1] = split_offset_and_register(off_rs1);
        ptr->storages.push_back(std::make_unique <LoadStore> (opcode, rd, rs1, off));
    };
    constexpr auto __insert_branch = [](Assembler *ptr, std::string_view rest, Bop opcode, bool swap = false) {
        auto [rs1, rs2, label] = split_command <3> (rest);
        if (swap) std::swap(rs1, rs2);
        ptr->storages.push_back(std::make_unique <Branch> (opcode, rs1, rs2, label));
    };
    constexpr auto __insert_jump = [](Assembler *ptr, std::string_view rest) {
        auto [rd, offset] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <JumpRelative> (rd, offset));
    };
    constexpr auto __insert_jalr = [](Assembler *ptr, std::string_view rest) {
        auto [rd, off_rs1] = split_command <2> (rest);
        auto [off, rs1] = split_offset_and_register(off_rs1);
        ptr->storages.push_back(std::make_unique <JumpRegister> (rd, rs1, off));
    };
    constexpr auto __insert_lui  = [](Assembler *ptr, std::string_view rest) {
        auto [rd, imm] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <LoadUpperImmediate> (rd, imm));
    };
    constexpr auto __insert_auipc = [](Assembler *ptr, std::string_view rest) {
        auto [rd, imm] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <AddUpperImmediatePC> (rd, imm));
    };
    constexpr auto __insert_move = [](Assembler *ptr, std::string_view rest) {
        auto [rd, rs1] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <ArithmeticReg> (Aop::ADD, rd, rs1, "zero"));
    };
    constexpr auto __insert_li = [](Assembler *ptr, std::string_view rest) {
        auto [rd, imm] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <LoadImmediate> (rd, imm));
    };
    constexpr auto __insert_neg = [](Assembler *ptr, std::string_view rest) {
        auto [rd, rs1] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <ArithmeticReg> (Aop::SUB, rd, "zero", rs1));
    };
    constexpr auto __insert_not = [](Assembler *ptr, std::string_view rest) {
        auto [rd, rs1] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <ArithmeticImm> (Aop::XOR, rd, rs1, "-1"));
    };
    constexpr auto __insert_seqz = [](Assembler *ptr, std::string_view rest) {
        auto [rd, rs1] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <ArithmeticImm> (Aop::SLTU, rd, rs1, "1"));
    };
    constexpr auto __insert_snez = [](Assembler *ptr, std::string_view rest) {
        auto [rd, rs1] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <ArithmeticReg> (Aop::SLTU, rd, "zero", rs1));
    };
    constexpr auto __insert_sgtz = [](Assembler *ptr, std::string_view rest) {
        auto [rd, rs1] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <ArithmeticReg> (Aop::SLT, rd, "zero", rs1));
    };
    constexpr auto __insert_sltz = [](Assembler *ptr, std::string_view rest) {
        auto [rd, rs1] = split_command <2> (rest);
        ptr->storages.push_back(std::make_unique <ArithmeticReg> (Aop::SLT, rd, rs1, "zero"));
    };
    enum class _Cmp_type { EQZ, NEZ, LTZ, GTZ, LEZ, GEZ };
    constexpr auto __insert_brz = [](Assembler *ptr, std::string_view rest, _Cmp_type opcode) {
        auto [rs1, label] = split_command <2> (rest);
        #define try_match(cmp, op, ...) case cmp:\
            ptr->storages.push_back(std::make_unique <Branch> (op, ##__VA_ARGS__, label)); return
        switch (opcode) {
            try_match(_Cmp_type::EQZ, Bop::BEQ, rs1, "zero");
            try_match(_Cmp_type::NEZ, Bop::BNE, rs1, "zero");
            try_match(_Cmp_type::LTZ, Bop::BLT, rs1, "zero");
            try_match(_Cmp_type::GTZ, Bop::BLT, "zero", rs1);
            try_match(_Cmp_type::LEZ, Bop::BGE, "zero", rs1);
            try_match(_Cmp_type::GEZ, Bop::BGE, rs1, "zero");
        }
        #undef try_match
        runtime_assert(false);
    };
    constexpr auto __insert_call = [](Assembler *ptr, std::string_view rest, bool is_tail) {
        auto [offset] = split_command <1> (rest);
        ptr->storages.push_back(std::make_unique <CallFunction> (is_tail, offset));
    };
    constexpr auto __insert_j = [](Assembler *ptr, std::string_view rest) {
        auto [offset] = split_command <1> (rest);
        ptr->storages.push_back(std::make_unique <JumpRelative> ("zero", offset));
    };
    constexpr auto __insert_jr = [](Assembler *ptr, std::string_view rest) {
        auto [rs] = split_command <1> (rest);
        ptr->storages.push_back(std::make_unique <JumpRegister> ("zero", rs, "0"));
    };
    constexpr auto __insert_ret = [](Assembler *ptr, std::string_view rest) {
        split_command <0> (rest);
        ptr->storages.push_back(std::make_unique <JumpRegister> ("zero", "ra", "0"));
    };

    using namespace ::dark::__hash;

    #define match_or_break(str, expr, ...) case switch_hash_impl(str):\
        if (token == str) { return expr(this, rest, ##__VA_ARGS__); } break

    switch (switch_hash_impl(token)) {
        match_or_break("add",   __insert_arith_reg, Aop::ADD);
        match_or_break("sub",   __insert_arith_reg, Aop::SUB);
        match_or_break("and",   __insert_arith_reg, Aop::AND);
        match_or_break("or",    __insert_arith_reg, Aop::OR);
        match_or_break("xor",   __insert_arith_reg, Aop::XOR);
        match_or_break("sll",   __insert_arith_reg, Aop::SLL);
        match_or_break("srl",   __insert_arith_reg, Aop::SRL);
        match_or_break("sra",   __insert_arith_reg, Aop::SRA);
        match_or_break("slt",   __insert_arith_reg, Aop::SLT);
        match_or_break("sltu",  __insert_arith_reg, Aop::SLTU);

        match_or_break("mul",   __insert_arith_reg, Aop::MUL);
        match_or_break("mulh",  __insert_arith_reg, Aop::MULH);
        match_or_break("mulhu", __insert_arith_reg, Aop::MULHU);
        match_or_break("mulhsu",__insert_arith_reg, Aop::MULHSU);
        match_or_break("div",   __insert_arith_reg, Aop::DIV);
        match_or_break("divu",  __insert_arith_reg, Aop::DIVU);
        match_or_break("rem",   __insert_arith_reg, Aop::REM);
        match_or_break("remu",  __insert_arith_reg, Aop::REMU);

        match_or_break("addi",  __insert_arith_imm, Aop::ADD);
        match_or_break("andi",  __insert_arith_imm, Aop::AND);
        match_or_break("ori",   __insert_arith_imm, Aop::OR);
        match_or_break("xori",  __insert_arith_imm, Aop::XOR);
        match_or_break("slli",  __insert_arith_imm, Aop::SLL);
        match_or_break("srli",  __insert_arith_imm, Aop::SRL);
        match_or_break("srai",  __insert_arith_imm, Aop::SRA);
        match_or_break("slti",  __insert_arith_imm, Aop::SLT);
        match_or_break("sltiu", __insert_arith_imm, Aop::SLTU);
    
        match_or_break("lb",    __insert_load_store, Mop::LB);
        match_or_break("lh",    __insert_load_store, Mop::LH);
        match_or_break("lw",    __insert_load_store, Mop::LW);
        match_or_break("lbu",   __insert_load_store, Mop::LBU);
        match_or_break("lhu",   __insert_load_store, Mop::LHU);
        match_or_break("sb",    __insert_load_store, Mop::SB);
        match_or_break("sh",    __insert_load_store, Mop::SH);
        match_or_break("sw",    __insert_load_store, Mop::SW);

        match_or_break("beq",   __insert_branch, Bop::BEQ);
        match_or_break("bne",   __insert_branch, Bop::BNE);
        match_or_break("blt",   __insert_branch, Bop::BLT);
        match_or_break("bge",   __insert_branch, Bop::BGE);
        match_or_break("bltu",  __insert_branch, Bop::BLTU);
        match_or_break("bgeu",  __insert_branch, Bop::BGEU);

        match_or_break("jal",   __insert_jump);
        match_or_break("jalr",  __insert_jalr);

        match_or_break("lui",   __insert_lui);
        match_or_break("auipc", __insert_auipc);

        match_or_break("mv",    __insert_move);
        match_or_break("li",    __insert_li);

        match_or_break("neg",   __insert_neg);
        match_or_break("not",   __insert_not);

        match_or_break("seqz",  __insert_seqz);
        match_or_break("snez",  __insert_snez);
        match_or_break("sgtz",  __insert_sgtz);
        match_or_break("sltz",  __insert_sltz);

        match_or_break("beqz",  __insert_brz, _Cmp_type::EQZ);
        match_or_break("bnez",  __insert_brz, _Cmp_type::NEZ);
        match_or_break("bltz",  __insert_brz, _Cmp_type::LTZ);
        match_or_break("bgtz",  __insert_brz, _Cmp_type::GTZ);
        match_or_break("blez",  __insert_brz, _Cmp_type::LEZ);
        match_or_break("bgez",  __insert_brz, _Cmp_type::GEZ);

        match_or_break("ble",   __insert_branch, Bop::BGE, true);
        match_or_break("bleu",  __insert_branch, Bop::BGEU, true);
        match_or_break("bgt",   __insert_branch, Bop::BLT, true);
        match_or_break("bgtu",  __insert_branch, Bop::BLTU, true);

        match_or_break("call",  __insert_call, false);
        match_or_break("tail",  __insert_call, true);

        match_or_break("j",     __insert_j);
        match_or_break("jr",    __insert_jr);
        match_or_break("ret",   __insert_ret);

    }

    throw FailToParse { std::format("Unknown command: \"{}\"", token) };
}

auto Assembler::split_by_section() const -> std::vector <StorageSlice> {
    std::vector <StorageSlice> slices;
    if (this->sections.empty()) return slices;

    auto storage = std::span(this->storages);
    auto [prev, prev_section] = this->sections[0];
    for (std::size_t i = 1 ; i < this->sections.size() ; ++i) {
        auto [next, next_section] = this->sections[i];
        auto prefix_size = next - prev;
        slices.push_back({ storage.subspan(0, prefix_size), prev_section });
        storage = storage.subspan(prefix_size);
        prev = next, prev_section = next_section;
    }

    slices.push_back({ storage, prev_section });
    runtime_assert(prev_section == this->current_section);
    return slices;
}

} // namespace dark
