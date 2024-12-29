#define HLSL

#include "VisualizeRaysSharedShaderStructs.h"
#include "GLSLCompat.h"
#include "VisualizationRaysCommon.h"

ByteAddressBuffer VisualizationRaysBuffer : register(t0);
Texture2D WorldPositionTexture : register(t1);
RWTexture2D<float4> OutputColor : register(u0);

cbuffer VisualizationRaysConstants
{
    VisualizationRaysConstants Constants;
}

int GetVisualizationRayCount()
{
    return VisualizationRaysBuffer.Load(0);
}

void GetVisualizationRay(in int Index, out float3 Origin, out float3 Direction, out float HitT, out float BounceCount)
{
    int OffsetToVisualizationRay = GetOffsetToVisualizationRay(Index);
    uint4 data0 = VisualizationRaysBuffer.Load4(OffsetToVisualizationRay + 0);
    uint4 data1 = VisualizationRaysBuffer.Load4(OffsetToVisualizationRay + 16);

    Origin = asfloat(data0.xyz);
    Direction = asfloat(uint3(data0.w, data1.xy));
    HitT = asfloat(data1.z);
    BounceCount = asfloat(data1.w);
}

// https://www.shadertoy.com/view/4lcSRn
vec4 iCylinder(in vec3 ro, in vec3 rd, in vec3 pa, in vec3 pb, float ra)
{
    vec3  ba = pb - pa;
    vec3  oc = ro - pa;

    float baba = dot(ba, ba);
    float bard = dot(ba, rd);
    float baoc = dot(ba, oc);

    float k2 = baba - bard * bard;
    float k1 = baba * dot(oc, rd) - baoc * bard;
    float k0 = baba * dot(oc, oc) - baoc * baoc - ra * ra * baba;

    /*
    // in case you really need to handle parallel raycasts
    if( k2==0.0 )
    {
        float ta = -dot(ro-pa,ba)/bard;
        float tb = ta + baba/bard;

        vec4 pt = (bard>0.0) ? vec4(pa,-ta) : vec4(pb,tb);

        vec3 q = ro + rd*abs(pt.w) - pt.xyz;
        if( dot(q,q)>ra*ra ) return vec4(-1.0);

        return vec4( abs(pt.w), sign(pt.w)*ba/sqrt(baba) );
    }
    */

    float h = k1 * k1 - k2 * k0;
    if (h < 0.0) return vec4(-1, -1, -1, -1);
    h = sqrt(h);
    float t = (-k1 - h) / k2;

    // body
    float y = baoc + t * bard;
    if (y > 0.0 && y < baba) return vec4(t, (oc + t * rd - ba * y / baba) / ra);

    // caps
    t = (((y < 0.0) ? 0.0 : baba) - baoc) / bard;
    if (abs(k1 + k2 * t) < h) return vec4(t, ba * sign(y) / sqrt(baba));

    return vec4(-1, -1, -1, -1);
}

// cylinder normal at point p
vec3 nCylinder(in vec3 p, in vec3 a, in vec3 b, in float ra)
{
    // body
    vec3  pa = p - a;
    vec3  ba = b - a;
    float baba = dot(ba, ba);
    float paba = dot(pa, ba);
    return (pa - ba * paba / baba) / ra;
}

float dot2(in vec3 v) { return dot(v, v); }
float inversesqrt(float x) { return 1.0f / sqrt(x); }
vec4 iCappedCone(in vec3  ro, in vec3  rd,
    in vec3  pa, in vec3  pb,
    in float ra, in float rb)
{
    vec3  ba = pb - pa;
    vec3  oa = ro - pa;
    vec3  ob = ro - pb;

    float m0 = dot(ba, ba);
    float m1 = dot(oa, ba);
    float m2 = dot(ob, ba);
    float m3 = dot(rd, ba);

    //caps
    if (m1 < 0.0) { if (dot2(oa * m3 - rd * m1) < (ra * ra * m3 * m3)) return vec4(-m1 / m3, -ba * inversesqrt(m0)); }
    else if (m2 > 0.0) { if (dot2(ob * m3 - rd * m2) < (rb * rb * m3 * m3)) return vec4(-m2 / m3, ba * inversesqrt(m0)); }

    // body
    float m4 = dot(rd, oa);
    float m5 = dot(oa, oa);
    float rr = ra - rb;
    float hy = m0 + rr * rr;

    float k2 = m0 * m0 - m3 * m3 * hy;
    float k1 = m0 * m0 * m4 - m1 * m3 * hy + m0 * ra * (rr * m3 * 1.0);
    float k0 = m0 * m0 * m5 - m1 * m1 * hy + m0 * ra * (rr * m1 * 2.0 - m0 * ra);

    float h = k1 * k1 - k2 * k0;
    if (h < 0.0) return vec4(-1, -1, -1, -1);

    float t = (-k1 - sqrt(h)) / k2;

    float y = m1 + t * m3;
    if (y > 0.0 && y < m0)
    {
        return vec4(t, normalize(m0 * (m0 * (oa + t * rd) + rr * ba * ra) - ba * hy * y));
    }

    return vec4(-1, -1, -1, -1);
}

