#pragma once
#include <cstdint>
#include "Utility/ComponentRestraint.h"
#include "Utility/DirectXInclude.h"
#include "Game/Model/Model.h"

namespace Game {
    ComponentDecl(
        BoundingBox,
        ComponentFields(
            ComponentField(DirectX::BoundingOrientedBox, mObb, DirectX::BoundingOrientedBox{})
            ComponentField(DirectX::BoundingOrientedBox, mWorldObb, DirectX::BoundingOrientedBox{})
            ComponentField(bool, mIsWorldObbValid, false)
        ),
        ComponentMethods(
            ComponentMethod(void ResetToUnitCube(), ResetToUnitCube)
            ComponentMethod(void UpdateFromModel(const Model* ModelValue, ::std::uint32_t NodeIndex), UpdateFromModel)
            ComponentMethod(void UpdateWorldObb(const SimpleMath::Matrix& WorldMatrix), UpdateWorldObb)
            ComponentMethod(void SetObb(const DirectX::BoundingOrientedBox& ObbValue), SetObb)
            ComponentMethod(void SetWorldObb(const DirectX::BoundingOrientedBox& WorldObbValue), SetWorldObb)
            ComponentMethod(void InvalidateWorldObb(), InvalidateWorldObb)
            ComponentMethod(bool HasWorldObb() const, HasWorldObb)
            ComponentMethod(const DirectX::BoundingOrientedBox& GetObb() const, GetObb)
            ComponentMethod(const DirectX::BoundingOrientedBox& GetWorldObb() const, GetWorldObb)
        )
    );
}
