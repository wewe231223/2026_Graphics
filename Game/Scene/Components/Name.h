#pragma once

#include <string_view>
#include "Game/Scene/Components/ComponentText.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    ComponentDecl(
        Name,
        ComponentFields(
            ComponentField(ComponentTextArray, Text)
        ),
        BOOST_PP_SEQ_NIL
    );

    Name CreateNameComponent(std::string_view SourceText);
    std::string_view GetNameTextView(const Name& NameComponent);
    const char* GetNameText(const Name& NameComponent);
}
