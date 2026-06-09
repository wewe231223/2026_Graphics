#include "PipelineSystemBindingResult.h"

namespace Game {
    namespace Pipeline {
        PipelineSystemBindingResult::PipelineSystemBindingResult() = default;
        PipelineSystemBindingResult::~PipelineSystemBindingResult() = default;
        PipelineSystemBindingResult::PipelineSystemBindingResult(const PipelineSystemBindingResult& Other) = default;
        PipelineSystemBindingResult& PipelineSystemBindingResult::operator=(const PipelineSystemBindingResult& Other) = default;
        PipelineSystemBindingResult::PipelineSystemBindingResult(PipelineSystemBindingResult&& Other) noexcept = default;
        PipelineSystemBindingResult& PipelineSystemBindingResult::operator=(PipelineSystemBindingResult&& Other) noexcept = default;
    }
}
