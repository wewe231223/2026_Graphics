#include "PostProcessCommon.hlsli"

float3 ApplyToneMapping(float3 HdrColor) {
    const float Gamma = max(asfloat(RootConstants.mParameter1), 0.0001f);
    const float InvGamma = 1.0f / Gamma;
    if (RootConstants.mParameter2 == 0u) {
        const float3 LinearColor = saturate(max(HdrColor, 0.0f));
        return Gamma == 1.0f ? LinearColor : pow(LinearColor, InvGamma);
    }

    const float Exposure = max(asfloat(RootConstants.mParameter0), 0.0f);
    const float3 ExposedColor = max(HdrColor * Exposure, 0.0f);
    const float3 MappedColor = (ExposedColor * ((2.51f * ExposedColor) + 0.03f)) / ((ExposedColor * ((2.43f * ExposedColor) + 0.59f)) + 0.14f);
    const float3 SaturatedMappedColor = saturate(MappedColor);
    return Gamma == 1.0f ? SaturatedMappedColor : pow(SaturatedMappedColor, InvGamma);
}

[numthreads(8, 8, 1)]
void CsMain(uint3 DispatchThreadId : SV_DispatchThreadID) {
    const uint2 PixelPosition = DispatchThreadId.xy;
    if (PixelPosition.x >= RootConstants.mTargetWidth || PixelPosition.y >= RootConstants.mTargetHeight) {
        return;
    }

    Texture2D<float4> SourceTexture = ResourceDescriptorHeap[RootConstants.mSourceSrvIndex];
    RWTexture2D<float4> DestinationTexture = ResourceDescriptorHeap[RootConstants.mDestinationUavIndex];
    const float4 SourceColor = SourceTexture.Load(int3(int2(PixelPosition), 0));
    const float3 ToneMappedColor = ApplyToneMapping(SourceColor.rgb);
    DestinationTexture[PixelPosition] = float4(ToneMappedColor, SourceColor.a);
}
