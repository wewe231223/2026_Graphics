#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* Frustum::GetComponentInspectionName() {
        return "Frustum";
    }

    void Frustum::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        (void)OutFields;
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
