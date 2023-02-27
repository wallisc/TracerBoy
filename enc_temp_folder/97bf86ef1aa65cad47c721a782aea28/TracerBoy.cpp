#include "pch.h"

#include "SharedPostProcessStructs.h"
#include "CompositeAlbedoSharedShaderStructs.h"
#include "PostProcessCS.h"
#include "RayGen.h"
#include "ClosestHit.h"
#include "AnyHit.h"
#include "Miss.h"
#include "RaytraceCS.h"
#include "SoftwareRaytraceCS.h"
#include "RaytracePS.h"
#include "VarianceStencilMaskPS.h"
#include "FullscreenVS.h"
#include "CompositeAlbedoCS.h"
#include "XInput.h"

#define USE_ANYHIT 1
#define USE_FAST_PATH_WITH_FALLBACK 1

// Useful for loading levels where the textures won't fit into memory
#define DISABLE_MATERIALS 0

struct HitGroupShaderRecord
{
	BYTE ShaderIdentifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES]; // 32
	UINT MaterialIndex; // 4
	UINT VertexBufferIndex; // 4
	UINT IndexBufferIndex; // 4
	UINT GeometryIndex; // 4
	BYTE Padding2[16]; // 16
};

//------------------------------------------------------------------------------------------------
// Heap-allocating UpdateSubresources implementation
inline UINT64 UpdateSubresourcesHelper(
	ID3D12Device* pDevice,
	_In_ ID3D12GraphicsCommandList* pCmdList,
	_In_ ID3D12Resource* pDestinationResource,
	_In_ ID3D12Resource* pIntermediate,
	UINT64 IntermediateOffset,
	_In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
	_In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
	_In_reads_(NumSubresources) D3D12_SUBRESOURCE_DATA* pSrcData)
{
	UINT64 RequiredSize = 0;
	UINT64 MemToAlloc = static_cast<UINT64>(sizeof(D3D12_PLACED_SUBRESOURCE_FOOTPRINT) + sizeof(UINT) + sizeof(UINT64))* NumSubresources;
	if (MemToAlloc > SIZE_MAX)
	{
		return 0;
	}
	void* pMem = HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(MemToAlloc));
	if (pMem == nullptr)
	{
		return 0;
	}
	auto pLayouts = reinterpret_cast<D3D12_PLACED_SUBRESOURCE_FOOTPRINT*>(pMem);
	UINT64* pRowSizesInBytes = reinterpret_cast<UINT64*>(pLayouts + NumSubresources);
	UINT* pNumRows = reinterpret_cast<UINT*>(pRowSizesInBytes + NumSubresources);

	auto Desc = pDestinationResource->GetDesc();
	pDevice->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, IntermediateOffset, pLayouts, pNumRows, pRowSizesInBytes, &RequiredSize);

	UINT64 Result = UpdateSubresources(pCmdList, pDestinationResource, pIntermediate, FirstSubresource, NumSubresources, RequiredSize, pLayouts, pNumRows, pRowSizesInBytes, pSrcData);
	HeapFree(GetProcessHeap(), 0, pMem);
	return Result;
}

inline UINT64 GetRequiredIntermediateSizeHelper(
	ID3D12Device *pDevice,
	_In_ ID3D12Resource* pDestinationResource,
	_In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
	_In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources)
{
	auto Desc = pDestinationResource->GetDesc();
	UINT64 RequiredSize = 0;

	pDevice->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, 0, nullptr, nullptr, nullptr, &RequiredSize);

	return RequiredSize;
}

float3 ConvertFloat3(const pbrt::vec3f& v)
{
	return { v.x, v.y, v.z };
}

float4 ConvertFloat4(const pbrt::vec3f& v, float w)
{
	return { v.x, v.y, v.z, w };
}

void ConvertAffine3f(const pbrt::affine3f m, FLOAT Transform[3][4])
{
	Transform[0][0] = m.l.vx.x;
	Transform[0][1] = m.l.vy.x;
	Transform[0][2] = m.l.vz.x;
	Transform[0][3] = m.p.x;
	Transform[1][0] = m.l.vx.y;
	Transform[1][1] = m.l.vy.y;
	Transform[1][2] = m.l.vz.y;
	Transform[1][3] = m.p.y;
	Transform[2][0] = m.l.vx.z;
	Transform[2][1] = m.l.vy.z;
	Transform[2][2] = m.l.vz.z;
	Transform[2][3] = m.p.z;
}

float ChannelAverage(const pbrt::vec3f& v)
{
	return (v.x + v.y + v.z) / 3.0;
}

float ConvertSpecularToIOR(float specular)
{
	return (sqrt(specular) + 1.0) / (1.0 - sqrt(specular));
}

struct MaterialTracker
{
	bool Exists(pbrt::Material* pMaterial)
	{
		const auto& iter = MaterialNameToIndex.find(pMaterial);
		return iter != MaterialNameToIndex.end();
	}

	UINT GetMaterial(pbrt::Material* pMaterial)
	{
		VERIFY(Exists(pMaterial));
		return MaterialNameToIndex[pMaterial];
	}

	UINT AddMaterial(pbrt::Material* pMaterial, const Material m)
	{
		UINT materialIndex = MaterialList.size();
		MaterialNameToIndex[pMaterial] = materialIndex;
		MaterialList.push_back(m);
		return materialIndex;
	}

	std::vector<Material> MaterialList;
	std::unordered_map<pbrt::Material*, UINT> MaterialNameToIndex;
};

bool IsNormalizedFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_R1_UNORM:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return true;
	default:
		return false;
	}
}

UINT TextureAllocator::CreateTexture(pbrt::Texture::SP& pPbrtTexture, bool bGammaCorrect)
{
	TextureData texture;
	pbrt::ImageTexture::SP pImageTexture = std::dynamic_pointer_cast<pbrt::ImageTexture>(pPbrtTexture);
	pbrt::CheckerTexture::SP pCheckerTexture = std::dynamic_pointer_cast<pbrt::CheckerTexture>(pPbrtTexture);
	if (pImageTexture)
	{
		ComPtr<ID3D12Resource> pTexture;
		ComPtr<ID3D12Resource> pUpload;

		texture.TextureType = IMAGE_TEXTURE_TYPE;
		texture.DescriptorHeapIndex = m_tracerboy.AllocateDescriptorHeapSlot();
		std::wstring fileName(pImageTexture->fileName.begin(), pImageTexture->fileName.end());

		m_tracerboy.InitializeTexture(fileName,
			*m_pCommandList.Get(),
			pTexture,
			texture.DescriptorHeapIndex,
			pUpload);

		texture.TextureFlags = DEFAULT_TEXTURE_FLAG;
		if (bGammaCorrect && IsNormalizedFormat(pTexture->GetDesc().Format))
		{
			texture.TextureFlags |= NEEDS_GAMMA_CORRECTION_TEXTURE_FLAG;
		}

		m_uploadResources.push_back(pUpload);
		m_tracerboy.m_pTextures.push_back(pTexture);
	}
	else if (pCheckerTexture)
	{ 
		texture.TextureType = CHECKER_TEXTURE_TYPE;
		texture.TextureFlags = DEFAULT_TEXTURE_FLAG;
		texture.UScale = pCheckerTexture->uScale;
		texture.VScale = pCheckerTexture->vScale;
		texture.CheckerColor1 = ConvertFloat3(pCheckerTexture->tex1);
		texture.CheckerColor2 = ConvertFloat3(pCheckerTexture->tex2);
	}
	else
	{
		VERIFY(false);
	}
	m_textureData.push_back(texture);
	return m_textureData.size() - 1;
}

pbrt::vec3f GetAreaLightColor(pbrt::AreaLight::SP pAreaLight)
{
	pbrt::vec3f emissive(0.0f);
	pbrt::DiffuseAreaLightRGB::SP pDiffuseAreaLight= std::dynamic_pointer_cast<pbrt::DiffuseAreaLightRGB>(pAreaLight);
	if (pDiffuseAreaLight)
	{
		emissive = pDiffuseAreaLight->L;
	}
	else
	{
		VERIFY(false);
	}
	return emissive;
}

Material CreateMaterial(pbrt::Material::SP& pPbrtMaterial, pbrt::Texture::SP *pAlphaTexture,  pbrt::vec3f emissive, MaterialTracker &materialTracker, TextureAllocator &textureAlloator)
{
	Material material = {};
	material.IOR = 1.5f;
	material.albedoIndex = UINT_MAX;
	material.alphaIndex = UINT_MAX;
	material.normalMapIndex = UINT_MAX;
	material.emissiveIndex = UINT_MAX;
	material.specularMapIndex = UINT_MAX;
	material.emissive = ConvertFloat3(emissive);
	material.Flags = ChannelAverage(emissive) > 0.0 ? LIGHT_MATERIAL_FLAG : DEFAULT_MATERIAL_FLAG;

	if (pAlphaTexture)
	{
		material.alphaIndex = textureAlloator.CreateTexture(*pAlphaTexture);
	}

	pbrt::SubstrateMaterial::SP pSubstrateMaterial = std::dynamic_pointer_cast<pbrt::SubstrateMaterial>(pPbrtMaterial);
	pbrt::UberMaterial::SP pUberMaterial = std::dynamic_pointer_cast<pbrt::UberMaterial>(pPbrtMaterial);
	pbrt::MixMaterial::SP pMixMaterial = std::dynamic_pointer_cast<pbrt::MixMaterial>(pPbrtMaterial);
	pbrt::MirrorMaterial::SP pMirrorMaterial = std::dynamic_pointer_cast<pbrt::MirrorMaterial>(pPbrtMaterial);
	pbrt::MetalMaterial::SP pMetalMaterial = std::dynamic_pointer_cast<pbrt::MetalMaterial>(pPbrtMaterial);
	pbrt::FourierMaterial::SP pFourierMaterial = std::dynamic_pointer_cast<pbrt::FourierMaterial>(pPbrtMaterial);
	pbrt::GlassMaterial::SP pGlassMaterial = std::dynamic_pointer_cast<pbrt::GlassMaterial>(pPbrtMaterial);
	pbrt::MatteMaterial::SP pMatteMaterial = std::dynamic_pointer_cast<pbrt::MatteMaterial>(pPbrtMaterial);
	pbrt::DisneyMaterial::SP pDisneyMaterial = std::dynamic_pointer_cast<pbrt::DisneyMaterial>(pPbrtMaterial);
	pbrt::PlasticMaterial::SP pPlasticMaterial = std::dynamic_pointer_cast<pbrt::PlasticMaterial>(pPbrtMaterial);
	pbrt::SubSurfaceMaterial::SP pSubsurfaceMaterial = std::dynamic_pointer_cast<pbrt::SubSurfaceMaterial>(pPbrtMaterial);
	pbrt::TranslucentMaterial::SP pTranslucentMaterial = std::dynamic_pointer_cast<pbrt::TranslucentMaterial>(pPbrtMaterial);
	
	if (!pPbrtMaterial)
	{
		// Stick with the default properties if null material
	}
	else if (pDisneyMaterial)
	{
		material.albedo = ConvertFloat3(pDisneyMaterial->color);

		if (material.albedo.x > 0.7) material.albedo = { 0.2, 0.2, 0.2 };

		material.roughness = pDisneyMaterial->roughness;
		material.IOR = pDisneyMaterial->eta;

		// Not supporting blend between normal and metallic surfaces
		if (pDisneyMaterial->metallic > 0.5)
		{
			material.Flags |= METALLIC_MATERIAL_FLAG;
		}

		if (pDisneyMaterial->specTrans > 0.001)
		{
			material.Flags |= SUBSURFACE_SCATTER_MATERIAL_FLAG;
			material.absorption = 0.0;
			material.roughness = 0.0;
		}
	}
	else if (pUberMaterial)
	{
		if (pUberMaterial->map_kd)
		{
			material.albedoIndex = textureAlloator.CreateTexture(pUberMaterial->map_kd, true);
		}
		if (pUberMaterial->map_normal)
		{
			material.normalMapIndex = textureAlloator.CreateTexture(pUberMaterial->map_normal);
		}
		if (pUberMaterial->map_emissive)
		{
			material.emissiveIndex = textureAlloator.CreateTexture(pUberMaterial->map_emissive);
		}
		if (pUberMaterial->map_specular)
		{
			material.specularMapIndex = textureAlloator.CreateTexture(pUberMaterial->map_specular);
		}
		material.albedo = ConvertFloat3(pUberMaterial->kd);

		VERIFY(!pUberMaterial->map_uRoughness); // Not supporting textures
		VERIFY(pUberMaterial->uRoughness == pUberMaterial->vRoughness); // Not supporting multi dimension rougness
		
		material.roughness = pUberMaterial->uRoughness > 0.0 ? pUberMaterial->uRoughness : pUberMaterial->roughness;

		if (ChannelAverage(pUberMaterial->opacity) < 1.0)
		{
			material.Flags |= SUBSURFACE_SCATTER_MATERIAL_FLAG;
			material.IOR = pUberMaterial->index;
			material.absorption = ChannelAverage(pUberMaterial->kt); // absorption != transmission but need oh well
		}
	}
	else if (pMixMaterial)
	{
		UINT mat0Index = materialTracker.AddMaterial(pMixMaterial->material0.get(), CreateMaterial(pMixMaterial->material0, nullptr, emissive, materialTracker, textureAlloator));
		UINT mat1Index = materialTracker.AddMaterial(pMixMaterial->material1.get(), CreateMaterial(pMixMaterial->material1, nullptr, emissive, materialTracker, textureAlloator));
		material.Flags = MIX_MATERIAL_FLAG;

		// TODO: gross
		material.albedo = { (float)mat0Index, (float)mat1Index, ChannelAverage(pMixMaterial->amount) };
	}
	else if (pMirrorMaterial)
	{
		material.albedo = ConvertFloat3(pMirrorMaterial->kr);
		material.roughness = 0.0;
		material.Flags |= METALLIC_MATERIAL_FLAG;
	}
	else if (pMetalMaterial)
	{
		// TODO: need to support real albedo
		material.albedo = { 1.0, 1.0, 1.0 };
		//material.IOR = ChannelAverage(pMetalMaterial->eta);
		VERIFY(!pMetalMaterial->map_uRoughness); // Not supporting textures
		VERIFY(pMetalMaterial->uRoughness == pMetalMaterial->vRoughness); // Not supporting multi dimension rougness
		material.roughness = 0.5;
		material.Flags |= METALLIC_MATERIAL_FLAG;
	}
	else if (pSubstrateMaterial)
	{
		if (pSubstrateMaterial->map_kd)
		{
			material.albedoIndex = textureAlloator.CreateTexture(pSubstrateMaterial->map_kd);
		}
		material.albedo = ConvertFloat3(pSubstrateMaterial->kd);
		material.IOR = ConvertSpecularToIOR(ChannelAverage(pSubstrateMaterial->ks));
		material.SpecularCoef = ChannelAverage(pSubstrateMaterial->ks);

		// not supporting different roughness
		VERIFY(pSubstrateMaterial->uRoughness == pSubstrateMaterial->vRoughness);
		material.roughness = pSubstrateMaterial->uRoughness;
	}
	else if (pGlassMaterial)
	{
		// TODO properly support transmission/absorption
		//material.Flags = DEFAULT_MATERIAL_FLAG;
		material.albedo = { };
		material.absorption = ChannelAverage(pGlassMaterial->kt * 0.01);
		material.IOR = pGlassMaterial->index;
		material.Flags |= SUBSURFACE_SCATTER_MATERIAL_FLAG;
		//material.scattering = pGlassMaterial->kt.x;
	}
	else if (pFourierMaterial)
	{
		// Not suppported
		material.albedo = { 0.6, 0.6, 0.6 };
		material.roughness = 0.2;
	}
	else if (pMatteMaterial)
	{
		material.roughness = pMatteMaterial->sigma;
		if (pMatteMaterial->map_kd)
		{
			material.albedoIndex = textureAlloator.CreateTexture(pMatteMaterial->map_kd);
		}

		material.albedo = ConvertFloat3(pMatteMaterial->kd);
		material.Flags |= NO_SPECULAR_MATERIAL_FLAG;
	}
	else if (pPlasticMaterial)
	{
		material.roughness = pPlasticMaterial->roughness;
		if (pPlasticMaterial->map_kd)
		{
			material.albedoIndex = textureAlloator.CreateTexture(pPlasticMaterial->map_kd);
		}
		material.albedo = ConvertFloat3(pPlasticMaterial->kd);
		material.IOR = ConvertSpecularToIOR(ChannelAverage(pPlasticMaterial->ks));
		material.SpecularCoef = ChannelAverage(pPlasticMaterial->ks);
		//VERIFY(!pPlasticMaterial->map_ks);
		//VERIFY(!pPlasticMaterial->map_roughness);
		//VERIFY(!pPlasticMaterial->map_bump);

		material.Flags |= DEFAULT_MATERIAL_FLAG;
	}
	else if (pSubsurfaceMaterial)
	{
		{
			if (pSubsurfaceMaterial->map_kd)
			{
				material.albedoIndex = textureAlloator.CreateTexture(pSubsurfaceMaterial->map_kd, true);
			}
			material.IOR = pSubsurfaceMaterial->eta;
			material.roughness = pSubsurfaceMaterial->uRoughness;
			material.absorption = 0.1;
			material.scattering = 1.0f / ChannelAverage(pSubsurfaceMaterial->mfp);
			material.Flags |= SUBSURFACE_SCATTER_MATERIAL_FLAG;
		}
	}
	else if (pTranslucentMaterial)
	{
		if (pTranslucentMaterial->map_kd)
		{
			material.albedoIndex = textureAlloator.CreateTexture(pTranslucentMaterial->map_kd);
		}
		else
		{
			material.albedo = { };
			material.absorption = 0.001;
			material.Flags |= SUBSURFACE_SCATTER_MATERIAL_FLAG;
		}
	}
	else
	{
		//VERIFY(false);
	}

	return material;
}

