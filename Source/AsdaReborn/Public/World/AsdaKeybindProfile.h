// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaKeybindProfile — DataAsset раскладки клавиш (правило [[ue-editor-friendly]]).
//
// ★ ЗАЧЕМ. В оригинале раскладка живёт в ТЕКСТОВОМ файле `<GameDir>\system\Shortcutkey.ini`
//   (keybind_LoadShortcutkeyIni@0xE3BBE0 / keybindSaveShortcutIni@0xE3C300). Порт читает и пишет
//   ровно тот же файл в том же формате — но файл нельзя редактировать «в редакторе UE», поэтому
//   рядом лежит этот DataAsset: дизайнер правит раскладку в Content Browser, а игрок — в игре/ini.
//
// ★ ПОРЯДОК СЛОЁВ (реализован в UAsdaKeybindConfig::LoadLayout):
//     1) хардкод-дефолты `keybindLoadDefaults@0xE3A9C0` (проверены по бинарю, 85 команд);
//     2) `Binds` этого профиля (если заполнен) — редакторный слой, которого в оригинале нет;
//     3) `system\Shortcutkey.ini`, если найден — ПОБЕЖДАЕТ, ровно как в оригинале
//        (`keybindInputCtrlInit` читает ini сразу после установки дефолтов).
//   Если ini не найден — оригинал ЗАПИСЫВАЕТ его из дефолтов; порт делает то же (bWriteIniIfMissing).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "World/AsdaKeybindConfig.h"
#include "AsdaKeybindProfile.generated.h"

UCLASS(BlueprintType)
class ASDAREBORN_API UAsdaKeybindProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Читать `system\Shortcutkey.ini` оригинала (слой 3). Выключить = играть строго по `Binds`/дефолтам.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Keybind")
	bool bUseShortcutkeyIni = true;

	// Путь к ini. Пусто = автопоиск (`<ProjectDir>/system/Shortcutkey.ini`, затем `<LaunchDir>/system/...`).
	//   Можно указать папку живого клиента, напр. `@@ASTORY_DEV_ROOT_FWD@@/Donors/AsdaChroniclesInterlude_MINE/system/Shortcutkey.ini`.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Keybind", meta = (EditCondition = "bUseShortcutkeyIni"))
	FString ShortcutkeyIniPath;

	// Оригинал при отсутствии ini пишет его из дефолтов (`keybind_LoadShortcutkeyIni` → ветка !pFile).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Keybind")
	bool bWriteIniIfMissing = true;

	// Переназначение клавиши в игре сохраняется обратно в ini (оригинал: `keybindSaveShortcutIni`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Keybind")
	bool bWriteIniOnRebind = true;

	// ★ Редакторный слой: cmd → клавиша. ПУСТО = слой не применяется (работают дефолты+ini).
	//   Диапазон: 1..85 — эталонные действия оригинала, 86..94 — НАШИ расширения (свободная камера,
	//   dev-меню, транспорт-пасхалка; `UAsdaKeybindConfig::IsExtensionCmd`). До правки 2026-09-02
	//   слой резал всё выше 85, из-за чего расширения нельзя было переназначить и здесь.
	//   Имена команд — `UAsdaKeybindConfig::GetCmdName`; VK — Windows virtual-key (см. таблицу имён
	//   `VkToKeyName`, 74 клавиши, ровно те, что умеет биндить оригинал).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Keybind")
	TMap<int32, FAsdaKeyBind> Binds;
};
