#ifndef POST_PROCESS_COMMON_HLSLI
#define POST_PROCESS_COMMON_HLSLI

struct PostProcessRootConstants {
    uint mSourceSrvIndex;
    uint mDestinationUavIndex;
    uint mTargetWidth;
    uint mTargetHeight;
    uint mParameter0;
    uint mParameter1;
    uint mParameter2;
    uint mParameter3;
    uint mParameter4;
    uint mParameter5;
    uint mParameter6;
    uint mParameter7;
};

ConstantBuffer<PostProcessRootConstants> RootConstants : register(b1);

#endif
