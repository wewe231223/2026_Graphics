#pragma once
#include <cstddef>
#include <string_view>

namespace Arche {

    using TypeID = std::size_t;

    namespace Internal {
        constexpr std::size_t FNV1a_64(std::string_view str) {
            std::size_t hash = 14695981039346656037ull;
            for (char c : str) {
                hash ^= static_cast<std::size_t>(c);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        template <typename T>
        consteval std::string_view GetTypeName() {
#if defined(_MSC_VER)
            return __FUNCSIG__;
#else
            return __PRETTY_FUNCTION__;
#endif
        }
    }

    template <typename T>
    struct TypeHash {
        static constexpr std::string_view Name = Internal::GetTypeName<T>();
        // Value 까지 constexpr. 이후 RT 에서 ID 결정 
        static constexpr std::size_t Value = Internal::FNV1a_64(Name);
    };

    TypeID GetRuntimeTypeID(std::size_t typeHash);

    template <typename T>
    struct TypeInfo {
        static constexpr std::size_t Hash = TypeHash<T>::Value;
        static constexpr std::size_t Size = sizeof(T);
        static constexpr std::size_t Align = alignof(T);

        static inline const TypeID ID = GetRuntimeTypeID(Hash);
    };
} // namespace Arche