TracerBoy::TracerBoy(ID3D12CommandQueue *pQueue) :
	m_pCommandQueue(pQueue),
	m_ActiveFrameIndex(0),
	m_SamplesRendered(0),
	m_mouseX(0),
	m_mouseY(0),
	m_bInvalidateHistory(false),
	m_flipTextureUVs(false),
	CurrentDescriptorSlot(NumReservedViewSlots),
	m_CachedOutputSettings(GetDefaultOutputSettings())
{
	m_pCommandQueue->GetDevice(IID_GRAPHICS_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf()));

	D3D12_FEATURE_DATA_D3D12_OPTIONS1 option1;
	VERIFY_HRESULT(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &option1, sizeof(option1)));

	const UINT AproximateOptimalOccupancy = 10;
	m_MinWaveAmount = AproximateOptimalOccupancy * option1.TotalLaneCount / option1.WaveLaneCountMax;

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options;
	VERIFY_HRESULT(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)));

#if SUPPORT_SW_RAYTRACING
	m_bSupportsHardwareRaytracing = options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
	if (EmulateRaytracing())
	{
		D3D12CreateRaytracingFallbackDevice(m_pDevice.Get(), CreateRaytracingFallbackDeviceFlags::ForceComputeFallback, 0, IID_PPV_ARGS(&m_fallbackDevice));
	}
#endif

	m_bSupportsInlineRaytracing = options.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 || EmulateRaytracing();
	{
		D3D12_DESCRIPTOR_HEAP_DESC viewDescriptorHeapDesc = {};
		viewDescriptorHeapDesc.NumDescriptors = ViewDescriptorHeapSlots::NumTotalViews;
		viewDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		viewDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		VERIFY_HRESULT(m_pDevice->CreateDescriptorHeap(&viewDescriptorHeapDesc, IID_GRAPHICS_PPV_ARGS(m_pViewDescriptorHeap.ReleaseAndGetAddressOf())));

		D3D12_DESCRIPTOR_HEAP_DESC nonShaderVisibleDescriptorHeapDesc = viewDescriptorHeapDesc;
		nonShaderVisibleDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VERIFY_HRESULT(m_pDevice->CreateDescriptorHeap(&nonShaderVisibleDescriptorHeapDesc, IID_GRAPHICS_PPV_ARGS(m_pNonShaderVisibleDescriptorHeap.ReleaseAndGetAddressOf())));
	
		D3D12_DESCRIPTOR_HEAP_DESC depthDescriptorHeapDesc = {};
		depthDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		depthDescriptorHeapDesc.NumDescriptors = 1;
		VERIFY_HRESULT(m_pDevice->CreateDescriptorHeap(&depthDescriptorHeapDesc, IID_GRAPHICS_PPV_ARGS(m_pDepthDescriptorHeap.ReleaseAndGetAddressOf())));
	}

	InitializeLocalRootSignature();

	{
		CD3DX12_ROOT_PARAMETER1 Parameters[RayTracingRootSignatureParameters::NumRayTracingParameters];
		Parameters[RayTracingRootSignatureParameters::PerFrameConstantsParam].InitAsConstants(sizeof(PerFrameConstants) / sizeof(UINT32), 0);
		Parameters[RayTracingRootSignatureParameters::ConfigConstantsParam].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);

		CD3DX12_DESCRIPTOR_RANGE1 PreviousFrameSRVDescriptor;
		PreviousFrameSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		Parameters[RayTracingRootSignatureParameters::PreviousFrameOutput].InitAsDescriptorTable(1, &PreviousFrameSRVDescriptor);

		CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
		OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		Parameters[RayTracingRootSignatureParameters::OutputUAV].InitAsDescriptorTable(1, &OutputUAVDescriptor);

		CD3DX12_DESCRIPTOR_RANGE1 JitteredOutputUAVDescriptor;
		JitteredOutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
		Parameters[RayTracingRootSignatureParameters::JitteredOutputUAV].InitAsDescriptorTable(1, &JitteredOutputUAVDescriptor);

		CD3DX12_DESCRIPTOR_RANGE1 AOVDescriptor;
		AOVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, NumAOVTextures, 2);
		Parameters[RayTracingRootSignatureParameters::AOVDescriptorTable].InitAsDescriptorTable(1, &AOVDescriptor);


		Parameters[RayTracingRootSignatureParameters::AccelerationStructureRootSRV].InitAsShaderResourceView(1);

		CD3DX12_DESCRIPTOR_RANGE1 SystemTexturesDescriptor;
		SystemTexturesDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, NumSystemTextures, 14);
		Parameters[RayTracingRootSignatureParameters::SystemTexturesDescriptorTable].InitAsDescriptorTable(1, &SystemTexturesDescriptor);

		CD3DX12_DESCRIPTOR_RANGE1 SceneDescriptor;
		SceneDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, NumSceneDescriptors, 20);
		Parameters[RayTracingRootSignatureParameters::SceneDescriptorTable].InitAsDescriptorTable(1, &SceneDescriptor);

#if SUPPORT_VOLUMES
		CD3DX12_DESCRIPTOR_RANGE1 VolumeDescriptor;
		VolumeDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 9);
		Parameters[RayTracingRootSignatureParameters::VolumeSRVParam].InitAsDescriptorTable(1, &VolumeDescriptor);
#endif

		CD3DX12_DESCRIPTOR_RANGE1 BindlessTableDescriptor[3];
		BindlessTableDescriptor[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
		BindlessTableDescriptor[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
		BindlessTableDescriptor[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0);
		Parameters[RayTracingRootSignatureParameters::ImageTextureTable].InitAsDescriptorTable(ARRAYSIZE(BindlessTableDescriptor), BindlessTableDescriptor);

		Parameters[RayTracingRootSignatureParameters::ShaderTable].InitAsShaderResourceView(11);
		Parameters[RayTracingRootSignatureParameters::StatsBuffer].InitAsUnorderedAccessView(10);

		D3D12_STATIC_SAMPLER_DESC StaticSamplers[] =
		{
			CD3DX12_STATIC_SAMPLER_DESC(
				0u,
				D3D12_FILTER_MIN_MAG_MIP_POINT,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP),

			CD3DX12_STATIC_SAMPLER_DESC(
				1u,
				D3D12_FILTER_MIN_MAG_MIP_LINEAR,
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,
				D3D12_TEXTURE_ADDRESS_MODE_WRAP),

			CD3DX12_STATIC_SAMPLER_DESC(
				2u,
				D3D12_FILTER_MIN_MAG_MIP_LINEAR,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP)
		};

		D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
		rootSignatureDesc.pParameters = Parameters;
		rootSignatureDesc.NumParameters = ARRAYSIZE(Parameters);
		rootSignatureDesc.pStaticSamplers = StaticSamplers;
		rootSignatureDesc.NumStaticSamplers = ARRAYSIZE(StaticSamplers);
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_RAYTRACING;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

		ComPtr<ID3DBlob> pRootSignatureBlob;

#if SUPPORT_SW_RAYTRACING
		if (EmulateRaytracing())
		{
			VERIFY_HRESULT(m_fallbackDevice->D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
			VERIFY_HRESULT(m_fallbackDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_GRAPHICS_PPV_ARGS(m_pRayTracingRootSignature.ReleaseAndGetAddressOf())));
		}
		else
#endif
		{
			VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
			VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_GRAPHICS_PPV_ARGS(m_pRayTracingRootSignature.ReleaseAndGetAddressOf())));
		}
	}

	if (m_bSupportsHardwareRaytracing)
	{
		auto RaygenExportName = L"RayGen";
		CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

		auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void*)g_pRayGen, ARRAYSIZE(g_pRayGen));
		lib->SetDXILLibrary(&libdxil);

		auto closestHitLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		D3D12_SHADER_BYTECODE closestHitLibDxil = CD3DX12_SHADER_BYTECODE((void*)g_pClosestHit, ARRAYSIZE(g_pClosestHit));
		closestHitLib->SetDXILLibrary(&closestHitLibDxil);

		auto anyHitLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		D3D12_SHADER_BYTECODE anyHitLibDxil = CD3DX12_SHADER_BYTECODE((void*)g_pAnyHit, ARRAYSIZE(g_pAnyHit));
		anyHitLib->SetDXILLibrary(&anyHitLibDxil);

		auto missLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		D3D12_SHADER_BYTECODE missLibDxil = CD3DX12_SHADER_BYTECODE((void*)g_pMiss, ARRAYSIZE(g_pMiss));
		missLib->SetDXILLibrary(&missLibDxil);

		auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
		hitGroup->SetClosestHitShaderImport(L"ClosestHit");
		hitGroup->SetAnyHitShaderImport(L"AnyHit");
		hitGroup->SetHitGroupExport(L"HitGroup");

		raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>()->SetRootSignature(m_pRayTracingRootSignature.Get());
		raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>()->SetRootSignature(m_pLocalRootSignature.Get());

		raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>()->Config(sizeof(RayPayload), 8);
		raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>()->Config(1);

		VERIFY_HRESULT(m_pDevice->CreateStateObject(raytracingPipeline, IID_GRAPHICS_PPV_ARGS(m_pRayTracingStateObject.ReleaseAndGetAddressOf())));
	}

	ComPtr<ID3D12StateObjectProperties> pStateObjectProperties;
	if (m_bSupportsHardwareRaytracing)
	{
		m_pRayTracingStateObject.As(&pStateObjectProperties);
	}


	D3D12_RESOURCE_DESC statsBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ReadbackStats), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	m_pStatsBuffer = CreateUAV(
		L"Stats",
		statsBufferDesc,
		nullptr,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	D3D12_UNORDERED_ACCESS_VIEW_DESC StatsUavDesc = {};
	StatsUavDesc.Format = DXGI_FORMAT_R32_UINT;
	StatsUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	StatsUavDesc.Buffer.NumElements = sizeof(ReadbackStats) / sizeof(UINT32);
	D3D12_CPU_DESCRIPTOR_HANDLE NonShaderVisibleStatsUavHandle = GetNonShaderVisibleCPUDescriptorHandle(ViewDescriptorHeapSlots::StatsBufferUAV);
	m_pDevice->CreateUnorderedAccessView(m_pStatsBuffer.Get(), nullptr, &StatsUavDesc,  NonShaderVisibleStatsUavHandle);
	m_pDevice->CopyDescriptorsSimple(1, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::StatsBufferUAV), NonShaderVisibleStatsUavHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	if(m_bSupportsInlineRaytracing && m_bSupportsHardwareRaytracing)
	{
		{
			D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.pRootSignature = m_pRayTracingRootSignature.Get();
			psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pRaytraceCS, ARRAYSIZE(g_pRaytraceCS));
			VERIFY_HRESULT(m_pDevice->CreateComputePipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pRayTracingPSO.ReleaseAndGetAddressOf())));
		}

		{
			D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
			psoDesc.InputLayout = { nullptr, 0 };
			psoDesc.pRootSignature = m_pRayTracingRootSignature.Get();
			psoDesc.VS = CD3DX12_SHADER_BYTECODE(g_pFullScreenVS, ARRAYSIZE(g_pFullScreenVS));
			psoDesc.PS = CD3DX12_SHADER_BYTECODE(g_pRaytracePS, ARRAYSIZE(g_pRaytracePS));
			psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			psoDesc.DepthStencilState.DepthEnable = TRUE;
			psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
			psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			psoDesc.DepthStencilState.StencilEnable = FALSE;
			psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
			psoDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
			psoDesc.DepthStencilState.StencilReadMask = 0xff;
			psoDesc.DepthStencilState.StencilWriteMask = 0x00;
			psoDesc.SampleMask = UINT_MAX;
			psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			psoDesc.NumRenderTargets = 0;
			psoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
			psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			psoDesc.SampleDesc.Count = 1;
			VERIFY_HRESULT(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pPixelShaderRayTracingPSO)));

			psoDesc.DepthStencilState.StencilWriteMask = 0xff;
			psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
			psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
			psoDesc.PS = CD3DX12_SHADER_BYTECODE(g_pVarianceStencilMaskPS, ARRAYSIZE(g_pVarianceStencilMaskPS));
			VERIFY_HRESULT(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pVarianceStencilPSO)));
		}
	
	}

#if SUPPORT_SW_RAYTRACING
	if (EmulateRaytracing())
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_pRayTracingRootSignature.Get();
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pSoftwareRayTraceCS, ARRAYSIZE(g_pSoftwareRayTraceCS));
		VERIFY_HRESULT(m_pDevice->CreateComputePipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pSoftwareRayTracingPSO.ReleaseAndGetAddressOf())));
	}
