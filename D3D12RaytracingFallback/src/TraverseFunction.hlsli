//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#define FAST_PATH 1

#define INSTANCE_FLAG_NONE                              0x0
#define INSTANCE_FLAG_TRIANGLE_CULL_DISABLE             0x1
#define INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE   0x2
#define INSTANCE_FLAG_FORCE_OPAQUE                      0x4
#define INSTANCE_FLAG_FORCE_NON_OPAQUE                  0x8

struct SoftwareRayDesc
{
    float3 Origin;
    float TMin;
    float3 Direction;
    float TMax;
};

struct SoftwareHitData
{
    float2 Barycentrics;
    uint PrimitiveIndex;
    uint GeometryIndex;
    uint InstanceIndex;
    uint InstanceID;
    float t;
};

#define COMMITTED_NOTHING 0
#define COMMITTED_TRIANGLE_HIT 1

struct SoftwareRayQuery
{
    SoftwareRayDesc RayDesc;
    uint RayFlags;
    uint InstanceInclusionMask;
    uint GroupIndex;

    void TraceRayInline(uint inRayFlags, uint inInstanceInclusionMask, SoftwareRayDesc inRayDesc, uint inGroupIndex)
    {
        RayDesc = inRayDesc;
        RayFlags = inRayFlags;
        InstanceInclusionMask = inInstanceInclusionMask;
        CommittedHitData.t = PendingHitData.t = RayDesc.TMax;
        GroupIndex = inGroupIndex;
    }

    void Proceed();

    uint CommittedStatus()
    {
        return CommittedRayT() < RayDesc.TMax ? COMMITTED_TRIANGLE_HIT : COMMITTED_NOTHING;
    }

    uint CommittedGeometryIndex()
    {
        return CommittedHitData.GeometryIndex;
    }

    float2 CommittedTriangleBarycentrics()
    {
        return CommittedHitData.Barycentrics;
    }

    uint CommittedPrimitiveIndex()
    {
        return CommittedHitData.PrimitiveIndex;
    }

    uint CommittedInstanceIndex()
    {
        return CommittedHitData.InstanceIndex;
    }

    float CommittedRayT()
    {
        return CommittedHitData.t;
    }

    float TMin()
    {
        return RayDesc.TMin;
    }

    void SetPendingHitData(float2 Barycentrics, uint PrimitiveIndex, uint GeometryIndex, uint InstanceIndex, uint InstanceID, float t)
    {
        PendingHitData.Barycentrics = Barycentrics;
        PendingHitData.PrimitiveIndex = PrimitiveIndex;
        PendingHitData.InstanceIndex = InstanceIndex;
        PendingHitData.InstanceID = InstanceID;
        PendingHitData.t = t;
        PendingHitData.GeometryIndex = GeometryIndex;
    }

    void CommitHit()
    {
        CommittedHitData = PendingHitData;
    }

    void UpdateObjectProperties(
        float3 InObjectRayOrigin,
        float3 InObjectRayDirection,
        row_major float3x4 InWorldToObject,
        row_major float3x4 InObjectToWorld)
    {
        ObjectRayOrigin = InObjectRayOrigin;
        ObjectRayDirection = InObjectRayDirection;
        WorldToObject = InWorldToObject;
        ObjectToWorld = InObjectToWorld;
    }


    SoftwareHitData PendingHitData;
    SoftwareHitData CommittedHitData;

    float3 ObjectRayOrigin;
    float3 ObjectRayDirection;
    row_major float3x4 WorldToObject;
    row_major float3x4 ObjectToWorld;
};

#define END_SEARCH -1
#define IGNORE      0
#define ACCEPT      1

#define USE_GROUPSHARED_STACK 0

#if USE_GROUPSHARED_STACK
groupshared uint stack[TRAVERSAL_MAX_STACK_DEPTH * 64];
#else
static uint stack[TRAVERSAL_MAX_STACK_DEPTH];
#endif


uint GetStackIndex(uint stackTop)
{
#if USE_GROUPSHARED_STACK
    return GI * TRAVERSAL_MAX_STACK_DEPTH + stackTop;
#else
    return stackTop;
#endif
}

void StackPush(inout int stackTop, uint value, uint level, uint tidInWave)
{
    uint stackIndex = GetStackIndex(stackTop);
    stack[ stackIndex] = value;
    stackTop++;
}

