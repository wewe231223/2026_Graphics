#include "Name.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* Name::GetComponentInspectionName() {
        return "Name";
    }

    void Name::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Text", std::format("{}", GetNameTextView(*this)) });
    }

    Name CreateNameComponent(std::string_view SourceText) {
        Name NewName{};
        NewName.Text.Assign(SourceText);
        return NewName;
    }

    std::string_view GetNameTextView(const Name& NameComponent) {
        return NameComponent.Text.AsStringView();
    }

    const char* GetNameText(const Name& NameComponent) {
        return NameComponent.Text.data();
    }
}
