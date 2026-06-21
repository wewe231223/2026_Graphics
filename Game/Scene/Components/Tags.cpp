#include "Tags.h"
#include <string>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* Tag::GetComponentInspectionName() {
        return "Tag";
    }

    void Tag::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Text", std::string{ GetTagTextView(*this) } });
    }

    Tag CreateTagComponent(std::string_view SourceText) {
        Tag NewTag{};
        NewTag.mText.Assign(SourceText);
        return NewTag;
    }

    std::string_view GetTagTextView(const Tag& TagComponent) {
        return TagComponent.mText.AsStringView();
    }

    const char* GetTagText(const Tag& TagComponent) {
        return TagComponent.mText.data();
    }
}
