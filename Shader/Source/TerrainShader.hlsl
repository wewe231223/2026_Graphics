#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);
SamplerComparisonState ShadowComparisonSampler : register(s1);

static const uint InvalidSrvDescriptorIndex = 0xffffffffu;

struct TerrainVertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float4 Color : COLOR0;
};

struct TerrainControlPoint
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float4 Color : COLOR0;
    uint DrawIndex : DRAW_INDEX;
};

struct TerrainPatchConstantOutput
{
    float EdgeTessFactors[4] : SV_TessFactor;
    float InsideTessFactors[2] : SV_InsideTessFactor;
};

struct TerrainVertexOutput
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLD_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    float2 LayerTexCoord : TEXCOORD1;
    float4 Color : COLOR0;
    nointerpolation uint MaterialIndex : MATERIAL_INDEX;
    nointerpolation uint Flags : FLAGS;
    nointerpolation uint DrawIndex : DRAW_INDEX;
};

struct TerrainDepthVertexOutput
{
    float4 Position : SV_POSITION;
};

TerrainControlPoint VsMain(TerrainVertexInput Input, uint InstanceId : SV_InstanceID)
{
    TerrainControlPoint Output;
    Output.Position = Input.Position;
    Output.Normal = Input.Normal;
    Output.TexCoord0 = Input.TexCoord0;
    Output.Color = Input.Color;
    Output.DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    return Output;
}

TerrainPatchContextGpu ResolveTerrainPatchContext(uint DrawIndex)
{
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];
    StructuredBuffer<TerrainPatchContextGpu> TerrainPatchContextBuffer = ResourceDescriptorHeap[RootConstants.TerrainPatchContextSrvIndex];
    return TerrainPatchContextBuffer[DrawRecordBuffer[DrawIndex].TerrainPatchContextIndex];
}

TerrainPatchConstantOutput HsPatchConstant(InputPatch<TerrainControlPoint, 4> Patch, uint PatchId : SV_PrimitiveID)
{
    TerrainPatchConstantOutput Output;
    const TerrainPatchContextGpu PatchContext = ResolveTerrainPatchContext(Patch[0].DrawIndex);
    const float4 OuterFactors = max(PatchContext.OuterTessFactors, 1.0f);
    const float2 InsideFactors = max(PatchContext.InsideTessFactors.xy, 1.0f);
    Output.EdgeTessFactors[0] = OuterFactors.x;
    Output.EdgeTessFactors[1] = OuterFactors.y;
    Output.EdgeTessFactors[2] = OuterFactors.z;
    Output.EdgeTessFactors[3] = OuterFactors.w;
    Output.InsideTessFactors[0] = InsideFactors.x;
    Output.InsideTessFactors[1] = InsideFactors.y;
    return Output;
}

[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(4)]
[patchconstantfunc("HsPatchConstant")]
TerrainControlPoint HsMain(InputPatch<TerrainControlPoint, 4> Patch, uint ControlPointId : SV_OutputControlPointID, uint PatchId : SV_PrimitiveID)
{
    return Patch[ControlPointId];
}

uint ResolveTerrainHeightFieldWidth(TerrainPatchContextGpu PatchContext)
{
    return max((uint) PatchContext.HeightFieldParameters.x, 1u);
}

uint ResolveTerrainHeightFieldHeight(TerrainPatchContextGpu PatchContext)
{
    return max((uint) PatchContext.HeightFieldParameters.y, 1u);
}