void StackPush2(inout int stackTop, bool selector, uint valueA, uint valueB, uint level, uint tidInWave)
{
    const uint store0 = selector ? valueA : valueB;
    const uint store1 = selector ? valueB : valueA;
    const uint stackIndex0 = GetStackIndex(stackTop);
    const uint stackIndex1 = GetStackIndex(stackTop+1);
    stack[stackIndex0] = store0;
    stack[stackIndex1] = store1;

    stackTop += 2;
}

uint StackPop(inout int stackTop, out uint depth, uint tidInWave)
{
    --stackTop;
    uint stackIndex = GetStackIndex(stackTop);
    return stack[stackIndex];
}

bool IsOpaque(bool geomOpaque, uint instanceFlags, uint rayFlags)
{
    bool opaque = geomOpaque;

    if (instanceFlags & INSTANCE_FLAG_FORCE_OPAQUE)
        opaque = true;
    else if (instanceFlags & INSTANCE_FLAG_FORCE_NON_OPAQUE)
        opaque = false;

    if (rayFlags & RAY_FLAG_FORCE_OPAQUE)
        opaque = true;
    else if (rayFlags & RAY_FLAG_FORCE_NON_OPAQUE)
        opaque = false;

    return opaque;
}

//
// Ray/AABB intersection, separating axes theorem
//

inline
bool RayBoxTest(
    out float resultT,
    float closestT,
    float3 rayOriginTimesRayInverseDirection,
    float3 rayInverseDirection,
    float3 boxCenter,
    float3 boxHalfDim)
{
    const float3 relativeMiddle = boxCenter * rayInverseDirection - rayOriginTimesRayInverseDirection; // 3
    const float3 maxL = relativeMiddle + boxHalfDim * abs(rayInverseDirection); // 3
    const float3 minL = relativeMiddle - boxHalfDim * abs(rayInverseDirection); // 3

    const float minT = max(max(minL.x, minL.y), minL.z); // 1
    const float maxT = min(min(maxL.x, maxL.y), maxL.z); // 1

    resultT = max(minT, 0);
    return max(minT, 0) < min(maxT, closestT);
}

float3 Swizzle(float3 v, int3 swizzleOrder)
{
    return float3(v[swizzleOrder.x], v[swizzleOrder.y], v[swizzleOrder.z]);
}

bool IsPositive(float f) { return f > 0.0f; }

// Using Woop/Benthin/Wald 2013: "Watertight Ray/Triangle Intersection"
inline
void RayTriangleIntersect(
    inout float hitT,
    in uint rayFlags,
    in uint instanceFlags,
    out float2 bary,
    float3 rayOrigin,
    float3 rayDirection,
    int3 swizzledIndicies,
    float3 shear,
    float3 v0,
    float3 v1,
    float3 v2)
{
    // Woop Triangle Intersection
    bool useCulling = !(instanceFlags & D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE);
    bool flipFaces = instanceFlags & D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
    uint backFaceCullingFlag = flipFaces ? RAY_FLAG_CULL_FRONT_FACING_TRIANGLES : RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
    uint frontFaceCullingFlag = flipFaces ? RAY_FLAG_CULL_BACK_FACING_TRIANGLES : RAY_FLAG_CULL_FRONT_FACING_TRIANGLES;
    bool useBackfaceCulling = useCulling && (rayFlags & backFaceCullingFlag);
    bool useFrontfaceCulling = useCulling && (rayFlags & frontFaceCullingFlag);

    float3 A = Swizzle(v0 - rayOrigin, swizzledIndicies);
    float3 B = Swizzle(v1 - rayOrigin, swizzledIndicies);
    float3 C = Swizzle(v2 - rayOrigin, swizzledIndicies);

    A.xy = A.xy - shear.xy * A.z;
    B.xy = B.xy - shear.xy * B.z;
    C.xy = C.xy - shear.xy * C.z;
    precise float U = C.x * B.y - C.y * B.x;
    precise float V = A.x * C.y - A.y * C.x;
    precise float W = B.x * A.y - B.y * A.x;

    float det = U + V + W;
    if (useFrontfaceCulling)
    {
        if (U > 0.0f || V > 0.0f || W > 0.0f) return;
    }
    else if (useBackfaceCulling)
    {
        if (U < 0.0f || V < 0.0f || W < 0.0f) return;
    }
    else
    {
        if ((U < 0.0f || V < 0.0f || W < 0.0f) &&
            (U > 0.0f || V > 0.0f || W > 0.0f)) return;
    }

    if (det == 0.0f) return;
    A.z = shear.z * A.z;
    B.z = shear.z * B.z;
    C.z = shear.z * C.z;
    const float T = U * A.z + V * B.z + W * C.z;

    if (useFrontfaceCulling)
    {
        if (T > 0.0f || T < hitT * det)
            return;
    }
    else if (useBackfaceCulling)
    {
        if (T < 0.0f || T > hitT * det)
            return;
    }
    else
    {
        float signCorrectedT = abs(T);
        if (IsPositive(T) != IsPositive(det))
        {
            signCorrectedT = -signCorrectedT;
        }

        if (signCorrectedT < 0.0f || signCorrectedT > hitT * abs(det))
        {
            return;
        }
    }

    const float rcpDet = rcp(det);
    bary.x = V * rcpDet;
    bary.y = W * rcpDet;
    hitT = T * rcpDet;
}