#endif

	if (m_bSupportsHardwareRaytracing)
	{
		const void* pRayGenShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(L"RayGen");
		AllocateBufferWithData(pRayGenShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, m_pRayGenShaderTable);

		const void* pMissShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(L"Miss");
		AllocateBufferWithData(pMissShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, m_pMissShaderTable);
	}

	{
		CD3DX12_ROOT_PARAMETER1 Parameters[CompositeAlbedoNumParameters];
		CD3DX12_DESCRIPTOR_RANGE1 AlbedoDescriptor;
		AlbedoDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_DESCRIPTOR_RANGE1 IndirectLightDescriptor;
		IndirectLightDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		CD3DX12_DESCRIPTOR_RANGE1 EmissiveDescriptor;
		EmissiveDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
		CD3DX12_DESCRIPTOR_RANGE1 outputTextureDescriptor;
		outputTextureDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		Parameters[CompositeAlbedoInputAlbedo].InitAsDescriptorTable(1, &AlbedoDescriptor);
		Parameters[CompositeAlbedoIndirectLighting].InitAsDescriptorTable(1, &IndirectLightDescriptor);
		Parameters[CompositeAlbedoEmissive].InitAsDescriptorTable(1, &EmissiveDescriptor);
		Parameters[CompositeAlbedoOutputTexture].InitAsDescriptorTable(1, &outputTextureDescriptor);

		D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = ARRAYSIZE(Parameters);
		rootSignatureDesc.pParameters = Parameters;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

		ComPtr<ID3DBlob> pRootSignatureBlob;
		VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
		VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_GRAPHICS_PPV_ARGS(m_pCompositeAlbedoRootSignature.ReleaseAndGetAddressOf())));

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_pCompositeAlbedoRootSignature.Get();
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_CompositeAlbedoCS, ARRAYSIZE(g_CompositeAlbedoCS));
		VERIFY_HRESULT(m_pDevice->CreateComputePipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pCompositeAlbedoPSO.ReleaseAndGetAddressOf())));
	}

	{
		CD3DX12_ROOT_PARAMETER1 Parameters[PostProcessRootSignatureParameters::NumParameters];
		CD3DX12_DESCRIPTOR_RANGE1 InputTextureDescriptor;
		InputTextureDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_DESCRIPTOR_RANGE1 AuxTextureDescriptor;
		AuxTextureDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		CD3DX12_DESCRIPTOR_RANGE1 outputTextureDescriptor;
		outputTextureDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		Parameters[PostProcessRootSignatureParameters::InputTexture].InitAsDescriptorTable(1, &InputTextureDescriptor);
		Parameters[PostProcessRootSignatureParameters::AuxTexture].InitAsDescriptorTable(1, &AuxTextureDescriptor);
		Parameters[PostProcessRootSignatureParameters::OutputTexture].InitAsDescriptorTable(1, &outputTextureDescriptor);
		Parameters[PostProcessRootSignatureParameters::Constants].InitAsConstants(sizeof(PostProcessConstants) / sizeof(UINT32), 0);

		D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = PostProcessRootSignatureParameters::NumParameters;
		rootSignatureDesc.pParameters = Parameters;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

		ComPtr<ID3DBlob> pRootSignatureBlob;
		VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
		VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_GRAPHICS_PPV_ARGS(m_pPostProcessRootSignature.ReleaseAndGetAddressOf())));

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_pPostProcessRootSignature.Get();
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_PostProcessCS, ARRAYSIZE(g_PostProcessCS));
		VERIFY_HRESULT(m_pDevice->CreateComputePipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pPostProcessPSO.ReleaseAndGetAddressOf())));
	}

	m_pDenoiserPass = std::unique_ptr<DenoiserPass>(new DenoiserPass(*m_pDevice.Get()));
	m_pTemporalAccumulationPass = std::unique_ptr<TemporalAccumulationPass>(new TemporalAccumulationPass(*m_pDevice.Get()));
	m_pFidelityFXSuperResolutionPass = std::unique_ptr<FidelityFXSuperResolutionPass>(new FidelityFXSuperResolutionPass(*m_pDevice.Get()));
}

class HeapAllocator
{
public:
	HeapAllocator(ID3D12Device& InDevice, UINT inAllocatorBlockSize, D3D12_RESOURCE_STATES InResourceState, D3D12_RESOURCE_FLAGS InResourceFlags, UINT InAlignment, bool bUploadHeap) :
		Device(InDevice),
		ResourceState(InResourceState),
		ResourceFlags(InResourceFlags),
		Alignment(InAlignment),
		AllocatorBlockSize(inAllocatorBlockSize),
		Offset(0),
		bUseUploadHeaps(bUploadHeap)
	{
	}

	D3D12_GPU_VIRTUAL_ADDRESS Allocate(UINT Size, ComPtr<ID3D12Resource>* ppResource = nullptr, UINT* pOffset = nullptr)
	{
		// If it's too big to fit into an allocator block, just make a one-off resource just for this allocation request
		if (Size > AllocatorBlockSize)
		{
			const D3D12_HEAP_PROPERTIES heapDesc = CD3DX12_HEAP_PROPERTIES(bUseUploadHeaps ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
			D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(Size, ResourceFlags);

			ComPtr<ID3D12Resource> pResource;
			VERIFY_HRESULT(Device.CreateCommittedResource(
				&heapDesc,
				D3D12_HEAP_FLAG_NONE,
				&bufferDesc,
				ResourceState,
				nullptr,
				IID_GRAPHICS_PPV_ARGS(pResource.ReleaseAndGetAddressOf())));
			if (pOffset) *pOffset = 0;
			if (ppResource) *ppResource = pResource;
			ResourceList.push_back(pResource);
			return pResource->GetGPUVirtualAddress();
		}

		Offset += Alignment - (Offset % Alignment);

		if (Offset + Size > AllocatorBlockSize || CurrentBlock == nullptr)
		{
			AllocateNewBlock();
		}
		
		if (pOffset)
		{
			*pOffset = Offset;
		}
		if (ppResource)
		{
			*ppResource = CurrentBlock;
		}
		D3D12_GPU_VIRTUAL_ADDRESS GpuVA = CurrentBlock->GetGPUVirtualAddress() + Offset;
		Offset += Size;

		return GpuVA;
	}

	const std::vector<ComPtr<ID3D12Resource>>& GetAllocatedResources() const
	{
		return ResourceList;
	}
private:
	void AllocateNewBlock()
	{
		if (CurrentBlock)
		{
			CurrentBlock = nullptr;
			Offset = 0;
		}

		const D3D12_HEAP_PROPERTIES heapDesc = CD3DX12_HEAP_PROPERTIES(bUseUploadHeaps ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT);
		D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(AllocatorBlockSize, ResourceFlags);

		VERIFY_HRESULT(Device.CreateCommittedResource(
			&heapDesc,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			ResourceState,
			nullptr,
			IID_GRAPHICS_PPV_ARGS(CurrentBlock.ReleaseAndGetAddressOf())));

		ResourceList.push_back(CurrentBlock);
	}

	ID3D12Device& Device;
	D3D12_RESOURCE_STATES ResourceState;
	UINT Offset;
	UINT Alignment;
	UINT AllocatorBlockSize;
	D3D12_RESOURCE_FLAGS ResourceFlags;
	bool bUseUploadHeaps;
	ComPtr<ID3D12Resource> CurrentBlock;
	std::vector<ComPtr<ID3D12Resource>> ResourceList;
};


void TracerBoy::LoadScene(ID3D12GraphicsCommandList& commandList, const std::string& sceneFileName, std::vector<ComPtr<ID3D12Resource>>& resourcesToDelete)
{
	std::size_t lastDeliminator = sceneFileName.find_last_of("/\\");
	m_sceneFileDirectory = sceneFileName.substr(0, lastDeliminator + 1);

	std::string sceneFileExtension = sceneFileName.substr(sceneFileName.find_last_of(".") + 1, sceneFileName.size());
	
	const UINT HeapBlockSize = 20 * 1024 * 1024;
	HeapAllocator UploadHeapAllocator(*m_pDevice.Get(), HeapBlockSize, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT, true);
	HeapAllocator RaytracingScratchMemoryHeapAllocator(*m_pDevice.Get(), HeapBlockSize, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, false);
	HeapAllocator RaytracingMemoryHeapAllocator(*m_pDevice.Get(), HeapBlockSize, EmulateRaytracing() ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, false);

	const D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	{
#if USE_OPENVDB
		std::string volumeFileName = "C:\\Users\\chris\\Documents\\GitHub\\TracerBoy\\Scenes\\bunny_cloud.vdb";
		openvdb::initialize();

		// Create a VDB file object.
		openvdb::io::File file(volumeFileName);
		file.open();
		openvdb::GridPtrVecPtr gridVec = file.getGrids();
		file.close();

		VERIFY(gridVec->size() == 1);
		openvdb::GridBase::Ptr gridPtr = (*gridVec)[0];

		openvdb::FloatGrid::Ptr floatGridPtr = openvdb::gridPtrCast<openvdb::FloatGrid>(gridPtr);
		VERIFY(floatGridPtr != nullptr);

		openvdb::GridBase& grid = *gridPtr;
		openvdb::VecType type = grid.getVectorType();
		openvdb::math::CoordBBox boundingBox;
		grid.baseTree().evalLeafBoundingBox(boundingBox);

		m_volumeMin = {
			(float)boundingBox.getStart().x(),
			(float)boundingBox.getStart().y(),
			(float)boundingBox.getStart().z()
		};

		m_volumeMax = {
			(float)boundingBox.getEnd().x(),
			(float)boundingBox.getEnd().y(),
			(float)boundingBox.getEnd().z()
		};

		UINT width = (UINT)(boundingBox.getEnd().x() - boundingBox.getStart().x());
		UINT height = (UINT)(boundingBox.getEnd().y() - boundingBox.getStart().y());
		UINT depth = (UINT)(boundingBox.getEnd().z() - boundingBox.getStart().z());

		D3D12_RESOURCE_DESC volumeDesc = CD3DX12_RESOURCE_DESC::Tex3D(DXGI_FORMAT_R32_FLOAT, width, height, depth, 1);
		VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
			&defaultHeapDesc,
			D3D12_HEAP_FLAG_NONE,
			&volumeDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_GRAPHICS_PPV_ARGS(m_pVolume.ReleaseAndGetAddressOf())));

		m_pDevice->CreateShaderResourceView(m_pVolume.Get(), nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::VolumeSRVSlot));


		D3D12_PLACED_SUBRESOURCE_FOOTPRINT volumeFootprint;
		UINT numRows;
		UINT64 rowSizeInBytes, totalSizeInBytes;
		m_pDevice->GetCopyableFootprints(&volumeDesc, 0, 1, 0, &volumeFootprint, &numRows, &rowSizeInBytes, &totalSizeInBytes);
		UINT64 rowPitch = volumeFootprint.Footprint.RowPitch;
		UINT64 slicePitch = volumeFootprint.Footprint.RowPitch * rowSizeInBytes;

		ComPtr<ID3D12Resource> pVolumeUpload;
		AllocateUploadBuffer(GetRequiredIntermediateSizeHelper(m_pDevice.Get(), m_pVolume.Get(), 0, 1), pVolumeUpload);
		resourcesToDelete.push_back(pVolumeUpload);

		std::vector<float> data;
		openvdb::FloatGrid::Accessor accessor = floatGridPtr->getAccessor();
		for (int z = 0; z < depth; z++)
		{
			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < width; x++)
				{
					openvdb::Coord coord(
						boundingBox.getStart().x() + (float)x + 0.5f,
						boundingBox.getStart().y() + (float)y + 0.5f,
						boundingBox.getStart().z() + (float)z + 0.5f);
					float density = accessor.getValue(coord);
					data.push_back(density);
				}
			}
		}
		const void* pData;
		LONG_PTR RowPitch;
		LONG_PTR SlicePitch;
		D3D12_SUBRESOURCE_DATA subresourceData = { data.data(), width * sizeof(float), width * height * sizeof(float) };
		UpdateSubresourcesHelper(m_pDevice.Get(), &commandList, m_pVolume.Get(), pVolumeUpload.Get(), 0, 0, 1, &subresourceData);

		D3D12_RESOURCE_BARRIER volumeBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_pVolume.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		};
		commandList.ResourceBarrier(ARRAYSIZE(volumeBarriers), volumeBarriers);
#endif

#if USE_ASSIMP
		AssimpImporter::ScratchData assimpScratchData;
#endif
		std::shared_ptr<pbrt::Scene> pScene;
		if (sceneFileExtension.compare("pbrt") == 0)
		{
			auto pbrtImportStart = std::chrono::high_resolution_clock::now();
			pScene = pbrt::importPBRT(sceneFileName);
			auto pbrtImportEnd = std::chrono::high_resolution_clock::now();

			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(pbrtImportEnd - pbrtImportStart);
			std::string pbrtImportLengthMessage = "PBRT import time: " + std::to_string(0.001f * (float)duration.count()) + " seconds";
			OutputDebugString(pbrtImportLengthMessage.c_str());

			bool bCachePBF = false;
			if (bCachePBF)
			{
				std::string pbfFileName = sceneFileName.substr(0, sceneFileName.size() - 4) + "pbf";
				pScene->saveTo(pbfFileName);
			}

			// PBRT uses GL style texture sampling
			m_flipTextureUVs = true;
		}
		else if (sceneFileExtension.compare("pbf") == 0)
		{
			auto pbfImportStart = std::chrono::high_resolution_clock::now();

			pScene = pbrt::Scene::loadFrom(sceneFileName);
			auto pbfImportEnd = std::chrono::high_resolution_clock::now();

			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(pbfImportEnd - pbfImportStart);
			std::string pbrtImportLengthMessage = "PBF import time: " + std::to_string(0.001f * (float)duration.count()) + " seconds";
			OutputDebugString(pbrtImportLengthMessage.c_str());

			// PBRT uses GL style texture sampling
			m_flipTextureUVs = true;
		}
		else
		{
#if USE_ASSIMP
			pScene = AssimpImporter::LoadScene(sceneFileName, assimpScratchData);
#else
			VERIFY(false); // Unsupported file type
#endif
		}
		
		auto d3d12SceneLoadStart = std::chrono::high_resolution_clock::now();

		assert(pScene->cameras.size() > 0);
		auto& pCamera = pScene->cameras[0];

		pbrt::vec3f CameraPosition = pbrt::vec3f(0);
		pbrt::vec3f CameraView = pbrt::vec3f(0.0, 0.0, 1.0);
		pbrt::vec3f CameraRight = pbrt::vec3f(1.0, 0.0, 0.0);
		pbrt::vec3f CameraUp = pbrt::vec3f(0.0, 1.0, 0.0);
		CameraPosition = pCamera->frame * CameraPosition;
		CameraView = pbrt::math::normalize(pbrt::math::xfmVector(pCamera->frame, CameraView));
		CameraRight = pbrt::math::normalize(pbrt::math::xfmVector(pCamera->frame, CameraRight));

		CameraUp = pbrt::math::xfmVector(pCamera->frame, CameraUp);
		m_camera.LensHeight = 2.0 * sqrtf(pbrt::math::dot(CameraUp,CameraUp));
		CameraUp = pbrt::math::normalize(CameraUp);

		auto pfnConvertVector3 = [](const pbrt::vec3f& v) -> Vector3
		{
			return Vector3(v.x, v.y, v.z);
		};

		float FOVAngle = pCamera->fov * M_PI / 180.0;
		m_camera.FocalDistance = (m_camera.LensHeight / 2.0) / tan(FOVAngle / 2.0);

		// I think this is how PBRT thinks of camera position....maybe
		CameraPosition = CameraPosition + (m_camera.FocalDistance + 0.01f) * CameraView;

		m_camera.Position = pfnConvertVector3(CameraPosition);
		m_camera.LookAt = pfnConvertVector3(CameraPosition + CameraView);
		m_camera.Right = pfnConvertVector3(CameraRight);
		m_camera.Up = pfnConvertVector3(CameraUp);

		std::vector< HitGroupShaderRecord> hitGroupShaderTable;
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;

		ComPtr<ID3D12GraphicsCommandList4> pCommandList;
		VERIFY_HRESULT(commandList.QueryInterface(IID_GRAPHICS_PPV_ARGS(pCommandList.ReleaseAndGetAddressOf())));

		TextureAllocator textureAllocator(*this, *pCommandList.Get());
		MaterialTracker materialTracker;

#if DISABLE_MATERIALS
		std::shared_ptr<pbrt::UberMaterial> pMaterial = std::make_shared<pbrt::UberMaterial>();
		pbrt::Material::SP spMaterial = pMaterial;
		materialTracker.AddMaterial(pMaterial.get(),
			CreateMaterial(
				spMaterial,
				nullptr,
				pbrt::vec3f(0, 0, 0),
				materialTracker,
				textureAllocator));
