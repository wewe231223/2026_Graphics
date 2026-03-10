#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);

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
    float4 Color : COLOR0;
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
    Output.Color = Input.Color;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

float4 PsMain(PrimitiveVertexOutput Input) : SV_TARGET
{
    const float4 BaseColor = ApplyBaseColor(Input.Color);
    return ResolveFlags(BaseColor, Input.Flags);
}
