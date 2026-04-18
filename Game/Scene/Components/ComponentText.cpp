#include "Game/Scene/Components/ComponentText.h"

#include <algorithm>

namespace Game {
    ComponentTextArray::value_type* ComponentTextArray::data() {
        return mText.data();
    }

    const ComponentTextArray::value_type* ComponentTextArray::data() const {
        return mText.data();
    }

    ::std::size_t ComponentTextArray::size() const {
        return ElementCount;
    }

    ComponentTextArray::value_type& ComponentTextArray::operator[](::std::size_t Index) {
        return mText[Index];
    }

    const ComponentTextArray::value_type& ComponentTextArray::operator[](::std::size_t Index) const {
        return mText[Index];
    }

    void ComponentTextArray::fill(const value_type Value) {
        mText.fill(Value);
    }

    void ComponentTextArray::Assign(const ::std::string_view SourceText) {
        mText.fill('\0');
        const ::std::size_t CopyLength{ ::std::min(SourceText.size(), ComponentTextMaxLength) };
        ::std::copy_n(SourceText.data(), CopyLength, mText.data());
    }

    ::std::string_view ComponentTextArray::AsStringView() const {
        ::std::size_t TextLength{};
        while (TextLength < ComponentTextMaxLength && mText[TextLength] != '\0') {
            TextLength = TextLength + 1;
        }

        return ::std::string_view{ mText.data(), TextLength };
    }
}
