#include "Common.hlsli"

ConstantBuffer<RootConstantsB1> RootConstants : register(b1);

static const uint DebugGeometryTypeLineValue = 0u;
static const uint DebugGeometryTypeWireCubeValue = 1u;
static const float MinimumDebugGeometryLineThickness = 0.0001f;

struct DebugGeometryVsOutput
{
    uint InstanceId : INSTANCE_ID;
};

struct DebugGeometryGsOutput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
};

DebugGeometryVsOutput VsMain(uint InstanceId : SV_InstanceID)
{
    DebugGeometryVsOutput Output;
    Output.InstanceId = InstanceId;
    return Output;
}

float3 RotateByQuaternion(float3 VectorValue, float4 QuaternionValue)
{
    const float3 QuaternionAxis = QuaternionValue.xyz;
    const float QuaternionW = QuaternionValue.w;
    const float3 AxisCrossVector = cross(QuaternionAxis, VectorValue);
    const float3 RotatedVector = VectorValue + (2.0f * (QuaternionW * AxisCrossVector + cross(QuaternionAxis, AxisCrossVector)));
    return RotatedVector;
}

void AppendClipVertexToTriangleStream(inout TriangleStream<DebugGeometryGsOutput> Stream, float4 ClipPosition, float4 ColorValue)
{
    DebugGeometryGsOutput Output;
    Output.Position = ClipPosition;
    Output.Color = ColorValue;
    Stream.Append(Output);
}

bool ClipLineSegmentToNearPlane(inout float3 StartViewPosition, inout float3 EndViewPosition, float NearPlaneDistance)
{
    const bool IsStartInside = StartViewPosition.z >= NearPlaneDistance;
    const bool IsEndInside = EndViewPosition.z >= NearPlaneDistance;
    if (IsStartInside == false && IsEndInside == false)
    {
        return false;
    }

    if (IsStartInside && IsEndInside)
    {
        return true;
    }

    const float3 ViewLineVector = EndViewPosition - StartViewPosition;
    const float Denominator = ViewLineVector.z;
    if (abs(Denominator) <= 0.000001f)
    {
        return false;
    }

    const float IntersectionT = saturate((NearPlaneDistance - StartViewPosition.z) / Denominator);
    const float3 IntersectionPoint = StartViewPosition + (ViewLineVector * IntersectionT);
    if (IsStartInside == false)
    {
        StartViewPosition = IntersectionPoint;
    }
    else
    {
        EndViewPosition = IntersectionPoint;
    }

    return true;
}

void AppendLineQuad(inout TriangleStream<DebugGeometryGsOutput> Stream, float3 StartPosition, float3 EndPosition, float4 ColorValue, float LineThickness, float4x4 ViewMatrix, float4x4 ProjectionMatrix)
{
    float3 StartViewPosition = mul(float4(StartPosition, 1.0f), ViewMatrix).xyz;
    float3 EndViewPosition = mul(float4(EndPosition, 1.0f), ViewMatrix).xyz;
    if (ClipLineSegmentToNearPlane(StartViewPosition, EndViewPosition, 0.001f) == false)
    {
        return;
    }

    const float4 StartClipPosition = mul(float4(StartViewPosition, 1.0f), ProjectionMatrix);
    const float4 EndClipPosition = mul(float4(EndViewPosition, 1.0f), ProjectionMatrix);
    if (abs(StartClipPosition.w) <= 0.00001f || abs(EndClipPosition.w) <= 0.00001f)
    {
        return;
    }

    const float2 StartNdcPosition = StartClipPosition.xy / StartClipPosition.w;
    const float2 EndNdcPosition = EndClipPosition.xy / EndClipPosition.w;
    const float2 NdcLineVector = EndNdcPosition - StartNdcPosition;
    const float NdcLineLength = length(NdcLineVector);
    if (NdcLineLength <= 0.00001f)
    {
        return;
    }

    const float2 NdcLineNormal = float2(-NdcLineVector.y, NdcLineVector.x) / NdcLineLength;
    const float2 NdcLineOffset = NdcLineNormal * (LineThickness * 0.5f);

    float4 StartPositiveClipPosition = StartClipPosition;
    float4 StartNegativeClipPosition = StartClipPosition;
    float4 EndPositiveClipPosition = EndClipPosition;
    float4 EndNegativeClipPosition = EndClipPosition;
    StartPositiveClipPosition.xy += NdcLineOffset * StartClipPosition.w;
    StartNegativeClipPosition.xy -= NdcLineOffset * StartClipPosition.w;
    EndPositiveClipPosition.xy += NdcLineOffset * EndClipPosition.w;
    EndNegativeClipPosition.xy -= NdcLineOffset * EndClipPosition.w;

    AppendClipVertexToTriangleStream(Stream, StartPositiveClipPosition, ColorValue);
    AppendClipVertexToTriangleStream(Stream, StartNegativeClipPosition, ColorValue);
    AppendClipVertexToTriangleStream(Stream, EndPositiveClipPosition, ColorValue);
    AppendClipVertexToTriangleStream(Stream, EndNegativeClipPosition, ColorValue);
    Stream.RestartStrip();
}