#endif

		const void* pHitGroupShaderIdentifier = nullptr;
		if (m_bSupportsHardwareRaytracing)
		{
			ComPtr<ID3D12StateObjectProperties> pStateObjectProperties;
			m_pRayTracingStateObject.As(&pStateObjectProperties);
			pHitGroupShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(L"HitGroup");
		}

		UINT geometryCount = 0;
		UINT triangleCount = 0;

		struct ShapeCacheEntry
		{
			D3D12_RAYTRACING_GEOMETRY_DESC GeometryDesc;
			UINT VertexBufferIndex;
			UINT IndexBufferIndex;
			D3D12_GPU_VIRTUAL_ADDRESS BLAS;

			UINT NumInstances;
			bool bIncludeInGlobalBLAS;
		};

		std::map<pbrt::Shape*, ShapeCacheEntry> shapeCache;

		struct InstanceEntry
		{
			pbrt::Shape* pShape;
			pbrt::affine3f Transform;
		};

		std::vector<InstanceEntry> instanceList;
		int instanceCount = pScene->world->shapes.size() + pScene->world->instances.size();
		instanceCount = std::min(instanceCount, 200000);
		instanceList.reserve(instanceCount);

		for (UINT i = 0; i < instanceCount; i++)
		{
			pbrt::Shape::SP pGeometry;
			pbrt::affine3f transform = pbrt::affine3f::identity();
			if (i < pScene->world->shapes.size())
			{
				UINT geometryIndex = i;
				pGeometry = pScene->world->shapes[geometryIndex];
			}
			else
			{
				UINT instanceIndex = i - pScene->world->shapes.size();
				auto& pInstance = pScene->world->instances[instanceIndex];
				transform = pInstance->xfm;
				pGeometry = pInstance->object->shapes[0];
			}

			pbrt::TriangleMesh::SP pTriangleMesh = std::dynamic_pointer_cast<pbrt::TriangleMesh>(pGeometry);
			pbrt::Curve::SP pCurve = std::dynamic_pointer_cast<pbrt::Curve>(pGeometry);
			if (!pTriangleMesh && !pCurve)
			{
				// Only supporting triangle meshes and curves
				continue;
			}

			// Handle curves by converting subdividing them into triangle meshes
			if (pCurve)
			{
				const UINT verticesPerLoop = 10;
				const float rotatedRadiansPerVert = 3.14 * 2.0 / (float)(verticesPerLoop);
				const UINT facesPerLoop = verticesPerLoop;
				const UINT indicesPerTriangle = 3;
				const UINT trianglesPerFace = 2;
				const UINT indicesPerLoop = facesPerLoop * trianglesPerFace * indicesPerTriangle;
				const UINT loopsPerCurve = 10;
				float curveIntepolantIncrementPerLoop = 1.0 / (float)loopsPerCurve;
				pTriangleMesh = pbrt::TriangleMesh::SP(new pbrt::TriangleMesh());

				pTriangleMesh->material = pCurve->material;
				pTriangleMesh->areaLight = pCurve->areaLight;

				// Always assuming curves are a single cubic bezier curve
				assert(pCurve->P.size() == 4);

				pbrt::math::vec3f p0 = pCurve->P[0];
				pbrt::math::vec3f p1 = pCurve->P[1];
				pbrt::math::vec3f p2 = pCurve->P[2];
				pbrt::math::vec3f p3 = pCurve->P[3];

				// Skip the first point as we always form a ring extending back to the previous point 
				// so nothing needs to be done for the initial point

				for (UINT loopIndex = 0; loopIndex < loopsPerCurve; loopIndex++)
				{
					float curveInterpolant = loopIndex * curveIntepolantIncrementPerLoop;
					float curveRadius = curveInterpolant * pCurve->width1 + (1.0 - curveInterpolant) * pCurve->width0;
					pbrt::math::vec3f loopCenter = CalculatePointOnCubicBezier(p0, p1, p2, p3, curveInterpolant);
					pbrt::math::vec3f curveForward, curveNormal0, curveNormal1;
					CalculateObjectSpaceAxisOnCubicBezier(p0, p1, p2, p3, curveInterpolant, curveForward, curveNormal0, curveNormal1);

					for (UINT vertIndex = 0; vertIndex < verticesPerLoop; vertIndex++)
					{
						float theta = vertIndex * rotatedRadiansPerVert;
						pbrt::math::vec3f normal = cos(theta) * curveNormal0 + sin(theta) * curveNormal1;
						pbrt::math::vec3f vertex = loopCenter + normal * curveRadius;
						pTriangleMesh->vertex.push_back(vertex);
						pTriangleMesh->normal.push_back(normal);
						pTriangleMesh->tangents.push_back(curveForward);
					}
					
					bool bFirstLoop = loopIndex == 0;
					// Skip generating faces for the first loop since we don't have any other edge loops
					// to create faces with
					if (!bFirstLoop)
					{
						UINT loopStartIndex = verticesPerLoop * loopIndex;
						UINT prevLoopStartIndex = verticesPerLoop * (loopIndex - 1);
						for (UINT faceIndex = 0; faceIndex < facesPerLoop; faceIndex++)
						{
							UINT leftIndexOffsetFromStart = faceIndex;
							UINT rightIndexOffsetFromStart = faceIndex == facesPerLoop - 1 ? 0 : faceIndex + 1;
							UINT leftIndex = loopStartIndex + leftIndexOffsetFromStart;
							UINT rightIndex = loopStartIndex + rightIndexOffsetFromStart;
							UINT prevLoopLeftIndex = prevLoopStartIndex + leftIndexOffsetFromStart;
							UINT prevLoopRightIndex = prevLoopStartIndex + rightIndexOffsetFromStart;

							pTriangleMesh->index.push_back(pbrt::vec3i(leftIndex, rightIndex, prevLoopLeftIndex));
							pTriangleMesh->index.push_back(pbrt::vec3i(rightIndex, prevLoopLeftIndex, prevLoopRightIndex));
						}

					}
				}
				
			}

			auto shapeCacheEntryIter = shapeCache.find(pGeometry.get());
			bool bShapeAlreadyCreated = shapeCacheEntryIter != shapeCache.end();
			if (!bShapeAlreadyCreated)
			{
				shapeCache[pGeometry.get()] = {};
				shapeCacheEntryIter = shapeCache.find(pGeometry.get());
			}

			ShapeCacheEntry& shapeCacheEntry = shapeCacheEntryIter->second;
			shapeCacheEntry.NumInstances++;

			pbrt::vec3f emissive(0.0f);
			if (pTriangleMesh->areaLight)
			{
				emissive = GetAreaLightColor(pTriangleMesh->areaLight);
			}

			UINT materialIndex = 0;
#if !DISABLE_MATERIALS
			if (materialTracker.Exists(pTriangleMesh->material.get()))
			{
				materialIndex = materialTracker.GetMaterial(pTriangleMesh->material.get());
			}
			else
			{
				materialIndex = materialTracker.AddMaterial(pTriangleMesh->material.get(), CreateMaterial(
					pTriangleMesh->material, 
					pTriangleMesh->textures.find("alpha") != pTriangleMesh->textures.end() ? &pTriangleMesh->textures["alpha"] : nullptr,
					emissive, 
					materialTracker, 
					textureAllocator));
			}
#endif

			if (!bShapeAlreadyCreated)
			{
				ComPtr<ID3D12Resource> pVertexBuffer;
				ComPtr<ID3D12Resource> pUploadVertexBuffer;
				UINT VertexBufferIndex = AllocateDescriptorHeapSlot();
				UINT vertexSize = sizeof(Vertex);
				UINT vertexBufferSize = static_cast<UINT>(pTriangleMesh->vertex.size() * vertexSize);
				UINT32 uploadVertexBufferOffset;
				UploadHeapAllocator.Allocate(vertexBufferSize, &pUploadVertexBuffer, &uploadVertexBufferOffset);				
				bool bNormalsProvided = pTriangleMesh->normal.size();
				Vertex* pVertexBufferData;
				{
					BYTE* pVertexBufferByteData;
					VERIFY_HRESULT(pUploadVertexBuffer->Map(0, nullptr, (void**)&pVertexBufferByteData));
					pVertexBufferByteData += uploadVertexBufferOffset;
					pVertexBufferData = (Vertex*)pVertexBufferByteData;

					
					for (UINT v = 0; v < pTriangleMesh->vertex.size(); v++)
					{
						bool bBakeTransformIntoVertexBuffer = false;
						pbrt::affine3f vertexBufferTransform = bBakeTransformIntoVertexBuffer ? transform : pbrt::affine3f::identity();
						auto parserVertex = vertexBufferTransform * pTriangleMesh->vertex[v];
						pbrt::vec3f parserNormal(0, 1, 0); // TODO: This likely needs to be a flat normal
						pbrt::vec3f parserTangent(0, 0, 1);
						if (bNormalsProvided)
						{
							parserNormal = normalize(xfmNormal(vertexBufferTransform, pTriangleMesh->normal[v]));
						}
						if (v < pTriangleMesh->tangents.size())
						{
							parserTangent = normalize(xfmNormal(vertexBufferTransform, pTriangleMesh->tangents[v]));
						}
						pbrt::vec2f uv(0.0, 0.0);
						if (v < pTriangleMesh->texcoord.size())
						{
							uv = pTriangleMesh->texcoord[v];
						}
						pVertexBufferData[v].Position = { parserVertex.x, parserVertex.y, parserVertex.z };
						pVertexBufferData[v].Normal = { parserNormal.x, parserNormal.y, parserNormal.z };
						pVertexBufferData[v].UV = { uv.x, uv.y };
						pVertexBufferData[v].Tangent = { parserTangent.x, parserTangent.y, parserTangent.z };
					}

					D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
					D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
					SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
					SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
					SRVDesc.Buffer.NumElements = desc.Width / 4;

					pVertexBuffer = CreateSRV(L"VertexBuffer", desc, SRVDesc, GetCPUDescriptorHandle(VertexBufferIndex), D3D12_RESOURCE_STATE_COPY_DEST);
				}

				UINT indexSize = sizeof(UINT32);
				UINT indexBufferSize = static_cast<UINT>(pTriangleMesh->index.size() * 3 * indexSize);
				ComPtr<ID3D12Resource> pUploadIndexBuffer;
				ComPtr<ID3D12Resource> pIndexBuffer;
				UINT32 uploadIndexBufferOffset;
				UploadHeapAllocator.Allocate(indexBufferSize, &pUploadIndexBuffer, &uploadIndexBufferOffset);
				UINT IndexBufferIndex = AllocateDescriptorHeapSlot();
				{
					BYTE* pIndexBufferByteData;
					VERIFY_HRESULT(pUploadIndexBuffer->Map(0, nullptr, (void**)&pIndexBufferByteData));
					pIndexBufferByteData += uploadIndexBufferOffset;

					UINT32* pIndexBufferData = (UINT32*)pIndexBufferByteData;;

					for (UINT i = 0; i < pTriangleMesh->index.size(); i++)
					{
						auto triangleIndices = pTriangleMesh->index[i];
						pIndexBufferData[3 * i] = triangleIndices.x;
						pIndexBufferData[3 * i + 1] = triangleIndices.y;
						pIndexBufferData[3 * i + 2] = triangleIndices.z;
						if (!bNormalsProvided)
						{
							pbrt::vec3f edge1 = pTriangleMesh->vertex[triangleIndices.y] - pTriangleMesh->vertex[triangleIndices.x];
							pbrt::vec3f edge2 = pTriangleMesh->vertex[triangleIndices.z] - pTriangleMesh->vertex[triangleIndices.y];
							pbrt::vec3f normal = pbrt::math::cross(pbrt::math::normalize(edge1), pbrt::math::normalize(edge2));

							pVertexBufferData[triangleIndices.x].Normal = { normal.x, normal.y, normal.z };
							pVertexBufferData[triangleIndices.y].Normal = { normal.x, normal.y, normal.z };
							pVertexBufferData[triangleIndices.z].Normal = { normal.x, normal.y, normal.z };
						}
					}
					D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
					D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
					SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					SRVDesc.Format = DXGI_FORMAT_R32_UINT;
					SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
					SRVDesc.Buffer.NumElements = desc.Width / 4;

					pIndexBuffer = CreateSRV(L"IndexBuffer", desc, SRVDesc, GetCPUDescriptorHandle(IndexBufferIndex), D3D12_RESOURCE_STATE_COPY_DEST);
				}
				triangleCount += pTriangleMesh->index.size() / 3;

				D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
				geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
				geometryDesc.Triangles.IndexBuffer = pIndexBuffer->GetGPUVirtualAddress();
				geometryDesc.Triangles.IndexCount = static_cast<UINT>(pIndexBuffer->GetDesc().Width) / (sizeof(UINT32));
				geometryDesc.Triangles.IndexFormat = indexSize == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
				geometryDesc.Triangles.Transform3x4 = 0;
				geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
				geometryDesc.Triangles.VertexCount = static_cast<UINT>(pVertexBuffer->GetDesc().Width) / vertexSize;
				geometryDesc.Triangles.VertexBuffer.StartAddress = pVertexBuffer->GetGPUVirtualAddress();
				geometryDesc.Triangles.VertexBuffer.StrideInBytes = vertexSize;
				geometryDesc.Flags = USE_ANYHIT ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
				geometryDescs.push_back(geometryDesc);

				commandList.CopyBufferRegion(pVertexBuffer.Get(), 0, pUploadVertexBuffer.Get(), uploadVertexBufferOffset, vertexBufferSize);
				commandList.CopyBufferRegion(pIndexBuffer.Get(), 0, pUploadIndexBuffer.Get(), uploadIndexBufferOffset, indexBufferSize);
				D3D12_RESOURCE_BARRIER copyBarriers[] =
				{
					CD3DX12_RESOURCE_BARRIER::Transition(pVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ),
					CD3DX12_RESOURCE_BARRIER::Transition(pIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ),
				};
				commandList.ResourceBarrier(ARRAYSIZE(copyBarriers), copyBarriers);

				m_pBuffers.push_back(pIndexBuffer);
				m_pBuffers.push_back(pVertexBuffer);

				shapeCacheEntry.GeometryDesc = geometryDesc;
				shapeCacheEntry.VertexBufferIndex = VertexBufferIndex;
				shapeCacheEntry.IndexBufferIndex = IndexBufferIndex;
			}

			HitGroupShaderRecord shaderRecord = {};
			shaderRecord.GeometryIndex = geometryCount++;
			shaderRecord.MaterialIndex = materialIndex;
			shaderRecord.VertexBufferIndex = shapeCacheEntry.VertexBufferIndex;
			shaderRecord.IndexBufferIndex = shapeCacheEntry.IndexBufferIndex;

			if (m_bSupportsHardwareRaytracing)
			{
				memcpy(shaderRecord.ShaderIdentifier, pHitGroupShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			}

			hitGroupShaderTable.push_back(shaderRecord);

			InstanceEntry instanceEntry;
			instanceEntry.Transform = transform;
			instanceEntry.pShape = pGeometry.get();

			instanceList.push_back(instanceEntry);
		}

		ComPtr<ID3D12Resource> pUploadHitGroupShaderTable;
		AllocateBufferWithData(commandList, hitGroupShaderTable.data(), hitGroupShaderTable.size() * sizeof(HitGroupShaderRecord), m_pHitGroupShaderTable, pUploadHitGroupShaderTable);
		resourcesToDelete.push_back(pUploadHitGroupShaderTable);

		ComPtr<ID3D12Resource> pUploadMaterialList;
		AllocateBufferWithData(commandList, materialTracker.MaterialList.data(), materialTracker.MaterialList.size() * sizeof(Material), m_pMaterialList, pUploadMaterialList);
		D3D12_SHADER_RESOURCE_VIEW_DESC materialListSRV = {};
		materialListSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		materialListSRV.Format = DXGI_FORMAT_UNKNOWN;
		materialListSRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		materialListSRV.Buffer.NumElements =  materialTracker.MaterialList.size();
		materialListSRV.Buffer.StructureByteStride = sizeof(Material);
		m_pDevice->CreateShaderResourceView(m_pMaterialList.Get(), &materialListSRV, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::MaterialListSRV));
		
		resourcesToDelete.push_back(pUploadMaterialList);

		if (textureAllocator.GetTextureData().size() > 0)
		{
			ComPtr<ID3D12Resource> pUploadTextureData;
			AllocateBufferWithData(commandList, textureAllocator.GetTextureData().data(), textureAllocator.GetTextureData().size() * sizeof(TextureData), m_pTextureDataList, pUploadTextureData);
			resourcesToDelete.push_back(pUploadTextureData);

			D3D12_SHADER_RESOURCE_VIEW_DESC textureDataSRV = {};
			textureDataSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			textureDataSRV.Format = DXGI_FORMAT_UNKNOWN;
			textureDataSRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			textureDataSRV.Buffer.NumElements = textureAllocator.GetTextureData().size();
			textureDataSRV.Buffer.StructureByteStride = sizeof(TextureData);
			m_pDevice->CreateShaderResourceView(m_pTextureDataList.Get(), &textureDataSRV, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::TextureDataSRV));
		}


		ComPtr<ID3D12Resource> pEnvironmentMapScratchBuffer;
		for (UINT lightIndex = 0; lightIndex < pScene->world->lightSources.size(); lightIndex++)
		{
			auto& pLight = pScene->world->lightSources[lightIndex];
			pbrt::InfiniteLightSource::SP pInfiniteLightSource = std::dynamic_pointer_cast<pbrt::InfiniteLightSource>(pLight);
			if (pInfiniteLightSource)
			{
				std::wstring textureName(pInfiniteLightSource->mapName.begin(), pInfiniteLightSource->mapName.end()); 
				InitializeTexture(textureName, *pCommandList.Get(), m_pEnvironmentMap, ViewDescriptorHeapSlots::EnvironmentMapSRVSlot, pEnvironmentMapScratchBuffer);
				m_EnvironmentMapTransform = pInfiniteLightSource->transform.l;
				m_EnvironmentMapColorScale = pInfiniteLightSource->scale;
			}
		}

		if(!m_pEnvironmentMap)
		{
			// Create a black texture cube if there's no area lights in the scene
			auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 6);
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&desc,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_GRAPHICS_PPV_ARGS(m_pEnvironmentMap.ReleaseAndGetAddressOf())));

			m_pDevice->CreateShaderResourceView(m_pEnvironmentMap.Get(), nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::EnvironmentMapSRVSlot));
			m_EnvironmentMapTransform = pbrt::math::affine3f::identity().l;
			m_EnvironmentMapColorScale = pbrt::math::vec3f(1, 1, 1);
		}

		resourcesToDelete.push_back(pEnvironmentMapScratchBuffer);
