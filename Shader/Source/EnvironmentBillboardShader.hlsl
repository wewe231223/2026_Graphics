#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);
SamplerComparisonState ShadowComparisonSampler : register(s1);

static const float BillboardEpsilon = 0.0001f;
static const float BillboardAtlasInset = 0.001f;
static const float BillboardTwoPi = 6.28318530718f;
static const float BillboardHalfPi = 1.57079632679f;
static const float BillboardQuarterPi = 0.78539816339f;
static const uint BillboardImageCount = 4u;

struct EnvironmentBillboardVertexInput
{
    float3 Position : POSITION;
};

struct EnvironmentBillboardVertexOutput
{
    float3 WorldCenter : WORLD_CENTER;
    float Width : WIDTH;
    float Height : HEIGHT;
    float InstanceYaw : INSTANCE_YAW;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

struct EnvironmentBillboardPixelInput
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
    float3 WorldPosition : WORLD_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
    nointerpolation uint Flags : FLAGS;
};

struct EnvironmentBillboardDepthPixelInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
};

float3 RotateEnvironmentYaw(float3 Value, float YawRadians) {
    float SinValue = 0.0f;
    float CosValue = 1.0f;
    sincos(YawRadians, SinValue, CosValue);
    return float3((Value.x * CosValue) + (Value.z * SinValue), Value.y, (-Value.x * SinValue) + (Value.z * CosValue));
}

float3 BuildEnvironmentWorldPosition(float3 LocalPosition, EnvironmentInstanceContextGpu InstanceContext, EnvironmentSegmentContextGpu SegmentContext) {
    const float4 SegmentPosition = mul(float4(LocalPosition, 1.0f), SegmentContext.LocalTransform);
    const float3 ScaledPosition = SegmentPosition.xyz * InstanceContext.PositionScale.w;
    return RotateEnvironmentYaw(ScaledPosition, InstanceContext.RotationVariation.x) + InstanceContext.PositionScale.xyz;
}

float3 BuildEnvironmentWorldVector(float3 LocalVector, EnvironmentInstanceContextGpu InstanceContext, EnvironmentSegmentContextGpu SegmentContext) {
    const float3 SegmentVector = mul(LocalVector, (float3x3)SegmentContext.LocalTransform) * InstanceContext.PositionScale.w;
    return RotateEnvironmentYaw(SegmentVector, InstanceContext.RotationVariation.x);
}