#define MULTIPLE_LEAVES_PER_NODE 0
bool TestLeafNodeIntersections(
    RWByteAddressBufferPointer accelStruct,
    in int rayFlags,
    uint2 flags,
    uint instanceFlags,
    float3 rayOrigin,
    float3 rayDirection,
    float TMin,
    int3 swizzledIndicies,
    float3 shear,
    inout float2 resultBary,
    inout float resultT,
    inout uint resultTriId)
{
    // Intersect a bunch of triangles
    const uint firstId = flags.x & 0x00ffffff;
    const uint numTris = flags.y;

    // Unroll mildly, it'd be awesome if we had some helpers here to intersect.
    uint i = 0;
    bool bIsIntersect = false;
#if MULTIPLE_LEAVES_PER_NODE
    const uint evenTris = numTris & ~1;
    for (i = 0; i < evenTris; i += 2)
    {
        const uint id0 = firstId + i;

        const uint2 triIds = uint2(id0, id0 + 1);

        // Read 3 vertices
        // This is pumping too much via SQC
        float3 v00, v01, v02;
        float3 v10, v11, v12;
        BVHReadTriangle(accelStruct, v00, v01, v02, triIds.x);
        BVHReadTriangle(accelStruct, v10, v11, v12, triIds.y);

        // Intersect
        float2 bary0, bary1;
        float t0 = resultT;
        RayTriangleIntersect(
            t0,
            rayFlags,
            instanceFlags,
            bary0,
            rayOrigin,
            rayDirection,
            swizzledIndicies,
            shear,
            v00, v01, v02);

        float t1 = resultT;
        RayTriangleIntersect(
            t1,
            rayFlags,
            instanceFlags,
            bary1,
            rayOrigin,
            rayDirection,
            swizzledIndicies,
            shear,
            v10, v11, v12);

        // Record nearest
        if (t0 < resultT)
        {
            resultBary = bary0.xy;
            resultT = t0;
            resultTriId = triIds.x;
            bIsIntersect = true;
        }

        if (t1 < resultT)
        {
            resultBary = bary1.xy;
            resultT = t1;
            resultTriId = triIds.y;
            bIsIntersect = true;
        }
    }

    if (numTris & 1)
#endif
    {
        const uint triId0 = firstId + i;

        // Read 3 vertices
        float3 v0, v1, v2;
        BVHReadTriangle(accelStruct, v0, v1, v2, triId0);

        // Intersect
        float2  bary0;
        float t0 = resultT;
        RayTriangleIntersect(
            t0,
            rayFlags,
            instanceFlags,
            bary0,
            rayOrigin,
            rayDirection,
            swizzledIndicies,
            shear,
            v0, v1, v2);

        // Record nearest
        if (t0 < resultT && t0 > TMin)
        {
            resultBary = bary0.xy;
            resultT = t0;
            resultTriId = triId0;
            bIsIntersect = true;
        }
    }
    return bIsIntersect;
}

