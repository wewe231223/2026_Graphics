#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
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
        std::uint64_t flags;

        static constexpr std::uint64_t DerivedEntityFlagMask{ 1ull << 63ull };

        EntityID() = default;
        EntityID(std::uint32_t Index, std::uint32_t Generation, std::uint64_t Flags = 0ull)
            : index{ Index },
            generation{ Generation },
            flags{ Flags } {
        }

        EntityID(NullEntityIDTag)
            : index{ std::numeric_limits<std::uint32_t>::max() },
            generation{ std::numeric_limits<std::uint32_t>::max() },
            flags{ std::numeric_limits<std::uint64_t>::max() } {
        }

        bool operator==(const EntityID& Other) const {
            return index == Other.index && generation == Other.generation;
        }

        bool operator!=(const EntityID& Other) const {
            return !(*this == Other);
        }

        bool IsDerivedEntity() const {
            return (flags & DerivedEntityFlagMask) != 0ull;
        }

        void SetDerivedEntity(bool IsDerivedEntityFlag) {
            if (IsDerivedEntityFlag == true) {
                flags |= DerivedEntityFlagMask;
                return;
            }

            flags &= ~DerivedEntityFlagMask;
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

namespace std {
    template<>
    struct hash<Arche::EntityID> {
        std::size_t operator()(const Arche::EntityID& Value) const noexcept {
            const std::size_t IndexHash{ std::hash<std::uint32_t>{}(Value.index) };
            const std::size_t GenerationHash{ std::hash<std::uint32_t>{}(Value.generation) };
            return IndexHash ^ (GenerationHash + 0x9e3779b97f4a7c15ull + (IndexHash << 6) + (IndexHash >> 2));
        }
    };
}
