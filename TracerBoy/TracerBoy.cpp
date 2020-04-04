#include "pch.h"

#include "PostProcessCS.h"
#include "ClearAOVCS.h"
#include "RayGen.h"
#include "ClosestHit.h"
#include "Miss.h"


struct HitGroupShaderRecord
{
	BYTE ShaderIdentifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
	UINT GeometryIndex;
	UINT MaterialIndex;
	D3D12_GPU_VIRTUAL_ADDRESS IndexBuffer;
	D3D12_GPU_VIRTUAL_ADDRESS VertexBuffer;
	BYTE Padding2[8];
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

UINT TextureAllocator::CreateTexture(pbrt::Texture::SP& pPbrtTexture)
{
	TextureData texture;
	pbrt::ImageTexture::SP pImageTexture = std::dynamic_pointer_cast<pbrt::ImageTexture>(pPbrtTexture);
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

		m_uploadResources.push_back(pUpload);
		m_tracerboy.m_pTextures.push_back(pTexture);
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

Material CreateMaterial(pbrt::Material::SP& pPbrtMaterial, pbrt::vec3f emissive, MaterialTracker &materialTracker, TextureAllocator &textureAlloator)
{
	Material material = {};
	material.IOR = 1.5f;
	material.albedoIndex = UINT_MAX;
	material.normalMapIndex = UINT_MAX;
	material.emissiveIndex = UINT_MAX;
	material.emissive = ConvertFloat3(emissive);
	material.Flags = ChannelAverage(emissive) > 0.0 ? LIGHT_MATERIAL_FLAG : DEFAULT_MATERIAL_FLAG;

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
	
	if (pDisneyMaterial)
	{
		material.albedo = ConvertFloat3(pDisneyMaterial->color);
		material.roughness = pDisneyMaterial->roughness;
		material.IOR = pDisneyMaterial->eta;
		
		// Not supporting blend between normal and metallic surfaces
		if (pDisneyMaterial->metallic > 0.5)
		{
			material.Flags |= METALLIC_MATERIAL_FLAG;
		}
	}
	else if (pUberMaterial)
	{
		if (pUberMaterial->map_kd)
		{
			material.albedoIndex = textureAlloator.CreateTexture(pUberMaterial->map_kd);
		}
		if (pUberMaterial->map_normal)
		{
			material.normalMapIndex = textureAlloator.CreateTexture(pUberMaterial->map_normal);
		}
		if (pUberMaterial->map_emissive)
		{
			material.emissiveIndex = textureAlloator.CreateTexture(pUberMaterial->map_emissive);
		}
		material.albedo = ConvertFloat3(pUberMaterial->kd);

		VERIFY(!pUberMaterial->map_uRoughness); // Not supporting textures
		VERIFY(pUberMaterial->uRoughness == pUberMaterial->vRoughness); // Not supporting multi dimension rougness
		VERIFY(pUberMaterial->uRoughness == 0.0f); // Not supporting multi dimension rougness
		material.roughness = pUberMaterial->roughness;

		if (ChannelAverage(pUberMaterial->opacity) < 1.0)
		{
			material.Flags |= SUBSURFACE_SCATTER_MATERIAL_FLAG;
			material.IOR = pUberMaterial->index;
			material.absorption = ChannelAverage(pUberMaterial->kt); // absorption != transmission but need oh well
		}
	}
	else if (pMixMaterial)
	{
		UINT mat0Index = materialTracker.AddMaterial(pMixMaterial->material0.get(), CreateMaterial(pMixMaterial->material0, emissive, materialTracker, textureAlloator));
		UINT mat1Index = materialTracker.AddMaterial(pMixMaterial->material1.get(), CreateMaterial(pMixMaterial->material1, emissive, materialTracker, textureAlloator));
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
		VERIFY(!pPlasticMaterial->map_ks);
		VERIFY(!pPlasticMaterial->map_roughness);
		//VERIFY(!pPlasticMaterial->map_bump);

		material.Flags |= DEFAULT_MATERIAL_FLAG;
	}
	else
	{
		VERIFY(false);
	}

	return material;
}

TracerBoy::TracerBoy(ID3D12CommandQueue *pQueue) :
	m_pCommandQueue(pQueue),
	m_ActiveFrameIndex(0),
	m_FramesRendered(0),
	m_mouseX(0),
	m_mouseY(0),
	m_bInvalidateHistory(false),
	m_flipTextureUVs(false),
	CurrentDescriptorSlot(NumReservedViewSlots)
{
	m_pCommandQueue->GetDevice(IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf()));

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options;
	VERIFY_HRESULT(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)));

	{
		D3D12_DESCRIPTOR_HEAP_DESC viewDescriptorHeapDesc = {};
		viewDescriptorHeapDesc.NumDescriptors = ViewDescriptorHeapSlots::NumTotalViews;
		viewDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		viewDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		VERIFY_HRESULT(m_pDevice->CreateDescriptorHeap(&viewDescriptorHeapDesc, IID_PPV_ARGS(m_pViewDescriptorHeap.ReleaseAndGetAddressOf())));
	}

	InitializeLocalRootSignature();

	{
		CD3DX12_ROOT_PARAMETER1 Parameters[RayTracingRootSignatureParameters::NumRayTracingParameters];
		Parameters[RayTracingRootSignatureParameters::PerFrameConstantsParam].InitAsConstants(sizeof(PerFrameConstants) / sizeof(UINT32), 0);
		Parameters[RayTracingRootSignatureParameters::ConfigConstantsParam].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);

		CD3DX12_DESCRIPTOR_RANGE1 LastFrameSRVDescriptor;
		LastFrameSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		Parameters[RayTracingRootSignatureParameters::LastFrameSRV].InitAsDescriptorTable(1, &LastFrameSRVDescriptor);

		CD3DX12_DESCRIPTOR_RANGE1 EnvironmentMapSRVDescriptor;
		EnvironmentMapSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
		Parameters[RayTracingRootSignatureParameters::EnvironmentMapSRV].InitAsDescriptorTable(1, &EnvironmentMapSRVDescriptor);

		CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
		OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		Parameters[RayTracingRootSignatureParameters::OutputUAV].InitAsDescriptorTable(1, &OutputUAVDescriptor);

		CD3DX12_DESCRIPTOR_RANGE1 AOVDescriptor;
		AOVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, ViewDescriptorHeapSlots::AOVLastUAVSlot - ViewDescriptorHeapSlots::AOVBaseUAVSlot + 1, 1);
		Parameters[RayTracingRootSignatureParameters::AOVDescriptorTable].InitAsDescriptorTable(1, &AOVDescriptor);


		Parameters[RayTracingRootSignatureParameters::AccelerationStructureRootSRV].InitAsShaderResourceView(1);
		Parameters[RayTracingRootSignatureParameters::RandSeedRootSRV].InitAsShaderResourceView(5);
		Parameters[RayTracingRootSignatureParameters::MaterialBufferSRV].InitAsShaderResourceView(6);
		Parameters[RayTracingRootSignatureParameters::TextureDataSRV].InitAsShaderResourceView(7);

		CD3DX12_DESCRIPTOR_RANGE1 ImageTextureTableDescriptor;
		ImageTextureTableDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		Parameters[RayTracingRootSignatureParameters::ImageTextureTable].InitAsDescriptorTable(1, &ImageTextureTableDescriptor);

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
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP)
		};

		D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
		rootSignatureDesc.pParameters = Parameters;
		rootSignatureDesc.NumParameters = ARRAYSIZE(Parameters);
		rootSignatureDesc.pStaticSamplers = StaticSamplers;
		rootSignatureDesc.NumStaticSamplers = ARRAYSIZE(StaticSamplers);
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

		ComPtr<ID3DBlob> pRootSignatureBlob;
		VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
		VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(m_pRayTracingRootSignature.ReleaseAndGetAddressOf())));
	}

	{
		auto RaygenExportName = L"RayGen";
		CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

		auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void*)g_pRayGen, ARRAYSIZE(g_pRayGen));
		lib->SetDXILLibrary(&libdxil);

		auto closestHitLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		D3D12_SHADER_BYTECODE closestHitLibDxil = CD3DX12_SHADER_BYTECODE((void*)g_pClosestHit, ARRAYSIZE(g_pClosestHit));
		closestHitLib->SetDXILLibrary(&closestHitLibDxil);

		auto missLib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		D3D12_SHADER_BYTECODE missLibDxil = CD3DX12_SHADER_BYTECODE((void*)g_pMiss, ARRAYSIZE(g_pMiss));
		missLib->SetDXILLibrary(&missLibDxil);

		auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
		hitGroup->SetClosestHitShaderImport(L"ClosestHit");
		hitGroup->SetHitGroupExport(L"HitGroup");

		raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>()->SetRootSignature(m_pRayTracingRootSignature.Get());
		raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>()->SetRootSignature(m_pLocalRootSignature.Get());

		raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>()->Config(sizeof(RayPayload), 8);
		raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>()->Config(1);

		VERIFY_HRESULT(m_pDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(m_pRayTracingStateObject.ReleaseAndGetAddressOf())));
	}

	ComPtr<ID3D12StateObjectProperties> pStateObjectProperties;
	m_pRayTracingStateObject.As(&pStateObjectProperties);

	{
		const void *pRayGenShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(L"RayGen");
		AllocateBufferWithData(pRayGenShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, m_pRayGenShaderTable);
	}

	{
		const void* pMissShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(L"Miss");
		AllocateBufferWithData(pMissShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, m_pMissShaderTable);
	}

	{
		CD3DX12_ROOT_PARAMETER1 Parameters[PostProcessRootSignatureParameters::NumParameters];
		CD3DX12_DESCRIPTOR_RANGE1 InputTextureDescriptor;
		InputTextureDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_DESCRIPTOR_RANGE1 outputTextureDescriptor;
		outputTextureDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		Parameters[PostProcessRootSignatureParameters::InputTexture].InitAsDescriptorTable(1, &InputTextureDescriptor);
		Parameters[PostProcessRootSignatureParameters::OutputTexture].InitAsDescriptorTable(1, &outputTextureDescriptor);
		Parameters[PostProcessRootSignatureParameters::Constants].InitAsConstants(4, 0);

		D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = PostProcessRootSignatureParameters::NumParameters;
		rootSignatureDesc.pParameters = Parameters;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

		ComPtr<ID3DBlob> pRootSignatureBlob;
		VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
		VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(m_pPostProcessRootSignature.ReleaseAndGetAddressOf())));

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_pPostProcessRootSignature.Get();
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_PostProcessCS, ARRAYSIZE(g_PostProcessCS));
		VERIFY_HRESULT(m_pDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(m_pPostProcessPSO.ReleaseAndGetAddressOf())));
	}

	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_pRayTracingRootSignature.Get();
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pClearAOVCS, ARRAYSIZE(g_pClearAOVCS));
		VERIFY_HRESULT(m_pDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(m_pClearAOVs.ReleaseAndGetAddressOf())));
	}

	m_pDenoiserPass = std::unique_ptr<DenoiserPass>(new DenoiserPass(*m_pDevice.Get()));
}