float SampleTerrainHeight01(TerrainPatchContextGpu PatchContext, float2 GridPosition)
{
    StructuredBuffer<float> HeightFieldBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(PatchContext.HeightFieldSrvDescriptorIndex)];
    const uint Width = ResolveTerrainHeightFieldWidth(PatchContext);
    const uint Height = ResolveTerrainHeightFieldHeight(PatchContext);
    const uint WidthMinusOne = Width - 1u;
    const uint HeightMinusOne = Height - 1u;
    const float2 ClampedGridPosition = clamp(GridPosition, 0.0f, float2((float) WidthMinusOne, (float) HeightMinusOne));
    const float2 FloorPosition = floor(ClampedGridPosition);
    const uint X0 = min((uint) FloorPosition.x, WidthMinusOne);
    const uint Z0 = min((uint) FloorPosition.y, HeightMinusOne);
    const uint X1 = min(X0 + 1u, WidthMinusOne);
    const uint Z1 = min(Z0 + 1u, HeightMinusOne);
    const float2 Blend = saturate(ClampedGridPosition - FloorPosition);

    const uint Z0Row = Z0 * Width;
    const uint Z1Row = Z1 * Width;
    const float Height00 = saturate(HeightFieldBuffer[Z0Row + X0]);
    const float Height10 = saturate(HeightFieldBuffer[Z0Row + X1]);
    const float Height01 = saturate(HeightFieldBuffer[Z1Row + X0]);
    const float Height11 = saturate(HeightFieldBuffer[Z1Row + X1]);
    const float HeightX0 = lerp(Height00, Height10, Blend.x);
    const float HeightX1 = lerp(Height01, Height11, Blend.x);
    return lerp(HeightX0, HeightX1, Blend.y);
}

float SampleTerrainHeight(TerrainPatchContextGpu PatchContext, float2 GridPosition)
{
    return SampleTerrainHeight01(PatchContext, GridPosition) * PatchContext.HeightFieldParameters.z;
}

float2 BuildTerrainGridPosition(TerrainPatchContextGpu PatchContext, float2 DomainUv)
{
    return PatchContext.TileGrid.xy + (DomainUv * PatchContext.TileGrid.zw);
}

float3 BuildTerrainLocalPosition(TerrainPatchContextGpu PatchContext, float2 GridPosition)
{
    const float HeightValue = SampleTerrainHeight(PatchContext, GridPosition);
    const float2 LocalXZ = GridPosition * PatchContext.TerrainParameters.xy - PatchContext.TerrainParameters.zw;
    return float3(LocalXZ.x, HeightValue, LocalXZ.y);
}

float2 BuildTerrainTexCoord(TerrainPatchContextGpu PatchContext, float2 GridPosition)
{
    const uint Width = ResolveTerrainHeightFieldWidth(PatchContext);
    const uint Height = ResolveTerrainHeightFieldHeight(PatchContext);
    const float2 InvSize = 1.0f / max(float2((float) (Width - 1u), (float) (Height - 1u)), 1.0f);
    float2 Uv = GridPosition * InvSize;
    if (PatchContext.HeightFieldParameters.w > 0.5f)
    {
        Uv.y = 1.0f - Uv.y;
    }

    return Uv;
}

float2 BuildTerrainLayerTexCoord(TerrainPatchContextGpu PatchContext, float2 GridPosition)
{
    const float2 GridUv = PatchContext.TerrainUvParameters.xy + GridPosition;
    float2 Uv = GridUv * PatchContext.TerrainParameters.xy;
    if (PatchContext.HeightFieldParameters.w > 0.5f)
    {
        Uv.y = -Uv.y;
    }

    return Uv;
}

float3 BuildTerrainLocalNormal(TerrainPatchContextGpu PatchContext, float2 GridPosition)
{
    const float HeightNegativeX = SampleTerrainHeight(PatchContext, GridPosition + float2(-1.0f, 0.0f));
    const float HeightPositiveX = SampleTerrainHeight(PatchContext, GridPosition + float2(1.0f, 0.0f));
    const float HeightNegativeZ = SampleTerrainHeight(PatchContext, GridPosition + float2(0.0f, -1.0f));
    const float HeightPositiveZ = SampleTerrainHeight(PatchContext, GridPosition + float2(0.0f, 1.0f));
    const float3 DeltaX = float3(PatchContext.TerrainParameters.x * 2.0f, HeightPositiveX - HeightNegativeX, 0.0f);
    const float3 DeltaZ = float3(0.0f, HeightPositiveZ - HeightNegativeZ, PatchContext.TerrainParameters.y * 2.0f);
    return normalize(cross(DeltaZ, DeltaX));
}

