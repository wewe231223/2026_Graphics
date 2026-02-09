#pragma once
#include <cstddef>
#include <cstdint>
#include "TypeSystem.h" 

namespace Arche {

    struct EntityID {
        std::uint32_t index;
        std::uint32_t generation;

        bool operator==(const EntityID& other) const {
            return index == other.index && generation == other.generation;
        }

        bool operator!=(const EntityID& other) const {
            return !(*this == other);
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