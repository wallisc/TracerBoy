#include "pch.h"

#include "PostProcessCS.h"
#include "RayGen.h"
#include "ClosestHit.h"
#include "Miss.h"
#include "SharedShaderStructs.h"

#ifdef WIN32
#undef min
#undef max
#endif

#include "PBRTParser\Scene.h"

#define USE_DXR 1

struct HitGroupShaderRecord
{
	BYTE ShaderIdentifier[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
	UINT GeometryIndex;
	UINT Padding1;
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

TracerBoy::TracerBoy(ID3D12CommandQueue *pQueue, const std::string &sceneFileName) : 
	m_pCommandQueue(pQueue), 
	m_SignalValue(1), 
	m_ActivePathTraceOutputIndex(0), 
	m_FramesRendered(0),
	m_mouseX(0),
	m_mouseY(0),
	m_bInvalidateHistory(false)
{
	VERIFY_HRESULT(m_pCommandQueue->GetDevice(IID_PPV_ARGS(&m_pDevice)));

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options;
	VERIFY_HRESULT(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)));
	VERIFY_HRESULT(m_pDevice->CreateFence(m_SignalValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)));

	{
		D3D12_DESCRIPTOR_HEAP_DESC viewDescriptorHeapDesc = {};
		viewDescriptorHeapDesc.NumDescriptors = ViewDescriptorHeapSlots::NumTotalViews;
		viewDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		viewDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		VERIFY_HRESULT(m_pDevice->CreateDescriptorHeap(&viewDescriptorHeapDesc, IID_PPV_ARGS(&m_pViewDescriptorHeap)));
	}

	InitializeLocalRootSignature();

	{
		CD3DX12_ROOT_PARAMETER1 Parameters[RayTracingRootSignatureParameters::NumRayTracingParameters];
		Parameters[RayTracingRootSignatureParameters::PerFrameConstantsParam].InitAsConstants(sizeof(PerFrameConstants) / sizeof(UINT32), 0);
		Parameters[RayTracingRootSignatureParameters::ConfigConstants].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);

		CD3DX12_DESCRIPTOR_RANGE1 LastFrameSRVDescriptor;
		LastFrameSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		Parameters[RayTracingRootSignatureParameters::LastFrameSRV].InitAsDescriptorTable(1, &LastFrameSRVDescriptor);
		
		CD3DX12_DESCRIPTOR_RANGE1 EnvironmentMapSRVDescriptor;
		EnvironmentMapSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
		Parameters[RayTracingRootSignatureParameters::EnvironmentMapSRV].InitAsDescriptorTable(1, &EnvironmentMapSRVDescriptor);

		CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
		OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
		Parameters[RayTracingRootSignatureParameters::OutputUAV].InitAsDescriptorTable(1, &OutputUAVDescriptor);


		Parameters[RayTracingRootSignatureParameters::AccelerationStructureRootSRV].InitAsShaderResourceView(1);

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
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

		CComPtr<ID3DBlob> pRootSignatureBlob;
		VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
		VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRayTracingRootSignature)));
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

		raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>()->SetRootSignature(m_pRayTracingRootSignature);
		raytracingPipeline.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>()->SetRootSignature(m_pLocalRootSignature);

		raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>()->Config(sizeof(RayPayload), 8);
		raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>()->Config(1);

		VERIFY_HRESULT(m_pDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_pRayTracingStateObject)));
	}

	CComPtr<ID3D12StateObjectProperties> pStateObjectProperties;
	m_pRayTracingStateObject->QueryInterface(&pStateObjectProperties);
	
	{
		void *pRayGenShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(L"RayGen");
		AllocateBufferWithData(pRayGenShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, m_pRayGenShaderTable);
	}

	{
		void* pMissShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(L"Miss");
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
		Parameters[PostProcessRootSignatureParameters::Constants].InitAsConstants(3, 0);

		D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
		rootSignatureDesc.NumParameters = PostProcessRootSignatureParameters::NumParameters;
		rootSignatureDesc.pParameters = Parameters;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

		CComPtr<ID3DBlob> pRootSignatureBlob;
		VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
		VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_pPostProcessRootSignature)));

		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_pPostProcessRootSignature;
		psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_PostProcessCS, ARRAYSIZE(g_PostProcessCS));
		VERIFY_HRESULT(m_pDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_pPostProcessPSO)));
	}

	{
		std::shared_ptr<pbrt::Scene> pScene = pbrt::importPBRT(sceneFileName);

		assert(pScene->cameras.size() > 0);
		auto& pCamera = pScene->cameras[0];

		pbrt::vec3f CameraPosition = pbrt::vec3f(0);
		pbrt::vec3f CameraView = pbrt::vec3f(0.0, 0.0, 1.0);
		CameraPosition = pCamera->frame * CameraPosition;
		CameraView = pCamera->frame * CameraView;
		CameraPosition = -pCamera->focalDistance * CameraView;
		pbrt::vec3f CameraRight = pbrt::math::cross(CameraView, pbrt::vec3f(0, 1, 0));
		pbrt::vec3f CameraUp = pbrt::math::cross(CameraRight, CameraView);

		auto pfnConvertVector3 = [](const pbrt::vec3f& v) -> Vector3
		{
			return Vector3(v.x, v.y, v.z);
		};
		m_camera.Position = pfnConvertVector3(CameraPosition);
		m_camera.LookAt = pfnConvertVector3(CameraView);
		m_camera.Right = pfnConvertVector3(CameraRight);
		m_camera.Up = pfnConvertVector3(CameraUp);
		m_camera.LensHeight = 2.0;
		m_camera.FocalDistance = pCamera->focalDistance;
;		// TODO figure out how to convert camera
#if 0
		auto pfnConvertVector3 =[](const SceneParser::Vector3& v) -> Vector3
		{
			return Vector3(v.x, v.y, v.z);
		};
		m_camera.Position = pfnConvertVector3(Scene.m_Camera.m_Position);
		m_camera.LookAt = pfnConvertVector3(Scene.m_Camera.m_LookAt);
		m_camera.Up = pfnConvertVector3(Scene.m_Camera.m_Up);
		
		// TODO: This is a hobby project, don't judge
		auto pfnConvertVec3 = [](Vector3& v) -> PBRTParser::glm::vec3
		{
			return PBRTParser::glm::vec3(v.x, v.y, v.z);
		};

		PBRTParser::glm::vec3 position = pfnConvertVec3(m_camera.Position);
		PBRTParser::glm::vec3 lookAt = pfnConvertVec3(m_camera.LookAt);
		PBRTParser::glm::vec3 view = PBRTParser::glm::normalize(lookAt - position);
		PBRTParser::glm::vec3 up = pfnConvertVec3(m_camera.Up);
		PBRTParser::glm::vec3 right = PBRTParser::glm::cross(up, view);

		m_camera.Right = Vector3(right.x, right.y, right.z);
		m_camera.LensHeight = 2.0;
		m_camera.FocalDistance = 7.0;
#endif

		std::vector< HitGroupShaderRecord> hitGroupShaderTable;
		std::vector<CComPtr<ID3D12Resource>> stagingResources;
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;

		void* pHitGroupShaderIdentifier = pStateObjectProperties->GetShaderIdentifier(L"HitGroup");
		UINT geometryCount = 0;
		for (UINT geometryIndex = 0; geometryIndex < pScene->world->shapes.size(); geometryIndex++)
		{
			auto &pGeometry = pScene->world->shapes[geometryIndex];
			pbrt::TriangleMesh::SP pTriangleMesh = std::dynamic_pointer_cast<pbrt::TriangleMesh>(pGeometry);
			if (!pTriangleMesh)
			{
				// Only supporting triangle meshes
				continue;
			}

			CComPtr<ID3D12Resource> pVertexBuffer;
			UINT vertexSize = sizeof(Vertex);
			UINT vertexBufferSize = static_cast<UINT>(pTriangleMesh->vertex.size() * vertexSize);
			{
				AllocateUploadBuffer(vertexBufferSize, pVertexBuffer);
				BYTE *pVertexBufferData;
				VERIFY_HRESULT(pVertexBuffer->Map(0, nullptr, (void**)&pVertexBufferData));
				for (UINT v = 0; v < pTriangleMesh->vertex.size(); v++)
				{
					auto& parserVertex = pTriangleMesh->vertex[v];
					auto &parserNormal = pTriangleMesh->normal[v];
					Vertex shaderVertex;
					shaderVertex.Position = { parserVertex.x, parserVertex.y, parserVertex.z };
					shaderVertex.Normal = { parserNormal.x, parserNormal.y, parserNormal.z };
					memcpy(pVertexBufferData + vertexSize * v, &shaderVertex, vertexSize);
				}
			}
			

			UINT indexSize = sizeof(UINT32);
			UINT indexBufferSize = static_cast<UINT>(pTriangleMesh->index.size() * 3 * indexSize);
			CComPtr<ID3D12Resource> pIndexBuffer;
			{
				AllocateUploadBuffer(indexBufferSize, pIndexBuffer);
				UINT32 *pIndexBufferData;
				VERIFY_HRESULT(pIndexBuffer->Map(0, nullptr, (void**)&pIndexBufferData));
				for (UINT i = 0; i < pTriangleMesh->index.size(); i++)
				{
					auto triangleIndices = pTriangleMesh->index[i];
					pIndexBufferData[3 * i    ] = triangleIndices.x;
					pIndexBufferData[3 * i + 1] = triangleIndices.y;
					pIndexBufferData[3 * i + 2] = triangleIndices.z;
				}
			}

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
			shaderRecord.IndexBuffer = pIndexBuffer->GetGPUVirtualAddress();
			shaderRecord.VertexBuffer = pVertexBuffer->GetGPUVirtualAddress();

			memcpy(shaderRecord.ShaderIdentifier, pHitGroupShaderIdentifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

			hitGroupShaderTable.push_back(shaderRecord);

			m_pBuffers.push_back(pIndexBuffer);
			m_pBuffers.push_back(pVertexBuffer);
		}

		AllocateBufferWithData(hitGroupShaderTable.data(), hitGroupShaderTable.size() * sizeof(HitGroupShaderRecord), m_pHitGroupShaderTable);

		CommandListAllocatorPair commandListAllocatorPair;
		AcquireCommandListAllocatorPair(commandListAllocatorPair);

		CComPtr<ID3D12GraphicsCommandList4> pCommandList;
		commandListAllocatorPair.first->QueryInterface(&pCommandList);


		DirectX::TexMetadata texMetaData;
		DirectX::ScratchImage scratchImage;
		VERIFY_HRESULT(DirectX::LoadFromHDRFile(L"..\\Scenes\\Teapot\\textures\\envmap.hdr", &texMetaData, scratchImage));
		VERIFY_HRESULT(DirectX::CreateTextureEx(m_pDevice, texMetaData, D3D12_RESOURCE_FLAG_NONE, false, &m_pEnvironmentMap));
		m_pDevice->CreateShaderResourceView(m_pEnvironmentMap, nullptr, GetCPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::EnvironmentMapSRVSlot));

		std::vector < D3D12_SUBRESOURCE_DATA> subresources;
		VERIFY_HRESULT(PrepareUpload(m_pDevice, scratchImage.GetImages(), scratchImage.GetImageCount(), texMetaData, subresources));

		const UINT64 uploadBufferSize = GetRequiredIntermediateSizeHelper(m_pDevice, m_pEnvironmentMap, 0, 1);

		CComPtr<ID3D12Resource> textureUploadHeap;
		AllocateUploadBuffer(uploadBufferSize, textureUploadHeap);

		UpdateSubresourcesHelper(m_pDevice, pCommandList,
			m_pEnvironmentMap, textureUploadHeap,
			0, 0, static_cast<unsigned int>(subresources.size()),
			subresources.data());

		D3D12_RESOURCE_BARRIER barriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_pEnvironmentMap, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		};
		pCommandList->ResourceBarrier(ARRAYSIZE(barriers), barriers);

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
				IID_PPV_ARGS(&m_pBottomLevelAS)));

			D3D12_RESOURCE_DESC scratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CComPtr<ID3D12Resource> pScratchBuffer;
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&scratchBufferDesc,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr,
				IID_PPV_ARGS(&pScratchBuffer)));
			stagingResources.push_back(pScratchBuffer);
			
			buildBottomLevelDesc.ScratchAccelerationStructureData = pScratchBuffer->GetGPUVirtualAddress();
			buildBottomLevelDesc.DestAccelerationStructureData = m_pBottomLevelAS->GetGPUVirtualAddress();
			
			pCommandList->BuildRaytracingAccelerationStructure(&buildBottomLevelDesc, 0, nullptr);
		}

		D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_pBottomLevelAS);
		pCommandList->ResourceBarrier(1, &uavBarrier);

		{
			D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
			instanceDesc.AccelerationStructure = m_pBottomLevelAS->GetGPUVirtualAddress();
			instanceDesc.Transform[0][0] = 1.0;
			instanceDesc.Transform[1][1] = 1.0;
			instanceDesc.Transform[2][2] = 1.0;
			instanceDesc.InstanceMask = 1;
			CComPtr<ID3D12Resource> pInstanceDescBuffer;

			AllocateUploadBuffer(sizeof(instanceDesc), pInstanceDescBuffer);
			stagingResources.push_back(pInstanceDescBuffer);
			void *pData;
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
				IID_PPV_ARGS(&m_pTopLevelAS)));

			D3D12_RESOURCE_DESC scratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			CComPtr<ID3D12Resource> pScratchBuffer;
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc,
				D3D12_HEAP_FLAG_NONE,
				&scratchBufferDesc,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr,
				IID_PPV_ARGS(&pScratchBuffer)));
			stagingResources.push_back(pScratchBuffer);

			buildTopLevelDesc.ScratchAccelerationStructureData = pScratchBuffer->GetGPUVirtualAddress();
			buildTopLevelDesc.DestAccelerationStructureData = m_pTopLevelAS->GetGPUVirtualAddress();

			pCommandList->BuildRaytracingAccelerationStructure(&buildTopLevelDesc, 0, nullptr);
		}

		VERIFY_HRESULT(commandListAllocatorPair.first->Close());
		ExecuteAndFreeCommandListAllocatorPair(commandListAllocatorPair);
		WaitForGPUIdle();
	}
}

