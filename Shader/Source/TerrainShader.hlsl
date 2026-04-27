#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);
SamplerState LinearWrapSampler : register(s0);
SamplerComparisonState ShadowComparisonSampler : register(s1);

static const uint InvalidSrvDescriptorIndex = 0xffffffffu;

struct TerrainVertexInput {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float4 Color : COLOR0;
};

struct TerrainControlPoint {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord0 : TEXCOORD0;
    float4 Color : COLOR0;
    uint DrawIndex : DRAW_INDEX;
};

struct TerrainPatchConstantOutput {
    float EdgeTessFactors[4] : SV_TessFactor;
    float InsideTessFactors[2] : SV_InsideTessFactor;
};

struct TerrainVertexOutput {
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
    float3 WorldPosition : WORLD_POSITION;
    float2 TexCoord0 : TEXCOORD0;
    float4 Color : COLOR0;
    uint MaterialIndex : MATERIAL_INDEX;
    uint Flags : FLAGS;
};

struct TerrainDepthVertexOutput {
    float4 Position : SV_POSITION;
};

TerrainControlPoint VsMain(TerrainVertexInput Input, uint InstanceId : SV_InstanceID) {
    TerrainControlPoint Output;
    Output.Position = Input.Position;
    Output.Normal = Input.Normal;
    Output.TexCoord0 = Input.TexCoord0;
    Output.Color = Input.Color;
    Output.DrawIndex = RootConstants.DrawRecordBaseIndex + InstanceId;
    return Output;
}

TerrainPatchContextGpu ResolveTerrainPatchContext(uint DrawIndex) {
    StructuredBuffer<DrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.DrawRecordSrvIndex];
    StructuredBuffer<TerrainPatchContextGpu> TerrainPatchContextBuffer = ResourceDescriptorHeap[RootConstants.TerrainPatchContextSrvIndex];

    const DrawRecordGpu DrawRecord = DrawRecordBuffer[DrawIndex];
    return TerrainPatchContextBuffer[DrawRecord.TerrainPatchContextIndex];
}

TerrainPatchConstantOutput HsPatchConstant(InputPatch<TerrainControlPoint, 4> Patch, uint PatchId : SV_PrimitiveID) {
    TerrainPatchConstantOutput Output;
    const TerrainPatchContextGpu PatchContext = ResolveTerrainPatchContext(Patch[0].DrawIndex);
    Output.EdgeTessFactors[0] = max(PatchContext.OuterTessFactors.x, 1.0f);
    Output.EdgeTessFactors[1] = max(PatchContext.OuterTessFactors.y, 1.0f);
    Output.EdgeTessFactors[2] = max(PatchContext.OuterTessFactors.z, 1.0f);
    Output.EdgeTessFactors[3] = max(PatchContext.OuterTessFactors.w, 1.0f);
    Output.InsideTessFactors[0] = max(PatchContext.InsideTessFactors.x, 1.0f);
    Output.InsideTessFactors[1] = max(PatchContext.InsideTessFactors.y, 1.0f);
    return Output;
}

[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(4)]
[patchconstantfunc("HsPatchConstant")]
TerrainControlPoint HsMain(InputPatch<TerrainControlPoint, 4> Patch, uint ControlPointId : SV_OutputControlPointID, uint PatchId : SV_PrimitiveID) {
    return Patch[ControlPointId];
}

uint ResolveTerrainHeightFieldWidth(TerrainPatchContextGpu PatchContext) {
    return max((uint)PatchContext.HeightFieldParameters.x, 1u);
}

uint ResolveTerrainHeightFieldHeight(TerrainPatchContextGpu PatchContext) {
    return max((uint)PatchContext.HeightFieldParameters.y, 1u);
}

