#pragma once
#include <string>
#include <vector>

namespace Game {
    namespace Pipeline {
        struct PipelineSystemBindingResult final {
        public:
            PipelineSystemBindingResult();
            ~PipelineSystemBindingResult();

            PipelineSystemBindingResult(const PipelineSystemBindingResult& Other);
            PipelineSystemBindingResult& operator=(const PipelineSystemBindingResult& Other);

            PipelineSystemBindingResult(PipelineSystemBindingResult&& Other) noexcept;
            PipelineSystemBindingResult& operator=(PipelineSystemBindingResult&& Other) noexcept;

        public:
            bool IsSuccess{ true };
            std::vector<std::string> FailureMessages{};
        };
    }
}
