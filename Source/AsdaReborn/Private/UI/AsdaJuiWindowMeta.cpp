// Copyright (c) 2026 @sitxovski (Discord)
#include "UI/AsdaJuiWindowMeta.h"

// Ассет генерирует импортёр JUI (`Tools/asda_jui_generic_umg.py` → `UAsdaUmgBuilder::CreateWindowMetaAsset`).
const TCHAR* UAsdaJuiWindowMetaAsset::kAssetPath = TEXT("/Game/AsdaAssets/UI/DA_JuiWindowMeta.DA_JuiWindowMeta");

const FAsdaJuiWindowMeta* UAsdaJuiWindowMetaAsset::FindWindow(FName WindowId) const
{
	for (const FAsdaJuiWindowMeta& M : Windows)
	{
		if (M.WindowId == WindowId) { return &M; }
	}
	return nullptr;
}