#if SUPPORT_SW_RAYTRACING
		ComPtr<ID3D12RaytracingFallbackCommandList> m_fallbackCommandList;
		if (EmulateRaytracing())
		{
			m_fallbackDevice->QueryRaytracingCommandList(pCommandList.Get(), IID_PPV_ARGS(&m_fallbackCommandList));

			ID3D12DescriptorHeap* pDescriptorHeaps[] = { m_pViewDescriptorHeap.Get() };
			m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
		}

		std::vector<UINT32> BLASDescriptorList;
#endif

		// Start at 1 assuming there will always be a global BLAS
		instanceCount = 1;
		UINT GlobalBLASTriangleCount = 0;
		for (auto& iter : shapeCache)
		{
			ShapeCacheEntry& shapeCacheEntry = iter.second;
			UINT primitiveCount = shapeCacheEntry.GeometryDesc.Triangles.IndexCount / 3;
			if (true)
			{
				shapeCacheEntry.bIncludeInGlobalBLAS = true;
				GlobalBLASTriangleCount += primitiveCount * shapeCacheEntry.NumInstances;
			}
			else
			{
				instanceCount += shapeCacheEntry.NumInstances;
			}
		}

		UINT32 numShapes = shapeCache.size();
		UINT32 numInstances = instanceList.size();
		
		UINT32 i = 0;
		for(auto &iter : shapeCache)
		{
			ShapeCacheEntry &shapeCacheEntry = iter.second;

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildBottomLevelDesc = {};
			buildBottomLevelDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			buildBottomLevelDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
			buildBottomLevelDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
			buildBottomLevelDesc.Inputs.NumDescs = 1;
			buildBottomLevelDesc.Inputs.pGeometryDescs = &shapeCacheEntry.GeometryDesc;

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
#if SUPPORT_SW_RAYTRACING
			if (EmulateRaytracing())
			{
				m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&buildBottomLevelDesc.Inputs, &prebuildInfo);
			}
			else
#endif
			{
				m_pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&buildBottomLevelDesc.Inputs, &prebuildInfo);
			}
			D3D12_RESOURCE_DESC bottomLevelASDesc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

			ComPtr<ID3D12Resource> pBLAS;
			UINT32 BLASOffset;
			D3D12_GPU_VIRTUAL_ADDRESS BLAS = RaytracingMemoryHeapAllocator.Allocate(prebuildInfo.ResultDataMaxSizeInBytes, &pBLAS, &BLASOffset);

			m_pBottomLevelASList.push_back(BLAS);
			shapeCacheEntry.BLAS = BLAS;

#if SUPPORT_SW_RAYTRACING
			UINT32 BLASDescriptorSlot = AllocateDescriptorHeapSlot();
			BLASDescriptorList.push_back(BLASDescriptorSlot);
			if (EmulateRaytracing())
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC blasDesc = {};
				blasDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				blasDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				blasDesc.Buffer.NumElements = bottomLevelASDesc.Width / sizeof(UINT32);
				blasDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
				blasDesc.Buffer.FirstElement = BLASOffset / sizeof(UINT32);
				m_pDevice->CreateUnorderedAccessView(pBLAS.Get(), nullptr, &blasDesc, GetCPUDescriptorHandle(BLASDescriptorSlot));
				if (USE_FAST_PATH_WITH_FALLBACK)
				{
					m_pDevice->CreateUnorderedAccessView(pBLAS.Get(), nullptr, &blasDesc, GetCPUDescriptorHandle(TopLevelAccelerationStructureUAV));
				}
			}
#endif
			buildBottomLevelDesc.ScratchAccelerationStructureData = RaytracingScratchMemoryHeapAllocator.Allocate(prebuildInfo.ScratchDataSizeInBytes);
			buildBottomLevelDesc.DestAccelerationStructureData = BLAS;

#if SUPPORT_SW_RAYTRACING
			if (EmulateRaytracing())
			{
				m_fallbackCommandList->BuildRaytracingAccelerationStructure(&buildBottomLevelDesc);
			}
			else
#endif
			{
				pCommandList->BuildRaytracingAccelerationStructure(&buildBottomLevelDesc, 0, nullptr);
			}
			i++;
		}

		D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
		pCommandList->ResourceBarrier(1, &uavBarrier);

		{
			std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
			for (UINT32 i = 0; i < numInstances; i++)
			{
				auto &instanceEntry = instanceList[i];
				auto& shapeCacheEntry = shapeCache[instanceEntry.pShape];
				
				D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
				instanceDesc.AccelerationStructure = shapeCacheEntry.BLAS;
#if SUPPORT_SW_RAYTRACING
				if (EmulateRaytracing())
				{
					WRAPPED_GPU_POINTER BLAS = m_fallbackDevice->GetWrappedPointerFromDescriptorHeapIndex(BLASDescriptorList[i]);
					instanceDesc.AccelerationStructure = BLAS.GpuVA;
				}
#endif

				ConvertAffine3f(instanceEntry.Transform, instanceDesc.Transform);
				instanceDesc.InstanceMask = 1;
				instanceDesc.InstanceContributionToHitGroupIndex = i;
				instanceDescs.push_back(instanceDesc);
			}

			ComPtr<ID3D12Resource> pInstanceDescBuffer;

			AllocateUploadBuffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceDescs.size(), pInstanceDescBuffer);
			resourcesToDelete.push_back(pInstanceDescBuffer);
			void* pData;
			pInstanceDescBuffer->Map(0, nullptr, &pData);
			memcpy(pData, instanceDescs.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceDescs.size());

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildTopLevelDesc = {};
			buildTopLevelDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			buildTopLevelDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			buildTopLevelDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
			buildTopLevelDesc.Inputs.NumDescs = numInstances;
			buildTopLevelDesc.Inputs.InstanceDescs = pInstanceDescBuffer->GetGPUVirtualAddress();

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;

#if SUPPORT_SW_RAYTRACING
			if (EmulateRaytracing())
			{
				m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&buildTopLevelDesc.Inputs, &prebuildInfo);
			}
			else
#endif
			{
				m_pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&buildTopLevelDesc.Inputs, &prebuildInfo);
			}
			D3D12_RESOURCE_DESC topLevelASDesc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&topLevelASDesc,
				EmulateRaytracing() ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
				nullptr,
				IID_GRAPHICS_PPV_ARGS(m_pTopLevelAS.ReleaseAndGetAddressOf())));
			
			
#if SUPPORT_SW_RAYTRACING
			if (EmulateRaytracing() && !USE_FAST_PATH_WITH_FALLBACK)
			{
				D3D12_UNORDERED_ACCESS_VIEW_DESC tlasDesc = {};
				tlasDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				tlasDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				tlasDesc.Buffer.NumElements = topLevelASDesc.Width / sizeof(UINT32);
				tlasDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

				m_pDevice->CreateUnorderedAccessView(m_pTopLevelAS.Get(), nullptr, &tlasDesc, GetCPUDescriptorHandle(TopLevelAccelerationStructureUAV));
			}
#endif

			buildTopLevelDesc.ScratchAccelerationStructureData = RaytracingScratchMemoryHeapAllocator.Allocate(prebuildInfo.ScratchDataSizeInBytes);
			buildTopLevelDesc.DestAccelerationStructureData = m_pTopLevelAS->GetGPUVirtualAddress();

#if SUPPORT_SW_RAYTRACING
			if (EmulateRaytracing())
			{
				m_fallbackCommandList->BuildRaytracingAccelerationStructure(&buildTopLevelDesc);
			}
			else
#endif
			{
				pCommandList->BuildRaytracingAccelerationStructure(&buildTopLevelDesc, 0, nullptr);
			}
		}

		textureAllocator.ExtractScratchResources(resourcesToDelete);

		auto d3d12SceneLoadEnd = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(d3d12SceneLoadEnd - d3d12SceneLoadStart);
		std::string d3d12SceneLoadLengthMessage = "D3D12 Scene Load time: " + std::to_string(0.001f * (float)duration.count()) + " seconds";
		OutputDebugString(d3d12SceneLoadLengthMessage.c_str());
	}

	{
		ComPtr<ID3D12Resource> pBlueNoise0UploadHeap;
		ComPtr<ID3D12Resource> pBlueNoise1UploadHeap;
		InitializeTexture(L"Textures/LDR_RGBA_0.png", commandList, m_pBlueNoise0Texture, ViewDescriptorHeapSlots::BlueNoise0SRVSlot, pBlueNoise0UploadHeap, true);
		InitializeTexture(L"Textures/LDR_RGBA_1.png", commandList, m_pBlueNoise1Texture, ViewDescriptorHeapSlots::BlueNoise1SRVSlot, pBlueNoise1UploadHeap, true);

		resourcesToDelete.push_back(pBlueNoise0UploadHeap);
		resourcesToDelete.push_back(pBlueNoise1UploadHeap);
	}	

	auto& uploadResourceList = UploadHeapAllocator.GetAllocatedResources();
	for (auto &pResource : uploadResourceList)
	{
		resourcesToDelete.push_back(pResource);
	}

	auto& scratchResourceList = RaytracingScratchMemoryHeapAllocator.GetAllocatedResources();
	for (auto& pResource : scratchResourceList)
	{
		resourcesToDelete.push_back(pResource);
	}

	auto& raytracingResourceList = RaytracingMemoryHeapAllocator.GetAllocatedResources();
	for (auto& pResource : raytracingResourceList)
	{
		m_pBuffers.push_back(pResource);
	}
}

void TracerBoy::UpdateOutputSettings(const OutputSettings& outputSettings)
{
	if (m_CachedOutputSettings.m_EnableNormalMaps != outputSettings.m_EnableNormalMaps ||
		m_CachedOutputSettings.m_renderMode != outputSettings.m_renderMode ||
		m_CachedOutputSettings.m_cameraSettings.m_FocalDistance != outputSettings.m_cameraSettings.m_FocalDistance ||
		m_CachedOutputSettings.m_cameraSettings.m_DOFFocalDistance  != outputSettings.m_cameraSettings.m_DOFFocalDistance ||
		m_CachedOutputSettings.m_cameraSettings.m_ApertureWidth != outputSettings.m_cameraSettings.m_ApertureWidth ||
		m_CachedOutputSettings.m_performanceSettings.m_bEnableBlueNoise != outputSettings.m_performanceSettings.m_bEnableBlueNoise)
	{
		InvalidateHistory(m_CachedOutputSettings.m_renderMode != outputSettings.m_renderMode);
	}
	m_CachedOutputSettings = outputSettings;
}


void TracerBoy::InitializeTexture(
	const std::wstring &textureName,
	ID3D12GraphicsCommandList& commandList,
	ComPtr<ID3D12Resource>& pResource,
	UINT SRVSlot,
	ComPtr<ID3D12Resource>& pUploadResource,
	bool bIsInternalAsset )
{
	std::wstring directory(m_sceneFileDirectory.begin(), m_sceneFileDirectory.end());
	std::wstring fullTextureName = bIsInternalAsset ? textureName : directory + textureName;

	DirectX::TexMetadata texMetaData = {};
	DirectX::ScratchImage scratchImage = {};
	std::wstring fileExt = textureName.substr(textureName.size() - 4, 4);

	auto start = textureName.find_last_of('\\');
	if (fileExt.compare(L".hdr") == 0)
	{
		VERIFY_HRESULT(DirectX::LoadFromHDRFile(fullTextureName.c_str(), &texMetaData, scratchImage));
	}
	else if (fileExt.compare(L".tga") == 0)
	{
		VERIFY_HRESULT(DirectX::LoadFromTGAFile(fullTextureName.c_str(), &texMetaData, scratchImage));
	}
	else if (fileExt.compare(L".dds") == 0)
	{
		VERIFY_HRESULT(DirectX::LoadFromDDSFile(fullTextureName.c_str(), DDS_FLAGS_NO_16BPP, &texMetaData, scratchImage));
	}
	else
	{
		VERIFY_HRESULT(DirectX::LoadFromWICFile(fullTextureName.c_str(), WIC_FLAGS_NONE, &texMetaData, scratchImage));
	}
	VERIFY_HRESULT(DirectX::CreateTextureEx(m_pDevice.Get(), texMetaData, D3D12_RESOURCE_FLAG_NONE, false, pResource.ReleaseAndGetAddressOf()));

	m_pDevice->CreateShaderResourceView(pResource.Get(), nullptr, GetCPUDescriptorHandle(SRVSlot));

	std::vector < D3D12_SUBRESOURCE_DATA> subresources;
	VERIFY_HRESULT(PrepareUpload(m_pDevice.Get(), scratchImage.GetImages(), scratchImage.GetImageCount(), texMetaData, subresources));

	const UINT64 uploadBufferSize = GetRequiredIntermediateSizeHelper(m_pDevice.Get(), pResource.Get(), 0, texMetaData.mipLevels);

	AllocateUploadBuffer(uploadBufferSize, pUploadResource);

	UpdateSubresourcesHelper(m_pDevice.Get(), &commandList,
		pResource.Get(), pUploadResource.Get(),
		0, 0, static_cast<unsigned int>(subresources.size()),
		subresources.data());

	D3D12_RESOURCE_BARRIER barriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(pResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
	};
	commandList.ResourceBarrier(ARRAYSIZE(barriers), barriers);
}

void TracerBoy::InitializeLocalRootSignature()
{
	if (!EmulateRaytracing())
	{
		CD3DX12_ROOT_PARAMETER1 Parameters[LocalRayTracingRootSignatureParameters::NumLocalRayTracingParameters];
		Parameters[LocalRayTracingRootSignatureParameters::GeometryIndexRootConstant].InitAsConstants(4, 2);

		D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
		rootSignatureDesc.pParameters = Parameters;
		rootSignatureDesc.NumParameters = ARRAYSIZE(Parameters);
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

		ComPtr<ID3DBlob> pRootSignatureBlob;
		VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
		VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_GRAPHICS_PPV_ARGS(m_pLocalRootSignature.ReleaseAndGetAddressOf())));
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE TracerBoy::GetCPUDescriptorHandle(UINT slot)
{
	auto descriptorHeapBase = m_pViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	auto descriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(m_pViewDescriptorHeap->GetDesc().Type);
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapBase, slot, descriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE TracerBoy::GetNonShaderVisibleCPUDescriptorHandle(UINT slot)
{
	auto descriptorHeapBase = m_pNonShaderVisibleDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	auto descriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(m_pNonShaderVisibleDescriptorHeap->GetDesc().Type);
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapBase, slot, descriptorSize);
}

D3D12_GPU_DESCRIPTOR_HANDLE TracerBoy::GetGPUDescriptorHandle(UINT slot)
{
	auto descriptorHeapBase = m_pViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	auto descriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(m_pViewDescriptorHeap->GetDesc().Type);
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(descriptorHeapBase, slot, descriptorSize);
}

