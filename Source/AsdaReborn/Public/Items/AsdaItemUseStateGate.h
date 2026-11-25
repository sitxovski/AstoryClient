// Copyright (c) 2026 @sitxovski (Discord)
#pragma once
// ★★★ [ОРИГИНАЛ] 05.11.2026 (закрытие гейта №5 `UseConsumable`, прежде «источника в порту нет»): «персонаж в
//   ограничивающем состоянии — предметы использовать нельзя», нотис **7007** в плашку 221 цветом −11514 на 10 000 мс.
//   Эталон `listProbeEntryTypesSetState@0xA38B40` (entity_char.c:26424) — поиск в СПИСКЕ СОСТОЯНИЙ своего персонажа
//   (`CCharacter+gap_96A[128]`, у порта — `UAsdaEffectsSubsystem`, те же пакеты 5272/4102/6079/5268/5273) записи с
//   `id ∈ {0, 1, 2, 9, 3, 14, 4, 8}` и `type == 0` (`CList_FindByTypeSub@0xF36D90`: node+18 = id, node+20 = type).
//   Имена (`Reports/skill_buff_fx.tsv`): 0 Оледенение, 1 Окаменение, 2 Ловушка, 9 Замаскированная ловушка,
//   3 Слабость (оглушение), 14 Нельзя снять оглушение, 4 Паника, 8 Сон.
//   Условие отказа у эталона: состояние есть И код предмета ВНЕ 1…2000 (коды 1…2000 — особые предметы, им можно).
//   Точки эталона (все три — та же плашка, пакет НЕ уходит):
//     • `Item_UseSelectedAndSend@0xD0A060` (item_inventory.c:38722) — гейт №5 применения расходника;
//     • `itemUseDoubleClick@0xD12580` (:40594) — обработчик ПАНЕЛИ БЫСТРОГО ДОСТУПА (расходники и подтип 34), после 107/106;
//     • `inventory_SendUseItemRequest@0xD13EA0` (:41046) — повтор автолопаты по таймеру.
//   Надевание из окна сумки (`CInventory_OnSlotUse` → `net_sendInventoryItemUse`), перенос на слот и надевание с панели
//   (`Hotbar_SlotActivate` → `net_sendInventoryItemUse`) этого гейта у эталона НЕ имеют — и у порта тоже.
//   Сервер на тот же случай отвечает 5029 кодом 12 (та же плашка 7007, `ApplyUseRefusal`).
//   Ключ `asda.Item.StateLockGate` (1 — как оригинал; 0 — прежний порт: предпроверки нет).
#include "CoreMinimal.h"

class UGameInstance;

namespace AsdaItemUseStateGate
{
	/** true — свой персонаж в ограничивающем состоянии (см. шапку). Ключ не учитывается. */
	ASDAREBORN_API bool IsSelfStateLocked(const UGameInstance* GI);

	/** true — отказ: состояние есть и код вне 1…2000 → плашка 7007 показана, пакет слать нельзя.
	 *  `Where` — только для журнала (какая точка эталона отказала). */
	ASDAREBORN_API bool RefuseIfStateLocked(const UGameInstance* GI, int32 ItemCode, const TCHAR* Where);

	/** Сколько отказов 7007 показал гейт за сессию (для самотеста: плашка 221 держит не больше трёх строк). */
	ASDAREBORN_API int32 GetRefusalCount();
}
