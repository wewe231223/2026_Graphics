#pragma once
#include <cstdint>
#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"
#include "Game/Model/Model.h"

namespace Game {
    Component(BoundingBox)
        void ResetToUnitCube();
        void UpdateFromModel(const Model* ModelValue, std::uint32_t NodeIndex);

        const DirectX::BoundingOrientedBox& GetObb() const;

        DirectX::BoundingOrientedBox mObb{ DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f }, DirectX::XMFLOAT3{ 0.5f, 0.5f, 0.5f }, DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f } };
    EndComponent(BoundingBox)
}
