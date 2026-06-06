#include "SceneYamlPhysicsComponent.h"
#include <string>
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "Game/Scene/SceneYaml/SceneYamlPhysics.h"
#include "Game/Scene/SceneYaml/SceneYamlReadUtils.h"

namespace Game::SceneYaml {
    const char* SceneYamlPhysicsComponentReader::TypeName() {
        return PhysicsTypeName;
    }

    void SceneYamlPhysicsComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        const c4::yml::ConstNodeRef PhysicsNode{ ComponentsNode[TypeName()] };
        PhysicsActorSettings NewPhysicsSettings{};
        std::string PhysicsErrorText{};
        if (TryReadPhysicsActorSettings(PhysicsNode, NewPhysicsSettings, PhysicsErrorText) == false) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back(PhysicsErrorText);
        }
        else {
            LoadContext.mScene.GetWorld().AddComponent(Entity, NewPhysicsSettings);
        }

        if (PhysicsNode.readable() == true && PhysicsNode.is_map() == true && PhysicsNode.has_child(BoundingBoxTypeName)) {
            PendingBoundingBoxBinding NewPendingBinding{};
            if (TryReadBoundingBoxBinding(PhysicsNode[BoundingBoxTypeName], Entity, NewPendingBinding) == true) {
                LoadContext.mPendingBoundingBoxBindings.push_back(NewPendingBinding);
            }
        }
    }

    const char* SceneYamlPhysicsComponentWriter::TypeName() {
        return PhysicsTypeName;
    }

    void SceneYamlPhysicsComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const PhysicsActorSettings* PhysicsActorSettingsComponent{ WriteContext.mReadOnlyWorld.GetComponent<PhysicsActorSettings>(EntityId) };
        if (PhysicsActorSettingsComponent == nullptr) {
            return;
        }

        const BoundingBox* BoundingBoxComponent{ WriteContext.mReadOnlyWorld.GetComponent<BoundingBox>(EntityId) };
        AppendPhysicsActorSettings(WriteContext.mStream, 3, *PhysicsActorSettingsComponent, BoundingBoxComponent);
    }
}
