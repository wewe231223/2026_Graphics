#pragma once

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "Arche/World.h"
#include "DirectXTK12/SimpleMath.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/StaticMeshRenderer.h"
#include "Utility/DirectXInclude.h"
#include "SceneYamlTypes.h"

namespace Game::SceneYaml {
    const char* ResolveCameraModeText(std::uint32_t CameraFlags);
    std::string MakeSceneRelativeResourcePath(const std::string& SceneName, const std::string& SourcePath);
    bool IsDefaultMaterialPath(const std::string& MaterialPath);
    bool TryFindRendererInHierarchy(const Arche::World::WorldReadOnlyView* ReadOnlyWorld, Arche::EntityID EntityId, const Game::StaticMeshRenderer*& OutRenderer);
    bool TryResolveMaterialGroupIndexInHierarchy(const Arche::World::WorldReadOnlyView* ReadOnlyWorld, Arche::EntityID EntityId, std::uint32_t& OutMaterialGroupIndex);
    bool TryFindAnimatorForSerializationInHierarchy(const Arche::World::WorldReadOnlyView* ReadOnlyWorld, Arche::EntityID RootEntityId, const Game::Animator*& OutAnimator, Arche::EntityID& OutAnimatorEntityId);
    bool ShouldSkipEntityInSceneExport(const Game::StaticMeshRenderer* StaticMeshRendererComponent);
    void AppendLine(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Text);
    std::string ToYamlText(const char* Text);
    std::string ToYamlText(const std::string& Text);
    std::string ToYamlText(const std::string_view Text);
    std::string ToYamlBooleanText(bool Value);
    std::string BuildFloatListText(const std::vector<float>& Values);
    void AppendVector3(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const DirectX::SimpleMath::Vector3& Value);
    void AppendVector2(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const DirectX::SimpleMath::Vector2& Value);
    void AppendQuaternion(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const DirectX::SimpleMath::Quaternion& Value);
    void AppendColor4(std::ostringstream& Stream, std::size_t IndentLevel, const std::string& Key, const float* Value);
    void AppendBoundingBox(std::ostringstream& Stream, std::size_t IndentLevel, const DirectX::BoundingOrientedBox& LocalBoundingBox);
}
