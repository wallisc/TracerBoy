//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
#include "pch.h"

namespace FallbackLayer
{
    void CompilePSO(ID3D12Device *pDevice, D3D12_SHADER_BYTECODE shaderByteCode, const StateObjectCollection &stateObjectCollection, ID3D12PipelineState **ppPipelineState)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(shaderByteCode);
        psoDesc.NodeMask = stateObjectCollection.m_nodeMask;
        psoDesc.pRootSignature = stateObjectCollection.m_pGlobalRootSignature;

        ThrowInternalFailure(pDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(ppPipelineState)));
    }

    UINT64 UberShaderRaytracingProgram::GetShaderStackSize(LPCWSTR pExportName)
    {
        auto shaderData = GetShaderData(pExportName);
        return shaderData ? shaderData->stackSize : 0;
    }

    UberShaderRaytracingProgram::ShaderData *UberShaderRaytracingProgram::GetShaderData(LPCWSTR pExportName)
    {
      if (pExportName)
      {
        const auto &shaderData = m_ExportNameToShaderData.find(pExportName);
        if (shaderData != m_ExportNameToShaderData.end())
        {
          return &shaderData->second;
        }
      }
      return nullptr;
    }

    StateIdentifier UberShaderRaytracingProgram::GetStateIdentfier(LPCWSTR pExportName)
    {
        auto shaderData = GetShaderData(pExportName);
        return shaderData ? shaderData->stateIdentifier.StateId : (StateIdentifier)0u;
    }


    UberShaderRaytracingProgram::UberShaderRaytracingProgram(ID3D12Device *pDevice, const StateObjectCollection &stateObjectCollection)
    {
    }

    ShaderIdentifier *UberShaderRaytracingProgram::GetShaderIdentifier(LPCWSTR pExportName)
    {
        auto pEntry = m_ExportNameToShaderData.find(pExportName);
        if (pEntry == m_ExportNameToShaderData.end())
        {
            return nullptr;
        }
        else
        {
            // Handing out this pointer is safe because the map is read-only at this point
            return &pEntry->second.stateIdentifier;
        }
    }

    void UberShaderRaytracingProgram::DispatchRays(
        ID3D12GraphicsCommandList *pCommandList, 
        ID3D12DescriptorHeap *pSrvCbvUavDescriptorHeap,
        ID3D12DescriptorHeap *pSamplerDescriptorHeap,
        const std::unordered_map<UINT, WRAPPED_GPU_POINTER> &boundAccelerationStructures,
        const D3D12_FALLBACK_DISPATCH_RAYS_DESC &desc)
    {
        assert(pSrvCbvUavDescriptorHeap);
        pCommandList->SetComputeRootDescriptorTable(m_patchRootSignatureParameterStart + CbvSrvUavDescriptorHeapAliasedTables, pSrvCbvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    }
}
