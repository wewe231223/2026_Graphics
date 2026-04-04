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
        ),
        ComponentMethods(
            ComponentMethod(void ResetToUnitCube(), ResetToUnitCube)
            ComponentMethod(void UpdateFromModel(const Model* ModelValue, ::std::uint32_t NodeIndex), UpdateFromModel)
            ComponentMethod(const DirectX::BoundingOrientedBox& GetObb() const, GetObb)
        )
    );
}
