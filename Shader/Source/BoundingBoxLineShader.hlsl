#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);

struct BoundingBoxVsOutput
{
    uint InstanceId : INSTANCE_ID;
};

struct BoundingBoxGsOutput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
};

BoundingBoxVsOutput VsMain(uint InstanceId : SV_InstanceID)
{
    BoundingBoxVsOutput Output;
    Output.InstanceId = InstanceId;
    return Output;
}

[maxvertexcount(24)]
void GsMain(point BoundingBoxVsOutput Input[1], inout LineStream<BoundingBoxGsOutput> Stream)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<ModelContextGpu> ModelContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];

    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[0];
    const ModelContextGpu ModelContext = ModelContextBuffer[Input[0].InstanceId];

    const float3 Center = ModelContext.BbCenter.xyz;
    const float3 Extents = max(ModelContext.BbExtents.xyz, float3(0.0001f, 0.0001f, 0.0001f));

    const float3 LocalCorners[8] =
    {
        float3(-1.0f, -1.0f, -1.0f),
        float3( 1.0f, -1.0f, -1.0f),
        float3( 1.0f,  1.0f, -1.0f),
        float3(-1.0f,  1.0f, -1.0f),
        float3(-1.0f, -1.0f,  1.0f),
        float3( 1.0f, -1.0f,  1.0f),
        float3( 1.0f,  1.0f,  1.0f),
        float3(-1.0f,  1.0f,  1.0f)
    };

    const uint2 EdgePairs[12] =
    {
        uint2(0, 1), uint2(1, 2), uint2(2, 3), uint2(3, 0),
        uint2(4, 5), uint2(5, 6), uint2(6, 7), uint2(7, 4),
        uint2(0, 4), uint2(1, 5), uint2(2, 6), uint2(3, 7)
    };

    float4x4 World = transpose(ModelContext.World);
    float4x4 ViewProj = transpose(FrameGlobals.ViewProj);

    for (uint EdgeIndex = 0; EdgeIndex < 12; ++EdgeIndex)
    {
        const uint StartCornerIndex = EdgePairs[EdgeIndex].x;
        const uint EndCornerIndex = EdgePairs[EdgeIndex].y;

        const float3 LocalStart = Center + (LocalCorners[StartCornerIndex] * Extents);
        const float3 LocalEnd = Center + (LocalCorners[EndCornerIndex] * Extents);

        const float4 WorldStart = mul(float4(LocalStart, 1.0f), World);
        const float4 WorldEnd = mul(float4(LocalEnd, 1.0f), World);

        BoundingBoxGsOutput StartVertex;
        StartVertex.Position = mul(WorldStart, ViewProj);
        StartVertex.Color = float4(1.0f, 0.0f, 0.0f, 1.0f);
        Stream.Append(StartVertex);

        BoundingBoxGsOutput EndVertex;
        EndVertex.Position = mul(WorldEnd, ViewProj);
        EndVertex.Color = float4(1.0f, 0.0f, 0.0f, 1.0f);
        Stream.Append(EndVertex);

        Stream.RestartStrip();
    }
}

float4 PsMain(BoundingBoxGsOutput Input) : SV_TARGET
{
    return Input.Color;
}
