#include "ArcheType.h"
#include "Memory.h"
#include <algorithm>
#include <cstring>
#include <new>

using namespace Arche;

namespace {
    size_t AlignUp(size_t Value, size_t Alignment) {
        size_t Remainder{ Value % Alignment };
        if (Remainder == 0) {
            return Value;
        }
        return Value + (Alignment - Remainder);
    }

    size_t CalculateRequiredBytes(std::uint32_t Capacity, const std::vector<size_t>& Sizes, const std::vector<size_t>& Aligns) {
        size_t Offset{ 0 };
        for (size_t Index{ 0 }; Index < Sizes.size(); ++Index) {
            Offset = AlignUp(Offset, Aligns[Index]);
            Offset += Sizes[Index] * static_cast<size_t>(Capacity);
        }
        Offset = AlignUp(Offset, alignof(EntityID));
        Offset += sizeof(EntityID) * static_cast<size_t>(Capacity);
        return Offset;
    }
}

Archetype::Archetype(std::vector<TypeID> types, std::vector<size_t> sizes, std::vector<size_t> aligns)
    : mSignature(std::move(types)) {
    TypeID MaxID{ 0 };
    auto MaxIt{ std::max_element(mSignature.begin(), mSignature.end()) };
    if (MaxIt != mSignature.end()) {
        MaxID = *MaxIt;
    }
    mTypeColumnIndexLUT.resize(MaxID + 1, -1);

    size_t DataBufferSize{ sizeof(Chunk::data) };
    std::uint32_t LowerBound{ 0 };
    std::uint32_t UpperBound{ static_cast<std::uint32_t>(DataBufferSize) };
    while (LowerBound < UpperBound) {
        std::uint32_t Mid{ static_cast<std::uint32_t>((static_cast<std::uint64_t>(LowerBound) + static_cast<std::uint64_t>(UpperBound) + 1ull) / 2ull) };
        if (CalculateRequiredBytes(Mid, sizes, aligns) <= DataBufferSize) {
            LowerBound = Mid;
        }
        else {
            UpperBound = Mid - 1;
        }
    }
    mCapacity = LowerBound;

    size_t CurrentOffset{ 0 };
    for (size_t Index{ 0 }; Index < mSignature.size(); ++Index) {
        TypeID Type{ mSignature[Index] };
        size_t Size{ sizes[Index] };
        size_t Align{ aligns[Index] };
        CurrentOffset = AlignUp(CurrentOffset, Align);
        mLayout.push_back(Column{ Type, Size, Align, CurrentOffset });
        mTypeColumnIndexLUT[Type] = static_cast<std::int32_t>(Index);
        CurrentOffset += Size * static_cast<size_t>(mCapacity);
    }

    CurrentOffset = AlignUp(CurrentOffset, alignof(EntityID));
    mEntityIDOffset = CurrentOffset;
}

Archetype::~Archetype() {
    for (Chunk* ChunkPtr : mChunks) {
        ChunkAllocator::Instance().Deallocate(ChunkPtr);
    }
}

bool Archetype::HasType(TypeID id) const {
    if (id >= mTypeColumnIndexLUT.size()) {
        return false;
    }
    return mTypeColumnIndexLUT[id] != -1;
}

size_t Archetype::GetTotalEntityCount() const {
    std::shared_lock lock{ mRWLock };
    size_t Total{ 0 };
    for (const Chunk* ChunkPtr : mChunks) {
        Total += ChunkPtr->count;
    }
    return Total;
}

const std::vector<Chunk*>& Arche::Archetype::GetChunks() const {
    return mChunks;
}

const std::vector<TypeID>& Arche::Archetype::GetSignature() const {
    return mSignature;
}

void* Archetype::GetBaseComponentArray(Chunk* chunk, TypeID typeID) const {
    if (typeID >= mTypeColumnIndexLUT.size()) {
        return nullptr;
    }

    std::int32_t ColumnIndex{ mTypeColumnIndexLUT[typeID] };
    if (ColumnIndex == -1) {
        return nullptr;
    }

    return chunk->data + mLayout[ColumnIndex].offset;
}

void* Archetype::GetComponentPtr(Chunk* chunk, std::uint32_t entityIdx, TypeID typeID) const {
    void* Base{ GetBaseComponentArray(chunk, typeID) };
    if (Base == nullptr) {
        return nullptr;
    }

    std::int32_t ColumnIndex{ mTypeColumnIndexLUT[typeID] };
    return static_cast<std::byte*>(Base) + (entityIdx * mLayout[ColumnIndex].size);
}

EntityID* Archetype::GetEntityIDs(Chunk* chunk) const {
    return reinterpret_cast<EntityID*>(chunk->data + mEntityIDOffset);
}

void Archetype::PushEntity(EntityID id, const std::vector<std::pair<TypeID, const void*>>& components) {
    std::unique_lock<std::shared_mutex> lock{ mRWLock };
    if (mCapacity == 0) {
        return;
    }

    if (mChunks.empty() or mChunks.back()->count >= mCapacity) {
        void* Memory{ ChunkAllocator::Instance().Allocate() };
        mChunks.push_back(new (Memory) Chunk());
    }

    Chunk* ChunkPtr{ mChunks.back() };
    std::uint32_t Index{ ChunkPtr->count };
    GetEntityIDs(ChunkPtr)[Index] = id;

    for (const auto& [TypeIDValue, Ptr] : components) {
        if (TypeIDValue >= mTypeColumnIndexLUT.size()) {
            continue;
        }
        std::int32_t ColumnIndex{ mTypeColumnIndexLUT[TypeIDValue] };
        if (ColumnIndex == -1) {
            continue;
        }
        const Column& ColumnInfo{ mLayout[ColumnIndex] };
        std::memcpy(ChunkPtr->data + ColumnInfo.offset + (Index * ColumnInfo.size), Ptr, ColumnInfo.size);
    }

    ChunkPtr->count++;
}

EntityID Archetype::PopEntity(std::uint32_t chunkIdx, std::uint32_t entityIdx) {
    std::unique_lock<std::shared_mutex> lock{ mRWLock };
    Chunk* ChunkPtr{ mChunks[chunkIdx] };
    Chunk* LastChunk{ mChunks.back() };
    std::uint32_t LastIndex{ LastChunk->count - 1 };

    EntityID MovedEntityID{ GetEntityIDs(LastChunk)[LastIndex] };
    if (ChunkPtr != LastChunk or entityIdx != LastIndex) {
        for (const Column& ColumnInfo : mLayout) {
            std::memcpy(ChunkPtr->data + ColumnInfo.offset + (entityIdx * ColumnInfo.size), LastChunk->data + ColumnInfo.offset + (LastIndex * ColumnInfo.size), ColumnInfo.size);
        }
        GetEntityIDs(ChunkPtr)[entityIdx] = MovedEntityID;
    }

    LastChunk->count--;
    if (LastChunk->count == 0 and mChunks.size() > 1) {
        ChunkAllocator::Instance().Deallocate(LastChunk);
        mChunks.pop_back();
    }

    return MovedEntityID;
}