void TracerBoy::LoadScene(ID3D12GraphicsCommandList& commandList, const std::string& sceneFileName, std::vector<ComPtr<ID3D12Resource>>& resourcesToDelete)
{
	std::size_t lastDeliminator = sceneFileName.find_last_of("/\\");
	m_sceneFileDirectory = sceneFileName.substr(0, lastDeliminator + 1);

	std::string sceneFileExtension = sceneFileName.substr(sceneFileName.find_last_of(".") + 1, sceneFileName.size());
	
	{
#if USE_ASSIMP
		AssimpImporter::ScratchData assimpScratchData;
#endif
		std::shared_ptr<pbrt::Scene> pScene;
		if (sceneFileExtension.compare("pbrt") == 0)
		{
			pScene = pbrt::importPBRT(sceneFileName);

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

		assert(pScene->cameras.size() > 0);
		auto& pCamera = pScene->cameras[0];

		pbrt::vec3f CameraPosition = pbrt::vec3f(0);
		pbrt::vec3f CameraView = pbrt::vec3f(0.0, 0.0, 1.0);
		CameraPosition = pCamera->frame * CameraPosition;
		CameraView = pbrt::math::normalize(pbrt::math::xfmVector(pCamera->frame, CameraView));
		pbrt::vec3f CameraRight = pbrt::math::cross(pbrt::vec3f(0, 1, 0), CameraView);
		pbrt::vec3f CameraUp = pbrt::math::cross(CameraView, CameraRight);

		auto pfnConvertVector3 = [](const pbrt::vec3f& v) -> Vector3
		{
			return Vector3(v.x, v.y, v.z);
		};
		m_camera.Position = pfnConvertVector3(CameraPosition);
		m_camera.LookAt = pfnConvertVector3(CameraPosition + CameraView);
		m_camera.Right = pfnConvertVector3(CameraRight);
		m_camera.Up = pfnConvertVector3(CameraUp);
		m_camera.LensHeight = 2.0;
		m_camera.FocalDistance = 7.0;

		std::vector< HitGroupShaderRecord> hitGroupShaderTable;
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;

		ComPtr<ID3D12GraphicsCommandList4> pCommandList;
		VERIFY_HRESULT(commandList.QueryInterface(IID_PPV_ARGS(&pCommandList)));

		TextureAllocator textureAllocator(*this, *pCommandList.Get());
		MaterialTracker materialTracker;

		ComPtr<ID3D12StateObjectProperties> pStateObjectProperties;
		m_pRayTracingStateObject.As(&pStateObjectProperties);
		const void* pHitGroupShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(L"HitGroup");
		UINT geometryCount = 0;
		UINT triangleCount = 0;
		for (UINT geometryIndex = 0; geometryIndex < pScene->world->shapes.size(); geometryIndex++)
		{
			auto& pGeometry = pScene->world->shapes[geometryIndex];
			pbrt::TriangleMesh::SP pTriangleMesh = std::dynamic_pointer_cast<pbrt::TriangleMesh>(pGeometry);
			if (!pTriangleMesh)
			{
				// Only supporting triangle meshes
				continue;
			}

			pbrt::vec3f emissive(0.0f);
			if (pTriangleMesh->areaLight)
			{
				emissive = GetAreaLightColor(pTriangleMesh->areaLight);
			}

			UINT materialIndex = 0;
			if (materialTracker.Exists(pTriangleMesh->material.get()))
			{
				materialIndex = materialTracker.GetMaterial(pTriangleMesh->material.get());
			}
			else
			{
				materialIndex = materialTracker.AddMaterial(pTriangleMesh->material.get(), CreateMaterial(pTriangleMesh->material, emissive, materialTracker, textureAllocator));
			}

			ComPtr<ID3D12Resource> pVertexBuffer;
			UINT vertexSize = sizeof(Vertex);
			UINT vertexBufferSize = static_cast<UINT>(pTriangleMesh->vertex.size() * vertexSize);
			AllocateUploadBuffer(vertexBufferSize, pVertexBuffer);
			bool bNormalsProvided = pTriangleMesh->normal.size();
			Vertex* pVertexBufferData;
			{
				VERIFY_HRESULT(pVertexBuffer->Map(0, nullptr, (void**)&pVertexBufferData));
				for (UINT v = 0; v < pTriangleMesh->vertex.size(); v++)
				{
					auto& parserVertex = pTriangleMesh->vertex[v];
					pbrt::vec3f parserNormal(0, 1, 0); // TODO: This likely needs to be a flat normal
					pbrt::vec3f parserTangent(0, 0, 1);
					if (bNormalsProvided)
					{
						parserNormal = pTriangleMesh->normal[v];
					}
					if (v < pTriangleMesh->tangents.size())
					{
						parserTangent = pTriangleMesh->tangents[v];
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
			}


			UINT indexSize = sizeof(UINT32);
			UINT indexBufferSize = static_cast<UINT>(pTriangleMesh->index.size() * 3 * indexSize);
			ComPtr<ID3D12Resource> pIndexBuffer;
			{
				AllocateUploadBuffer(indexBufferSize, pIndexBuffer);
				UINT32* pIndexBufferData;
				VERIFY_HRESULT(pIndexBuffer->Map(0, nullptr, (void**)&pIndexBufferData));
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
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometryDescs.push_back(geometryDesc);

			HitGroupShaderRecord shaderRecord = {};
			shaderRecord.GeometryIndex = geometryCount++;
			shaderRecord.MaterialIndex = materialIndex;
			shaderRecord.IndexBuffer = pIndexBuffer->GetGPUVirtualAddress();
			shaderRecord.VertexBuffer = pVertexBuffer->GetGPUVirtualAddress();

			memcpy(shaderRecord.ShaderIdentifier, pHitGroupShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

			hitGroupShaderTable.push_back(shaderRecord);

			m_pBuffers.push_back(pIndexBuffer);
			m_pBuffers.push_back(pVertexBuffer);
		}

		AllocateBufferWithData(hitGroupShaderTable.data(), hitGroupShaderTable.size() * sizeof(HitGroupShaderRecord), m_pHitGroupShaderTable);
		AllocateBufferWithData(materialTracker.MaterialList.data(), materialTracker.MaterialList.size() * sizeof(Material), m_pMaterialList);
		if (textureAllocator.GetTextureData().size() > 0)
		{
			AllocateBufferWithData(textureAllocator.GetTextureData().data(), textureAllocator.GetTextureData().size() * sizeof(TextureData), m_pTextureDataList);
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
			}
		}
		resourcesToDelete.push_back(pEnvironmentMapScratchBuffer);

		const D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		{
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildBottomLevelDesc = {};
			buildBottomLevelDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			buildBottomLevelDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
			buildBottomLevelDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
			buildBottomLevelDesc.Inputs.NumDescs = static_cast<UINT>(geometryDescs.size());
			buildBottomLevelDesc.Inputs.pGeometryDescs = geometryDescs.data();

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
			m_pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&buildBottomLevelDesc.Inputs, &prebuildInfo);
			D3D12_RESOURCE_DESC bottomLevelASDesc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&bottomLevelASDesc,
				D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
				nullptr,
				IID_PPV_ARGS(m_pBottomLevelAS.ReleaseAndGetAddressOf())));

			D3D12_RESOURCE_DESC scratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			ComPtr<ID3D12Resource> pScratchBuffer;
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&scratchBufferDesc,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr,
				IID_PPV_ARGS(pScratchBuffer.ReleaseAndGetAddressOf())));
			resourcesToDelete.push_back(pScratchBuffer);

			buildBottomLevelDesc.ScratchAccelerationStructureData = pScratchBuffer->GetGPUVirtualAddress();
			buildBottomLevelDesc.DestAccelerationStructureData = m_pBottomLevelAS->GetGPUVirtualAddress();

			pCommandList->BuildRaytracingAccelerationStructure(&buildBottomLevelDesc, 0, nullptr);
		}

		D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_pBottomLevelAS.Get());
		pCommandList->ResourceBarrier(1, &uavBarrier);

		{
			D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
			instanceDesc.AccelerationStructure = m_pBottomLevelAS->GetGPUVirtualAddress();
			instanceDesc.Transform[0][0] = 1.0;
			instanceDesc.Transform[1][1] = 1.0;
			instanceDesc.Transform[2][2] = 1.0;
			instanceDesc.InstanceMask = 1;
			ComPtr<ID3D12Resource> pInstanceDescBuffer;

			AllocateUploadBuffer(sizeof(instanceDesc), pInstanceDescBuffer);
			resourcesToDelete.push_back(pInstanceDescBuffer);
			void* pData;
			pInstanceDescBuffer->Map(0, nullptr, &pData);
			memcpy(pData, &instanceDesc, sizeof(instanceDesc));

			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildTopLevelDesc = {};
			buildTopLevelDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			buildTopLevelDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			buildTopLevelDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
			buildTopLevelDesc.Inputs.NumDescs = 1;
			buildTopLevelDesc.Inputs.InstanceDescs = pInstanceDescBuffer->GetGPUVirtualAddress();

			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo;
			m_pDevice->GetRaytracingAccelerationStructurePrebuildInfo(&buildTopLevelDesc.Inputs, &prebuildInfo);
			D3D12_RESOURCE_DESC topLevelASDesc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&topLevelASDesc,
				D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
				nullptr,
				IID_PPV_ARGS(m_pTopLevelAS.ReleaseAndGetAddressOf())));

			D3D12_RESOURCE_DESC scratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			ComPtr<ID3D12Resource> pScratchBuffer;
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&scratchBufferDesc,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr,
				IID_PPV_ARGS(pScratchBuffer.ReleaseAndGetAddressOf())));
			resourcesToDelete.push_back(pScratchBuffer);

			buildTopLevelDesc.ScratchAccelerationStructureData = pScratchBuffer->GetGPUVirtualAddress();
			buildTopLevelDesc.DestAccelerationStructureData = m_pTopLevelAS->GetGPUVirtualAddress();

			pCommandList->BuildRaytracingAccelerationStructure(&buildTopLevelDesc, 0, nullptr);
		}

		textureAllocator.ExtractScratchResources(resourcesToDelete);
	}

}


