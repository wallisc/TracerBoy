#define MAX_BOUNCES 6
#define EPSILON 0.0001
#define PI 3.14
#define LARGE_NUMBER 1e20

#define AIR_IOR 1.0
#define PLASTIC_IOR 1.46f
#define FLOOR_IOR 1.0;

#define USE_DOF 0

#ifndef IS_SHADER_TOY
#define IS_SHADER_TOY 1
#endif

#if IS_SHADER_TOY
#define GLOBAL
float GetTime() { return iTime; }
vec4 GetMouse() { return iMouse; }
vec3 GetResoultion() { return iResolution; }
vec3 mul(vec3 v, mat3 m) { return v * m; }
vec4 GetAccumulatedColor(vec2 uv) { return texture(iChannel0, uv); }
vec4 GetLastFrameData() { return texture(iChannel0, vec2(0.0)); }
#endif 

// Rand taken from https://www.shadertoy.com/view/4sfGDB
GLOBAL float seed = 0.;
float rand() { return fract(sin(seed++ + GetTime())*43758.5453123); }
struct Ray
{ 
    vec3 origin; 
    vec3 direction; 
};

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

#define DEFAULT_MATERIAL_FLAG 0x0
#define METALLIC_MATERIAL_FLAG 0x1
#define SUBSURFACE_SCATTER_MATERIAL_FLAG 0x2
    
struct Material
{
    vec3 albedo;
    float IOR;
    
    float roughness;
    vec3 emissive;
    
    float absorption;    
    float scattering; // TODO should come from IOR
    int Flags;
};
    
bool IsMetallic(Material material)
{
    return (material.Flags & METALLIC_MATERIAL_FLAG) != 0;
}

bool IsSubsurfaceScattering(Material material)
{
    return (material.Flags & SUBSURFACE_SCATTER_MATERIAL_FLAG) != 0;
}

