#include "Common.hlsli"

struct EnvironmentGpuRootConstants
{
    uint mStatusUavIndex;
    uint mFrameIndexLow;
    uint mFrameIndexHigh;
    uint mTerrainHeightSrvIndex;
    uint mTerrainSplatSrvIndex;
    uint mTerrainSplat1SrvIndex;
    uint mTerrainWidth;
    uint mTerrainHeight;
    uint mFocusPositionX;
    uint mFocusPositionY;
    uint mFocusPositionZ;
    uint mDispatchThreadGroupSize;
    uint mInstanceContextSrvIndex;
    uint mInstanceContextUavIndex;
    uint mDrawRecordSrvIndex;
    uint mPlacementConfigSrvIndex;
    uint mPlacementRuleSrvIndex;
    uint mPlacementDrawRecordSrvIndex;
    uint mIndirectArgumentUavIndex;
    uint mVisibleInstanceIndexUavIndex;
    uint mDrawRecordCount;
    uint mVisibleInstanceIndexCapacity;
    uint mMaximumDrawDistance;
    uint mCullingRadius;
    uint mPlacementCandidateRecordSrvIndex;
    uint mCandidateContextSrvIndex;
    uint mCandidateContextUavIndex;
    uint mCandidateRecordCount;
    float4 mFrustumPlanes[6];
};

ConstantBuffer<EnvironmentGpuRootConstants> RootConstants : register(b1);

struct EnvironmentIndirectDrawArgument
{
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int BaseVertexLocation;
    uint StartInstanceLocation;
};

struct EnvironmentGpuPlacementConfig
{
    float4 FocusPositionRenderRadius;
    float4 TerrainPosition;
    float4 TerrainScale;
    float4 TerrainGridParameters;
    float4 TerrainSizeParameters;
    float4 DensityParameters;
    float4 ClusterParameters;
    float4 ClumpParameters0;
    float4 ClumpParameters1;
    float4 ForestParameters0;
    float4 ForestParameters1;
    float4 ForestParameters2;
    uint TerrainSeed;
    uint CandidateRandomXStream;
    uint CandidateRandomZStream;
    uint CandidateRandomChanceStream;
    uint CandidateRandomYawStream;
    uint CandidateRandomScaleStream;
    uint ClusterCornerStream;
    uint ClumpCenterXStream;
    uint ClumpCenterZStream;
    uint ClumpAngleStream;
    uint ClumpDistanceStream;
    uint ForestPatchCornerStream;
    uint ForestPatchNoiseIndex;
    uint MinimumSpacingPriorityStream;
    uint SeedSalt;
    uint MinimumSpacingCellRadius;
};

struct EnvironmentGpuPlacementRule
{
    float4 ScaleYawOffset;
    float4 DensityCluster;
    float4 ClusterShape;
    float4 ClusterForest;
    uint LayerIndex;
    uint ExcludedLayerMask;
    uint InstancesPerCell;
    uint Padding0;
};

struct EnvironmentGpuPlacementDrawRecord
{
    int MinimumCellX;
    int MinimumCellZ;
    uint CellCountX;
    uint CellCountZ;
    uint RuleIndex;
    uint LodIndex;
    float MinimumDistance;
    float MaximumDistance;
    uint CandidateOffset;
    uint CandidateCount;
    uint Padding0;
    uint Padding1;
};

struct EnvironmentGpuPlacementCandidateRecord
{
    int MinimumCellX;
    int MinimumCellZ;
    uint CellCountX;
    uint CellCountZ;
    uint RuleIndex;
    uint CandidateOffset;
    uint CandidateCount;
    uint Padding0;
};

struct EnvironmentGpuPlacementCandidate
{
    float4 PositionScale;
    float4 RotationValid;
    int CellX;
    int CellZ;
    uint RuleIndex;
    uint InstanceIndex;
};

struct FoliageCandidateKey
{
    int CellX;
    int CellZ;
    uint RuleIndex;
    uint InstanceIndex;
};

struct FoliageCandidate
{
    FoliageCandidateKey Key;
    float3 Position;
    float YawRadians;
    float Scale;
};

static const uint FoliageHashOffset = 2166136261u;
static const uint FoliageHashPrime = 16777619u;

uint MixHash(uint Value) {
    Value ^= Value >> 16u;
    Value *= 0x7feb352du;
    Value ^= Value >> 15u;
    Value *= 0x846ca68bu;
    Value ^= Value >> 16u;
    return Value;
}

uint BuildCandidateHash(uint TerrainSeed, uint Salt, FoliageCandidateKey Key, uint Stream) {
    uint Hash = MixHash(TerrainSeed ^ Salt ^ Stream);
    Hash = MixHash(Hash ^ (uint)Key.CellX);
    Hash = MixHash(Hash ^ ((uint)Key.CellZ * 0x9e3779b9u));
    Hash = MixHash(Hash ^ (Key.RuleIndex * 0x85ebca6bu));
    Hash = MixHash(Hash ^ (Key.InstanceIndex * 0xc2b2ae35u));
    return Hash;
}

uint BuildClusterHash(uint TerrainSeed, uint Salt, int GridX, int GridZ, uint ClusterIndex, uint Stream) {
    uint Hash = MixHash(TerrainSeed ^ Salt ^ Stream);
    Hash = MixHash(Hash ^ (uint)GridX);
    Hash = MixHash(Hash ^ ((uint)GridZ * 0x9e3779b9u));
    Hash = MixHash(Hash ^ (ClusterIndex * 0x85ebca6bu));
    return Hash;
}

float HashToUnitFloat(uint Hash) {
    return (float)(Hash & 0x00ffffffu) * (1.0f / 16777215.0f);
}

float SmoothStep01(float Value) {
    const float ClampedValue = clamp(Value, 0.0f, 1.0f);
    return ClampedValue * ClampedValue * (3.0f - (2.0f * ClampedValue));
}

float SmoothStepRange(float Start, float End, float Value) {
    if (End <= Start) {
        return Value >= End ? 1.0f : 0.0f;
    }

    return SmoothStep01((Value - Start) / (End - Start));
}

