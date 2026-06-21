#include "Game/Scene/Components/Frustum.h"
#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* Frustum::GetComponentInspectionName() {
        return "Frustum";
    }

    void Frustum::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Origin", std::format("{:.3f}, {:.3f}, {:.3f}", mValue.Origin.x, mValue.Origin.y, mValue.Origin.z) });
        OutFields.push_back(ComponentInspectionField{ "Near", std::format("{:.3f}", mValue.Near) });
        OutFields.push_back(ComponentInspectionField{ "Far", std::format("{:.3f}", mValue.Far) });
    }

    void Frustum::UpdateFromViewProjection(const SimpleMath::Matrix& ViewMatrix, const SimpleMath::Matrix& ProjectionMatrix) {
        DirectX::BoundingFrustum::CreateFromMatrix(mValue, ProjectionMatrix);

        const SimpleMath::Matrix InverseViewMatrix{ ViewMatrix.Invert() };
        mValue.Transform(mValue, InverseViewMatrix);
    }

    bool Frustum::Intersects(const DirectX::BoundingOrientedBox& WorldBoundingBox) const {
        return mValue.Intersects(WorldBoundingBox);
    }
}
