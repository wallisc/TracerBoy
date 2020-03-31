#include "pch.h"

#include "DenoiserSharedShaderStructs.h"
#include "DenoiserCS.h"

DenoiserPass::DenoiserPass(ID3D12Device& device) 
{
	CD3DX12_ROOT_PARAMETER1 Parameters[DenoiserRootSignatureParameters::NumRootSignatureParameters] = {};
	Parameters[DenoiserRootSignatureParameters::ConstantsParam].InitAsConstants(sizeof(DenoiserConstants) / sizeof(UINT32), 0);

	CD3DX12_DESCRIPTOR_RANGE1 InputSRVDescriptor;
	InputSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	Parameters[DenoiserRootSignatureParameters::InputSRV].InitAsDescriptorTable(1, &InputSRVDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
	OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
	Parameters[DenoiserRootSignatureParameters::OutputUAV].InitAsDescriptorTable(1, &OutputUAVDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 AOVNormalDescriptor;
	AOVNormalDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	Parameters[DenoiserRootSignatureParameters::AOVNormal].InitAsDescriptorTable(1, &AOVNormalDescriptor);

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

void DenoiserPass::Run(ID3D12GraphicsCommandList& commandList,
	D3D12_GPU_DESCRIPTOR_HANDLE outputUAV,
	D3D12_GPU_DESCRIPTOR_HANDLE inputSRV,
	D3D12_GPU_DESCRIPTOR_HANDLE normalsSRV,
	UINT width,
	UINT height) 
{
	DenoiserConstants constants;
	constants.Resolution.x = width;
	constants.Resolution.y = height;

	commandList.SetComputeRootSignature(m_pRootSignature.Get());
	commandList.SetComputeRoot32BitConstants(DenoiserRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);
	commandList.SetComputeRootDescriptorTable(DenoiserRootSignatureParameters::InputSRV, inputSRV);
	commandList.SetComputeRootDescriptorTable(DenoiserRootSignatureParameters::OutputUAV, outputUAV);
	commandList.SetComputeRootDescriptorTable(DenoiserRootSignatureParameters::AOVNormal, normalsSRV);
	commandList.SetPipelineState(m_pPSO.Get());
	commandList.Dispatch(width / DENOISER_THREAD_GROUP_WIDTH, height / DENOISER_THREAD_GROUP_HEIGHT, 1);
}