[domain("quad")]
TerrainVertexOutput DsMain(TerrainPatchConstantOutput PatchConstants, float2 DomainUv : SV_DomainLocation, const OutputPatch<TerrainControlPoint, 4> Patch)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = Patch[0].DrawIndex;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    const TerrainPatchContextGpu PatchContext = ResolveTerrainPatchContext(DrawIndex);
    const float2 GridPosition = BuildTerrainGridPosition(PatchContext, DomainUv);
    const float3 LocalPosition = BuildTerrainLocalPosition(PatchContext, GridPosition);
    const float3 LocalNormal = BuildTerrainLocalNormal(PatchContext, GridPosition);

    const float4x4 World = transpose(ModelContext.World);
    const float4x4 ViewProj = transpose(FrameGlobals.ViewProj);
    const float4 WorldPosition = mul(float4(LocalPosition, 1.0f), World);

    TerrainVertexOutput Output;
    Output.Position = mul(WorldPosition, ViewProj);
    Output.Normal = normalize(mul(LocalNormal, (float3x3) World));
    Output.WorldPosition = WorldPosition.xyz;
    Output.TexCoord0 = BuildTerrainTexCoord(PatchContext, GridPosition);
    Output.LayerTexCoord = BuildTerrainLayerTexCoord(PatchContext, GridPosition);
    Output.Color = Patch[0].Color;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    Output.DrawIndex = DrawIndex;
    return Output;
}

[domain("quad")]
TerrainDepthVertexOutput DsMainDepth(TerrainPatchConstantOutput PatchConstants, float2 DomainUv : SV_DomainLocation, const OutputPatch<TerrainControlPoint, 4> Patch)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];

    const uint DrawIndex = Patch[0].DrawIndex;
    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    const ModelContextGpu ModelContext = ModelContextBuffer[DrawRecord.ObjectIndex];
    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    const TerrainPatchContextGpu PatchContext = ResolveTerrainPatchContext(DrawIndex);
    const float2 GridPosition = BuildTerrainGridPosition(PatchContext, DomainUv);
    const float3 LocalPosition = BuildTerrainLocalPosition(PatchContext, GridPosition);
    const float4x4 WorldViewProj = mul(transpose(ModelContext.World), transpose(FrameGlobals.ViewProj));

    TerrainDepthVertexOutput Output;
    Output.Position = mul(float4(LocalPosition, 1.0f), WorldViewProj);
    return Output;
}

float2 ResolveTerrainTransformedUv(float2 BaseUv, float4 UvTransform)
{
    const float2 UvScale = (abs(UvTransform.x) + abs(UvTransform.y) <= 0.0f) ? float2(1.0f, 1.0f) : UvTransform.xy;
    return BaseUv * UvScale + UvTransform.zw;
}

uint ResolveMaterialTextureSrvIndex(StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, int64_t TextureTableIndex)
{
    if (TextureTableIndex < 0)
    {
        return InvalidSrvDescriptorIndex;
    }

    return MaterialTextureTableBuffer[(uint) TextureTableIndex].TextureSrvDescriptorIndex;
}

bool IsMaterialColorValid(float4 ColorValue)
{
    return dot(ColorValue, ColorValue) > 0.0f;
}

float4 ResolveTerrainFallbackColor(MaterialGpu MaterialData)
{
    const float4 BaseColor = MaterialData.Fields[MATERIAL_TYPE_BASE_COLOR].FloatValue;
    if (IsMaterialColorValid(BaseColor))
    {
        return ApplyBaseColorToLinear(BaseColor);
    }

    const float4 DiffuseColor = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_COLOR].FloatValue;
    if (IsMaterialColorValid(DiffuseColor))
    {
        return ApplyBaseColorToLinear(DiffuseColor);
    }

    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}

