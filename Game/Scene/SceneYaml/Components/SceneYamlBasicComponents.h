#pragma once

#include "Game/Scene/SceneYaml/SceneYamlComponentRegistry.h"

namespace Game::SceneYaml {
    class SceneYamlTagComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlTagComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };

    class SceneYamlDirectionalLightComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlDirectionalLightComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };

    class SceneYamlBoundingBoxComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlBoundingBoxComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };

    class SceneYamlPrefabInstanceComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlPrefabInstanceComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };

    class SceneYamlBoneSkinReferenceComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlBoneSkinReferenceComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };

    class SceneYamlFootIKRigComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlFootIKRigComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };

    class SceneYamlMaterialComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlMaterialComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };

    class SceneYamlCullingComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlCullingComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };

    class SceneYamlStaticMeshRendererComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };

    class SceneYamlStaticMeshRendererComponentWriter final {
    public:
        static const char* TypeName();
        static void Write(const SceneYamlComponentWriteContext& WriteContext);
    };

    class SceneYamlScriptComponentReader final {
    public:
        static const char* TypeName();
        static void Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState);
    };
}
