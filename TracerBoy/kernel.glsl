#define MAX_BOUNCES 6
#define EPSILON 0.000001
#define PI 3.1415926535
#define LARGE_NUMBER 1e20
#define SMALL_NUMBER 0.01
#define AIR_IOR 1.0
#define PLASTIC_IOR 1.46f
#define FLOOR_IOR 1.0
#define MARCH_SIZE 3.0f
#define MIN_ROUGHNESS 0.04
#define MIN_ROUGHNESS_SQUARED (MIN_ROUGHNESS * MIN_ROUGHNESS)

#ifndef IS_SHADER_TOY
#define IS_SHADER_TOY 1
#endif

#if IS_SHADER_TOY
#define SCENE_TRACERBOY_BENCHMARK 0
#define SCENE_CORNELL_BOX 1
#define SELECTED_SCENE SCENE_CORNELL_BOX

#define GLOBAL
bool IsTargettingRealTime() { return false; }
float GetTime() { return iTime; }
vec4 GetMouse() { return iMouse; }
vec3 GetResolution() { return iResolution; }
vec3 mul(vec3 v, mat3 m) { return v * m; }
vec4 GetAccumulatedColor(vec2 uv) { return texture(iChannel0, uv); }
vec4 GetLastFrameData() { return texture(iChannel0, vec2(0.0)); }
void OutputPrimaryEmissive(vec3 Emissive) {}
void OutputPrimaryAlbedo(vec3 Albedo, float DiffuseContribution) {}
void OutputPrimaryNormal(vec3 Normal) {}
void OutputPrimaryWorldPosition(vec3 WorldPosition, float DistanceToNeighbor) {}

float saturate(float x) { return clamp(x, 0.0, 1.0); }
#endif

GLOBAL float seed = 0.;
float rand() { return fract(sin(seed++ + GetTime())*43758.5453123); }

