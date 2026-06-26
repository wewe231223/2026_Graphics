#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerComparisonState ShadowComparisonSampler : register(s1);

static const uint GBufferDisplayModeShaded = 0u;
static const uint GBufferDisplayModeAlbedo = 1u;
static const uint GBufferDisplayModeNormal = 2u;
static const uint GBufferDisplayModeWorldPosition = 3u;
static const uint GBufferDisplayModeMotionVector = 4u;
static const uint GBufferDisplayModeDepth = 5u;
static const uint GBufferDisplayMode = GBufferDisplayModeMotionVector;

struct DeferredLightingVertexOutput {
    float4 Position : SV_POSITION;
    float2 TexCoord0 : TEXCOORD0;
};

DeferredLightingVertexOutput VsMain(uint VertexId : SV_VertexID) {
    const float2 Positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f)
    };

    DeferredLightingVertexOutput Output;
    Output.Position = float4(Positions[VertexId], 0.0f, 1.0f);
    Output.TexCoord0 = (Positions[VertexId] * float2(0.5f, -0.5f)) + float2(0.5f, 0.5f);
    return Output;
}

float4 PsMain(DeferredLightingVertexOutput Input) : SV_TARGET {
    Texture2D<float4> GBufferAlbedoTexture = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    Texture2D<float4> GBufferNormalTexture = ResourceDescriptorHeap[RootConstants.BonePaletteSrvIndex];
    Texture2D<float4> GBufferWorldPositionTexture = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];
    Texture2D<float2> GBufferMotionVectorTexture = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    Texture2D<float> DepthTexture = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const int2 PixelPosition = int2(Input.Position.xy);
    const float4 Albedo = GBufferAlbedoTexture.Load(int3(PixelPosition, 0));
    const float4 NormalFlags = GBufferNormalTexture.Load(int3(PixelPosition, 0));
    const float4 WorldPosition = GBufferWorldPositionTexture.Load(int3(PixelPosition, 0));
    const float2 MotionVector = GBufferMotionVectorTexture.Load(int3(PixelPosition, 0));
    const float Depth = DepthTexture.Load(int3(PixelPosition, 0));
    if (GBufferDisplayMode == GBufferDisplayModeAlbedo) {
        return float4(Albedo.rgb, 1.0f);
    }

    if (GBufferDisplayMode == GBufferDisplayModeNormal) {
        return float4(NormalFlags.rgb, 1.0f);
    }

    if (GBufferDisplayMode == GBufferDisplayModeWorldPosition) {
        return float4(saturate(abs(WorldPosition.xyz) * 0.05f), 1.0f);
    }

    if (GBufferDisplayMode == GBufferDisplayModeMotionVector) {
        return float4(saturate((MotionVector * 0.02f) + 0.5f), 0.5f, 1.0f);
    }

    if (GBufferDisplayMode == GBufferDisplayModeDepth) {
        return float4(Depth, Depth, Depth, 1.0f);
    }

    if (WorldPosition.w <= 0.0f) {
        return float4(0.0f, 0.0f, 1.0f, 1.0f);
    }

    if (WorldPosition.w == SkyGBufferSurfaceMarker) {
        return float4(Albedo.rgb, 1.0f);
    }

    const bool IsFoliageSurface = WorldPosition.w >= FoliageGBufferSurfaceMarker;
    const float3 WorldNormal = (NormalFlags.xyz * 2.0f) - 1.0f;
    float4 LitColor;
    if (RootConstants.ShadowMappingParameterSrvIndex != 0xffffffffu) {
        StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
        StructuredBuffer<ShadowMappingParameterGpu> ShadowMappingParameterBuffer = ResourceDescriptorHeap[RootConstants.ShadowMappingParameterSrvIndex];
        const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[0];
        const ShadowMappingParameterGpu ShadowMappingParameter = ShadowMappingParameterBuffer[0];
        if (RootConstants.ShadowMapTextureBaseSrvIndex != 0xffffffffu) {
            LitColor = ApplyMaterialLightingWithShadow(Albedo, WorldNormal, WorldPosition.xyz, ShadowMappingParameter, FrameGlobals, RootConstants.ShadowMapTextureBaseSrvIndex, ShadowComparisonSampler);
        }
        else {
            LitColor = ApplyMaterialLighting(Albedo, WorldNormal, ShadowMappingParameter.DirectionalLight);
        }
    }
    else {
        LitColor = ApplyMaterialLighting(Albedo, WorldNormal);
    }

    if (IsFoliageSurface) {
        LitColor.rgb = lerp(LitColor.rgb, Albedo.rgb, FoliageBaseColorBlendFactor);
    }

    const uint Flags = NormalFlags.w > 0.5f ? 0x1u : 0u;
    return ResolveFlags(LitColor, Flags);
}
