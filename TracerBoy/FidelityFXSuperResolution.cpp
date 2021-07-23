#include "pch.h"

#include "FidelityFXSuperResolution.h"
#include "FidelityFXSuperResolutionSharedShaderStructs.h"
#include "FidelityFXSuperResolutionCS.h"
#include "FidelityFXSharpenCS.h"

FidelityFXSuperResolutionPass::FidelityFXSuperResolutionPass(ID3D12Device& device) 
{
	CD3DX12_ROOT_PARAMETER1 Parameters[FidelityFXSuperResolutionRootSignatureParameters::NumRootSignatureParameters] = {};
	Parameters[FidelityFXSuperResolutionRootSignatureParameters::ConstantsParam].InitAsConstants(sizeof(FSRConstants) / sizeof(UINT32), 0);

	CD3DX12_DESCRIPTOR_RANGE1 InputTextureDescriptor;
	InputTextureDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[FidelityFXSuperResolutionRootSignatureParameters::InputTexture].InitAsDescriptorTable(1, &InputTextureDescriptor);

	CD3DX12_DESCRIPTOR_RANGE1 OutputUAVDescriptor;
	OutputUAVDescriptor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
	Parameters[FidelityFXSuperResolutionRootSignatureParameters::OutputTexture].InitAsDescriptorTable(1, &OutputUAVDescriptor);

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
	psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pFidelityFXSuperResolutionCS, sizeof(g_pFidelityFXSuperResolutionCS));
	VERIFY_HRESULT(device.CreateComputePipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pUpscalePSO.ReleaseAndGetAddressOf())));

	psoDesc.CS = CD3DX12_SHADER_BYTECODE(g_pFidelityFXSharpenCS, sizeof(g_pFidelityFXSharpenCS));
	VERIFY_HRESULT(device.CreateComputePipelineState(&psoDesc, IID_GRAPHICS_PPV_ARGS(m_pSharpenPSO.ReleaseAndGetAddressOf())));
}

D3D12_GPU_DESCRIPTOR_HANDLE FidelityFXSuperResolutionPass::Run(
	ID3D12GraphicsCommandList& commandList,
	PassResource OutputBuffer,
	PassResource IntermediateBuffer,
	D3D12_GPU_DESCRIPTOR_HANDLE InputTexture,
	UINT inputWidth,
	UINT inputHeight)
{
	commandList.SetComputeRootSignature(m_pRootSignature.Get());
	
	{
        PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"FidelityFX Upscale");

		ScopedResourceBarrier outputBarrier(commandList, IntermediateBuffer.m_pResource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		
		commandList.SetPipelineState(m_pUpscalePSO.Get());
		commandList.SetComputeRootDescriptorTable(FidelityFXSuperResolutionRootSignatureParameters::OutputTexture, IntermediateBuffer.m_uavHandle);
		commandList.SetComputeRootDescriptorTable(FidelityFXSuperResolutionRootSignatureParameters::InputTexture, InputTexture);

		auto outputDesc = IntermediateBuffer.m_pResource->GetDesc();
		FSRConstants constants = {};
		FsrEasuCon(
			constants.const0, constants.const1, constants.const2, constants.const3,
			inputWidth,
			inputHeight,
			inputWidth,
			inputHeight,
			outputDesc.Width,
			outputDesc.Height);

		commandList.SetComputeRoot32BitConstants(FidelityFXSuperResolutionRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);

		UINT DispatchWidth = (outputDesc.Width - 1) / 16 + 1;
		UINT DispatchHeight = (outputDesc.Height - 1) / 16 + 1;

		commandList.Dispatch(DispatchWidth, DispatchHeight, 1);
	}

	{
        PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"FidelityFX Sharpen");

		ScopedResourceBarrier outputBarrier(commandList, OutputBuffer.m_pResource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		commandList.SetPipelineState(m_pSharpenPSO.Get());
		commandList.SetComputeRootDescriptorTable(FidelityFXSuperResolutionRootSignatureParameters::OutputTexture, OutputBuffer.m_uavHandle);
		commandList.SetComputeRootDescriptorTable(FidelityFXSuperResolutionRootSignatureParameters::InputTexture, IntermediateBuffer.m_srvHandle);

		auto outputDesc = OutputBuffer.m_pResource->GetDesc();
		FSRConstants constants = {};
		FsrRcasCon(constants.const0, 0.2f);

		commandList.SetComputeRoot32BitConstants(FidelityFXSuperResolutionRootSignatureParameters::ConstantsParam, sizeof(constants) / sizeof(UINT32), &constants, 0);

		UINT DispatchWidth = (outputDesc.Width - 1) / 16 + 1;
		UINT DispatchHeight = (outputDesc.Height - 1) / 16 + 1;

		commandList.Dispatch(DispatchWidth, DispatchHeight, 1);
	}
	return OutputBuffer.m_srvHandle;
}