void TracerBoy::InitializeLocalRootSignature()
{
	CD3DX12_ROOT_PARAMETER1 Parameters[LocalRayTracingRootSignatureParameters::NumLocalRayTracingParameters];
	Parameters[LocalRayTracingRootSignatureParameters::GeometryIndexRootConstant].InitAsConstants(1, 2);
	Parameters[LocalRayTracingRootSignatureParameters::IndexBufferSRV].InitAsShaderResourceView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
	Parameters[LocalRayTracingRootSignatureParameters::VertexBufferSRV].InitAsShaderResourceView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);

	D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
	rootSignatureDesc.pParameters = Parameters;
	rootSignatureDesc.NumParameters = ARRAYSIZE(Parameters);
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

	CComPtr<ID3DBlob> pRootSignatureBlob;
	VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
	VERIFY_HRESULT(m_pDevice->CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_pLocalRootSignature)));
}


UINT64 TracerBoy::SignalFence()
{
	UINT64 signalledValue = m_SignalValue;
	m_pCommandQueue->Signal(m_pFence, m_SignalValue++);
	return signalledValue;
}

void TracerBoy::WaitForGPUIdle()
{
	UINT64 signalledValue = SignalFence();

	HANDLE waitEvent = CreateEvent(nullptr, false, false, nullptr);
	m_pFence->SetEventOnCompletion(signalledValue, waitEvent);
	WaitForSingleObject(waitEvent, INFINITE);
	CloseHandle(waitEvent);
}