#if IS_SHADER_TOY
float hash13(vec3 p3)
{
	p3  = fract(p3 * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float GetRotationFactor()
{
    if(GetMouse().x <= 0.0)
    {
        // Default value when shader is initially loaded up
        return 0.5f;
    }
    
    return GetMouse().x / GetResolution().x;
}

float GetLastFrameRotationFactor(vec4 lastFrameData)
{
    return fract(lastFrameData.a);
}

float GetLastFrameCount(vec4 lastFrameData)
{
    return lastFrameData.x;
}

bool areFloatsEqual(float a, float b)
{
    return a + EPSILON > b && a - EPSILON < b;
}

bool HasCameraMoved(float lastFrameRotationFactor, float rotationFactor)
{
    return !areFloatsEqual(lastFrameRotationFactor, rotationFactor);
}

bool ShouldInvalidateHistory()
{
    float rotationFactor = GetRotationFactor();
    
    vec4 lastFrameData = GetLastFrameData();
    return HasCameraMoved(GetLastFrameRotationFactor(lastFrameData), rotationFactor);
}

struct Ray
{ 
    vec3 origin; 
    vec3 direction; 
};

struct BlueNoiseData
{
	vec2 PrimaryJitter;
	vec2 SecondaryRayDirection;
	vec2 AreaLightJitter;
	vec2 DOFJitter;
};

BlueNoiseData GetBlueNoise()
{
	BlueNoiseData data;

	data.PrimaryJitter = vec2(rand(), rand());
	data.SecondaryRayDirection = vec2(rand(), rand());
	data.AreaLightJitter = vec2(rand(), rand());
	data.DOFJitter = vec2(rand(), rand());

	return data;
}

vec3 SampleEnvironmentMap(vec3 v)
{
#if SELECTED_SCENE == SCENE_CORNELL_BOX
    return vec3(0, 0, 0);
#else
    const float EnvironmentLightMultipier = 0.05;
	return EnvironmentLightMultipier * texture(iChannel1, v).xyz;
#endif
}

#define DEFAULT_MATERIAL_FLAG 0x0
#define METALLIC_MATERIAL_FLAG 0x1
#define SUBSURFACE_SCATTER_MATERIAL_FLAG 0x2
#define NO_SPECULAR_MATERIAL_FLAG 0x4
#define LIGHT_MATERIAL_FLAG 0x8
    
struct Material
{
    vec3 albedo;
    float IOR;
    
    float roughness;
    vec3 emissive;

    float absorption;    
    float scattering;
    float SpecularCoef;
    int Flags;
};

vec3 GetDetailNormal(Material mat, vec3 normal, vec3 tangent, vec2 uv)
{
    // Not supported for ShaderToy
    return normal;
}
#endif 

Ray NewRay(vec3 origin, vec3 direction)
{
	Ray r;
	r.origin = origin;
	r.direction = direction;
	return r;
}
    
struct CameraDescription
{
    vec3 position;
    vec3 lookAt;    
    vec3 up;
    vec3 right;

    float lensHeight;
    float focalDistance;
};

bool AllowsSpecular(Material material)
{
    return (material.Flags & NO_SPECULAR_MATERIAL_FLAG) == 0;
}
    
bool IsMetallic(Material material)
{
    return (material.Flags & METALLIC_MATERIAL_FLAG) != 0;
}

bool IsSubsurfaceScattering(Material material)
{
    return (material.Flags & SUBSURFACE_SCATTER_MATERIAL_FLAG) != 0;
}

bool IsHairMaterial(Material material)
{
    return (material.Flags & HAIR_MATERIAL_FLAG) != 0;
}

bool IsLight(Material material)
{
    return (material.Flags & LIGHT_MATERIAL_FLAG) != 0;
}

bool UsePerfectSpecularOptimization(float roughness)
{
    return roughness < 0.05f;
}
    
Material NewMaterial(vec3 albedo, float IOR, float roughness, vec3 emissive, float absorption, float scattering, int flags)
{
	Material m;
	m.albedo = albedo;
	m.IOR = IOR;
	m.roughness = roughness;
	m.emissive = emissive;
	m.absorption = absorption;
	m.scattering = scattering;
    m.SpecularCoef = 0.0;
	m.Flags = flags;
	return m;
}
    
Material DiffuseMaterial(vec3 albedo)
{
    return NewMaterial(albedo, AIR_IOR, 0.5, vec3(0.0, 0.0, 0.0), 0.0, 0.0, DEFAULT_MATERIAL_FLAG);
}

Material SubsurfaceScatterMaterial(vec3 albedo, float roughness, float IOR, float absorption, float transmittance)
{
    return NewMaterial(albedo, IOR, roughness, vec3(0.0, 0.0, 0.0), absorption, transmittance, SUBSURFACE_SCATTER_MATERIAL_FLAG);
}

Material EmissiveMaterial(vec3 albedo, vec3 emissive)
{
    return NewMaterial(albedo, AIR_IOR, 0.5, emissive, 0.0, 0.0, DEFAULT_MATERIAL_FLAG);
}

Material PlasticMaterial(vec3 albedo)
{
    return NewMaterial(albedo, PLASTIC_IOR, 0.2,  vec3(0.0, 0.0, 0.0), 0.0, 0.0, DEFAULT_MATERIAL_FLAG);
}

Material LightMaterial(vec3 albedo)
{
    return NewMaterial(vec3(0.0, 0.0, 0.0), PLASTIC_IOR, 0.2,  albedo, 0.0, 0.0, LIGHT_MATERIAL_FLAG);
}

Material NormalMaterial(vec3 color, float IOR, float roughness)
{
    return NewMaterial(color, IOR, roughness, vec3(0.0, 0.0, 0.0), 0.0, 0.0, DEFAULT_MATERIAL_FLAG);
}

Material MetalMaterial(vec3 color, float IOR, float roughness)
{
    return NewMaterial(color, IOR, roughness, vec3(0.0, 0.0, 0.0), 0.0, 0.0, METALLIC_MATERIAL_FLAG);
}

Material ReflectiveMaterial()
{
    return MetalMaterial(vec3(1.0, 1.0, 1.0), 3.5, 0.0);
}

Material MatteMaterial(vec3 color)
{
    return NewMaterial(color, 1.0, 0.0, vec3(0.0, 0.0, 0.0), 0.0, 0.0, NO_SPECULAR_MATERIAL_FLAG);
}

struct BoundedPlane 
{
    vec3 origin;
    vec3 normal;
    vec3 Axis1;
    vec3 Axis2;
    int materialID;
};    

BoundedPlane NewBoundedPlane(vec3 origin, vec3 normal, vec3 Axis1, vec3 Axis2, int materialID)
{
	BoundedPlane b;
	b.origin = origin;
	b.normal = normal;
	b.Axis1 = Axis1;
	b.Axis2 = Axis2;
	b.materialID = materialID;
	return b;
}

  
struct Box 
{
    vec3 origin;
    vec3 Axis1;
    vec3 Axis2;
    vec3 Axis3;
    int materialID;
};
    
struct Sphere
{
    vec3 origin;
    float radius;
    int materialID;
};

Sphere NewSphere(vec3 origin, float radius, int materialID)
{
	Sphere s;
	s.origin = origin;
	s.radius = radius;
	s.materialID = materialID;
	return s;
}

GLOBAL float LightPositionYOffset = 0.0;

vec3 GetRayPoint(Ray ray, float t)
{
    return ray.origin + ray.direction * t;
}

float BoundedPlaneIntersection(Ray ray, BoundedPlane plane)
{
      float denom = dot(plane.normal, ray.direction);
      if(abs(denom) > 0.0)
      {
          float t =  dot(plane.origin - ray.origin, plane.normal) / denom;
          vec3 planeToIntersection = GetRayPoint(ray, t) - plane.origin;
          float axis1Length =  length(plane.Axis1);          
          float axis2Length =  length(plane.Axis2);

          if(abs(dot(planeToIntersection, plane.Axis1)) / axis1Length < axis1Length &&
             abs(dot(planeToIntersection, plane.Axis2)) / axis2Length < axis2Length)
          {
              return t;
          }
          
      }
      return -1.0;
}

bool ObjectIsCloser(float newParametricDistance, float oldParametricDistance)
{
    return newParametricDistance > 0.0 && newParametricDistance < oldParametricDistance;
}

float SphereIntersection(Ray ray, Sphere sphere, out vec3 normal)
{
      vec3 eMinusC = ray.origin - sphere.origin;
      float dDotD = dot(ray.direction, ray.direction);

      float discriminant = dot(ray.direction, (eMinusC)) * dot(ray.direction, (eMinusC))
         - dDotD * (dot(eMinusC, eMinusC) - sphere.radius * sphere.radius);

      // If the ray doesn't intersect
      if (discriminant < 0.0) 
         return -1.0;

      float firstIntersect = (dot(-ray.direction, eMinusC) - sqrt(discriminant))
             / dDotD;
      
      float t = firstIntersect;
    
      // If the ray is inside the sphere
      if (firstIntersect < EPSILON) {
         t = (dot(-ray.direction, eMinusC) + sqrt(discriminant))
             / dDotD;
      }
    
      normal = normalize(GetRayPoint(ray, t) - sphere.origin);
      return t;
}

float BoxIntersection(Ray ray, Box box, out vec3 normal)
{
    BoundedPlane boxPlanes[6];

    boxPlanes[0] =  NewBoundedPlane(
            box.origin + box.Axis1, 
            normalize(box.Axis1),
            box.Axis2, 
            box.Axis3, 
            0);
    
    boxPlanes[1] = boxPlanes[0];
    boxPlanes[1].origin = box.origin - box.Axis1;
    boxPlanes[1].normal = -boxPlanes[1].normal;
    
    boxPlanes[2] =  NewBoundedPlane(
            box.origin + box.Axis2, 
            normalize(box.Axis2),
            box.Axis1, 
            box.Axis3, 
            0);
    
    boxPlanes[3] = boxPlanes[2];
    boxPlanes[3].origin = box.origin - box.Axis2;
    boxPlanes[3].normal = -boxPlanes[2].normal;
    
    boxPlanes[4] =  NewBoundedPlane(
        box.origin - box.Axis3 , 
        -normalize(box.Axis3),
        box.Axis1, 
        box.Axis2, 
        0);
    
    
    boxPlanes[5] = boxPlanes[4];
    boxPlanes[5].origin = box.origin + box.Axis3;
    boxPlanes[5].normal = -boxPlanes[4].normal;
    
    float t = 999999.9f;
    
    for (int i = 0; i < 6; i++)
    {    
        float newT = BoundedPlaneIntersection(ray, boxPlanes[i]);
        if(ObjectIsCloser(newT, t))
        {
            t = newT;
            normal = boxPlanes[i].normal;
        }
    }
           
    return t;
}

bool RayAABIntersect(
    out float entryT,
    out float exitT,
    float closestT,
    Ray ray,
    vec3 boxCenter,
    vec3 boxHalfDim)
{
    vec3 rayInverseDirection = 1.0 / (ray.direction);
    vec3 rayOriginTimesRayInverseDirection = ray.origin * rayInverseDirection;

    vec3 relativeMiddle = boxCenter * rayInverseDirection - rayOriginTimesRayInverseDirection;
    vec3 maxL = relativeMiddle + boxHalfDim * abs(rayInverseDirection);
    vec3 minL = relativeMiddle - boxHalfDim * abs(rayInverseDirection);

    float minT = max(max(minL.x, minL.y), minL.z);
    float maxT = min(min(maxL.x, maxL.y), maxL.z);

    entryT = max(minT, 0.0);
    exitT = min(maxT, closestT);
    return entryT < exitT;
}

float FresnelFactor(
    float CurrentIOR,
    float NewIOR,
    vec3 Normal,
    vec3 RayDirection)
{
    float ReflectionCoefficient = 
        ((CurrentIOR - NewIOR) / (CurrentIOR + NewIOR)) *
        ((CurrentIOR - NewIOR) / (CurrentIOR + NewIOR));
    return ReflectionCoefficient + (1.0 - ReflectionCoefficient) * pow(1.0 - dot(Normal, -RayDirection), 5.0); 
}

float BlinnPhongNormalDistributionFunction(
    vec3 Normal,
    vec3 HalfVector,
    float RoughnessSquared)
{
    float RoughnessPow4 = RoughnessSquared * RoughnessSquared;
    float nDotH = dot(Normal, HalfVector);
    
	float numerator = pow(nDotH, (2.0 / RoughnessPow4) - 2.0);
    float denominator = PI * RoughnessPow4;
    return numerator / denominator;
}

float GGXNormalDistributionFunction(
    vec3 Normal,
    vec3 HalfVector,
    float RoughnessSquared)
{
   RoughnessSquared = max(RoughnessSquared, MIN_ROUGHNESS_SQUARED);
   float a2 = RoughnessSquared * RoughnessSquared;
   float nDotH = dot(Normal, HalfVector);
   float Numerator = a2;
   float Denominator = PI * pow(nDotH * nDotH * (a2 - 1.0) + 1.0, 2.0);
    
   return Numerator / Denominator;
}

float NormalDistributionFunction(
    vec3 Normal,
    vec3 HalfVector,
    float RoughnessSquared)
{
   return BlinnPhongNormalDistributionFunction(Normal, HalfVector, RoughnessSquared);  
}

float ImplicitGeometricShadowing(
    float nDotV,    
    float nDotL)
{
    return nDotL * nDotV;
}

float GGXGeometricShadowing(
    float nDotV,
    float RoughnessSquared)
{
    float Numerator = 2.0 * nDotV;
    float Denominator = nDotV + 
        sqrt(RoughnessSquared * RoughnessSquared + (1.0 - RoughnessSquared * RoughnessSquared) * nDotV * nDotV);
    return Numerator / Denominator;
}

float GeometricShadowing(
    float nDotV,    
    float nDotL,
    float RoughnessSquared)
{
    return ImplicitGeometricShadowing(
        nDotV,
        nDotL);
}

float SpecularBRDF(
    vec3 ViewDirection,
    vec3 LightDirection,
    vec3 Normal,
    float Roughness)
{
    float RoughnessSquared = Roughness * Roughness;
    vec3 HalfVector = normalize(ViewDirection + LightDirection);
    
    float nDotV = dot(Normal, ViewDirection);
    float nDotL = dot(Normal, LightDirection);
    
    if(nDotV < SMALL_NUMBER || nDotL < SMALL_NUMBER)
    {
        return 0.0;
    }
    
    // Note that fresnel is omitted because this is already accounted for 
    // when calculating whether the chance whether a ray should be specular or
    // diffuse
    float Numerator = NormalDistributionFunction(Normal, HalfVector, RoughnessSquared)  *
                      GeometricShadowing(nDotV, nDotL, RoughnessSquared);
    float Denominator = (4.0 * nDotL * nDotV);
    return Numerator / Denominator;
}

float DiffuseBRDF(
    vec3 LightDirection,
    vec3 Normal)
{
	return max(dot(LightDirection, Normal), 0.0f) / PI;
}

bool IsFloatZero(float f)
{
    return f >= -EPSILON && f <= EPSILON;
}

bool IsVectorZero(vec3 v)
{
    return IsFloatZero(v.x) && IsFloatZero(v.y) && IsFloatZero(v.z);
}

// Based off of MicrofacetTransmission::f from
// https://github.com/mmp/pbrt-v3/blob/master/src/core/reflection.cpp
float SpecularBTDF(    
    vec3 IncomingDirection,
    float IncomingIOR,
    vec3 OutgoingDirection,
	float OutgoingIOR,
	vec3 Normal,
	float Roughness)
{
    bool IsInsidePrimitive = dot(OutgoingDirection, Normal) > 0.0;
	float RoughnessSquared = Roughness * Roughness;
    
    float nDotIncoming = dot(IncomingDirection, Normal);
    float nDotOutgoing = dot(OutgoingDirection, Normal); 
    
    if(IsFloatZero(nDotIncoming) || IsFloatZero(nDotOutgoing))
    {
        return 0.0;
    }
    
    // Need to handle this case properly
    if(IncomingIOR == OutgoingIOR)
    {
        return 1.0;
    }
    
    float eta = IsInsidePrimitive ? (OutgoingIOR / IncomingIOR) : (IncomingIOR / OutgoingIOR);
    vec3 halfVector = OutgoingDirection + IncomingDirection * eta;
    
    if (IsVectorZero(halfVector))
    {
        vec3 axis1 = cross(OutgoingDirection, vec3(0, 1, 0));
        halfVector = cross(axis1, OutgoingDirection); 
    }
    halfVector = normalize(halfVector);
    
    if (dot(halfVector, Normal) < 0.0)
    {
        halfVector = -halfVector;
    }

    
    float oDotH = dot(OutgoingDirection, halfVector);

    float factor = 1.0;
    float sqrtDenom = dot(OutgoingDirection, halfVector) + eta * dot(IncomingDirection, halfVector);

    return 1.0;
    return abs(NormalDistributionFunction(Normal, halfVector, RoughnessSquared) * 
              GeometricShadowing(nDotIncoming, nDotOutgoing, RoughnessSquared) *
               eta * eta *
               abs(dot(IncomingDirection, halfVector)) * abs(dot(OutgoingDirection, halfVector)) * factor * factor /
                    (nDotIncoming * nDotOutgoing * sqrtDenom * sqrtDenom));
}

#define INVALID_MATERIAL_ID -1
#if SELECTED_SCENE == SCENE_TRACERBOY_BENCHMARK
#define DEFAULT_WALL_MATERIAL_ID 0
#define BRONZE_MATERIAL_ID 1
#define BLUE_PLASTIC_MATERIAL_ID 2
#define MIRROR_MATERIAL_ID 3
#define REFRACTIVE_MATERIAL_ID 4
#define ROUGH_MIRROR_MATERIAL_ID 5
#define RADIOACTIVE_MATERIAL_ID 6
#define WAX_MATERIAL_ID 7
#define GLASS_MATERIAL_ID 8
#define ICE_MATERIAL_ID 9
#define GOLD_MATERIAL_ID 10
#define AREA_LIGHT_MATERIAL_ID 11
#define NUM_MATERIALS 12

#elif SELECTED_SCENE == SCENE_CORNELL_BOX
#define DEFAULT_WALL_MATERIAL_ID 0
#define LEFT_WALL_MATERIAL_ID 1
#define RIGHT_WALL_MATERIAL_ID 2
#define AREA_LIGHT_MATERIAL_ID 3
#define NUM_MATERIALS 4
#endif


// Materials with custom functions
#define FLOOR_MATERIAL_ID 32
#define WOOD_MATERIAL_ID 33
#define GLASS_PEBBLE_MATERIAL_ID 34
#define CHECKER_MATERIAL_ID 35
#define CUSTOM_SCATTERING_MATERIAL_ID 36


#if SELECTED_SCENE == SCENE_TRACERBOY_BENCHMARK
#define numBoundedPlanes  2
#define AreaLightIndex (numBoundedPlanes - 1)
#define numBoxes 1
#define numSpheres 19
#elif SELECTED_SCENE == SCENE_CORNELL_BOX
#define numBoundedPlanes  6
#define AreaLightIndex (numBoundedPlanes - 1)
#define numBoxes 2
#define numSpheres 0
#endif

struct Scene 
{
    CameraDescription camera;    
    BoundedPlane BoundedPlanes[numBoundedPlanes];
    Box Boxes[numBoxes];
#if numSpheres > 0
    Sphere Spheres[numSpheres];
#endif
};

#if IS_SHADER_TOY
// Cornell Box
Scene CurrentScene = Scene(
#if SELECTED_SCENE == SCENE_TRACERBOY_BENCHMARK
    CameraDescription(
        vec3(0.0, 1.3, 1.8), // position
        vec3(0.0, 1.0, 0.0), // lookAt
        vec3(0.0, 1.0, 0.0), // up
        vec3(1.0, 0.0, 0.0), // right
        2.0,                 // lensHeight
        3.5                  // focalDistance 
    ),
    
    // Scene Geometry
    BoundedPlane[numBoundedPlanes](
       BoundedPlane(vec3(0, 0, 0), vec3(0, 1, 0), vec3(10,0,0), vec3(0,0,10), FLOOR_MATERIAL_ID), // Bottom wall
       BoundedPlane(vec3(0.0, 2.0, 0.0), vec3(0, -1, 0), vec3(0.5,0,0), vec3(0,0,.5), AREA_LIGHT_MATERIAL_ID)
    ),
        
   Box[numBoxes](
       Box(vec3(0.0, 0.6, -1.5), vec3(0, 0.6, 0), vec3(-0.285, 0., 0.09), vec3(-0.09, 0., -0.29), DEFAULT_WALL_MATERIAL_ID)
   ),
   
    Sphere[numSpheres](
        // Front Row
        Sphere(vec3(2.0, 0.4,  0.5), 0.4f, ROUGH_MIRROR_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4,  0.5), 0.4f, ICE_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  0.5), 0.4f, WOOD_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4, 0.5), 0.4f, REFRACTIVE_MATERIAL_ID),
        Sphere(vec3(-2.0, 0.4, 0.5), 0.4f, GLASS_PEBBLE_MATERIAL_ID),
        
        // Second Row
        Sphere(vec3(-2.0, 0.4, -1.5), 0.4f, GLASS_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4, -1.5), 0.4f, CHECKER_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4,  -1.5), 0.4f, BLUE_PLASTIC_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4,  -1.5), 0.4f, MIRROR_MATERIAL_ID),
        
        // Third Row
        Sphere(vec3(2.0, 0.4,  -3.5), 0.4f, RADIOACTIVE_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4,  -3.5), 0.4f, GLASS_PEBBLE_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  -3.5), 0.4f, WAX_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4, -3.5), 0.4f, DEFAULT_WALL_MATERIAL_ID),
        Sphere(vec3(-2.0, 0.4, -3.5), 0.4f, CHECKER_MATERIAL_ID),
        
        // Fourth Row
        Sphere(vec3(2.0, 0.4,  -5.5), 0.4f, DEFAULT_WALL_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4,  -5.5), 0.4f, WOOD_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  -5.5), 0.4f, ROUGH_MIRROR_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4, -5.5), 0.4f, GOLD_MATERIAL_ID),
        Sphere(vec3(-2.0, 0.4, -5.5), 0.4f, ROUGH_MIRROR_MATERIAL_ID)
    )
