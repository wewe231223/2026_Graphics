#include "Animator.h"
#include <format>
#include <string>
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    std::string ResolveClipName(const Game::Animator& AnimatorComponent) {
        if (AnimatorComponent.animation == nullptr || AnimatorComponent.clipIndex < 0) {
            return "<None>";
        }

        const std::vector<asset::AnimationClip>& Clips{ AnimatorComponent.animation->Clips() };
        const std::size_t ClipIndex{ static_cast<std::size_t>(AnimatorComponent.clipIndex) };
        if (ClipIndex >= Clips.size()) {
            return "<None>";
        }

        if (Clips[ClipIndex].Name.empty()) {
            return std::format("Clip_{}", ClipIndex);
        }

        return Clips[ClipIndex].Name;
    }
}

namespace Game {
    const char* Animator::GetComponentInspectionName() {
        return "Animator";
    }

    void Animator::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "AnimationBound", animation == nullptr ? "false" : "true" });
        OutFields.push_back(ComponentInspectionField{ "ClipIndex", std::format("{}", clipIndex) });
        OutFields.push_back(ComponentInspectionField{ "CurrentClip", ResolveClipName(*this) });
        OutFields.push_back(ComponentInspectionField{ "Counter", std::format("{}", counter) });
    }
}
