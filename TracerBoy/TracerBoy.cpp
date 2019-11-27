#include "pch.h"

#include "FullscreenPlaneVS.h"
#include "RayTracingPS.h"
#include "PostProcessCS.h"

#include "PBRTParser.h"

TracerBoy::TracerBoy(ID3D12CommandQueue *pQueue, const std::string &sceneFileName) : 
	m_pCommandQueue(pQueue), 
	m_SignalValue(1), 
	m_ActivePathTraceOutputIndex(0), 
	m_FramesRendered(0),
	m_mouseX(0),
	m_mouseY(0)
{
	m_camera.Position = Vector3(0.0, 2.0, 3.5);
	m_camera.LookAt = Vector3(0.0, 1.0, 0.0);
	m_camera.Up = Vector3(0.0, 1.0, 0.0);
	m_camera.Right = Vector3(1.0, 0.0, 0.0);
	m_camera.LensHeight = 2.0;
	m_camera.FocalDistance = 7.0;

	VERIFY_HRESULT(m_pCommandQueue->GetDevice(IID_PPV_ARGS(&m_pDevice)));

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options;
	VERIFY_HRESULT(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)));
	VERIFY_HRESULT(m_pDevice->CreateFence(m_SignalValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)));
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc = {};
		rtvDescriptorHeapDesc.NumDescriptors = RenderTargets::NumRTVs;
		rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		VERIFY_HRESULT(m_pDevice->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&m_pRTVDescriptorHeap)));
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC viewDescriptorHeapDesc = {};
		viewDescriptorHeapDesc.NumDescriptors = ViewDescriptorHeapSlots::NumTotalViews;
		viewDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		viewDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		VERIFY_HRESULT(m_pDevice->CreateDescriptorHeap(&viewDescriptorHeapDesc, IID_PPV_ARGS(&m_pViewDescriptorHeap)));
	}

	{
		CD3DX12_ROOT_PARAMETER1 Parameters[RayTracingRootSignatureParameters::NumRayTracingParameters];
		Parameters[RayTracingRootSignatureParameters::PerFrameConstants].InitAsConstants(6, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
		Parameters[RayTracingRootSignatureParameters::ConfigConstants].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_PIXEL);

		CD3DX12_DESCRIPTOR_RANGE1 LastFrameSRVDescriptor;
		LastFrameSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		Parameters[RayTracingRootSignatureParameters::LastFrameSRV].InitAsDescriptorTable(1, &LastFrameSRVDescriptor);
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

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT());
		psoDesc.DepthStencilState.DepthEnable = false;
		psoDesc.NumRenderTargets = 1;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT());
		psoDesc.RasterizerState.DepthClipEnable = false;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.RTVFormats[0] = RayTracingOutputFormat;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.pRootSignature = m_pRayTracingRootSignature;
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(g_FullScreenPlaneVS, ARRAYSIZE(g_FullScreenPlaneVS));
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(g_RayTracingPS, ARRAYSIZE(g_RayTracingPS));

		VERIFY_HRESULT(m_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pRayTracingPSO)));
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
		PBRTParser::PBRTParser SceneParser;
		SceneParser::Scene Scene;
		SceneParser.Parse(sceneFileName, Scene);

		std::vector<CComPtr<ID3D12Resource>> stagingResources;
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
		for (auto &mesh : Scene.m_Meshes)
		{
			CComPtr<ID3D12Resource> pVertexBuffer;
			UINT vertexSize = sizeof(float) * 3;
			UINT vertexBufferSize = static_cast<UINT>(mesh.m_VertexBuffer.size() * vertexSize);
			{
				AllocateUploadBuffer(vertexBufferSize, pVertexBuffer);
				BYTE *pVertexBufferData;
				VERIFY_HRESULT(pVertexBuffer->Map(0, nullptr, (void**)&pVertexBufferData));
				for (UINT v = 0; v < mesh.m_VertexBuffer.size(); v++)
				{
					const SceneParser::Vertex &vertex = mesh.m_VertexBuffer[v];
					memcpy(pVertexBufferData + vertexSize * v, &vertex.Position, vertexSize);
				}
			}
			

			UINT indexSize = sizeof(mesh.m_IndexBuffer[0]);
			assert(indexSize == 4 || indexSize == 2);
			UINT indexBufferSize = static_cast<UINT>(mesh.m_IndexBuffer.size() * indexSize);
			CComPtr<ID3D12Resource> pIndexBuffer;
			{
				AllocateUploadBuffer(indexBufferSize, pIndexBuffer);
				BYTE *pIndexBufferData;
				VERIFY_HRESULT(pIndexBuffer->Map(0, nullptr, (void**)&pIndexBufferData));
				for (UINT i = 0; i < mesh.m_IndexBuffer.size(); i++)
				{
					auto index = mesh.m_IndexBuffer[i];
					memcpy(pIndexBufferData + indexSize, &index, indexSize);
				}
			}

			D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
			geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			geometryDesc.Triangles.IndexBuffer = pIndexBuffer->GetGPUVirtualAddress();
			geometryDesc.Triangles.IndexCount = static_cast<UINT>(pIndexBuffer->GetDesc().Width) / (sizeof(UINT32));
			geometryDesc.Triangles.IndexFormat = indexSize == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
			geometryDesc.Triangles.Transform3x4 = 0;
			geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			geometryDesc.Triangles.VertexCount = static_cast<UINT>(pVertexBuffer->GetDesc().Width) / (sizeof(float) * 3);
			geometryDesc.Triangles.VertexBuffer.StartAddress = pVertexBuffer->GetGPUVirtualAddress();
			geometryDesc.Triangles.VertexBuffer.StrideInBytes = (sizeof(float) * 3);
			geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
			geometryDescs.push_back(geometryDesc);

			stagingResources.push_back(pVertexBuffer);
			stagingResources.push_back(pIndexBuffer);

		}
		CommandListAllocatorPair commandListAllocatorPair;
