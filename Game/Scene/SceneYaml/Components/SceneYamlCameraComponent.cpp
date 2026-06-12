#include "SceneYamlCameraComponent.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include "Game/Scene/Components/Camera.h"
#include "Game/Scene/Components/Frustum.h"
#include "Game/Scene/Components/SkySphere.h"
#include "Game/Scene/SceneYaml/SceneYamlReadUtils.h"
#include "Game/Scene/SceneYaml/SceneYamlWriteUtils.h"

namespace Game::SceneYaml {
    const char* SceneYamlCameraComponentReader::TypeName() {
        return CameraTypeName;
    }

    void SceneYamlCameraComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        Camera NewCamera{};
        Frustum NewFrustum{};
        const c4::yml::ConstNodeRef CameraNode{ ComponentsNode[TypeName()] };
        if (CameraNode.has_child("fov")) {
            CameraNode["fov"] >> NewCamera.fov;
        }

        if (CameraNode.has_child("aspectRatio")) {
            CameraNode["aspectRatio"] >> NewCamera.aspectRatio;
        }

        if (CameraNode.has_child("nearPlane")) {
            CameraNode["nearPlane"] >> NewCamera.nearPlane;
        }

        if (CameraNode.has_child("farPlane")) {
            CameraNode["farPlane"] >> NewCamera.farPlane;
        }

        if (CameraNode.has_child("isActive")) {
            CameraNode["isActive"] >> NewCamera.isActive;
        }

        if (CameraNode.has_child("isOrthographic")) {
            CameraNode["isOrthographic"] >> NewCamera.isOrthographic;
        }

        if (CameraNode.has_child("orthoSize")) {
            CameraNode["orthoSize"] >> NewCamera.orthoSize;
        }

        if (CameraNode.has_child("clearColor")) {
            ReadColor4(CameraNode["clearColor"], NewCamera.clearColor.data());
        }

        if (CameraNode.has_child("startMode")) {
            std::string CameraModeText{};
            CameraNode["startMode"] >> CameraModeText;
            TryParseCameraModeText(CameraModeText, NewCamera.cameraFlags);
        }
        else if (CameraNode.has_child("cameraFlags")) {
            CameraNode["cameraFlags"] >> NewCamera.cameraFlags;
        }

        if (CameraNode.has_child("thirdPersonFollowTargetEntityId")) {
            CameraNode["thirdPersonFollowTargetEntityId"] >> NewCamera.thirdPersonFollowTargetSerializedId;
            LoadContext.mDeferredThirdPersonFollowTargetEntities.push_back(std::pair<Arche::EntityID, std::int64_t>{ Entity, NewCamera.thirdPersonFollowTargetSerializedId });
        }

        if (CameraNode.has_child("thirdPersonDistance")) {
            CameraNode["thirdPersonDistance"] >> NewCamera.thirdPersonDistance;
        }

        if (CameraNode.has_child("thirdPersonMinDistance")) {
            CameraNode["thirdPersonMinDistance"] >> NewCamera.thirdPersonMinDistance;
        }

        if (CameraNode.has_child("thirdPersonMaxDistance")) {
            CameraNode["thirdPersonMaxDistance"] >> NewCamera.thirdPersonMaxDistance;
        }

        if (CameraNode.has_child("thirdPersonHeightOffset")) {
            CameraNode["thirdPersonHeightOffset"] >> NewCamera.thirdPersonHeightOffset;
        }

        if (CameraNode.has_child("thirdPersonOrbitYaw")) {
            CameraNode["thirdPersonOrbitYaw"] >> NewCamera.thirdPersonOrbitYaw;
        }

        if (CameraNode.has_child("thirdPersonOrbitPitch")) {
            CameraNode["thirdPersonOrbitPitch"] >> NewCamera.thirdPersonOrbitPitch;
        }

        if (CameraNode.has_child("thirdPersonPositionLerpSpeed")) {
            CameraNode["thirdPersonPositionLerpSpeed"] >> NewCamera.thirdPersonPositionLerpSpeed;
        }

        if (CameraNode.has_child("thirdPersonZoomSpeed")) {
            CameraNode["thirdPersonZoomSpeed"] >> NewCamera.thirdPersonZoomSpeed;
        }

        SkySphere NewSkySphere{};
        const bool HasSkySphereNode{ CameraNode.has_child("skySphereEntityId") };
        if (HasSkySphereNode) {
            CameraNode["skySphereEntityId"] >> NewSkySphere.SkySphereSerializedEntityId;
            LoadContext.mDeferredSkySphereEntities.push_back(std::pair<Arche::EntityID, std::int64_t>{ Entity, NewSkySphere.SkySphereSerializedEntityId });
        }

