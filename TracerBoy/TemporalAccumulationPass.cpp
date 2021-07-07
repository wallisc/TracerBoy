#include "pch.h"

#include "TemporalAccumulationSharedShaderStructs.h"
#include "TemporalAccumulationCS.h"

TemporalAccumulationPass::TemporalAccumulationPass(ID3D12Device& device) 
{
	CD3DX12_ROOT_PARAMETER1 Parameters[TemporalAccumulationRootSignatureParameters::NumRootSignatureParameters] = {};
	Parameters[TemporalAccumulationRootSignatureParameters::ConstantsParam].InitAsConstants(sizeof(TemporalAccumulationConstants) / sizeof(UINT32), 0);

	CD3DX12_DESCRIPTOR_RANGE1 TemporalHistorySRVDescriptor;
	TemporalHistorySRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[TemporalAccumulationRootSignatureParameters::TemporalHistory].InitAsDescriptorTable(1, &TemporalHistorySRVDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 CurrentFrameDescriptor;
	CurrentFrameDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[TemporalAccumulationRootSignatureParameters::CurrentFrameTexture].InitAsDescriptorTable(1, &CurrentFrameDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 AOVPositionDescriptor;
	AOVPositionDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[TemporalAccumulationRootSignatureParameters::WorldPositionTexture].InitAsDescriptorTable(1, &AOVPositionDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 MomentHistoryDescriptor;
	MomentHistoryDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[TemporalAccumulationRootSignatureParameters::MomentHistory].InitAsDescriptorTable(1, &MomentHistoryDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
	OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[TemporalAccumulationRootSignatureParameters::OutputUAV].InitAsDescriptorTable(1, &OutputUAVDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 OutputMomentUAVDescriptor;
	OutputMomentUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[TemporalAccumulationRootSignatureParameters::OutputMomentUAV].InitAsDescriptorTable(1, &OutputMomentUAVDescriptor);

	D3D12_STATIC_SAMPLER_DESC StaticSamplers[] =
	{
		CD3DX12_STATIC_SAMPLER_DESC(
			0u,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP)
	};

	D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
	rootSignatureDesc.pParameters = Parameters;
	rootSignatureDesc.NumParameters = ARRAYSIZE(Parameters);
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	rootSignatureDesc.pStaticSamplers = StaticSamplers;
	rootSignatureDesc.NumStaticSamplers = ARRAYSIZE(StaticSamplers);
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);


	ComPtr<ID3DBlob> pRootSignatureBlob;
	VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
	VERIFY_HRESULT(device.CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.ReleaseAndGetAddressOf())));

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pRootSignature.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pTemporalAccumulationCS, sizeof(g_pTemporalAccumulationCS));
	VERIFY_HRESULT(device.CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(m_pPSO.ReleaseAndGetAddressOf())));
}

D3D12_GPU_DESCRIPTOR_HANDLE TemporalAccumulationPass::Run(ID3D12GraphicsCommandList& commandList,
	PassResource OutputBuffer,
	D3D12_GPU_DESCRIPTOR_HANDLE TemporalHistory,
	D3D12_GPU_DESCRIPTOR_HANDLE CurrentFrame,
	D3D12_GPU_DESCRIPTOR_HANDLE AOVWorldPosition,
	Camera &CurrentFrameCamera,
	Camera &PreviousFrameCamera,
	float HistoryWeight,
	bool bIgnoreHistory,
	UINT width,
	UINT height,
	MomentResources *pMomentResources)
{
	const bool bMomentInfoRequested = pMomentResources != nullptr;

	commandList.SetPipelineState(m_pPSO.Get());
	commandList.SetComputeRootSignature(m_pRootSignature.Get());

	ScopedResourceBarrier outputBarrier(commandList, OutputBuffer.m_pResource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	ScopedResourceBarrier momentBarrier(commandList, pMomentResources ? &pMomentResources->MomentBuffer : nullptr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	TemporalAccumulationConstants constants = {};
	constants.OutputMomentInformation = bMomentInfoRequested;
	constants.Resolution.x = width;
	constants.Resolution.y = height;
	constants.IgnoreHistory = bIgnoreHistory;
	constants.HistoryWeight = HistoryWeight;
	constants.CameraLensHeight = CurrentFrameCamera.LensHeight;
	constants.CameraPosition = { CurrentFrameCamera.Position.x, CurrentFrameCamera.Position.y, CurrentFrameCamera.Position.z };
	constants.CameraLookAt = { CurrentFrameCamera.LookAt.x, CurrentFrameCamera.LookAt.y, CurrentFrameCamera.LookAt.z };
	constants.CameraRight = { CurrentFrameCamera.Right.x, CurrentFrameCamera.Right.y, CurrentFrameCamera.Right.z };
	constants.CameraUp = { CurrentFrameCamera.Up.x, CurrentFrameCamera.Up.y, CurrentFrameCamera.Up.z };
	constants.CameraFocalDistance = CurrentFrameCamera.FocalDistance;
	constants.PrevFrameCameraPosition = { PreviousFrameCamera.Position.x, PreviousFrameCamera.Position.y, PreviousFrameCamera.Position.z };
	constants.PrevFrameCameraRight = { PreviousFrameCamera.Right.x, PreviousFrameCamera.Right.y, PreviousFrameCamera.Right.z };
	constants.PrevFrameCameraUp = { PreviousFrameCamera.Up.x, PreviousFrameCamera.Up.y, PreviousFrameCamera.Up.z };
	constants.PrevFrameCameraLookAt = { PreviousFrameCamera.LookAt.x, PreviousFrameCamera.LookAt.y, PreviousFrameCamera.LookAt.z };

	UINT DispatchWidth = (width - 1) / TEMPORAL_ACCUMULATION_THREAD_GROUP_WIDTH + 1;
	UINT DispatchHeight= (height - 1) / TEMPORAL_ACCUMULATION_THREAD_GROUP_HEIGHT + 1;

	commandList.SetComputeRoot32BitConstants(TemporalAccumulationRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);
	commandList.SetComputeRootDescriptorTable(TemporalAccumulationRootSignatureParameters::TemporalHistory, TemporalHistory);
	commandList.SetComputeRootDescriptorTable(TemporalAccumulationRootSignatureParameters::CurrentFrameTexture, CurrentFrame);
	commandList.SetComputeRootDescriptorTable(TemporalAccumulationRootSignatureParameters::WorldPositionTexture, AOVWorldPosition);
	commandList.SetComputeRootDescriptorTable(TemporalAccumulationRootSignatureParameters::OutputUAV, OutputBuffer.m_uavHandle);
	if (bMomentInfoRequested)
	{
		commandList.SetComputeRootDescriptorTable(TemporalAccumulationRootSignatureParameters::OutputMomentUAV, pMomentResources->MomentBufferUAV);
		commandList.SetComputeRootDescriptorTable(TemporalAccumulationRootSignatureParameters::MomentHistory, pMomentResources->MomentHistory);
	}
	commandList.Dispatch(DispatchWidth, DispatchHeight, 1);

	return OutputBuffer.m_srvHandle;
}