void TracerBoy::InitializeTexture(
	const std::wstring &textureName,
	ID3D12GraphicsCommandList& commandList,
	ComPtr<ID3D12Resource>& pResource,
	UINT SRVSlot,
	ComPtr<ID3D12Resource>& pUploadResource)
{
	std::wstring directory(m_sceneFileDirectory.begin(), m_sceneFileDirectory.end());
	std::wstring fullTextureName = directory + textureName;

	DirectX::TexMetadata texMetaData = {};
	DirectX::ScratchImage scratchImage = {};
	std::wstring fileExt = textureName.substr(textureName.size() - 4, 4);
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

	m_pDevice->CreateShaderResourceView(pResource.Get(), nullptr, GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), SRVSlot));

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
	CD3DX12_ROOT_PARAMETER1 Parameters[LocalRayTracingRootSignatureParameters::NumLocalRayTracingParameters];
	Parameters[LocalRayTracingRootSignatureParameters::GeometryIndexRootConstant].InitAsConstants(2, 2);
	Parameters[LocalRayTracingRootSignatureParameters::IndexBufferSRV].InitAsShaderResourceView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
	Parameters[LocalRayTracingRootSignatureParameters::VertexBufferSRV].InitAsShaderResourceView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);

	D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
	rootSignatureDesc.pParameters = Parameters;
	rootSignatureDesc.NumParameters = ARRAYSIZE(Parameters);
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

	ComPtr<ID3DBlob> pRootSignatureBlob;
	VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
	VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(m_pLocalRootSignature.ReleaseAndGetAddressOf())));
}

