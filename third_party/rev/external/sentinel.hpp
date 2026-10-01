#pragma once

#include <bit>
#include <cstdint>

namespace sentinel {

    inline constexpr float null = -0.0f;

    // Returns if value is set
    inline bool set(float& f) {
        return std::bit_cast<uint32_t>(f) != 0x80000000;
    };
};
