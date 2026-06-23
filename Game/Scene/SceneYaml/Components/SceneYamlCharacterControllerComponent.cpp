#include "Game/Scene/SceneYaml/Components/SceneYamlCharacterControllerComponent.h"

#include <string>

#include "Game/Scene/Components/CharacterController.h"
#include "Game/Scene/SceneYaml/SceneYamlReadUtils.h"

namespace Game::SceneYaml {
    const char* SceneYamlCharacterControllerComponentReader::TypeName() {
        return "CharacterController";
    }

    void SceneYamlCharacterControllerComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        const c4::yml::ConstNodeRef CharacterControllerNode{ ComponentsNode[TypeName()] };
        CharacterController NewCharacterController{};
        TryReadBoolChild(CharacterControllerNode, { "active", "isActive", "IsActive" }, NewCharacterController.mIsActive);
        TryReadFloatChild(CharacterControllerNode, { "horizontalAcceleration", "HorizontalAcceleration" }, NewCharacterController.mHorizontalAcceleration);
        TryReadFloatChild(CharacterControllerNode, { "jumpSpeed", "JumpSpeed" }, NewCharacterController.mJumpSpeed);
        TryReadFloatChild(CharacterControllerNode, { "groundSnapDistance", "GroundSnapDistance" }, NewCharacterController.mGroundSnapDistance);
        LoadContext.mScene.GetWorld().AddComponent(Entity, NewCharacterController);
    }

    const char* SceneYamlCharacterControllerComponentWriter::TypeName() {
        return "CharacterController";
    }

    void SceneYamlCharacterControllerComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const CharacterController* CharacterControllerComponent{ WriteContext.mReadOnlyWorld.GetComponent<CharacterController>(EntityId) };
        if (CharacterControllerComponent == nullptr) {
            return;
        }

        WriteContext.mStream << "      " << TypeName() << ":\n";
        WriteContext.mStream << "        active: " << (CharacterControllerComponent->mIsActive == true ? "true" : "false") << '\n';
        WriteContext.mStream << "        horizontalAcceleration: " << std::to_string(CharacterControllerComponent->mHorizontalAcceleration) << '\n';
        WriteContext.mStream << "        jumpSpeed: " << std::to_string(CharacterControllerComponent->mJumpSpeed) << '\n';
        WriteContext.mStream << "        groundSnapDistance: " << std::to_string(CharacterControllerComponent->mGroundSnapDistance) << '\n';
    }
}
