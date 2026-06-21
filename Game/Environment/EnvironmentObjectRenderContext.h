#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include "Game/Environment/EnvironmentObjectRenderDataBuilder.h"
#include "Game/Environment/EnvironmentObjectTypes.h"
#include "RenderContract/Gather/RenderGatherResult.h"

namespace Game {
    struct EnvironmentObjectRenderContextStats final {
    public:
        std::size_t mResidentCellCount{};
        std::size_t mResidentInstanceContextCount{};
        std::size_t mResidentSegmentContextCount{};
        std::size_t mResidentDrawRecordCount{};
    };

    class EnvironmentObjectRenderContext final {
    public:
        EnvironmentObjectRenderContext();
        ~EnvironmentObjectRenderContext();

        EnvironmentObjectRenderContext(const EnvironmentObjectRenderContext& Other);
        EnvironmentObjectRenderContext& operator=(const EnvironmentObjectRenderContext& Other);

        EnvironmentObjectRenderContext(EnvironmentObjectRenderContext&& Other) noexcept;
        EnvironmentObjectRenderContext& operator=(EnvironmentObjectRenderContext&& Other) noexcept;

    public:
        void Clear();
        void SetResidentCellLimit(std::size_t ResidentCellLimit);
        std::size_t GetResidentCellLimit() const;
        bool Empty() const;
        bool ContainsCell(const EnvironmentObjectCellKey& Key) const;
        const EnvironmentObjectRenderPacket* FindCell(const EnvironmentObjectCellKey& Key) const;
        void RemoveCell(const EnvironmentObjectCellKey& Key);
        void UpsertCell(const EnvironmentObjectCell& Cell);
        void TouchCell(const EnvironmentObjectCellKey& Key, std::uint64_t FrameIndex);
        void TrimResidentCells();
        bool AppendCellRenderData(const EnvironmentObjectCellKey& Key, const EnvironmentObjectRenderBuildOptions& Options, RenderContract::RenderGatherResult& OutRenderGatherResult);
        void AppendVisibleCellRenderData(std::span<const EnvironmentObjectCellKey> CellKeys, const EnvironmentObjectRenderBuildOptions& Options, std::uint64_t FrameIndex, RenderContract::RenderGatherResult& OutRenderGatherResult);
        EnvironmentObjectRenderContextStats BuildStats() const;

    private:
        std::unordered_map<EnvironmentObjectCellKey, EnvironmentObjectRenderPacket, EnvironmentObjectCellKeyHasher> mCells{};
        std::size_t mResidentCellLimit{};
    };
}
