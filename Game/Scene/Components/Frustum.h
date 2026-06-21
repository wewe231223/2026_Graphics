#pragma once

#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"

namespace Game {
    ComponentDecl(
        Frustum,
        ComponentFields(
            ComponentField(DirectX::BoundingFrustum, mValue, {})
        ),
        ComponentMethods(
            ComponentMethod(void UpdateFromViewProjection(const SimpleMath::Matrix& ViewMatrix, const SimpleMath::Matrix& ProjectionMatrix), UpdateFromViewProjection)
            ComponentMethod(bool Intersects(const DirectX::BoundingOrientedBox& WorldBoundingBox) const, Intersects)
        )
    );
}
