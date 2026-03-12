#pragma once

#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"

namespace Game {
    Component(Frustum)
        void UpdateFromViewProjection(const SimpleMath::Matrix& ViewMatrix, const SimpleMath::Matrix& ProjectionMatrix);
        bool Intersects(const DirectX::BoundingOrientedBox& WorldBoundingBox) const;

        DirectX::BoundingFrustum mValue{};
    EndComponent(Frustum)
}