int GetIndexOfBiggestChannel(float3 vec)
{
    if (vec.x > vec.y && vec.x > vec.z)
    {
        return 0;
    }
    else if (vec.y > vec.z)
    {
        return 1;
    }
    else
    {
        return 2;
    }
}

void swap(inout int a, inout int b)
{
    int temp = a;
    a = b;
    b = temp;
}

#define TOP_LEVEL_INDEX 0
#define BOTTOM_LEVEL_INDEX 1
#define NUM_BVH_LEVELS 2

struct HitData
{
    uint ContributionToHitGroupIndex;
    uint PrimitiveIndex;
};

struct RayData
{
    // Precalculated Stuff for intersection tests
    float3 InverseDirection;
    float3 OriginTimesRayInverseDirection;
    float3 Shear;
    int3   SwizzledIndices;
};

RayData GetRayData(float3 rayOrigin, float3 rayDirection)
{
    RayData data;

    // Precompute stuff
    data.InverseDirection = rcp(rayDirection);
    data.OriginTimesRayInverseDirection = rayOrigin * data.InverseDirection;

    int zIndex = GetIndexOfBiggestChannel(abs(rayDirection));
    data.SwizzledIndices = int3(
        (zIndex + 1) % 3,
        (zIndex + 2) % 3,
        zIndex);

    if (rayDirection[data.SwizzledIndices.z] < 0.0f) swap(data.SwizzledIndices.x, data.SwizzledIndices.y);

    data.Shear = float3(
        rayDirection[data.SwizzledIndices.x] / rayDirection[data.SwizzledIndices.z],
        rayDirection[data.SwizzledIndices.y] / rayDirection[data.SwizzledIndices.z],
        1.0 / rayDirection[data.SwizzledIndices.z]);

    return data;
}

bool Cull(bool opaque, uint rayFlags)
{
    return (opaque && (rayFlags & RAY_FLAG_CULL_OPAQUE)) || (!opaque && (rayFlags & RAY_FLAG_CULL_NON_OPAQUE));
}

float ComputeCullFaceDir(uint instanceFlags, uint rayFlags)
{
    float cullFaceDir = 0;
    if (rayFlags & RAY_FLAG_CULL_FRONT_FACING_TRIANGLES)
        cullFaceDir = 1;
    else if (rayFlags & RAY_FLAG_CULL_BACK_FACING_TRIANGLES)
        cullFaceDir = -1;
    if (instanceFlags & INSTANCE_FLAG_TRIANGLE_CULL_DISABLE)
        cullFaceDir = 0;

    return cullFaceDir;
}

Declare_Fallback_SetPendingAttr(BuiltInTriangleIntersectionAttributes);

#define EndSearch 0x1
#define ProcessingBottomLevel 0x2

void SetBoolFlag(inout uint flagContainer, uint flag, bool enable)
{
    if (enable)
    {
        flagContainer |= flag;
    }
    else
    {
        flagContainer &= ~flag;
    }
}

bool GetBoolFlag(uint flagContainer, uint flag)
{
    return flagContainer & flag;
}

