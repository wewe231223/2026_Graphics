#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);

struct SkyDomeVertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float4 Color : COLOR0;
};

struct SkyDomeVertexOutput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 TexCoord0 : TEXCOORD0;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

SkyDomeVertexOutput VsMain(SkyDomeVertexInput Input, uint InstanceId : SV_InstanceID)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[0];

    SkyDomeVertexOutput Output;

    float4x4 View = transpose(FrameGlobals.View);
    float4x4 Proj = transpose(FrameGlobals.Proj);

    float3x3 ViewRotation = (float3x3) View;
    float3 RotatedPosition = mul(Input.Position, ViewRotation);

    Output.Position = mul(float4(RotatedPosition, 1.0f), Proj);
    
    Output.Position.z = Output.Position.w;

    Output.Color = Input.Color;
    Output.TexCoord0 = Input.TexCoord0;
    Output.Flags = DrawRecord.Flags;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    
    return Output;
}

float4 PsMain(SkyDomeVertexOutput Input) : SV_TARGET
{
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;

    float4 SampledColor = Input.Color;

    if (DiffuseColorTextureTableIndex >= 0)
    {
        const uint TextureTableIndex = (uint) DiffuseColorTextureTableIndex;
        const uint TextureSrvIndex = MaterialTextureTableBuffer[TextureTableIndex].TextureSrvDescriptorIndex;
        
        if (TextureSrvIndex != 0xffffffffu)
        {
            Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[TextureSrvIndex];
            SampledColor = ApplyBaseColor(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
        }
    }
    
    return SampledColor;
}