float SampleClusterCorner(uint TerrainSeed, uint Salt, int GridX, int GridZ, uint ClusterIndex, uint Stream) {
    return HashToUnitFloat(BuildClusterHash(TerrainSeed, Salt, GridX, GridZ, ClusterIndex, Stream));
}

float SampleValueNoise01(uint TerrainSeed, uint Salt, float X, float Z, uint ClusterIndex, uint Stream) {
    const int X0 = (int)floor(X);
    const int Z0 = (int)floor(Z);
    const int X1 = X0 + 1;
    const int Z1 = Z0 + 1;
    const float BlendX = SmoothStep01(X - (float)X0);
    const float BlendZ = SmoothStep01(Z - (float)Z0);
    const float Value00 = SampleClusterCorner(TerrainSeed, Salt, X0, Z0, ClusterIndex, Stream);
    const float Value10 = SampleClusterCorner(TerrainSeed, Salt, X1, Z0, ClusterIndex, Stream);
    const float Value01 = SampleClusterCorner(TerrainSeed, Salt, X0, Z1, ClusterIndex, Stream);
    const float Value11 = SampleClusterCorner(TerrainSeed, Salt, X1, Z1, ClusterIndex, Stream);
    const float ValueX0 = lerp(Value00, Value10, BlendX);
    const float ValueX1 = lerp(Value01, Value11, BlendX);
    return lerp(ValueX0, ValueX1, BlendZ);
}

float SampleFoliageClusterFactor(EnvironmentGpuPlacementConfig Config, EnvironmentGpuPlacementRule Rule, uint ClusterIndex, float WorldX, float WorldZ) {
    const float ClusterStrength = Rule.ClusterShape.x;
    if (ClusterStrength <= 0.0f) {
        return 1.0f;
    }

    const float ClusterScale = max(Rule.ClusterShape.y, 1.0f);
    const float NoiseValue = SampleValueNoise01(Config.TerrainSeed, Config.SeedSalt, WorldX / ClusterScale, WorldZ / ClusterScale, ClusterIndex, Config.ClusterCornerStream);
    const float ContrastedValue = clamp(((NoiseValue - 0.5f) * Rule.ClusterShape.z) + 0.5f, 0.0f, 1.0f);
    const float Coverage = clamp(Rule.ClusterShape.w, 0.01f, 1.0f);
    const float Threshold = 1.0f - Coverage;
    const float EdgeSoftness = max(Rule.ClusterForest.x, 0.0f);
    const float ClusterMask = SmoothStepRange(Threshold - EdgeSoftness, Threshold + EdgeSoftness, ContrastedValue);
    const float ClusterBoost = min(1.0f / Coverage, Config.ClusterParameters.x);
    const float ClusterDensity = lerp(Rule.ClusterForest.y, ClusterBoost, ClusterMask);
    return clamp(lerp(1.0f, ClusterDensity, ClusterStrength), 0.0f, Config.ClusterParameters.x);
}

float SampleForestAreaFactor(EnvironmentGpuPlacementConfig Config, float WorldX, float WorldZ) {
    if (Config.ClumpParameters1.w <= 0.5f) {
        return 1.0f;
    }

    const float PatchScale = max(Config.ForestParameters0.y, 1.0f);
    const float NoiseValue = SampleValueNoise01(Config.TerrainSeed, Config.SeedSalt, WorldX / PatchScale, WorldZ / PatchScale, Config.ForestPatchNoiseIndex, Config.ForestPatchCornerStream);
    const float ContrastedValue = clamp(((NoiseValue - 0.5f) * Config.ForestParameters0.w) + 0.5f, 0.0f, 1.0f);
    const float Coverage = clamp(Config.ForestParameters0.z, 0.01f, 1.0f);
    const float Threshold = 1.0f - Coverage;
    const float EdgeSoftness = max(Config.ForestParameters1.x, 0.0f);
    const float ForestMask = SmoothStepRange(Threshold - EdgeSoftness, Threshold + EdgeSoftness, ContrastedValue);
    return clamp(lerp(Config.ForestParameters1.y, 1.0f, ForestMask), 0.0f, 1.0f);
}

float ResolveRuleForestFactor(EnvironmentGpuPlacementConfig Config, EnvironmentGpuPlacementRule Rule, float WorldX, float WorldZ) {
    const float ForestStrength = Rule.ClusterForest.z;
    if (ForestStrength <= 0.0f) {
        return 1.0f;
    }

    const float ForestFactor = SampleForestAreaFactor(Config, WorldX, WorldZ);
    return clamp(lerp(1.0f, ForestFactor, ForestStrength), 0.0f, 1.0f);
}

float ResolveCandidateScale(EnvironmentGpuPlacementRule Rule, float LayerWeight, float ClusterFactor, float ForestFactor, float RandomScale) {
    const float BaseScale = lerp(Rule.ScaleYawOffset.x, Rule.ScaleYawOffset.y, RandomScale);
    const float WeightRange = max(1.0f - Rule.DensityCluster.z, 0.0001f);
    const float LayerVitality = SmoothStep01((LayerWeight - Rule.DensityCluster.z) / WeightRange);
    const float ClusterVitality = Rule.ClusterShape.x <= 0.0001f ? 1.0f : SmoothStep01(min(ClusterFactor, 1.0f));
    const float ForestVitality = Rule.ClusterForest.z <= 0.0001f ? 1.0f : SmoothStep01(ForestFactor);
    const float EdgeScale = lerp(0.72f, 1.0f, clamp(LayerVitality * ClusterVitality * ForestVitality, 0.0f, 1.0f));
    return BaseScale * EdgeScale;
}