[numthreads(VISUALIZE_RAYS_THREAD_GROUP_WIDTH, VISUALIZE_RAYS_THREAD_GROUP_HEIGHT, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float4 SceneColor = OutputColor[DTid.xy];

    float2 dispatchUV = float2(DTid.xy) / float2(Constants.Resolution);
    float2 uv = vec2(0, 1) + dispatchUV * vec2(1, -1);

    float aspectRatio = float(Constants.Resolution.x) / float(Constants.Resolution.y);
    vec3 focalPoint = Constants.CameraPosition - Constants.FocalLength * normalize(Constants.CameraLookAt - Constants.CameraPosition);
    float lensWidth = Constants.LensHeight * aspectRatio;

    float3 RayPosition = Constants.CameraPosition;
    RayPosition += Constants.CameraRight * (uv.x * 2.0 - 1.0) * lensWidth / 2.0;
    RayPosition += Constants.CameraUp * (uv.y * 2.0 - 1.0) * Constants.LensHeight / 2.0;

    float3 RayDirection = normalize(RayPosition - focalPoint);

    float RayHitT = -1.0f;
    float3 WorldPosition = WorldPositionTexture[DTid.xy];
    if (any(abs(WorldPosition) > 0.0001f))
    {
        RayHitT = length(WorldPositionTexture[DTid.xy].xyz - RayPosition);
    }

    float4 VisualizerColor = float4(0, 0, 0, 0);
    float closestVisualizerT = 9999999.9f;
    float3 cylinderNormal = float3(0, 1, 0);
    int VisualizerRayCount = min(GetVisualizationRayCount(), Constants.RayCount);
    for (int RayIndex = 0; RayIndex < VisualizerRayCount; RayIndex++)
    {
        float3 VisualizerRayOrigin, VisualizerRayDirection;
        float VisualizerRayT, VisualizerRayBounceCount;

        GetVisualizationRay(RayIndex, VisualizerRayOrigin, VisualizerRayDirection, VisualizerRayT, VisualizerRayBounceCount);

        float3 CurrentColor = float3(0, 0, 0);
        // NEE ray
        if (VisualizerRayBounceCount < 0.0f)
        {
            CurrentColor = float3(0, 0, 1);
        }
        else if (VisualizerRayBounceCount <= 1.1f)
        {
            CurrentColor = float3(0, 1, 0);
        }
        else
        {
            CurrentColor = float3(1, 1, 0);
        }

        float3 CylinderStart = VisualizerRayOrigin;
        float CylinderLength = VisualizerRayT > 0.0 ? VisualizerRayT : 9999999.9f;

        VisualizerRayBounceCount = abs(VisualizerRayBounceCount);
        if (VisualizerRayBounceCount > Constants.RayDepth)
        {
            continue;
        }
        else
        {
            float CylinderLengthFraction = min(1.0, Constants.RayDepth - VisualizerRayBounceCount);
            CylinderLength *= CylinderLengthFraction;
        }
        

        float3 CylinderEnd = CylinderStart + VisualizerRayDirection * CylinderLength;
        float4 result = iCylinder(RayPosition, RayDirection, CylinderStart, CylinderEnd, Constants.CylinderRadius);

        if (result.x > 0.0f && result.x < closestVisualizerT && (RayHitT < 0.0f || result.x < RayHitT))
        {
            closestVisualizerT = result.x;

            cylinderNormal = nCylinder(RayPosition + RayDirection * result.x, CylinderStart, CylinderEnd, Constants.CylinderRadius);
            VisualizerColor.rgb = lerp(float3(0.2, 0.2, 0.2), CurrentColor, abs(dot(cylinderNormal, RayDirection)));
            VisualizerColor.a = 0.75f;
        }

        if (VisualizerRayT > 0.0f)
        {
            result = iCappedCone(RayPosition, RayDirection, CylinderStart + CylinderLength * 0.8 * VisualizerRayDirection, CylinderEnd, Constants.CylinderRadius * 10.0f, Constants.CylinderRadius);
            if (result.x > 0.0f && result.x < closestVisualizerT && (RayHitT < 0.0f || result.x < RayHitT))
            {
                closestVisualizerT = result.x;

                //cylinderNormal = nCylinder(RayPosition + RayDirection * result.x, CylinderStart, CylinderEnd, Constants.CylinderRadius);
                VisualizerColor.rgb = CurrentColor;// lerp(float3(0.2, 0.2, 0.2), CurrentColor, abs(dot(cylinderNormal, RayDirection)));
                VisualizerColor.a = 0.75f;
            }
        }
    }

    OutputColor[DTid.xy] = float4(lerp(SceneColor.rgb, VisualizerColor.rgb, VisualizerColor.a), SceneColor.a);
}