bool HasTerrainSplatMap(MaterialGpu MaterialData)
{
    [unroll]
    for (uint SplatMapIndex = 0u; SplatMapIndex < TERRAIN_MAX_SPLAT_MAP_COUNT; SplatMapIndex += 1u)
    {
        if (MaterialData.Fields[MATERIAL_TYPE_TERRAIN_SPLAT_TEXTURE0 + SplatMapIndex].IntValue >= 0)
        {
            return true;
        }
    }

    return false;
}

bool HasGeneratedTerrainSplatMap(TerrainPatchContextGpu PatchContext)
{
    return PatchContext.SplatMapSrvDescriptorIndex != InvalidSrvDescriptorIndex && PatchContext.SplatMapWidth > 0u && PatchContext.SplatMapHeight > 0u;
}

uint ResolveTerrainLayerCount(MaterialGpu MaterialData, bool HasSplatMapValue, bool HasGeneratedSplatMapValue)
{
    const int64_t LayerCountValue = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_LAYER_COUNT].IntValue;
    if (LayerCountValue > 0)
    {
        return min((uint) LayerCountValue, TERRAIN_MAX_LAYER_COUNT);
    }

    return (HasSplatMapValue || HasGeneratedSplatMapValue) ? TERRAIN_MAX_LAYER_COUNT : 1u;
}

float4 SampleGeneratedTerrainSplatMap(TerrainPatchContextGpu PatchContext, float2 BaseUv)
{
    StructuredBuffer<float4> SplatMapBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(PatchContext.SplatMapSrvDescriptorIndex)];
    const uint Width = max(PatchContext.SplatMapWidth, 1u);
    const uint Height = max(PatchContext.SplatMapHeight, 1u);
    const uint WidthMinusOne = Width - 1u;
    const uint HeightMinusOne = Height - 1u;
    const float2 SplatPosition = saturate(BaseUv) * float2((float) WidthMinusOne, (float) HeightMinusOne);
    const float2 FloorPosition = floor(SplatPosition);
    const uint X0 = min((uint) FloorPosition.x, WidthMinusOne);
    const uint Z0 = min((uint) FloorPosition.y, HeightMinusOne);
    const uint X1 = min(X0 + 1u, WidthMinusOne);
    const uint Z1 = min(Z0 + 1u, HeightMinusOne);
    const float2 Blend = saturate(SplatPosition - FloorPosition);
    const uint Z0Row = Z0 * Width;
    const uint Z1Row = Z1 * Width;
    const float4 Splat00 = SplatMapBuffer[Z0Row + X0];
    const float4 Splat10 = SplatMapBuffer[Z0Row + X1];
    const float4 Splat01 = SplatMapBuffer[Z1Row + X0];
    const float4 Splat11 = SplatMapBuffer[Z1Row + X1];
    const float4 SplatX0 = lerp(Splat00, Splat10, Blend.x);
    const float4 SplatX1 = lerp(Splat01, Splat11, Blend.x);
    return saturate(lerp(SplatX0, SplatX1, Blend.y));
}

