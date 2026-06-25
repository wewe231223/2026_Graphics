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
    uint mReserved0;
};

ConstantBuffer<EnvironmentGpuRootConstants> RootConstants : register(b1);

[numthreads(64, 1, 1)]
void CsMain(uint3 DispatchThreadId : SV_DispatchThreadID) {
    RWStructuredBuffer<uint> StatusBuffer = ResourceDescriptorHeap[RootConstants.mStatusUavIndex];

    if (DispatchThreadId.x != 0u) {
        return;
    }

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
    StatusBuffer[10] = DispatchThreadId.x;
    StatusBuffer[11] = 1u;
    StatusBuffer[12] = 0u;
    StatusBuffer[13] = 0u;
    StatusBuffer[14] = 0u;
    StatusBuffer[15] = 0u;
}
