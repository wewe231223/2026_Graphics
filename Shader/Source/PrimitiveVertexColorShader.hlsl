#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerComparisonState ShadowComparisonSampler : register(s1);

struct PrimitiveVertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float4 Color : COLOR0;
};

struct PrimitiveVertexOutput
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLD_POSITION;
    float4 Color : COLOR0;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

PrimitiveVertexOutput VsMain(PrimitiveVertexInput Input, uint InstanceId : SV_InstanceID)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[0];

    PrimitiveVertexOutput Output;

    float4x4 World = transpose(ModelContext.World);
    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    Output.Position = mul(WorldPosition, transpose(FrameGlobals.ViewProj));
    Output.Normal = normalize(mul(Input.Normal, (float3x3)World));
    Output.WorldPosition = WorldPosition.xyz;
    Output.Color = Input.Color;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

float4 PsMain(PrimitiveVertexOutput Input) : SV_TARGET
{
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const float4 BaseColor = ApplyBaseColor(Input.Color);
    const float4 ScalarAppliedColor = ApplyMaterialScalarColor(BaseColor, MaterialData);
    float4 LitColor = ApplyMaterialLighting(ScalarAppliedColor, Input.Normal);
    if (RootConstants.ShadowMappingParameterSrvIndex != 0xffffffffu && RootConstants.ShadowMapTextureSrvIndex != 0xffffffffu) {
        StructuredBuffer<ShadowMappingParameterGpu> ShadowMappingParameterBuffer = ResourceDescriptorHeap[RootConstants.ShadowMappingParameterSrvIndex];
        Texture2D<float> ShadowMapTexture = ResourceDescriptorHeap[RootConstants.ShadowMapTextureSrvIndex];
        const ShadowMappingParameterGpu ShadowMappingParameter = ShadowMappingParameterBuffer[0];
        LitColor = ApplyMaterialLightingWithShadow(ScalarAppliedColor, Input.Normal, Input.WorldPosition, ShadowMappingParameter, ShadowMapTexture, ShadowComparisonSampler);
    }

    return ResolveFlags(LitColor, Input.Flags);
}