float GetDiffuseMultiplier(Material material)
{
    if(IsSubsurfaceScattering(material))
    {
        return 0.0;
        //return min(material.scattering / 10.0, 1.0);
    }
    else
    {
        return 1.0f;
    }
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
    boxPlanes[5].origin = box.origin - box.Axis3;
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

float GGXNormalDistributionFunction(
    vec3 Normal,
    vec3 HalfVector,
    float RoughnessSquared)
{
   float nDotH = dot(Normal, HalfVector);
   float Numerator =  (RoughnessSquared * RoughnessSquared);
   float Denominator = (nDotH * nDotH * (RoughnessSquared * RoughnessSquared - 1.0)) + 1.0;
   Denominator *= Denominator * PI;
    
   return Numerator / Denominator;
}


float NormalDistributionFunction(
    vec3 Normal,
    vec3 HalfVector,
    float RoughnessSquared)
{
   return GGXNormalDistributionFunction(Normal, HalfVector, RoughnessSquared);  
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
    float RoughnessSquared)
{
    return GGXGeometricShadowing(
        nDotV,
        RoughnessSquared);
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
    
    if(nDotV < EPSILON || nDotL < EPSILON)
    {
        return 0.0;
    }
    
    // Note that fresnel is omitted because this is already accounted for 
    // when calculating whether the chance whether a ray should be specular or
    // diffuse
    float Numerator = NormalDistributionFunction(Normal, HalfVector, RoughnessSquared) *
                      GeometricShadowing(nDotV, RoughnessSquared);
    float Denominator = (4.0 * nDotL * nDotV);
    return Numerator / Denominator;
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

    //Spectrum F = fresnel.Evaluate(Dot(wo, wh));
 	//float factor = (mode == TransportMode::Radiance) ? (1 / eta) : 1;
    float factor = 1.0;
    float sqrtDenom = dot(OutgoingDirection, halfVector) + eta * dot(IncomingDirection, halfVector);

    return abs(//NormalDistributionFunction(Normal, halfVector, RoughnessSquared) * 
               GeometricShadowing(IsInsidePrimitive ? -nDotIncoming : nDotIncoming, RoughnessSquared) * 
               eta * eta *
               abs(dot(IncomingDirection, halfVector)) * abs(dot(OutgoingDirection, halfVector)) * factor * factor /
                    (nDotIncoming * nDotOutgoing * sqrtDenom * sqrtDenom));
}

#define INVALID_MATERIAL_ID -1
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
#define AREA_LIGHT_MATERIAL_ID 10
#define NUM_MATERIALS 11

// Materials with custom functions
#define FLOOR_MATERIAL_ID 32
#define WOOD_MATERIAL_ID 33
#define GLASS_PEBBLE_MATERIAL_ID 34
#define CHECKER_MATERIAL_ID 35
#define CUSTOM_SCATTERING_MATERIAL_ID 36

#define USE_LARGE_SCENE 1

#define numBoundedPlanes  2
#define AreaLightIndex (numBoundedPlanes - 1)
#define numBoxes 0
#if USE_LARGE_SCENE
#define numSpheres 25
#else
#define numSpheres 10
#endif

struct Scene 
{
    CameraDescription camera;    
    BoundedPlane BoundedPlanes[numBoundedPlanes];
    Sphere Spheres[numSpheres];
};

#if IS_SHADER_TOY
// Cornell Box
Scene CurrentScene = Scene(
    CameraDescription(
        vec3(0.0, 2.0, 3.5), // position
        vec3(0.0, 1.0, 0.0), // lookAt
        vec3(0.0, 1.0, 0.0), // up
        vec3(1.0, 0.0, 0.0), // right
        2.0,                 // lensHeight
        7.0                  // focalDistance 
    ),
    
    // Scene Geometry
    BoundedPlane[numBoundedPlanes](
       BoundedPlane(vec3(0, 0, 0), vec3(0, 1, 0), vec3(10,0,0), vec3(0,0,10), FLOOR_MATERIAL_ID), // Bottom wall
       BoundedPlane(vec3(0.0, 2.0, -0.5), vec3(0, -1, 0), vec3(0.5,0,0), vec3(0,0,.5), AREA_LIGHT_MATERIAL_ID)
    ),
        
    Sphere[numSpheres](
        // Front Row
        Sphere(vec3(-2.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4, 0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4, 0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        
        // Second Row
        Sphere(vec3(-2.0, 0.4, -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4, -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4,  -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4,  -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID)
        

        #if USE_LARGE_SCENE
        ,
        
        // Third Row
        Sphere(vec3(-2.0, 0.4,  -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4,  -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4, -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4, -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        
        // Fourth Row
        Sphere(vec3(-2.0, 0.4,  -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4,  -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4, -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4, -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        
        // Fifth Row
        Sphere(vec3(-2.0, 0.4,-7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4,-7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4, -7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4, -7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4, -7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID)
        #endif
        
    )
);
#else
GLOBAL Scene CurrentScene;
void InitializeScene()
{
	CurrentScene.camera.position = vec3(0.0, 2.0, 3.5);
	CurrentScene.camera.lookAt = vec3(0.0, 1.0, 0.0);
	CurrentScene.camera.up = vec3(0.0, 1.0, 0.0);
	CurrentScene.camera.right = vec3(1.0, 0.0, 0.0);
	CurrentScene.camera.lensHeight = 2.0;
	CurrentScene.camera.focalDistance = 7.0;

	CurrentScene.BoundedPlanes[0] = NewBoundedPlane(vec3(0, 0, 0), vec3(0, 1, 0), vec3(10,0,0), vec3(0,0,10), FLOOR_MATERIAL_ID);
	CurrentScene.BoundedPlanes[1] = NewBoundedPlane(vec3(0.0, 2.0, -0.5), vec3(0, -1, 0), vec3(0.5,0,0), vec3(0,0,.5), AREA_LIGHT_MATERIAL_ID);
	
	CurrentScene.Spheres[0] = NewSphere(vec3(-2.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID);
	CurrentScene.Spheres[1] = NewSphere(vec3(-1.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID);
	CurrentScene.Spheres[2] = NewSphere(vec3( 0.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID);
	CurrentScene.Spheres[3] = NewSphere(vec3( 1.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID);
	CurrentScene.Spheres[4] = NewSphere(vec3( 2.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID);

	#if 0 

    Sphere[numSpheres](
        // Front Row
        Sphere(vec3(-2.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4, 0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4, 0.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        
        // Second Row
        Sphere(vec3(-2.0, 0.4, -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4, -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4,  -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4,  -1.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID)
        

        #if USE_LARGE_SCENE
        ,
        
        // Third Row
        Sphere(vec3(-2.0, 0.4,  -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4,  -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4, -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4, -3.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        
        // Fourth Row
        Sphere(vec3(-2.0, 0.4,  -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4,  -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4,  -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4, -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4, -5.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        
        // Fifth Row
        Sphere(vec3(-2.0, 0.4,-7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(-1.0, 0.4,-7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(0.0, 0.4, -7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(1.0, 0.4, -7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID),
        Sphere(vec3(2.0, 0.4, -7.5), 0.4f, CUSTOM_SCATTERING_MATERIAL_ID)
        #endif
    )
	#endif
};
// TODO intialize
#endif

vec3 GenerateRandomDirection()
{ 
    // Uniform hemisphere sampling from: http://www.rorydriscoll.com/2009/01/07/better-sampling/
    float u1 = (rand() * 2.0 - 1.0), u2 = rand();
    float r = sqrt(1.0 - u1 * u1);
    float phi = 2.0 * 3.14 * u2;
 
    return vec3(cos(phi) * r, sin(phi) * r, u1);
}

vec3 GenerateRandomDirection(vec3 normal)
{ 
    vec3 direction = GenerateRandomDirection();
    if(dot(normal, direction) > 0.0)
    {
        return direction;
    }
    else
    {
        return -direction;
    }
}

vec3 GenerateRandomImportanceSampledDirection(vec3 normal, float roughness, out float PDFValue)
{ 
    float lobeMultiplier = pow(1.0 - roughness, 5.0) * 1000.0;
        
    float u1 = rand(), u2 = rand();
    float theta = 2.0 * PI * u2;
    float phi = acos(sqrt(pow(u1, 1.0/(lobeMultiplier + 1.0))));
 
    vec3 direction = vec3(
        sin(phi) * cos(theta),
        cos(phi),
        sin(phi) * sin(theta));
    
    vec3 xAxis = cross(normal, vec3(1, 0, 0));
    vec3 zAxis = cross(normal, xAxis);
    
    PDFValue = (lobeMultiplier + 1.0) * pow(cos(phi), lobeMultiplier) / (2.0 * PI);
    
    return direction.x * xAxis + direction.y * normal + direction.z * zAxis;
}

vec2 Intersect(Ray ray, out vec3 normal, out uint PrimitiveID)
{
    float t = 999999.0;
    float materialID = float(INVALID_MATERIAL_ID);

    uint PrimitiveIDIterator = 0u;    
    PrimitiveID = uint(-1);
    float intersect;
    
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

    return vec2(t, materialID);
}

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
 
    if(primitiveID < PrimitiveIDIterator + uint(numSpheres))
    {
        uint SphereIndex = primitiveID - PrimitiveIDIterator;
        GetSphereAttributes(worldPosition, 
                            CurrentScene.Spheres[SphereIndex],
                            uv);
        return;
    }
    PrimitiveIDIterator += uint(numSpheres);
}

Material GetCustomScatteringMaterial(uint PrimitiveID)
{
    float absorptionLerpValue = float(PrimitiveID % 5u) / 4.0; 
    float absorption = mix(0.0, 10.0, absorptionLerpValue);
    
    float scatteringLerpValue = float(PrimitiveID / 5u) / 4.0; 
    float scattering = mix(0.0, 3.0, scatteringLerpValue);
    
    return SubsurfaceScatterMaterial(vec3(1.0, 0.6, 0.6), 0.0, 1.05, absorption, scattering);
}

Material GetWoodMaterial(uint PrimitiveID, vec3 WorldPosition)
{
    vec2 uv;
    GetPrimitiveAttributes(WorldPosition, PrimitiveID, uv);
    
    float IOR = 1.0;
    float roughness = 0.5;
#if IS_SHADER_TOY
    vec3 albedo = texture(iChannel2, uv.yx).rgb;
#else
	vec3 albedo = vec3(1, 1, 1);
#endif
    return NormalMaterial(albedo * albedo, IOR, roughness);
}

Material GetGlassPebbleMaterial(uint PrimitiveID, vec3 WorldPosition)
{
    vec2 uv;
    GetPrimitiveAttributes(WorldPosition, PrimitiveID, uv);
    
#if IS_SHADER_TOY
    vec3 albedo = texture(iChannel3, uv.yx).rgb;
#else
	vec3 albedo = vec3(1, 1, 1);
#endif
    albedo = albedo.bgr;
    return SubsurfaceScatterMaterial(albedo, 0.0, 1.05, 0.1, 0.4);
}


Material GetFloorMaterial(vec3 WorldPosition)
{
    // Checker board pattern 
    
    // Use this to scale the size of the checker board tiling
    const float ScaleMultiplier = 0.1;
    
    float IOR = 2.0;
    float roughness = 0.0;
    Material Mat0 = NormalMaterial(vec3(0.725, .71, .68), IOR, roughness);
    Material Mat1 = NormalMaterial(vec3(0.73, .35, .15), IOR, roughness);
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

Material GetMaterial(int MaterialID, uint PrimitiveID, vec3 WorldPosition)
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
    //Material SubsurfaceScatterMaterial(vec3 albedo, float roughness, float IOR, float absorption, float transmittance)

    
    Material materials[NUM_MATERIALS];
    materials[WAX_MATERIAL_ID] = SubsurfaceScatterMaterial(vec3(0.725, .1, .1), 0.2, 1.05, 0.2, 5.0);
    materials[DEFAULT_WALL_MATERIAL_ID] = PlasticMaterial(vec3(0.725, .71, .68));
    materials[BRONZE_MATERIAL_ID] = MetalMaterial(vec3(0.55, .2, .075), 1.18, 0.6);
    materials[BLUE_PLASTIC_MATERIAL_ID] = PlasticMaterial(vec3(.05, .05, .55));
    materials[RADIOACTIVE_MATERIAL_ID] = EmissiveMaterial(vec3(.05, .45, .05), vec3(0.0, .6, 0.0));
    materials[MIRROR_MATERIAL_ID] = ReflectiveMaterial();    
    materials[ROUGH_MIRROR_MATERIAL_ID] = MetalMaterial(vec3(1.0, 1.0, 1.0), 1.5, 0.5);
    materials[REFRACTIVE_MATERIAL_ID] = SubsurfaceScatterMaterial(vec3(1.0, 1.0, 1.0), 0.0, 1.5, 0.0, 0.0);
    
    materials[ICE_MATERIAL_ID] = SubsurfaceScatterMaterial(vec3(0.6, 0.6, 0.8), 0.0, 1.1, 0.0, 1.2); // ice
    
    materials[GLASS_MATERIAL_ID] = SubsurfaceScatterMaterial(vec3(1.0, 0.6, 0.6), 0.0, 1.05, 0.1, 0.0);
    materials[AREA_LIGHT_MATERIAL_ID] = PlasticMaterial(vec3(0.7, 0.7, 0.7));
    
    return materials[MaterialID];
}

// For most material purposes, GetMaterial can be used. This should be used for 
// accurate albedo information. Abusing this function can result in skyrocketing 
// shader compile times
Material GetMaterialWithTextures(int MaterialID, uint PrimitiveID, vec3 WorldPosition)
{
	if(MaterialID == WOOD_MATERIAL_ID)
    {
        return GetWoodMaterial(PrimitiveID, WorldPosition);
    }
    else if(MaterialID == GLASS_PEBBLE_MATERIAL_ID)
    {
        return GetGlassPebbleMaterial(PrimitiveID, WorldPosition);
    }
    
    return GetMaterial(MaterialID, PrimitiveID, WorldPosition);
} 



Material GetAreaLightMaterial()
{
    return GetMaterial(AREA_LIGHT_MATERIAL_ID, 0u, vec3(0.0, 0.0, 0.0));
}

void GetAreaLightSample(out vec3 LightPosition, out vec3 LightColor)
{
    vec2 areaLightUV = vec2(rand() * 2.0 - 1.0, rand() * 2.0 - 1.0);
    LightPosition = CurrentScene.BoundedPlanes[AreaLightIndex].origin +
        CurrentScene.BoundedPlanes[AreaLightIndex].Axis1 * areaLightUV.x +
        CurrentScene.BoundedPlanes[AreaLightIndex].Axis2 * areaLightUV.y;

    LightColor = GetAreaLightMaterial().albedo;
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

vec4 Trace(Ray ray)
{
    vec3 accumulatedColor = vec3(0.0, 0.0, 0.0);
    vec3 accumulatedIndirectLightMultiplier = vec3(1.0, 1.0, 1.0);
    uint FirstPrimitiveID = uint(-1); 
    for (int i = 0; i < MAX_BOUNCES; i++)
    {
        vec3 normal;
        uint PrimitiveID;
        vec2 result = Intersect(ray, normal, PrimitiveID);
        
        if(i == 0)
        {
            FirstPrimitiveID = PrimitiveID;
        }
        
        if(accumulatedIndirectLightMultiplier.x < EPSILON && 
          accumulatedIndirectLightMultiplier.y < EPSILON &&
          accumulatedIndirectLightMultiplier.z < EPSILON)
        {
            // No longer tracking any reasonable amount of light,
            // early out
            break;
        }
        
        if(int(result.y) == INVALID_MATERIAL_ID)
        {
            // Dial down the cube map intensity
            const float EnvironmentLightMultipier = 0.35;
#if IS_SHADER_TOY
            accumulatedColor += accumulatedIndirectLightMultiplier * EnvironmentLightMultipier * texture(iChannel1, ray.direction).xyz;
#else
            accumulatedColor += accumulatedIndirectLightMultiplier * EnvironmentLightMultipier * vec3(0.75, 0.2, 0.75);
#endif
            break;
        }
        else if(int(result.y) == AREA_LIGHT_MATERIAL_ID)
        {
           accumulatedColor += accumulatedIndirectLightMultiplier * GetAreaLightMaterial().albedo;
           break;
        }
        else
        {   
            vec3 RayPoint = GetRayPoint(ray, result.x);
            ray.origin = RayPoint + normal * EPSILON;
            Material material = GetMaterialWithTextures(int(result.y), PrimitiveID, RayPoint);

            float RayDirectionDotN = dot(normal, ray.direction);
            bool IsInsidePrimitve = RayDirectionDotN > 0.0;
              
            float CurrentIOR = IsInsidePrimitve ? material.IOR : AIR_IOR;
            float NewIOR = IsInsidePrimitve ? AIR_IOR : material.IOR;
            
            if(IsInsidePrimitve)
            {
                // Normals are calculated assuming the ray came from outside,
                // so flip the normal if the ray is inside the primitive
                normal = -normal;
                RayDirectionDotN = -RayDirectionDotN;
            }

            float fresnelFactor = FresnelFactor(
                CurrentIOR,
                NewIOR,
                normal,
                ray.direction);
            
            vec3 previousDirection = ray.direction;
            bool bSpecularRay = IsMetallic(material) || rand() < fresnelFactor;
            if(bSpecularRay)
            {
                float PDFValue;
                vec3 ReflectedDirection = reflect(ray.direction, normal);
                ray.direction = GenerateRandomImportanceSampledDirection(ReflectedDirection, material.roughness, PDFValue);
                //accumulatedIndirectLightMultiplier /= PDFValue;
            }
            else
            {
                if(IsSubsurfaceScattering(material))
                {
                    float nr = CurrentIOR / NewIOR;
                    float discriminant = 1.0 - nr * nr * (1.0 - RayDirectionDotN * RayDirectionDotN);
                    if (discriminant > EPSILON) 
                    {
                        ray.direction = normalize( nr * (ray.direction - normal * RayDirectionDotN) - normal * sqrt(discriminant));
                    }
                    else
                    {
                        ray.direction = reflect(ray.direction, normal);
                    }
                } 
                else 
                {
                    ray.direction = GenerateRandomDirection(normal);
                }
            }
            
            vec3 lightPosition, lightColor;
            GetAreaLightSample(lightPosition, lightColor);
                
                if(IsSubsurfaceScattering(material) && !bSpecularRay)
                {
                    // Hack required to avoid black edges on translucent spheres
                    // due to rays coming in at grazing angles
                    ray.origin += -normal * 0.05;
                    
                    #define MAX_SSS_BOUNCES 5
                    bool noScatter = material.scattering < EPSILON;
                    // TODO: may want to consider the reciprocal be defined in the material to
                    // avoid a divide
                    float DistancePerScatter =  1.0 / material.scattering; 
                    float maxTravelDistance = noScatter ? LARGE_NUMBER : DistancePerScatter;
                    bool exittingPrimitive = false;
                    
                    vec3 AccumulatedSSLightSamples = vec3(0.0, 0.0, 0.0);;
                    int NumLightSamples = 0;
                    for(int i = 0; i < MAX_SSS_BOUNCES && !exittingPrimitive; i++)
                    {
                        if(i != 0)
                        {
                            vec3 lightDirection = normalize(lightPosition - ray.origin);
                        
                            Ray lightRay;
                            lightRay.origin = ray.origin;
                            lightRay.direction = lightDirection;
                            vec3 unused;
                            uint unusedUint;
                            vec2 lightResult = Intersect(lightRay, unused, unusedUint);
                            vec3 lightEntryPoint = GetRayPoint(lightRay, lightResult.x);
                            float lightDistance = length(lightEntryPoint - ray.origin);

                            float extinctionChancePerUnitLength = material.absorption + material.scattering;
                            float distancePerExtinction = 1.0 / extinctionChancePerUnitLength;
                            if(lightDistance < distancePerExtinction)
                            {
                                vec3 remainingLightColor = lightColor * (1.0 - lightDistance / distancePerExtinction);
                                AccumulatedSSLightSamples += remainingLightColor * accumulatedIndirectLightMultiplier;
                                NumLightSamples++;
                            }
                        }
                        
                        
                        ray.origin += ray.direction * EPSILON * 4.0;
                        uint unusedPrimitiveID;
                        result = Intersect(ray, normal, unusedPrimitiveID);
                        
                        float travelDistance = max(-log(rand()), 0.1) * maxTravelDistance;
                        result.x = min(travelDistance, result.x);
                        float distanceTravelledBeforeScatter = result.x;
                        
                        exittingPrimitive = result.x < travelDistance || noScatter;
                        bool lastRay = (i == MAX_SSS_BOUNCES - 1);
                        if(lastRay && !exittingPrimitive)
                        {
                            // Couldn't find an exit point
                            accumulatedColor = vec3(0.0, 0.0, 0.0);
                            accumulatedIndirectLightMultiplier = vec3(0.0, 0.0, 0.0);
                        }

                        RayPoint = GetRayPoint(ray, result.x);
                        ray.origin = RayPoint + normal * EPSILON;

                        vec3 inverseAlbedo = vec3(1.0, 1.0, 1.0) - material.albedo;
                        accumulatedIndirectLightMultiplier *= exp(-inverseAlbedo * distanceTravelledBeforeScatter * material.absorption);
                        if(exittingPrimitive)
                        {
                            previousDirection = ray.direction;
                        }
                        else
                        {
                            float pdfValue;
                            ray.direction = GenerateNewDirectionFromBSDF(ray.direction, 0.0, pdfValue); 
                            // TODO: Divide by pdfValue
                        }

                    }
                    if(NumLightSamples > 0)
                    {
                        accumulatedColor += AccumulatedSSLightSamples / float(NumLightSamples);
                    }

                    
                    // TODO: We know the exact primitive it will hit, should just intersect against
                    // that specific primitive

                }
            else
            {
            	vec3 lightDirection = lightPosition - RayPoint;
            	float lightDistance = length(lightDirection);
            	lightDirection = lightDirection / lightDistance;
            	    
                #define SHADOW_BOUNCES 1
                vec3 ShadowMultiplier = vec3(1.0, 1.0, 1.0);
                Ray shadowFeeler = NewRay(RayPoint + normal * EPSILON, lightDirection);
                for(int i = 0; i < SHADOW_BOUNCES; i++)
                {
                    vec3 shadowNormal;
                    uint shadowPrimitiveID;
                    vec2 shadowResult = Intersect(shadowFeeler, shadowNormal, shadowPrimitiveID); 
                    vec3 shadowPoint = GetRayPoint(shadowFeeler, shadowResult.x);
                     
                    int materialID = int(shadowResult.y);
                    Material material = GetMaterial(materialID, shadowPrimitiveID, shadowPoint);
                    if(materialID == AREA_LIGHT_MATERIAL_ID)
                    {
                        break;
                    }
                    else if(IsSubsurfaceScattering(material))
                    {
                       shadowFeeler.origin = GetRayPoint(shadowFeeler, shadowResult.x);
                       ShadowMultiplier *= max(0.0, 1.0 - FresnelFactor(
                        AIR_IOR,
                        material.IOR,
                        shadowNormal,
                        shadowFeeler.direction));
                        
                       shadowResult = Intersect(shadowFeeler, shadowNormal, shadowPrimitiveID);
                       vec3 ExitPoint = GetRayPoint(shadowFeeler, shadowResult.x);
                       float subsurfacePathLength = length(ExitPoint - shadowFeeler.origin);
                       
                       if(material.absorption > EPSILON)
                       {
                           vec3 inverseAlbedo = vec3(1.0, 1.0, 1.0) - material.albedo;
                           ShadowMultiplier *= exp(-inverseAlbedo * subsurfacePathLength * material.absorption);
                       }
                       float ScatterChance = min(material.scattering * subsurfacePathLength, 1.0);
                        
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
                
                // TODO: Should be checking the environment light also
                float nDotL = dot(lightDirection, normal);
            	float lightMultiplier = IsSubsurfaceScattering(material) ?
                    GetDiffuseMultiplier(material) * nDotL : //SpecularBTDF(-previousDirection, material.IOR, ray.direction, AIR_IOR, normal, material.roughness) :
            		nDotL;
                accumulatedColor += accumulatedIndirectLightMultiplier * material.albedo * lightMultiplier * ShadowMultiplier * lightColor + material.emissive;
                if(bSpecularRay)
                {
                    if(IsMetallic(material))
                    {
                        accumulatedIndirectLightMultiplier *= material.albedo;
                    }
                    
                    if(false)
                    {
                        accumulatedIndirectLightMultiplier *= SpecularBRDF(
                            -previousDirection,
                            ray.direction,
                            normal,
                            material.roughness);
                    }
                }
                else // diffuse ray
                {
                   accumulatedIndirectLightMultiplier *= material.albedo * dot(ray.direction, normal); 
                }
            }
        }
    }
    
    return vec4(accumulatedColor, float(FirstPrimitiveID));
}

bool areFloatsEqual(float a, float b)
{
    return a + EPSILON > b && a - EPSILON < b;
}

float GetRotationFactor()
{
    if(GetMouse().x <= 0.0)
    {
        // Default value when shader is initially loaded up
        return 0.5f;
    }
    
    return GetMouse().x / GetResoultion().x;
}

float GetLightYOffset()
{
    if(GetMouse().y <= 0.0)
    {
        // Default value when shader is initially loaded up
        return 1.0f;
    }
    return mix(-2.0, 2.0, GetMouse().y / GetResoultion().y);
}

float GetLastFrameRotationFactor(vec4 lastFrameData)
{
    return fract(lastFrameData.a);
}

float GetLastFrameLightYPosition(vec4 lastFrameData)
{
    return lastFrameData.b;
}

float GetLastFrameCount(vec4 lastFrameData)
{
    return lastFrameData.x;
}

bool HasCameraMoved(float lastFrameRotationFactor, float rotationFactor)
{
    return !areFloatsEqual(lastFrameRotationFactor, rotationFactor);
}

mat3 GetViewMatrix(float rotationFactor)
{ 
   float rotation = ((1.0 - rotationFactor) - 0.5) * PI * 1.75;
   return mat3( cos(rotation), 0.0, sin(rotation),
                0.0,           1.0, 0.0,    
                -sin(rotation),0.0, cos(rotation));
}

vec4 PathTrace(in vec2 pixelCoord)
{
    vec2 uv = pixelCoord.xy / GetResoultion().xy;
    float rotationFactor = GetRotationFactor();
    LightPositionYOffset = GetLightYOffset();
    
    vec4 lastFrameData = GetLastFrameData();
#if !IS_SHADER_TOY
	InitializeScene();
#endif

    bool HasSceneChanged = HasCameraMoved(GetLastFrameRotationFactor(lastFrameData), rotationFactor) ||
      !areFloatsEqual(GetLastFrameLightYPosition(lastFrameData), LightPositionYOffset);

    // Sacrifice the top left pixel to store previous frame meta-data
    if(int(pixelCoord.x) == 0 && int(pixelCoord.y) == 0)
    {
        float FrameCount = HasSceneChanged ? 0.0 : GetLastFrameCount(lastFrameData);
        return vec4(FrameCount + 1.0, 0, LightPositionYOffset, rotationFactor);
    }
    CurrentScene.BoundedPlanes[AreaLightIndex].origin.y += LightPositionYOffset;
    
    seed = GetTime() + GetResoultion().y * pixelCoord.x / GetResoultion().x + pixelCoord.y / GetResoultion().y;
    vec4 accumulatedColor = GetAccumulatedColor(uv);
    // Add some jitter for anti-aliasing
    uv += (vec2(rand(), rand()) - vec2(0.5, 0.5)) / GetResoultion().xy;
    
    float aspectRatio = GetResoultion().x / GetResoultion().y ; 
    vec3 focalPoint = CurrentScene.camera.position - CurrentScene.camera.focalDistance * normalize(CurrentScene.camera.lookAt - CurrentScene.camera.position);
    vec3 lensPoint = CurrentScene.camera.position;
    
    if(HasSceneChanged)
    {
        // Discard all past samples since they're now invalid
        accumulatedColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
    
    float lensWidth = CurrentScene.camera.lensHeight * aspectRatio;
    lensPoint += CurrentScene.camera.right * (uv.x * 2.0 - 1.0) * lensWidth / 2.0;
    lensPoint += CurrentScene.camera.up * (uv.y * 2.0 - 1.0) * CurrentScene.camera.lensHeight / 2.0;
    
    mat3 viewMatrix = GetViewMatrix(rotationFactor);
    lensPoint = mul(lensPoint, viewMatrix);
    focalPoint = mul(focalPoint, viewMatrix);
    
    Ray cameraRay = NewRay(focalPoint, normalize(lensPoint - focalPoint));

#if USE_DOF
    float FocusDistance = 5.5;
    vec3 FocusPoint = GetRayPoint(cameraRay, FocusDistance);
    
    float ApertureWidth = 0.075;
    vec2 FocalJitter = (vec2(rand(), rand()) - vec2(0.5)) * ApertureWidth;
    cameraRay.origin += FocalJitter.x * CurrentScene.camera.right;
    cameraRay.origin += FocalJitter.y * CurrentScene.camera.up;
    
    cameraRay.direction = normalize(FocusPoint - cameraRay.origin);
#endif
    
    // Use the alpha channel as the counter for how 
    // many samples have been takes so far
    vec4 result = Trace(cameraRay);
    return vec4(accumulatedColor.rgb + result.rgb, result.w);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{   
    fragColor = PathTrace(fragCoord.xy);
}