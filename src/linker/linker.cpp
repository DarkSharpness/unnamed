#include <utility.h>
#include <libc/libc.h>
#include <linker/linker.h>
#include <assembly/storage.h>
#include <assembly/assembly.h>
#include <ranges>
#include <algorithm>

namespace dark {

using Iterator = Linker::StorageDetails::Iterator;

bool operator == (const Iterator &lhs, const Iterator &rhs) {
    return lhs.storage == rhs.storage;
}

Iterator &Iterator::operator ++() {
    ++this->storage;
    this->location.next_offset();
    return *this;
}

Linker::StorageDetails::StorageDetails(_Slice_t storage, _Symbol_Table_t &table)
    : storage(storage), begin_position(),
      offsets(std::make_unique <std::size_t[]> (storage.size() + 1)),
      table(&table) {}

auto Linker::StorageDetails::begin() const -> Iterator {
    return Iterator {
        .storage    = this->storage.data(),
        .location   = SymbolLocation { *this, 0 }
    };
}

auto Linker::StorageDetails::end() const -> Iterator {
    return Iterator {
        .storage    = this->storage.data() + this->storage.size(),
        .location   = SymbolLocation { *this, this->storage.size() }
    };
}

Linker::SymbolLocation::SymbolLocation(const StorageDetails &details, std::size_t index)
    : absolute(&details.begin_position), offset(details.offsets.get() + index) {
    // Allow to be at the end, due to zero-size storage
    runtime_assert(index <= details.storage.size());
}

struct SymbolLocationLibc : Linker::SymbolLocation {
    explicit SymbolLocationLibc(const std::size_t &position, const std::size_t &offset)
        : Linker::SymbolLocation { &position, &offset } {}
};

/**
 * Memory Layout:
 * 1. Text Section from 0x10000 # 64KiB
 *  1.1 Libc functions
 *  1.2 User functions
 * 2. Static Data Section next
 *  2.1 Data Section
 *  2.2 RODATA Section
 *  2.3 BSS Section
 *   (all these parts may be merged into one section)
 * 3. Heap Space until 0x10000000 # 256MiB
 * 4. Stack from 0x10000000 # 256MiB
 *          to   0x20000000 # 512MiB
*/
Linker::Linker(std::span <Assembler> data) {
    std::vector <_Symbol_Table_t> local_symbol_table(data.size());

    for (auto i : std::views::iota(0llu, data.size()))
        this->add_file(data[i], local_symbol_table[i]);

    this->add_libc();

    this->make_estimate();

    this->make_relaxation();

    this->make_estimate();

    this->link();
}


/** Add a file to the linker. */
void Linker::add_file(Assembler &assembler, _Symbol_Table_t &local_table) {
    using _Pair_t = std::pair <_Storage_t *, StorageDetails *>;
    using _Section_Map_t = struct : std::vector <_Pair_t> {
        void add_mapping(_Storage_t *pointer, StorageDetails *details)
        {   this->emplace_back(pointer, details);   }

        auto get_location(_Storage_t *pointer) -> SymbolLocation {
            auto pair = _Pair_t { pointer, nullptr };
            auto iter = std::ranges::upper_bound(*this, pair,
                [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });

            // The first no greater element
            runtime_assert(--iter >= this->begin());

            // Index of the element
            std::size_t index = pointer - iter->first;

            return SymbolLocation { *iter->second, index };
        }
    };

    _Section_Map_t section_map;

    // Initialize the section map
    for (auto [slice, section] : assembler.split_by_section()) {
        auto &vec       = this->get_section(section);
        auto &storage   = vec.emplace_back(slice, local_table);
        section_map.add_mapping(slice.data(), &storage);
    }

    const auto [storage, max_size] = assembler.get_storages();

    // Mark the position information for the labels
    for (auto &label : assembler.get_labels()) {
        auto &[line, index, name, global, section] = label;

        if (!label.is_defined() && global) assembler.handle_at(line,
            std::format("Symbol \"{}\" is declared global, but not defined", name));

        runtime_assert(index <= max_size);  // Allow to be at the end

        auto location = section_map.get_location(storage + index);
        auto &table = global ? this->global_symbol_table : local_table;

        auto [iter, success] = table.try_emplace(name, location);
        panic_if(!success, "Duplicate {} symbol \"{}\"", global ? "global" : "local", name);
    }
}


/**
 * Add the libc functions to the global symbol table.
 * It will set up the starting offset of the user functions.
 */
void Linker::add_libc() {
    // Set the initial offset
    // This is required by the RISC-V ABI
    static constexpr std::size_t absolute = 0x10000;

    // An additional bias is caused by the libc functions
    static constexpr auto offset_table = []() {
        std::array <std::size_t, std::size(libc::names)> array;
        for (auto i : std::views::iota(0llu, std::size(libc::names)))
            array[i] = i * sizeof(target_size_t);
        return array;
    } ();

    std::size_t count = 0;
    for (auto &name : libc::names) {
        auto location = SymbolLocationLibc { absolute, offset_table[count++] };
        auto [iter, success] = this->global_symbol_table.try_emplace(name, location);
        panic_if(!success, "Global symbol \"{}\" conflicts with libc", name);
    }

    runtime_assert(libc::kLibcEnd == absolute + count * sizeof(target_size_t));
}

auto Linker::get_section(Section section) -> _Details_Vec_t & {
    auto index = static_cast <std::size_t> (section);
    runtime_assert(index < this->kSections);
    return this->details_vec[index];
}

} // namespace dark