float SampleTerrainHeight01(TerrainPatchContextGpu PatchContext, float2 GridPosition) {
    StructuredBuffer<float> HeightFieldBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(PatchContext.HeightFieldSrvDescriptorIndex)];
    const uint Width = ResolveTerrainHeightFieldWidth(PatchContext);
    const uint Height = ResolveTerrainHeightFieldHeight(PatchContext);
    const float MaxGridX = (float)(Width - 1u);
    const float MaxGridZ = (float)(Height - 1u);
    const float2 ClampedGridPosition = clamp(GridPosition, float2(0.0f, 0.0f), float2(MaxGridX, MaxGridZ));
    const uint X0 = min((uint)floor(ClampedGridPosition.x), Width - 1u);
    const uint Z0 = min((uint)floor(ClampedGridPosition.y), Height - 1u);
    const uint X1 = min(X0 + 1u, Width - 1u);
    const uint Z1 = min(Z0 + 1u, Height - 1u);
    const float2 Blend = saturate(ClampedGridPosition - float2((float)X0, (float)Z0));

    const float Height00 = saturate(HeightFieldBuffer[(Z0 * Width) + X0]);
    const float Height10 = saturate(HeightFieldBuffer[(Z0 * Width) + X1]);
    const float Height01 = saturate(HeightFieldBuffer[(Z1 * Width) + X0]);
    const float Height11 = saturate(HeightFieldBuffer[(Z1 * Width) + X1]);
    const float HeightX0 = lerp(Height00, Height10, Blend.x);
    const float HeightX1 = lerp(Height01, Height11, Blend.x);
    return lerp(HeightX0, HeightX1, Blend.y);
}

float SampleTerrainHeight(TerrainPatchContextGpu PatchContext, float2 GridPosition) {
    return SampleTerrainHeight01(PatchContext, GridPosition) * PatchContext.HeightFieldParameters.z;
}

float2 BuildTerrainGridPosition(TerrainPatchContextGpu PatchContext, float2 DomainUv) {
    return PatchContext.TileGrid.xy + (DomainUv * PatchContext.TileGrid.zw);
}

float3 BuildTerrainLocalPosition(TerrainPatchContextGpu PatchContext, float2 GridPosition) {
    const float HeightValue = SampleTerrainHeight(PatchContext, GridPosition);
    const float LocalX = (GridPosition.x * PatchContext.TerrainParameters.x) - PatchContext.TerrainParameters.z;
    const float LocalZ = (GridPosition.y * PatchContext.TerrainParameters.y) - PatchContext.TerrainParameters.w;
    return float3(LocalX, HeightValue, LocalZ);
}

float2 BuildTerrainTexCoord(TerrainPatchContextGpu PatchContext, float2 GridPosition) {
    const uint Width = ResolveTerrainHeightFieldWidth(PatchContext);
    const uint Height = ResolveTerrainHeightFieldHeight(PatchContext);
    const float U = GridPosition.x / max((float)(Width - 1u), 1.0f);
    float V = GridPosition.y / max((float)(Height - 1u), 1.0f);
    if (PatchContext.HeightFieldParameters.w > 0.5f) {
        V = 1.0f - V;
    }

    return float2(U, V);
}

float3 BuildTerrainLocalNormal(TerrainPatchContextGpu PatchContext, float2 GridPosition) {
    const float HeightNegativeX = SampleTerrainHeight(PatchContext, GridPosition + float2(-1.0f, 0.0f));
    const float HeightPositiveX = SampleTerrainHeight(PatchContext, GridPosition + float2(1.0f, 0.0f));
    const float HeightNegativeZ = SampleTerrainHeight(PatchContext, GridPosition + float2(0.0f, -1.0f));
    const float HeightPositiveZ = SampleTerrainHeight(PatchContext, GridPosition + float2(0.0f, 1.0f));
    const float3 DeltaX = float3(PatchContext.TerrainParameters.x * 2.0f, HeightPositiveX - HeightNegativeX, 0.0f);
    const float3 DeltaZ = float3(0.0f, HeightPositiveZ - HeightNegativeZ, PatchContext.TerrainParameters.y * 2.0f);
    return normalize(cross(DeltaZ, DeltaX));
}

