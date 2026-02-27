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
    
    float4x4 world = transpose(ModelContext.World);
    
    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), world);
    Output.Position = mul(WorldPosition, transpose(FrameGlobals.ViewProj));
    Output.Normal = normalize(mul(Input.Normal, (float3x3) world));
    return Output;
}

float4 PsMain(VertexOutput Input) : SV_TARGET
{
    float3 normal = normalize(Input.Normal);

    float3 normalColor = Input.Normal * 0.5f + 0.5f;

    return float4(normalColor, 1.0f);
}
