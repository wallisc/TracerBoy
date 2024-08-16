// ======================================================================== //
// Copyright 2015-2020 Ingo Wald                                            //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

/*! \file pbrtParser/Scene.h The actual publicly visible Scene class
  after parsing */


#include "pbrtParser/math.h"
// std
#include <istream>
#include <ostream>
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <assert.h>
#include <mutex>

/*! namespace for all things pbrt parser, both syntactical *and* semantical parser */
namespace pbrt {

#if defined(PBRT_PARSER_VECTYPE_NAMESPACE)
  using vec2f    = PBRT_PARSER_VECTYPE_NAMESPACE::vec2f;
  using vec3f    = PBRT_PARSER_VECTYPE_NAMESPACE::vec3f;
  using vec4f    = PBRT_PARSER_VECTYPE_NAMESPACE::vec4f;
  using vec2i    = PBRT_PARSER_VECTYPE_NAMESPACE::vec2i;
  using vec3i    = PBRT_PARSER_VECTYPE_NAMESPACE::vec3i;
  using vec4i    = PBRT_PARSER_VECTYPE_NAMESPACE::vec4i;
  using mat3f    = PBRT_PARSER_VECTYPE_NAMESPACE::mat3f;
  using affine3f = PBRT_PARSER_VECTYPE_NAMESPACE::affine3f;
  using box3f    = PBRT_PARSER_VECTYPE_NAMESPACE::box3f;
  using pairNf   = PBRT_PARSER_VECTYPE_NAMESPACE::pairNf;
#else
  using vec2f    = pbrt::math::vec2f;
  using vec3f    = pbrt::math::vec3f;
  using vec4f    = pbrt::math::vec4f;
  using vec2i    = pbrt::math::vec2i;
  using vec3i    = pbrt::math::vec3i;
  using vec4i    = pbrt::math::vec4i;
  using mat3f    = pbrt::math::mat3f;
  using affine3f = pbrt::math::affine3f;
  using box3f    = pbrt::math::box3f;
  using pairNf   = pbrt::math::pairNf;
#endif

  /*! internal class used for serializing a scene graph to/from disk */
  struct BinaryWriter;
    
  /*! internal class used for serializing a scene graph to/from disk */
  struct BinaryReader;

  struct Object;

  /*! base abstraction for any entity in the pbrt scene graph that's
    not a paramter type (eg, it's a shape/shape, a object, a
    instance, matierla, tetxture, etc */
  struct Entity : public std::enable_shared_from_this<Entity> {
    typedef std::shared_ptr<Entity> SP;

    template<typename T>
    std::shared_ptr<T> as() { return std::dynamic_pointer_cast<T>(shared_from_this()); }
      
    /*! pretty-printer, for debugging */
    virtual std::string toString() const = 0; // { return "Entity"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) = 0;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) {}
  };


  /*! a spectrum is defined by a spectral power distribution,
    i.e. a list of (wavelength, value) pairs */
  struct Spectrum : public Entity {
    typedef std::shared_ptr<Spectrum> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Spectrum"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    pairNf spd;
  };

  
  /*! base abstraction for any material type. Note this is
    intentionally *not* abstract, since we will use it for any
    syntactic material type that we couldn't parse/recognize. This
    allows the app to eventually find non-recognized mateirals and
    replace them with whatever that app wants to use as a default
    material. Note at least one pbrt-v3 model uses a un-named
    material type, which will return this (empty) base class */
  struct Material : public Entity {
    typedef std::shared_ptr<Material> SP;

    /*! constructor */
    Material(const std::string &name = "") : name(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Material"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    /*! the logical name that this was defined under, such as
        "BackWall". Note this may be an empty string for some scenes
        (it is only defined for 'NamedMaterial's) */
    std::string name;
  };



  // ==================================================================
  // Singular Light Sources
  // ==================================================================
  struct LightSource : public Entity {
    typedef std::shared_ptr<LightSource> SP;
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "LightSource"; }
    // /*! serialize out to given binary writer */
    // virtual int writeTo(BinaryWriter &) override;
    // /*! serialize _in_ from given binary file reader */
    // virtual void readFrom(BinaryReader &) override;
  };

