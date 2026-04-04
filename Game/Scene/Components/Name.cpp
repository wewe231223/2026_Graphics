#include "Name.h"
#include <algorithm>
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* Name::GetComponentInspectionName() {
        return "Name";
    }

    void Name::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Text", std::format("{}", Text.data()) });
    }

    Name CreateNameComponent(std::string_view SourceText) {
        Name NewName{};
        const std::size_t CopyLength{ std::min(SourceText.size(), NameMaxLength) };
        std::copy_n(SourceText.data(), CopyLength, NewName.Text.data());
        NewName.Text[CopyLength] = '\0';
        return NewName;
    }

    const char* GetNameText(const Name& NameComponent) {
        return NameComponent.Text.data();
    }
}
