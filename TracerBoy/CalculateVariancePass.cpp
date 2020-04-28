#include "pch.h"

#include "CalculateVarianceSharedShaderStructs.h"
#include "CalculateVarianceCS.h"

CalculateVariancePass::CalculateVariancePass(ID3D12Device& device)
{
	CD3DX12_ROOT_PARAMETER1 Parameters[CalculateVarianceRootSignatureParameters::NumRootSignatureParameters] = {};

	Parameters[CalculateVarianceRootSignatureParameters::RootConstants].InitAsConstants(sizeof(CalculateVarianceConstants) / sizeof(UINT32), 0);

	CD3DX12_DESCRIPTOR_RANGE1 InputSRVDescriptor;
	InputSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[CalculateVarianceRootSignatureParameters::SummedLumaSquaredSRV].InitAsDescriptorTable(1, &InputSRVDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 PathTracedOutputSRVDescriptor;
	PathTracedOutputSRVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[CalculateVarianceRootSignatureParameters::PathTracedOutputSRV].InitAsDescriptorTable(1, &PathTracedOutputSRVDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
	OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[CalculateVarianceRootSignatureParameters::LuminanceVarianceUAV].InitAsDescriptorTable(1, &OutputUAVDescriptor);

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
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pCalculateVarianceCS, sizeof(g_pCalculateVarianceCS));
	VERIFY_HRESULT(device.CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(m_pPSO.ReleaseAndGetAddressOf())));
}

void CalculateVariancePass::Run(ID3D12GraphicsCommandList& commandList,
	D3D12_GPU_DESCRIPTOR_HANDLE summedLumaSquaredSRV,
	D3D12_GPU_DESCRIPTOR_HANDLE pathTracedOutputSRV,
	D3D12_GPU_DESCRIPTOR_HANDLE lumianceVarianceUAV,
	UINT width,
	UINT height)
{
	commandList.SetPipelineState(m_pPSO.Get());
	commandList.SetComputeRootSignature(m_pRootSignature.Get());

	CalculateVarianceConstants constants;
	constants.Resolution = { width, height };
	commandList.SetComputeRoot32BitConstants(CalculateVarianceRootSignatureParameters::RootConstants, sizeof(constants) / sizeof(UINT32), &constants, 0);

	commandList.SetComputeRootDescriptorTable(CalculateVarianceRootSignatureParameters::SummedLumaSquaredSRV, summedLumaSquaredSRV);
	commandList.SetComputeRootDescriptorTable(CalculateVarianceRootSignatureParameters::PathTracedOutputSRV, pathTracedOutputSRV);
	commandList.SetComputeRootDescriptorTable(CalculateVarianceRootSignatureParameters::LuminanceVarianceUAV, lumianceVarianceUAV);

	commandList.Dispatch(width, height, 1);

}