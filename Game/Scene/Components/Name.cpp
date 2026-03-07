#include "Name.h"
#include <algorithm>

namespace Game {
    Name CreateNameComponent(std::string_view SourceText) {
        Name NewName{};
        const std::size_t CopyLength{ std::min(SourceText.size(), Name::MaxLength) };
        std::copy_n(SourceText.data(), CopyLength, NewName.Text.data());
        NewName.Text[CopyLength] = '\0';
        return NewName;
    }

    const char* GetNameText(const Name& NameComponent) {
        return NameComponent.Text.data();
    }
}
