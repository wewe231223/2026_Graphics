#include "SceneYamlAnimationComponent.h"
#include <memory>
#include <string>
#include <utility>
#include "Game/Asset/AnimationGraphAsset.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/SceneYaml/SceneYamlReadUtils.h"
#include "Game/Scene/SceneYaml/SceneYamlWriteUtils.h"

namespace Game::SceneYaml {
    const char* SceneYamlAnimationComponentReader::TypeName() {
        return AnimationTypeName;
    }

    void SceneYamlAnimationComponentReader::Read(c4::yml::ConstNodeRef ComponentsNode, Arche::EntityID Entity, SceneYamlLoadContext& LoadContext, SceneYamlComponentReadState& ReadState) {
        static_cast<void>(ReadState);

        if (ComponentsNode.has_child(TypeName()) == false) {
            return;
        }

        const c4::yml::ConstNodeRef AnimationNode{ ComponentsNode[TypeName()] };
        std::string AnimationPath{};
        PendingAnimatorBinding NewBinding{};
        NewBinding.mSourceEntityId = Entity;

        if (AnimationNode.has_child("text")) {
            AnimationNode["text"] >> AnimationPath;
        }

        if (AnimationNode.has_child("initclip")) {
            AnimationNode["initclip"] >> NewBinding.mClipIndex;
            NewBinding.mFallbackClipIndex = NewBinding.mClipIndex;
        }

        if (AnimationNode.has_child("AnimationGraph")) {
            std::string AnimationGraphPath{};
            AnimationNode["AnimationGraph"] >> AnimationGraphPath;
            const std::string ResolvedAnimationGraphPath{ ResolveSceneResourcePath(LoadContext.mSceneName, AnimationGraphPath) };
            const std::shared_ptr<AnimationGraphAsset> AnimationGraphData{ LoadContext.mScene.GetAssetRegistry().GetAnimationGraph(ResolvedAnimationGraphPath) };
            if (AnimationGraphData == nullptr) {
                LoadContext.mLoadResult.IsSuccess = false;
                LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "AnimationGraph 파일 로드 실패: " } + ResolvedAnimationGraphPath);
            }
            else {
                NewBinding.mAnimationGraphData = AnimationGraphData.get();
            }
        }

        if (AnimationNode.has_child("node")) {
            AnimationNode["node"] >> NewBinding.mTargetNodeName;
        }

        if (ComponentsNode.has_child(RuntimeVariablesTypeName)) {
            const c4::yml::ConstNodeRef RuntimeVariablesNode{ ComponentsNode[RuntimeVariablesTypeName] };
            for (const c4::yml::ConstNodeRef RuntimeVariableNode : RuntimeVariablesNode.children()) {
                PendingAnimatorBinding::PendingRuntimeVariableInitialization NewInitialization{};
                std::string TypeText{};
                RuntimeVariableNode["Name"] >> NewInitialization.mParameterName;
                RuntimeVariableNode["Type"] >> TypeText;

                if (TypeText == "Bool") {
                    NewInitialization.mType = PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Bool;
                    RuntimeVariableNode["Value"] >> NewInitialization.mBoolValue;
                }
                else if (TypeText == "Int") {
                    NewInitialization.mType = PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Int;
                    RuntimeVariableNode["Value"] >> NewInitialization.mIntValue;
                }
                else if (TypeText == "Float") {
                    NewInitialization.mType = PendingAnimatorBinding::PendingRuntimeVariableInitialization::RuntimeVariableType::Float;
                    RuntimeVariableNode["Value"] >> NewInitialization.mFloatValue;
                }
                else {
                    LoadContext.mLoadResult.IsSuccess = false;
                    LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "RuntimeVariables Type 값 오류: " } + TypeText);
                    continue;
                }

                NewBinding.mRuntimeVariableInitializations.push_back(std::move(NewInitialization));
            }
        }

        if (AnimationPath.empty() == false) {
            const std::string ResolvedAnimationPath{ ResolveSceneResourcePath(LoadContext.mSceneName, AnimationPath) };
            const std::shared_ptr<asset::Animation> AnimationData{ LoadContext.mScene.GetAssetRegistry().GetAnimation(ResolvedAnimationPath) };
            if (AnimationData == nullptr) {
                LoadContext.mLoadResult.IsSuccess = false;
                LoadContext.mLoadResult.UndecidedItems.push_back(std::string{ "Animation 파일 로드 실패: " } + ResolvedAnimationPath);
            }
            else {
                NewBinding.mAnimationData = AnimationData.get();
            }
        }

        LoadContext.mPendingAnimatorBindings.push_back(std::move(NewBinding));
    }

    const char* SceneYamlAnimationComponentWriter::TypeName() {
        return AnimationTypeName;
    }

    void SceneYamlAnimationComponentWriter::Write(const SceneYamlComponentWriteContext& WriteContext) {
        const Arche::EntityID EntityId{ WriteContext.mEntitySnapshot.mEntityId };
        const Animator* AnimatorComponent{ nullptr };
        Arche::EntityID AnimatorEntityId{ Arche::NullEntityID };
        const bool IsAnimatorFound{ TryFindAnimatorForSerializationInHierarchy(&WriteContext.mReadOnlyWorld, EntityId, AnimatorComponent, AnimatorEntityId) };
        static_cast<void>(IsAnimatorFound);
        static_cast<void>(AnimatorEntityId);

        if (AnimatorComponent == nullptr) {
            return;
        }

        AppendLine(WriteContext.mStream, 3, std::string{ TypeName() } + std::string{ ":" });
        const std::string AnimationSelector{ WriteContext.mAssetRegistry.FindAnimationSelectorByPointer(AnimatorComponent->animation) };
        const std::string AnimationSelectorForYaml{ MakeSceneRelativeResourcePath(WriteContext.mTargetSnapshot.GetSceneName(), AnimationSelector) };
        AppendLine(WriteContext.mStream, 4, std::string{ "text: " } + ToYamlText(AnimationSelectorForYaml));
        AppendLine(WriteContext.mStream, 4, std::string{ "initclip: " } + std::to_string(AnimatorComponent->FallbackClipIndex));
        const std::string AnimationGraphSelector{ WriteContext.mAssetRegistry.FindAnimationGraphSelectorByPointer(AnimatorComponent->GraphAsset) };
        const std::string AnimationGraphSelectorForYaml{ MakeSceneRelativeResourcePath(WriteContext.mTargetSnapshot.GetSceneName(), AnimationGraphSelector) };
        if (AnimationGraphSelectorForYaml.empty() == false) {
            AppendLine(WriteContext.mStream, 4, std::string{ "AnimationGraph: " } + ToYamlText(AnimationGraphSelectorForYaml));
        }
    }
}