float ResolveTerrainLayerWeight(MaterialGpu MaterialData, StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, TerrainPatchContextGpu PatchContext, float2 BaseUv, uint LayerIndex, bool HasSplatMapValue, bool HasGeneratedSplatMapValue, float4 GeneratedSplatCache)
{
    if (HasSplatMapValue == false)
    {
        if (HasGeneratedSplatMapValue == true)
        {
            if (LayerIndex >= 4u)
            {
                return 0.0f;
            }

            return saturate(GeneratedSplatCache[LayerIndex]);
        }

        return LayerIndex == 0u ? 1.0f : 0.0f;
    }

    const uint SplatMapIndex = LayerIndex / 4u;
    const uint SplatChannelIndex = LayerIndex & 3u;
    const int64_t TextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_SPLAT_TEXTURE0 + SplatMapIndex].IntValue;
    const uint TextureSrvIndex = ResolveMaterialTextureSrvIndex(MaterialTextureTableBuffer, TextureTableIndex);
    if (TextureSrvIndex == InvalidSrvDescriptorIndex)
    {
        return 0.0f;
    }

    const float4 SplatUvTransform = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_SPLAT_UV_TRANSFORM0 + SplatMapIndex].FloatValue;
    const float2 SplatUv = ResolveTerrainTransformedUv(BaseUv, SplatUvTransform);
    Texture2D<float4> SplatTexture = ResourceDescriptorHeap[NonUniformResourceIndex(TextureSrvIndex)];
    return saturate(SplatTexture.Sample(LinearWrapSampler, SplatUv)[SplatChannelIndex]);
}

float4 ResolveTerrainLayerDiffuse(MaterialGpu MaterialData, StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, float2 LayerBaseUv, uint LayerIndex, float4 FallbackColor)
{
    const float4 LayerUvTransform = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_LAYER_UV_TRANSFORM0 + LayerIndex].FloatValue;
    const float2 LayerUv = ResolveTerrainTransformedUv(LayerBaseUv, LayerUvTransform);
    const float4 LayerColorValue = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_DIFFUSE_COLOR0 + LayerIndex].FloatValue;
    const float4 LayerColor = IsMaterialColorValid(LayerColorValue) ? ApplyBaseColorToLinear(LayerColorValue) : FallbackColor;
    const int64_t TextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_DIFFUSE_TEXTURE0 + LayerIndex].IntValue;
    const uint TextureSrvIndex = ResolveMaterialTextureSrvIndex(MaterialTextureTableBuffer, TextureTableIndex);
    if (TextureSrvIndex == InvalidSrvDescriptorIndex)
    {
        return LayerColor;
    }

    Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(TextureSrvIndex)];
    return ApplyBaseColorToLinear(DiffuseTexture.Sample(LinearWrapSampler, LayerUv)) * LayerColor;
}

float3 ResolveTerrainLayerNormalTangent(MaterialGpu MaterialData, StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, float2 LayerBaseUv, uint LayerIndex)
{
    const float4 LayerUvTransform = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_LAYER_UV_TRANSFORM0 + LayerIndex].FloatValue;
    const float2 LayerUv = ResolveTerrainTransformedUv(LayerBaseUv, LayerUvTransform);
    const int64_t TextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_NORMAL_TEXTURE0 + LayerIndex].IntValue;
    const uint TextureSrvIndex = ResolveMaterialTextureSrvIndex(MaterialTextureTableBuffer, TextureTableIndex);

    float4 NormalColor;
    if (TextureSrvIndex != InvalidSrvDescriptorIndex)
    {
        Texture2D<float4> NormalTexture = ResourceDescriptorHeap[NonUniformResourceIndex(TextureSrvIndex)];
        NormalColor = NormalTexture.Sample(LinearWrapSampler, LayerUv);
    }
    else
    {
        NormalColor = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_NORMAL_COLOR0 + LayerIndex].FloatValue;
        if (IsMaterialColorValid(NormalColor) == false)
        {
            NormalColor = float4(0.5f, 0.5f, 1.0f, 1.0f);
        }
    }

    float3 NormalTangent = NormalColor.xyz * 2.0f - 1.0f;
    const float NormalScaleRaw = MaterialData.Fields[MATERIAL_TYPE_NORMAL_SCALE].FloatValue.x;
    const float NormalScale = (NormalScaleRaw <= 0.0f) ? 1.0f : NormalScaleRaw;
    NormalTangent.xy *= NormalScale;
    return normalize(NormalTangent);
}

