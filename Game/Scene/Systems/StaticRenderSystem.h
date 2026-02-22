#pragma once
#include <cstddef>
#include <cstdint>
#include "Game/Scene/System.h"
#include "Game/Model/Model.h"

namespace Game {
	class StaticRenderSystem final : public ISystem {
	public:
		StaticRenderSystem() = default;
		~StaticRenderSystem() override = default;

		StaticRenderSystem(const StaticRenderSystem&) = default;
		StaticRenderSystem& operator=(const StaticRenderSystem&) = default;

		StaticRenderSystem(StaticRenderSystem&&) noexcept = default;
		StaticRenderSystem& operator=(StaticRenderSystem&&) noexcept = default;
	public:
		virtual const std::string& Name() const override;
		virtual Phase GetPhase() const override;
		virtual std::span<const ComponentAccess> ComponentAccesses() const override;
		virtual std::span<const ResourceAccess> ResourceAccesses() const override;
		virtual void Execute(Arche::World& World, FrameContext& Ctx, float Dt) override;

	private:
		void TraverseNode(const ModelNode& node, const std::vector<ModelNode>& nodes, const SimpleMath::Matrix& parentWorld, RFD::RenderFrameData& renderData) const;
	
	private:
		const std::string mName{ "StaticRenderSystem" };
	};
}
