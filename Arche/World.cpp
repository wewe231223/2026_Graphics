#include "ArcheContainer.h"

using namespace Arche;

Archetype* ArcheContainer::GetOrCreateArchetype(std::span<const TypeID> sortedIDs, std::span<const size_t> sizes, std::span<const size_t> aligns) {
    for (auto& arch : mArcheTypes) { // ArcheType 순회 
        const auto& sig = arch->GetSignature();
        if (sig.size() == sortedIDs.size() && std::equal(sig.begin(), sig.end(), sortedIDs.begin())) { // 찾는게 맞으면? 
            return arch.get();
        }
    }

    // 새 ArcheType 생성 과정 
    std::vector<TypeID> vecIDs(sortedIDs.begin(), sortedIDs.end());
    std::vector<size_t> vecSizes(sizes.begin(), sizes.end());
    std::vector<size_t> vecAligns(aligns.begin(), aligns.end());

    mArcheTypes.push_back(std::make_unique<Archetype>(std::move(vecIDs), std::move(vecSizes), std::move(vecAligns)));
    Archetype* newArch = mArcheTypes.back().get();

    // Query 캐시 갱신 
    for (auto& cache : mQueryCaches) {
        const auto& as = newArch->GetSignature();
        if (std::includes(as.begin(), as.end(), cache.signature.begin(), cache.signature.end())) {
            cache.archetypes.push_back(newArch);
        }
    }

    return newArch;
}


void ArcheContainer::DestroyEntity(EntityID id) {
    if (id.index >= mEntityTable.size()) { // 에러 id 
        return;
    }

    EntityRecord& record{ mEntityTable[id.index] };
    if (!record.active or record.generation != id.generation) { // invalid id 
        return;
    }

    EntityID movedID{ record.archetype->PopEntity(record.chunkIndex, record.entityIndex) };
    if (movedID != id) { // 삭제된 자리에 들어온 엔티티와 삭제한 엔티티가 다르다면 
		EntityRecord& movedRecord{ mEntityTable[movedID.index] }; // 채워진 엔티티로 레코드 갱신 
        movedRecord.chunkIndex = record.chunkIndex; 
        movedRecord.entityIndex = record.entityIndex;
    }

    // 비활성화 
    record.active = false; 
    record.archetype = nullptr; 
    record.generation++;

    // 빈자리 기록 
    mFreeIndices.push_back(id.index);
}

void ArcheContainer::GetArchetypeInfo(Archetype* arch, std::vector<TypeID>& outIds, std::vector<size_t>& outSizes, std::vector<size_t>& outAligns) {
    outIds.reserve(arch->mLayout.size());
    outSizes.reserve(arch->mLayout.size());
    outAligns.reserve(arch->mLayout.size());

    for (const auto& col : arch->mLayout) {
        outIds.push_back(col.id);
        outSizes.push_back(col.size);
        outAligns.push_back(col.align);
    }
}