#elif SELECTED_SCENE == SCENE_CORNELL_BOX
    CameraDescription(
        vec3(0.0, 1.0, 0.97), // position
        vec3(0.0, 1.0, 0.0), // lookAt
        vec3(0.0, 1.0, 0.0), // up
        vec3(1.0, 0.0, 0.0), // right
        2.0,                 // lensHeight
        5.819                // focalDistance
    ),
    
    // Scene Geometry
    BoundedPlane[numBoundedPlanes](
       BoundedPlane(vec3(-1, 1, 0), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), LEFT_WALL_MATERIAL_ID),
       BoundedPlane(vec3(1, 1, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), RIGHT_WALL_MATERIAL_ID), // Right wall
       BoundedPlane(vec3(0, 1, -1), vec3(0, 0, 1), vec3(1, 0, 0), vec3(0,1,0),DEFAULT_WALL_MATERIAL_ID), // Back wall 
       BoundedPlane(vec3(0, 2, 0), vec3(0, -1, 0), vec3(1,0,0), vec3(0,0,1), DEFAULT_WALL_MATERIAL_ID), // Top wall
       BoundedPlane(vec3(0, 0, 0), vec3(0, 1, 0), vec3(1,0,0), vec3(0,0,1), DEFAULT_WALL_MATERIAL_ID), // Bottom wall
       BoundedPlane(vec3(-0.005, 1.98, 0.085), vec3(0, -1, 0), vec3(.235,0,0), vec3(0,0,.19), AREA_LIGHT_MATERIAL_ID)
    ),
        
   Box[numBoxes](
       Box(vec3(0.3275, 0.3, 0.3275), vec3(0, 0.3, 0), vec3(0.2875, 0., 0.0875), vec3(0.0875, 0.0, -0.2875), DEFAULT_WALL_MATERIAL_ID),
       Box(vec3(-0.335, 0.6, -.29), vec3(0, 0.6, 0), vec3(-0.285, 0., 0.09), vec3(-0.09, 0., -0.29), DEFAULT_WALL_MATERIAL_ID)
   )
#endif
);

vec3 GetCameraPosition() { return CurrentScene.camera.position; }
vec3 GetCameraLookAt() { return CurrentScene.camera.lookAt; }
vec3 GetCameraUp() { return CurrentScene.camera.up; }
vec3 GetCameraRight() { return CurrentScene.camera.right; }
float GetCameraLensHeight() { return CurrentScene.camera.lensHeight; }
float GetCameraFocalDistance() { return CurrentScene.camera.focalDistance; }

