#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include "Utility/ComponentRestraint.h"

namespace Game {
    inline constexpr ::std::size_t NameMaxLength{ 128 };
    using NameTextArray = ::std::array<char, NameMaxLength + 1>;

    ComponentDecl(
        Name,
        ComponentFields(
            ComponentField(NameTextArray, Text, {})
        ),
        BOOST_PP_SEQ_NIL
    );

    Name CreateNameComponent(::std::string_view SourceText);
    const char* GetNameText(const Name& NameComponent);
}
