#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);

struct TerrainVertexInput {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float4 Color : COLOR0;
};

struct TerrainVertexOutput {
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float4 Color : COLOR0;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

TerrainVertexOutput VsMain(TerrainVertexInput Input, uint InstanceId : SV_InstanceID) {
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[0];

    TerrainVertexOutput Output;

    const float4x4 World = transpose(ModelContext.World);
    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    Output.Position = mul(WorldPosition, transpose(FrameGlobals.ViewProj));
    Output.Normal = normalize(mul(Input.Normal, (float3x3)World));
    Output.TexCoord0 = Input.TexCoord0;
    Output.Color = Input.Color;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

float4 PsMain(TerrainVertexOutput Input) : SV_TARGET {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];

    const float HeightFactor = saturate(Input.TexCoord0.y);
    const float3 LowColor = float3(0.15f, 0.45f, 0.10f);
    const float3 HighColor = float3(0.75f, 0.72f, 0.55f);
    float4 TerrainColor = float4(lerp(LowColor, HighColor, HeightFactor), 1.0f);
    TerrainColor *= Input.Color;

    const float4 BaseColor = ApplyBaseColor(TerrainColor);
    const float4 ScalarAppliedColor = ApplyMaterialScalarColor(BaseColor, MaterialData);
    const float4 LitColor = ApplyMaterialLighting(ScalarAppliedColor, Input.Normal);
    return ResolveFlags(LitColor, Input.Flags);
}