float atan2(float x, float y)
{
    if(x > 0.0) return atan(y/x);
    else if(x < 0.0 && y >= 0.0) return atan(y/x) + PI;
    else if(x < 0.0 && y < 0.0) return atan(y/x) - PI;
    return 0.0;   
}

void GetSphereAttributes(
    in vec3 worldPosition,
    in Sphere sphere,
    out vec2 uv)
{
    vec3 objectSpacePosition = (worldPosition - sphere.origin) / sphere.radius;
    uv = vec2(acos(objectSpacePosition.y) / PI, (atan2(objectSpacePosition.z, objectSpacePosition.x) + PI / 2.0) / PI);
}
    
void GetPrimitiveAttributes(
    in vec3 worldPosition,
    in uint primitiveID,
    out vec2 uv) 
{
    uint PrimitiveIDIterator = 0u;
    
#if numSpheres > 0
    if(primitiveID < PrimitiveIDIterator + uint(numSpheres))
    {
        uint SphereIndex = primitiveID - PrimitiveIDIterator;
        GetSphereAttributes(worldPosition, 
                            CurrentScene.Spheres[SphereIndex],
                            uv);
        return;
    }
    PrimitiveIDIterator += uint(numSpheres);
#endif

    if(primitiveID < PrimitiveIDIterator + uint(numBoundedPlanes))
    {
        // not supporting UVs for bounded planes
        return;
    }
    PrimitiveIDIterator += uint(numBoundedPlanes);
    
    if(primitiveID < PrimitiveIDIterator + uint(numBoxes))
    {
        // not supporting UVs for boxes
        return;
    }
    PrimitiveIDIterator += uint(numBoxes);
}

// TODO: Consolidate with GetFloorMaterial
Material GetCheckerMaterial(uint PrimitiveID, vec3 WorldPosition)
{
    vec2 uv;
    GetPrimitiveAttributes(WorldPosition, PrimitiveID, uv);
    // Checker board pattern 
    
    const float ScaleMultiplier = 4.0;
    
    float IOR = 2.0;
    float roughness = 0.0;
    Material Mat0 = NormalMaterial(vec3(0.725, .1, .1), IOR, roughness);
    Material Mat1 = NormalMaterial(vec3(0.1, .1, .1), IOR, roughness);
    if(uint(uv.x * ScaleMultiplier) % 2u == 0u)
    {
        if(uint(uv.y * ScaleMultiplier) % 2u == 0u)
        {
            return Mat0;
        }
    }
    else
    {
        if(uint(uv.y * ScaleMultiplier) % 2u == 1u)
        {
            return Mat0;
        }
    }
    return Mat1;
}


Material GetGlassPebbleMaterial(uint PrimitiveID, vec3 WorldPosition)
{
    vec2 uv;
    GetPrimitiveAttributes(WorldPosition, PrimitiveID, uv);
    
    vec3 albedo = texture(iChannel3, uv.yx).rgb;
    albedo = albedo.bgr;
    return SubsurfaceScatterMaterial(albedo, 0.35, 1.05, 3.0, 0.0);
}


Material GetFloorMaterial(vec3 WorldPosition)
{
    // Checker board pattern 
    
    // Use this to scale the size of the checker board tiling
    const float ScaleMultiplier = 0.01;
    
    float IOR = 1.5;
    float roughness = 1.0;
    Material Mat0 = MatteMaterial(vec3(0.725, .71, .68));
    Material Mat1 = MatteMaterial(vec3(0.325, .35, .25));
    if(uint(fract(WorldPosition.x * ScaleMultiplier) * 10.0) % 2u == 0u)
    {
        if(uint(fract(WorldPosition.z * ScaleMultiplier) * 10.0) % 2u == 0u)
        {
            return Mat0;
        }
    }
    else
    {
        if(uint(fract(WorldPosition.z * ScaleMultiplier) * 10.0) % 2u == 1u)
        {
            return Mat0;
        }
    }
    return Mat1;
}

Material GetWoodMaterial(uint PrimitiveID, vec3 WorldPosition)
{
    vec2 uv;
    GetPrimitiveAttributes(WorldPosition, PrimitiveID, uv);
    
    float IOR = 1.0;
    float roughness = 0.5;
    vec3 albedo = texture(iChannel2, uv.yx).rgb;
    return NormalMaterial(albedo * albedo, IOR, roughness);
}

Material GetCustomScatteringMaterial(uint PrimitiveID)
{
    float absorptionLerpValue = float(PrimitiveID % 5u) / 4.0; 
    float absorption = mix(0.0, 10.0, absorptionLerpValue);
    
    float scatteringLerpValue = float(PrimitiveID / 5u) / 4.0; 
    float scattering = mix(0.0, 3.0, scatteringLerpValue);
    
    return SubsurfaceScatterMaterial(vec3(1.0, 0.6, 0.6), 0.0, 1.05, absorption, scattering);
}

Material GetMaterialInternal(int MaterialID, uint PrimitiveID, vec3 WorldPosition, vec2 uv, bool IsBacksideOfGeometry)
{
    if(MaterialID == FLOOR_MATERIAL_ID)
    {
        return GetFloorMaterial(WorldPosition);
    }
    else if(MaterialID == CHECKER_MATERIAL_ID)
    {
        return GetCheckerMaterial(PrimitiveID, WorldPosition);
    }
    if(MaterialID == WOOD_MATERIAL_ID || MaterialID == GLASS_PEBBLE_MATERIAL_ID)
    {
        return NormalMaterial(vec3(0.5, 0.5, 0.5), 0.5, 0.5);
    }
    if(MaterialID == CUSTOM_SCATTERING_MATERIAL_ID)
    {
		return GetCustomScatteringMaterial(PrimitiveID);
    }
    
    Material materials[NUM_MATERIALS];
#if SELECTED_SCENE == SCENE_TRACERBOY_BENCHMARK
    materials[WAX_MATERIAL_ID] = SubsurfaceScatterMaterial(vec3(0.725, .1, .1), 0.2, 1.05, 0.2, 5.0);
    materials[DEFAULT_WALL_MATERIAL_ID] = NormalMaterial(vec3(0.9, 0.9, 0.9), 2.2, 0.001);
    materials[BRONZE_MATERIAL_ID] = MetalMaterial(vec3(0.55, .2, .075), 1.18, 0.1);
    materials[GOLD_MATERIAL_ID] = MetalMaterial(vec3(0.65, .5, .075), 1.18, 0.15);
    materials[BLUE_PLASTIC_MATERIAL_ID] = PlasticMaterial(vec3(.05, .05, .55));
    materials[RADIOACTIVE_MATERIAL_ID] = EmissiveMaterial(vec3(.05, .45, .05), vec3(0.0, .1, 0.0));
    materials[MIRROR_MATERIAL_ID] = ReflectiveMaterial();    
    materials[ROUGH_MIRROR_MATERIAL_ID] = MetalMaterial(vec3(1.0, 1.0, 1.0), 1.5, 0.5);
    materials[REFRACTIVE_MATERIAL_ID] = SubsurfaceScatterMaterial(vec3(1.0, 1.0, 1.0), 0.0, 1.5, 0.0, 0.0);
    
    materials[ICE_MATERIAL_ID] = SubsurfaceScatterMaterial(vec3(0.65, 0.65, 0.8), 0.3, 1.1, 0.1, 0.2); // ice
    
    materials[GLASS_MATERIAL_ID] = SubsurfaceScatterMaterial(vec3(1.0, 0.6, 0.6), 0.0, 1.05, 0.1, 0.0);
    materials[AREA_LIGHT_MATERIAL_ID] = LightMaterial(vec3(0.45, 0.45, 0.45));
#elif SELECTED_SCENE == SCENE_CORNELL_BOX
    materials[DEFAULT_WALL_MATERIAL_ID] = MatteMaterial(vec3(0.725, .71, .68));
    materials[LEFT_WALL_MATERIAL_ID] = MatteMaterial(vec3(0.63, .065, .05));
    materials[RIGHT_WALL_MATERIAL_ID] = MatteMaterial(vec3(.14, .45, .091));
    materials[AREA_LIGHT_MATERIAL_ID] = LightMaterial(vec3(17.0, 12.0, 4.0));
#endif
    
    return materials[MaterialID];
}

// For most material purposes, GetMaterial can be used. This should be used for 
// accurate albedo information. Abusing this function can result in skyrocketing 
// shader compile times
Material GetMaterialWithTextures(int MaterialID, uint PrimitiveID, vec3 WorldPosition, vec2 uv)
{
	if(MaterialID == WOOD_MATERIAL_ID)
    {
        return GetWoodMaterial(PrimitiveID, WorldPosition);
    }
    else if(MaterialID == GLASS_PEBBLE_MATERIAL_ID)
    {
        return GetGlassPebbleMaterial(PrimitiveID, WorldPosition);
    }
    
    return GetMaterialInternal(MaterialID, PrimitiveID, WorldPosition, uv, false);
} 

