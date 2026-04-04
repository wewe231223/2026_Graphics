#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include "Utility/ComponentRestraint.h"

namespace Game {
    inline constexpr std::size_t NameMaxLength{ 128 };

    Component(Name)
        std::array<char, NameMaxLength + 1> Text{};
    EndComponent(Name)

    Name CreateNameComponent(std::string_view SourceText);
    const char* GetNameText(const Name& NameComponent);
}
