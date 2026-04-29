#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);
SamplerComparisonState ShadowComparisonSampler : register(s1);

VertexOutput VsMain(VertexInput Input, uint InstanceId : SV_InstanceID)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];

    VertexOutput Output;

    float4x4 World = transpose(ModelContext.World);
    const float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);
    Output.Position = mul(WorldPosition, transpose(FrameGlobals.ViewProj));
    Output.Normal = normalize(mul(Input.Normal, (float3x3)World));
    Output.WorldPosition = WorldPosition.xyz;
    Output.TexCoord0 = Input.TexCoord0;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

GBufferOutput PsMain(VertexOutput Input) {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;

    if (DiffuseColorTextureTableIndex < 0) {
        return BuildGBufferOutput(float4(1.0f, 0.0f, 1.0f, 1.0f), Input.Normal, Input.WorldPosition, Input.Flags);
    }

    const uint TextureTableIndex = (uint)DiffuseColorTextureTableIndex;
    const uint TextureSrvIndex = MaterialTextureTableBuffer[TextureTableIndex].TextureSrvDescriptorIndex;
    if (TextureSrvIndex == 0xffffffffu) {
        return BuildGBufferOutput(float4(1.0f, 0.0f, 1.0f, 1.0f), Input.Normal, Input.WorldPosition, Input.Flags);
    }
    
    Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[TextureSrvIndex];
    const float4 SampledColor = ApplyBaseColorToLinear(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
    const float4 ScalarAppliedColor = ApplyMaterialScalarColor(SampledColor, MaterialData);
    return BuildGBufferOutput(ScalarAppliedColor, Input.Normal, Input.WorldPosition, Input.Flags);
}