void TracerBoy::AllocateUploadBuffer(UINT bufferSize, ComPtr<ID3D12Resource> &pBuffer)
{
	const D3D12_HEAP_PROPERTIES uploadHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
		&uploadHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_GRAPHICS_PPV_ARGS(pBuffer.ReleaseAndGetAddressOf())));
}

void TracerBoy::AllocateBufferWithData(const void* pData, UINT dataSize, ComPtr<ID3D12Resource>& pBuffer)
{
	AllocateUploadBuffer(dataSize, pBuffer);
	void* pMappedData;
	pBuffer->Map(0, nullptr, &pMappedData);
	memcpy(pMappedData, pData, dataSize);
	pBuffer->Unmap(0, nullptr);
}

void TracerBoy::AllocateBufferWithData(
	ID3D12GraphicsCommandList& CommandList,
	const void* pData, 
	UINT dataSize,
	ComPtr<ID3D12Resource>& pBuffer,
	ComPtr<ID3D12Resource>& pUploadBuffer)
{
	AllocateBufferWithData(pData, dataSize, pUploadBuffer);

	const D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(dataSize);
	VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
		&defaultHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_GRAPHICS_PPV_ARGS(pBuffer.ReleaseAndGetAddressOf())));

	CommandList.CopyResource(pBuffer.Get(), pUploadBuffer.Get());
	D3D12_RESOURCE_BARRIER copyBarriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(pBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ),
	};
	CommandList.ResourceBarrier(ARRAYSIZE(copyBarriers), copyBarriers);

}


D3D12_GPU_DESCRIPTOR_HANDLE TracerBoy::GetOutputSRV(OutputType outputType)
{
	UINT slot;
	switch (outputType)
	{
	case OutputType::Lit:
	case OutputType::Luminance:
	default:
		slot = GetPathTracerOutputSRV();
		break;
	case OutputType::Albedo:
	case OutputType::LivePixels:
		slot = ViewDescriptorHeapSlots::AOVCustomOutputSRV;
		break;
	case OutputType::Normals:
		slot = ViewDescriptorHeapSlots::AOVNormalsSRV;
		break;
	case OutputType::LuminanceVariance:
		slot = ViewDescriptorHeapSlots::LuminanceVarianceSRV;
		break;
	}
	return GetGPUDescriptorHandle(slot);
}

UINT ShaderOutputType(TracerBoy::OutputType type)
{
	switch (type)
	{
	default:
	case TracerBoy::OutputType::Lit:
		return OUTPUT_TYPE_LIT;
	case TracerBoy::OutputType::Normals:
		return OUTPUT_TYPE_NORMAL;
	case TracerBoy::OutputType::Albedo:
		return OUTPUT_TYPE_ALBEDO;
	case TracerBoy::OutputType::LuminanceVariance:
		return OUTPUT_TYPE_VARIANCE;
	case TracerBoy::OutputType::Luminance:
		return OUTPUT_TYPE_LUMINANCE;
	case TracerBoy::OutputType::LivePixels:
		return OUTPUT_TYPE_LIVE_PIXELS;
	case TracerBoy::OutputType::LiveWaves:
		return OUTPUT_TYPE_LIVE_WAVES;
	}
}

void SetRootSignature(bool bIsGraphics, ID3D12GraphicsCommandList& commandList, ID3D12RootSignature& rootSignature)
{
	if (bIsGraphics)
	{
		commandList.SetGraphicsRootSignature(&rootSignature);
	}
	else
	{
		commandList.SetComputeRootSignature(&rootSignature);
	}
}

void SetRoot32BitConstants(
	bool bIsGraphics,
	ID3D12GraphicsCommandList& commandList,
	_In_  UINT RootParameterIndex,
	_In_  UINT Num32BitValuesToSet,
	_In_reads_(Num32BitValuesToSet * sizeof(UINT))  const void* pSrcData,
	_In_  UINT DestOffsetIn32BitValues)
{
	if (bIsGraphics)
	{
		commandList.SetGraphicsRoot32BitConstants(RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
	}
	else
	{
		commandList.SetComputeRoot32BitConstants(RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
	}
}

void SetRootConstantBufferView(
	bool bIsGraphics,
	ID3D12GraphicsCommandList& commandList,
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
	if (bIsGraphics)
	{
		commandList.SetGraphicsRootConstantBufferView(RootParameterIndex, BufferLocation);
	}
	else
	{
		commandList.SetComputeRootConstantBufferView(RootParameterIndex, BufferLocation);
	}
}

void SetRootUnorderedAccessView(
	bool bIsGraphics,
	ID3D12GraphicsCommandList& commandList,
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
	if (bIsGraphics)
	{
		commandList.SetGraphicsRootUnorderedAccessView(RootParameterIndex, BufferLocation);
	}
	else
	{
		commandList.SetComputeRootUnorderedAccessView(RootParameterIndex, BufferLocation);
	}
}

void SetRootShaderResourceView(
	bool bIsGraphics,
	ID3D12GraphicsCommandList& commandList,
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_VIRTUAL_ADDRESS BufferLocation)
{
	if (bIsGraphics)
	{
		commandList.SetGraphicsRootShaderResourceView(RootParameterIndex, BufferLocation);
	}
	else
	{
		commandList.SetComputeRootShaderResourceView(RootParameterIndex, BufferLocation);
	}
}

void SetRootDescriptorTable(
	bool bIsGraphics,
	ID3D12GraphicsCommandList& commandList,
	_In_  UINT RootParameterIndex,
	_In_  D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor)
{
	if (bIsGraphics)
	{
		commandList.SetGraphicsRootDescriptorTable(RootParameterIndex, BaseDescriptor);
	}
	else
	{
		commandList.SetComputeRootDescriptorTable(RootParameterIndex, BaseDescriptor);
	}
}


void TracerBoy::Render(ID3D12GraphicsCommandList& commandList, ID3D12Resource* pBackBuffer, ID3D12Resource* pReadbackStats, const OutputSettings& outputSettings)
{
	bool bUnderSampleLimit = outputSettings.m_debugSettings.m_SampleLimit == 0 || m_SamplesRendered < outputSettings.m_debugSettings.m_SampleLimit;
	bool bUnderTimeLimit = outputSettings.m_debugSettings.m_TimeLimitInSeconds <= 0.0 ||
		((float)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_RenderStartTime).count() / 1000.0f) < outputSettings.m_debugSettings.m_TimeLimitInSeconds;
	bool bRender = bUnderSampleLimit && bUnderTimeLimit;

	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS1 option1;
		VERIFY_HRESULT(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &option1, sizeof(option1)));

		m_MinWaveAmount = outputSettings.m_performanceSettings.m_OccupancyMultiplier * option1.TotalLaneCount / option1.WaveLaneCountMax;
	}

	const float DefaultConvergenceIncrement = 0.0001;
	if (m_SamplesRendered == 0)
	{
		m_ConvergenceIncrement = DefaultConvergenceIncrement;
		m_ConvergencePercentPad = 0.1;
		m_LastFrameTime = std::chrono::steady_clock::now();
	}
	else if (m_SamplesRendered % FramesPerConvergencePercentIncrement == 0)
	{
		float frameTime = ((float)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_LastFrameTime).count()) / (float)FramesPerConvergencePercentIncrement;
		float targetFrame = 1000.0f / (float)outputSettings.m_performanceSettings.m_TargetFrameRate;
		if (frameTime < targetFrame && m_ConvergenceIncrement > 0.0)
		{
			// If we're going faster than the target frame rate, decrease convergence percent to increase active waves
			m_ConvergenceIncrement = -DefaultConvergenceIncrement;
		}
		else if (frameTime > targetFrame && m_ConvergenceIncrement < 0.0)
		{
			// If we're going slower than the target frame rate, increase convergence percent to decrease active waves
			m_ConvergenceIncrement = DefaultConvergenceIncrement;
		}
		else
		{
			float IncrementMultiplier = std::min(1.0 + 0.25 * abs(frameTime - targetFrame) / frameTime, 2.0);;
			m_ConvergenceIncrement *= IncrementMultiplier;
		}

		if (abs(m_ConvergenceIncrement) > m_ConvergencePercentPad * 0.25)
		{
			m_ConvergenceIncrement = (m_ConvergencePercentPad * 0.25) * (m_ConvergenceIncrement > 0.0 ? 1.0 : -1.0);
		}

		m_ConvergencePercentPad += m_ConvergenceIncrement;
		m_ConvergencePercentPad = std::max(0.0f, m_ConvergencePercentPad);
		m_LastFrameTime = std::chrono::steady_clock::now();

	}

	UpdateOutputSettings(outputSettings);

	ResizeBuffersIfNeeded(pBackBuffer);
	VERIFY(GetPathTracerOutput() && m_pPostProcessOutput);

	ID3D12DescriptorHeap* pDescriptorHeaps[] = { m_pViewDescriptorHeap.Get() };
	commandList.SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(m_pPostProcessOutput.Get());

	if (m_bInvalidateHistory)
	{
		m_SamplesRendered = 0;
		m_RenderStartTime = std::chrono::steady_clock::now();

		D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_pDepthDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		commandList.ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0, 0, 0, nullptr);
	}

	const UINT ZeroValue[4] = {};
	commandList.ClearUnorderedAccessViewUint(
		GetGPUDescriptorHandle(ViewDescriptorHeapSlots::StatsBufferUAV),
		GetNonShaderVisibleCPUDescriptorHandle(ViewDescriptorHeapSlots::StatsBufferUAV),
		m_pStatsBuffer.Get(),
		ZeroValue,
		0,
		nullptr);

	const bool bIsGraphics = !EmulateRaytracing() && outputSettings.m_performanceSettings.m_bEnableInlineRaytracing && m_bSupportsInlineRaytracing && outputSettings.m_performanceSettings.m_bEnablePixelShaderRaytracing;
	SetRootSignature(bIsGraphics, commandList, *m_pRayTracingRootSignature.Get());

	ComPtr<ID3D12GraphicsCommandList5> pRaytracingCommandList;
	commandList.QueryInterface(IID_GRAPHICS_PPV_ARGS(pRaytracingCommandList.ReleaseAndGetAddressOf()));

	SYSTEMTIME time;
	GetSystemTime(&time);
	PerFrameConstants constants = {};
	constants.CameraPosition = { m_camera.Position.x, m_camera.Position.y, m_camera.Position.z };
	constants.CameraLookAt = { m_camera.LookAt.x, m_camera.LookAt.y, m_camera.LookAt.z };
	constants.CameraRight = { m_camera.Right.x, m_camera.Right.y, m_camera.Right.z };
	constants.CameraUp = { m_camera.Up.x, m_camera.Up.y, m_camera.Up.z };

	constants.Time = static_cast<float>(time.wMilliseconds) / 1000.0f;
	constants.EnableNormalMaps = outputSettings.m_EnableNormalMaps;
	constants.FocalDistance = m_camera.FocalDistance;
	constants.DOFFocusDistance = outputSettings.m_cameraSettings.m_DOFFocalDistance;
	constants.DOFApertureWidth = outputSettings.m_cameraSettings.m_ApertureWidth;
	constants.InvalidateHistory = m_bInvalidateHistory;
	constants.FireflyClampValue = outputSettings.m_denoiserSettings.m_fireflyClampValue;
	constants.GlobalFrameCount = m_SamplesRendered;
	constants.MinConvergence = outputSettings.m_performanceSettings.m_ConvergencePercentage;
	constants.UseBlueNoise = outputSettings.m_performanceSettings.m_bEnableBlueNoise;
	constants.IsRealTime = outputSettings.m_renderMode == RenderMode::RealTime;
	constants.OutputMode = ShaderOutputType(outputSettings.m_OutputType);

	if (outputSettings.m_performanceSettings.m_TargetFrameRate > 0)
	{
		constants.MinConvergence += m_ConvergencePercentPad;
	}

#if SUPPORT_VOLUMES
	constants.VolumeMin = { m_volumeMin.x / 2.0f - constants.fogScatterDirection, m_volumeMin.y / 2.0f, m_volumeMin.z / 2.0f - constants.fogScatterDirection };
	constants.VolumeMax = { m_volumeMax.x / 2.0f - constants.fogScatterDirection, m_volumeMax.y / 2.0f, m_volumeMax.z / 2.0f - constants.fogScatterDirection };
#endif

	SetRoot32BitConstants(bIsGraphics, commandList, RayTracingRootSignatureParameters::PerFrameConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);
	SetRootConstantBufferView(bIsGraphics, commandList, RayTracingRootSignatureParameters::ConfigConstantsParam, m_pConfigConstants->GetGPUVirtualAddress());

#if SUPPORT_SW_RAYTRACING
	ComPtr<ID3D12RaytracingFallbackCommandList> m_fallbackCommandList;
	if (EmulateRaytracing())
	{
		m_fallbackDevice->QueryRaytracingCommandList(&commandList, IID_PPV_ARGS(&m_fallbackCommandList));

		WRAPPED_GPU_POINTER TLAS;
#if USE_FAST_PATH_WITH_FALLBACK
		TLAS.GpuVA = m_pBottomLevelASList[0];
#else 
		TLAS = m_fallbackDevice->GetWrappedPointerFromDescriptorHeapIndex(TopLevelAccelerationStructureUAV);
#endif
		m_fallbackCommandList->BindDescriptorHeap(m_pRayTracingRootSignature.Get(), m_pViewDescriptorHeap.Get(), TLAS);
	}
	else
#endif
	{
		SetRootShaderResourceView(bIsGraphics, commandList, RayTracingRootSignatureParameters::AccelerationStructureRootSRV, m_pTopLevelAS->GetGPUVirtualAddress());
	}
	
	SetRootDescriptorTable(bIsGraphics, commandList, RayTracingRootSignatureParameters::SystemTexturesDescriptorTable, GetGPUDescriptorHandle(ViewDescriptorHeapSlots::SystemTexturesBaseSlot));
#if SUPPORT_VOLUMES
	if (m_pVolume)
	{
		commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::VolumeSRVParam, GetGPUDescriptorHandle(ViewDescriptorHeapSlots::VolumeSRVSlot));
	}
