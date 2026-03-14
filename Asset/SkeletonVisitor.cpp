#include "SkeletonVisitor.h"

#include <string>

using namespace asset;

SkeletonVisitor::SkeletonVisitor(ModelResult& OutResult)
    : mResult{ OutResult } {
}

Mat4 SkeletonVisitor::ToMat4(const ufbx_matrix& Matrix) {
    Mat4 Out{ 1.0f };
    Out.mValue[0][0] = static_cast<float>(Matrix.cols[0].x);
    Out.mValue[1][0] = static_cast<float>(Matrix.cols[0].y);
    Out.mValue[2][0] = static_cast<float>(Matrix.cols[0].z);
    Out.mValue[3][0] = 0.0f;
    Out.mValue[0][1] = static_cast<float>(Matrix.cols[1].x);
    Out.mValue[1][1] = static_cast<float>(Matrix.cols[1].y);
    Out.mValue[2][1] = static_cast<float>(Matrix.cols[1].z);
    Out.mValue[3][1] = 0.0f;
    Out.mValue[0][2] = static_cast<float>(Matrix.cols[2].x);
    Out.mValue[1][2] = static_cast<float>(Matrix.cols[2].y);
    Out.mValue[2][2] = static_cast<float>(Matrix.cols[2].z);
    Out.mValue[3][2] = 0.0f;
    Out.mValue[0][3] = static_cast<float>(Matrix.cols[3].x);
    Out.mValue[1][3] = static_cast<float>(Matrix.cols[3].y);
    Out.mValue[2][3] = static_cast<float>(Matrix.cols[3].z);
    Out.mValue[3][3] = 1.0f;
    return Out;
}

std::string SkeletonVisitor::ToString(const ufbx_string& StringValue) {
    if (StringValue.data == nullptr) {
        return std::string{};
    }
    return std::string{ StringValue.data, StringValue.length };
}

void SkeletonVisitor::RegisterBone(const ufbx_node& Node) {
    if (Node.bone == nullptr) {
        return;
    }

    const std::uint32_t NodeTypedId{ Node.typed_id };
    if (mBoneIndexByNodeTypedId.find(NodeTypedId) != mBoneIndexByNodeTypedId.end()) {
        return;
    }

    SkeletonBone BoneData{};
    BoneData.Name = ToString(Node.bone->name);
    BoneData.NodeName = ToString(Node.name);
    BoneData.NodeTypedId = NodeTypedId;
    BoneData.BoneTypedId = Node.bone->typed_id;
    BoneData.Radius = static_cast<float>(Node.bone->radius);
    BoneData.RelativeLength = static_cast<float>(Node.bone->relative_length);
    BoneData.IsRoot = Node.bone->is_root;
    BoneData.NodeToParent = ToMat4(Node.node_to_parent);
    BoneData.NodeToWorld = ToMat4(Node.node_to_world);

    const std::size_t BoneIndex{ mSkeletonData.Bones.size() };
    mSkeletonData.Bones.push_back(std::move(BoneData));
    mBoneIndexByNodeTypedId.emplace(NodeTypedId, BoneIndex);
}

void SkeletonVisitor::RegisterSkin(const ufbx_node& Node, const ufbx_skin_deformer& Skin) {
    SkeletonSkin SkinData{};
    SkinData.Name = ToString(Skin.name);
    SkinData.MeshNodeName = ToString(Node.name);
    SkinData.SkinDeformerTypedId = Skin.typed_id;
    SkinData.MeshNodeTypedId = Node.typed_id;
    SkinData.SkinningMethod = static_cast<std::uint32_t>(Skin.skinning_method);

    SkinData.ClusterIndices.reserve(Skin.clusters.count);

    for (std::size_t ClusterListIndex{ 0 }; ClusterListIndex < Skin.clusters.count; ++ClusterListIndex) {
        const ufbx_skin_cluster* Cluster{ Skin.clusters.data[ClusterListIndex] };
        if (Cluster == nullptr) {
            continue;
        }

        std::uint32_t BoneIndex{ 0 };
        if (Cluster->bone_node != nullptr) {
            RegisterBone(*Cluster->bone_node);
            const auto FoundBone{ mBoneIndexByNodeTypedId.find(Cluster->bone_node->typed_id) };
            if (FoundBone != mBoneIndexByNodeTypedId.end()) {
                BoneIndex = static_cast<std::uint32_t>(FoundBone->second);
            }
        }

        const std::uint32_t ClusterTypedId{ Cluster->typed_id };
        auto FoundCluster{ mClusterIndexByClusterTypedId.find(ClusterTypedId) };
        if (FoundCluster == mClusterIndexByClusterTypedId.end()) {
            SkeletonCluster ClusterData{};
            ClusterData.Name = ToString(Cluster->name);
            ClusterData.ClusterTypedId = ClusterTypedId;
            ClusterData.SkinDeformerTypedId = Skin.typed_id;
            ClusterData.BoneIndex = BoneIndex;
            ClusterData.GeometryToBone = ToMat4(Cluster->geometry_to_bone);
            ClusterData.MeshNodeToBone = ToMat4(Cluster->mesh_node_to_bone);
            ClusterData.BindToWorld = ToMat4(Cluster->bind_to_world);
            ClusterData.GeometryToWorld = ToMat4(Cluster->geometry_to_world);

            const std::size_t NewClusterIndex{ mSkeletonData.Clusters.size() };
            mSkeletonData.Clusters.push_back(std::move(ClusterData));
            mClusterIndexByClusterTypedId.emplace(ClusterTypedId, NewClusterIndex);
            SkinData.ClusterIndices.push_back(static_cast<std::uint32_t>(NewClusterIndex));
        }
        else {
            SkinData.ClusterIndices.push_back(static_cast<std::uint32_t>(FoundCluster->second));
        }
    }

    mSkeletonData.Skins.push_back(std::move(SkinData));
}

void SkeletonVisitor::OnNodeBegin(const ufbx_scene& Scene, const ufbx_node& Node, const NodeVisitContext& Context) {
    static_cast<void>(Scene);
    static_cast<void>(Context);

    RegisterBone(Node);

    if (Node.mesh == nullptr) {
        return;
    }

    const ufbx_mesh& Mesh{ *Node.mesh };
    for (std::size_t SkinIndex{ 0 }; SkinIndex < Mesh.skin_deformers.count; ++SkinIndex) {
        const ufbx_skin_deformer* Skin{ Mesh.skin_deformers.data[SkinIndex] };
        if (Skin == nullptr) {
            continue;
        }
        RegisterSkin(Node, *Skin);
    }
}

void SkeletonVisitor::OnNodeEnd(const ufbx_scene& Scene, const ufbx_node& Node) {
    static_cast<void>(Scene);
    static_cast<void>(Node);
}

void SkeletonVisitor::Finalize(const ufbx_scene& Scene) {
    static_cast<void>(Scene);
    mResult.SetSkeletonData(std::move(mSkeletonData));
}
