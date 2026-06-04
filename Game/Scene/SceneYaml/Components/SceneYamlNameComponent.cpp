#include "SceneYamlNameComponent.h"
#include <string>
#include "Game/Scene/Components/Name.h"
#include "Game/Scene/SceneYaml/SceneYamlReadUtils.h"
#include "Game/Scene/SceneYaml/SceneYamlWriteUtils.h"

namespace Game::SceneYaml {
    const char* SceneYamlNameComponentReader::TypeName() {
        return NameTypeName;
    }

    void SceneYamlNameComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        const c4::yml::ConstNodeRef NameNode{ ComponentsNode[TypeName()] };
        if (NameNode.has_child("text")) {
            std::string NameText{};
            NameNode["text"] >> NameText;
            const Name NewName{ Game::CreateNameComponent(NameText) };
            LoadContext.mScene.GetWorld().AddComponent(Entity, NewName);
        }
    }

    const char* SceneYamlNameComponentWriter::TypeName() {
        return NameTypeName;
    }

    void SceneYamlNameComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const Name* NameComponent{ WriteContext.mReadOnlyWorld.GetComponent<Game::Name>(EntityId) };
        if (NameComponent == nullptr) {
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        AppendLine(WriteContext.mStream, 4, std::string{ "text: " } + ToYamlText(Game::GetNameText(*NameComponent)));
    }
}
