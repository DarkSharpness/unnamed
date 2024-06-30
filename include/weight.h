#pragma once
#include <span>
#include <array>
#include <string_view>

/* Some weight configurations. */
namespace dark::__config {

static constexpr std::size_t kArith     = 1;
static constexpr std::size_t kBitwise   = 1;
static constexpr std::size_t kBranch    = 10;
static constexpr std::size_t kMemory    = 64;
static constexpr std::size_t kMultiply  = 4;
static constexpr std::size_t kDivide    = 20;
static constexpr std::size_t kOther     = 0; // Magic number

static constexpr std::string_view arith_name_list[] = {
    "add", "sub", "lui", "slt", "sltu", "auipc",
};
static constexpr std::string_view bitwise_name_list[] = {
    "and", "or", "xor", "sll", "srl", "sra",
};
static constexpr std::string_view branch_name_list[] = {
    "beq", "bne", "blt", "bge", "bltu", "bgeu",
};
static constexpr std::string_view memory_name_list[] = {
    "lb", "lh", "lw", "lbu", "lhu", "sb", "sh", "sw",
};
static constexpr std::string_view multiply_name_list[] = {
    "mul", "mulh", "mulhsu", "mulhu",
};
static constexpr std::string_view divide_name_list[] = {
    "div", "divu", "rem", "remu",
};
static constexpr std::string_view other_name_list[] = {
    "jal=1",
    "jalr=2",
    "call=3",
};

using _Weight_Range_t = struct _Impl_0 {
    std::string_view name;
    std::span <const std::string_view> list;
    std::size_t weight;
    constexpr _Impl_0(
        std::string_view name,
        std::span <const std::string_view> list,
        std::size_t weight)
        : name(name), list(list), weight(weight) {}
};

static constexpr _Weight_Range_t weight_ranges[] = {
    { "arith"   , arith_name_list   , kArith    },
    { "bitwise" , bitwise_name_list , kBitwise },
    { "branch"  , branch_name_list  , kBranch   },
    { "memory"  , memory_name_list  , kMemory   },
    { "multiply", multiply_name_list, kMultiply },
    { "divide"  , divide_name_list  , kDivide   },
    { "others"  , other_name_list   , kOther    },
};

static constexpr std::size_t kWeightCount = []() {
    std::size_t total_weight = 0;
    for (const auto &[name, list, weight] : weight_ranges)
        total_weight += list.size();
    return total_weight;
} ();

static constexpr std::size_t kMaxAlign = 20;
static constexpr std::size_t kMaxZeros = 1ull << 32;

} // namespace dark::__config