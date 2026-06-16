#include "PostProcessCommon.hlsli"

float3 EncodeLinearToSrgbColor(float3 Color) {
    const float3 SqrtColor = sqrt(Color);
    const float3 FourthRootColor = sqrt(SqrtColor);
    const float3 EighthRootColor = sqrt(FourthRootColor);
    return saturate((0.662002687f * SqrtColor) + (0.684122060f * FourthRootColor) - (0.323583601f * EighthRootColor) - (0.0225411470f * Color));
}

float3 ApplyGammaCorrection(float3 LinearColor, float Gamma) {
    const float3 SaturatedColor = saturate(LinearColor);
    if (Gamma == 1.0f) {
        return SaturatedColor;
    }

    if (abs(Gamma - 2.2f) <= 0.001f) {
        return EncodeLinearToSrgbColor(SaturatedColor);
    }

    return pow(SaturatedColor, 1.0f / Gamma);
}

float3 ApplyToneMapping(float3 HdrColor) {
    const float Gamma = max(asfloat(RootConstants.mParameter1), 0.0001f);
    if (RootConstants.mParameter2 == 0u) {
        return ApplyGammaCorrection(HdrColor, Gamma);
    }

    const float Exposure = max(asfloat(RootConstants.mParameter0), 0.0f);
    const float3 ExposedColor = max(HdrColor * Exposure, 0.0f);
    const float3 MappedColor = (ExposedColor * ((2.51f * ExposedColor) + 0.03f)) / ((ExposedColor * ((2.43f * ExposedColor) + 0.59f)) + 0.14f);
    return ApplyGammaCorrection(MappedColor, Gamma);
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