D3D12_CPU_DESCRIPTOR_HANDLE TracerBoy::GetCPUDescriptorHandle(ID3D12DescriptorHeap *pDescriptorHeap, UINT slot)
{
	auto descriptorHeapBase = pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	auto descriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(pDescriptorHeap->GetDesc().Type);
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapBase, slot, descriptorSize);
}

D3D12_GPU_DESCRIPTOR_HANDLE TracerBoy::GetGPUDescriptorHandle(ID3D12DescriptorHeap *pDescriptorHeap, UINT slot)
{
	auto descriptorHeapBase = pDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	auto descriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(pDescriptorHeap->GetDesc().Type);
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
		IID_PPV_ARGS(pBuffer.ReleaseAndGetAddressOf())));
}

void TracerBoy::AllocateBufferWithData(const void* pData, UINT dataSize, ComPtr<ID3D12Resource>& pBuffer)
{
	AllocateUploadBuffer(dataSize, pBuffer);
	void* pMappedData;
	pBuffer->Map(0, nullptr, &pMappedData);
	memcpy(pMappedData, pData, dataSize);
	pBuffer->Unmap(0, nullptr);
}

ID3D12Resource* TracerBoy::GetOutputResource(OutputType outputType)
{
	switch (outputType)
	{
	case OutputType::Lit:
	default:
		return m_pPostProcessOutput.Get();
	case OutputType::Albedo:
		return m_pAOVAlbedo.Get();
	case OutputType::Normals:
		return m_pAOVNormals.Get();
	}
}