  struct InfiniteLightSource : public LightSource {
    typedef std::shared_ptr<InfiniteLightSource> SP;
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "InfiniteLightSource"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    std::string mapName;
    affine3f    transform;
    vec3f       L { 1.f,1.f,1.f };
    vec3f       scale { 1.f,1.f,1.f };
    int         nSamples { 1 };
  };

  // LightSource "distant" 
  //         "point from" [ 0 0 0 ] 
  //         "point to" [ 0 0 1 ] 
  //         "rgb L" [ 3.1359820366 2.4853198528 1.4146273136 ] 
  //         "rgb scale" [ 3.2500000000 3.2500000000 3.2500000000 ] 
  struct DistantLightSource : public LightSource {
    typedef std::shared_ptr<DistantLightSource> SP;
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "DistantLightSource"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    affine3f transform;
    vec3f from  { 0.f,0.f,0.f };
    vec3f to    { 0.f,0.f,1.f };
    vec3f L     { 1.f,1.f,1.f };
    vec3f scale { 1.f,1.f,1.f };
  };


  struct SpotLightSource : public LightSource {
    typedef std::shared_ptr<SpotLightSource> SP;
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "SpotLightSource"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f       from;
    vec3f       to;
    vec3f       I              { 1.f,1.f,1.f };
    Spectrum    Ispectrum;
    float       coneAngle      { 0.f };
    float       coneDeltaAngle { 0.f };
    vec3f       scale          { 1.f,1.f,1.f };
  };

  struct PointLightSource : public LightSource {
    typedef std::shared_ptr<PointLightSource> SP;
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "PointLightSource"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f       from;
    vec3f       I              { 1.f,1.f,1.f };
    Spectrum    Ispectrum;
    vec3f       scale          { 1.f,1.f,1.f };
  };
  
  
  // ==================================================================
  // Area Lights
  // ==================================================================
  
  struct AreaLight : public Entity {
    typedef std::shared_ptr<AreaLight> SP;
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "AreaLight"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
  };
  
  /*! a area light of type 'diffuse', with a 'color L' parameter */
  struct DiffuseAreaLightRGB : public AreaLight {
    typedef std::shared_ptr<DiffuseAreaLightRGB> SP;
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "DiffuseAreaLightRGB"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f L;
    int nSamples = 1;
  };

  /*! a area light of type 'diffuse', with a 'blackbody L' parameter */
  struct DiffuseAreaLightBB : public AreaLight {
    typedef std::shared_ptr<DiffuseAreaLightBB> SP;
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "DiffuseAreaLightBlackBody"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    /*! rgb from temperature */
    vec3f LinRGB() const;

    float temperature, scale;
    int nSamples = 1;
  };

  struct Texture : public Entity {
    typedef std::shared_ptr<Texture> SP;
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "(Abstract)Texture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
  };

  /*! a texture defined by a 2D image. Note we hadnle ptex separately,
    as ptex is not a 2D image format */
  struct ImageTexture : public Texture {
    typedef std::shared_ptr<ImageTexture> SP;

    ImageTexture(const std::string &fileName="") : fileName(fileName) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "ImageTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    std::string fileName;
    float uscale = 1.f;
    float vscale = 1.f;
  };
  
  /*! a texture defined by a disney ptex file. these are kind-of like
    image textures, but also kind-of not, so we handle them
    separately */
  struct PtexFileTexture : public Texture {
    typedef std::shared_ptr<PtexFileTexture> SP;

    PtexFileTexture(const std::string &fileName="") : fileName(fileName) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "PtexFileTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    std::string fileName;
  };
  
  struct FbmTexture : public Texture {
    typedef std::shared_ptr<FbmTexture> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "FbmTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
  };
  
  struct WindyTexture : public Texture {
    typedef std::shared_ptr<WindyTexture> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "WindyTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
  };
  
  struct MarbleTexture : public Texture {
    typedef std::shared_ptr<MarbleTexture> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "MarbleTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    float scale { 1.f };
  };
  
  struct WrinkledTexture : public Texture {
    typedef std::shared_ptr<WrinkledTexture> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "WrinkledTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
  };
  
  struct ScaleTexture : public Texture {
    typedef std::shared_ptr<ScaleTexture> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "ScaleTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    Texture::SP tex1, tex2;
    vec3f scale1 { 1.f };
    vec3f scale2 { 1.f };
  };
  