[domain("quad")]
TerrainVertexOutput DsMain(TerrainPatchConstantOutput PatchConstants, float2 DomainUv : SV_DomainLocation, const OutputPatch<TerrainControlPoint, 4> Patch) {
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

    TerrainVertexOutput Output;
    const float4x4 World = transpose(ModelContext.World);
    const float4 WorldPosition = mul(float4(LocalPosition, 1.0f), World);
    Output.Position = mul(WorldPosition, transpose(FrameGlobals.ViewProj));
    Output.Normal = normalize(mul(LocalNormal, (float3x3)World));
    Output.WorldPosition = WorldPosition.xyz;
    Output.TexCoord0 = BuildTerrainTexCoord(PatchContext, GridPosition);
    Output.Color = Patch[0].Color;
    Output.MaterialIndex = DrawRecord.MaterialIndex;
    Output.Flags = DrawRecord.Flags;
    return Output;
}

[domain("quad")]
TerrainDepthVertexOutput DsMainDepth(TerrainPatchConstantOutput PatchConstants, float2 DomainUv : SV_DomainLocation, const OutputPatch<TerrainControlPoint, 4> Patch) {
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
    const float4x4 World = transpose(ModelContext.World);
    const float4 WorldPosition = mul(float4(LocalPosition, 1.0f), World);

    TerrainDepthVertexOutput Output;
    Output.Position = mul(WorldPosition, transpose(FrameGlobals.ViewProj));
    return Output;
}

float2 ResolveTerrainTransformedUv(float2 BaseUv, float4 UvTransform) {
    float2 UvScale = UvTransform.xy;
    if (abs(UvScale.x) + abs(UvScale.y) <= 0.0f) {
        UvScale = float2(1.0f, 1.0f);
    }

    return BaseUv * UvScale + UvTransform.zw;
}

uint ResolveMaterialTextureSrvIndex(StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, int64_t TextureTableIndex) {
    if (TextureTableIndex < 0) {
        return InvalidSrvDescriptorIndex;
    }

    const uint TextureTableIndexValue = (uint)TextureTableIndex;
    return MaterialTextureTableBuffer[TextureTableIndexValue].TextureSrvDescriptorIndex;
}

bool IsMaterialColorValid(float4 ColorValue) {
    return dot(ColorValue, ColorValue) > 0.0f;
}

float4 ResolveTerrainFallbackColor(MaterialGpu MaterialData) {
    const float4 BaseColor = MaterialData.Fields[MATERIAL_TYPE_BASE_COLOR].FloatValue;
    if (IsMaterialColorValid(BaseColor)) {
        return BaseColor;
    }

    const float4 DiffuseColor = MaterialData.Fields[MATERIAL_TYPE_DIFFUSE_COLOR].FloatValue;
    if (IsMaterialColorValid(DiffuseColor)) {
        return DiffuseColor;
    }

    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}

bool HasTerrainSplatMap(MaterialGpu MaterialData) {
    [unroll]
    for (uint SplatMapIndex = 0u; SplatMapIndex < TERRAIN_MAX_SPLAT_MAP_COUNT; SplatMapIndex += 1u) {
        if (MaterialData.Fields[MATERIAL_TYPE_TERRAIN_SPLAT_TEXTURE0 + SplatMapIndex].IntValue >= 0) {
            return true;
        }
    }

    return false;
}

uint ResolveTerrainLayerCount(MaterialGpu MaterialData, bool HasSplatMapValue) {
    const int64_t LayerCountValue = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_LAYER_COUNT].IntValue;
    if (LayerCountValue > 0) {
        return min((uint)LayerCountValue, TERRAIN_MAX_LAYER_COUNT);
    }

    return HasSplatMapValue ? TERRAIN_MAX_LAYER_COUNT : 1u;
}

float ResolveTerrainLayerWeight(MaterialGpu MaterialData, StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, float2 BaseUv, uint LayerIndex, bool HasSplatMapValue) {
    if (HasSplatMapValue == false) {
        return LayerIndex == 0u ? 1.0f : 0.0f;
    }

    const uint SplatMapIndex = LayerIndex / 4u;
    const uint SplatChannelIndex = LayerIndex % 4u;
    const int64_t TextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_SPLAT_TEXTURE0 + SplatMapIndex].IntValue;
    const uint TextureSrvIndex = ResolveMaterialTextureSrvIndex(MaterialTextureTableBuffer, TextureTableIndex);
    if (TextureSrvIndex == InvalidSrvDescriptorIndex) {
        return 0.0f;
    }

    const float4 SplatUvTransform = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_SPLAT_UV_TRANSFORM0 + SplatMapIndex].FloatValue;
    const float2 SplatUv = ResolveTerrainTransformedUv(BaseUv, SplatUvTransform);
    Texture2D<float4> SplatTexture = ResourceDescriptorHeap[NonUniformResourceIndex(TextureSrvIndex)];
    const float4 SplatValue = SplatTexture.Sample(LinearWrapSampler, SplatUv);
    return saturate(SplatValue[SplatChannelIndex]);
}

