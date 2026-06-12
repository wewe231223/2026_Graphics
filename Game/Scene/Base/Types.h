#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Game {
    namespace Pipeline {
        using PipelineId = std::uint32_t;

        inline constexpr PipelineId InvalidPipelineId{ std::numeric_limits<PipelineId>::max() };

        class PipelineDefinition final {
        public:
            PipelineDefinition();
            ~PipelineDefinition();

            PipelineDefinition(const PipelineDefinition& Other);
            PipelineDefinition& operator=(const PipelineDefinition& Other);

            PipelineDefinition(PipelineDefinition&& Other) noexcept;
            PipelineDefinition& operator=(PipelineDefinition&& Other) noexcept;

        public:
            PipelineId GetPipelineId() const;
            void SetPipelineId(PipelineId PipelineIdValue);

            std::string& GetName();
            const std::string& GetName() const;

            std::vector<std::string>& GetSystemNames();
            const std::vector<std::string>& GetSystemNames() const;

        private:
            PipelineId mPipelineId{ InvalidPipelineId };
            std::string mName{};
            std::vector<std::string> mSystemNames{};
        };
    }
}