void TracerBoy::Render(ID3D12GraphicsCommandList& commandList, ID3D12Resource *pBackBuffer, const OutputSettings& outputSettings)
{
	ResizeBuffersIfNeeded(pBackBuffer);
	VERIFY(m_pAccumulatedPathTracerOutput && m_pPostProcessOutput);

	UpdateRandSeedBuffer(m_ActiveFrameIndex);

	ID3D12DescriptorHeap* pDescriptorHeaps[] = { m_pViewDescriptorHeap.Get() };
	commandList.SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(m_pAccumulatedPathTracerOutput[m_ActiveFrameIndex].Get());
	if (m_bInvalidateHistory)
	{
		// TODO: make root signature for clearing
		commandList.SetComputeRootSignature(m_pRayTracingRootSignature.Get());
		commandList.SetPipelineState(m_pClearAOVs.Get());
		commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::AOVDescriptorTable, GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVBaseUAVSlot));
		
		commandList.Dispatch(viewport.Width, viewport.Height, 1);
	}

	commandList.SetComputeRootSignature(m_pRayTracingRootSignature.Get());

	ComPtr<ID3D12GraphicsCommandList5> pRaytracingCommandList;
	commandList.QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));
	pRaytracingCommandList->SetPipelineState1(m_pRayTracingStateObject.Get());

	SYSTEMTIME time;
	GetSystemTime(&time);
	PerFrameConstants constants = {};
	constants.CameraPosition = { m_camera.Position.x, m_camera.Position.y, m_camera.Position.z };
	constants.CameraLookAt = { m_camera.LookAt.x, m_camera.LookAt.y, m_camera.LookAt.z };
	constants.CameraRight = { m_camera.Right.x, m_camera.Right.y, m_camera.Right.z };
	constants.CameraUp = { m_camera.Up.x, m_camera.Up.y, m_camera.Up.z };
	constants.Time = static_cast<float>(time.wMilliseconds) / 1000.0f;
	constants.EnableNormalMaps = outputSettings.m_EnableNormalMaps;
	constants.InvalidateHistory = m_bInvalidateHistory;
	
	commandList.SetComputeRoot32BitConstants(RayTracingRootSignatureParameters::PerFrameConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);
	commandList.SetComputeRootConstantBufferView(RayTracingRootSignatureParameters::ConfigConstantsParam, m_pConfigConstants->GetGPUVirtualAddress());
	commandList.SetComputeRootShaderResourceView(RayTracingRootSignatureParameters::RandSeedRootSRV, m_pRandSeedBuffer[m_ActiveFrameIndex]->GetGPUVirtualAddress());
	commandList.SetComputeRootShaderResourceView(RayTracingRootSignatureParameters::MaterialBufferSRV, m_pMaterialList->GetGPUVirtualAddress());
	if (m_pTextureDataList)
	{
		commandList.SetComputeRootShaderResourceView(RayTracingRootSignatureParameters::TextureDataSRV, m_pTextureDataList->GetGPUVirtualAddress());
	}
	
	commandList.SetComputeRootShaderResourceView(RayTracingRootSignatureParameters::AccelerationStructureRootSRV, m_pTopLevelAS->GetGPUVirtualAddress());

	D3D12_RESOURCE_BARRIER preDispatchRaysBarrier[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(m_pAccumulatedPathTracerOutput[m_ActiveFrameIndex].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	};
	commandList.ResourceBarrier(ARRAYSIZE(preDispatchRaysBarrier), preDispatchRaysBarrier);

	UINT LastFrameBufferSRVIndex = m_ActiveFrameIndex == 0 ? ARRAYSIZE(m_pAccumulatedPathTracerOutput) - 1 : m_ActiveFrameIndex - 1;
	commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::LastFrameSRV, GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::PathTracerOutputSRVBaseSlot + LastFrameBufferSRVIndex));
	commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::EnvironmentMapSRV, GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::EnvironmentMapSRVSlot));
	commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::OutputUAV, GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::PathTracerOutputUAVBaseSlot + m_ActiveFrameIndex));
	commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::ImageTextureTable, m_pViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::AOVDescriptorTable, GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVBaseUAVSlot));

	D3D12_RESOURCE_DESC desc = m_pAccumulatedPathTracerOutput[m_ActiveFrameIndex]->GetDesc();

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

	D3D12_RESOURCE_BARRIER postDispatchRaysBarrier[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(m_pAccumulatedPathTracerOutput[m_ActiveFrameIndex].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
	};
	commandList.ResourceBarrier(ARRAYSIZE(postDispatchRaysBarrier), postDispatchRaysBarrier);

	D3D12_GPU_DESCRIPTOR_HANDLE PostProcessInput = GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::PathTracerOutputSRVBaseSlot + m_ActiveFrameIndex);
	if(outputSettings.m_denoiserSettings.m_bEnabled)
	{
		ScopedResourceBarrier normalsBarrier(commandList, *m_pAOVNormals.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		ScopedResourceBarrier intersectPositionsBarrier(commandList, *m_pAOVWorldPosition.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		ScopedResourceBarrier histogramBarrier(commandList, *m_pAOVSDRHistogram.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		PostProcessInput = m_pDenoiserPass->Run(commandList,
			m_pDenoiserBuffers,
			outputSettings.m_denoiserSettings,
			PostProcessInput,
			GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVNormalsSRV),
			GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVWorldPositionSRV),
			GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVSDRHistogramSRV),
			viewport.Width,
			viewport.Height);
	}

	{
		commandList.SetComputeRootSignature(m_pPostProcessRootSignature.Get());
		commandList.SetPipelineState(m_pPostProcessPSO.Get());

		auto outputDesc = m_pPostProcessOutput->GetDesc();
		UINT32 constants[] = { static_cast<UINT32>(outputDesc.Width), static_cast<UINT32>(outputDesc.Height), ++m_FramesRendered, *(UINT*)&outputSettings.m_ExposureMultiplier };
		commandList.SetComputeRoot32BitConstants(PostProcessRootSignatureParameters::Constants, ARRAYSIZE(constants), constants, 0);
		commandList.SetComputeRootDescriptorTable(
			PostProcessRootSignatureParameters::InputTexture,
			PostProcessInput);
		commandList.SetComputeRootDescriptorTable(
			PostProcessRootSignatureParameters::OutputTexture,
			GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::PostProcessOutputUAV));
		commandList.Dispatch(constants[0], constants[1], 1);
	}

	{
		ID3D12Resource* pResourceToPresent = GetOutputResource(outputSettings.m_OutputType);
		D3D12_RESOURCE_BARRIER preCopyBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(pResourceToPresent, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(pBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
		};
		commandList.ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);
		commandList.CopyResource(pBackBuffer, pResourceToPresent);

		D3D12_RESOURCE_BARRIER postCopyBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(pResourceToPresent, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(pBackBuffer,D3D12_RESOURCE_STATE_COPY_DEST,  D3D12_RESOURCE_STATE_RENDER_TARGET)
		};
		commandList.ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);
	}

	m_ActiveFrameIndex = (m_ActiveFrameIndex + 1) % MaxActiveFrames;
	m_bInvalidateHistory = false;
}


