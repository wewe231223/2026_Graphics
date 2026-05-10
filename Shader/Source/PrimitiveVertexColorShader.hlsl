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
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];

    PrimitiveVertexOutput Output;

    float4x4 World = ModelContext.World;
    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    Output.Position = mul(WorldPosition, FrameGlobals.ViewProj);
    Output.Normal = normalize(mul(Input.Normal, (float3x3)World));
    Output.WorldPosition = WorldPosition.xyz;
    Output.Color = Input.Color;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

GBufferOutput PsMain(PrimitiveVertexOutput Input) {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const float4 BaseColor = ApplyBaseColorToLinear(Input.Color);
    const float4 ScalarAppliedColor = ApplyMaterialScalarColor(BaseColor, MaterialData);
    return BuildGBufferOutput(ScalarAppliedColor, Input.Normal, Input.WorldPosition, Input.Flags);
}