void TracerBoy::AcquireCommandListAllocatorPair(CommandListAllocatorPair &pair)
{
	if (FreedCommandListAllocatorPairs.size() && m_pFence->GetCompletedValue() >= FreedCommandListAllocatorPairs.back().second)
	{
		pair = FreedCommandListAllocatorPairs.back().first;
		FreedCommandListAllocatorPairs.pop_back();

		VERIFY_HRESULT(pair.second->Reset());
		VERIFY_HRESULT(pair.first->Reset(pair.second, nullptr));
	}
	else
	{
		VERIFY_HRESULT(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pair.second)));
		VERIFY_HRESULT(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pair.second, nullptr, IID_PPV_ARGS(&pair.first)))
	}
}
void TracerBoy::ExecuteAndFreeCommandListAllocatorPair(CommandListAllocatorPair &pair)
{
	ID3D12CommandList *pCommandLists[] = { pair.first };
	m_pCommandQueue->ExecuteCommandLists(ARRAYSIZE(pCommandLists), pCommandLists);

	// Not sure why this is needed but, too many command lists are getting queued up
	WaitForGPUIdle();

	UINT64 signalledValue = SignalFence();
	FreedCommandListAllocatorPairs.push_front(std::pair<CommandListAllocatorPair, UINT64>(pair, signalledValue));
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

void TracerBoy::AllocateUploadBuffer(UINT bufferSize, CComPtr<ID3D12Resource> &pBuffer)
{
	const D3D12_HEAP_PROPERTIES uploadHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
		&uploadHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&uploadBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&pBuffer)));
}