  struct MixTexture : public Texture {
    typedef std::shared_ptr<MixTexture> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "MixTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f amount { 1.f };
    Texture::SP map_amount;
    Texture::SP tex1, tex2;
    vec3f scale1 { 1.f };
    vec3f scale2 { 1.f };
  };
  
  struct ConstantTexture : public Texture {
    typedef std::shared_ptr<ConstantTexture> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "ConstantTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f value;
  };
  
  struct CheckerTexture : public Texture {
    typedef std::shared_ptr<CheckerTexture> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "CheckerTexture"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    float uScale { 1.f};
    float vScale { 1.f};
    vec3f tex1 { 0.f, 0.f, 0.f };
    vec3f tex2 { 1.f, 1.f, 1.f };
  };
  
  /*! disney 'principled' material, as used in moana model */
  struct DisneyMaterial : public Material {
    typedef std::shared_ptr<DisneyMaterial> SP;
    
    /*! constructor */
    DisneyMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "DisneyMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    float anisotropic    { 0.f };
    float clearCoat      { 0.f };
    float clearCoatGloss { 1.f };
    vec3f color          { 0.5f, 0.5f, 0.5f };
    float diffTrans      { 1.f };
    float eta            { 1.5f };
    float flatness       { 0.f };
    float metallic       { 0.f };
    float roughness      { .5f };
    float sheen          { 0.f };
    float sheenTint      { 0.5f };
    float specTrans      { 0.f };
    float specularTint   { 0.f };
    bool  thin           { false };
  };

  struct MixMaterial : public Material
  {
    typedef std::shared_ptr<MixMaterial> SP;
    
    /*! constructor */
    MixMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "MixMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    Material::SP material0, material1;
    vec3f amount { 0.5f, 0.5f, 0.5f };
    Texture::SP map_amount;
  };
    
  struct MetalMaterial : public Material
  {
    typedef std::shared_ptr<MetalMaterial> SP;
    
    /*! constructor */
    MetalMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "MetalMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    float roughness  { 0.01f };
    Texture::SP map_roughness;
    float uRoughness { 0.f };
    Texture::SP map_uRoughness;
    float vRoughness { 0.f };
    Texture::SP map_vRoughness;
    bool remapRoughness { true };
    vec3f       eta  { 0.21221054f, 0.91804785f, 1.1000715f }; //1.f, 1.f, 1.f };
    Spectrum spectrum_eta;
    vec3f       k    { 3.9132357f, 2.4519274f, 2.1321275f }; //1.f, 1.f, 1.f };
    Spectrum spectrum_k;
    Texture::SP map_bump;
  };
    
  struct HairMaterial : public Material
  {
    typedef std::shared_ptr<HairMaterial> SP;
    
    /*! constructor */
    HairMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "HairMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    float eumelanin  { 1.f };
    float beta_m     { .25f };
    float alpha      { 2.f };
  };
    
  struct TranslucentMaterial : public Material
  {
    typedef std::shared_ptr<TranslucentMaterial> SP;
    
    /*! constructor */
    TranslucentMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "TranslucentMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f transmit { 0.5f, 0.5f, 0.5f };
    vec3f reflect  { 0.5f, 0.5f, 0.5f };
    vec3f kd { 0.25, 0.25, 0.25 };
    Texture::SP map_kd;
  };
    
  struct PlasticMaterial : public Material
  {
    typedef std::shared_ptr<PlasticMaterial> SP;
    
    /*! constructor */
    PlasticMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "PlasticMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f kd { .25f };
    Texture::SP map_kd;
    vec3f ks { .25f };
    Texture::SP map_ks;
    Texture::SP map_bump;
    float roughness { 0.1f };
    Texture::SP map_roughness;
    bool remapRoughness { true };
  };
    
  struct SubstrateMaterial : public Material
  {
    typedef std::shared_ptr<SubstrateMaterial> SP;
    
    /*! constructor */
    SubstrateMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "SubstrateMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    float uRoughness { .1f };
    Texture::SP map_uRoughness;
    float vRoughness { .1f };
    Texture::SP map_vRoughness;
    bool remapRoughness { true };
      
    vec3f kd { .5f };
    Texture::SP map_kd;
    vec3f ks { .5f };
    Texture::SP map_ks;
    Texture::SP map_bump;
  };
    
