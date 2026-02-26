#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);

VertexOutput VsMain(VertexInput Input, uint InstanceId : SV_InstanceID)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[0];

    VertexOutput Output;
    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), ModelContext.World);
    Output.Position = mul(WorldPosition, FrameGlobals.ViewProj);
    Output.Normal = normalize(mul(float4(Input.Normal, 0.0f), ModelContext.World).xyz);
    return Output;
}

float4 PsMain(VertexOutput Input) : SV_TARGET
{
    const float4 BaseColor = float4(abs(Input.Normal), 1.0f);
    return ApplyBaseColor(BaseColor * RootConstants.TintColor);
}
