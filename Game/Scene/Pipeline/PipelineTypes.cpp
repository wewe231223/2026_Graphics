#include "PipelineTypes.h"
#include <utility>

namespace Game {
    namespace Pipeline {
        PipelineDefinition::PipelineDefinition()
            : mPipelineId{ InvalidPipelineId },
            mName{},
            mSystemNames{} {
        }

        PipelineDefinition::~PipelineDefinition() {
        }

        PipelineDefinition::PipelineDefinition(const PipelineDefinition& Other)
            : mPipelineId{ Other.mPipelineId },
            mName{ Other.mName },
            mSystemNames{ Other.mSystemNames } {
        }

        PipelineDefinition& PipelineDefinition::operator=(const PipelineDefinition& Other) {
            if (this == &Other) {
                return *this;
            }

            mPipelineId = Other.mPipelineId;
            mName = Other.mName;
            mSystemNames = Other.mSystemNames;
            return *this;
        }

        PipelineDefinition::PipelineDefinition(PipelineDefinition&& Other) noexcept
            : mPipelineId{ std::move(Other.mPipelineId) },
            mName{ std::move(Other.mName) },
            mSystemNames{ std::move(Other.mSystemNames) } {
        }

        PipelineDefinition& PipelineDefinition::operator=(PipelineDefinition&& Other) noexcept {
            if (this == &Other) {
                return *this;
            }

            mPipelineId = std::move(Other.mPipelineId);
            mName = std::move(Other.mName);
            mSystemNames = std::move(Other.mSystemNames);
            return *this;
        }

        PipelineId PipelineDefinition::GetPipelineId() const {
            return mPipelineId;
        }

        void PipelineDefinition::SetPipelineId(PipelineId PipelineIdValue) {
            mPipelineId = PipelineIdValue;
        }

        std::string& PipelineDefinition::GetName() {
            return mName;
        }

        const std::string& PipelineDefinition::GetName() const {
            return mName;
        }

        std::vector<std::string>& PipelineDefinition::GetSystemNames() {
            return mSystemNames;
        }

        const std::vector<std::string>& PipelineDefinition::GetSystemNames() const {
            return mSystemNames;
        }
    }
}