  struct SubSurfaceMaterial : public Material
  {
    typedef std::shared_ptr<SubSurfaceMaterial> SP;
    
    /*! constructor */
    SubSurfaceMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "SubSurfaceMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    float uRoughness { 0.f };
    float vRoughness { 0.f };
    bool  remapRoughness { true };
    /*! in the one pbrt v3 model that uses that, these materials
      carry 'name' fields like "Apple" etc - not sure if that's
      supposed to specify some sub-surface medium properties!? */
    std::string name;
  };
    
  struct MirrorMaterial : public Material
  {
    typedef std::shared_ptr<MirrorMaterial> SP;
    
    /*! constructor */
    MirrorMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "MirrorMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f kr { .9f };
    Texture::SP map_bump;
  };
    
    
  struct FourierMaterial : public Material
  {
    typedef std::shared_ptr<FourierMaterial> SP;
    
    /*! constructor */
    FourierMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "FourierMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    std::string fileName;
  };
    
  struct MatteMaterial : public Material
  {
    typedef std::shared_ptr<MatteMaterial> SP;
    
    /*! constructor */
    MatteMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "MatteMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f kd { .5f };
    Texture::SP map_kd;
    float sigma { 0.f };
    Texture::SP map_sigma;
    Texture::SP map_bump;
  };
    
  struct GlassMaterial : public Material
  {
    typedef std::shared_ptr<GlassMaterial> SP;
    
    /*! constructor */
    GlassMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "GlassMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
    
    vec3f kr { 1.f, 1.f, 1.f };
    vec3f kt { 1.f, 1.f, 1.f };
    float index { 1.5f };
  };
    
  struct UberMaterial : public Material {
    typedef std::shared_ptr<UberMaterial> SP;
    
    /*! constructor */
    UberMaterial(const std::string &name = "") : Material(name) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "UberMaterial"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    vec3f kd { .25f };
    Texture::SP map_kd;
    
    Texture::SP map_emissive;
    Texture::SP map_normal;
    Texture::SP map_specular;


    vec3f ks { .25f };
    Texture::SP map_ks;
    
    vec3f kr { 0.f };
    Texture::SP map_kr;
      
    vec3f kt { 0.f };
    Texture::SP map_kt;
      
    vec3f opacity { 1.f };
    Texture::SP map_opacity;
      
    float alpha { 0.f };
    Texture::SP map_alpha;

    float shadowAlpha { 0.f };
    Texture::SP map_shadowAlpha;

    float index { 1.5f };
    float roughness { 0.1f };
    float uRoughness { 0.f };
    Texture::SP map_uRoughness;
    float vRoughness { 0.f };
    Texture::SP map_vRoughness;

    Texture::SP map_roughness;
    Texture::SP map_bump;
  };


  /*! base abstraction for any geometric shape. for pbrt, this
    should actually be called a 'Shape'; we call it shape for
    historic reasons (it's the term that embree and ospray - as
    well as a good many others - use) */
  struct Shape : public Entity {
    typedef std::shared_ptr<Shape> SP;
    
    Shape(Material::SP material = Material::SP()) : material(material) {}
    
    /*! virtual destructor, to force this to be polymorphic */
    virtual ~Shape() {}
    
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    virtual size_t getNumPrims() const = 0;
    virtual box3f getBounds() = 0;
    virtual box3f getPrimBounds(const size_t primID, const affine3f &xfm) = 0;
    virtual box3f getPrimBounds(const size_t primID);
      
    /*! the pbrt material assigned to the underlying shape */
    Material::SP material;

    /*! if enabled, the geometry normal of this shape should get
        flipped during rendering */
    bool reverseOrientation { false };

    /*! opacity value of that shape; a value of 0 means "fully
        transparent" (and is often used on ligth soruces) to create
        "virtual" quad light sources */
    float alpha = 1.f;
    
    /*! textures directl assigned to the shape (ie, not to the
      material) */
    std::map<std::string,Texture::SP> textures;
    
    /*! the area light that was active in the attributes when the
        shape was defined. Though you could in theory have multiple
        area lights active for a single shape we only support one,
        because this is easier to use for the end user, and arguably
        more than one doesn' tmake sense, nayway!? */
    AreaLight::SP                     areaLight;
  };

