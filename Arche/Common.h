#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include "TypeSystem.h" 

#ifdef max 
#undef max 
#endif 

namespace Arche {

    struct NullEntityIDTag {};

    inline constexpr NullEntityIDTag NullEntityID{};

    struct EntityID {
        std::uint32_t index;
        std::uint32_t generation;

        EntityID() = default;
        EntityID(std::uint32_t Index, std::uint32_t Generation)
            : index{ Index },
            generation{ Generation } {
        }

        EntityID(NullEntityIDTag)
            : index{ std::numeric_limits<std::uint32_t>::max() },
            generation{ std::numeric_limits<std::uint32_t>::max() } {
        }

        bool operator==(const EntityID& Other) const {
            return index == Other.index && generation == Other.generation;
        }

        bool operator!=(const EntityID& Other) const {
            return !(*this == Other);
        }
    };


    struct EntityRecord {
        class Archetype* archetype = nullptr;
        std::uint32_t chunkIndex = 0;
        std::uint32_t entityIndex = 0;
        std::uint32_t generation = 0;
        bool active = false;
    };

    constexpr size_t CHUNK_SIZE = 16 * 1024;

    struct Chunk {
        std::uint32_t count = 0;
        // alignas 64 를 사용하였기 때문에, sizeof(uint32_t) 인 4 가 아닌 64 바이트가 차지됨
        alignas(64) std::byte data[CHUNK_SIZE - 64];
    };

    static_assert(sizeof(Chunk) <= CHUNK_SIZE, "Chunk size exceeds allocated memory!");

} // namespace Arche