        LoadContext.mScene.GetWorld().AddComponent(Entity, NewCamera);
        LoadContext.mScene.GetWorld().AddComponent(Entity, NewFrustum);
        if (HasSkySphereNode) {
            LoadContext.mScene.GetWorld().AddComponent(Entity, NewSkySphere);
        }
    }

    const char* SceneYamlCameraComponentWriter::TypeName() {
        return CameraTypeName;
    }

    void SceneYamlCameraComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const Camera* CameraComponent{ WriteContext.mReadOnlyWorld.GetComponent<Camera>(EntityId) };
        if (CameraComponent == nullptr) {
            return;
        }

        const SkySphere* SkySphereComponent{ WriteContext.mReadOnlyWorld.GetComponent<SkySphere>(EntityId) };
        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        AppendLine(WriteContext.mStream, 4, std::string{ "fov: " } + std::to_string(CameraComponent->fov));
        AppendLine(WriteContext.mStream, 4, std::string{ "aspectRatio: " } + std::to_string(CameraComponent->aspectRatio));
        AppendLine(WriteContext.mStream, 4, std::string{ "nearPlane: " } + std::to_string(CameraComponent->nearPlane));
        AppendLine(WriteContext.mStream, 4, std::string{ "farPlane: " } + std::to_string(CameraComponent->farPlane));
        AppendLine(WriteContext.mStream, 4, std::string{ "isActive: " } + ToYamlBooleanText(CameraComponent->isActive));
        AppendLine(WriteContext.mStream, 4, std::string{ "isOrthographic: " } + ToYamlBooleanText(CameraComponent->isOrthographic));
        AppendLine(WriteContext.mStream, 4, std::string{ "orthoSize: " } + std::to_string(CameraComponent->orthoSize));
        AppendColor4(WriteContext.mStream, 4, "clearColor", CameraComponent->clearColor.data());
        const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator ThirdPersonFollowTargetSerializedIter{ WriteContext.mSerializedEntityIds.find(CameraComponent->thirdPersonFollowTarget) };
        const std::int32_t ThirdPersonFollowTargetSerializedId{ ThirdPersonFollowTargetSerializedIter == WriteContext.mSerializedEntityIds.end() ? -1 : static_cast<std::int32_t>(ThirdPersonFollowTargetSerializedIter->second) };
        AppendLine(WriteContext.mStream, 4, std::string{ "thirdPersonFollowTargetEntityId: " } + std::to_string(ThirdPersonFollowTargetSerializedId));
        AppendLine(WriteContext.mStream, 4, std::string{ "thirdPersonDistance: " } + std::to_string(CameraComponent->thirdPersonDistance));
        AppendLine(WriteContext.mStream, 4, std::string{ "thirdPersonMinDistance: " } + std::to_string(CameraComponent->thirdPersonMinDistance));
        AppendLine(WriteContext.mStream, 4, std::string{ "thirdPersonMaxDistance: " } + std::to_string(CameraComponent->thirdPersonMaxDistance));
        AppendLine(WriteContext.mStream, 4, std::string{ "thirdPersonHeightOffset: " } + std::to_string(CameraComponent->thirdPersonHeightOffset));
        AppendLine(WriteContext.mStream, 4, std::string{ "thirdPersonOrbitYaw: " } + std::to_string(CameraComponent->thirdPersonOrbitYaw));
        AppendLine(WriteContext.mStream, 4, std::string{ "thirdPersonOrbitPitch: " } + std::to_string(CameraComponent->thirdPersonOrbitPitch));
        AppendLine(WriteContext.mStream, 4, std::string{ "thirdPersonPositionLerpSpeed: " } + std::to_string(CameraComponent->thirdPersonPositionLerpSpeed));
        AppendLine(WriteContext.mStream, 4, std::string{ "thirdPersonZoomSpeed: " } + std::to_string(CameraComponent->thirdPersonZoomSpeed));
        if (SkySphereComponent != nullptr) {
            const std::unordered_map<Arche::EntityID, std::uint32_t>::const_iterator SkySphereSerializedIter{ WriteContext.mSerializedEntityIds.find(SkySphereComponent->SkySphereEntityId) };
            const std::int32_t SkySphereSerializedId{ SkySphereSerializedIter == WriteContext.mSerializedEntityIds.end() ? -1 : static_cast<std::int32_t>(SkySphereSerializedIter->second) };
            AppendLine(WriteContext.mStream, 4, std::string{ "skySphereEntityId: " } + std::to_string(SkySphereSerializedId));
        }

        AppendLine(WriteContext.mStream, 4, std::string{ "startMode: " } + ResolveCameraModeText(CameraComponent->cameraFlags));
    }
}