void ApplyFoliageClumpPosition(EnvironmentGpuPlacementConfig Config, EnvironmentGpuPlacementRule Rule, FoliageCandidateKey Key, uint ClusterIndex, inout float WorldX, inout float WorldZ) {
    const float ClusterStrength = Rule.ClusterShape.x;
    if (ClusterStrength <= 0.0f) {
        return;
    }

    const float CellSize = max(Config.DensityParameters.x, Config.DensityParameters.w);
    const float ClusterScale = max(Rule.ClusterShape.y, CellSize * Config.ClusterParameters.y);
    const float ClumpGridScale = max(ClusterScale * Config.ClumpParameters0.x, CellSize * Config.ClumpParameters0.y);
    const int ClumpGridX = (int)floor(WorldX / ClumpGridScale);
    const int ClumpGridZ = (int)floor(WorldZ / ClumpGridScale);
    const float CenterX = ((float)ClumpGridX + Config.ClumpParameters0.z + (HashToUnitFloat(BuildClusterHash(Config.TerrainSeed, Config.SeedSalt, ClumpGridX, ClumpGridZ, ClusterIndex, Config.ClumpCenterXStream)) * Config.ClumpParameters0.w)) * ClumpGridScale;
    const float CenterZ = ((float)ClumpGridZ + Config.ClumpParameters0.z + (HashToUnitFloat(BuildClusterHash(Config.TerrainSeed, Config.SeedSalt, ClumpGridX, ClumpGridZ, ClusterIndex, Config.ClumpCenterZStream)) * Config.ClumpParameters0.w)) * ClumpGridScale;
    const float Angle = HashToUnitFloat(BuildCandidateHash(Config.TerrainSeed, Config.SeedSalt, Key, Config.ClumpAngleStream)) * Config.DensityParameters.z;
    const float DistanceAlpha = sqrt(HashToUnitFloat(BuildCandidateHash(Config.TerrainSeed, Config.SeedSalt, Key, Config.ClumpDistanceStream)));
    const float ClumpRadius = ClumpGridScale * Config.ClumpParameters1.x;
    const float TargetX = CenterX + (cos(Angle) * DistanceAlpha * ClumpRadius);
    const float TargetZ = CenterZ + (sin(Angle) * DistanceAlpha * ClumpRadius);
    const float PullStrength = clamp(ClusterStrength * Config.ClumpParameters1.y, 0.0f, Config.ClumpParameters1.z);
    WorldX = lerp(WorldX, TargetX, PullStrength);
    WorldZ = lerp(WorldZ, TargetZ, PullStrength);
}

float SampleHeight01(StructuredBuffer<float> HeightFieldBuffer, EnvironmentGpuPlacementConfig Config, float GridX, float GridZ) {
    const uint Width = max((uint)Config.TerrainSizeParameters.x, 1u);
    const uint Height = max((uint)Config.TerrainSizeParameters.y, 1u);
    const uint WidthMinusOne = Width > 0u ? Width - 1u : 0u;
    const uint HeightMinusOne = Height > 0u ? Height - 1u : 0u;
    const float ClampedGridX = clamp(GridX, 0.0f, (float)WidthMinusOne);
    const float ClampedGridZ = clamp(GridZ, 0.0f, (float)HeightMinusOne);
    const uint X0 = min((uint)floor(ClampedGridX), WidthMinusOne);
    const uint Z0 = min((uint)floor(ClampedGridZ), HeightMinusOne);
    const uint X1 = min(X0 + 1u, WidthMinusOne);
    const uint Z1 = min(Z0 + 1u, HeightMinusOne);
    const float BlendX = ClampedGridX - (float)X0;
    const float BlendZ = ClampedGridZ - (float)Z0;
    const float Height00 = HeightFieldBuffer[(Z0 * Width) + X0];
    const float Height10 = HeightFieldBuffer[(Z0 * Width) + X1];
    const float Height01 = HeightFieldBuffer[(Z1 * Width) + X0];
    const float Height11 = HeightFieldBuffer[(Z1 * Width) + X1];
    const float HeightX0 = lerp(Height00, Height10, BlendX);
    const float HeightX1 = lerp(Height01, Height11, BlendX);
    return lerp(HeightX0, HeightX1, BlendZ);
}

float4 SampleSplatWeight(Texture2D<float4> SplatMapTexture, EnvironmentGpuPlacementConfig Config, float GridX, float GridZ) {
    const uint HeightWidth = max((uint)Config.TerrainSizeParameters.x, 1u);
    const uint HeightHeight = max((uint)Config.TerrainSizeParameters.y, 1u);
    const uint SplatWidth = max((uint)Config.TerrainSizeParameters.z, 1u);
    const uint SplatHeight = max((uint)Config.TerrainSizeParameters.w, 1u);
    const float ScaleX = HeightWidth > 1u ? (float)(SplatWidth - 1u) / (float)(HeightWidth - 1u) : 1.0f;
    const float ScaleZ = HeightHeight > 1u ? (float)(SplatHeight - 1u) / (float)(HeightHeight - 1u) : 1.0f;
    const float SplatGridX = GridX * ScaleX;
    const float SplatGridZ = GridZ * ScaleZ;
    const float ClampedGridX = clamp(SplatGridX, 0.0f, (float)(SplatWidth - 1u));
    const float ClampedGridZ = clamp(SplatGridZ, 0.0f, (float)(SplatHeight - 1u));
    const uint X0 = min((uint)floor(ClampedGridX), SplatWidth - 1u);
    const uint Z0 = min((uint)floor(ClampedGridZ), SplatHeight - 1u);
    const uint X1 = min(X0 + 1u, SplatWidth - 1u);
    const uint Z1 = min(Z0 + 1u, SplatHeight - 1u);
    const float BlendX = ClampedGridX - (float)X0;
    const float BlendZ = ClampedGridZ - (float)Z0;
    const float4 Weight00 = SplatMapTexture.Load(int3((int)X0, (int)Z0, 0));
    const float4 Weight10 = SplatMapTexture.Load(int3((int)X1, (int)Z0, 0));
    const float4 Weight01 = SplatMapTexture.Load(int3((int)X0, (int)Z1, 0));
    const float4 Weight11 = SplatMapTexture.Load(int3((int)X1, (int)Z1, 0));
    const float4 WeightX0 = lerp(Weight00, Weight10, BlendX);
    const float4 WeightX1 = lerp(Weight01, Weight11, BlendX);
    return lerp(WeightX0, WeightX1, BlendZ);
}