void TracerBoy::UpdateConfigConstants(UINT backBufferWidth, UINT backBufferHeight)
{
	ConfigConstants configConstants;
	configConstants.CameraLensHeight = m_camera.LensHeight;
	configConstants.FocalDistance = m_camera.FocalDistance;
	configConstants.FlipTextureUVs = m_flipTextureUVs;

	AllocateBufferWithData(&configConstants, sizeof(configConstants), m_pConfigConstants);
}

void TracerBoy::Update(int mouseX, int mouseY, bool keyboardInput[CHAR_MAX], float dt, const CameraSettings& cameraSettings)
{
	bool bCameraMoved = false;

	float yaw = 0.0;
	float pitch = 0.0;
	const float rotationScaler = 0.5f;
	if (m_pPostProcessOutput)
	{
		auto outputDesc = m_pPostProcessOutput->GetDesc();

		yaw = rotationScaler * 2.0 * 6.28f * ((float)mouseX - (float)m_mouseX) / (float)outputDesc.Width;
		pitch = rotationScaler * 0.5f * 3.14f * ((float)mouseY - (float)m_mouseY) / (float)outputDesc.Height;
	}


	if (m_mouseX != mouseX || m_mouseY != mouseY)
	{
		bCameraMoved = true;
		m_mouseX = mouseX;
		m_mouseY = mouseY;
	}
	
	XMVECTOR RightAxis = XMVectorSet(m_camera.Right.x, m_camera.Right.y, m_camera.Right.z, 1.0);
	XMVECTOR UpAxis = XMVectorSet(m_camera.Up.x, m_camera.Up.y, m_camera.Up.z, 1.0);
	XMVECTOR Position = XMVectorSet(m_camera.Position.x, m_camera.Position.y, m_camera.Position.z, 1.0);
	XMVECTOR LookAt = XMVectorSet(m_camera.LookAt.x, m_camera.LookAt.y, m_camera.LookAt.z, 1.0);
	XMVECTOR ViewDir = LookAt - Position;

	XMVECTOR GlobalUp = XMVectorSet(0.0, 1.0, 0.0, 1.0);

	XMMATRIX RotationMatrix = XMMatrixRotationAxis(GlobalUp, yaw);// *XMMatrixRotationAxis(RightAxis, pitch);
	ViewDir = XMVector3Normalize(XMVector3Transform(ViewDir, RotationMatrix));
	RightAxis = XMVector3Normalize(XMVector3Transform(RightAxis, RotationMatrix));
	UpAxis = XMVector3Normalize(XMVector3Transform(UpAxis, RotationMatrix));
	LookAt = Position + ViewDir;

	const float cameraMoveSpeed = cameraSettings.m_movementSpeed;
	if (keyboardInput['w'] || keyboardInput['W'])
	{
		Position += dt * cameraMoveSpeed * ViewDir;
		LookAt += dt * cameraMoveSpeed * ViewDir;
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
	if (keyboardInput['D'] || keyboardInput['d'])
	{
		Position += dt * cameraMoveSpeed * RightAxis;
		LookAt += dt * cameraMoveSpeed * RightAxis;
		bCameraMoved = true;
	}
	if (keyboardInput['Q'] || keyboardInput['q'])
	{
		Position += dt * cameraMoveSpeed * UpAxis;
		LookAt += dt * cameraMoveSpeed * UpAxis;
		bCameraMoved = true;
	}
	if (keyboardInput['E'] || keyboardInput['e'])
	{
		Position -= dt * cameraMoveSpeed * UpAxis;
		LookAt -= dt * cameraMoveSpeed * UpAxis;
		bCameraMoved = true;
	}

	if (bCameraMoved)
	{
		m_camera.Position = { XMVectorGetX(Position),  XMVectorGetY(Position), XMVectorGetZ(Position) };
		m_camera.LookAt = { XMVectorGetX(LookAt),  XMVectorGetY(LookAt), XMVectorGetZ(LookAt) };
		m_camera.Right = { XMVectorGetX(RightAxis),  XMVectorGetY(RightAxis), XMVectorGetZ(RightAxis) };
		m_camera.Up = { XMVectorGetX(UpAxis),  XMVectorGetY(UpAxis), XMVectorGetZ(UpAxis) };
		m_bInvalidateHistory = true;
	}
}

ComPtr<ID3D12Resource> TracerBoy::CreateUAV(const D3D12_RESOURCE_DESC &uavDesc, D3D12_CPU_DESCRIPTOR_HANDLE uavHandle)
{
	ComPtr<ID3D12Resource> pResource;
	const D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
		&defaultHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&uavDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(pResource.ReleaseAndGetAddressOf())));

	m_pDevice->CreateUnorderedAccessView(pResource.Get(), nullptr, nullptr, uavHandle);
	return pResource;
}
ComPtr<ID3D12Resource> TracerBoy::CreateUAVandSRV(const D3D12_RESOURCE_DESC& uavDesc, D3D12_CPU_DESCRIPTOR_HANDLE uavHandle, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
	ComPtr<ID3D12Resource> pResource = CreateUAV(uavDesc, uavHandle);
	m_pDevice->CreateShaderResourceView(pResource.Get(), nullptr, srvHandle);
	return pResource;
}

