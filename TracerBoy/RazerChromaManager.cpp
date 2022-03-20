#include "pch.h"

# ifdef _WIN64
#define CHROMA_EDITOR_DLL	"CChromaEditorLibrary64.dll"
#else
#define CHROMA_EDITOR_DLL	"CChromaEditorLibrary.dll"
#endif

RazerChromaManager::RazerChromaManager()
{
	HMODULE razerChromaLib = LoadLibrary(CHROMA_EDITOR_DLL);
	if (razerChromaLib != NULL)
	{
		INIT Init = (INIT)::GetProcAddress(razerChromaLib, "PluginCoreInit");
		if (Init != NULL)
		{
			RZRESULT rzResult = Init();
			if (rzResult == RZRESULT_SUCCESS)
			{
				CreateChromaLinkEffect = (CREATECHROMALINKEFFECT)::GetProcAddress(razerChromaLib, "PluginCoreCreateChromaLinkEffect");
				SetEffect = (SETEFFECT)GetProcAddress(razerChromaLib, "PluginCoreSetEffect");
				DeleteEffect = (DELETEEFFECT)GetProcAddress(razerChromaLib, "PluginCoreDeleteEffect");

				m_bRazerChromaEnabled = (CreateChromaLinkEffect && SetEffect && DeleteEffect);
			}
		}
	}
}

void RazerChromaManager::UpdateLighting(float red, float green, float blue)
{
	if (m_bRazerChromaEnabled)
	{
		RZEFFECTID effectID;
		ChromaSDK::ChromaLink::STATIC_EFFECT_TYPE staticEffectData = {};
		staticEffectData.Color = (DWORD)(red * 0xff) | ((DWORD)(green * 0xff) << 8) | ((DWORD)(blue * 0xff) << 16) | 0xff000000;

		const bool bLightsInitialized = m_lastEffectID != cNullEffectID;
		if (!bLightsInitialized || m_lastColor != staticEffectData.Color)
		{
			RZRESULT rzResult = CreateChromaLinkEffect(ChromaSDK::ChromaLink::CHROMA_STATIC, &staticEffectData, &effectID);
			if (rzResult == RZRESULT_SUCCESS)
			{
				SetEffect(effectID);
			}

			if (bLightsInitialized)
			{
				DeleteEffect(m_lastEffectID);
				m_lastEffectID = effectID;
			}
		}
	}
}