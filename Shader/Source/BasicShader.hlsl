#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);

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

    float4x4 World = transpose(ModelContext.World);

    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    Output.Position = mul(WorldPosition, transpose(FrameGlobals.ViewProj));
    Output.Normal = normalize(mul(Input.Normal, (float3x3)World));
    Output.TexCoord0 = Input.TexCoord0;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    return Output;
}

float4 PsMain(VertexOutput Input) : SV_TARGET
{
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_COLOR_MAP].IntValue;

    if (DiffuseColorTextureTableIndex < 0) {
        return float4(0.1f, 0.1f, 0.1f, 1.0f);
    }

    const uint TextureTableIndex = (uint)DiffuseColorTextureTableIndex;
    const uint TextureSrvIndex = MaterialTextureTableBuffer[TextureTableIndex].TextureSrvDescriptorIndex;
    if (TextureSrvIndex == 0xffffffffu) {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }
    
    Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[TextureSrvIndex];
    return ApplyBaseColor(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
}
