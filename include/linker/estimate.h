#pragma once
#include "linker.h"
#include <assembly/storage.h>

namespace dark::__details {

template <std::derived_from <RealData> _Data>
inline constexpr std::size_t align_size(_Data &storage) {
    if constexpr (std::same_as <_Data, Alignment>) {
        return storage.alignment;
    } else if constexpr (std::same_as <_Data, IntegerData>) {
        return std::size_t(1) << static_cast <int> (storage.type);
    } else if constexpr (std::same_as <_Data, ZeroBytes>) {
        return 1;
    } else if constexpr (std::same_as <_Data, ASCIZ>) {
        return 1;
    } else {
        static_assert(sizeof(_Data) != sizeof(_Data), "Invalid type");
    }
}

template <std::derived_from <RealData> _Data>
inline constexpr std::size_t real_size(_Data &storage) {
    if constexpr (std::same_as <_Data, Alignment>) {
        return 0;
    } else if constexpr (std::same_as <_Data, IntegerData>) {
        return align_size(storage) * 1;
    } else if constexpr (std::same_as <_Data, ZeroBytes>) {
        return storage.count;
    } else if constexpr (std::same_as <_Data, ASCIZ>) {
        return storage.data.size() + 1; // Null terminator
    } else {
        static_assert(sizeof(_Data) != sizeof(_Data), "Invalid type");
    }
}

} // namespace dark