Material GetAreaLightMaterial()
{
    return GetMaterialInternal(AREA_LIGHT_MATERIAL_ID, 0u, vec3(0.0, 0.0, 0.0), vec2(0.0, 0.0), false);
}

void GetOneLightSample(out vec3 LightPosition, out vec3 LightColor, out float PDFValue, out vec3 LightNormal)
{
#if 0
    vec2 areaLightUV = vec2(rand() * 2.0 - 1.0, rand() * 2.0 - 1.0);
    LightPosition = CurrentScene.BoundedPlanes[AreaLightIndex].origin +
        CurrentScene.BoundedPlanes[AreaLightIndex].Axis1 * areaLightUV.x +
        CurrentScene.BoundedPlanes[AreaLightIndex].Axis2 * areaLightUV.y;
    LightNormal = CurrentScene.BoundedPlanes[AreaLightIndex].normal;
    LightColor = GetAreaLightMaterial().albedo;
	PDFValue = 1.0;
#else
	LightPosition = vec3(-0.05, 1.98, -0.03);
    LightNormal = vec3(0, -1, 0);
	vec2 LightHalfDimension = vec2(0.235, 0.19);
	BlueNoiseData BlueNoise = GetBlueNoise();
	LightPosition.xz += vec2(BlueNoise.AreaLightJitter.x * 2.0 - 1.0, BlueNoise.AreaLightJitter.y * 2.0 - 1.0) * LightHalfDimension;

	float LightSurfaceArea = LightHalfDimension.x * 2.0 * LightHalfDimension.y * 2.0;

	LightColor = vec3(17.0, 12.0, 4.0);
	PDFValue = 1.0 / LightSurfaceArea;
#endif
}
#else
GLOBAL Scene CurrentScene;
#endif

vec3 GenerateRandomDirection()
{ 
    // Uniform hemisphere sampling from: http://www.rorydriscoll.com/2009/01/07/better-sampling/
    float u1 = rand(), u2 = rand();
    float r = sqrt(1.0 - u1 * u1);
    float phi = 2.0 * 3.14 * u2;
 
    return vec3(cos(phi) * r, sin(phi) * r, u1);
}

vec3 ReorientVectorAroundNormal(vec3 v, vec3 normal)
{
    vec3 tangent;
    if(abs(normal.x) > abs(normal.y))
    {
        tangent = vec3(-normal.z, 0, normal.x) / sqrt(normal.x * normal.x + normal.z * normal.z);
    }
    else
    {
        tangent = vec3(0, normal.z, -normal.y) / sqrt(normal.y * normal.y + normal.z * normal.z);
    }

    vec3 bitangent = cross(normal, tangent);
    return normalize(v.x * tangent + v.y * normal + v.z * bitangent);
}

vec3 GenerateRandomDirection(vec3 normal)
{ 
    float theta = PI * 2.0f * rand();
    float u     = rand();
    float r     = sqrt(1.0 - u * u);
	return ReorientVectorAroundNormal(vec3(r * cos(theta), u, r * sin(theta)), normal);
}

vec3 GenerateCosineWeightedDirection(float rand0, float rand1, out float pdfValue)
{
    float r = sqrt(rand0);
    float theta = 2.0 * PI * rand1;
 
    float x = r * cos(theta);
	float y = sqrt(max(EPSILON, 1.0 - rand0));
    float z = r * sin(theta);
    pdfValue = y / PI;
    return vec3(x, y, z);
}

vec3 GenerateCosineWeightedDirection(vec3 normal, float rand0, float rand1, out float pdfValue)
{
    vec3 direction = GenerateCosineWeightedDirection(rand0, rand1, pdfValue);
	return ReorientVectorAroundNormal(direction, normal);
}

vec3 GenerateRandomCosineWeightedDirection(vec3 normal, out float pdfValue)
{
	return GenerateCosineWeightedDirection(normal, rand(), rand(), pdfValue);
}

vec3 GenerateImportanceSampledDirection(vec3 normal, float roughness, float rand0, float rand1, out float PDFValue)
{ 
    float lobeMultiplier = pow(1.0 - roughness, 5.0) * 1000.0;
        
    float u1 = rand0, u2 = rand1;
    float theta = 2.0 * PI * u2;
    float phi = acos(sqrt(pow(u1, 1.0/(lobeMultiplier + 1.0))));
 
    vec3 direction = vec3(
        sin(phi) * cos(theta),
        cos(phi),
        sin(phi) * sin(theta));
    
    PDFValue = (lobeMultiplier + 1.0) * pow(cos(phi), lobeMultiplier) / (2.0 * PI);
    
	return ReorientVectorAroundNormal(direction, normal);
}

vec3 ImportanceSampleGGX(vec3 incomingRay, vec3 normal, float roughness)
{ 
    roughness = max(MIN_ROUGHNESS, roughness);
    float a = roughness * roughness;
    float a2 = a * a;
    float u1 = rand(), u2 = rand();
    float theta = 2.0 * PI * u2;
    float phi = acos(sqrt((1.0 - u1) / ((a2 - 1.0) * u1 + 1.0)));

    vec3 direction = vec3(
        sin(phi) * cos(theta),
        cos(phi),
        sin(phi) * sin(theta));
    
	vec3 GGXSampledNormal = ReorientVectorAroundNormal(direction, normal);
    return reflect(incomingRay, GGXSampledNormal);
}

float ImportanceSampleGGXPDF(vec3 normal, vec3 outgoingRay, vec3 halfVector, float roughness)
{
    roughness = max(MIN_ROUGHNESS, roughness);
    float a = roughness * roughness;
    float a2 = a * a;
    float cosTheta = abs(dot(normal, halfVector));
    float exp = (a2 - 1.0) * cosTheta*cosTheta + 1.0;
    if((exp) <= 0.0) return LARGE_NUMBER;
    float d = a2 / (PI * exp * exp);
    return d * abs(dot(halfVector, normal)) / (4.0 * abs(dot(outgoingRay, halfVector)));
}

vec3 GenerateRandomImportanceSampledDirection(vec3 normal, float roughness, out float PDFValue)
{ 
    return GenerateImportanceSampledDirection(normal, roughness, rand(), rand(), PDFValue);
}

#if IS_SHADER_TOY
vec2 IntersectWithMaxDistance(Ray ray, float maxT, out vec3 normal, out vec3 tangent, out vec2 uv, out uint PrimitiveID)
{
    float t = 999999.0;
    float materialID = float(INVALID_MATERIAL_ID);

    uint PrimitiveIDIterator = 0u;    
    PrimitiveID = uint(-1);
    float intersect;
	uv = vec2(0.0, 0.0);
    
#if numSpheres > 0
    for (int i = 0; i < numSpheres; i++)
    {
        vec3 sphereNormal;
        intersect = SphereIntersection(ray, CurrentScene.Spheres[i], sphereNormal);
        if(intersect > 0.0 && t > intersect)
        {
            t = intersect;
            materialID = float(CurrentScene.Spheres[i].materialID);
            normal = sphereNormal;
            PrimitiveID = PrimitiveIDIterator;
        }
        PrimitiveIDIterator++;
    }
#endif
    
    for (int i = 0; i < numBoundedPlanes; i++)
    {
        intersect = BoundedPlaneIntersection(ray, CurrentScene.BoundedPlanes[i]);
        if(intersect > 0.0 && t > intersect)
        {
            t = intersect;
            materialID = float(CurrentScene.BoundedPlanes[i].materialID);
            normal = CurrentScene.BoundedPlanes[i].normal;
            PrimitiveID = PrimitiveIDIterator;
        }
        PrimitiveIDIterator++;
    }
    
    for (int i = 0; i < numBoxes; i++)
    {
        vec3 boxNormal;
        intersect = BoxIntersection(ray, CurrentScene.Boxes[i], boxNormal);
        if(intersect > 0.0 && t > intersect)
        {
            t = intersect;
            materialID = float(CurrentScene.Boxes[i].materialID);
            normal = boxNormal;
            PrimitiveID = PrimitiveIDIterator;
        }
        PrimitiveIDIterator++;
    }

    return vec2(t, materialID);
}
#endif
vec2 Intersect(Ray ray, out vec3 normal, out vec3 tangent, out vec2 uv, out uint PrimitiveID)
{
	vec2 result = IntersectWithMaxDistance(ray, 999999.0, normal, tangent, uv, PrimitiveID);
    return result;
}


struct Intersection
{
    int MaterialID;
    vec3 Position;
    vec3 Normal;
};
    
// Hack that treats the "Normal" as a union for the missed ray direction
// if the ray missed
void SetMissedRayDirection(inout Intersection intersection, vec3 Direction)
{
    // Assumes MaterialID == INVALID_MATERIAL_ID
    intersection.Normal = Direction;
}
    
