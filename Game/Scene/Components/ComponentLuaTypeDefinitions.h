#pragma once

#include <array>
#include <tuple>
#include "Arche/Common.h"
#include "DirectXTK12/SimpleMath.h"
#include "Game/Scene/Components/ComponentText.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"
#include "Utility/ComponentRestraint.h"

namespace Game {
    void InitializeComponentLuaTypeDefinitions();
}

LuaTypeDefinitionDeclWithName(
    Arche::EntityID,
    "EntityID",
    ComponentFields(
        ComponentField(::std::uint32_t, index)
        ComponentField(::std::uint32_t, generation)
        ComponentField(::std::uint32_t, flags)
    ),
    BOOST_PP_SEQ_NIL
);

LuaTypeDefinitionDeclWithName(
    DirectX::SimpleMath::Vector2,
    "Vector2",
    ComponentFields(
        ComponentField(float, x)
        ComponentField(float, y)
    ),
    BOOST_PP_SEQ_NIL
);

LuaTypeDefinitionDeclWithName(
    DirectX::SimpleMath::Vector3,
    "Vector3",
    ComponentFields(
        ComponentField(float, x)
        ComponentField(float, y)
        ComponentField(float, z)
    ),
    BOOST_PP_SEQ_NIL
);

LuaTypeDefinitionDeclWithName(
    DirectX::SimpleMath::Quaternion,
    "Quaternion",
    ComponentFields(
        ComponentField(float, x)
        ComponentField(float, y)
        ComponentField(float, z)
        ComponentField(float, w)
    ),
    BOOST_PP_SEQ_NIL
);

LuaTypeConstructorSignaturesDecl(DirectX::SimpleMath::Vector2, DirectX::SimpleMath::Vector2(float, float));
LuaTypeConstructorSignaturesDecl(DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Vector3(float, float, float));
LuaTypeConstructorSignaturesDecl(DirectX::SimpleMath::Quaternion, DirectX::SimpleMath::Quaternion(float, float, float, float));

LuaTypeDefinitionDeclWithName(
    DirectX::SimpleMath::Matrix,
    "Matrix",
    ComponentFields(
        ComponentField(float, _11)
        ComponentField(float, _12)
        ComponentField(float, _13)
        ComponentField(float, _14)
        ComponentField(float, _21)
        ComponentField(float, _22)
        ComponentField(float, _23)
        ComponentField(float, _24)
        ComponentField(float, _31)
        ComponentField(float, _32)
        ComponentField(float, _33)
        ComponentField(float, _34)
        ComponentField(float, _41)
        ComponentField(float, _42)
        ComponentField(float, _43)
        ComponentField(float, _44)
    ),
    BOOST_PP_SEQ_NIL
);

LuaStdArrayTypeDefinitionDeclWithName(Game::ComponentTextArray::value_type, Game::ComponentTextArray::ElementCount, "Text");
LuaStdArrayTypeDefinitionDeclWithName(Game::RuntimeVariableBoolArray::value_type, ::std::tuple_size_v<Game::RuntimeVariableBoolArray>, "BoolValues");
LuaStdArrayTypeDefinitionDeclWithName(Game::RuntimeVariableIntArray::value_type, ::std::tuple_size_v<Game::RuntimeVariableIntArray>, "IntValues");
LuaStdArrayTypeDefinitionDeclWithName(Game::RuntimeVariableFloatArray::value_type, ::std::tuple_size_v<Game::RuntimeVariableFloatArray>, "FloatValues");
