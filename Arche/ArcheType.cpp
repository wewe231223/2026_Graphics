#include "Archetype.h"
#include "Memory.h"
#include <algorithm>
#include <cstring>
#include <new> 

using namespace Arche;

Archetype::Archetype(std::vector<TypeID> types, std::vector<size_t> sizes, std::vector<size_t> aligns) : mSignature(std::move(types)) {
    size_t bytesPerEntity{ 0 };

    for (size_t s : sizes) {
        bytesPerEntity += s;
    }
    bytesPerEntity += sizeof(EntityID); // 헤더를 포함한 전체 Entity 크기 

    size_t dataBufferSize{ sizeof(Chunk::data) };
    if (bytesPerEntity > 0) {
        mCapacity = static_cast<std::uint32_t>(dataBufferSize / bytesPerEntity); // 앞선 결과를 바탕으로 전체 capacity 계산 
        if (mCapacity > 0) {
			mCapacity--; // 안전 마진
        }
    }
    else {
        mCapacity = 0;
    }

    auto it = std::max_element(mSignature.begin(), mSignature.end());
    TypeID maxID{ (it != mSignature.end()) ? *it : 0 };

    mTypeColumnIndexLUT.resize(maxID + 1, -1); // 가장 큰 ID 에 맞게 LUT 초기화 

    size_t currentOffset{ 0 };
    for (size_t i = 0; i < mSignature.size(); ++i) {
        TypeID tId{ mSignature[i] };
        size_t s{ sizes[i] };                               // 크기
		size_t a{ aligns[i] };  					        // 정렬 요구사항     
        size_t padding{ (a - (currentOffset % a)) % a };    // 이전 타입 패딩 계산 ( aligns 사용 ) 
        currentOffset += padding;

        mLayout.emplace_back(tId, s, a, currentOffset);
        mTypeColumnIndexLUT[tId] = static_cast<std::int32_t>(i);
        currentOffset += s * mCapacity; // 해당 타입의 전체 크기 추가
    }

    size_t idAlign{ alignof(EntityID) };
	// ID 필드 정렬 패딩 계산
    size_t padding{ (idAlign - (currentOffset % idAlign)) % idAlign };
    currentOffset += padding;
    mEntityIDOffset = currentOffset;
}

Archetype::~Archetype() {
    for (Chunk* c : mChunks) {
        ChunkAllocator::Instance().Deallocate(c);
    }
}

bool Archetype::HasType(TypeID id) const {
    if (id >= mTypeColumnIndexLUT.size()) { // ID 가 LUT 범위 내에 있지도 않는다면 false 
        return false;
    }

	return mTypeColumnIndexLUT[id] != -1; // 해당 ID 가 -1 이 아니라면 존재
}

size_t Archetype::GetTotalEntityCount() const {
    std::shared_lock lock{ mRWLock }; // 읽기 락
    size_t total{ 0 };
    for (const auto* c : mChunks) {
        total += c->count;
    }
    return total;
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

    std::int32_t colIdx{ mTypeColumnIndexLUT[typeID] };
    if (colIdx == -1) {
        return nullptr;
    }

    return chunk->data + mLayout[colIdx].offset;
}

void* Archetype::GetComponentPtr(Chunk* chunk, std::uint32_t entityIdx, TypeID typeID) const {
    void* base{ GetBaseComponentArray(chunk, typeID) };
    if (!base) {
        return nullptr;
    }

    std::int32_t colIdx{ mTypeColumnIndexLUT[typeID] };

    return static_cast<std::byte*>(base) + (entityIdx * mLayout[colIdx].size);
}

EntityID* Archetype::GetEntityIDs(Chunk* chunk) const {
    return reinterpret_cast<EntityID*>(chunk->data + mEntityIDOffset);
}

void Archetype::PushEntity(EntityID id, const std::vector<std::pair<TypeID, const void*>>& components) {
    std::unique_lock<std::shared_mutex> lock{ mRWLock }; // 쓰기 락 

    if (mChunks.empty() or mChunks.back()->count >= mCapacity) {
        void* mem{ ChunkAllocator::Instance().Allocate() };
        mChunks.emplace_back(new (mem) Chunk()); // Placement new 
    }

    Chunk* chunk{ mChunks.back() };
    std::uint32_t idx{ chunk->count };
    GetEntityIDs(chunk)[idx] = id;

    for (const auto& [typeId, ptr] : components) { // 각 타입에 대해 
        if (typeId < mTypeColumnIndexLUT.size()) {
            std::int32_t colIdx{ mTypeColumnIndexLUT[typeId] };
            if (colIdx != -1) {
                const Column& col = mLayout[colIdx];
                std::memcpy(chunk->data + col.offset + (idx * col.size), ptr, col.size); // 적절한 컬럼으로 memcpy 
            }
        }
    }

    chunk->count++;
}

EntityID Archetype::PopEntity(std::uint32_t chunkIdx, std::uint32_t entityIdx) {
    std::unique_lock<std::shared_mutex> lock{ mRWLock }; // 쓰기 락 
	Chunk* chunk{ mChunks[chunkIdx] }; // 대상 청크
    Chunk* lastChunk{ mChunks.back() }; // 마지막 청크 
    std::uint32_t lastIdx{ lastChunk->count - 1 }; // 마지막 청크 마지막 컴포넌트 

	EntityID movedEntityID{ GetEntityIDs(lastChunk)[lastIdx] }; // 움직일 엔티티 ID
	if (chunk != lastChunk or entityIdx != lastIdx) { // 삭제할 놈이 마지막 놈이 아니라면 
        for (const auto& col : mLayout) { // 각 컬럼에 대해 memcpy 
            std::memcpy(chunk->data + col.offset + (entityIdx * col.size), lastChunk->data + col.offset + (lastIdx * col.size), col.size);
        }
        GetEntityIDs(chunk)[entityIdx] = movedEntityID; 
    }

    lastChunk->count--;
    if (lastChunk->count == 0 && mChunks.size() > 1) { // empty 청크 삭제 
        ChunkAllocator::Instance().Deallocate(lastChunk);
        mChunks.pop_back();
    }
    return movedEntityID;
}