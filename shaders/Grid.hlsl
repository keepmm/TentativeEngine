// 地面（y = 0）の無限グリッド。ModelViewer の Grid.hlsl を移植したもの。
// 頂点バッファは使わず、画面全体を覆う 2 枚の三角形を描き、
// ピクセルごとに「カメラからの視線が地面と交わる点」を求めて、そこに格子を描く
#include "Common.hlsli"

struct GridVSOutput
{
    float4 position                  : SV_Position;
    VK_LOCATION(0) float3 nearPoint  : TEXCOORD0;  // このピクセルの視線の、ニア面上の点（ワールド）
    VK_LOCATION(1) float3 farPoint   : TEXCOORD1;  // 同じく、ファー面上の点
};

struct GridPSOutput
{
    float4 color : SV_Target;
    float  depth : SV_Depth;  // 板そのものではなく、交点の深度を書く（物体との前後関係を正しくするため）
};

// 画面全体を覆う 2 枚の三角形（NDC）。SV_VertexID で 0〜5 を引く
static const float2 kCorners[6] =
{
    float2(-1.0f, -1.0f), float2(-1.0f,  1.0f), float2( 1.0f,  1.0f),
    float2(-1.0f, -1.0f), float2( 1.0f,  1.0f), float2( 1.0f, -1.0f),
};

// 画面上の点（NDC の x, y と深度 z）をワールド座標に戻す
float3 Unproject(float2 ndc, float z)
{
    const float4 p = mul(float4(ndc, z, 1.0f), g_InverseViewProjection);
    return p.xyz / p.w;
}

GridVSOutput VSMain(uint vertexID : SV_VertexID)
{
    const float2 ndc = kCorners[vertexID];
    GridVSOutput output;
    output.position  = float4(ndc, 0.0f, 1.0f);
    output.nearPoint = Unproject(ndc, 0.0f);  // D3D の深度は 0（手前）〜 1（奥）
    output.farPoint  = Unproject(ndc, 0.5f);
    return output;
}

// 格子の線の濃さ（線の上で 1、線から 1 ピクセル以上離れると 0）。
// derivative は「1 ピクセル進むとワールドで何 m 進むか」。これで割ると、距離をピクセル単位で測れる
float GridLine(float2 coord, float2 derivative)
{
    const float2 grid = abs(frac(coord - 0.5f) - 0.5f) / max(derivative, float2(1e-4f, 1e-4f));
    return 1.0f - min(min(grid.x, grid.y), 1.0f);
}

GridPSOutput PSMain(GridVSOutput input)
{
    // ---- 視線と地面の交点 ----
    const float3 ray = input.farPoint - input.nearPoint;
    // 地面と平行な視線（ray.y が 0）では割り算できないので、0 にならない値で割っておく。捨てるかどうかは下で決める
    const float rayY = abs(ray.y) < 1e-5f ? 1e-5f : ray.y;
    const float t = -input.nearPoint.y / rayY;
    const float3 worldPosition = input.nearPoint + t * ray;

    // fwidth（隣のピクセルとの差）は discard より前に求める。
    // 元の ModelViewer は discard の後で求めていて、隣のピクセルが捨てられていると値が決まらなかった
    const float2 coord = worldPosition.xz;
    const float2 derivative1  = fwidth(coord);
    const float2 derivative10 = fwidth(coord * 0.1f);

    // 地平線より上（視線が地面に届かない）は描かない
    if (abs(ray.y) < 1e-5f || t <= 0.0f)
    {
        discard;
    }
    const float4 clip = mul(float4(worldPosition, 1.0f), g_ViewProjection);
    const float depth = clip.z / clip.w;
    if (depth < 0.0f || depth > 1.0f)
    {
        discard;
    }

    // ---- 色 ----
    // 1m ごとに薄い線、10m ごとに濃い線
    const float line1  = GridLine(coord, derivative1);
    const float line10 = GridLine(coord * 0.1f, derivative10);
    float4 color = float4(0.45f, 0.45f, 0.45f, line1 * 0.3f);
    color = lerp(color, float4(0.7f, 0.7f, 0.7f, 0.6f), line10);

    // 軸の強調（X 軸 = z が 0 の線を赤、Z 軸 = x が 0 の線を青）。幅は 1.5 ピクセル
    const float axisWidth = 1.5f;
    const float toAxisX = abs(worldPosition.z) / max(derivative1.y, 1e-4f);
    const float toAxisZ = abs(worldPosition.x) / max(derivative1.x, 1e-4f);
    if (toAxisX < axisWidth)
    {
        color = lerp(color, float4(0.85f, 0.2f, 0.2f, 1.0f), 1.0f - toAxisX / axisWidth);
    }
    else if (toAxisZ < axisWidth)
    {
        color = lerp(color, float4(0.2f, 0.45f, 0.9f, 1.0f), 1.0f - toAxisZ / axisWidth);
    }

    // 遠くほど薄くする。遠くの格子は 1 ピクセルに何本も入ってちらつくため。
    // カメラが高いほど遠くまで見せる（ModelViewer はカメラの距離を C++ から渡していたが、高さで代わりにする）
    const float maxDistance = max(abs(g_CameraPosition.y) * 15.0f, 100.0f);
    const float distance = length(worldPosition - g_CameraPosition.xyz);
    color.a *= saturate((maxDistance - distance) / (maxDistance * 0.5f));
    if (color.a < 0.01f)
    {
        discard;  // 透明なピクセルで深度を書かない
    }

    GridPSOutput output;
    output.color = color;
    output.depth = depth;
    return output;
}