#endif

	D3D12_RESOURCE_BARRIER preDispatchRaysBarrier[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(GetPathTracerOutput(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pJitteredAccumulatedPathTracerOutput.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	};
	commandList.ResourceBarrier(ARRAYSIZE(preDispatchRaysBarrier), preDispatchRaysBarrier);

	SetRootDescriptorTable(bIsGraphics, commandList, RayTracingRootSignatureParameters::PreviousFrameOutput, GetGPUDescriptorHandle(GetPreviousFramePathTracerOutputSRV()));
	SetRootDescriptorTable(bIsGraphics, commandList, RayTracingRootSignatureParameters::SceneDescriptorTable, GetGPUDescriptorHandle(ViewDescriptorHeapSlots::SceneDescriptorsBaseSlot));
	SetRootDescriptorTable(bIsGraphics, commandList, RayTracingRootSignatureParameters::OutputUAV, GetGPUDescriptorHandle(GetPathTracerOutputUAV()));
	SetRootDescriptorTable(bIsGraphics, commandList, RayTracingRootSignatureParameters::JitteredOutputUAV, GetGPUDescriptorHandle(ViewDescriptorHeapSlots::JitteredPathTracerOutputUAV));
	SetRootDescriptorTable(bIsGraphics, commandList, RayTracingRootSignatureParameters::ImageTextureTable, m_pViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	SetRootDescriptorTable(bIsGraphics, commandList, RayTracingRootSignatureParameters::AOVDescriptorTable, GetGPUDescriptorHandle(ViewDescriptorHeapSlots::AOVBaseUAVSlot));
	SetRootShaderResourceView(bIsGraphics, commandList, RayTracingRootSignatureParameters::ShaderTable, m_pHitGroupShaderTable->GetGPUVirtualAddress());
	SetRootUnorderedAccessView(bIsGraphics, commandList, RayTracingRootSignatureParameters::StatsBuffer, m_pStatsBuffer->GetGPUVirtualAddress());

	if (bRender)
	{
        PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"Path Trace");

		if (outputSettings.m_performanceSettings.m_bEnableInlineRaytracing && m_bSupportsInlineRaytracing)
		{
			if (!EmulateRaytracing() && outputSettings.m_performanceSettings.m_bEnablePixelShaderRaytracing)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_pDepthDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

				commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				commandList.RSSetViewports(1, &viewport);

				D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, viewport.Width, viewport.Height);
				commandList.RSSetScissorRects(1, &scissorRect);

				commandList.OMSetRenderTargets(0, nullptr, true, &dsv);

				if (m_SamplesRendered % 32 == 0)
				{
					commandList.OMSetStencilRef(0x1);
					commandList.SetPipelineState(m_pVarianceStencilPSO.Get());
					commandList.DrawInstanced(3, 1, 0, 0);
				}
				
				commandList.OMSetStencilRef(0x0);
				commandList.SetPipelineState(m_pPixelShaderRayTracingPSO.Get());
				commandList.DrawInstanced(3, 1, 0, 0);
			}
			else
			{
				commandList.SetPipelineState(EmulateRaytracing() ? m_pSoftwareRayTracingPSO.Get() : m_pRayTracingPSO.Get());

				UINT DispatchWidth = (viewport.Width - 1) / RAYTRACE_THREAD_GROUP_WIDTH + 1;
				UINT DispatchHeight = (viewport.Height - 1) / RAYTRACE_THREAD_GROUP_HEIGHT + 1;
				commandList.Dispatch(DispatchWidth, DispatchHeight, 1);
			}
		}
		else
		{
			pRaytracingCommandList->SetPipelineState1(m_pRayTracingStateObject.Get());

			D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
			dispatchDesc.Width = static_cast<UINT>(viewport.Width);
			dispatchDesc.Height = static_cast<UINT>(viewport.Height);
			dispatchDesc.Depth = 1;
			dispatchDesc.RayGenerationShaderRecord.StartAddress = m_pRayGenShaderTable->GetGPUVirtualAddress();
			dispatchDesc.RayGenerationShaderRecord.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
			dispatchDesc.HitGroupTable.StartAddress = m_pHitGroupShaderTable->GetGPUVirtualAddress();
			dispatchDesc.HitGroupTable.SizeInBytes = m_pHitGroupShaderTable->GetDesc().Width;
			dispatchDesc.HitGroupTable.StrideInBytes = sizeof(HitGroupShaderRecord);
			dispatchDesc.MissShaderTable.StartAddress = m_pMissShaderTable->GetGPUVirtualAddress();
			dispatchDesc.MissShaderTable.SizeInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
			dispatchDesc.MissShaderTable.StrideInBytes = 0; // Only 1 entry

			pRaytracingCommandList->DispatchRays(&dispatchDesc);
		}

		m_SamplesRendered++;
	}

	D3D12_RESOURCE_BARRIER postDispatchRaysBarrier[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(GetPathTracerOutput(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pJitteredAccumulatedPathTracerOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVNormals.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVWorldPosition[0].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVWorldPosition[1].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVEmissive.Get(),	D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVCustomOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(m_pStatsBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
	};
	commandList.ResourceBarrier(ARRAYSIZE(postDispatchRaysBarrier), postDispatchRaysBarrier);
	commandList.CopyBufferRegion(pReadbackStats, 0, m_pStatsBuffer.Get(), 0, sizeof(ReadbackStats));
	
	D3D12_GPU_DESCRIPTOR_HANDLE PostProcessInput = GetOutputSRV(outputSettings.m_OutputType);
	if(outputSettings.m_renderMode == RenderMode::RealTime)
	{
		UINT32 previousFrameIndex = m_ActiveFrameIndex == 0 ? MaxActiveFrames - 1 : (m_ActiveFrameIndex - 1);

		TemporalAccumulationPass::MomentResources momentResources = {
			*(m_pMomentBuffer[m_ActiveFrameIndex].m_pResource.Get()),
			m_pMomentBuffer[m_ActiveFrameIndex].m_uavHandle,
			m_pMomentBuffer[previousFrameIndex].m_srvHandle
		};

		PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"TAA - Indirect Lighting");
		PostProcessInput = m_pTemporalAccumulationPass->Run(commandList,
			m_pIndirectLightingTemporalOutput[m_ActiveFrameIndex],
			m_pIndirectLightingTemporalOutput[previousFrameIndex].m_srvHandle,
			PostProcessInput,
			GetGPUDescriptorHandle(GetWorldPositionSRV()),
			GetGPUDescriptorHandle(GetPreviousFrameWorldPositionSRV()),
			GetGPUDescriptorHandle(ViewDescriptorHeapSlots::AOVNormalsSRV),
			m_camera,
			m_prevFrameCamera,
			0.95,
			m_SamplesRendered == 0,
			viewport.Width,
			viewport.Height,
			&momentResources);
	}

	if(outputSettings.m_renderMode == RenderMode::RealTime && outputSettings.m_denoiserSettings.m_bEnabled && outputSettings.m_OutputType == OutputType::Lit)
	{
        PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"Denoise");
		PostProcessInput = m_pDenoiserPass->Run(commandList,
			m_pDenoiserBuffers,
			outputSettings.m_denoiserSettings,
			PostProcessInput,
			GetGPUDescriptorHandle(ViewDescriptorHeapSlots::AOVNormalsSRV),
			GetGPUDescriptorHandle(GetWorldPositionSRV()),
			m_SamplesRendered,
			viewport.Width,
			viewport.Height);
	}

	if(outputSettings.m_renderMode == RenderMode::RealTime)
	{
		{
			PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"Composite Albedo");
			D3D12_RESOURCE_BARRIER preCompositeBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(m_pComposittedOutput.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			commandList.ResourceBarrier(ARRAYSIZE(preCompositeBarriers), preCompositeBarriers);

			commandList.SetComputeRootSignature(m_pCompositeAlbedoRootSignature.Get());
			commandList.SetPipelineState(m_pCompositeAlbedoPSO.Get());

			UINT DispatchWidth = (viewport.Width - 1) / COMPOSITE_ALBEDO_THREAD_GROUP_WIDTH + 1;
			UINT DispatchHeight = (viewport.Height - 1) / COMPOSITE_ALBEDO_THREAD_GROUP_HEIGHT + 1;

			commandList.SetComputeRootDescriptorTable(
				CompositeAlbedoInputAlbedo,
				GetGPUDescriptorHandle(ViewDescriptorHeapSlots::AOVCustomOutputSRV));
			commandList.SetComputeRootDescriptorTable(
				CompositeAlbedoIndirectLighting,
				PostProcessInput);
			commandList.SetComputeRootDescriptorTable(
				CompositeAlbedoEmissive,
				GetGPUDescriptorHandle(ViewDescriptorHeapSlots::AOVEmissiveSRV));
			commandList.SetComputeRootDescriptorTable(
				CompositeAlbedoOutputTexture,
				GetGPUDescriptorHandle(ViewDescriptorHeapSlots::ComposittedOutputUAV));

			commandList.Dispatch(DispatchWidth, DispatchHeight, 1);

			D3D12_RESOURCE_BARRIER postCompositeBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(m_pComposittedOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			commandList.ResourceBarrier(ARRAYSIZE(postCompositeBarriers), postCompositeBarriers);
			PostProcessInput = GetGPUDescriptorHandle(ComposittedOutputSRV);
		}
        
		{
			UINT32 previousFrameIndex = m_ActiveFrameIndex == 0 ? MaxActiveFrames - 1 : (m_ActiveFrameIndex - 1);

			PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"TAA");
			PostProcessInput = m_pTemporalAccumulationPass->Run(commandList,
				m_pFinalTemporalOutput[m_ActiveFrameIndex],
				m_pFinalTemporalOutput[previousFrameIndex].m_srvHandle,
				PostProcessInput,
				GetGPUDescriptorHandle(GetWorldPositionSRV()),
				GetGPUDescriptorHandle(GetPreviousFrameWorldPositionSRV()),
				GetGPUDescriptorHandle(ViewDescriptorHeapSlots::AOVNormalsSRV),
				m_camera,
				m_prevFrameCamera,
				0.95,
				m_SamplesRendered == 0,
				viewport.Width,
				viewport.Height);
		}
	}

	ComPtr<ID3D12Resource> FinalBuffer;
	{
        PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"Post Processing");

		commandList.SetComputeRootSignature(m_pPostProcessRootSignature.Get());
		commandList.SetPipelineState(m_pPostProcessPSO.Get());

		auto outputDesc = m_pPostProcessOutput->GetDesc();
		auto& postProcessSettings = outputSettings.m_postProcessSettings;
		PostProcessConstants postProcessConstants;
		postProcessConstants.Resolution.x = static_cast<UINT32>(outputDesc.Width);
		postProcessConstants.Resolution.y = static_cast<UINT32>(outputDesc.Height);
		postProcessConstants.ExposureMultiplier = postProcessSettings.m_ExposureMultiplier;
		postProcessConstants.UseGammaCorrection = postProcessSettings.m_bEnableGammaCorrection;
		postProcessConstants.UseToneMapping = postProcessSettings.m_bEnableToneMapping;
		postProcessConstants.OutputType = ShaderOutputType(outputSettings.m_OutputType);
		postProcessConstants.VarianceMultiplier = outputSettings.m_debugSettings.m_VarianceMultiplier;

		commandList.SetComputeRoot32BitConstants(PostProcessRootSignatureParameters::Constants, sizeof(postProcessConstants) / sizeof(UINT32), &postProcessConstants, 0);
		commandList.SetComputeRootDescriptorTable(
			PostProcessRootSignatureParameters::InputTexture,
			PostProcessInput);
		commandList.SetComputeRootDescriptorTable(
			PostProcessRootSignatureParameters::AuxTexture,
			GetGPUDescriptorHandle(ViewDescriptorHeapSlots::AOVCustomOutputSRV));
		commandList.SetComputeRootDescriptorTable(
			PostProcessRootSignatureParameters::OutputTexture,
			GetGPUDescriptorHandle(ViewDescriptorHeapSlots::PostProcessOutputUAV));

		UINT DispatchWidth = (postProcessConstants.Resolution.x - 1) / 8 + 1;
		UINT DispatchHeight = (postProcessConstants.Resolution.y - 1) / 8 + 1;

		commandList.Dispatch(DispatchWidth, DispatchHeight, 1);
		FinalBuffer = m_pPostProcessOutput;
	}

	if(outputSettings.m_postProcessSettings.m_bEnableFSR)
	{
		auto inputDesc = m_pPostProcessOutput->GetDesc();
		m_pFidelityFXSuperResolutionPass->Run(
			commandList,
			m_pUpscaleOutput,
			m_pUpscaleItermediateOutput,
			GetGPUDescriptorHandle(ViewDescriptorHeapSlots::PostProcessOutputSRV),
			inputDesc.Width,
			inputDesc.Height);
		FinalBuffer = m_pUpscaleOutput.m_pResource;
	}

	{
		D3D12_RESOURCE_BARRIER preCopyBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_pPostProcessOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(pBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
		};
		commandList.ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);
		commandList.CopyResource(pBackBuffer, FinalBuffer.Get());

		D3D12_RESOURCE_BARRIER postCopyBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_pPostProcessOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(pBackBuffer,D3D12_RESOURCE_STATE_COPY_DEST,  D3D12_RESOURCE_STATE_RENDER_TARGET),
			CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVNormals.Get(),		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVWorldPosition[0].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVWorldPosition[1].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVEmissive.Get(),	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_pAOVCustomOutput.Get(),	D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(m_pStatsBuffer.Get(),	D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		};
		commandList.ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
	}

	if (bRender)
	{
		m_ActiveFrameIndex = (m_ActiveFrameIndex + 1) % MaxActiveFrames;
		m_prevFrameCamera = m_camera;
	}
	m_bInvalidateHistory = false;
}


void TracerBoy::UpdateConfigConstants(UINT backBufferWidth, UINT backBufferHeight)
{
	ConfigConstants configConstants;
	configConstants.CameraLensHeight = m_camera.LensHeight;
	configConstants.FlipTextureUVs = m_flipTextureUVs;

	configConstants.EnvironmentMapTransform.vx = ConvertFloat4(m_EnvironmentMapTransform.vx, 0.0);
	configConstants.EnvironmentMapTransform.vy = ConvertFloat4(m_EnvironmentMapTransform.vy, 0.0);
	configConstants.EnvironmentMapTransform.vz = ConvertFloat4(m_EnvironmentMapTransform.vz, 0.0);
	configConstants.EnvironmentMapColorScale = ConvertFloat3(m_EnvironmentMapColorScale);

	AllocateBufferWithData(&configConstants, sizeof(configConstants), m_pConfigConstants);
}

void TracerBoy::Update(int mouseX, int mouseY, bool keyboardInput[CHAR_MAX], float dt, const ControllerState &controllerState, const CameraSettings& cameraSettings)
{
	bool bCameraMoved = false;

	float yaw = 0.0;
	float pitch = 0.0;
	const float rotationScaler = 0.5f;
	if (m_pPostProcessOutput && !cameraSettings.m_ignoreMouse)
	{
		auto outputDesc = m_pPostProcessOutput->GetDesc();

		yaw = rotationScaler * 2.0 * 6.28f * ((float)mouseX - (float)m_mouseX) / (float)outputDesc.Width;
		pitch = rotationScaler * 3.14f * ((float)mouseY - (float)m_mouseY) / (float)outputDesc.Height;
	}

	const float ControllerDeadzone = 0.2;
	if (abs(controllerState.m_RightStickX) > ControllerDeadzone || abs(controllerState.m_RightStickY) > ControllerDeadzone)
	{
		const float rotationScaler = 0.001f;
		yaw += controllerState.m_RightStickX * rotationScaler * dt;
		pitch += -controllerState.m_RightStickY * rotationScaler * dt;
		bCameraMoved = true;
	}

	if (m_mouseX != mouseX || m_mouseY != mouseY)
	{
		if (!cameraSettings.m_ignoreMouse)
		{
			bCameraMoved = true;
		}
		m_mouseX = mouseX;
		m_mouseY = mouseY;
	}
	
	XMVECTOR RightAxis = XMVectorSet(m_camera.Right.x, m_camera.Right.y, m_camera.Right.z, 1.0);
	XMVECTOR UpAxis = XMVectorSet(m_camera.Up.x, m_camera.Up.y, m_camera.Up.z, 1.0);
	XMVECTOR Position = XMVectorSet(m_camera.Position.x, m_camera.Position.y, m_camera.Position.z, 1.0);
	XMVECTOR LookAt = XMVectorSet(m_camera.LookAt.x, m_camera.LookAt.y, m_camera.LookAt.z, 1.0);
	XMVECTOR ViewDir = LookAt - Position;

	XMVECTOR GlobalUp = IS_Y_AXIS_UP ?  XMVectorSet(0.0, 1.0, 0.0, 1.0) : XMVectorSet(0.0, 0.0, 1.0, 1.0);
	XMVECTOR XZAlignedRight = XMVector3Normalize(XMVectorSet(XMVectorGetX(RightAxis), XMVectorGetY(RightAxis), 0.0, 1.0f));

	XMMATRIX RotationMatrix = XMMatrixRotationAxis(GlobalUp, yaw) * XMMatrixRotationAxis(XZAlignedRight, pitch);
	ViewDir = XMVector3Normalize(XMVector3Transform(ViewDir, RotationMatrix));
	RightAxis = XMVector3Normalize(XMVector3Cross(GlobalUp, ViewDir));
	UpAxis = XMVector3Normalize(XMVector3Cross(ViewDir, RightAxis));
	LookAt = Position + ViewDir;

	const float cameraMoveSpeed = cameraSettings.m_movementSpeed;
	float leftStickY = controllerState.m_LeftStickY;
	bool leftStickYActive = abs(leftStickY) > ControllerDeadzone;
	if (keyboardInput['w'] || keyboardInput['W'] || leftStickYActive)
	{
		float multiplier = leftStickYActive ? leftStickY : 1;
		Position += dt * cameraMoveSpeed * ViewDir * multiplier;
		LookAt += dt * cameraMoveSpeed * ViewDir * multiplier;
		bCameraMoved = true;
	}
	if (keyboardInput['s'] || keyboardInput['S'])
	{
		Position -= dt * cameraMoveSpeed * ViewDir;
		LookAt -= dt * cameraMoveSpeed * ViewDir;
		bCameraMoved = true;
	}

	if (keyboardInput['a'] || keyboardInput['A'])
	{
		Position -= dt * cameraMoveSpeed * RightAxis;
		LookAt -= dt * cameraMoveSpeed * RightAxis;
		bCameraMoved = true;
	}

	float leftStickX = controllerState.m_LeftStickX;
	bool leftStickXActive = abs(leftStickX) > ControllerDeadzone;
	if (keyboardInput['D'] || keyboardInput['d'] || leftStickXActive)
	{
		float multiplier = leftStickXActive ? leftStickX : 1;
		Position += dt * cameraMoveSpeed * RightAxis * multiplier;
		LookAt += dt * cameraMoveSpeed * RightAxis * multiplier;
		bCameraMoved = true;
	}

	float rightTrigger = controllerState.m_RightTrigger;
	bool rightTriggerActive = rightTrigger > ControllerDeadzone;
	if (keyboardInput['Q'] || keyboardInput['q'] || rightTriggerActive)
	{
		float multiplier = rightTriggerActive ? rightTrigger : 1;
		Position += dt * cameraMoveSpeed * UpAxis * multiplier;
		LookAt += dt * cameraMoveSpeed * UpAxis * multiplier;
		bCameraMoved = true;
	}



	float leftTrigger = controllerState.m_LeftTrigger;
	bool leftTriggerActive = leftTrigger > ControllerDeadzone;
	if (keyboardInput['E'] || keyboardInput['e'] || leftTriggerActive)
	{
		float multiplier = leftTriggerActive ? leftTrigger : 1;
		Position -= dt * cameraMoveSpeed * UpAxis * multiplier;
		LookAt -= dt * cameraMoveSpeed * UpAxis * multiplier;
		bCameraMoved = true;
	}

	if (bCameraMoved)
	{
		m_camera.Position = { XMVectorGetX(Position),  XMVectorGetY(Position), XMVectorGetZ(Position) };
		m_camera.LookAt = { XMVectorGetX(LookAt),  XMVectorGetY(LookAt), XMVectorGetZ(LookAt) };
		m_camera.Right = { XMVectorGetX(RightAxis),  XMVectorGetY(RightAxis), XMVectorGetZ(RightAxis) };
		m_camera.Up = { XMVectorGetX(UpAxis),  XMVectorGetY(UpAxis), XMVectorGetZ(UpAxis) };
		InvalidateHistory();
	}
}