  /*! a plain triangle mesh, with vec3f vertex and normal arrays, and
    vec3i indices for triangle vertex indices. normal and texture
    arrays may be empty, but if they exist, will use the same vertex
    indices as vertex positions */
  struct TriangleMesh : public Shape {
    typedef std::shared_ptr<TriangleMesh> SP;

    /*! constructor */
    TriangleMesh(Material::SP material=Material::SP()) : Shape(material) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override;

    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
    
    virtual size_t getNumPrims() const override
    {
      return index.size();
    }
    virtual box3f getPrimBounds(const size_t primID, const affine3f &xfm) override;
    virtual box3f getPrimBounds(const size_t primID) override;
    
    virtual box3f getBounds() override;

    std::vector<vec3f> vertex;
    std::vector<vec3f> normal;
    std::vector<vec3f> tangents;
    std::vector<vec2f> texcoord;
    std::vector<vec3i> index;
    /*! mutex to lock anything within this object that might get
      changed by multiple threads (eg, computing bbox */
    std::mutex         mutex;
    bool  haveComputedBounds { false };
    box3f bounds;
  };

  /*! a plain qaud mesh, where every quad is a pair of two
    triangles. "degenerate" quads (in the sense of quads that are
    actually only triangles) are in fact allowed by having index.w
    be equal to index.z. 

    \note Note that PBRT itself doesn't actually _have_ quad meshes
    (only triangle meshes), but many objects do in fact contain
    either mostly or even all quads; so we added this type for
    convenience - that renderers that _can_ optimie for quads can
    easily convert from pbrt's trianglemehs to a quadmesh using \see
    QuadMesh::createFrom(TriangleMesh::SP) */
  struct QuadMesh : public Shape {
    typedef std::shared_ptr<QuadMesh> SP;

    /*! constructor */
    QuadMesh(Material::SP material=Material::SP()) : Shape(material) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "QuadMesh"; }

    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    /*! create a new quad mesh by merging triangle pairs in given
      triangle mesh. triangles that cannot be merged into quads will
      be stored as degenerate quads */
    static QuadMesh::SP makeFrom(TriangleMesh::SP triangleMesh);
    
    virtual size_t getNumPrims() const override
    {
      return index.size();
    }
    virtual box3f getPrimBounds(const size_t primID, const affine3f &xfm) override;
    virtual box3f getPrimBounds(const size_t primID) override;
    virtual box3f getBounds() override;

