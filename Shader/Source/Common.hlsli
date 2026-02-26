struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
};

struct VertexOutput
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
};

struct FrameGlobalsGpu
{
    float4x4 View;
    float4x4 Proj;
    float4x4 ViewProj;
    float4x4 PrevViewProj;
    float Dt;
    uint FrameIndex;
    uint Flags;
    uint Pad0;
};

struct ModelContextGpu
{
    float4x4 World;
    float4x4 PrevWorld;
    float4 BbCenter;
    float4 BbExtents;
    uint Flags;
    uint BoneIndexStart;
    uint ObjectId;
    uint Pad0;
    float4 Custom0;
    float4 Custom1;
};

struct DrawRecordGpu
{
    uint ObjectIndex;
    uint MaterialIndex;
    uint Flags;
    uint Pad0;
};

struct RootConstantsB1
{
    uint FrameGlobalsSrvIndex;
    uint ModelContextSrvIndex;
    uint DrawRecordSrvIndex;
    uint DrawRecordBaseIndex;
    float4 TintColor;
    uint Reserved0;
    uint Reserved1;
    uint Reserved2;
    uint Reserved3;
};

float4 ApplyBaseColor(float4 Color)
{
    return saturate(Color);
}