vec3 GetMissedRayDirection(Intersection intersection)
{
    // Assumes MaterialID == INVALID_MATERIAL_ID
    return intersection.Normal;
}

float BDSF_InverseCDF(float scatteringDirectionFactor)
{
    float t = (1.0 - scatteringDirectionFactor * scatteringDirectionFactor) /
        (1.0 - scatteringDirectionFactor + 2.0 * scatteringDirectionFactor * rand());
	return ((1.0 + scatteringDirectionFactor * scatteringDirectionFactor) - t * t) / 
        (2.0 * scatteringDirectionFactor);
}

float BSDF_PDF(float cosTheta, float scatteringDirectionFactor)
{
    return 0.25 * (1.0 - scatteringDirectionFactor * scatteringDirectionFactor) /
        (PI * pow(1.0 + scatteringDirectionFactor * scatteringDirectionFactor - 2.0 * scatteringDirectionFactor * cosTheta, 1.5));
}

// Anisotropic BSDF taken from https://graphics.pixar.com/library/ProductionVolumeRendering/paper.pdf
vec3 GenerateNewDirectionFromBSDF(vec3 RayDirection, float scatteringDirectionFactor, out float pdfValue)
{

    // Isotropic
    if(abs(scatteringDirectionFactor) < EPSILON)
    {
		pdfValue = 1.0; // TODO: Wrong
		return GenerateRandomDirection();
    }
    else
    {
        float phi = rand() * 2.0 * PI;
        float cosTheta = BDSF_InverseCDF(scatteringDirectionFactor);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        
        vec3 up = cross(RayDirection, vec3(0.0, 1.0, 0.0));
        vec3 right = cross(RayDirection, up);
            
        pdfValue = BSDF_PDF(cosTheta, scatteringDirectionFactor);
        return sinTheta * sin(phi) * up + sinTheta * cos(phi) * right + cosTheta * RayDirection;
    }
}

void ArtistFriendlyAlbdeoToAbsorption(vec3 color, vec3 mfp, out vec3 absorption, out vec3 scattering)
{
    //return 1.0 / color;
    // Practical and Controllable Subsurface Scattering for Production Path Tracing by Burley
    vec3 alpha = 1.0f - exp(-5.09406*color + 2.61188*color*color - 4.31805*color*color*color);
    vec3 s = 1.9 - color + 3.5 * (color - 0.8) * (color - 0.8);
    vec3 transmission = 1.0 / (s * mfp);
    scattering = transmission * alpha;
    absorption = transmission - scattering;
}

Material GetMaterial(int MaterialID, uint PrimitiveID, float3 WorldPosition, float2 uv, bool IsBacksideOfGeometry)
{
    Material material = GetMaterialInternal(MaterialID, PrimitiveID, WorldPosition, uv, IsBacksideOfGeometry);
    if(IsSubsurfaceScattering(material) && any(material.albedo) > 0.0f)
    {
        // Subsurface scattering media doesn't technically have an "albedo" but some materials
        // use this as an artist friendly way of specifying absorption so handle the conversion here
        ArtistFriendlyAlbdeoToAbsorption(material.albedo, 1.0 / material.scattering, material.absorption, material.scattering);
        material.albedo = float3(0, 0, 0);
    }
    return material;
}

float BeerLambert(float absorption, float dist)
{
    return exp(-absorption * dist);
}

float AbsPow(float x, float y)
{
    return pow(abs(x), y);
}

vec3 GetHalfVectorSafe(vec3 a, vec3 b, vec3 normal)
{
    float aDotB = dot(a, b);
    if(aDotB > (-1.0 + EPSILON))
    {
        return normalize(a + b);
    }
    else // if vectors are pointing to directly opposite directions
    {
        return normal;
    }
}

