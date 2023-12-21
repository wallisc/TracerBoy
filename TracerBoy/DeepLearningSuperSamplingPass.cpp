#include "pch.h"

#if USE_DLSS
#include "DeepLearningSuperSampling.h"

#define NV_VERIFY(x) \
{																										\
	NVSDK_NGX_Result nvResult = x;																		\
	if(nvResult != NVSDK_NGX_Result_Success)															\
	{																									\
		std::wstring ngxErrorWString = L"NGX error: " + std::wstring(GetNGXResultAsString(nvResult));	\
		std::string ngxErrorString(ngxErrorWString.begin(), ngxErrorWString.end());						\
		OutputDebugString(ngxErrorString.c_str());														\
		HANDLE_FAILURE();																				\
	}																									\
}

DeepLearningSuperSamplingPass::DeepLearningSuperSamplingPass(ID3D12Device& device, bool bEnable)
{
	if (bEnable)
	{
		Enable(device);
	}
}

void DeepLearningSuperSamplingPass::Enable(ID3D12Device& device)
{
	NV_VERIFY(NVSDK_NGX_D3D12_Init(231313132, L".", &device));

	NV_VERIFY(NVSDK_NGX_D3D12_GetCapabilityParameters(&m_pNGXParameters));

	int needsUpdatedDriver = 0;
	unsigned int minDriverVersionMajor = 0;
	unsigned int minDriverVersionMinor = 0;
	NV_VERIFY(m_pNGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsUpdatedDriver));
	NV_VERIFY(m_pNGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minDriverVersionMajor));
	NV_VERIFY(m_pNGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minDriverVersionMinor));
	NV_VERIFY(m_pNGXParameters->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &m_bSupportsDLSS));

	m_bEnabled = true;
}


void DeepLearningSuperSamplingPass::OnResize(
	ID3D12GraphicsCommandList& commandList,
	UINT Width,
	UINT Height,
	UINT UpscaledWidth,
	UINT UpscaledHeight)
{
	if (IsSupported())
	{
		unsigned int CreationNodeMask = 1;
		unsigned int VisibilityNodeMask = 1;
		NVSDK_NGX_Result ResultDLSS = NVSDK_NGX_Result_Fail;

		// TODO: What is this?
		unsigned int renderPreset = 0;

		bool bDoSharpening = false;
		int DlssCreateFeatureFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
		DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
		DlssCreateFeatureFlags |= bDoSharpening ? NVSDK_NGX_DLSS_Feature_Flags_DoSharpening : 0;

		NVSDK_NGX_DLSS_Create_Params DlssCreateParams = {};

		DlssCreateParams.Feature.InWidth = Width;
		DlssCreateParams.Feature.InHeight = Height;
		DlssCreateParams.Feature.InTargetWidth = UpscaledWidth;
		DlssCreateParams.Feature.InTargetHeight = UpscaledHeight;
		DlssCreateParams.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxQuality;
		DlssCreateParams.InFeatureCreateFlags = DlssCreateFeatureFlags;

		// Select render preset (DL weights)
		NVSDK_NGX_Parameter_SetUI(m_pNGXParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, renderPreset);              // will remain the chosen weights after OTA
		NVSDK_NGX_Parameter_SetUI(m_pNGXParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, renderPreset);           // ^
		NVSDK_NGX_Parameter_SetUI(m_pNGXParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, renderPreset);          // ^
		NVSDK_NGX_Parameter_SetUI(m_pNGXParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, renderPreset);       // ^
		NVSDK_NGX_Parameter_SetUI(m_pNGXParameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, renderPreset);  // ^


		NV_VERIFY(NGX_D3D12_CREATE_DLSS_EXT(&commandList, CreationNodeMask, VisibilityNodeMask, &m_pDLSSFeature, m_pNGXParameters, &DlssCreateParams));
	}
}

D3D12_GPU_DESCRIPTOR_HANDLE DeepLearningSuperSamplingPass::Run(
	ID3D12GraphicsCommandList& commandList,
	PassResource OutputBuffer,
	ID3D12Resource* InputBuffer,
	ID3D12Resource* MotionVectors,
	ID3D12Resource* DepthBuffer,
	float PixelOffsetX,
	float PixelOffsetY,
	UINT inputWidth,
	UINT inputHeight)
{
	NVSDK_NGX_D3D12_DLSS_Eval_Params D3D12DlssEvalParams = {};
	D3D12DlssEvalParams.Feature.pInColor = InputBuffer;
	D3D12DlssEvalParams.Feature.pInOutput = OutputBuffer.m_pResource.Get();
	D3D12DlssEvalParams.pInDepth = DepthBuffer;
	D3D12DlssEvalParams.pInMotionVectors = MotionVectors;
	D3D12DlssEvalParams.pInExposureTexture = nullptr;

	D3D12DlssEvalParams.InJitterOffsetX = PixelOffsetX;
	D3D12DlssEvalParams.InJitterOffsetY = PixelOffsetY;
	D3D12DlssEvalParams.InReset = false;
	D3D12DlssEvalParams.InMVScaleX = 1.0f;
	D3D12DlssEvalParams.InMVScaleY = 1.0f;
	D3D12DlssEvalParams.InColorSubrectBase = {};
	D3D12DlssEvalParams.InDepthSubrectBase = {};
	D3D12DlssEvalParams.InTranslucencySubrectBase = {};
	D3D12DlssEvalParams.InMVSubrectBase = {};
	D3D12DlssEvalParams.InRenderSubrectDimensions = { inputWidth, inputHeight };

	NV_VERIFY(NGX_D3D12_EVALUATE_DLSS_EXT(&commandList, m_pDLSSFeature, m_pNGXParameters, &D3D12DlssEvalParams));

	return OutputBuffer.m_srvHandle;
}

void DeepLearningSuperSamplingPass::Disable(ID3D12Device& device)
{
	NVSDK_NGX_D3D12_ReleaseFeature(m_pDLSSFeature);
	NVSDK_NGX_D3D12_DestroyParameters(m_pNGXParameters);
	NVSDK_NGX_D3D12_Shutdown1(&device);

	m_pDLSSFeature = nullptr;
	m_pNGXParameters = nullptr; 

	m_bEnabled = false;
}

#endif