float GetLayerWeight(float4 Splat0, float4 Splat1, uint LayerIndex) {
    if (LayerIndex == 0u) {
        return Splat0.x;
    }

    if (LayerIndex == 1u) {
        return Splat0.y;
    }

    if (LayerIndex == 2u) {
        return Splat0.z;
    }

    if (LayerIndex == 3u) {
        return Splat0.w;
    }

    if (LayerIndex == 4u) {
        return Splat1.x;
    }

    if (LayerIndex == 5u) {
        return Splat1.y;
    }

    if (LayerIndex == 6u) {
        return Splat1.z;
    }

    if (LayerIndex == 7u) {
        return Splat1.w;
    }

    return 0.0f;
}

float ResolveLayerWeight(EnvironmentGpuPlacementRule Rule, float4 Splat0, float4 Splat1) {
    if (Rule.ExcludedLayerMask != 0u) {
        float ExcludedWeight = 0.0f;
        [unroll]
        for (uint LayerIndex = 0u; LayerIndex < 8u; LayerIndex += 1u) {
            if ((Rule.ExcludedLayerMask & (1u << LayerIndex)) != 0u) {
                ExcludedWeight += GetLayerWeight(Splat0, Splat1, LayerIndex);
            }
        }

        return clamp(1.0f - ExcludedWeight, 0.0f, 1.0f);
    }

    return clamp(GetLayerWeight(Splat0, Splat1, Rule.LayerIndex), 0.0f, 1.0f);
}

bool TrySampleTerrain(StructuredBuffer<float> HeightFieldBuffer, Texture2D<float4> Splat0Texture, Texture2D<float4> Splat1Texture, EnvironmentGpuPlacementConfig Config, EnvironmentGpuPlacementRule Rule, float WorldX, float WorldZ, out float OutWorldY, out float OutLayerWeight) {
    OutWorldY = 0.0f;
    OutLayerWeight = 0.0f;
    const float ScaleX = abs(Config.TerrainScale.x) > Config.DensityParameters.w ? Config.TerrainScale.x : 1.0f;
    const float ScaleY = abs(Config.TerrainScale.y) > Config.DensityParameters.w ? Config.TerrainScale.y : 1.0f;
    const float ScaleZ = abs(Config.TerrainScale.z) > Config.DensityParameters.w ? Config.TerrainScale.z : 1.0f;
    const float LocalX = (WorldX - Config.TerrainPosition.x) / ScaleX;
    const float LocalZ = (WorldZ - Config.TerrainPosition.z) / ScaleZ;
    const float GridX = (LocalX + Config.TerrainGridParameters.z) / Config.TerrainGridParameters.x;
    const float GridZ = (LocalZ + Config.TerrainGridParameters.w) / Config.TerrainGridParameters.y;
    const uint Width = max((uint)Config.TerrainSizeParameters.x, 1u);
    const uint Height = max((uint)Config.TerrainSizeParameters.y, 1u);
    if (GridX < 0.0f || GridZ < 0.0f || GridX > (float)(Width - 1u) || GridZ > (float)(Height - 1u)) {
        return false;
    }

    const float Height01 = SampleHeight01(HeightFieldBuffer, Config, GridX, GridZ);
    const float4 Splat0 = SampleSplatWeight(Splat0Texture, Config, GridX, GridZ);
    const float4 Splat1 = SampleSplatWeight(Splat1Texture, Config, GridX, GridZ);
    OutWorldY = Config.TerrainPosition.y + (Height01 * Config.TerrainPosition.w * ScaleY);
    OutLayerWeight = ResolveLayerWeight(Rule, Splat0, Splat1);
    return true;
}

bool IsForestAreaWideEnough(StructuredBuffer<float> HeightFieldBuffer, Texture2D<float4> Splat0Texture, Texture2D<float4> Splat1Texture, EnvironmentGpuPlacementConfig Config, EnvironmentGpuPlacementRule Rule, float WorldX, float WorldZ) {
    if (Config.ClumpParameters1.w <= 0.5f || Config.ForestParameters0.x <= Config.DensityParameters.w || Rule.ClusterForest.z <= Config.DensityParameters.w || Rule.ClusterForest.w <= Config.DensityParameters.w) {
        return true;
    }

    const float CenterForestFactor = ResolveRuleForestFactor(Config, Rule, WorldX, WorldZ);
    if (CenterForestFactor < Config.ForestParameters1.z) {
        return false;
    }

    const float SampleDistance = Config.ForestParameters0.x * Config.ForestParameters1.w;
    if (SampleDistance <= Config.DensityParameters.w) {
        return true;
    }

    const float DiagonalDistance = SampleDistance * Config.ForestParameters2.x;
    const uint RequiredSampleCount = (uint)ceil(8.0f * Config.ForestParameters2.y);
    uint AcceptedSampleCount = 0u;
    const float2 SampleOffsets[8] = { float2(SampleDistance, 0.0f), float2(-SampleDistance, 0.0f), float2(0.0f, SampleDistance), float2(0.0f, -SampleDistance), float2(DiagonalDistance, DiagonalDistance), float2(-DiagonalDistance, DiagonalDistance), float2(DiagonalDistance, -DiagonalDistance), float2(-DiagonalDistance, -DiagonalDistance) };
    [unroll]
    for (uint SampleIndex = 0u; SampleIndex < 8u; SampleIndex += 1u) {
        float SampleWorldY = 0.0f;
        float SampleLayerWeight = 0.0f;
        const float SampleWorldX = WorldX + SampleOffsets[SampleIndex].x;
        const float SampleWorldZ = WorldZ + SampleOffsets[SampleIndex].y;
        const bool HasSample = TrySampleTerrain(HeightFieldBuffer, Splat0Texture, Splat1Texture, Config, Rule, SampleWorldX, SampleWorldZ, SampleWorldY, SampleLayerWeight);
        const float SampleForestFactor = ResolveRuleForestFactor(Config, Rule, SampleWorldX, SampleWorldZ);
        if (HasSample && SampleLayerWeight >= Rule.DensityCluster.z && SampleForestFactor >= Config.ForestParameters1.z) {
            AcceptedSampleCount += 1u;
        }
    }

    return AcceptedSampleCount >= RequiredSampleCount;
}

