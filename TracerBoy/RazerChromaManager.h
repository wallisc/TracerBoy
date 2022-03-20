#pragma once

#include "ChromaSDK/RzChromaSDKTypes.h"
#include "ChromaSDK/RzErrors.h"

typedef RZRESULT(*INIT)();
typedef RZRESULT(*CREATECHROMALINKEFFECT)(ChromaSDK::ChromaLink::EFFECT_TYPE Effect, PRZPARAM pParam, RZEFFECTID* pEffectId);
typedef RZRESULT(*SETEFFECT)(RZEFFECTID EffectId);
typedef RZRESULT(*DELETEEFFECT)(RZEFFECTID EffectId);

class RazerChromaManager
{
public:
	RazerChromaManager();
	void UpdateLighting(float red, float green, float blue);

private:
	bool m_bRazerChromaEnabled = false;
	const RZEFFECTID cNullEffectID = {};
	RZEFFECTID m_lastEffectID = cNullEffectID;
	DWORD m_lastColor = {};

	CREATECHROMALINKEFFECT CreateChromaLinkEffect = NULL;
	SETEFFECT SetEffect = NULL;
	DELETEEFFECT DeleteEffect = NULL;
};