AcquireCommandListAllocatorPair(commandListAllocatorPair);

CComPtr<ID3D12GraphicsCommandList4> pCommandList;
commandListAllocatorPair.first->QueryInterface(&pCommandList);

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


void TracerBoy::Render(ID3D12Resource *pBackBuffer)
{
	ResizeBuffersIfNeeded(pBackBuffer);
	VERIFY(m_pAccumulatedPathTracerOutput && m_pPostProcessOutput);

	CommandListAllocatorPair commandListAllocatorPair;
	AcquireCommandListAllocatorPair(commandListAllocatorPair);

	ID3D12GraphicsCommandList &commandList = *commandListAllocatorPair.first;
	ID3D12DescriptorHeap *pDescriptorHeaps[] = { m_pViewDescriptorHeap };
	commandList.SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	{
		D3D12_RESOURCE_BARRIER preDrawBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_pAccumulatedPathTracerOutput[m_ActivePathTraceOutputIndex], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		};
		commandList.ResourceBarrier(ARRAYSIZE(preDrawBarriers), preDrawBarriers);

		commandList.SetGraphicsRootSignature(m_pRayTracingRootSignature);
		commandList.SetPipelineState(m_pRayTracingPSO);
		commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(m_pAccumulatedPathTracerOutput[m_ActivePathTraceOutputIndex]);
		commandList.RSSetViewports(1, &viewport);

		D3D12_RECT outputRect = CD3DX12_RECT(0, 0, static_cast<LONG>(viewport.Width), static_cast<LONG>(viewport.Height));
		commandList.RSSetScissorRects(1, &outputRect);

		SYSTEMTIME time;
		GetSystemTime(&time);
		float shaderConstants[] = { m_camera.Position.x, m_camera.Position.y, m_camera.Position.z, static_cast<float>(time.wMilliseconds) / 1000.0f, static_cast<float>(m_mouseX), static_cast<float>(m_mouseY)};
		commandList.SetGraphicsRoot32BitConstants(RayTracingRootSignatureParameters::PerFrameConstants, ARRAYSIZE(shaderConstants), shaderConstants, 0);
		commandList.SetGraphicsRootConstantBufferView(RayTracingRootSignatureParameters::ConfigConstants, m_pConfigConstants->GetGPUVirtualAddress());

		UINT LastFrameBufferSRVIndex = m_ActivePathTraceOutputIndex == 0 ? ARRAYSIZE(m_pAccumulatedPathTracerOutput) - 1 : m_ActivePathTraceOutputIndex - 1;
		commandList.SetGraphicsRootDescriptorTable(RayTracingRootSignatureParameters::LastFrameSRV, GetGPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::PathTracerOutputSRVBaseSlot + LastFrameBufferSRVIndex));

		D3D12_CPU_DESCRIPTOR_HANDLE pRTVs[] = { GetCPUDescriptorHandle(m_pRTVDescriptorHeap, m_ActivePathTraceOutputIndex) };
		commandList.OMSetRenderTargets(ARRAYSIZE(pRTVs), pRTVs, true, nullptr);
		commandList.DrawInstanced(3, 1, 0, 0);

		D3D12_RESOURCE_BARRIER postDrawBarriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(m_pAccumulatedPathTracerOutput[m_ActivePathTraceOutputIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		};
		commandList.ResourceBarrier(ARRAYSIZE(postDrawBarriers), postDrawBarriers);

	}

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
}

