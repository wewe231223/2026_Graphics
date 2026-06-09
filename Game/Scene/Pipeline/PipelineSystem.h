#pragma once
#include <string>

namespace Game {
    namespace Pipeline {
        class PipelineContext;

        class IPipelineSystem abstract {
        public:
            virtual ~IPipelineSystem() = default;

        public:
            virtual const std::string& Name() const = 0;
            virtual void Execute(PipelineContext& Ctx, float Dt) = 0;
        };
    }
}