ComPtr<ID3D12Resource> TracerBoy::CreateUAV(
	const std::wstring &resourceName,
	const D3D12_RESOURCE_DESC &uavDesc, 
	D3D12_CPU_DESCRIPTOR_HANDLE *pUavHandle,
	D3D12_RESOURCE_STATES defaultState)
{
	ComPtr<ID3D12Resource> pResource;
	const D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
		&defaultHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&uavDesc,
		defaultState,
		nullptr,
		IID_GRAPHICS_PPV_ARGS(pResource.ReleaseAndGetAddressOf())));


	if (pUavHandle)
	{
		m_pDevice->CreateUnorderedAccessView(pResource.Get(), nullptr, nullptr, *pUavHandle);
	}
	pResource->SetName(resourceName.c_str());
	return pResource;
}

ComPtr<ID3D12Resource> TracerBoy::CreateSRV(
	const std::wstring &resourceName,
	const D3D12_RESOURCE_DESC &resourceDesc, 
	const D3D12_SHADER_RESOURCE_VIEW_DESC &srvDesc,
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle, 
	D3D12_RESOURCE_STATES defaultState)
{
	ComPtr<ID3D12Resource> pResource;
	const D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
		&defaultHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		defaultState,
		nullptr,
		IID_GRAPHICS_PPV_ARGS(pResource.ReleaseAndGetAddressOf())));

	m_pDevice->CreateShaderResourceView(pResource.Get(), &srvDesc, srvHandle);
	pResource->SetName(resourceName.c_str());
	return pResource;
}

ComPtr<ID3D12Resource> TracerBoy::CreateUAVandSRV(
	const std::wstring &resourceName,
	const D3D12_RESOURCE_DESC& uavDesc, 
	D3D12_CPU_DESCRIPTOR_HANDLE uavHandle, 
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle, 
	D3D12_RESOURCE_STATES defaultState)
{
	ComPtr<ID3D12Resource> pResource = CreateUAV(resourceName, uavDesc, &uavHandle, defaultState);
	m_pDevice->CreateShaderResourceView(pResource.Get(), nullptr, srvHandle);
	return pResource;
}
	
void TracerBoy::InvalidateHistory(bool bForceRealTimeInvalidate)
{
	if (m_CachedOutputSettings.m_renderMode != RenderMode::RealTime || bForceRealTimeInvalidate)
	{
		m_bInvalidateHistory = true;
	}
}


UINT TracerBoy::GetPathTracerOutputIndex()
{
	switch (m_CachedOutputSettings.m_renderMode)
	{
	case RenderMode::Unbiased:
		return 0;
	case RenderMode::RealTime:
		return m_ActiveFrameIndex;
	}
}

UINT TracerBoy::GetPreviousFramePathTracerOutputIndex()
{
	return GetPathTracerOutputIndex() == 0 ? ARRAYSIZE(m_pPathTracerOutput) - 1 : GetPathTracerOutputIndex() - 1;

}

ID3D12Resource *TracerBoy::GetPathTracerOutput()
{
	return m_pPathTracerOutput[GetPathTracerOutputIndex()].Get();
}

UINT TracerBoy::GetPathTracerOutputUAV()
{
	return ViewDescriptorHeapSlots::PathTracerOutputUAV0 + GetPathTracerOutputIndex();
}
UINT TracerBoy::GetPathTracerOutputSRV()
{
	return ViewDescriptorHeapSlots::PathTracerOutputSRV0 + GetPathTracerOutputIndex();
}

UINT TracerBoy::GetPreviousFramePathTracerOutputSRV()
{
	return ViewDescriptorHeapSlots::PathTracerOutputSRV0 + GetPreviousFramePathTracerOutputIndex();
}

UINT TracerBoy::GetWorldPositionSRV()
{
	return ViewDescriptorHeapSlots::AOVWorldPosition0SRV + GetPathTracerOutputIndex();
}

UINT TracerBoy::GetPreviousFrameWorldPositionSRV()
{
	return ViewDescriptorHeapSlots::AOVWorldPosition0SRV + GetPreviousFramePathTracerOutputIndex();
}

void TracerBoy::ResizeBuffersIfNeeded(ID3D12Resource *pBackBuffer)
{
	float downscaleFactor = 1.0;

	D3D12_RESOURCE_DESC upscaledBackBufferDesc = pBackBuffer->GetDesc();
	D3D12_RESOURCE_DESC backBufferDesc = upscaledBackBufferDesc;
	backBufferDesc.Width *= downscaleFactor;
	backBufferDesc.Height *= downscaleFactor;
	VERIFY(backBufferDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);

	bool bResizeNeeded = true;
	if (m_pPostProcessOutput)
	{
		D3D12_RESOURCE_DESC outputDesc = m_pPostProcessOutput->GetDesc();
		bResizeNeeded = outputDesc.Width != backBufferDesc.Width ||
			outputDesc.Height != backBufferDesc.Height ||
			outputDesc.Format != backBufferDesc.Format;
	}

	const D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	if (bResizeNeeded)
	{
		UpdateConfigConstants((UINT)backBufferDesc.Width, (UINT)backBufferDesc.Height);

		D3D12_RESOURCE_DESC pathTracerOutput = CD3DX12_RESOURCE_DESC::Tex2D(
			RayTracingOutputFormat,
			backBufferDesc.Width,
			backBufferDesc.Height,
			1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

		{
			for (UINT i = 0; i < ARRAYSIZE(m_pPathTracerOutput); i++)
			{
				std::wstring outputName = std::wstring(L"PathTracerOutput") + std::to_wstring(i);
				m_pPathTracerOutput[i] = CreateUAVandSRV(
					outputName,
					pathTracerOutput,
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::PathTracerOutputUAV0 + i),
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::PathTracerOutputSRV0 + i),
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			}


			m_pJitteredAccumulatedPathTracerOutput = CreateUAVandSRV(
				L"JitteredPathTracerOutput",
				pathTracerOutput,
				GetCPUDescriptorHandle(ViewDescriptorHeapSlots::JitteredPathTracerOutputUAV),
				GetCPUDescriptorHandle(ViewDescriptorHeapSlots::JitteredPathTracerOutputSRV),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			
			m_pComposittedOutput = CreateUAVandSRV(
				L"ComposittedOutput",
				pathTracerOutput,
				GetCPUDescriptorHandle(ViewDescriptorHeapSlots::ComposittedOutputUAV),
				GetCPUDescriptorHandle(ViewDescriptorHeapSlots::ComposittedOutputSRV),
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		}


		D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_D24_UNORM_S8_UINT,
			backBufferDesc.Width,
			backBufferDesc.Height,
			1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
		
		VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
			&defaultHeapDesc,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			nullptr,
			IID_GRAPHICS_PPV_ARGS(m_pDepthStencil.ReleaseAndGetAddressOf())));
		D3D12_DEPTH_STENCIL_VIEW_DESC depthDesc = {};
		depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthDesc.Flags = D3D12_DSV_FLAG_NONE;
		depthDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		m_pDevice->CreateDepthStencilView(m_pDepthStencil.Get(), &depthDesc, m_pDepthDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		for(UINT i = 0; i < ARRAYSIZE(m_pFinalTemporalOutput); i++)
		{
			PassResource& passResource = m_pFinalTemporalOutput[i];
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&pathTracerOutput,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_GRAPHICS_PPV_ARGS(passResource.m_pResource.ReleaseAndGetAddressOf())));

			m_pDevice->CreateShaderResourceView(passResource.m_pResource.Get(), nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::TemporalOutputBaseSRV + i));
			m_pDevice->CreateUnorderedAccessView(passResource.m_pResource.Get(), nullptr, nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::TemporalOutputBaseUAV + i));

			passResource.m_srvHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::TemporalOutputBaseSRV + i);
			passResource.m_uavHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::TemporalOutputBaseUAV + i);
		}

		for(UINT i = 0; i < ARRAYSIZE(m_pIndirectLightingTemporalOutput); i++)
		{
			PassResource& passResource = m_pIndirectLightingTemporalOutput[i];
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&pathTracerOutput,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_GRAPHICS_PPV_ARGS(passResource.m_pResource.ReleaseAndGetAddressOf())));

			m_pDevice->CreateShaderResourceView(passResource.m_pResource.Get(), nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::IndirectLightingTemporalOutputBaseSRV + i));
			m_pDevice->CreateUnorderedAccessView(passResource.m_pResource.Get(), nullptr, nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::IndirectLightingTemporalOutputBaseUAV + i));

			passResource.m_srvHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::IndirectLightingTemporalOutputBaseSRV + i);
			passResource.m_uavHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::IndirectLightingTemporalOutputBaseUAV + i);
		}

		for(UINT i = 0; i < ARRAYSIZE(m_pMomentBuffer); i++)
		{
			D3D12_RESOURCE_DESC momentBufferDesc = pathTracerOutput;
			momentBufferDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

			PassResource& passResource = m_pMomentBuffer[i];
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&momentBufferDesc,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_GRAPHICS_PPV_ARGS(passResource.m_pResource.ReleaseAndGetAddressOf())));

			m_pDevice->CreateShaderResourceView(passResource.m_pResource.Get(), nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::MomentTextureBaseSRV + i));
			m_pDevice->CreateUnorderedAccessView(passResource.m_pResource.Get(), nullptr, nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::MomentTextureBaseUAV + i));

			passResource.m_srvHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::MomentTextureBaseSRV + i);
			passResource.m_uavHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::MomentTextureBaseUAV + i);
		}


		for(UINT i = 0; i < ARRAYSIZE(m_pDenoiserBuffers); i++)
		{
			PassResource& passResource = m_pDenoiserBuffers[i];
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&pathTracerOutput,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_GRAPHICS_PPV_ARGS(passResource.m_pResource.ReleaseAndGetAddressOf())));

			m_pDevice->CreateShaderResourceView(passResource.m_pResource.Get(), nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::DenoiserOuputBaseSRV + i));
			m_pDevice->CreateUnorderedAccessView(passResource.m_pResource.Get(), nullptr, nullptr, GetCPUDescriptorHandle(ViewDescriptorHeapSlots::DenoiserOutputBaseUAV + i));

			passResource.m_srvHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::DenoiserOuputBaseSRV + i);
			passResource.m_uavHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::DenoiserOutputBaseUAV + i);
		}


		{
			D3D12_RESOURCE_DESC postProcessOutput = CD3DX12_RESOURCE_DESC::Tex2D(
				backBufferDesc.Format,
				backBufferDesc.Width,
				backBufferDesc.Height,
				1, 1, 1, 0,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);


			m_pPostProcessOutput = CreateUAVandSRV(
				L"Post Process Output",
				postProcessOutput,
				GetCPUDescriptorHandle(ViewDescriptorHeapSlots::PostProcessOutputUAV),
				GetCPUDescriptorHandle(ViewDescriptorHeapSlots::PostProcessOutputSRV));

			{
				D3D12_RESOURCE_DESC upscaleDesc = postProcessOutput;
				upscaleDesc.Width = upscaledBackBufferDesc.Width;
				upscaleDesc.Height = upscaledBackBufferDesc.Height;

				auto cpuHandle = GetCPUDescriptorHandle(ViewDescriptorHeapSlots::UpscaledBufferUAV);
				m_pUpscaleOutput.m_pResource = CreateUAV(
					L"UpscaleOutput",
					upscaleDesc,
					&cpuHandle,
					D3D12_RESOURCE_STATE_GENERIC_READ);
				m_pUpscaleOutput.m_uavHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::UpscaledBufferUAV);

				m_pUpscaleItermediateOutput.m_pResource = CreateUAVandSRV(
					L"UpscaleIntermediateOutput",
					upscaleDesc,
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::UpscaledIntermediateBufferUAV),
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::UpscaledIntermediateBufferSRV),
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				m_pUpscaleItermediateOutput.m_uavHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::UpscaledIntermediateBufferUAV);
				m_pUpscaleItermediateOutput.m_srvHandle = GetGPUDescriptorHandle(ViewDescriptorHeapSlots::UpscaledIntermediateBufferSRV);
			}

			{
				D3D12_RESOURCE_DESC aovDesc = postProcessOutput;
				aovDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				m_pAOVCustomOutput = CreateUAVandSRV(
					L"AOVCustomOutput",
					aovDesc,
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::AOVCustomOutputUAV),
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::AOVCustomOutputSRV));
			}

			for (UINT i = 0; i < ARRAYSIZE(m_pAOVWorldPosition); i++)
			{
				D3D12_RESOURCE_DESC worldPositionDesc = postProcessOutput;
				worldPositionDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				m_pAOVWorldPosition[i] = CreateUAVandSRV(
					L"AOVWorldPosition",
					worldPositionDesc,
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::AOVWorldPosition0UAV + i),
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::AOVWorldPosition0SRV + i));
			}

			{
				D3D12_RESOURCE_DESC normalDesc = postProcessOutput;
				normalDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				m_pAOVNormals = CreateUAVandSRV(
					L"AOVNormals",
					normalDesc,
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::AOVNormalsUAV), 
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::AOVNormalsSRV));
			}

			{
				D3D12_RESOURCE_DESC emissiveDesc = postProcessOutput;
				m_pAOVEmissive = CreateUAVandSRV(
					L"AOVEmissive",
					emissiveDesc,
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::AOVEmissiveUAV),
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::AOVEmissiveSRV));
			}

			{
				D3D12_RESOURCE_DESC varianceDesc = postProcessOutput;
				varianceDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				m_pLuminanceVariance = CreateUAVandSRV(
					L"LuminanceVariance",
					varianceDesc,
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::LuminanceVarianceUAV),
					GetCPUDescriptorHandle(ViewDescriptorHeapSlots::LuminanceVarianceSRV), 
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			}
		}
	}
}