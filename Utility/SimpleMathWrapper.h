#pragma once

#include "Utility/DirectXInclude.h"

namespace DirectX {
    namespace SimpleMath {
        inline SimpleMath::Vector3 Unproject(const SimpleMath::Vector3& SourcePosition, float ViewportX, float ViewportY, float ViewportWidth, float ViewportHeight, float ViewportMinZ, float ViewportMaxZ, const SimpleMath::Matrix& ProjectionMatrix, const SimpleMath::Matrix& ViewMatrix, const SimpleMath::Matrix& WorldMatrix) {
            const DirectX::XMVECTOR SourceVector{ DirectX::XMVectorSet(SourcePosition.x, SourcePosition.y, SourcePosition.z, 1.0f) };
            const DirectX::XMVECTOR ResultVector{ DirectX::XMVector3Unproject(SourceVector, ViewportX, ViewportY, ViewportWidth, ViewportHeight, ViewportMinZ, ViewportMaxZ, ProjectionMatrix, ViewMatrix, WorldMatrix) };
            SimpleMath::Vector3 Result{};
            DirectX::XMStoreFloat3(&Result, ResultVector);
            return Result;
        }
    }
}

