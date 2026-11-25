// Copyright (c) 2026 @sitxovski (Discord)
// ★★ R5-fix-b 13.11.2026 (третий заход): КАТАЛОГ WBP ОКОН ДЛЯ САМОТЕСТОВ ОКОН — общая точка трёх дословных копий
//   (`AsdaOpenOnTopSelfTest.cpp`, `AsdaRaiseClickSelfTest.cpp`, `AsdaClickThroughSelfTest.cpp`): ассеты `WBP_<id>` в
//   `/Game/AsdaAssets/UI` (кроме папок Login / Dev / Admin) → карта «id окна → имя пакета».
//
//   ★ ИСПРАВЛЕНИЕ КОПИЙ. В СОБРАННОЙ игре реестр ассетов перечисляет, кроме ассета `WBP_<id>`, ещё и класс виджета `WBP_<id>_C`
//   (объект в том же пакете `WBP_<id>`), а у части окон — только его (в прогоне baseline3 так 22 окна: SKILL_WIN, QUEST_WIN,
//   CHARAINFO_WIN, COMMUNITY_WIN и др.). Копии клали такой класс под id `<id>_C`:
//     - `UI.SelfTestOpenOnTop` проверял окна дважды — каталог 446 окон в игре против 233 в редакторе (511 проверок против 294);
//     - `UI.SelfTestRaiseClick` и `UI.SelfTestClickThrough` ищут пакет по id окна из .JUI и окна с одним `_C` молча теряли
//       (в игре без INVEN_WIN и HERO_NOTICE_WIN — 255 проверок против 257 в редакторе).
//   Здесь класс `WBP_<id>_C` из пакета `WBP_<id>` кладётся под id `<id>` — пакет тот же. В редакторе классов в реестре нет:
//   карта та же, что у копий. Только для самотестов (подключается внутри `#if !UE_BUILD_SHIPPING`).
#pragma once
#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

namespace AsdaUiWbpCatalog
{
	// ★ R5-fix-b 13.11.2026 (заключительный заход): разбор ОДНОГО ассета реестра — общий для `Collect` и скана языка (`AsdaLangScanSelfTest.cpp`):
	//   окно ли это (`WBP_*`), его пакет и id окна (класс `WBP_<id>_C` пакета `WBP_<id>` → `<id>`). false — не окно.
	inline bool WindowAsset(const FAssetData& D, FString& OutPackage, FString& OutId)
	{
		const FString N = D.AssetName.ToString();
		if (!N.StartsWith(TEXT("WBP_"))) { return false; }
		OutPackage = D.PackageName.ToString();
		OutId = N.Mid(4);
		if (N == FPackageName::GetShortName(OutPackage) + TEXT("_C")) { OutId.LeftChopInline(2); }   // класс виджета пакета WBP_<id> → id окна
		return true;
	}

	// Дописать в `Wbp` окна `WBP_<id>` из `/Game/AsdaAssets/UI` (синхронный скан папки, как у копий). Совпадение id — последний.
	inline void Collect(TMap<FName, FString>& Wbp)
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AR.ScanPathsSynchronous({ TEXT("/Game/AsdaAssets/UI") }, false);
		TArray<FAssetData> A;
		AR.GetAssetsByPath(FName(TEXT("/Game/AsdaAssets/UI")), A, true);
		for (const FAssetData& D : A)
		{
			FString P, Id;
			if (!WindowAsset(D, P, Id) || P.Contains(TEXT("/Login/")) || P.Contains(TEXT("/Dev/")) || P.Contains(TEXT("/Admin/"))) { continue; }
			Wbp.Add(FName(*Id), P);
		}
	}
}