void TracerBoy::AllocateBufferWithData(void* pData, UINT dataSize, CComPtr<ID3D12Resource>& pBuffer)
{
	AllocateUploadBuffer(dataSize, pBuffer);
	void* pMappedData;
	pBuffer->Map(0, nullptr, &pMappedData);
	memcpy(pMappedData, pData, dataSize);
	pBuffer->Unmap(0, nullptr);
}


void TracerBoy::Render(ID3D12Resource *pBackBuffer)
{
	ResizeBuffersIfNeeded(pBackBuffer);
	VERIFY(m_pAccumulatedPathTracerOutput && m_pPostProcessOutput);

	CommandListAllocatorPair commandListAllocatorPair;
	AcquireCommandListAllocatorPair(commandListAllocatorPair);

	ID3D12GraphicsCommandList &commandList = *commandListAllocatorPair.first;
	ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_pViewDescriptorHeap };
	commandList.SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	commandList.SetComputeRootSignature(m_pRayTracingRootSignature);

	CComPtr<ID3D12GraphicsCommandList5> pRaytracingCommandList;
	commandList.QueryInterface(&pRaytracingCommandList);
	pRaytracingCommandList->SetPipelineState1(m_pRayTracingStateObject);

	D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(m_pAccumulatedPathTracerOutput[m_ActivePathTraceOutputIndex]);
	SYSTEMTIME time;
	GetSystemTime(&time);
	PerFrameConstants constants;
	constants.CameraPosition = { m_camera.Position.x, m_camera.Position.y, m_camera.Position.z };
	constants.Time = static_cast<float>(time.wMilliseconds) / 1000.0f;
	constants.InvalidateHistory = m_bInvalidateHistory;
	constants.Mouse = { static_cast<float>(m_mouseX), static_cast<float>(m_mouseY) };
	constants.CameraLookAt = { m_camera.LookAt.x, m_camera.LookAt.y, m_camera.LookAt.z };
	commandList.SetComputeRoot32BitConstants(RayTracingRootSignatureParameters::PerFrameConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);
	commandList.SetComputeRootConstantBufferView(RayTracingRootSignatureParameters::ConfigConstants, m_pConfigConstants->GetGPUVirtualAddress());
		
	commandList.SetComputeRootShaderResourceView(RayTracingRootSignatureParameters::AccelerationStructureRootSRV, m_pTopLevelAS->GetGPUVirtualAddress());
	
	D3D12_RESOURCE_BARRIER preDispatchRaysBarrier[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(m_pAccumulatedPathTracerOutput[m_ActivePathTraceOutputIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	};
	commandList.ResourceBarrier(ARRAYSIZE(preDispatchRaysBarrier), preDispatchRaysBarrier);

	UINT LastFrameBufferSRVIndex = m_ActivePathTraceOutputIndex == 0 ? ARRAYSIZE(m_pAccumulatedPathTracerOutput) - 1 : m_ActivePathTraceOutputIndex - 1;
	commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::LastFrameSRV, GetGPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::PathTracerOutputSRVBaseSlot + LastFrameBufferSRVIndex));
	commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::EnvironmentMapSRV, GetGPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::EnvironmentMapSRVSlot));
	commandList.SetComputeRootDescriptorTable(RayTracingRootSignatureParameters::OutputUAV, GetGPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::PathTracerOutputUAVBaseSlot + m_ActivePathTraceOutputIndex));

	D3D12_RESOURCE_DESC desc = m_pAccumulatedPathTracerOutput[m_ActivePathTraceOutputIndex]->GetDesc();

	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
	dispatchDesc.Width =  static_cast<UINT>(viewport.Width);
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
		CD3DX12_RESOURCE_BARRIER::Transition(m_pAccumulatedPathTracerOutput[m_ActivePathTraceOutputIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
	};
	commandList.ResourceBarrier(ARRAYSIZE(postDispatchRaysBarrier), postDispatchRaysBarrier);

	{
		commandList.SetComputeRootSignature(m_pPostProcessRootSignature);
		commandList.SetPipelineState(m_pPostProcessPSO);

		auto outputDesc = m_pPostProcessOutput->GetDesc();
		UINT32 constants[] = { static_cast<UINT32>(outputDesc.Width), static_cast<UINT32>(outputDesc.Height), ++m_FramesRendered };
		commandList.SetComputeRoot32BitConstants(PostProcessRootSignatureParameters::Constants, ARRAYSIZE(constants), constants, 0);
		commandList.SetComputeRootDescriptorTable(
			PostProcessRootSignatureParameters::InputTexture,
			GetGPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::PathTracerOutputSRVBaseSlot + m_ActivePathTraceOutputIndex));
		commandList.SetComputeRootDescriptorTable(
			PostProcessRootSignatureParameters::OutputTexture,
			GetGPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::PostProcessOutputUAV));
		commandList.Dispatch(constants[0], constants[1], 1);
	}

	m_ActivePathTraceOutputIndex = (m_ActivePathTraceOutputIndex + 1) % ARRAYSIZE(m_pAccumulatedPathTracerOutput);


	{
		D3D12_RESOURCE_BARRIER preCopyBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_pPostProcessOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(pBackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
		};
		commandList.ResourceBarrier(ARRAYSIZE(preCopyBarriers), preCopyBarriers);
		commandList.CopyResource(pBackBuffer, m_pPostProcessOutput);

		D3D12_RESOURCE_BARRIER postCopyBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_pPostProcessOutput, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(pBackBuffer,D3D12_RESOURCE_STATE_COPY_DEST,  D3D12_RESOURCE_STATE_PRESENT)
		};
		commandList.ResourceBarrier(ARRAYSIZE(postCopyBarriers), postCopyBarriers);

	}

	VERIFY_HRESULT(commandListAllocatorPair.first->Close());
	ExecuteAndFreeCommandListAllocatorPair(commandListAllocatorPair);
	m_bInvalidateHistory = false;
}


