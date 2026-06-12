#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include "Arche/World.h"
#include "DirectXTK12/SimpleMath.h"
#include "Game/Scene/Base/SynchronousSystem.h"
#include "SceneYamlTypes.h"

namespace Game::SceneYaml {
    bool ReadVector3(c4::yml::ConstNodeRef TargetNode, DirectX::SimpleMath::Vector3& OutValue);
    bool ReadVector2(c4::yml::ConstNodeRef TargetNode, DirectX::SimpleMath::Vector2& OutValue);
    bool ReadQuaternion(c4::yml::ConstNodeRef TargetNode, DirectX::SimpleMath::Quaternion& OutValue);
    bool ReadColor4(c4::yml::ConstNodeRef TargetNode, float* OutValue);
    bool TryParseCameraModeText(const std::string& CameraModeText, std::uint32_t& OutCameraFlags);
    std::unique_ptr<Game::ISystem> CreateSystemByName(const std::string& SystemName);
    bool TryReadSystemName(c4::yml::ConstNodeRef SystemNode, std::string& OutSystemName);
    bool StartsWith(const std::string& Text, const std::string& Prefix);
    std::string BuildPrimitiveSelector(const std::string& PrimitiveType, float PrimitiveSize, const std::array<float, 4>& PrimitiveColor);
    std::string ResolveSceneResourcePath(const std::string& SceneName, const std::string& FileName);
    bool TryParseFloatListText(const std::string& Text, std::vector<float>& OutValues);
    bool TryParseYamlBoolText(const std::string& ValueText, bool& OutValue);
    std::string TrimCopy(const std::string& Text);
    std::string ToLowerCopy(const std::string& Text);
    bool TryReadStringChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, std::string& OutValue);
    bool TryReadBoolChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, bool& OutValue);
    bool TryReadFloatChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, float& OutValue);
    bool TryReadBoundingBoxBinding(c4::yml::ConstNodeRef BoundingBoxNode, Arche::EntityID EntityId, PendingBoundingBoxBinding& OutBinding);
    bool TryFindEntityByNameInHierarchy(const Arche::World* World, Arche::EntityID EntityId, const std::string& TargetNodeName, Arche::EntityID& OutEntityId, bool IncludeRoot);
}
