#pragma once

#include <string_view>
#include "Game/Scene/Components/ComponentText.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        Tag,
        ComponentFields(
            ComponentField(ComponentTextArray, mText)
        ),
        BOOST_PP_SEQ_NIL
    );

    Tag CreateTagComponent(std::string_view SourceText);
    std::string_view GetTagTextView(const Tag& TagComponent);
    const char* GetTagText(const Tag& TagComponent);
}