float4 ResolveTerrainLayerDiffuse(MaterialGpu MaterialData, StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, float2 BaseUv, uint LayerIndex, float4 FallbackColor) {
    const float4 LayerUvTransform = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_LAYER_UV_TRANSFORM0 + LayerIndex].FloatValue;
    const float2 LayerUv = ResolveTerrainTransformedUv(BaseUv, LayerUvTransform);
    const float4 LayerColorValue = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_DIFFUSE_COLOR0 + LayerIndex].FloatValue;
    const float4 LayerColor = IsMaterialColorValid(LayerColorValue) ? LayerColorValue : FallbackColor;
    const int64_t TextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_DIFFUSE_TEXTURE0 + LayerIndex].IntValue;
    const uint TextureSrvIndex = ResolveMaterialTextureSrvIndex(MaterialTextureTableBuffer, TextureTableIndex);
    if (TextureSrvIndex == InvalidSrvDescriptorIndex) {
        return LayerColor;
    }

    Texture2D<float4> DiffuseTexture = ResourceDescriptorHeap[NonUniformResourceIndex(TextureSrvIndex)];
    return ApplyBaseColor(DiffuseTexture.Sample(LinearWrapSampler, LayerUv)) * LayerColor;
}

float3 ResolveTerrainLayerNormalTangent(MaterialGpu MaterialData, StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, float2 BaseUv, uint LayerIndex) {
    const float4 LayerUvTransform = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_LAYER_UV_TRANSFORM0 + LayerIndex].FloatValue;
    const float2 LayerUv = ResolveTerrainTransformedUv(BaseUv, LayerUvTransform);
    float4 NormalColor = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_NORMAL_COLOR0 + LayerIndex].FloatValue;
    if (IsMaterialColorValid(NormalColor) == false) {
        NormalColor = float4(0.5f, 0.5f, 1.0f, 1.0f);
    }

    const int64_t TextureTableIndex = MaterialData.Fields[MATERIAL_TYPE_TERRAIN_NORMAL_TEXTURE0 + LayerIndex].IntValue;
    const uint TextureSrvIndex = ResolveMaterialTextureSrvIndex(MaterialTextureTableBuffer, TextureTableIndex);
    if (TextureSrvIndex != InvalidSrvDescriptorIndex) {
        Texture2D<float4> NormalTexture = ResourceDescriptorHeap[NonUniformResourceIndex(TextureSrvIndex)];
        NormalColor = NormalTexture.Sample(LinearWrapSampler, LayerUv);
    }

    float3 NormalTangent = NormalColor.xyz * 2.0f - 1.0f;
    float NormalScale = MaterialData.Fields[MATERIAL_TYPE_NORMAL_SCALE].FloatValue.x;
    if (NormalScale <= 0.0f) {
        NormalScale = 1.0f;
    }

    NormalTangent.xy *= NormalScale;
    return normalize(NormalTangent);
}

float3 ResolveTerrainWorldNormal(float3 VertexNormal, float3 NormalTangent) {
    const float3 Normal = normalize(VertexNormal);
    float3 Tangent = float3(1.0f, 0.0f, 0.0f) - Normal * dot(float3(1.0f, 0.0f, 0.0f), Normal);
    if (dot(Tangent, Tangent) <= 0.0001f) {
        Tangent = float3(0.0f, 0.0f, 1.0f) - Normal * dot(float3(0.0f, 0.0f, 1.0f), Normal);
    }

    Tangent = normalize(Tangent);
    const float3 Bitangent = normalize(cross(Normal, Tangent));
    return normalize((Tangent * NormalTangent.x) + (Bitangent * NormalTangent.y) + (Normal * NormalTangent.z));
}