void TracerBoy::UpdateConfigConstants(UINT backBufferWidth, UINT backBufferHeight)
{
	float padding = 0.0f;
	float configConstants[] = {
		backBufferWidth,
		backBufferHeight,
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

	void *pData;
	m_pConfigConstants->Map(0, nullptr, &pData);
	memcpy(pData, configConstants, sizeof(configConstants));
	m_pConfigConstants->Unmap(0, nullptr);
}

void TracerBoy::Update(int mouseX, int mouseY, bool keyboardInput[CHAR_MAX], float dt)
{
	m_mouseX = mouseX;
	m_mouseY = mouseY;

	const float cameraMoveSpeed = 0.01;
	if (keyboardInput['w'] || keyboardInput['W'])
	{
		m_camera.Position.z += dt * cameraMoveSpeed;
	}
	if (keyboardInput['s'] || keyboardInput['S'])
	{
		m_camera.Position.z += dt * cameraMoveSpeed;
	}
	if (keyboardInput['a'] || keyboardInput['A'])
	{
		m_camera.Position.x += dt * cameraMoveSpeed;
	}
	if (keyboardInput['D'] || keyboardInput['D'])
	{
		m_camera.Position.x -= dt * cameraMoveSpeed;
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
		UpdateConfigConstants(backBufferDesc.Width, backBufferDesc.Height);


		D3D12_RESOURCE_DESC pathTracerOutput = CD3DX12_RESOURCE_DESC::Tex2D(
			RayTracingOutputFormat,
			backBufferDesc.Width,
			backBufferDesc.Height,
			1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

		for (UINT i = 0; i < ARRAYSIZE(m_pAccumulatedPathTracerOutput); i++)
		{
			const float Black[4] = {};
			const D3D12_CLEAR_VALUE ClearValue = CD3DX12_CLEAR_VALUE(pathTracerOutput.Format, Black);
			auto &pResource = m_pAccumulatedPathTracerOutput[i];
			VERIFY_HRESULT(m_pDevice->CreateCommittedResource(
				&defaultHeapDesc, 
				D3D12_HEAP_FLAG_NONE,
				&pathTracerOutput, 
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
				&ClearValue, 
				IID_PPV_ARGS(&pResource)));

			auto rtvDescriptorHeapBase = m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			auto rtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			m_pDevice->CreateRenderTargetView(pResource, nullptr, CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvDescriptorHeapBase, i, rtvDescriptorSize));
			m_pDevice->CreateShaderResourceView(pResource, nullptr, GetCPUDescriptorHandle(m_pViewDescriptorHeap, ViewDescriptorHeapSlots::PathTracerOutputSRVBaseSlot + i));
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