bool TryCreateBaseCandidate(StructuredBuffer<float> HeightFieldBuffer, Texture2D<float4> Splat0Texture, Texture2D<float4> Splat1Texture, EnvironmentGpuPlacementConfig Config, EnvironmentGpuPlacementRule Rule, FoliageCandidateKey Key, out FoliageCandidate OutCandidate) {
    const float RandomX = HashToUnitFloat(BuildCandidateHash(Config.TerrainSeed, Config.SeedSalt, Key, Config.CandidateRandomXStream));
    const float RandomZ = HashToUnitFloat(BuildCandidateHash(Config.TerrainSeed, Config.SeedSalt, Key, Config.CandidateRandomZStream));
    const float RandomChance = HashToUnitFloat(BuildCandidateHash(Config.TerrainSeed, Config.SeedSalt, Key, Config.CandidateRandomChanceStream));
    float WorldX = ((float)Key.CellX + RandomX) * Config.DensityParameters.x;
    float WorldZ = ((float)Key.CellZ + RandomZ) * Config.DensityParameters.x;
    const uint ClusterIndex = Rule.LayerIndex;
    ApplyFoliageClumpPosition(Config, Rule, Key, ClusterIndex, WorldX, WorldZ);
    const float ClusterFactor = SampleFoliageClusterFactor(Config, Rule, ClusterIndex, WorldX, WorldZ);
    const float ForestFactor = ResolveRuleForestFactor(Config, Rule, WorldX, WorldZ);
    const float SpawnChanceBase = Rule.DensityCluster.y * Rule.DensityCluster.x * Config.DensityParameters.y * ClusterFactor * ForestFactor;
    const float MaximumEffectiveSpawnChance = clamp(SpawnChanceBase, 0.0f, 1.0f);
    if (MaximumEffectiveSpawnChance <= 0.0f || RandomChance > MaximumEffectiveSpawnChance) {
        return false;
    }

    float WorldY = 0.0f;
    float LayerWeight = 0.0f;
    const bool HasTerrainSample = TrySampleTerrain(HeightFieldBuffer, Splat0Texture, Splat1Texture, Config, Rule, WorldX, WorldZ, WorldY, LayerWeight);
    const float EffectiveSpawnChance = clamp(SpawnChanceBase * LayerWeight, 0.0f, 1.0f);
    if (HasTerrainSample == false || LayerWeight < Rule.DensityCluster.z || EffectiveSpawnChance <= 0.0f || RandomChance > EffectiveSpawnChance) {
        return false;
    }

    if (IsForestAreaWideEnough(HeightFieldBuffer, Splat0Texture, Splat1Texture, Config, Rule, WorldX, WorldZ) == false) {
        return false;
    }

    const float RandomYaw = HashToUnitFloat(BuildCandidateHash(Config.TerrainSeed, Config.SeedSalt, Key, Config.CandidateRandomYawStream));
    const float RandomScale = HashToUnitFloat(BuildCandidateHash(Config.TerrainSeed, Config.SeedSalt, Key, Config.CandidateRandomScaleStream));
    OutCandidate.Key = Key;
    OutCandidate.Position = float3(WorldX, WorldY + Rule.DensityCluster.w, WorldZ);
    OutCandidate.YawRadians = lerp(Rule.ScaleYawOffset.z, Rule.ScaleYawOffset.w, RandomYaw);
    OutCandidate.Scale = ResolveCandidateScale(Rule, LayerWeight, ClusterFactor, ForestFactor, RandomScale);
    return true;
}

bool IsCandidateKeyLess(FoliageCandidateKey Left, FoliageCandidateKey Right) {
    if (Left.CellX != Right.CellX) {
        return Left.CellX < Right.CellX;
    }

    if (Left.CellZ != Right.CellZ) {
        return Left.CellZ < Right.CellZ;
    }

    if (Left.RuleIndex != Right.RuleIndex) {
        return Left.RuleIndex < Right.RuleIndex;
    }

    return Left.InstanceIndex < Right.InstanceIndex;
}

bool AreCandidateKeysEqual(FoliageCandidateKey Left, FoliageCandidateKey Right) {
    return Left.CellX == Right.CellX && Left.CellZ == Right.CellZ && Left.RuleIndex == Right.RuleIndex && Left.InstanceIndex == Right.InstanceIndex;
}

bool IsHigherPriorityCandidate(EnvironmentGpuPlacementConfig Config, FoliageCandidateKey CandidateKey, FoliageCandidateKey OtherKey) {
    const uint CandidatePriority = BuildCandidateHash(Config.TerrainSeed, Config.SeedSalt, CandidateKey, Config.MinimumSpacingPriorityStream);
    const uint OtherPriority = BuildCandidateHash(Config.TerrainSeed, Config.SeedSalt, OtherKey, Config.MinimumSpacingPriorityStream);
    return OtherPriority < CandidatePriority || (OtherPriority == CandidatePriority && IsCandidateKeyLess(OtherKey, CandidateKey));
}

bool IsPlacementCandidateValid(EnvironmentGpuPlacementCandidate Candidate) {
    return Candidate.RotationValid.y > 0.5f;
}

FoliageCandidate BuildFoliageCandidate(EnvironmentGpuPlacementCandidate SourceCandidate) {
    FoliageCandidate Candidate;
    Candidate.Key.CellX = SourceCandidate.CellX;
    Candidate.Key.CellZ = SourceCandidate.CellZ;
    Candidate.Key.RuleIndex = SourceCandidate.RuleIndex;
    Candidate.Key.InstanceIndex = SourceCandidate.InstanceIndex;
    Candidate.Position = SourceCandidate.PositionScale.xyz;
    Candidate.Scale = SourceCandidate.PositionScale.w;
    Candidate.YawRadians = SourceCandidate.RotationValid.x;
    return Candidate;
}

