#include "AnimationGraphSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "Game/Asset/AnimationGraphAsset.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace {
    float Clamp01(float Value) {
        return std::clamp(Value, 0.0f, 1.0f);
    }

    bool EvaluateCondition(const Game::AnimationGraphAsset::AnimationGraphConditionAsset& Condition, const Game::AnimationGraphAsset::AnimationGraphParameterDefinition& Definition, const Game::AnimatorGraphPlayer& Player) {
        if (Condition.ParameterIndex >= Game::AnimatorGraphPlayer::MaxParameterCount) {
            return false;
        }

        if (Definition.ParameterTypeValue == Game::AnimationGraphAsset::ParameterType::Bool || Definition.ParameterTypeValue == Game::AnimationGraphAsset::ParameterType::Trigger) {
            const bool LeftValue{ Player.BoolValues[Condition.ParameterIndex] };
            if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::NotEquals) {
                return LeftValue != Condition.BoolValue;
            }
            return LeftValue == Condition.BoolValue;
        }

        if (Definition.ParameterTypeValue == Game::AnimationGraphAsset::ParameterType::Int) {
            const std::int32_t LeftValue{ Player.IntValues[Condition.ParameterIndex] };
            if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::NotEquals) {
                return LeftValue != Condition.IntValue;
            }
            if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::Greater) {
                return LeftValue > Condition.IntValue;
            }
            if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::GreaterOrEqual) {
                return LeftValue >= Condition.IntValue;
            }
            if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::Less) {
                return LeftValue < Condition.IntValue;
            }
            if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::LessOrEqual) {
                return LeftValue <= Condition.IntValue;
            }
            return LeftValue == Condition.IntValue;
        }

        const float LeftValue{ Player.FloatValues[Condition.ParameterIndex] };
        if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::NotEquals) {
            return LeftValue != Condition.FloatValue;
        }
        if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::Greater) {
            return LeftValue > Condition.FloatValue;
        }
        if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::GreaterOrEqual) {
            return LeftValue >= Condition.FloatValue;
        }
        if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::Less) {
            return LeftValue < Condition.FloatValue;
        }
        if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::LessOrEqual) {
            return LeftValue <= Condition.FloatValue;
        }
        return LeftValue == Condition.FloatValue;
    }
}

namespace Game {
    const std::string& AnimationGraphSystem::Name() const {
        return mName;
    }

    Phase AnimationGraphSystem::GetPhase() const {
        return Phase::Update;
    }