bool Traverse(
    inout SoftwareRayQuery SWRayQuery
)
{
    uint GI = SWRayQuery.GroupIndex;
    const GpuVA nullptr = GpuVA(0, 0);

    RayData currentRayData = GetRayData(SWRayQuery.RayDesc.Origin, SWRayQuery.RayDesc.Direction);

    uint flagContainer = 0;
    SetBoolFlag(flagContainer, ProcessingBottomLevel, FAST_PATH ? true : false);

    uint nodesToProcess[NUM_BVH_LEVELS];
    GpuVA currentGpuVA = TopLevelAccelerationStructureGpuVA;
    uint instanceIndex = 0;
    uint instanceFlags = 0;
    uint instanceOffset = 0;
    uint instanceId = 0;

    uint stackPointer = 0;

    const uint TopLevelIndex = FAST_PATH ? BOTTOM_LEVEL_INDEX : TOP_LEVEL_INDEX;
    nodesToProcess[TopLevelIndex] = 0;

    RWByteAddressBufferPointer topLevelAccelerationStructure = CreateRWByteAddressBufferPointerFromGpuVA(currentGpuVA);
        
    uint offsetToInstanceDescs = GetOffsetToInstanceDesc(topLevelAccelerationStructure);
    uint2 flags;
    float unusedT;
    BoundingBox topLevelBox = BVHReadBoundingBox(
        topLevelAccelerationStructure,
        0,
        flags);

    if (RayBoxTest(unusedT,
        SWRayQuery.CommittedRayT(),
        currentRayData.OriginTimesRayInverseDirection,
        currentRayData.InverseDirection,
        topLevelBox.center,
        topLevelBox.halfDim))
    {
        StackPush(stackPointer, 0, 0, GI);
        nodesToProcess[TopLevelIndex]++;
    }

    float closestBoxT = FLT_MAX;

    while (nodesToProcess[TopLevelIndex] != 0)
    {
        do
        {
            uint currentLevel;
            uint thisNodeIndex = StackPop(stackPointer, currentLevel, GI);
            nodesToProcess[GetBoolFlag(flagContainer, ProcessingBottomLevel)]--;

            RWByteAddressBufferPointer currentBVH = CreateRWByteAddressBufferPointerFromGpuVA(currentGpuVA);

            uint2 flags;
            BoundingBox box = BVHReadBoundingBox(
                currentBVH,
                thisNodeIndex,
                flags);

            {
                if (IsLeaf(flags))
                {
#if !FAST_PATH
                    if (!GetBoolFlag(flagContainer, ProcessingBottomLevel))
                    {
                        uint leafIndex = GetLeafIndexFromFlag(flags);
                        BVHMetadata metadata = GetBVHMetadataFromLeafIndex(
                            topLevelAccelerationStructure,
                            offsetToInstanceDescs,
                            leafIndex);
                        RaytracingInstanceDesc instanceDesc = metadata.instanceDesc;
                        instanceIndex = metadata.InstanceIndex;
                        instanceOffset = GetInstanceContributionToHitGroupIndex(instanceDesc);
                        instanceId = GetInstanceID(instanceDesc);

                        bool validInstance = GetInstanceMask(instanceDesc) & SWRayQuery.InstanceInclusionMask;
                        if (validInstance)
                        {
                            SetBoolFlag(flagContainer, ProcessingBottomLevel, true);
                            StackPush(stackPointer, 0, currentLevel + 1, GI);
                            currentGpuVA = instanceDesc.AccelerationStructure;
                            instanceFlags = GetInstanceFlags(instanceDesc);

                            float3x4 CurrentWorldToObject = CreateMatrix(instanceDesc.Transform);
                            float3x4 CurrentObjectToWorld = CreateMatrix(metadata.ObjectToWorld);

                            float3 objectSpaceOrigin = mul(CurrentWorldToObject, float4(SWRayQuery.RayDesc.Origin, 1));
                            float3 objectSpaceDirection = mul(CurrentWorldToObject, float4(SWRayQuery.RayDesc.Direction, 0));

                            currentRayData = GetRayData(
                                objectSpaceOrigin,
                                objectSpaceDirection);

                            SWRayQuery.UpdateObjectProperties(objectSpaceOrigin, objectSpaceDirection, CurrentWorldToObject, CurrentObjectToWorld);

                            nodesToProcess[BOTTOM_LEVEL_INDEX] = 1;
                        }
                    }
                    else // if it's a bottom level
#endif
                    {
                        RWByteAddressBufferPointer bottomLevelAccelerationStructure = CreateRWByteAddressBufferPointerFromGpuVA(currentGpuVA);
                        const uint leafIndex = GetLeafIndexFromFlag(flags);
                        PrimitiveMetaData primitiveMetadata = BVHReadPrimitiveMetaData(bottomLevelAccelerationStructure, leafIndex);

                        bool geomOpaque = primitiveMetadata.GeometryFlags & D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
                        bool opaque = IsOpaque(geomOpaque, instanceFlags, SWRayQuery.RayFlags);
                        bool culled = Cull(opaque, SWRayQuery.RayFlags);
                        
                        float resultT = SWRayQuery.CommittedRayT();
                        float2 resultBary;
                        uint resultTriId;

                        bool isProceduralGeometry = IsProceduralGeometry(flags);
                        bool endSearch = false;
#ifdef DISABLE_PROCEDURAL_GEOMETRY
                        isProceduralGeometry = false;
#endif
                        if (!culled && TestLeafNodeIntersections( // TODO: We need to break out this function so we can run anyhit on each triangle
                            currentBVH,
                            SWRayQuery.RayFlags,
                            flags,
                            instanceFlags,
#if FAST_PATH
                            SWRayQuery.RayDesc.Origin,
                            SWRayQuery.RayDesc.Direction,
#else
                            SWRayQuery.ObjectRayOrigin,
                            SWRayQuery.ObjectRayDirection,
#endif
                            SWRayQuery.TMin(),
                            currentRayData.SwizzledIndices,
                            currentRayData.Shear,
                            resultBary,
                            resultT,
                            resultTriId))
                        {
                            uint primIdx = primitiveMetadata.PrimitiveIndex;
                            uint hitKind = HIT_KIND_TRIANGLE_FRONT_FACE;

                            SWRayQuery.SetPendingHitData(resultBary, primIdx, primitiveMetadata.GeometryContributionToHitGroupIndex, instanceIndex, instanceId, resultT);
                            closestBoxT = min(closestBoxT, resultT);

#ifdef DISABLE_ANYHIT 
                            bool skipAnyHit = true;
#else
                            bool skipAnyHit = opaque;
#endif

                            if (skipAnyHit)
                            {
                                SWRayQuery.CommitHit();
                                SetBoolFlag(flagContainer, EndSearch, SWRayQuery.RayFlags & RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH);
                            }
                            else
                            {
                                int ret = ACCEPT;
                                // TODO return to app?
                                if (ret != IGNORE)
                                    SWRayQuery.CommitHit();

                                SetBoolFlag(flagContainer, EndSearch, (ret == END_SEARCH) || (SWRayQuery.RayFlags & RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH));
                            }
                        }
                        if (GetBoolFlag(flagContainer, EndSearch))
                        {
                            nodesToProcess[BOTTOM_LEVEL_INDEX] = 0;
                            nodesToProcess[TOP_LEVEL_INDEX] = 0;
                        }
                    }
                }
                else
                {
                    const uint leftChildIndex = GetLeftNodeIndex(flags);
                    const uint rightChildIndex = GetRightNodeIndex(flags);

                    float resultT = SWRayQuery.CommittedRayT();
                    uint2 flags;
                    float leftT, rightT;
                    BoundingBox leftBox = BVHReadBoundingBox(
                        currentBVH,
                        leftChildIndex,
                        flags);

                    BoundingBox rightBox = BVHReadBoundingBox(
                        currentBVH,
                        rightChildIndex,
                        flags);

                    bool leftTest = RayBoxTest(
                        leftT,
                        resultT,
                        currentRayData.OriginTimesRayInverseDirection,
                        currentRayData.InverseDirection,
                        leftBox.center,
                        leftBox.halfDim);

                    bool rightTest = RayBoxTest(
                        rightT,
                        resultT,
                        currentRayData.OriginTimesRayInverseDirection,
                        currentRayData.InverseDirection,
                        rightBox.center,
                        rightBox.halfDim);

                    bool isBottomLevel = GetBoolFlag(flagContainer, ProcessingBottomLevel);
                    if (leftTest && rightTest)
                    {
                        // If equal, traverse the left side first since it's encoded to have less triangles
                        bool traverseRightSideFirst = rightT < leftT;
                        StackPush2(stackPointer, traverseRightSideFirst, leftChildIndex, rightChildIndex, currentLevel + 1, GI);
                        nodesToProcess[isBottomLevel] += 2;
                    }
                    else if (leftTest || rightTest)
                    {
                        StackPush(stackPointer, rightTest ? rightChildIndex : leftChildIndex, currentLevel + 1, GI);
                        nodesToProcess[isBottomLevel] += 1;
                    }
                }
            }
        } while (nodesToProcess[GetBoolFlag(flagContainer, ProcessingBottomLevel)] != 0);

#if !FAST_PATH
        SetBoolFlag(flagContainer, ProcessingBottomLevel, false);
#endif
        currentRayData = GetRayData(SWRayQuery.RayDesc.Origin, SWRayQuery.RayDesc.Direction);
        currentGpuVA = TopLevelAccelerationStructureGpuVA;
    } 
    bool isHit = SWRayQuery.CommittedRayT() < SWRayQuery.RayDesc.TMax;

    return isHit;   
}

void SoftwareRayQuery::Proceed()
{
    Traverse(this);
}