void AppendWireCube(inout TriangleStream<DebugGeometryGsOutput> Stream, float3 CenterValue, float3 ExtentsValue, float4 OrientationValue, float4 ColorValue, float LineThickness, float4x4 ViewMatrix, float4x4 ProjectionMatrix)
{
    const float3 LocalCorners[8] =
    {
        float3(-1.0f, -1.0f, -1.0f),
        float3(1.0f, -1.0f, -1.0f),
        float3(1.0f, 1.0f, -1.0f),
        float3(-1.0f, 1.0f, -1.0f),
        float3(-1.0f, -1.0f, 1.0f),
        float3(1.0f, -1.0f, 1.0f),
        float3(1.0f, 1.0f, 1.0f),
        float3(-1.0f, 1.0f, 1.0f)
    };

    const uint2 EdgePairs[12] =
    {
        uint2(0, 1), uint2(1, 2), uint2(2, 3), uint2(3, 0),
        uint2(4, 5), uint2(5, 6), uint2(6, 7), uint2(7, 4),
        uint2(0, 4), uint2(1, 5), uint2(2, 6), uint2(3, 7)
    };

    for (uint EdgeIndex = 0; EdgeIndex < 12; ++EdgeIndex)
    {
        const uint StartCornerIndex = EdgePairs[EdgeIndex].x;
        const uint EndCornerIndex = EdgePairs[EdgeIndex].y;
        const float3 LocalStart = RotateByQuaternion(LocalCorners[StartCornerIndex] * ExtentsValue, OrientationValue) + CenterValue;
        const float3 LocalEnd = RotateByQuaternion(LocalCorners[EndCornerIndex] * ExtentsValue, OrientationValue) + CenterValue;
        AppendLineQuad(Stream, LocalStart, LocalEnd, ColorValue, LineThickness, ViewMatrix, ProjectionMatrix);
    }
}

[maxvertexcount(48)]
void GsMain(point DebugGeometryVsOutput Input[1], inout TriangleStream<DebugGeometryGsOutput> Stream)
{
    StructuredBuffer<FrameGlobalsGpu> FrameGlobalsBuffer = ResourceDescriptorHeap[RootConstants.FrameGlobalsSrvIndex];
    StructuredBuffer<DebugGeometryContextGpu> DebugGeometryContextBuffer = ResourceDescriptorHeap[RootConstants.ModelContextSrvIndex];

    const FrameGlobalsGpu FrameGlobals = FrameGlobalsBuffer[RootConstants.FrameGlobalsElementIndex];
    const DebugGeometryContextGpu DebugGeometryContext = DebugGeometryContextBuffer[Input[0].InstanceId];
    const float4x4 ViewMatrix = transpose(FrameGlobals.View);
    const float4x4 ProjectionMatrix = transpose(FrameGlobals.Proj);
    const float LineThickness = max(DebugGeometryContext.LineThickness, MinimumDebugGeometryLineThickness);

    if (DebugGeometryContext.Type == DebugGeometryTypeLineValue)
    {
        AppendLineQuad(Stream, DebugGeometryContext.Parameter0.xyz, DebugGeometryContext.Parameter1.xyz, DebugGeometryContext.Color, LineThickness, ViewMatrix, ProjectionMatrix);
    }
    else if (DebugGeometryContext.Type == DebugGeometryTypeWireCubeValue)
    {
        AppendWireCube(Stream, DebugGeometryContext.Parameter0.xyz, max(DebugGeometryContext.Parameter1.xyz, float3(0.0001f, 0.0001f, 0.0001f)), DebugGeometryContext.Parameter2, DebugGeometryContext.Color, LineThickness, ViewMatrix, ProjectionMatrix);
    }
}

float4 PsMain(DebugGeometryGsOutput Input) : SV_TARGET
{
    return Input.Color;
}
