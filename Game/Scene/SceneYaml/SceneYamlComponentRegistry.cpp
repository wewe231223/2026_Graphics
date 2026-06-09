#include "SceneYamlComponentRegistry.h"
#include <memory>
#include "Game/Model/Model.h"
#include "Game/Scene/SceneEntityFactory.h"
#include "Game/Scene/SceneYaml/Components/SceneYamlAnimationComponent.h"
#include "Game/Scene/SceneYaml/Components/SceneYamlBasicComponents.h"
#include "Game/Scene/SceneYaml/Components/SceneYamlCameraComponent.h"
#include "Game/Scene/SceneYaml/Components/SceneYamlNameComponent.h"
#include "Game/Scene/SceneYaml/Components/SceneYamlPhysicsComponent.h"
#include "Game/Scene/SceneYaml/Components/SceneYamlTerrainComponent.h"
#include "Game/Scene/SceneYaml/Components/SceneYamlTransformComponent.h"

namespace Game::SceneYaml {
    const std::vector<SceneYamlComponentReader>& GetSceneYamlPreModelComponentReaders() {
        static const std::vector<SceneYamlComponentReader> ComponentReaders{
            SceneYamlComponentReader{ SceneYamlNameComponentReader::TypeName, SceneYamlNameComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlTransformComponentReader::TypeName, SceneYamlTransformComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlDirectionalLightComponentReader::TypeName, SceneYamlDirectionalLightComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlBoundingBoxComponentReader::TypeName, SceneYamlBoundingBoxComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlPhysicsComponentReader::TypeName, SceneYamlPhysicsComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlPrefabInstanceComponentReader::TypeName, SceneYamlPrefabInstanceComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlBoneSkinReferenceComponentReader::TypeName, SceneYamlBoneSkinReferenceComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlFootIKRigComponentReader::TypeName, SceneYamlFootIKRigComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlMaterialComponentReader::TypeName, SceneYamlMaterialComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlCullingComponentReader::TypeName, SceneYamlCullingComponentReader::Read },
        };

        return ComponentReaders;
    }

    const std::vector<SceneYamlComponentReader>& GetSceneYamlModelComponentReaders() {
        static const std::vector<SceneYamlComponentReader> ComponentReaders{
            SceneYamlComponentReader{ SceneYamlTerrainComponentReader::TypeName, SceneYamlTerrainComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlStaticMeshRendererComponentReader::TypeName, SceneYamlStaticMeshRendererComponentReader::Read },
        };

        return ComponentReaders;
    }

    const std::vector<SceneYamlComponentReader>& GetSceneYamlPostModelComponentReaders() {
        static const std::vector<SceneYamlComponentReader> ComponentReaders{
            SceneYamlComponentReader{ SceneYamlAnimationComponentReader::TypeName, SceneYamlAnimationComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlCameraComponentReader::TypeName, SceneYamlCameraComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlTagComponentReader::TypeName, SceneYamlTagComponentReader::Read },
            SceneYamlComponentReader{ SceneYamlScriptComponentReader::TypeName, SceneYamlScriptComponentReader::Read },
        };

        return ComponentReaders;
    }

    const std::vector<SceneYamlComponentWriter>& GetSceneYamlComponentWriters() {
        static const std::vector<SceneYamlComponentWriter> ComponentWriters{
            SceneYamlComponentWriter{ SceneYamlTagComponentWriter::TypeName, SceneYamlTagComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlNameComponentWriter::TypeName, SceneYamlNameComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlTransformComponentWriter::TypeName, SceneYamlTransformComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlDirectionalLightComponentWriter::TypeName, SceneYamlDirectionalLightComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlBoneSkinReferenceComponentWriter::TypeName, SceneYamlBoneSkinReferenceComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlFootIKRigComponentWriter::TypeName, SceneYamlFootIKRigComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlMaterialComponentWriter::TypeName, SceneYamlMaterialComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlCullingComponentWriter::TypeName, SceneYamlCullingComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlAnimationComponentWriter::TypeName, SceneYamlAnimationComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlPhysicsComponentWriter::TypeName, SceneYamlPhysicsComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlBoundingBoxComponentWriter::TypeName, SceneYamlBoundingBoxComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlPrefabInstanceComponentWriter::TypeName, SceneYamlPrefabInstanceComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlStaticMeshRendererComponentWriter::TypeName, SceneYamlStaticMeshRendererComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlTerrainComponentWriter::TypeName, SceneYamlTerrainComponentWriter::Write },
            SceneYamlComponentWriter{ SceneYamlCameraComponentWriter::TypeName, SceneYamlCameraComponentWriter::Write },
        };

        return ComponentWriters;
    }

    void SpawnPrefabModelIfNeeded(Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        if (ReadState.mHasPrefabInstance == false || ReadState.mPrefabModelSelector.empty() == true) {
            return;
        }

        const std::shared_ptr<Model> ModelData{ LoadContext.mScene.GetAssetRegistry().GetModel(ReadState.mPrefabModelSelector) };
        if (ModelData == nullptr) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Prefab modelPath 로 Model 로드 실패: " } + ReadState.mPrefabModelSelector);
            return;
        }

        ModelHierarchySpawnRequest SpawnRequest{};
        SpawnRequest.ModelData = ModelData;
        SpawnRequest.RootEntityId = Entity;
        SpawnRequest.MaterialGroupIndex = ReadState.mMaterialGroupIndexForModel;
        SpawnRequest.FrustumCullingEnabled = ReadState.mFrustumCullingEnabled;
        SpawnRequest.IsActive = ReadState.mPrefabIsActive;
        ReadState.mHasInstantiatedPrefabModel = LoadContext.mEntityFactory.SpawnModelHierarchy(SpawnRequest);
        if (ReadState.mHasInstantiatedPrefabModel == false) {
            LoadContext.mLoadResult.IsSuccess = false;
            LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Model RootNode 를 찾을 수 없습니다: " } + ReadState.mPrefabModelSelector);
        }
    }
}