#define USE_RUSSIAN_ROULETTE 1
#define MIN_BOUNCES_BEFORE_RUSSIAN_ROULETTE 2
vec4 Trace(Ray ray, Ray neighborRay)
{
    vec3 accumulatedColor = vec3(0.0, 0.0, 0.0);
    vec3 accumulatedIndirectLightMultiplier = vec3(1.0, 1.0, 1.0);
    uint FirstPrimitiveID = uint(-1); 
    BlueNoiseData BlueNoise = GetBlueNoise();

    
	float lightPDF;
    vec3 lightPosition, lightColor, lightNormal;
    GetOneLightSample(lightPosition, lightColor, lightPDF, lightNormal);
    
    for (int i = 0; i < MAX_BOUNCES; i++)
    {
#if USE_RUSSIAN_ROULETTE
        if(i >= MIN_BOUNCES_BEFORE_RUSSIAN_ROULETTE)
        {
            float p = max(max(accumulatedIndirectLightMultiplier.r, accumulatedIndirectLightMultiplier.g), accumulatedIndirectLightMultiplier.b);
            p = max(p, EPSILON);
            if(p < rand())
            {
                break;
            }
            else
            {
                accumulatedIndirectLightMultiplier *= 1.0 / p;
            }
        }
#endif

        bool bLastRay = (i == MAX_BOUNCES - 1);
        bool bFirstRay = (i == 0);

        vec3 normal;
        vec3 tangent;
        vec2 uv;
        uint PrimitiveID;

	    vec2 result = Intersect(ray, normal, tangent, uv, PrimitiveID);

        if(i == 0)
        {
            FirstPrimitiveID = PrimitiveID;
        }

        if(accumulatedIndirectLightMultiplier.r < EPSILON && 
            accumulatedIndirectLightMultiplier.g < EPSILON &&
            accumulatedIndirectLightMultiplier.b < EPSILON)
        {
            // No longer tracking any reasonable amount of light,
            // early out
            break;
        }

        if(int(result.y) == INVALID_MATERIAL_ID)
        {
            accumulatedColor += accumulatedIndirectLightMultiplier * SampleEnvironmentMap(ray.direction);
            if(bFirstRay)
            {
                OutputPrimaryEmissive(accumulatedColor);
            }
            break;
        }
		else
        {   
            vec3 RayPoint = GetRayPoint(ray, result.x);
            ray.origin = RayPoint + normal * EPSILON;

            float RayDirectionDotN = dot(normal, ray.direction);
            bool IsBacksideOfGeometry = RayDirectionDotN > 0.0;
            Material material = GetMaterial(int(result.y), PrimitiveID, RayPoint, uv, IsBacksideOfGeometry);

            // Keep normals derived from a normal/bump map separate from the mesh normal.
            // Detail normals are used whenever possible but should be avoided anytime they're
            // used to calculate the ray direction because it can lead to generating rays
            // that go into a mesh. Not really aware of anyway around this since normal maps
            // are really a hack to start with
            vec3 detailNormal = GetDetailNormal(material, normal, tangent, uv);
			if(i == 0)
			{	
                vec3 NeighborRayPoint = GetRayPoint(neighborRay, result.x);
                OutputPrimaryWorldPosition(RayPoint, length(NeighborRayPoint - RayPoint));
			    OutputPrimaryNormal(detailNormal);
			}

            // Duplicate but a more readable variable depending on the scenario
            bool IsInsidePrimitve = IsBacksideOfGeometry;
              
            float CurrentIOR = IsInsidePrimitve ? material.IOR : AIR_IOR;
            float NewIOR = IsInsidePrimitve ? AIR_IOR : material.IOR;
            
            if(IsInsidePrimitve)
            {
                // Normals are calculated assuming the ray came from outside,
                // so flip the normal if the ray is inside the primitive
                normal = -normal;
                RayDirectionDotN = -RayDirectionDotN;
                detailNormal = -detailNormal;
            }

            // Omit emissives if the intersected object is marked as a light. This is because 
            // we will sample the lighting contribution when adding direct lighting 
            // and adding the emissive contribution here would double it's contribution.
            // However, we DO take the emissive contribution if it's the first primary ray 
            // as a special case so that the light doesn't just appear black
            if(bFirstRay || !IsLight(material) || !perFrameConstants.EnableNextEventEstimation)
            {
			    accumulatedColor += accumulatedIndirectLightMultiplier * material.emissive;
            }

            if(IsLight(material))
			{
				break;
			}

            {
                vec3 lightDirection = lightPosition - RayPoint;
				if(lightPDF > EPSILON && dot(lightDirection, lightNormal) < 0.0)
				{
            		float lightDistance = length(lightDirection);
            		lightDirection = lightDirection / lightDistance;

					#define SHADOW_BOUNCES 1
					vec3 ShadowMultiplier = vec3(1.0, 1.0, 1.0);
					Ray shadowFeeler = NewRay(RayPoint + normal * EPSILON, lightDirection);
					for(int i = 0; i < SHADOW_BOUNCES; i++)
					{
						vec3 shadowNormal;
                        vec3 shadowTangent;
						vec2 shadowUV;
						uint shadowPrimitiveID;
                        vec2 shadowResult = Intersect(shadowFeeler, shadowNormal, shadowTangent, shadowUV, shadowPrimitiveID); 
                        //vec2 shadowResult = IntersectAnything(shadowFeeler, 999999.0, shadowNormal, shadowTangent, shadowUV, shadowPrimitiveID); 
						vec3 shadowPoint = GetRayPoint(shadowFeeler, shadowResult.x);
                     
						int materialID = int(shadowResult.y);

						// TODO: This shouldn't really be possible unless we're talking a directional light...
						if(materialID == INVALID_MATERIAL_ID)
						{
							break;
						}

                        float LightDirectionDotN = dot(shadowNormal, lightDirection);
                        bool IsShadowFeelerIntersectingBacksideOfGeometry = LightDirectionDotN > 0.0;
						Material material = GetMaterial(materialID, shadowPrimitiveID, shadowPoint, shadowUV, IsShadowFeelerIntersectingBacksideOfGeometry);

                        if(IsLight(material))
                        {
							break;
						}
						else if(false)//IsSubsurfaceScattering(material))
						{
						   shadowFeeler.origin = GetRayPoint(shadowFeeler, shadowResult.x);
						   ShadowMultiplier *= max(0.0, 1.0 - FresnelFactor(
							AIR_IOR,
							material.IOR,
							shadowNormal,
							shadowFeeler.direction));
                        
						   shadowResult = Intersect(shadowFeeler, shadowNormal, shadowTangent, shadowUV, shadowPrimitiveID);
						   vec3 ExitPoint = GetRayPoint(shadowFeeler, shadowResult.x);
						   float subsurfacePathLength = length(ExitPoint - shadowFeeler.origin);
                       
						   if(any(material.absorption) > EPSILON)
						   {
							   vec3 inverseAlbedo = vec3(1.0, 1.0, 1.0) - material.albedo;
							   ShadowMultiplier *= exp(-inverseAlbedo * subsurfacePathLength * material.absorption);
						   }
						   float ScatterChance = min(material.scattering.r * subsurfacePathLength, 1.0);
                        
						   ShadowMultiplier *= (1.0 - ScatterChance); // Ignoring possiblity of in scattering
						   ShadowMultiplier *= max(0.0, 1.0 - FresnelFactor(
							  material.IOR,
							  AIR_IOR,
							  -shadowNormal,
							  shadowFeeler.direction));
                        
						   shadowFeeler.origin = GetRayPoint(shadowFeeler, shadowResult.x);
						}
						else
						{
							ShadowMultiplier = vec3(0.0, 0.0, 0.0);
							break;
						}
					}
                
            		float lightMultiplier = DiffuseBRDF(lightDirection, detailNormal) * abs(dot(lightNormal, lightDirection)) / (lightPDF * lightDistance * lightDistance);
					accumulatedColor += accumulatedIndirectLightMultiplier * material.albedo * lightMultiplier * ShadowMultiplier * lightColor;
                }
            }

            float fresnelFactor = FresnelFactor(
                CurrentIOR,
                NewIOR,
                normal,
                ray.direction);
            
            vec3 previousDirection = ray.direction;
            bool bSpecularRay = false;
            if(AllowsSpecular(material))
            {
                if(IsMetallic(material) || IsHairMaterial(material))
                {
                    bSpecularRay = true;
                }
                else
                {
                    bSpecularRay = rand() < 0.5;
                }
            }
            
            if(bSpecularRay)
            {
                float PDFValue;
                vec3 ReflectedDirection = reflect(ray.direction, normal);
                ray.direction = ImportanceSampleGGX(ray.direction, normal, material.roughness);
            }
            else
            {
                if(IsSubsurfaceScattering(material))
                {
                    float nr = CurrentIOR / NewIOR;
                    float discriminant = 1.0 - nr * nr * (1.0 - RayDirectionDotN * RayDirectionDotN);
                    if (discriminant > EPSILON) 
                    {
                        vec3 refractionDirection = normalize( nr * (ray.direction - normal * RayDirectionDotN) - normal * sqrt(discriminant));
                        if(UsePerfectSpecularOptimization(material.roughness))
                        {
                            ray.direction = refractionDirection;
                        }
                        else
                        {
                            float PDFValue;
                            ray.direction = GenerateRandomImportanceSampledDirection(refractionDirection, material.roughness, PDFValue);
                            if(PDFValue < EPSILON)
                            {
                                // This ray is statistically not relevant, attempt to find a better ray
                                ray.direction = GenerateRandomImportanceSampledDirection(refractionDirection, material.roughness, PDFValue);
                                if(PDFValue < EPSILON)
                                {
                                    // Still no luck, call it quits
                                    break;
                                }
                            }
                            
                            // TODO: Overly darkens rough refractions for some reason
                            // accumulatedIndirectLightMultiplier /= PDFValue;
                        }
                    }
                    else
                    {
                        ray.direction = reflect(ray.direction, normal);
                    }

                    #define MAX_SSS_BOUNCES 100
                    
                    bool noScatter = material.scattering < EPSILON;

                    #define PER_CHANNEL_SCATTER 0

                    #if PER_CHANNEL_SCATTER
                    // TODO: may want to consider the reciprocal be defined in the material to
                    // avoid a divide
                    float channelSelectRand = rand();
                    uint selectedChannel = channelSelectRand < 0.33 ? 0 : (channelSelectRand < 0.66 ? 1 : 2);
                    float DistancePerScatter; 
                    switch(selectedChannel)
                    {
                        default:
                        case 0:
                            DistancePerScatter =  1.0 / material.scattering.r;
                            accumulatedIndirectLightMultiplier *= float3(3, 0, 0);
                            break;
                        case 1:
                            DistancePerScatter =  1.0 / material.scattering.g;
                            accumulatedIndirectLightMultiplier *= float3(0, 3, 0);
                            break;
                        case 2:
                            DistancePerScatter =  1.0 / material.scattering.b;
                            accumulatedIndirectLightMultiplier *= float3(0, 0, 3);
                            break;
                    }
                    #else
                    float DistancePerScatter =  1.0 / ((material.scattering.r + material.scattering.g + material.scattering.b) / 3.0);
                    #endif
                    float maxTravelDistance = noScatter ? LARGE_NUMBER : DistancePerScatter;
                    bool exittingPrimitive = false;

                    for(int i = 0; i < MAX_SSS_BOUNCES && !exittingPrimitive; i++)
                    {
                        float travelDistance = max(-log(rand()), 0.1) * maxTravelDistance;

                        uint unusedPrimitiveID;
                        // TODO: Pass in max trace distance
                        result = Intersect(ray, normal, tangent, uv, unusedPrimitiveID);
                        
                        bool bHitFound = int(result.y) != INVALID_MATERIAL_ID;
                        if(!bHitFound)
                        {
                            // Uh...this should never happen
                            accumulatedIndirectLightMultiplier = vec3(0.0, 0.0, 0.0);
                            break;
                        }

                        result.x = min(travelDistance, result.x);
                        float distanceTravelledBeforeScatter = result.x;
                        
                        exittingPrimitive = result.x < travelDistance || noScatter;

                        bool lastRay = (i == MAX_SSS_BOUNCES - 1);
                        if(lastRay && !exittingPrimitive)
                        {
                            // Couldn't find an exit point
                            accumulatedIndirectLightMultiplier = vec3(0.0, 0.0, 0.0);
                        }

                        RayPoint = GetRayPoint(ray, result.x);
                        ray.origin = RayPoint + normal * EPSILON;

                        float3 beerLambert = exp(-distanceTravelledBeforeScatter * material.absorption);
                        accumulatedIndirectLightMultiplier *= beerLambert;
                        if(exittingPrimitive)
                        {
                            RayDirectionDotN = dot(normal, ray.direction);

                            if(RayDirectionDotN >= 0.0)
                            {
                                normal = -normal;
                                RayDirectionDotN = -RayDirectionDotN;
                            }


                            float nr = NewIOR / CurrentIOR;
                            float discriminant = 1.0 - nr * nr * (1.0 - RayDirectionDotN * RayDirectionDotN);
                            if (discriminant > EPSILON) 
                            {
                                vec3 refractionDirection = normalize( nr * (ray.direction - normal * RayDirectionDotN) - normal * sqrt(discriminant));
                                if(UsePerfectSpecularOptimization(material.roughness))
                                {
                                    ray.direction = refractionDirection;
                                }
                                else
                                {
                                    float PDFValue;
                                    ray.direction = GenerateRandomImportanceSampledDirection(refractionDirection, material.roughness, PDFValue);
                                    if(PDFValue < EPSILON)
                                    {
                                        // This ray is statistically not relevant, attempt to find a better ray
                                        ray.direction = GenerateRandomImportanceSampledDirection(refractionDirection, material.roughness, PDFValue);
                                        if(PDFValue < EPSILON)
                                        {
                                            // Still no luck, call it quits
                                            break;
                                        }
                                    }
                            
                                    // TODO: Overly darkens rough refractions for some reason
                                    // accumulatedIndirectLightMultiplier /= PDFValue;
                                }
                            }
                            else
                            {
                                ray.direction = reflect(ray.direction, normal);
                                exittingPrimitive = false;
                            }
                            previousDirection = ray.direction;
                        }
                        else
                        {
                            float pdfValue;
                            ray.direction = GenerateNewDirectionFromBSDF(ray.direction, 0.0, pdfValue); 
                            accumulatedIndirectLightMultiplier /= pdfValue;
                        }
                    }

                    // This is certain wrong but...
                    continue;
                } 
                else 
                {
					float PDFValue; 
                    ray.direction = GenerateRandomCosineWeightedDirection(normal, PDFValue);
                }
            }
            
            float DiffusePDF = dot(ray.direction, normal) / PI;
            if(AllowsSpecular(material))
            {
                vec3 halfVector = GetHalfVectorSafe(-previousDirection, ray.direction, normal);

                float roughnessSquared = max(material.roughness * material.roughness, MIN_ROUGHNESS_SQUARED);
                float DistributionPDF = ImportanceSampleGGXPDF(normal, ray.direction, halfVector, material.roughness);
                float SpecularPDF = DistributionPDF;
                float PDFValue = IsMetallic(material) ? SpecularPDF : mix(SpecularPDF, DiffusePDF, 0.5);
                accumulatedIndirectLightMultiplier /= PDFValue;
            }
            else
            {
                accumulatedIndirectLightMultiplier /= DiffusePDF;
            }
            
            
			float lightPDF;
            vec3 lightPosition, lightColor, lightNormal;
            GetOneLightSample(lightPosition, lightColor, lightPDF, lightNormal);
            {
			    
                if(bFirstRay)
                {
                    OutputPrimaryEmissive(material.emissive);
                }

				if(IsLight(material))
				{
					break;
				}
				
                bool bDecoupleFirstAlbedoFromOutput = IsTargettingRealTime();
                bool bRemoveAlbedo = bDecoupleFirstAlbedoFromOutput && bFirstRay;
                vec3 albedo = bRemoveAlbedo ? vec3(1,1,1) : material.albedo;
                float diffuseContribution = 1.0;
                if(IsMetallic(material))
                {
                    vec3 halfVector = normalize(-previousDirection + ray.direction);
                    float roughnessSquared = max(material.roughness * material.roughness, MIN_ROUGHNESS_SQUARED);
                    float specular = GGXNormalDistributionFunction(detailNormal, halfVector, roughnessSquared) / 
                        (4.0 * abs(dot(-previousDirection, halfVector)) * max(abs(dot(-previousDirection, normal)), abs(dot(ray.direction, normal))));
                    accumulatedIndirectLightMultiplier *= specular * albedo * saturate(dot(ray.direction, normal));
                }
                else
                {
                    if(AllowsSpecular(material))
                    {
                        vec3 halfVector = GetHalfVectorSafe(-previousDirection, ray.direction, normal);
                        
                        float ReflectionCoefficient = material.SpecularCoef;
                        float fresnel = ReflectionCoefficient + (1.0 - ReflectionCoefficient) * 
                            AbsPow(1.0 - dot(-previousDirection, halfVector), 5.0); 

                        float diffuseMultiplier = (28.0 / (23.0 * PI))
                            * (1.0 - ReflectionCoefficient)
                            * (1.0 - pow(1.0 - 0.5 * dot(-previousDirection, normal), 5.0))
                            * (1.0 - pow(1.0 - 0.5 * dot(ray.direction, normal), 5.0));
                        vec3 diffuse = albedo * diffuseMultiplier;

                        float roughnessSquared = max(material.roughness * material.roughness, MIN_ROUGHNESS_SQUARED);
                        float specular = GGXNormalDistributionFunction(detailNormal, halfVector, roughnessSquared) / 
                            (4.0 * abs(dot(-previousDirection, halfVector)) * max(abs(dot(-previousDirection, normal)), abs(dot(ray.direction, normal))));
                        
                        vec3 IndirectLightMultiplier = (diffuse + fresnel * specular)* saturate(dot(ray.direction, normal));
                        diffuseContribution = diffuse.r / ((diffuseMultiplier + fresnel * specular)* saturate(dot(ray.direction, normal)));

                        accumulatedIndirectLightMultiplier *= IndirectLightMultiplier;
                    }
                    else
                    {
                        accumulatedIndirectLightMultiplier *= albedo * DiffuseBRDF(ray.direction, detailNormal); 
                    }
                }
                if(bFirstRay) OutputPrimaryAlbedo(material.albedo, diffuseContribution);
            }
        }
    }
    
    return vec4(accumulatedColor, float(FirstPrimitiveID));
}