EnvironmentGpuPlacementCandidate BuildGpuPlacementCandidate(FoliageCandidate Candidate, bool IsValid) {
    EnvironmentGpuPlacementCandidate GpuCandidate;
    GpuCandidate.PositionScale = float4(Candidate.Position, Candidate.Scale);
    GpuCandidate.RotationValid = float4(Candidate.YawRadians, IsValid ? 1.0f : 0.0f, 0.0f, 0.0f);
    GpuCandidate.CellX = Candidate.Key.CellX;
    GpuCandidate.CellZ = Candidate.Key.CellZ;
    GpuCandidate.RuleIndex = Candidate.Key.RuleIndex;
    GpuCandidate.InstanceIndex = Candidate.Key.InstanceIndex;
    return GpuCandidate;
}

EnvironmentGpuPlacementCandidate BuildInvalidGpuPlacementCandidate(FoliageCandidateKey Key) {
    EnvironmentGpuPlacementCandidate GpuCandidate;
    GpuCandidate.PositionScale = float4(0.0f, 0.0f, 0.0f, 0.0f);
    GpuCandidate.RotationValid = float4(0.0f, 0.0f, 0.0f, 0.0f);
    GpuCandidate.CellX = Key.CellX;
    GpuCandidate.CellZ = Key.CellZ;
    GpuCandidate.RuleIndex = Key.RuleIndex;
    GpuCandidate.InstanceIndex = Key.InstanceIndex;
    return GpuCandidate;
}

bool TryGetPlacementCandidateFromBuffer(StructuredBuffer<EnvironmentGpuPlacementCandidateRecord> CandidateRecordBuffer, StructuredBuffer<EnvironmentGpuPlacementCandidate> CandidateBuffer, StructuredBuffer<EnvironmentGpuPlacementRule> RuleBuffer, uint RuleIndex, int CellX, int CellZ, uint InstanceIndex, out FoliageCandidate OutCandidate) {
    if (RuleIndex >= RootConstants.mCandidateRecordCount) {
        return false;
    }

    const EnvironmentGpuPlacementCandidateRecord CandidateRecord = CandidateRecordBuffer[RuleIndex];
    if (CandidateRecord.CandidateCount == 0u || CellX < CandidateRecord.MinimumCellX || CellZ < CandidateRecord.MinimumCellZ) {
        return false;
    }

    const uint CellOffsetX = (uint)(CellX - CandidateRecord.MinimumCellX);
    const uint CellOffsetZ = (uint)(CellZ - CandidateRecord.MinimumCellZ);
    if (CellOffsetX >= CandidateRecord.CellCountX || CellOffsetZ >= CandidateRecord.CellCountZ) {
        return false;
    }

    const EnvironmentGpuPlacementRule Rule = RuleBuffer[RuleIndex];
    if (InstanceIndex >= Rule.InstancesPerCell) {
        return false;
    }

    const uint CandidateLocalIndex = ((CellOffsetZ * CandidateRecord.CellCountX) + CellOffsetX) * Rule.InstancesPerCell + InstanceIndex;
    if (CandidateLocalIndex >= CandidateRecord.CandidateCount) {
        return false;
    }

    const EnvironmentGpuPlacementCandidate GpuCandidate = CandidateBuffer[CandidateRecord.CandidateOffset + CandidateLocalIndex];
    if (IsPlacementCandidateValid(GpuCandidate) == false) {
        return false;
    }

    OutCandidate = BuildFoliageCandidate(GpuCandidate);
    return true;
}

