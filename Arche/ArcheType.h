#pragma once
#include <vector>
#include <shared_mutex>
#include <atomic>
#include "Common.h"

namespace Arche {
    class World; 

    class Archetype {
        friend class World;
    public:
        struct Column {
            TypeID id;
            size_t size;
            size_t align; // 각 Column 의 정렬 크기 요구사항. 
            size_t offset;
        };

    public:
        Archetype(std::vector<TypeID> types, std::vector<size_t> sizes, std::vector<size_t> aligns);
        ~Archetype();
        
        Archetype(const Archetype&) = delete;
        Archetype& operator=(const Archetype&) = delete;

        Archetype(Archetype&&) = delete;
		Archetype& operator=(Archetype&&) = delete;

    public:
        bool HasType(TypeID id) const;
        size_t GetTotalEntityCount() const;
        const std::vector<Chunk*>& GetChunks() const;
        const std::vector<TypeID>& GetSignature() const;

        void* GetBaseComponentArray(Chunk* chunk, TypeID typeID) const;
        void* GetComponentPtr(Chunk* chunk, std::uint32_t entityIdx, TypeID typeID) const;
        EntityID* GetEntityIDs(Chunk* chunk) const;

        void PushEntity(EntityID id, const std::vector<std::pair<TypeID, const void*>>& components);
        EntityID PopEntity(std::uint32_t chunkIdx, std::uint32_t entityIdx);

    private:
        const std::vector<TypeID> mSignature{};
        std::vector<Column> mLayout{};
        std::vector<Chunk*> mChunks{};
        std::uint32_t mCapacity{ 0 };
        size_t mEntityIDOffset{ 0 };
        std::vector<std::int32_t> mTypeColumnIndexLUT{};
        mutable std::shared_mutex mRWLock{};
    };
}
