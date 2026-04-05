#include "AnimationGraphSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

#include "Game/Asset/AnimationGraphAsset.h"
#include "Game/Scene/Components/Animator.h"
#include "Game/Scene/Components/AnimatorGraphPlayer.h"
#include "Game/Scene/Components/RuntimeVariableTable.h"

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

    float AdvanceAnimationLocalTime(float CurrentLocalTime, float DeltaTime, float PlaySpeed, bool IsLoop, const asset::AnimationClip& Clip) {
        const double TicksPerSecond{ Clip.TicksPerSecond > 0.0 ? Clip.TicksPerSecond : 30.0 };
        const float DurationSeconds{ static_cast<float>(Clip.Duration / TicksPerSecond) };
        if (DurationSeconds <= 0.0f) {
            return 0.0f;
        }

        float NextLocalTime{ CurrentLocalTime + (DeltaTime * PlaySpeed) };
        if (IsLoop == true) {
            NextLocalTime = std::fmod(NextLocalTime, DurationSeconds);
            if (NextLocalTime < 0.0f) {
                NextLocalTime += DurationSeconds;
            }
            return NextLocalTime;
        }

        return std::clamp(NextLocalTime, 0.0f, DurationSeconds);
    }

    bool EvaluateCondition(const Game::AnimationGraphAsset::AnimationGraphConditionAsset& Condition, const Game::RuntimeParameterDefinition& Definition, const Game::RuntimeVariableTable& VariableTable) {
        if (Condition.ParameterIndex >= Game::RuntimeVariableTableMaxParameterCount) {
            return false;
        }

        if (Definition.ParameterTypeValue == Game::RuntimeParameterDefinition::ParameterType::Trigger) {
            if (VariableTable.TriggerConsumed[Condition.ParameterIndex]) {
                return false;
            }

            const bool LeftValue{ VariableTable.BoolValues[Condition.ParameterIndex] };
            if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::NotEquals) {
                return LeftValue != Condition.BoolValue;
            }

            return LeftValue == Condition.BoolValue;
        }

        if (Definition.ParameterTypeValue == Game::RuntimeParameterDefinition::ParameterType::Bool) {
            const bool LeftValue{ VariableTable.BoolValues[Condition.ParameterIndex] };
            if (Condition.Operator == Game::AnimationGraphAsset::ConditionOperator::NotEquals) {
                return LeftValue != Condition.BoolValue;
            }
            return LeftValue == Condition.BoolValue;
        }

        if (Definition.ParameterTypeValue == Game::RuntimeParameterDefinition::ParameterType::Int) {
            const std::int32_t LeftValue{ VariableTable.IntValues[Condition.ParameterIndex] };
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

        const float LeftValue{ VariableTable.FloatValues[Condition.ParameterIndex] };
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

    void ConsumeTriggerParameters(const Game::AnimationGraphAsset::AnimationGraphTransitionAsset& Transition, const std::vector<Game::RuntimeParameterDefinition>& ParameterDefinitions, Game::RuntimeVariableTable& OutVariableTable) {
        for (const Game::AnimationGraphAsset::AnimationGraphConditionAsset& Condition : Transition.Conditions) {
            if (Condition.ParameterIndex >= ParameterDefinitions.size() || Condition.ParameterIndex >= Game::RuntimeVariableTableMaxParameterCount) {
                continue;
            }

            if (ParameterDefinitions[Condition.ParameterIndex].ParameterTypeValue != Game::RuntimeParameterDefinition::ParameterType::Trigger) {
                continue;
            }

            OutVariableTable.BoolValues[Condition.ParameterIndex] = false;
            OutVariableTable.TriggerConsumed[Condition.ParameterIndex] = true;
        }
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
        static std::array<ComponentAccess, 3> Accesses{ { { typeid(Animator), Access::Write }, { typeid(AnimatorGraphPlayer), Access::Write }, { typeid(RuntimeVariableTable), Access::Write } } };
        return Accesses;
    }

    std::span<const ResourceAccess> AnimationGraphSystem::ResourceAccesses() const {
        static std::array<ResourceAccess, 0> Accesses{};
        return Accesses;
    }

    void AnimationGraphSystem::Execute(Arche::World& World, FrameContext& Ctx, float Dt) {
        for (auto [AnimatorComponent, GraphPlayer, VariableTable] : World.Query<Animator, AnimatorGraphPlayer, RuntimeVariableTable>()) {
            if (AnimatorComponent.GraphAsset == nullptr || AnimatorComponent.IsGraphEnabled == false) {
                AnimatorComponent.clipIndex = AnimatorComponent.FallbackClipIndex;
                continue;
            }

            const std::vector<AnimationGraphAsset::AnimationGraphNodeAsset>& Nodes{ AnimatorComponent.GraphAsset->GetNodes() };
            const std::vector<AnimationGraphAsset::AnimationGraphTransitionAsset>& Transitions{ AnimatorComponent.GraphAsset->GetTransitions() };
            const std::vector<RuntimeParameterDefinition>& ParameterDefinitions{ AnimatorComponent.GraphAsset->GetParameterDefinitions() };

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

            if (AnimatorComponent.animation != nullptr && GraphPlayer.IsInTransition == false) {
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
            if (GraphPlayer.IsInTransition == false) {
                for (const AnimationGraphAsset::AnimationGraphTransitionAsset& Transition : Transitions) {
                    if (Transition.FromNodeIndex != static_cast<std::uint32_t>(GraphPlayer.CurrentNodeIndex)) {
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

                        if (EvaluateCondition(Condition, ParameterDefinitions[Condition.ParameterIndex], VariableTable) == false) {
                            IsSatisfied = false;
                            break;
                        }
                    }

                    if (IsSatisfied) {
                        SelectedTransition = &Transition;
                        break;
                    }
                }
            }

            if (SelectedTransition != nullptr) {
                ConsumeTriggerParameters(*SelectedTransition, ParameterDefinitions, VariableTable);
                GraphPlayer.NextNodeIndex = static_cast<std::int32_t>(SelectedTransition->ToNodeIndex);
                GraphPlayer.IsInTransition = true;
                GraphPlayer.BlendElapsed = 0.0f;
                GraphPlayer.BlendDuration = std::max(SelectedTransition->BlendDuration, 0.0f);
                GraphPlayer.SampleSourceLocalTime = GraphPlayer.CurrentLocalTime;
                GraphPlayer.SampleDestinationLocalTime = 0.0f;
            }

            if (GraphPlayer.IsInTransition) {
                GraphPlayer.BlendElapsed += Dt;
                GraphPlayer.SampleBlendAlpha = GraphPlayer.BlendDuration <= 0.0f ? 1.0f : Clamp01(GraphPlayer.BlendElapsed / GraphPlayer.BlendDuration);
                GraphPlayer.SampleSourceClipIndex = Nodes[GraphPlayer.CurrentNodeIndex].ClipIndex;
                GraphPlayer.SampleSourceLocalTime = GraphPlayer.CurrentLocalTime;
                GraphPlayer.SampleDestinationLocalTime = GraphPlayer.BlendElapsed;
                if (GraphPlayer.NextNodeIndex >= 0 && static_cast<std::size_t>(GraphPlayer.NextNodeIndex) < Nodes.size()) {
                    const AnimationGraphAsset::AnimationGraphNodeAsset& DestinationNode{ Nodes[GraphPlayer.NextNodeIndex] };
                    GraphPlayer.SampleDestinationClipIndex = DestinationNode.ClipIndex;

                    if (AnimatorComponent.animation != nullptr) {
                        const std::size_t DestinationClipIndex{ static_cast<std::size_t>(std::max(DestinationNode.ClipIndex, 0)) };
                        const std::vector<asset::AnimationClip>& Clips{ AnimatorComponent.animation->Clips() };
                        if (DestinationClipIndex < Clips.size()) {
                            const float DestinationPlaySpeed{ DestinationNode.PlaySpeed <= 0.0f ? 1.0f : DestinationNode.PlaySpeed };
                            GraphPlayer.SampleDestinationLocalTime = AdvanceAnimationLocalTime(0.0f, GraphPlayer.BlendElapsed, DestinationPlaySpeed, DestinationNode.IsLoop, Clips[DestinationClipIndex]);
                        }
                    }
                }
                else {
                    GraphPlayer.SampleDestinationClipIndex = GraphPlayer.SampleSourceClipIndex;
                }

                if (GraphPlayer.SampleBlendAlpha >= 1.0f && GraphPlayer.NextNodeIndex >= 0 && static_cast<std::size_t>(GraphPlayer.NextNodeIndex) < Nodes.size()) {
                    GraphPlayer.CurrentNodeIndex = GraphPlayer.NextNodeIndex;
                    GraphPlayer.NextNodeIndex = -1;
                    GraphPlayer.CurrentLocalTime = GraphPlayer.SampleDestinationLocalTime;
                    GraphPlayer.CurrentNormalizedTime = 0.0f;
                    if (AnimatorComponent.animation != nullptr) {
                        const std::size_t CurrentClipIndex{ static_cast<std::size_t>(std::max(Nodes[GraphPlayer.CurrentNodeIndex].ClipIndex, 0)) };
                        const std::vector<asset::AnimationClip>& Clips{ AnimatorComponent.animation->Clips() };
                        if (CurrentClipIndex < Clips.size()) {
                            const asset::AnimationClip& CurrentClip{ Clips[CurrentClipIndex] };
                            const double TicksPerSecond{ CurrentClip.TicksPerSecond > 0.0 ? CurrentClip.TicksPerSecond : 30.0 };
                            const float DurationSeconds{ static_cast<float>(CurrentClip.Duration / TicksPerSecond) };
                            if (DurationSeconds > 0.0f) {
                                GraphPlayer.CurrentNormalizedTime = Clamp01(GraphPlayer.CurrentLocalTime / DurationSeconds);
                            }
                        }
                    }
                    GraphPlayer.IsInTransition = false;
                    GraphPlayer.SampleBlendAlpha = 0.0f;
                    GraphPlayer.SampleSourceClipIndex = Nodes[GraphPlayer.CurrentNodeIndex].ClipIndex;
                    GraphPlayer.SampleDestinationClipIndex = GraphPlayer.SampleSourceClipIndex;
                    GraphPlayer.SampleSourceLocalTime = GraphPlayer.CurrentLocalTime;
                    GraphPlayer.SampleDestinationLocalTime = GraphPlayer.CurrentLocalTime;
                }
            }
            else {
                GraphPlayer.SampleBlendAlpha = 0.0f;
                GraphPlayer.SampleSourceClipIndex = Nodes[GraphPlayer.CurrentNodeIndex].ClipIndex;
                GraphPlayer.SampleDestinationClipIndex = GraphPlayer.SampleSourceClipIndex;
                GraphPlayer.SampleSourceLocalTime = GraphPlayer.CurrentLocalTime;
                GraphPlayer.SampleDestinationLocalTime = GraphPlayer.CurrentLocalTime;
            }

            GraphPlayer.SampleIsLoop = CurrentNode.IsLoop;
            GraphPlayer.SamplePlaySpeed = PlaybackSpeed;
            AnimatorComponent.clipIndex = GraphPlayer.SampleSourceClipIndex;
            AnimatorComponent.counter = GraphPlayer.SampleSourceLocalTime;
        }
    }
}
