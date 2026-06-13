#include "EnvironmentObjectRenderContext.h"

#include <limits>
#include <utility>

namespace Game {
    EnvironmentObjectRenderContext::EnvironmentObjectRenderContext()
        : mCells{},
        mResidentCellLimit{} {
    }

    EnvironmentObjectRenderContext::~EnvironmentObjectRenderContext() {
    }

    EnvironmentObjectRenderContext::EnvironmentObjectRenderContext(const EnvironmentObjectRenderContext& Other)
        : mCells{ Other.mCells },
        mResidentCellLimit{ Other.mResidentCellLimit } {
    }

    EnvironmentObjectRenderContext& EnvironmentObjectRenderContext::operator=(const EnvironmentObjectRenderContext& Other) {
        if (this == &Other) {
            return *this;
        }

        mCells = Other.mCells;
        mResidentCellLimit = Other.mResidentCellLimit;
        return *this;
    }

    EnvironmentObjectRenderContext::EnvironmentObjectRenderContext(EnvironmentObjectRenderContext&& Other) noexcept
        : mCells{ std::move(Other.mCells) },
        mResidentCellLimit{ Other.mResidentCellLimit } {
    }

    EnvironmentObjectRenderContext& EnvironmentObjectRenderContext::operator=(EnvironmentObjectRenderContext&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mCells = std::move(Other.mCells);
        mResidentCellLimit = Other.mResidentCellLimit;
        return *this;
    }

    void EnvironmentObjectRenderContext::Clear() {
        mCells.clear();
    }

    void EnvironmentObjectRenderContext::SetResidentCellLimit(std::size_t ResidentCellLimit) {
        mResidentCellLimit = ResidentCellLimit;
        TrimResidentCells();
    }

    std::size_t EnvironmentObjectRenderContext::GetResidentCellLimit() const {
        return mResidentCellLimit;
    }

    bool EnvironmentObjectRenderContext::Empty() const {
        return mCells.empty();
    }

    bool EnvironmentObjectRenderContext::ContainsCell(const EnvironmentObjectCellKey& Key) const {
        return mCells.find(Key) != mCells.end();
    }

    const EnvironmentObjectRenderPacket* EnvironmentObjectRenderContext::FindCell(const EnvironmentObjectCellKey& Key) const {
        const std::unordered_map<EnvironmentObjectCellKey, EnvironmentObjectRenderPacket, EnvironmentObjectCellKeyHasher>::const_iterator FoundIterator{ mCells.find(Key) };
        if (FoundIterator == mCells.end()) {
            return nullptr;
        }

        return &FoundIterator->second;
    }

    void EnvironmentObjectRenderContext::RemoveCell(const EnvironmentObjectCellKey& Key) {
        mCells.erase(Key);
    }

    void EnvironmentObjectRenderContext::UpsertCell(const EnvironmentObjectCell& Cell) {
        std::unordered_map<EnvironmentObjectCellKey, EnvironmentObjectRenderPacket, EnvironmentObjectCellKeyHasher>::iterator FoundIterator{ mCells.find(Cell.mKey) };
        if (FoundIterator != mCells.end() && FoundIterator->second.mGenerationVersion == Cell.mGenerationVersion) {
            FoundIterator->second.mLastTouchedFrame = Cell.mLastTouchedFrame;
            return;
        }

        EnvironmentObjectRenderPacket Packet{ BuildEnvironmentObjectRenderPacket(Cell) };
        if (EmptyEnvironmentObjectRenderPacket(Packet) == true) {
            RemoveCell(Cell.mKey);
            return;
        }

        mCells[Cell.mKey] = std::move(Packet);
        TrimResidentCells();
    }

    void EnvironmentObjectRenderContext::TouchCell(const EnvironmentObjectCellKey& Key, std::uint64_t FrameIndex) {
        std::unordered_map<EnvironmentObjectCellKey, EnvironmentObjectRenderPacket, EnvironmentObjectCellKeyHasher>::iterator FoundIterator{ mCells.find(Key) };
        if (FoundIterator == mCells.end()) {
            return;
        }

        FoundIterator->second.mLastTouchedFrame = FrameIndex;
    }

    void EnvironmentObjectRenderContext::TrimResidentCells() {
        if (mResidentCellLimit == 0u) {
            return;
        }

        while (mCells.size() > mResidentCellLimit) {
            std::unordered_map<EnvironmentObjectCellKey, EnvironmentObjectRenderPacket, EnvironmentObjectCellKeyHasher>::iterator OldestIterator{ mCells.end() };
            std::uint64_t OldestTouchedFrame{ std::numeric_limits<std::uint64_t>::max() };
            for (std::unordered_map<EnvironmentObjectCellKey, EnvironmentObjectRenderPacket, EnvironmentObjectCellKeyHasher>::iterator Iterator{ mCells.begin() }; Iterator != mCells.end(); ++Iterator) {
                if (Iterator->second.mLastTouchedFrame < OldestTouchedFrame) {
                    OldestTouchedFrame = Iterator->second.mLastTouchedFrame;
                    OldestIterator = Iterator;
                }
            }

            if (OldestIterator == mCells.end()) {
                return;
            }

            mCells.erase(OldestIterator);
        }
    }

    bool EnvironmentObjectRenderContext::AppendCellRenderData(const EnvironmentObjectCellKey& Key, const EnvironmentObjectRenderBuildOptions& Options, Pipeline::RenderGatherResult& OutRenderGatherResult) {
        const EnvironmentObjectRenderPacket* Packet{ FindCell(Key) };
        if (Packet == nullptr) {
            return false;
        }

        AppendEnvironmentObjectRenderPacketData(*Packet, Options, OutRenderGatherResult);
        return true;
    }

    void EnvironmentObjectRenderContext::AppendVisibleCellRenderData(std::span<const EnvironmentObjectCellKey> CellKeys, const EnvironmentObjectRenderBuildOptions& Options, std::uint64_t FrameIndex, Pipeline::RenderGatherResult& OutRenderGatherResult) {
        for (const EnvironmentObjectCellKey& CellKey : CellKeys) {
            TouchCell(CellKey, FrameIndex);
            AppendCellRenderData(CellKey, Options, OutRenderGatherResult);
        }

        TrimResidentCells();
    }

    EnvironmentObjectRenderContextStats EnvironmentObjectRenderContext::BuildStats() const {
        EnvironmentObjectRenderContextStats Stats{};
        Stats.mResidentCellCount = mCells.size();
        for (const std::pair<const EnvironmentObjectCellKey, EnvironmentObjectRenderPacket>& Cell : mCells) {
            const EnvironmentObjectRenderPacket& Packet{ Cell.second };
            Stats.mResidentInstanceContextCount += Packet.mInstanceContexts.size();
            for (const EnvironmentObjectRenderPacketLod& Lod : Packet.mLods) {
                Stats.mResidentSegmentContextCount += Lod.mSegmentContexts.size();
                Stats.mResidentDrawRecordCount += Lod.mDrawRecords.size();
            }
        }

        return Stats;
    }
}
