// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystemInternal.h — ★ 23.10.2026 (разнос AsdaInventorySubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaInventorySubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала LogAsdaInven (один экземпляр — DEFINE_LOG_CATEGORY в
//   AsdaInventorySubsystem.cpp) и чистые static-константы/хелперы без состояния — текст байт в байт, у каждой части своя копия.
//   Подключать ПОСЛЕ #include пролога.
#pragma once

#include "CoreMinimal.h"
#include "UI/AsdaInventorySubsystem.h"

// Категория журнала инвентаря: объявление; определение — AsdaInventorySubsystem.cpp (до разноса там был DEFINE_LOG_CATEGORY_STATIC).
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaInven, Log, All);

static const FName  kInvenWindowId(TEXT("INVEN_WIN"));

// ★ Ветка САМОЦВЕТОВ правой панели живёт отдельной подсистемой (`UAsdaSowelSubsystem`), но точки её
//   включения — ровно те же, где эталон наполняет инфо-панель: `CInventoryUI_OnSlotClick` зовёт и
//   `ItemTooltip_PopulateInfoPanel`, и `inventorySowelWin_BuildSowelList` ОДНИМ куском кода
//   (item_inventory.c:36278-36325). Cell < 0 ⇒ выбор снят (`uiWindowCloseAndReset`).
static void SowelNotifySelect(UGameInstance* GI, int32 Container, int32 Cell)
{
	if (UAsdaSowelSubsystem* S = GI ? GI->GetSubsystem<UAsdaSowelSubsystem>() : nullptr)
	{
		if (Cell < 0) { S->OnSelectionCleared(); }
		else          { S->OnItemSelected(Container, Cell); }
	}
}

// ══════════════════════════════ FAsdaItemInstance ══════════════════════════════════════════════

// ★ D4 (аудит `code_dedup_audit_2.md`): чтение/запись little-endian переехали в `AsdaBytes`
//   (Public/AsdaBytes.h) — там же держится единое правило границ для битых записей.
static FORCEINLINE int32 RdI16(const uint8* P) { return AsdaBytes::RdI16(P); }
static FORCEINLINE int32 RdI32(const uint8* P) { return AsdaBytes::RdI32(P); }
static FORCEINLINE void  WrI16(uint8* P, int32 V) { AsdaBytes::WrI16(P, V); }
static FORCEINLINE void  WrI32(uint8* P, int32 V) { AsdaBytes::WrI32(P, V); }
