#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
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
		struct DrawBatchKey final {
			const Interface::IPipeline* pso{ nullptr };
			const Interface::IModelNode* mesh{ nullptr };
			std::uint32_t submesh{ 0 };
			std::uint32_t pass{ 0 };

			bool operator==(const DrawBatchKey& other) const {
				return pso == other.pso && mesh == other.mesh && submesh == other.submesh && pass == other.pass;
			}
		};

		struct DrawBatchKeyHasher final {
			std::size_t operator()(const DrawBatchKey& key) const noexcept {
				std::size_t seed{ 0 };
				seed ^= std::hash<const void*>{}(key.pso) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
				seed ^= std::hash<const void*>{}(key.mesh) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
				seed ^= std::hash<std::uint32_t>{}(key.submesh) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
				seed ^= std::hash<std::uint32_t>{}(key.pass) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
				return seed;
			}
		};

		using InstanceBucket = std::vector<RFD::DrawInstance>;
		using InstanceBucketMap = std::unordered_map<DrawBatchKey, InstanceBucket, DrawBatchKeyHasher>;

	private:
		void TraverseNode(const ModelNode& node, const std::vector<ModelNode>& nodes, const SimpleMath::Matrix& parentWorld, RFD::RenderFrameData& renderData, InstanceBucketMap& drawInstanceBuckets, std::vector<DrawBatchKey>& batchOrder) const;
	
	private:
		const std::string mName{ "StaticRenderSystem" };
	};
}