mat3 GetViewMatrix(float xRotationFactor)
{ 
   float xRotation = ((1.0 - xRotationFactor) - 0.5) * PI * 1.75;
   return mat3( cos(xRotation), 0.0, sin(xRotation),
                0.0,           1.0, 0.0,    
                -sin(xRotation),0.0, cos(xRotation));
}

vec3 GetLensPosition(in vec2 uv, float aspectRatio, float rotationFactor)
{
    vec3 lensPoint = GetCameraPosition();

    float lensWidth = GetCameraLensHeight() * aspectRatio;
    lensPoint += GetCameraRight() * (uv.x * 2.0 - 1.0) * lensWidth / 2.0;
    lensPoint += GetCameraUp() * (uv.y * 2.0 - 1.0) * GetCameraLensHeight() / 2.0;
    
    mat3 viewMatrix = GetViewMatrix(rotationFactor);
    lensPoint = mul(lensPoint, viewMatrix);

    return lensPoint;
}

vec4 PathTrace(in vec2 pixelCoord)
{
    vec2 pixelUVSize = 1.0 / GetResolution().xy;
    vec2 uv = pixelCoord.xy * pixelUVSize;
    float rotationFactor = GetRotationFactor();
    
    vec4 lastFrameData = GetLastFrameData();
    
    bool HasSceneChanged = ShouldInvalidateHistory();

#if IS_SHADER_TOY
    // Sacrifice the top left pixel to store previous frame meta-data
    if(int(pixelCoord.x) == 0 && int(pixelCoord.y) == 0)
    {
        float FrameCount = HasSceneChanged ? 0.0 : GetLastFrameCount(lastFrameData);
        return vec4(FrameCount + 1.0, 0, 0.0f, rotationFactor);
    }
#endif

#if IS_SHADER_TOY
    seed = hash13(vec3(pixelCoord.x, pixelCoord.y, iFrame));
#endif
	vec4 accumulatedColor = GetAccumulatedColor(uv);

    // Add some jitter for anti-aliasing
	BlueNoiseData BlueNoise = GetBlueNoise();
    uv += (BlueNoise.PrimaryJitter.xy - vec2(0.5, 0.5)) * pixelUVSize;
    
    float aspectRatio = GetResolution().x / GetResolution().y ; 
    vec3 focalPoint = GetCameraPosition() - GetCameraFocalDistance() * normalize(GetCameraLookAt() - GetCameraPosition());
    
    if(HasSceneChanged)
    {
        // Discard all past samples since they're now invalid
        accumulatedColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
    vec3 lensPoint = GetLensPosition(uv, aspectRatio, rotationFactor);
    vec3 neighborLensPoint = GetLensPosition(uv + pixelUVSize, aspectRatio, rotationFactor);
    
    mat3 viewMatrix = GetViewMatrix(rotationFactor);
    focalPoint = mul(focalPoint, viewMatrix);
    
    Ray cameraRay = NewRay(focalPoint, normalize(lensPoint - focalPoint));
    Ray neighborCameraRay = NewRay(focalPoint, normalize(neighborLensPoint - focalPoint));

#if !IS_SHADER_TOY
    if(perFrameConstants.DOFFocusDistance > 0.0f)
    {
        vec3 FocusPoint = GetRayPoint(cameraRay, perFrameConstants.DOFFocusDistance);

        vec2 DOFJitter = BlueNoise.DOFJitter;
        float Radius = sqrt(DOFJitter.x) * perFrameConstants.DOFApertureWidth;
        float Theta = BlueNoise.DOFJitter.y * 2.0 * PI;
        vec2 FocalJitter = vec2(cos(Theta) * Radius, sin(Theta) * Radius);
        cameraRay.origin += FocalJitter.x * GetCameraRight() + FocalJitter.y * GetCameraUp();
    
        cameraRay.direction = normalize(FocusPoint - cameraRay.origin);
    }
#endif 
    
    // Use the alpha channel as the counter for how 
    // many samples have been takes so far
    vec4 result = Trace(cameraRay, neighborCameraRay);
    vec3 outputColor = result.rgb;

#if !IS_SHADER_TOY
    if(perFrameConstants.FireflyClampValue >= EPSILON)
    {
        outputColor = min(outputColor, perFrameConstants.FireflyClampValue);
    }
#endif
    
#if IS_SHADER_TOY
    outputColor += accumulatedColor.rgb;
#endif

    return vec4(outputColor, 1.0);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{   
    fragColor = PathTrace(fragCoord.xy);
}