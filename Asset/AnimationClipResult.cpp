#include "AnimationClipResult.h"

namespace asset {
    AnimationClipResult::AnimationClipResult() {
    }

    std::vector<AnimationClip>& AnimationClipResult::Clips() {
        return mClips;
    }

    const std::vector<AnimationClip>& AnimationClipResult::Clips() const {
        return mClips;
    }

    std::size_t AnimationClipResult::ClipCount() const {
        return mClips.size();
    }
}