bool PassesMinimumSpacing(StructuredBuffer<EnvironmentGpuPlacementCandidateRecord> CandidateRecordBuffer, StructuredBuffer<EnvironmentGpuPlacementCandidate> CandidateBuffer, StructuredBuffer<EnvironmentGpuPlacementRule> RuleBuffer, EnvironmentGpuPlacementConfig Config, EnvironmentGpuPlacementRule Rule, FoliageCandidate Candidate) {
    const float MinimumSpacing = Rule.ClusterForest.w;
    if (MinimumSpacing <= Config.DensityParameters.w) {
        return true;
    }

    uint RuleCount = 0u;
    uint RuleStride = 0u;
    RuleBuffer.GetDimensions(RuleCount, RuleStride);
    const int CellRadius = (int)Config.MinimumSpacingCellRadius;
    [loop]
    for (int CellZ = Candidate.Key.CellZ - CellRadius; CellZ <= Candidate.Key.CellZ + CellRadius; CellZ += 1) {
        [loop]
        for (int CellX = Candidate.Key.CellX - CellRadius; CellX <= Candidate.Key.CellX + CellRadius; CellX += 1) {
            [loop]
            for (uint RuleIndex = 0u; RuleIndex < min(RuleCount, RootConstants.mCandidateRecordCount); RuleIndex += 1u) {
                const EnvironmentGpuPlacementRule OtherRule = RuleBuffer[RuleIndex];
                if (OtherRule.ClusterForest.w <= Config.DensityParameters.w) {
                    continue;
                }

                [loop]
                for (uint InstanceIndex = 0u; InstanceIndex < OtherRule.InstancesPerCell; InstanceIndex += 1u) {
                    FoliageCandidateKey OtherKey;
                    OtherKey.CellX = CellX;
                    OtherKey.CellZ = CellZ;
                    OtherKey.RuleIndex = RuleIndex;
                    OtherKey.InstanceIndex = InstanceIndex;
                    if (AreCandidateKeysEqual(Candidate.Key, OtherKey) || IsHigherPriorityCandidate(Config, Candidate.Key, OtherKey) == false) {
                        continue;
                    }

                    FoliageCandidate OtherCandidate;
                    if (TryGetPlacementCandidateFromBuffer(CandidateRecordBuffer, CandidateBuffer, RuleBuffer, RuleIndex, CellX, CellZ, InstanceIndex, OtherCandidate) == false) {
                        continue;
                    }

                    const float ResolvedMinimumSpacing = max(MinimumSpacing, OtherRule.ClusterForest.w);
                    const float2 Delta = Candidate.Position.xz - OtherCandidate.Position.xz;
                    if (dot(Delta, Delta) < (ResolvedMinimumSpacing * ResolvedMinimumSpacing)) {
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

bool DoesCellIntersectRenderRadius(EnvironmentGpuPlacementConfig Config, int CellX, int CellZ) {
    const float CellSize = Config.DensityParameters.x;
    const float MinimumX = (float)CellX * CellSize;
    const float MaximumX = MinimumX + CellSize;
    const float MinimumZ = (float)CellZ * CellSize;
    const float MaximumZ = MinimumZ + CellSize;
    const float ClosestX = clamp(Config.FocusPositionRenderRadius.x, MinimumX, MaximumX);
    const float ClosestZ = clamp(Config.FocusPositionRenderRadius.z, MinimumZ, MaximumZ);
    const float DistanceX = Config.FocusPositionRenderRadius.x - ClosestX;
    const float DistanceZ = Config.FocusPositionRenderRadius.z - ClosestZ;
    return ((DistanceX * DistanceX) + (DistanceZ * DistanceZ)) <= (Config.FocusPositionRenderRadius.w * Config.FocusPositionRenderRadius.w);
}

bool DoesCandidateMatchDrawLod(EnvironmentGpuPlacementConfig Config, EnvironmentGpuPlacementDrawRecord PlacementDrawRecord, FoliageCandidate Candidate) {
    const float2 Delta = Candidate.Position.xz - Config.FocusPositionRenderRadius.xz;
    const float DistanceSquared = dot(Delta, Delta);
    const float MaximumDistanceSquared = PlacementDrawRecord.MaximumDistance * PlacementDrawRecord.MaximumDistance;
    if (PlacementDrawRecord.LodIndex == 0u) {
        return DistanceSquared <= MaximumDistanceSquared;
    }

    const float MinimumDistanceSquared = PlacementDrawRecord.MinimumDistance * PlacementDrawRecord.MinimumDistance;
    return DistanceSquared > MinimumDistanceSquared && DistanceSquared <= MaximumDistanceSquared;
}

bool IsCandidateVisible(EnvironmentGpuPlacementConfig Config, FoliageCandidate Candidate) {
    const float2 Delta = Candidate.Position.xz - Config.FocusPositionRenderRadius.xz;
    const float DistanceSquared = dot(Delta, Delta);
    const float MaximumDistance = min(asfloat(RootConstants.mMaximumDrawDistance), Config.FocusPositionRenderRadius.w);
    if (DistanceSquared > MaximumDistance * MaximumDistance) {
        return false;
    }

    const float Radius = asfloat(RootConstants.mCullingRadius) * max(Candidate.Scale, 1.0f);
    [unroll]
    for (uint PlaneIndex = 0u; PlaneIndex < 6u; PlaneIndex += 1u) {
        const float PlaneDistance = dot(RootConstants.mFrustumPlanes[PlaneIndex].xyz, Candidate.Position) + RootConstants.mFrustumPlanes[PlaneIndex].w;
        if (PlaneDistance < -Radius) {
            return false;
        }
    }

    return true;
}

FoliageCandidateKey BuildCandidateKey(EnvironmentGpuPlacementCandidateRecord CandidateRecord, EnvironmentGpuPlacementRule Rule, uint LocalIndex) {
    const uint CellLinearIndex = LocalIndex / Rule.InstancesPerCell;
    FoliageCandidateKey Key;
    Key.CellX = CandidateRecord.MinimumCellX + (int)(CellLinearIndex % CandidateRecord.CellCountX);
    Key.CellZ = CandidateRecord.MinimumCellZ + (int)(CellLinearIndex / CandidateRecord.CellCountX);
    Key.RuleIndex = CandidateRecord.RuleIndex;
    Key.InstanceIndex = LocalIndex % Rule.InstancesPerCell;
    return Key;
}

[numthreads(64, 1, 1)]
void GenerateCandidatesCsMain(uint3 DispatchThreadId : SV_DispatchThreadID, uint3 GroupId : SV_GroupID, uint GroupIndex : SV_GroupIndex) {
    RWStructuredBuffer<uint> StatusBuffer = ResourceDescriptorHeap[RootConstants.mStatusUavIndex];
    if (GroupId.x == 0u && GroupIndex == 0u) {
        StatusBuffer[0] = RootConstants.mFrameIndexLow;
        StatusBuffer[1] = RootConstants.mFrameIndexHigh;
        StatusBuffer[2] = RootConstants.mTerrainHeightSrvIndex;
        StatusBuffer[3] = RootConstants.mTerrainSplatSrvIndex;
        StatusBuffer[4] = RootConstants.mTerrainSplat1SrvIndex;
        StatusBuffer[5] = RootConstants.mTerrainWidth;
        StatusBuffer[6] = RootConstants.mTerrainHeight;
        StatusBuffer[7] = RootConstants.mDrawRecordCount;
        StatusBuffer[8] = RootConstants.mVisibleInstanceIndexCapacity;
        StatusBuffer[9] = RootConstants.mCandidateRecordCount;
    }

    const uint CandidateRecordIndex = GroupId.x;
    if (CandidateRecordIndex >= RootConstants.mCandidateRecordCount) {
        return;
    }

    StructuredBuffer<float> HeightFieldBuffer = ResourceDescriptorHeap[RootConstants.mTerrainHeightSrvIndex];
    Texture2D<float4> Splat0Texture = ResourceDescriptorHeap[RootConstants.mTerrainSplatSrvIndex];
    Texture2D<float4> Splat1Texture = ResourceDescriptorHeap[RootConstants.mTerrainSplat1SrvIndex];
    StructuredBuffer<EnvironmentGpuPlacementConfig> PlacementConfigBuffer = ResourceDescriptorHeap[RootConstants.mPlacementConfigSrvIndex];
    StructuredBuffer<EnvironmentGpuPlacementRule> PlacementRuleBuffer = ResourceDescriptorHeap[RootConstants.mPlacementRuleSrvIndex];
    StructuredBuffer<EnvironmentGpuPlacementCandidateRecord> PlacementCandidateRecordBuffer = ResourceDescriptorHeap[RootConstants.mPlacementCandidateRecordSrvIndex];
    RWStructuredBuffer<EnvironmentGpuPlacementCandidate> CandidateBuffer = ResourceDescriptorHeap[RootConstants.mCandidateContextUavIndex];

    const EnvironmentGpuPlacementConfig Config = PlacementConfigBuffer[0];
    const EnvironmentGpuPlacementCandidateRecord CandidateRecord = PlacementCandidateRecordBuffer[CandidateRecordIndex];
    const EnvironmentGpuPlacementRule Rule = PlacementRuleBuffer[CandidateRecord.RuleIndex];
    for (uint LocalIndex = GroupIndex; LocalIndex < CandidateRecord.CandidateCount; LocalIndex += RootConstants.mDispatchThreadGroupSize) {
        const FoliageCandidateKey Key = BuildCandidateKey(CandidateRecord, Rule, LocalIndex);
        if (DoesCellIntersectRenderRadius(Config, Key.CellX, Key.CellZ) == false) {
            CandidateBuffer[CandidateRecord.CandidateOffset + LocalIndex] = BuildInvalidGpuPlacementCandidate(Key);
            continue;
        }

        FoliageCandidate Candidate;
        if (TryCreateBaseCandidate(HeightFieldBuffer, Splat0Texture, Splat1Texture, Config, Rule, Key, Candidate) == false) {
            CandidateBuffer[CandidateRecord.CandidateOffset + LocalIndex] = BuildInvalidGpuPlacementCandidate(Key);
            continue;
        }

        CandidateBuffer[CandidateRecord.CandidateOffset + LocalIndex] = BuildGpuPlacementCandidate(Candidate, true);
    }
}

[numthreads(64, 1, 1)]
void ClassifyCandidatesCsMain(uint3 DispatchThreadId : SV_DispatchThreadID, uint3 GroupId : SV_GroupID, uint GroupIndex : SV_GroupIndex) {
    const uint DrawRecordIndex = GroupId.x;
    if (DrawRecordIndex >= RootConstants.mDrawRecordCount) {
        return;
    }

    StructuredBuffer<EnvironmentDrawRecordGpu> DrawRecordBuffer = ResourceDescriptorHeap[RootConstants.mDrawRecordSrvIndex];
    StructuredBuffer<EnvironmentGpuPlacementConfig> PlacementConfigBuffer = ResourceDescriptorHeap[RootConstants.mPlacementConfigSrvIndex];
    StructuredBuffer<EnvironmentGpuPlacementRule> PlacementRuleBuffer = ResourceDescriptorHeap[RootConstants.mPlacementRuleSrvIndex];
    StructuredBuffer<EnvironmentGpuPlacementDrawRecord> PlacementDrawRecordBuffer = ResourceDescriptorHeap[RootConstants.mPlacementDrawRecordSrvIndex];
    StructuredBuffer<EnvironmentGpuPlacementCandidateRecord> PlacementCandidateRecordBuffer = ResourceDescriptorHeap[RootConstants.mPlacementCandidateRecordSrvIndex];
    StructuredBuffer<EnvironmentGpuPlacementCandidate> CandidateBuffer = ResourceDescriptorHeap[RootConstants.mCandidateContextSrvIndex];
    RWStructuredBuffer<EnvironmentInstanceContextGpu> InstanceContextBuffer = ResourceDescriptorHeap[RootConstants.mInstanceContextUavIndex];
    RWStructuredBuffer<EnvironmentIndirectDrawArgument> IndirectArgumentBuffer = ResourceDescriptorHeap[RootConstants.mIndirectArgumentUavIndex];
    RWStructuredBuffer<uint> VisibleInstanceIndexBuffer = ResourceDescriptorHeap[RootConstants.mVisibleInstanceIndexUavIndex];

    const EnvironmentGpuPlacementConfig Config = PlacementConfigBuffer[0];
    const EnvironmentDrawRecordGpu DrawRecord = DrawRecordBuffer[DrawRecordIndex];
    const EnvironmentGpuPlacementDrawRecord PlacementDrawRecord = PlacementDrawRecordBuffer[DrawRecordIndex];
    const EnvironmentGpuPlacementRule Rule = PlacementRuleBuffer[PlacementDrawRecord.RuleIndex];

    if (GroupIndex == 0u) {
        IndirectArgumentBuffer[DrawRecordIndex].InstanceCount = 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint LocalIndex = GroupIndex; LocalIndex < DrawRecord.InstanceCount; LocalIndex += RootConstants.mDispatchThreadGroupSize) {
        if (LocalIndex >= PlacementDrawRecord.CandidateCount) {
            continue;
        }

        const EnvironmentGpuPlacementCandidate GpuCandidate = CandidateBuffer[PlacementDrawRecord.CandidateOffset + LocalIndex];
        if (IsPlacementCandidateValid(GpuCandidate) == false) {
            continue;
        }

        const FoliageCandidate Candidate = BuildFoliageCandidate(GpuCandidate);
        if (DoesCandidateMatchDrawLod(Config, PlacementDrawRecord, Candidate) == false || IsCandidateVisible(Config, Candidate) == false) {
            continue;
        }

        if (PassesMinimumSpacing(PlacementCandidateRecordBuffer, CandidateBuffer, PlacementRuleBuffer, Config, Rule, Candidate) == false) {
            continue;
        }

        const uint InstanceIndex = DrawRecord.InstanceOffset + LocalIndex;
        EnvironmentInstanceContextGpu InstanceContext;
        InstanceContext.PositionScale = float4(Candidate.Position, Candidate.Scale);
        InstanceContext.RotationVariation = float4(Candidate.YawRadians, (float)Candidate.Key.InstanceIndex, 0.0f, 0.0f);
        InstanceContextBuffer[InstanceIndex] = InstanceContext;

        uint VisibleLocalIndex = 0u;
        InterlockedAdd(IndirectArgumentBuffer[DrawRecordIndex].InstanceCount, 1u, VisibleLocalIndex);
        const uint VisibleIndex = DrawRecord.VisibleInstanceOffset + VisibleLocalIndex;
        if (VisibleIndex < RootConstants.mVisibleInstanceIndexCapacity) {
            VisibleInstanceIndexBuffer[VisibleIndex] = InstanceIndex;
        }
    }
}
