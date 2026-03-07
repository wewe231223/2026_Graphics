#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include "Utility/ComponentRestraint.h"

namespace Game {
    Component(Name)
        static constexpr std::size_t MaxLength{ 128 };
        std::array<char, MaxLength + 1> Text{};
    EndComponent(Name)

    Name CreateNameComponent(std::string_view SourceText);
    const char* GetNameText(const Name& NameComponent);
}
