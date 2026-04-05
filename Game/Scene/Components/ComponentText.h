#pragma once

#include <array>
#include <cstddef>

namespace Game {
    inline constexpr ::std::size_t ComponentTextMaxLength{ 128 };
    using ComponentTextArray = ::std::array<char, ComponentTextMaxLength + 1>;
}