    std::span<const ComponentAccess> AnimationGraphSystem::ComponentAccesses() const {
        static std::array<ComponentAccess, 2> Accesses{ { { typeid(Animator), Access::Write }, { typeid(AnimatorGraphPlayer), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> AnimationGraphSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void AnimationGraphSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        (void)Ctx;
        for (auto [AnimatorComponent, GraphPlayer] : World.Query<Animator, AnimatorGraphPlayer>()) {
            if (AnimatorComponent.GraphAsset == nullptr || AnimatorComponent.IsGraphEnabled == false) {
                AnimatorComponent.clipIndex = AnimatorComponent.FallbackClipIndex;
                continue;
            }

            const std::vector<AnimationGraphAsset::AnimationGraphNodeAsset>& Nodes{ AnimatorComponent.GraphAsset->GetNodes() };
            const std::vector<AnimationGraphAsset::AnimationGraphTransitionAsset>& Transitions{ AnimatorComponent.GraphAsset->GetTransitions() };
            const std::vector<AnimationGraphAsset::AnimationGraphParameterDefinition>& ParameterDefinitions{ AnimatorComponent.GraphAsset->GetParameterDefinitions() };
            if (Nodes.empty()) {
                AnimatorComponent.clipIndex = AnimatorComponent.FallbackClipIndex;
                continue;
            }

            if (GraphPlayer.CurrentNodeIndex < 0 || static_cast<std::size_t>(GraphPlayer.CurrentNodeIndex) >= Nodes.size()) {
                GraphPlayer.CurrentNodeIndex = AnimatorComponent.GraphAsset->GetDefaultNodeIndex();
                GraphPlayer.NextNodeIndex = -1;
                GraphPlayer.CurrentLocalTime = 0.0f;
                GraphPlayer.CurrentNormalizedTime = 0.0f;
                GraphPlayer.IsInTransition = false;
            }

            const AnimationGraphAsset::AnimationGraphNodeAsset& CurrentNode{ Nodes[GraphPlayer.CurrentNodeIndex] };
            const float PlaybackSpeed{ CurrentNode.PlaySpeed <= 0.0f ? 1.0f : CurrentNode.PlaySpeed };

            if (AnimatorComponent.animation != nullptr) {
                const std::size_t ClipIndex{ static_cast<std::size_t>(std::max(CurrentNode.ClipIndex, 0)) };
                const std::vector<asset::AnimationClip>& Clips{ AnimatorComponent.animation->Clips() };
                if (ClipIndex < Clips.size()) {
                    const asset::AnimationClip& Clip{ Clips[ClipIndex] };
                    const double TicksPerSecond{ Clip.TicksPerSecond > 0.0 ? Clip.TicksPerSecond : 30.0 };
                    const float DurationSeconds{ static_cast<float>(Clip.Duration / TicksPerSecond) };
                    GraphPlayer.CurrentLocalTime += Dt * PlaybackSpeed;
                    if (DurationSeconds > 0.0f) {
                        if (CurrentNode.IsLoop) {
                            GraphPlayer.CurrentLocalTime = std::fmod(GraphPlayer.CurrentLocalTime, DurationSeconds);
                            if (GraphPlayer.CurrentLocalTime < 0.0f) {
                                GraphPlayer.CurrentLocalTime += DurationSeconds;
                            }
                        }
                        else {
                            GraphPlayer.CurrentLocalTime = std::clamp(GraphPlayer.CurrentLocalTime, 0.0f, DurationSeconds);
                        }
                        GraphPlayer.CurrentNormalizedTime = Clamp01(GraphPlayer.CurrentLocalTime / DurationSeconds);
                    }
                }
            }

            const AnimationGraphAsset::AnimationGraphTransitionAsset* SelectedTransition{ nullptr };
            for (const AnimationGraphAsset::AnimationGraphTransitionAsset& Transition : Transitions) {
                if (Transition.FromNodeIndex != static_cast<std::uint32_t>(GraphPlayer.CurrentNodeIndex)) {
                    continue;
                }

                if (GraphPlayer.IsInTransition && Transition.CanInterrupt == false) {
                    continue;
                }

                if (Transition.HasExitTime && GraphPlayer.CurrentNormalizedTime < Clamp01(Transition.ExitTimeNormalized)) {
                    continue;
                }

                bool IsSatisfied{ true };
                for (const AnimationGraphAsset::AnimationGraphConditionAsset& Condition : Transition.Conditions) {
                    if (Condition.ParameterIndex >= ParameterDefinitions.size()) {
                        IsSatisfied = false;
                        break;
                    }

                    if (EvaluateCondition(Condition, ParameterDefinitions[Condition.ParameterIndex], GraphPlayer) == false) {
                        IsSatisfied = false;
                        break;
                    }
                }

                if (IsSatisfied) {
                    SelectedTransition = &Transition;
                    break;
                }
            }

            if (SelectedTransition != nullptr) {
                GraphPlayer.NextNodeIndex = static_cast<std::int32_t>(SelectedTransition->ToNodeIndex);
                GraphPlayer.IsInTransition = true;
                GraphPlayer.BlendElapsed = 0.0f;
                GraphPlayer.BlendDuration = std::max(SelectedTransition->BlendDuration, 0.0f);
            }

            if (GraphPlayer.IsInTransition) {
                GraphPlayer.BlendElapsed += Dt;
                GraphPlayer.SampleBlendAlpha = GraphPlayer.BlendDuration <= 0.0f ? 1.0f : Clamp01(GraphPlayer.BlendElapsed / GraphPlayer.BlendDuration);
                GraphPlayer.SampleSourceClipIndex = Nodes[GraphPlayer.CurrentNodeIndex].ClipIndex;
                if (GraphPlayer.NextNodeIndex >= 0 && static_cast<std::size_t>(GraphPlayer.NextNodeIndex) < Nodes.size()) {
                    GraphPlayer.SampleDestinationClipIndex = Nodes[GraphPlayer.NextNodeIndex].ClipIndex;
                }
                else {
                    GraphPlayer.SampleDestinationClipIndex = GraphPlayer.SampleSourceClipIndex;
                }

                if (GraphPlayer.SampleBlendAlpha >= 1.0f && GraphPlayer.NextNodeIndex >= 0 && static_cast<std::size_t>(GraphPlayer.NextNodeIndex) < Nodes.size()) {
                    GraphPlayer.CurrentNodeIndex = GraphPlayer.NextNodeIndex;
                    GraphPlayer.NextNodeIndex = -1;
                    GraphPlayer.CurrentLocalTime = 0.0f;
                    GraphPlayer.CurrentNormalizedTime = 0.0f;
                    GraphPlayer.IsInTransition = false;
                    GraphPlayer.SampleBlendAlpha = 0.0f;
                }
            }
            else {
                GraphPlayer.SampleBlendAlpha = 0.0f;
                GraphPlayer.SampleSourceClipIndex = Nodes[GraphPlayer.CurrentNodeIndex].ClipIndex;
                GraphPlayer.SampleDestinationClipIndex = GraphPlayer.SampleSourceClipIndex;
            }

            GraphPlayer.SampleIsLoop = CurrentNode.IsLoop;
            GraphPlayer.SamplePlaySpeed = PlaybackSpeed;
            AnimatorComponent.clipIndex = GraphPlayer.SampleSourceClipIndex;
            AnimatorComponent.counter = GraphPlayer.CurrentLocalTime;
        }
    }
}