    std::vector<vec3f> vertex;
    std::vector<vec3f> normal;
    // std::vector<vec2f> texcoord;
    std::vector<vec4i> index;
    /*! mutex to lock anything within this object that might get
      changed by multiple threads (eg, computing bbox */
    std::mutex         mutex;
    bool  haveComputedBounds { false };
    box3f bounds;
  };

  /*! what we create for 'Shape "curve" type "cylinder"' */
  struct Curve : public Shape {
    typedef std::shared_ptr<Curve> SP;

    typedef enum : uint8_t {
      CurveType_Cylinder=0, CurveType_Flat, CurveType_Ribbon, CurveType_Unknown
        } CurveType;
    typedef enum : uint8_t {
      CurveBasis_Bezier=0, CurveBasis_BSpline, CurveBasis_Unknown
        } BasisType;
      
    /*! constructor */
    Curve(Material::SP material=Material::SP()) : Shape(material) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Curve"; }

    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
      
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    /*! for now, return '1' prim for the entire curve. since in pbrt
      a curve can be an entire spline of many points, most
      renderers will likely want to subdivide that into multiple
      smaller cubis curve segements .... but since we can't know
      what those renderers will do let's just report a single prim
      here */
    virtual size_t getNumPrims() const override
    { return 1; }
      
    virtual box3f getPrimBounds(const size_t primID, const affine3f &xfm) override;
    virtual box3f getPrimBounds(const size_t primID) override;
    
    virtual box3f getBounds() override;

    affine3f  transform;
    /*! the array of control points */
    CurveType type;
    BasisType basis;
    uint8_t   degree { 3 };
    std::vector<vec3f> P;
    float width0 { 1.f };
    float width1 { 1.f };
  };
      
  /*! a single sphere, with a radius and a transform. note we do
    _not_ yet apply the transform to update ratius and center, and
    use the pbrt way of storing them individually (in thoery this
    allows ellipsoins, too!?) */
  struct Sphere : public Shape {
    typedef std::shared_ptr<Sphere> SP;

    /*! constructor */
    Sphere(Material::SP material=Material::SP()) : Shape(material) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Sphere"; }

    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
    
    virtual size_t getNumPrims() const override
    {
      return 1;
    }
    virtual box3f getPrimBounds(const size_t primID, const affine3f &xfm) override;
    virtual box3f getPrimBounds(const size_t primID) override;
    
    virtual box3f getBounds() override;

    affine3f transform;
    float radius { 1.f };
  };


  /*! a single unit disk, with a radius and a transform. note we do
    _not_ yet apply the transform to update ratius and center, and
    use the pbrt way of storing them individually (in thoery this
    allows ellipsoins, too!?) */
  struct Disk : public Shape {
    typedef std::shared_ptr<Disk> SP;

    /*! constructor */
    Disk(Material::SP material=Material::SP()) : Shape(material) {}
    
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Disk"; }

    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
    
    virtual size_t getNumPrims() const override
    {
      return 1;
    }
    virtual box3f getPrimBounds(const size_t primID, const affine3f &xfm) override;
    virtual box3f getPrimBounds(const size_t primID) override;
    
    virtual box3f getBounds() override;

    affine3f transform;
    float radius { 1.f };
    float height { 0.f };
  };


  struct Instance : public Entity {
    Instance() {}
    Instance(const std::shared_ptr<Object> &object,
             const affine3f                &xfm)
      : object(object), xfm(xfm)
    {}
      
    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Instance"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    virtual box3f getBounds();

    typedef std::shared_ptr<Instance> SP;
      
    std::shared_ptr<Object> object;
    affine3f                xfm; // { PBRT_PARSER_VECTYPE_NAMESPACE::one };

    /*! mutex to lock anything within this object that might get
      changed by multiple threads (eg, computing bbox */
    std::mutex         mutex;
    bool  haveComputedBounds { false };
    box3f bounds;
  };

  /*! a logical "NamedObject" that can be instanced */
  struct Object : public Entity {
      
    /*! a "Type::SP" shorthand for std::shared_ptr<Type> - makes code
      more concise, and easier to read */
    typedef std::shared_ptr<Object> SP;

    /*! constructor */
    Object(const std::string &name="") : name(name) {
    }

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Object"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    virtual box3f getBounds();
    
    std::vector<Shape::SP>       shapes;
    /*! all _non_-area light sources; in the pbrt spec area light
        sources get attached to shapes; only non-area light source
        directly live in an object */
    std::vector<LightSource::SP> lightSources;
    std::vector<Instance::SP>    instances;
    std::string name = "";

    /*! mutex to lock anything within this object that might get
      changed by multiple threads (eg, computing bbox */
    std::mutex         mutex;
    bool  haveComputedBounds { false };
    box3f bounds;
  };

  /*! a camera as specified in the root .pbrt file. Note that unlike
    other entities for the camera we go one step beyond just
    seminatically parsing the objects, and actually already
    transform to a easier camera model.... this should eventually be
    reverted, only storing the actual values in this place */
  struct Camera : public Entity {
    /*! a "Type::SP" shorthand for std::shared_ptr<Type> - makes code
      more consise, and easier to read */
    typedef std::shared_ptr<Camera> SP;

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Camera"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;

    float fov { 30.f };
      
    // PBRT-Doc: float lensradius 0 The radius of the lens. Used to
    // render scenes with depth of field and focus effects. The
    // default value yields a pinhole camera.
    float lensRadius { 0.f };
      
    // PBRT-Doc: float focaldistance 10^30 The focal distance of the lens. If
    // "lensradius" is zero, this has no effect. Otherwise, it
    // specifies the distance from the camera origin to the focal
    // plane
    float focalDistance { 1e3f }; // iw: real default is 1e30f, but
    // that messes up our 'simplified'
    // camera...

    // the camera transform active when the camera was created
    affine3f frame;
      
    struct {
      /*! @{ computed mostly from 'lookat' value - using a _unit_ screen
        (that spans [-1,-1]-[+1,+1) that does not involve either
        screen size, screen resolution, or aspect ratio */
      vec3f screen_center;
      vec3f screen_du;
      vec3f screen_dv;
      /*! @} */
      vec3f lens_center;
      vec3f lens_du;
      vec3f lens_dv;
    } simplified;
  };

  /*! specification of 'film' / frame buffer. note this will only
    store the size specification, not the actual pixels itself */
  struct Film : public Entity {
    /*! a "Type::SP" shorthand for std::shared_ptr<Type> - makes code
      more consise, and easier to read */
    typedef std::shared_ptr<Film> SP;

    Film(const vec2i &resolution, const std::string &fileName="")
      : resolution(resolution),
        fileName(fileName)
    {}

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Film"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
    
    vec2i              resolution;
    /*! some files do specify the desired filename to be written
      to. may be empty if not set */
    std::string        fileName;
  };

  struct Sampler : public Entity {
      typedef std::shared_ptr<Sampler> SP;

      enum class Type {
          stratified
      };

      Type type = Type::stratified;
      int pixelSamples = 1;

      // used by stratified sampler
      int xSamples = 1;
      int ySamples = 1;

      std::string toString() const override { return "Sampler"; }
      int writeTo(BinaryWriter &) override;
      void readFrom(BinaryReader &) override;
  };

  struct Integrator : public Entity {
      typedef std::shared_ptr<Integrator> SP;

      enum class Type {
          direct_lighting,
          path_tracer
      };

      Type type = Type::path_tracer;
      int maxDepth = 0;

      std::string toString() const override { return "Integrator"; }
      int writeTo(BinaryWriter &) override;
      void readFrom(BinaryReader &) override;
  };

  struct PixelFilter : public Entity {
      typedef std::shared_ptr<PixelFilter> SP;

      enum class Type {
          box,
          gaussian,
          triangle
      };

      Type type = Type::box;
      float radius = 0.f;

      // used by gaussian filter
      float alpha = 0.f;

      std::string toString() const override { return "PixelFilter"; }
      int writeTo(BinaryWriter &) override;
      void readFrom(BinaryReader &) override;

  };

  /*! the complete scene - pretty much the 'root' object that
    contains the WorldBegin/WorldEnd entities, plus high-level
    stuff like camera, frame buffer specification, etc */
  struct Scene : public Entity //, public std::enable_shared_from_this<Scene>
  {
    typedef std::shared_ptr<Scene> SP;


    /*! save scene to given stream, and return number of bytes written */
    size_t saveTo(std::ostream &outStream);
    /*! save scene to given file name, and return number of bytes written */
    size_t saveTo(const std::string &outFileName);
    /*! load scene from given stream */
    static Scene::SP loadFrom(std::istream &inStream);
    /*! load scene from given file name */
    static Scene::SP loadFrom(const std::string &inFileName);

    /*! pretty-printer, for debugging */
    virtual std::string toString() const override { return "Scene"; }
    /*! serialize out to given binary writer */
    virtual int writeTo(BinaryWriter &) override;
    /*! serialize _in_ from given binary file reader */
    virtual void readFrom(BinaryReader &) override;
    
    /*! checks if the scene contains more than one level of instancing */
    bool isMultiLevel() const;
    /*! checks if the scene contains more than one level of instancing */
    bool isSingleLevel() const;
    /*! checks if the scene contains more than one level of instancing */
    bool isSingleObject() const;
    
    /*! helper function that flattens a multi-level scene into a
      flattned scene */
    void makeSingleLevel();

    /*! return bounding box of scene */
    box3f getBounds() const
    { assert(world); return world->getBounds(); }

    /*! just in case there's multiple cameras defined, this is a
      vector */
    std::vector<Camera::SP> cameras;
      
    Film::SP                film;

    Sampler::SP             sampler;

    Integrator::SP          integrator;

    PixelFilter::SP         pixelFilter;

    /*! the worldbegin/worldend content */
    Object::SP              world;
  };
    
  /* compute some _rough_ storage cost esimate for a scene. this will
     allow bricking builders to greedily split only the most egregious
     objects */
  double computeApproximateStorageWeight(Scene::SP scene);

  /*! parse a pbrt file (using the pbrt_parser project, and convert
    the result over to a naivescenelayout */
  PBRT_PARSER_INTERFACE Scene::SP importPBRT(const std::string &fileName, const std::string &basePath = "");
  
} // ::pbrt
