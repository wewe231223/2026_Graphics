#include "Game/Scene/Components/BoundingBox.h"

#include <format>
#include <vector>
#include "Game/Model/Model.h"
#include "Game/Scene/Components/ComponentInspection.h"

namespace {
    bool TryBuildObbFromModelNodePositions(const Game::ModelNode& ModelNodeValue, DirectX::BoundingOrientedBox& OutObb) {
        std::size_t PositionAttributeIndex{ ModelNodeValue.GetVertexAttributeBufferCount() };

        for (std::size_t AttributeIndex{ 0 }; AttributeIndex < ModelNodeValue.GetVertexAttributeBufferCount(); ++AttributeIndex) {
            if (ModelNodeValue.GetVertexAttributeKind(AttributeIndex) == Game::VertexAttributeKind::Position) {
                PositionAttributeIndex = AttributeIndex;
                break;
            }
        }

        if (PositionAttributeIndex == ModelNodeValue.GetVertexAttributeBufferCount()) {
            return false;
        }

        const std::span<const std::byte> PositionRawData{ ModelNodeValue.GetVertexAttributeRawData(PositionAttributeIndex) };

        if (PositionRawData.size() < sizeof(asset::Vec3)) {
            return false;
        }

        const std::size_t VertexCount{ PositionRawData.size() / sizeof(asset::Vec3) };
        const asset::Vec3* Positions{ reinterpret_cast<const asset::Vec3*>(PositionRawData.data()) };
        std::vector<DirectX::XMFLOAT3> PositionPoints{};
        PositionPoints.reserve(VertexCount);

        for (std::size_t VertexIndex{ 0 }; VertexIndex < VertexCount; ++VertexIndex) {
            PositionPoints.push_back(DirectX::XMFLOAT3{ Positions[VertexIndex].x, Positions[VertexIndex].y, Positions[VertexIndex].z });
        }

        if (PositionPoints.empty()) {
            return false;
        }

        DirectX::BoundingBox AxisAlignedBoundingBox{};
        DirectX::BoundingBox::CreateFromPoints(AxisAlignedBoundingBox, PositionPoints.size(), PositionPoints.data(), sizeof(DirectX::XMFLOAT3));
        DirectX::BoundingOrientedBox::CreateFromBoundingBox(OutObb, AxisAlignedBoundingBox);
        return true;
    }
}

namespace Game {
    const char* BoundingBox::GetComponentInspectionName() {
        return "BoundingBox";
    }

    void BoundingBox::BuildComponentInspectionFields(std::vector<ComponentInspectionField>& OutFields) const {
        OutFields.push_back(ComponentInspectionField{ "Center", std::format("{:.3f}, {:.3f}, {:.3f}", mObb.Center.x, mObb.Center.y, mObb.Center.z) });
        OutFields.push_back(ComponentInspectionField{ "Extents", std::format("{:.3f}, {:.3f}, {:.3f}", mObb.Extents.x, mObb.Extents.y, mObb.Extents.z) });
    }

    void BoundingBox::ResetToUnitCube() {
        mObb.Center = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        mObb.Extents = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
        mObb.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
        mWorldObb = mObb;
        mIsWorldObbValid = false;
    }

    void BoundingBox::UpdateFromModel(const Model* ModelValue, std::uint32_t NodeIndex) {
        ResetToUnitCube();

        if (ModelValue == nullptr) {
            return;
        }

        const std::vector<ModelNode>& ModelNodes{ ModelValue->GetNodes() };

        if (NodeIndex >= ModelNodes.size()) {
            return;
        }

        const ModelNode& TargetNode{ ModelNodes[NodeIndex] };
        if (TargetNode.HasBoundingBox() == true) {
            mObb = TargetNode.GetBoundingBox();
            mWorldObb = mObb;
            mIsWorldObbValid = false;
            return;
        }

        DirectX::BoundingOrientedBox ExtractedObb{};
        const bool IsBuilt{ TryBuildObbFromModelNodePositions(TargetNode, ExtractedObb) };
        if (IsBuilt == false) {
            return;
        }

        mObb = ExtractedObb;
        mWorldObb = mObb;
        mIsWorldObbValid = false;
    }

    void BoundingBox::UpdateWorldObb(const SimpleMath::Matrix& WorldMatrix) {
        mObb.Transform(mWorldObb, WorldMatrix);
        mIsWorldObbValid = true;
    }

    void BoundingBox::InvalidateWorldObb() {
        mWorldObb = mObb;
        mIsWorldObbValid = false;
    }

    bool BoundingBox::HasWorldObb() const {
        return mIsWorldObbValid;
    }

    const DirectX::BoundingOrientedBox& BoundingBox::GetObb() const {
        return mObb;
    }

    const DirectX::BoundingOrientedBox& BoundingBox::GetWorldObb() const {
        return mWorldObb;
    }
}
