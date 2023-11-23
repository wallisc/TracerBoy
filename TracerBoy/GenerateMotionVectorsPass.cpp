#include "pch.h"

#include "GenerateMotionVectorsSharedShaderStructs.h"
#include "GenerateMotionVectorsCS.h"

GenerateMotionVectorsPass::GenerateMotionVectorsPass(ID3D12Device& device)
{
	CD3DX12_ROOT_PARAMETER1 Parameters[GenerateMotionVectorsRootSignatureParameters::NumRootSignatureParameters] = {};
	Parameters[GenerateMotionVectorsRootSignatureParameters::ConstantsParam].InitAsConstants(sizeof(GenerateMotionVectorsConstants) / sizeof(UINT32), 0);

	CD3DX12_DESCRIPTOR_RANGE1 AOVPositionDescriptor;
	AOVPositionDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[GenerateMotionVectorsRootSignatureParameters::WorldPositionTexture].InitAsDescriptorTable(1, &AOVPositionDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 PrevFrameAOVPositionDescriptor;
	PrevFrameAOVPositionDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[GenerateMotionVectorsRootSignatureParameters::PreviousFrameWorldPositionTexture].InitAsDescriptorTable(1, &PrevFrameAOVPositionDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
	OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[GenerateMotionVectorsRootSignatureParameters::OutputUAV].InitAsDescriptorTable(1, &OutputUAVDescriptor);

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
	VERIFY_HRESULT(device.CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_GRAPHICS_PPV_ARGS(m_pRootSignature.ReleaseAndGetAddressOf())));

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pRootSignature.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pGenerateMotionVectorsCS, sizeof(g_pGenerateMotionVectorsCS));
	VERIFY_HRESULT(device.CreateComputePipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pPSO.ReleaseAndGetAddressOf())));
}

D3D12_GPU_DESCRIPTOR_HANDLE GenerateMotionVectorsPass::Run(ID3D12GraphicsCommandList& commandList,
	PassResource OutputBuffer,
	D3D12_GPU_DESCRIPTOR_HANDLE AOVWorldPosition,
	D3D12_GPU_DESCRIPTOR_HANDLE PreviousFrameWorldPosition,
	Camera& CurrentFrameCamera,
	Camera& PreviousFrameCamera,
	bool bIgnoreHistory,
	UINT width,
	UINT height)
{
	commandList.SetPipelineState(m_pPSO.Get());
	commandList.SetComputeRootSignature(m_pRootSignature.Get());

	ScopedResourceBarrier outputBarrier(commandList, OutputBuffer.m_pResource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	GenerateMotionVectorsConstants constants = {};
	constants.Resolution.x = width;
	constants.Resolution.y = height;
	constants.IgnoreHistory = bIgnoreHistory;
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

	UINT DispatchWidth = (width - 1) / GENERATE_MOTION_VECTORS_THREAD_GROUP_WIDTH + 1;
	UINT DispatchHeight = (height - 1) / GENERATE_MOTION_VECTORS_THREAD_GROUP_HEIGHT + 1;

	commandList.SetComputeRoot32BitConstants(GenerateMotionVectorsRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);
	commandList.SetComputeRootDescriptorTable(GenerateMotionVectorsRootSignatureParameters::WorldPositionTexture, AOVWorldPosition);
	commandList.SetComputeRootDescriptorTable(GenerateMotionVectorsRootSignatureParameters::PreviousFrameWorldPositionTexture, PreviousFrameWorldPosition);
	commandList.SetComputeRootDescriptorTable(GenerateMotionVectorsRootSignatureParameters::OutputUAV, OutputBuffer.m_uavHandle);
	commandList.Dispatch(DispatchWidth, DispatchHeight, 1);

	return OutputBuffer.m_srvHandle;
}