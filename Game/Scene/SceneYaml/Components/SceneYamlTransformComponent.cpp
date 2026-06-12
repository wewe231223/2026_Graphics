#include "SceneYamlTransformComponent.h"
#include "Game/Scene/Components/Transform.h"
#include "Game/Scene/SceneYaml/SceneYamlReadUtils.h"
#include "Game/Scene/SceneYaml/SceneYamlWriteUtils.h"

namespace Game::SceneYaml {
    const char* SceneYamlTransformComponentReader::TypeName() {
        return TransformTypeName;
    }

    void SceneYamlTransformComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        Transform NewTransform{};
        const c4::yml::ConstNodeRef TransformNode{ ComponentsNode[TypeName()] };
        if (TransformNode.has_child("position")) {
            ReadVector3(TransformNode["position"], NewTransform.position);
        }

        if (TransformNode.has_child("rotationEuler")) {
            ReadVector3(TransformNode["rotationEuler"], NewTransform.rotationEuler);
        }

        if (TransformNode.has_child("rotation")) {
            ReadQuaternion(TransformNode["rotation"], NewTransform.rotation);
        }

        if (TransformNode.has_child("scale")) {
            ReadVector3(TransformNode["scale"], NewTransform.scale);
        }

        LoadContext.mScene.GetWorld().AddComponent(Entity, NewTransform);

        bool SnapToTerrainOnSpawn{};
        if (TryReadBoolChild(TransformNode, { "snapToTerrainOnSpawn", "SnapToTerrainOnSpawn" }, SnapToTerrainOnSpawn) == true && SnapToTerrainOnSpawn == true) {
            PendingTerrainSnapBinding NewPendingTerrainSnapBinding{};
            NewPendingTerrainSnapBinding.mEntityId = Entity;
            TryReadFloatChild(TransformNode, { "terrainOffsetY", "TerrainOffsetY" }, NewPendingTerrainSnapBinding.mOffsetY);
            LoadContext.mPendingTerrainSnapBindings.push_back(NewPendingTerrainSnapBinding);
        }
    }

    const char* SceneYamlTransformComponentWriter::TypeName() {
        return TransformTypeName;
    }

    void SceneYamlTransformComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const Transform* TransformComponent{ WriteContext.mReadOnlyWorld.GetComponent<Game::Transform>(EntityId) };
        if (TransformComponent == nullptr) {
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        AppendVector3(WriteContext.mStream, 4, "position", TransformComponent->position);
        AppendVector3(WriteContext.mStream, 4, "rotationEuler", TransformComponent->rotationEuler);
        AppendQuaternion(WriteContext.mStream, 4, "rotation", TransformComponent->rotation);
        AppendVector3(WriteContext.mStream, 4, "scale", TransformComponent->scale);
    }
}
