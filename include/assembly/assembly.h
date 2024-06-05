#pragma once
#include <declarations.h>
#include <vector>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>

namespace dark {

/**
 * @brief Represents a collection of parts that are assembled together.
 * Supported part now:
 *  .text
 *  .data
 *  .sdata
 *  .rodata
 *  .srodata
 *  .bss
 *  .sbss
 *  .section
 *  .globl
 * 
 *  .align
 *  .p2align
 *  .balign
 * 
 *  .string
 *  .asciz
 *  .byte
 *  .2byte
 *  .half
 *  .short
 *  .4byte
 *  .long
 *  .word
 *  .type
 * Ignored attributes:
 *  .attribute
 *  .file
 *
 *  .size
 *  others
*/

struct Assembly {
    enum class Section : std::uint8_t {
        UNKNOWN, // Invalid section
        TEXT,   // Code
        DATA,   // Initialized data
        RODATA, // Read-only data
        BSS     // Initialized to zero
    } current_section { Section::UNKNOWN };
    struct LabelData {
        std::size_t line_number {};
        std::size_t data_index  {};
        std::string_view label_name;
        bool    global  {};
        Section section {};
    };
    struct Storage {
        virtual ~Storage() noexcept = default;
        virtual void debug(std::ostream&) const = 0;
    };

    std::unordered_map <std::string, LabelData> labels;
    std::vector <std::unique_ptr <Storage>> storages;
    std::vector <std::pair<std::size_t, Section>> sections;

    std::string         file_info;      // Debug information
    std::size_t         line_number;    // Debug information

    explicit Assembly(std::string_view);

  private:

    void set_section(Section);
    void add_label(std::string_view);
    void parse_line(std::string_view);
    void parse_command(std::string_view, std::string_view);
    void parse_storage(std::string_view, std::string_view);

    auto parse_storage_impl(std::string_view, std::string_view) -> std::string_view;
    void parse_command_impl(std::string_view, std::string_view);
    void debug(std::ostream &os);
};

} // namespace dark
