// Copyright (c) 2026 @sitxovski (Discord)
#pragma once
// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 (баг игроков «Pig Pang Vehicle — This booster shall not be open till level 10»,
//   разбор — сессия item-level-boxes): минимальный уровень предмета (`item_list` +0x50, `RequiredLevel`) при ПРИМЕНЕНИИ.
//   Эталон клиента его не проверяет ни при вскрытии коробки (6096, `CInventory_OnSlotUse` → панель 26500), ни у
//   бустера-гачи (5468, окно 10050), ни у расходника (5028, `Item_UseSelectedAndSend@0xD0A060`, 15 гейтов без уровня);
//   сток сервера — тоже (GameServer.c: `Recv_OpenPackageItem@0x55A820`, `Recv_GachaItemUse@0x4F48C0`,
//   `CharSpendItemUse@0x4D8C40` поля +8 не читают). Итог в оригинале: «бустер свинки-шарика» 28229 (уровень 10)
//   открывается на 4-м уровне. Серверная сторона — патч P95 `[P95_ItemUseGuard]`; здесь — клиентская предпроверка:
//   уровень персонажа ниже → плашка 104012 «Твой уровень слишком низок», пакет не уходит.
//   Ключ `asda.Item.UseLevelGate` (1 — проверять; 0 — как эталон).
#include "CoreMinimal.h"

class UGameInstance;

namespace AsdaItemLevelGate
{
	/** Канал применения — только для журнала (какая ветка отказала). */
	enum class EUse : uint8
	{
		Consumable,   // 5028 — расходник
		Package,      // 6096 — коробка (панель состава 26500)
		Booster       // 5468 — бустер-гача (окно 10050)
	};

	/** true — персонаж НИЖЕ минимального уровня предмета: плашка 104012 показана, применять нельзя.
	 *  false — проверка пройдена, ключ выключен, уровень персонажа ещё неизвестен (0) или у предмета нет записи item_list. */
	ASDAREBORN_API bool RefuseBelowLevel(const UGameInstance* GI, int32 ItemCode, EUse Use);
}
