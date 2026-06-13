#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);

struct FoliageCrossVsOutput {
    uint InstanceId : INSTANCE_ID;
};

struct FoliageCrossGsOutput {
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    float3 WorldPosition : WORLD_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
    nointerpolation uint Flags : FLAGS;
};

FoliageCrossVsOutput VsMain(uint InstanceId : SV_InstanceID) {
    FoliageCrossVsOutput Output;
    Output.InstanceId = InstanceId;
    return Output;
}

float3 RotateEnvironmentYaw(float3 Value, float YawRadians) {
    float SinValue = 0.0f;
    float CosValue = 1.0f;
    sincos(YawRadians, SinValue, CosValue);
    return float3((Value.x * CosValue) + (Value.z * SinValue), Value.y, (-Value.x * SinValue) + (Value.z * CosValue));
}

float3 BuildFoliageCrossWorldPosition(float3 LocalPosition, EnvironmentInstanceContextGpu InstanceContext, EnvironmentSegmentContextGpu SegmentContext) {
    const float4 SegmentPosition = mul(float4(LocalPosition, 1.0f), SegmentContext.LocalTransform);
    const float3 ScaledPosition = SegmentPosition.xyz * InstanceContext.PositionScale.w;
    return RotateEnvironmentYaw(ScaledPosition, InstanceContext.RotationVariation.x) + InstanceContext.PositionScale.xyz;
}

float3 BuildFoliageCrossWorldDirection(float3 LocalDirection, EnvironmentInstanceContextGpu InstanceContext, EnvironmentSegmentContextGpu SegmentContext) {
    const float3 SegmentDirection = mul(LocalDirection, (float3x3)SegmentContext.LocalTransform);
    return normalize(RotateEnvironmentYaw(SegmentDirection, InstanceContext.RotationVariation.x));
}

void AppendFoliageCrossVertex(float3 LocalPosition, float3 LocalNormal, float3 LocalTangent, float3 LocalBitangent, float2 TexCoord, FrameGlobalsGpu FrameGlobals, EnvironmentInstanceContextGpu InstanceContext, EnvironmentSegmentContextGpu SegmentContext, EnvironmentDrawRecordGpu DrawRecord, inout TriangleStream<FoliageCrossGsOutput> Stream) {
    const float3 WorldPosition = BuildFoliageCrossWorldPosition(LocalPosition, InstanceContext, SegmentContext);

    FoliageCrossGsOutput Output;
    Output.Position = mul(float4(WorldPosition, 1.0f), FrameGlobals.ViewProj);
    Output.Normal = BuildFoliageCrossWorldDirection(LocalNormal, InstanceContext, SegmentContext);
    Output.Tangent = BuildFoliageCrossWorldDirection(LocalTangent, InstanceContext, SegmentContext);
    Output.Bitangent = BuildFoliageCrossWorldDirection(LocalBitangent, InstanceContext, SegmentContext);
    Output.WorldPosition = WorldPosition;
    Output.TexCoord0 = TexCoord;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    Stream.Append(Output);
}

[maxvertexcount(8)]
void GsMain(point FoliageCrossVsOutput Input[1], inout TriangleStream<FoliageCrossGsOutput> Stream) {
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<EnvironmentInstanceContextGpu> InstanceContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<EnvironmentSegmentContextGpu> SegmentContextBuffer = ResourceDescriptorHeap[RootConstants.BonePaletteSrvIndex];
    StructuredBuffer<EnvironmentDrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const EnvironmentDrawRecordGpu DrawRecord = DrawRecordBuffer[RootConstants.DrawRecordBaseIndex];
    const EnvironmentInstanceContextGpu InstanceContext = InstanceContextBuffer[DrawRecord.InstanceOffset + Input[0].InstanceId];
    const EnvironmentSegmentContextGpu SegmentContext = SegmentContextBuffer[DrawRecord.SegmentContextIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    const float Width = max(SegmentContext.ProceduralParameters.x, 0.0001f);
    const float Height = max(SegmentContext.ProceduralParameters.y, 0.0001f);
    const float HalfWidth = Width * 0.5f;

    AppendFoliageCrossVertex(float3(-HalfWidth, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float2(0.0f, 1.0f), FrameGlobals, InstanceContext, SegmentContext, DrawRecord, Stream);
    AppendFoliageCrossVertex(float3(-HalfWidth, Height, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float2(0.0f, 0.0f), FrameGlobals, InstanceContext, SegmentContext, DrawRecord, Stream);
    AppendFoliageCrossVertex(float3(HalfWidth, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float2(0.25f, 1.0f), FrameGlobals, InstanceContext, SegmentContext, DrawRecord, Stream);
    AppendFoliageCrossVertex(float3(HalfWidth, Height, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f), float2(0.25f, 0.0f), FrameGlobals, InstanceContext, SegmentContext, DrawRecord, Stream);
    Stream.RestartStrip();

    AppendFoliageCrossVertex(float3(0.0f, 0.0f, -HalfWidth), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(0.0f, 1.0f, 0.0f), float2(0.5f, 1.0f), FrameGlobals, InstanceContext, SegmentContext, DrawRecord, Stream);
    AppendFoliageCrossVertex(float3(0.0f, Height, -HalfWidth), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(0.0f, 1.0f, 0.0f), float2(0.5f, 0.0f), FrameGlobals, InstanceContext, SegmentContext, DrawRecord, Stream);
    AppendFoliageCrossVertex(float3(0.0f, 0.0f, HalfWidth), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(0.0f, 1.0f, 0.0f), float2(0.75f, 1.0f), FrameGlobals, InstanceContext, SegmentContext, DrawRecord, Stream);
    AppendFoliageCrossVertex(float3(0.0f, Height, HalfWidth), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), float3(0.0f, 1.0f, 0.0f), float2(0.75f, 0.0f), FrameGlobals, InstanceContext, SegmentContext, DrawRecord, Stream);
}

GBufferOutput PsMain(FoliageCrossGsOutput Input) {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;
    const int64_t NormalTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_NORMAL_TEXTURE].IntValue;
    float4 BaseColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 WorldNormal = Input.Normal;

    const uint DiffuseTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, DiffuseColorTextureTableIndex);
    if (DiffuseTextureSrvIndex != 0xffffffffu) {
        Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(DiffuseTextureSrvIndex)];
        BaseColor = ApplyBaseColorToLinear(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0));
    }
    else {
        BaseColor = ResolveMaterialColorFallback(MaterialData);
    }

    BaseColor = ApplyMaterialOpacity(BaseColor, MaterialData);
    ApplyMaterialAlphaCut(BaseColor.a, MaterialData);

    const uint NormalTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, NormalTextureTableIndex);
    if (NormalTextureSrvIndex != 0xffffffffu) {
        Texture2D<float4> NormalTexture = ResourceDescriptorHeap[NonUniformResourceIndex(NormalTextureSrvIndex)];
        const float NormalScale = ResolveMaterialNormalScale(MaterialData);
        const float3 NormalTangent = DecodeNormalMapColor(NormalTexture.Sample(LinearWrapSampler, Input.TexCoord0), NormalScale);
        WorldNormal = ResolveTbnNormalMappedWorldNormal(Input.Normal, Input.Tangent, Input.Bitangent, NormalTangent);
    }

    GBufferOutput Output = BuildGBufferOutput(BaseColor, WorldNormal, Input.WorldPosition, Input.Flags);
    Output.WorldPosition.w = FoliageGBufferSurfaceMarker;
    return Output;
}
