#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace Game {
    inline constexpr std::size_t ComponentTextMaxLength{ 128 };

    class ComponentTextArray final {
    public:
        using value_type = char;
        static constexpr std::size_t ElementCount{ ComponentTextMaxLength + 1 };

    public:
        value_type* data();
        const value_type* data() const;
        std::size_t size() const;
        value_type& operator[](std::size_t Index);
        const value_type& operator[](std::size_t Index) const;
        void fill(value_type Value);
        void Assign(std::string_view SourceText);
        std::string_view AsStringView() const;

    private:
        std::array<value_type, ElementCount> mText{};
    };
}