float3 ResolveTerrainWorldNormal(float3 VertexNormal, float3 NormalTangent)
{
    const float3 Normal = normalize(VertexNormal);
    float3 Tangent = float3(1.0f, 0.0f, 0.0f) - Normal * Normal.x;
    if (dot(Tangent, Tangent) <= 0.0001f)
    {
        Tangent = float3(0.0f, 0.0f, 1.0f) - Normal * Normal.z;
    }

    Tangent = normalize(Tangent);
    const float3 Bitangent = cross(Normal, Tangent);
    return normalize(Tangent * NormalTangent.x + Bitangent * NormalTangent.y + Normal * NormalTangent.z);
}

void ResolveTerrainMaterial(MaterialGpu MaterialData, StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, TerrainPatchContextGpu PatchContext, float2 BaseUv, float2 LayerBaseUv, out float4 OutColor, out float3 OutNormalTangent)
{
    const bool HasSplatMapValue = HasTerrainSplatMap(MaterialData);
    const bool HasGeneratedSplatMapValue = HasGeneratedTerrainSplatMap(PatchContext);
    const uint LayerCount = ResolveTerrainLayerCount(MaterialData, HasSplatMapValue, HasGeneratedSplatMapValue);
    const float4 FallbackColor = ResolveTerrainFallbackColor(MaterialData);

    const float4 GeneratedSplatCache = (HasSplatMapValue == false && HasGeneratedSplatMapValue == true)
        ? SampleGeneratedTerrainSplatMap(PatchContext, BaseUv)
        : float4(0.0f, 0.0f, 0.0f, 0.0f);

    float4 AccumulatedColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 AccumulatedNormal = float3(0.0f, 0.0f, 0.0f);
    float TotalWeight = 0.0f;

    [loop]
    for (uint LayerIndex = 0u; LayerIndex < LayerCount; LayerIndex += 1u)
    {
        const float LayerWeight = ResolveTerrainLayerWeight(MaterialData, MaterialTextureTableBuffer, PatchContext, BaseUv, LayerIndex, HasSplatMapValue, HasGeneratedSplatMapValue, GeneratedSplatCache);
        if (LayerWeight <= 0.0f)
        {
            continue;
        }

        AccumulatedColor += ResolveTerrainLayerDiffuse(MaterialData, MaterialTextureTableBuffer, LayerBaseUv, LayerIndex, FallbackColor) * LayerWeight;
        AccumulatedNormal += ResolveTerrainLayerNormalTangent(MaterialData, MaterialTextureTableBuffer, LayerBaseUv, LayerIndex) * LayerWeight;
        TotalWeight += LayerWeight;
    }

    if (TotalWeight <= 0.0f)
    {
        OutColor = ResolveTerrainLayerDiffuse(MaterialData, MaterialTextureTableBuffer, LayerBaseUv, 0u, FallbackColor);
        OutNormalTangent = ResolveTerrainLayerNormalTangent(MaterialData, MaterialTextureTableBuffer, LayerBaseUv, 0u);
        return;
    }

    const float InvTotalWeight = 1.0f / TotalWeight;
    OutColor = AccumulatedColor * InvTotalWeight;
    OutNormalTangent = normalize(AccumulatedNormal * InvTotalWeight);
}

GBufferOutput PsMain(TerrainVertexOutput Input)
{
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];
    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];
    const TerrainPatchContextGpu PatchContext = ResolveTerrainPatchContext(Input.DrawIndex);

    float4 TerrainColor;
    float3 TerrainNormalTangent;
    ResolveTerrainMaterial(MaterialData, MaterialTextureTableBuffer, PatchContext, Input.TexCoord0, Input.LayerTexCoord, TerrainColor, TerrainNormalTangent);
    const float3 TerrainNormal = ResolveTerrainWorldNormal(Input.Normal, TerrainNormalTangent);
    const float4 BaseColor = ApplyBaseColor(TerrainColor);
    const float4 ScalarAppliedColor = ApplyMaterialScalarColor(BaseColor, MaterialData);
    return BuildGBufferOutput(ScalarAppliedColor, TerrainNormal, Input.WorldPosition, Input.Flags);
}