void ResolveTerrainMaterial(MaterialGpu MaterialData, StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer, float2 BaseUv, out float4 OutColor, out float3 OutNormalTangent) {
    const bool HasSplatMapValue = HasTerrainSplatMap(MaterialData);
    const uint LayerCount = ResolveTerrainLayerCount(MaterialData, HasSplatMapValue);
    const float4 FallbackColor = ResolveTerrainFallbackColor(MaterialData);

    float4 AccumulatedColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 AccumulatedNormal = float3(0.0f, 0.0f, 0.0f);
    float TotalWeight = 0.0f;

    [loop]
    for (uint LayerIndex = 0u; LayerIndex < TERRAIN_MAX_LAYER_COUNT; LayerIndex += 1u) {
        if (LayerIndex >= LayerCount) {
            break;
        }

        const float LayerWeight = ResolveTerrainLayerWeight(MaterialData, MaterialTextureTableBuffer, BaseUv, LayerIndex, HasSplatMapValue);
        if (LayerWeight <= 0.0f) {
            continue;
        }

        AccumulatedColor += ResolveTerrainLayerDiffuse(MaterialData, MaterialTextureTableBuffer, BaseUv, LayerIndex, FallbackColor) * LayerWeight;
        AccumulatedNormal += ResolveTerrainLayerNormalTangent(MaterialData, MaterialTextureTableBuffer, BaseUv, LayerIndex) * LayerWeight;
        TotalWeight += LayerWeight;
    }

    if (TotalWeight <= 0.0f) {
        OutColor = ResolveTerrainLayerDiffuse(MaterialData, MaterialTextureTableBuffer, BaseUv, 0u, FallbackColor);
        OutNormalTangent = ResolveTerrainLayerNormalTangent(MaterialData, MaterialTextureTableBuffer, BaseUv, 0u);
        return;
    }

    OutColor = AccumulatedColor / TotalWeight;
    OutNormalTangent = normalize(AccumulatedNormal / TotalWeight);
}

float4 PsMain(TerrainVertexOutput Input) : SV_TARGET {
    StructuredBuffer<MaterialGpu> MaterialBuffer = ResourceDescriptorHeap[RootConstants.MaterialSrvIndex];
    StructuredBuffer<MaterialTextureTableItemGpu> MaterialTextureTableBuffer = ResourceDescriptorHeap[RootConstants.MaterialTextureTableSrvIndex];
    const MaterialGpu MaterialData = MaterialBuffer[Input.MaterialIndex];

    float4 TerrainColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float3 TerrainNormalTangent = float3(0.0f, 0.0f, 1.0f);
    ResolveTerrainMaterial(MaterialData, MaterialTextureTableBuffer, Input.TexCoord0, TerrainColor, TerrainNormalTangent);
    const float3 TerrainNormal = ResolveTerrainWorldNormal(Input.Normal, TerrainNormalTangent);
    const float4 BaseColor = ApplyBaseColor(TerrainColor);
    const float4 ScalarAppliedColor = ApplyMaterialScalarColor(BaseColor, MaterialData);
    float4 LitColor = ApplyMaterialLighting(ScalarAppliedColor, TerrainNormal);
    if (RootConstants.ShadowMappingParameterSrvIndex != 0xffffffffu && RootConstants.ShadowMapTextureBaseSrvIndex != 0xffffffffu) {
        StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
        StructuredBuffer<ShadowMappingParameterGpu> ShadowMappingParameterBuffer = ResourceDescriptorHeap[RootConstants.ShadowMappingParameterSrvIndex];
        const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[0];
        const ShadowMappingParameterGpu ShadowMappingParameter = ShadowMappingParameterBuffer[0];
        LitColor = ApplyMaterialLightingWithShadow(ScalarAppliedColor, TerrainNormal, Input.WorldPosition, ShadowMappingParameter, FrameGlobals, RootConstants.ShadowMapTextureBaseSrvIndex, ShadowComparisonSampler);
    }

    return ResolveFlags(LitColor, Input.Flags);
}