EnvironmentBillboardVertexOutput VsMain(EnvironmentBillboardVertexInput Input, uint InstanceId : SV_InstanceID) {
    StructuredBuffer<EnvironmentInstanceContextGpu> InstanceContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<EnvironmentSegmentContextGpu> SegmentContextBuffer = ResourceDescriptorHeap[RootConstants.BonePaletteSrvIndex];
    StructuredBuffer<EnvironmentDrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const EnvironmentDrawRecordGpu DrawRecord = DrawRecordBuffer[RootConstants.DrawRecordBaseIndex];
    const EnvironmentInstanceContextGpu InstanceContext = InstanceContextBuffer[DrawRecord.InstanceOffset + InstanceId];
    const EnvironmentSegmentContextGpu SegmentContext = SegmentContextBuffer[DrawRecord.SegmentContextIndex];
    const float3 WorldWidthVector = BuildEnvironmentWorldVector(float3(1.0f, 0.0f, 0.0f), InstanceContext, SegmentContext);
    const float3 WorldHeightVector = BuildEnvironmentWorldVector(float3(0.0f, 1.0f, 0.0f), InstanceContext, SegmentContext);

    EnvironmentBillboardVertexOutput Output;
    Output.WorldCenter = BuildEnvironmentWorldPosition(Input.Position, InstanceContext, SegmentContext);
    Output.Width = max(length(WorldWidthVector), BillboardEpsilon);
    Output.Height = max(length(WorldHeightVector), BillboardEpsilon);
    Output.InstanceYaw = InstanceContext.RotationVariation.x;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

uint ResolveBillboardImageIndex(float3 WorldCenter, float3 CameraPosition, float InstanceYaw) {
    float3 CameraDirection = float3(CameraPosition.x - WorldCenter.x, 0.0f, CameraPosition.z - WorldCenter.z);
    const float DirectionLengthSquared = dot(CameraDirection, CameraDirection);
    if (DirectionLengthSquared <= BillboardEpsilon) {
        CameraDirection = float3(0.0f, 0.0f, 1.0f);
    }
    else {
        CameraDirection *= rsqrt(DirectionLengthSquared);
    }

    float Angle = atan2(CameraDirection.x, CameraDirection.z) - InstanceYaw + BillboardQuarterPi;
    Angle = Angle - (floor(Angle / BillboardTwoPi) * BillboardTwoPi);
    return min((uint)floor(Angle / BillboardHalfPi), BillboardImageCount - 1u);
}

void ResolveBillboardAxes(float3 WorldCenter, float3 CameraPosition, out float3 Normal, out float3 Tangent, out float3 Bitangent) {
    Normal = float3(CameraPosition.x - WorldCenter.x, 0.0f, CameraPosition.z - WorldCenter.z);
    const float NormalLengthSquared = dot(Normal, Normal);
    if (NormalLengthSquared <= BillboardEpsilon) {
        Normal = float3(0.0f, 0.0f, 1.0f);
    }
    else {
        Normal *= rsqrt(NormalLengthSquared);
    }

    Bitangent = float3(0.0f, 1.0f, 0.0f);
    Tangent = normalize(cross(Bitangent, Normal));
}

EnvironmentBillboardPixelInput BuildBillboardPixelInput(float3 WorldPosition, float2 TexCoord, float3 Normal, float3 Tangent, float3 Bitangent, uint MaterialIndex, uint Flags, FrameGlobalsGpu FrameGlobals) {
    EnvironmentBillboardPixelInput Output;
    Output.Position = mul(float4(WorldPosition, 1.0f), FrameGlobals.ViewProj);
    Output.Normal = Normal;
    Output.Tangent = Tangent;
    Output.Bitangent = Bitangent;
    Output.WorldPosition = WorldPosition;
    Output.TexCoord0 = TexCoord;
    Output.MaterialIndex = MaterialIndex;
    Output.Flags = Flags;
    return Output;
}

EnvironmentBillboardDepthPixelInput BuildBillboardDepthPixelInput(float3 WorldPosition, float2 TexCoord, uint MaterialIndex, FrameGlobalsGpu FrameGlobals) {
    EnvironmentBillboardDepthPixelInput Output;
    Output.Position = mul(float4(WorldPosition, 1.0f), FrameGlobals.ViewProj);
    Output.TexCoord0 = TexCoord;
    Output.MaterialIndex = MaterialIndex;
    return Output;
}

float3 ResolveCameraPosition(FrameGlobalsGpu FrameGlobals) {
    const float3 ViewTranslation = FrameGlobals.View[3].xyz;
    return -float3(dot(ViewTranslation, FrameGlobals.View[0].xyz), dot(ViewTranslation, FrameGlobals.View[1].xyz), dot(ViewTranslation, FrameGlobals.View[2].xyz));
}

[maxvertexcount(4)]
void GsMain(point EnvironmentBillboardVertexOutput Input[1], inout TriangleStream<EnvironmentBillboardPixelInput> OutputStream) {
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    const float3 WorldCenter = Input[0].WorldCenter;
    const float3 CameraPosition = ResolveCameraPosition(FrameGlobals);
    float3 Normal = float3(0.0f, 0.0f, 1.0f);
    float3 Tangent = float3(1.0f, 0.0f, 0.0f);
    float3 Bitangent = float3(0.0f, 1.0f, 0.0f);
    ResolveBillboardAxes(WorldCenter, CameraPosition, Normal, Tangent, Bitangent);

    const float HalfWidth = Input[0].Width * 0.5f;
    const float HalfHeight = Input[0].Height * 0.5f;
    const uint ImageIndex = ResolveBillboardImageIndex(WorldCenter, CameraPosition, Input[0].InstanceYaw);
    const float UMin = ((float)ImageIndex / (float)BillboardImageCount) + BillboardAtlasInset;
    const float UMax = ((float)(ImageIndex + 1u) / (float)BillboardImageCount) - BillboardAtlasInset;
    const float3 BottomLeft = WorldCenter - (Tangent * HalfWidth) - (Bitangent * HalfHeight);
    const float3 TopLeft = WorldCenter - (Tangent * HalfWidth) + (Bitangent * HalfHeight);
    const float3 BottomRight = WorldCenter + (Tangent * HalfWidth) - (Bitangent * HalfHeight);
    const float3 TopRight = WorldCenter + (Tangent * HalfWidth) + (Bitangent * HalfHeight);

    OutputStream.Append(BuildBillboardPixelInput(BottomLeft, float2(UMax, 1.0f), Normal, Tangent, Bitangent, Input[0].MaterialIndex, Input[0].Flags, FrameGlobals));
    OutputStream.Append(BuildBillboardPixelInput(TopLeft, float2(UMax, 0.0f), Normal, Tangent, Bitangent, Input[0].MaterialIndex, Input[0].Flags, FrameGlobals));
    OutputStream.Append(BuildBillboardPixelInput(BottomRight, float2(UMin, 1.0f), Normal, Tangent, Bitangent, Input[0].MaterialIndex, Input[0].Flags, FrameGlobals));
    OutputStream.Append(BuildBillboardPixelInput(TopRight, float2(UMin, 0.0f), Normal, Tangent, Bitangent, Input[0].MaterialIndex, Input[0].Flags, FrameGlobals));
}

[maxvertexcount(4)]
void GsMainDepth(point EnvironmentBillboardVertexOutput Input[1], inout TriangleStream<EnvironmentBillboardDepthPixelInput> OutputStream) {
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    const float3 WorldCenter = Input[0].WorldCenter;
    const float3 CameraPosition = ResolveCameraPosition(FrameGlobals);
    float3 Normal = float3(0.0f, 0.0f, 1.0f);
    float3 Tangent = float3(1.0f, 0.0f, 0.0f);
    float3 Bitangent = float3(0.0f, 1.0f, 0.0f);
    ResolveBillboardAxes(WorldCenter, CameraPosition, Normal, Tangent, Bitangent);

    const float HalfWidth = Input[0].Width * 0.5f;
    const float HalfHeight = Input[0].Height * 0.5f;
    const uint ImageIndex = ResolveBillboardImageIndex(WorldCenter, CameraPosition, Input[0].InstanceYaw);
    const float UMin = ((float)ImageIndex / (float)BillboardImageCount) + BillboardAtlasInset;
    const float UMax = ((float)(ImageIndex + 1u) / (float)BillboardImageCount) - BillboardAtlasInset;
    const float3 BottomLeft = WorldCenter - (Tangent * HalfWidth) - (Bitangent * HalfHeight);
    const float3 TopLeft = WorldCenter - (Tangent * HalfWidth) + (Bitangent * HalfHeight);
    const float3 BottomRight = WorldCenter + (Tangent * HalfWidth) - (Bitangent * HalfHeight);
    const float3 TopRight = WorldCenter + (Tangent * HalfWidth) + (Bitangent * HalfHeight);

    OutputStream.Append(BuildBillboardDepthPixelInput(BottomLeft, float2(UMax, 1.0f), Input[0].MaterialIndex, FrameGlobals));
    OutputStream.Append(BuildBillboardDepthPixelInput(TopLeft, float2(UMax, 0.0f), Input[0].MaterialIndex, FrameGlobals));
    OutputStream.Append(BuildBillboardDepthPixelInput(BottomRight, float2(UMin, 1.0f), Input[0].MaterialIndex, FrameGlobals));
    OutputStream.Append(BuildBillboardDepthPixelInput(TopRight, float2(UMin, 0.0f), Input[0].MaterialIndex, FrameGlobals));
}

GBufferOutput PsMain(EnvironmentBillboardPixelInput Input) {
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

void PsMainDepth(EnvironmentBillboardDepthPixelInput Input) {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];

    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const int64_t DiffuseColorTextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_TEXTURE].IntValue;
    const uint DiffuseTextureSrvIndex = ResolveMaterialTextureSrvDescriptorIndex(MaterialTextureTableBuffer, DiffuseColorTextureTableIndex);
    float Alpha = 0.0f;
    if (DiffuseTextureSrvIndex != 0xffffffffu) {
        Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(DiffuseTextureSrvIndex)];
        Alpha = ApplyMaterialOpacity(DiffuseTexture.Sample(LinearWrapSampler, Input.TexCoord0), MaterialData).a;
    }
    else {
        Alpha = ApplyMaterialOpacity(ResolveMaterialColorFallback(MaterialData), MaterialData).a;
    }

    ApplyMaterialAlphaCut(Alpha, MaterialData);
}
