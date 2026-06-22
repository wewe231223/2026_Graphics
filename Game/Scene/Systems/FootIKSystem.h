#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include <DirectXTK12/SimpleMath.h>

#include "Arche/Common.h"
#include "Game/Scene/IK/FootIKSolver.h"
#include "Game/Scene/Base/System.h"

namespace Terrain {
    class ITerrainQuery;
}

namespace Game {
    namespace Pipeline {
        class PipelineFootIKSystem final : public IPipelineSystem {
        public:
            PipelineFootIKSystem();
            ~PipelineFootIKSystem() override;

            PipelineFootIKSystem(const PipelineFootIKSystem& Other);
            PipelineFootIKSystem& operator=(const PipelineFootIKSystem& Other);

            PipelineFootIKSystem(PipelineFootIKSystem&& Other) noexcept;
            PipelineFootIKSystem& operator=(PipelineFootIKSystem&& Other) noexcept;

        public:
            const std::string& Name() const override;
            void Execute(PipelineContext& Ctx, float Dt) override;

        private:
            bool TryResolveRaycastHitOnTerrain(const Terrain::ITerrainQuery& TerrainQuery, const DirectX::SimpleMath::Ray& Ray, float RayLength, DirectX::SimpleMath::Vector3& OutHitPoint, DirectX::SimpleMath::Vector3& OutHitNormal) const;
            void AppendFootCornerDebugLines(PipelineContext& Ctx, const Terrain::ITerrainQuery& TerrainQuery, Arche::EntityID FootEntityId, Arche::EntityID ToeEntityId, std::unordered_map<Arche::EntityID, DirectX::SimpleMath::Matrix>& InOutWorldMatrices) const;

            std::unique_ptr<IFootIKSolver> mFootIKSolver{};
        };
    }
}