void TracerBoy::UpdateConfigConstants(UINT backBufferWidth, UINT backBufferHeight)
{
	float padding = 0.0f;
	float configConstants[] = {
		static_cast<float>(backBufferWidth),
		static_cast<float>(backBufferHeight),
		m_camera.FocalDistance,
		m_camera.LensHeight,
		m_camera.LookAt.x, m_camera.LookAt.y, m_camera.LookAt.z, padding, // lookAt 
		m_camera.Right.x, m_camera.Right.y, m_camera.Right.z, padding, // right
		m_camera.Up.x, m_camera.Up.y, m_camera.Up.z, padding // up
	};
	if (!m_pConfigConstants)
	{
		AllocateUploadBuffer(sizeof(configConstants), m_pConfigConstants);
	}

	void* pData;
	m_pConfigConstants->Map(0, nullptr, &pData);
	memcpy(pData, configConstants, sizeof(configConstants));
	m_pConfigConstants->Unmap(0, nullptr);
}

void TracerBoy::Update(int mouseX, int mouseY, bool keyboardInput[CHAR_MAX], float dt)
{
	m_mouseX = mouseX;
	m_mouseY = mouseY;

	bool bCameraMoved  = false;
	const float cameraMoveSpeed = 0.07f;
	Vector3 ViewDir = (m_camera.LookAt - m_camera.Position).Normalize();
	if (keyboardInput['w'] || keyboardInput['W'])
	{
		m_camera.Position += ViewDir * (dt * cameraMoveSpeed);
		m_camera.LookAt += ViewDir * (dt * cameraMoveSpeed);
		bCameraMoved = true;
	}
	if (keyboardInput['s'] || keyboardInput['S'])
	{
		m_camera.Position -= ViewDir * (dt * cameraMoveSpeed);
		m_camera.LookAt -= ViewDir * (dt * cameraMoveSpeed);
		bCameraMoved = true;
	}
	if (keyboardInput['a'] || keyboardInput['A'])
	{
		m_camera.Position -= m_camera.Right * (dt * cameraMoveSpeed);
		m_camera.LookAt -= m_camera.Right * (dt * cameraMoveSpeed);
		bCameraMoved = true;
	}
	if (keyboardInput['D'] || keyboardInput['d'])
	{
		m_camera.Position += m_camera.Right * (dt * cameraMoveSpeed);
		m_camera.LookAt += m_camera.Right * (dt * cameraMoveSpeed);
		bCameraMoved = true;
	}
	if (keyboardInput['Q'] || keyboardInput['q'])
	{
		m_camera.Position += m_camera.Up * (dt * cameraMoveSpeed);
		m_camera.LookAt += m_camera.Up * (dt * cameraMoveSpeed);
		bCameraMoved = true;
	}
	if (keyboardInput['E'] || keyboardInput['e'])
	{
		m_camera.Position -= m_camera.Up * (dt * cameraMoveSpeed);
		m_camera.LookAt -= m_camera.Up * (dt * cameraMoveSpeed);
		bCameraMoved = true;
	}

	if (bCameraMoved)
	{
		m_bInvalidateHistory = true;
	}
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
		WaitForGPUIdle();
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
				IID_PPV_ARGS(&pResource)));

			m_pDevice->CreateShaderResourceView(pResource, nullptr, GetCPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::PathTracerOutputSRVBaseSlot + i));
			m_pDevice->CreateUnorderedAccessView(pResource, nullptr, nullptr, GetCPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::PathTracerOutputUAVBaseSlot + i));
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
				IID_PPV_ARGS(&m_pPostProcessOutput)));

			auto viewDescriptorHeapBase = m_pViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			auto viewDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = postProcessOutput.Format;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			m_pDevice->CreateUnorderedAccessView(
				m_pPostProcessOutput, 
				nullptr, 
				&uavDesc,
				CD3DX12_CPU_DESCRIPTOR_HANDLE(viewDescriptorHeapBase, ViewDescriptorHeapSlots::PostProcessOutputUAV, viewDescriptorSize));

		}


	}
}