void TracerBoy::ResizeBuffersIfNeeded(ID3D12Resource *pBackBuffer)
{
	D3D12_RESOURCE_DESC backBufferDesc = pBackBuffer->GetDesc();
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

		for (UINT i = 0; i < ARRAYSIZE(m_pAccumulatedPathTracerOutput); i++)
		{
			auto &pResource = m_pAccumulatedPathTracerOutput[i];
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&pathTracerOutput,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				nullptr,
				IID_PPV_ARGS(pResource.ReleaseAndGetAddressOf())));

			m_pDevice->CreateShaderResourceView(pResource.Get(), nullptr, GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::PathTracerOutputSRVBaseSlot + i));
			m_pDevice->CreateUnorderedAccessView(pResource.Get(), nullptr, nullptr, GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::PathTracerOutputUAVBaseSlot + i));
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
				IID_PPV_ARGS(passResource.m_pResource.ReleaseAndGetAddressOf())));

			m_pDevice->CreateShaderResourceView(passResource.m_pResource.Get(), nullptr, GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::DenoiserOuputBaseSRV + i));
			m_pDevice->CreateUnorderedAccessView(passResource.m_pResource.Get(), nullptr, nullptr, GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::DenoiserOutputBaseUAV + i));

			passResource.m_srvHandle = GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::DenoiserOuputBaseSRV + i);
			passResource.m_uavHandle = GetGPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::DenoiserOutputBaseUAV + i);
		}


		{
			D3D12_RESOURCE_DESC postProcessOutput = CD3DX12_RESOURCE_DESC::Tex2D(
				backBufferDesc.Format,
				backBufferDesc.Width,
				backBufferDesc.Height,
				1, 1, 1, 0,
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&postProcessOutput,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr,
				IID_PPV_ARGS(m_pPostProcessOutput.ReleaseAndGetAddressOf())));

			{
				D3D12_RESOURCE_DESC aovDesc = postProcessOutput;
				m_pAOVAlbedo = CreateUAVandSRV(
					aovDesc,
					GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVAlbedoUAV),
					GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVAlbedoSRV));
			}

			{
				D3D12_RESOURCE_DESC worldPositionDesc = postProcessOutput;
				worldPositionDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				m_pAOVWorldPosition = CreateUAVandSRV(
					worldPositionDesc,
					GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVWorldPositionUAV),
					GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVWorldPositionSRV));
			}

			{
				D3D12_RESOURCE_DESC normalDesc = postProcessOutput;
				normalDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
				m_pAOVNormals = CreateUAVandSRV(
					normalDesc,
					GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVNormalsUAV), 
					GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVNormalsSRV));
			}

			{
				UINT numElements = backBufferDesc.Width * backBufferDesc.Height;
				D3D12_RESOURCE_DESC aovHistogramDesc = CD3DX12_RESOURCE_DESC::Buffer(
					sizeof(SDRHistogram) * numElements, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
				
				const D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
				VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
					&defaultHeapDesc,
					D3D12_HEAP_FLAG_NONE,
					&aovHistogramDesc,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
					nullptr,
					IID_PPV_ARGS(m_pAOVSDRHistogram.ReleaseAndGetAddressOf())));

				D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
				uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
				uavDesc.Format = DXGI_FORMAT_UNKNOWN;
				uavDesc.Buffer.StructureByteStride = sizeof(SDRHistogram);
				uavDesc.Buffer.FirstElement = 0;
				uavDesc.Buffer.NumElements = numElements;
				m_pDevice->CreateUnorderedAccessView(m_pAOVSDRHistogram.Get(), nullptr, &uavDesc,
					GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVSDRHistogramUAV));
				
				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Format = DXGI_FORMAT_UNKNOWN;
				srvDesc.Buffer.FirstElement = 0;
				srvDesc.Buffer.NumElements = numElements;
				srvDesc.Buffer.StructureByteStride = sizeof(SDRHistogram);
				m_pDevice->CreateShaderResourceView(m_pAOVSDRHistogram.Get(), &srvDesc, 
					GetCPUDescriptorHandle(m_pViewDescriptorHeap.Get(), ViewDescriptorHeapSlots::AOVSDRHistogramSRV));
			}

			auto viewDescriptorHeapBase = m_pViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			auto viewDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = postProcessOutput.Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			m_pDevice->CreateUnorderedAccessView(
				m_pPostProcessOutput.Get(),
				nullptr,
				&uavDesc,
				CD3DX12_CPU_DESCRIPTOR_HANDLE(viewDescriptorHeapBase, ViewDescriptorHeapSlots::PostProcessOutputUAV, viewDescriptorSize));

		}

		for (auto& pRandBuffer : m_pRandSeedBuffer)
		{
			AllocateUploadBuffer(sizeof(float) * backBufferDesc.Width * backBufferDesc.Height, pRandBuffer);
		}
	}
}

void TracerBoy::UpdateRandSeedBuffer(UINT bufferIndex)
{
	UINT numSeeds = m_pRandSeedBuffer[bufferIndex]->GetDesc().Width / sizeof(UINT32);
	float* pData;
	VERIFY_HRESULT(m_pRandSeedBuffer[bufferIndex]->Map(0, nullptr, (void**)&pData));
	for (UINT i = 0; i < numSeeds; i++)
	{
		pData[i] = 10000.0f * (float)rand() / (float)RAND_MAX;
	}
	m_pRandSeedBuffer[bufferIndex]->Unmap(0, nullptr);
}
