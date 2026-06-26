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
    float4 ClipPosition : CLIP_POSITION;
    float4 PreviousClipPosition : PREVIOUS_CLIP_POSITION;
    nointerpolation float2 RenderTargetSize : RENDER_TARGET_SIZE;
    float4 Color : COLOR0;
    float2 TexCoord0 : TEXCOORD0;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

SkyDomeVertexOutput VsMain(SkyDomeVertexInput Input, uint InstanceId : SV_InstanceID)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];

    SkyDomeVertexOutput Output;

    const float4x4 View = FrameGlobals.View;
    const float4x4 Proj = FrameGlobals.Proj;
    const float4x4 PreviousView = FrameGlobals.PrevView;
    const float4x4 PreviousProj = FrameGlobals.PrevProj;

    const float3x3 ViewRotation = (float3x3) View;
    const float3x3 PreviousViewRotation = (float3x3) PreviousView;
    const float3 RotatedPosition = mul(Input.Position, ViewRotation);
    const float3 PreviousRotatedPosition = mul(Input.Position, PreviousViewRotation);

    Output.Position = mul(float4(RotatedPosition, 1.0f), Proj);
    Output.Position.z = Output.Position.w;
    Output.ClipPosition = Output.Position;
    Output.PreviousClipPosition = mul(float4(PreviousRotatedPosition, 1.0f), PreviousProj);
    Output.RenderTargetSize = FrameGlobals.RenderTargetSize.xy;

    Output.Color = Input.Color;
    Output.TexCoord0 = Input.TexCoord0;
    Output.Flags = DrawRecord.Flags;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    
    return Output;
}

GBufferOutput PsMain(SkyDomeVertexOutput Input)
{
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;
    const uint DiffuseTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, DiffuseColorTextureTableIndex);

    float4 BaseColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (DiffuseTextureSrvIndex != 0xffffffffu)
    {
        Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(DiffuseTextureSrvIndex)];
        BaseColor = ApplyBaseColorToLinear(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
    }
    else
    {
        BaseColor = ResolveMaterialColorFallbackWithDefault(MaterialData, Input.Color);
    }
    
    GBufferOutput Output = BuildGBufferOutput(ApplyMaterialOpacity(BaseColor, MaterialData), float3(0.0f, 1.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), Input.Flags, Input.ClipPosition, Input.PreviousClipPosition, Input.RenderTargetSize);
    Output.WorldPosition.w = SkyGBufferSurfaceMarker;
    return Output;
}
