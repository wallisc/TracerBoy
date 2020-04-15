#include "pch.h"

#include "DenoiserSharedShaderStructs.h"
#include "DenoiserCS.h"

DenoiserPass::DenoiserPass(ID3D12Device& device) 
{
	CD3DX12_ROOT_PARAMETER1 Parameters[DenoiserRootSignatureParameters::NumRootSignatureParameters] = {};
	Parameters[DenoiserRootSignatureParameters::ConstantsParam].InitAsConstants(sizeof(DenoiserConstants) / sizeof(UINT32), 0);

	CD3DX12_DESCRIPTOR_RANGE1 InputSRVDescriptor;
	InputSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[DenoiserRootSignatureParameters::InputSRV].InitAsDescriptorTable(1, &InputSRVDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
	OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[DenoiserRootSignatureParameters::OutputUAV].InitAsDescriptorTable(1, &OutputUAVDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 AOVNormalDescriptor;
	AOVNormalDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[DenoiserRootSignatureParameters::AOVNormal].InitAsDescriptorTable(1, &AOVNormalDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 AOVIntersectPositionDescriptor;
	AOVIntersectPositionDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[DenoiserRootSignatureParameters::AOVIntersectPosition].InitAsDescriptorTable(1, &AOVIntersectPositionDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 AOVSDRHistogramDescriptor;
	AOVSDRHistogramDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[DenoiserRootSignatureParameters::AOVSDRHistogram].InitAsDescriptorTable(1, &AOVSDRHistogramDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 UndenoisedInputDescriptor;
	UndenoisedInputDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[DenoiserRootSignatureParameters::UndenoisedInputSRV].InitAsDescriptorTable(1, &UndenoisedInputDescriptor);


	D3D12_ROOT_SIGNATURE_DESC1 rootSignatureDesc = {};
	rootSignatureDesc.pParameters = Parameters;
	rootSignatureDesc.NumParameters = ARRAYSIZE(Parameters);
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionedRSDesc(rootSignatureDesc);

	ComPtr<ID3DBlob> pRootSignatureBlob;
	VERIFY_HRESULT(D3D12SerializeVersionedRootSignature(&versionedRSDesc, &pRootSignatureBlob, nullptr));
	VERIFY_HRESULT(device.CreateRootSignature(0, pRootSignatureBlob->GetBufferPointer(), pRootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(m_pRootSignature.ReleaseAndGetAddressOf())));

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pRootSignature.Get();
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pDenoiserCS, sizeof(g_pDenoiserCS));
	VERIFY_HRESULT(device.CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(m_pPSO.ReleaseAndGetAddressOf())));
}

D3D12_GPU_DESCRIPTOR_HANDLE DenoiserPass::Run(ID3D12GraphicsCommandList& commandList,
	PassResource DenoiserBuffers[2],
	const DenoiserSettings& denoiserSettings,
	D3D12_GPU_DESCRIPTOR_HANDLE inputSRV,
	D3D12_GPU_DESCRIPTOR_HANDLE normalsSRV,
	D3D12_GPU_DESCRIPTOR_HANDLE intersectPositionSRV,
	D3D12_GPU_DESCRIPTOR_HANDLE sdrHistogramSRV,
	UINT width,
	UINT height) 
{
	commandList.SetPipelineState(m_pPSO.Get());
	commandList.SetComputeRootSignature(m_pRootSignature.Get());

	UINT OutputDenoiserBufferIndex = 0;
	UINT InputDenoiserBufferIndex = 1;
	for (UINT i = 0; i < denoiserSettings.m_waveletIterations; i++)
	{
		ScopedResourceBarrier denoiserBarrier(commandList, DenoiserBuffers[InputDenoiserBufferIndex].m_pResource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		DenoiserConstants constants;
		constants.Resolution.x = width;
		constants.Resolution.y = height;
		constants.OffsetMultiplier = pow(2, i);
		constants.NormalWeightingExponential = denoiserSettings.m_normalWeightingExponential;
		constants.IntersectionPositionWeightingMultiplier = denoiserSettings.m_intersectPositionWeightingMultiplier;
		constants.LumaWeightingMultiplier = denoiserSettings.m_luminanceWeightingMultiplier;

		D3D12_GPU_DESCRIPTOR_HANDLE iterationInputSRV = i == 0 ? inputSRV : DenoiserBuffers[InputDenoiserBufferIndex].m_srvHandle;

		commandList.SetComputeRoot32BitConstants(DenoiserRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);
		commandList.SetComputeRootDescriptorTable(DenoiserRootSignatureParameters::InputSRV, iterationInputSRV);
		commandList.SetComputeRootDescriptorTable(DenoiserRootSignatureParameters::OutputUAV, DenoiserBuffers[OutputDenoiserBufferIndex].m_uavHandle);
		commandList.SetComputeRootDescriptorTable(DenoiserRootSignatureParameters::AOVNormal, normalsSRV);
		commandList.SetComputeRootDescriptorTable(DenoiserRootSignatureParameters::AOVIntersectPosition, intersectPositionSRV);
		commandList.SetComputeRootDescriptorTable(DenoiserRootSignatureParameters::AOVSDRHistogram, sdrHistogramSRV);
		commandList.SetComputeRootDescriptorTable(DenoiserRootSignatureParameters::UndenoisedInputSRV, inputSRV);
		commandList.Dispatch(width / DENOISER_THREAD_GROUP_WIDTH, height / DENOISER_THREAD_GROUP_HEIGHT, 1);

		InputDenoiserBufferIndex = OutputDenoiserBufferIndex;
		OutputDenoiserBufferIndex = (OutputDenoiserBufferIndex + 1) % 2;
	}

	return DenoiserBuffers[InputDenoiserBufferIndex].m_srvHandle;
}