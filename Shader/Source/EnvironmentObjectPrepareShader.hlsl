struct EnvironmentGpuRootConstants {
    uint mStatusUavIndex;
    uint mFrameIndexLow;
    uint mFrameIndexHigh;
    uint mTerrainHeightSrvIndex;
    uint mTerrainSplatSrvIndex;
    uint mTerrainWidth;
    uint mTerrainHeight;
    uint mFocusPositionX;
    uint mFocusPositionY;
    uint mFocusPositionZ;
    uint mDispatchThreadGroupSize;
    uint mInstanceContextSrvIndex;
    uint mDrawRecordSrvIndex;
    uint mIndirectArgumentUavIndex;
    uint mVisibleInstanceIndexUavIndex;
    uint mDrawRecordCount;
    uint mVisibleInstanceIndexCapacity;
    uint mMaximumDrawDistance;
    uint mCullingRadius;
    uint mReserved0;
    uint4 mFrustumPlanes[6];
};

ConstantBuffer<EnvironmentGpuRootConstants> RootConstants : register(b1);

struct EnvironmentInstanceContextGpu {
    float4 PositionScale;
    float4 RotationVariation;
};

struct EnvironmentDrawRecordGpu {
    uint InstanceOffset;
    uint InstanceCount;
    uint SegmentContextIndex;
    uint MaterialIndex;
    uint Flags;
    uint VisibleInstanceOffset;
    uint GpuDrivenFlags;
    uint Padding2;
};

struct EnvironmentIndirectDrawArgument {
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
};

float4 LoadFrustumPlane(uint PlaneIndex) {
    const uint4 PackedPlane = RootConstants.mFrustumPlanes[PlaneIndex];
    return float4(asfloat(PackedPlane.x), asfloat(PackedPlane.y), asfloat(PackedPlane.z), asfloat(PackedPlane.w));
}

bool IsEnvironmentInstanceVisible(float3 Position, float Radius) {
    const float3 FocusPosition = float3(asfloat(RootConstants.mFocusPositionX), asfloat(RootConstants.mFocusPositionY), asfloat(RootConstants.mFocusPositionZ));
    const float MaximumDrawDistance = asfloat(RootConstants.mMaximumDrawDistance);
    const float3 FocusDelta = Position - FocusPosition;
    if (dot(FocusDelta, FocusDelta) > MaximumDrawDistance * MaximumDrawDistance) {
        return false;
    }

    [unroll]
    for (uint PlaneIndex = 0u; PlaneIndex < 6u; PlaneIndex += 1u) {
        const float4 Plane = LoadFrustumPlane(PlaneIndex);
        if (dot(Plane.xyz, Position) + Plane.w < -Radius) {
            return false;
        }
    }

    return true;
}

[numthreads(64, 1, 1)]
void CsMain(uint3 DispatchThreadId : SV_DispatchThreadID, uint3 GroupId : SV_GroupID, uint3 GroupThreadId : SV_GroupThreadID) {
    RWStructuredBuffer<uint> StatusBuffer = ResourceDescriptorHeap[RootConstants.mStatusUavIndex];

    if (DispatchThreadId.x == 0u) {
        StatusBuffer[0] = RootConstants.mFrameIndexLow;
        StatusBuffer[1] = RootConstants.mFrameIndexHigh;
        StatusBuffer[2] = RootConstants.mTerrainHeightSrvIndex;
        StatusBuffer[3] = RootConstants.mTerrainSplatSrvIndex;
        StatusBuffer[4] = RootConstants.mTerrainWidth;
        StatusBuffer[5] = RootConstants.mTerrainHeight;
        StatusBuffer[6] = RootConstants.mFocusPositionX;
        StatusBuffer[7] = RootConstants.mFocusPositionY;
        StatusBuffer[8] = RootConstants.mFocusPositionZ;
        StatusBuffer[9] = RootConstants.mDispatchThreadGroupSize;
        StatusBuffer[10] = RootConstants.mDrawRecordCount;
        StatusBuffer[11] = RootConstants.mVisibleInstanceIndexCapacity;
        StatusBuffer[12] = 1u;
        StatusBuffer[13] = 0u;
        StatusBuffer[14] = 0u;
        StatusBuffer[15] = 0u;
    }

    if (RootConstants.mDrawRecordCount == 0u || GroupId.x >= RootConstants.mDrawRecordCount) {
        return;
    }

    StructuredBuffer<EnvironmentInstanceContextGpu> InstanceContextBuffer = ResourceDescriptorHeap[RootConstants.mInstanceContextSrvIndex];
    StructuredBuffer<EnvironmentDrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.mDrawRecordSrvIndex];
    RWStructuredBuffer<EnvironmentIndirectDrawArgument> IndirectArgumentBuffer = ResourceDescriptorHeap[RootConstants.mIndirectArgumentUavIndex];
    RWStructuredBuffer<uint> VisibleInstanceIndexBuffer = ResourceDescriptorHeap[RootConstants.mVisibleInstanceIndexUavIndex];

    const uint DrawRecordIndex = GroupId.x;
    const EnvironmentDrawRecordGpu DrawRecord = DrawRecordBuffer[DrawRecordIndex];

    if (GroupThreadId.x == 0u) {
        IndirectArgumentBuffer[DrawRecordIndex].InstanceCount = 0u;
    }

    DeviceMemoryBarrierWithGroupSync();

    const float BaseRadius = asfloat(RootConstants.mCullingRadius);
    for (uint LocalInstanceIndex = GroupThreadId.x; LocalInstanceIndex < DrawRecord.InstanceCount; LocalInstanceIndex += RootConstants.mDispatchThreadGroupSize) {
        const uint InstanceIndex = DrawRecord.InstanceOffset + LocalInstanceIndex;
        const EnvironmentInstanceContextGpu InstanceContext = InstanceContextBuffer[InstanceIndex];
        const float Radius = max(BaseRadius * max(InstanceContext.PositionScale.w, 0.01f), 0.01f);
        if (IsEnvironmentInstanceVisible(InstanceContext.PositionScale.xyz, Radius) == false) {
            continue;
        }

        uint VisibleLocalIndex = 0u;
        InterlockedAdd(IndirectArgumentBuffer[DrawRecordIndex].InstanceCount, 1u, VisibleLocalIndex);
        const uint VisibleIndex = DrawRecord.VisibleInstanceOffset + VisibleLocalIndex;
        if (VisibleIndex < RootConstants.mVisibleInstanceIndexCapacity) {
            VisibleInstanceIndexBuffer[VisibleIndex] = InstanceIndex;
        }
    }
}
