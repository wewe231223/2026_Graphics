#include "AnimatorGraphPlayer.h"

#include <format>
#include "Game/Scene/Components/ComponentInspection.h"

namespace Game {
    const char* AnimatorGraphPlayer::GetComponentInspectionName() {
        return "AnimatorGraphPlayer";
    }

    void AnimatorGraphPlayer::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "CurrentNodeIndex", std::format("{}", CurrentNodeIndex) });
        OutFields.push_back(ComponentInspectionField{ "NextNodeIndex", std::format("{}", NextNodeIndex) });
        OutFields.push_back(ComponentInspectionField{ "CurrentLocalTime", std::format("{}", CurrentLocalTime) });
        OutFields.push_back(ComponentInspectionField{ "BlendAlpha", std::format("{}", SampleBlendAlpha) });
        OutFields.push_back(ComponentInspectionField{ "IsInTransition", IsInTransition ? "true" : "false" });
    }
}
