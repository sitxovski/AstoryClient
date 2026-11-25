// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Use.cpp — ★ 23.10.2026 часть UAsdaInventorySubsystem, вынесенная из AsdaInventorySubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaInventorySubsystem.plan.json).
//   Сеть: отправка (использование, двойной клик, расходники, уничтожение, перемещение).
//   #include — те же, что у AsdaInventorySubsystem.cpp, в том же порядке; общее — AsdaInventorySubsystemInternal.h.
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSoulStoneSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Items/AsdaItemLevelGate.h"   // ★ [ИСПРАВЛЕНИЕ] 29.10.2026: минимальный уровень предмета при применении
#include "Items/AsdaItemUseStateGate.h"   // ★ [ОРИГИНАЛ] 05.11.2026: гейт 7007 «ограничивающее состояние»
#include "World/AsdaSkillCastLock.h"   // ★ [ИСПРАВЛЕНИЕ] 05.11.2026: расходник во время своего навыка — после навыка
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель
#include "UI/AsdaScrollBarNomal.h"   // D3: общая полоса прокрутки скина `Nomal_Scr`            // ★ D1/D2/D5: единые UI-хелперы (дедуп 2026-08-08)
#include "UI/AsdaShowOffSubsystem.h"   // ПКМ по ячейке → меню «похвастаться» (28000)
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT_MSGBOX 1500 — AsdaLoc::kBtnOk)
#include "UI/AsdaMiniStateSubsystem.h"   // общий тултип HUD (эталон держит один g_ItemTooltip)
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // свиток возврата склада (5451 подтип 112) → окно 5500
#include "UI/AsdaSelfStoreSubsystem.h"   // билет премиум-лавки (5451 подтип 65) → 6300 (волна Л1 торговли)
#include "UI/AsdaHudSubsystem.h"       // ★ анонс редкой добычи из КОРОБКИ (NotifyItemInfo → 6616), наше добавление
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"      // ★ кнопка 454 — окно питомца 43000 (волна 2)
#include "UI/AsdaItemUpgradeSubsystem.h"  // ★ кнопка 449 — окно улучшения снаряжения 20000 (25.09.2026)
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ кнопка 452 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaDisassemblySubsystem.h"    // ★ кнопка 453 — окно разборки снаряжения 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaCraftSubsystem.h"         // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейты рюкзака и бустера — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "Resources/AsdaSoundSubsystem.h"  // ★ кнопка 449 — звук id 5 ветки 66561 (волна 5 заточки)
#include "UI/AsdaFishingToolSubsystem.h"   // ★ окно «Взять рыболовную снасть» 15900 (точки открытия/закрытия)
#include "UI/AsdaFishBookSubsystem.h"      // ★ книга рыбака 74000: использование книги-предмета (подтип 210) → диалог 79
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
#include "UI/AsdaPetHatcherySubsystem.h"   // ★ окно ИНКУБАТОРА 42000 (двойной клик по предмету subType 107)
#include "Npc/AsdaNpcDialogSubsystem.h"     // ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)
#include "Pet/AsdaPetSubsystem.h"          // ★ волна 6: корм (subType 106 → 6108) и расширитель ячеек (104 → 6121)
#include "UI/AsdaSkillUseSubsystem.h"      // ★ волна 2: выбранная цель для поля @0 запроса 6096
#include "UI/AsdaPackagePanelSubsystem.h"  // ★ панель состава пакета 26500 (двойной клик открывает ЕЁ)
#include "UI/AsdaCampWarSubsystem.h"      // ★ 30.10.2026: предмет «сброс армии» (subType 113) → TWOBUT 10141 → 6704
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ окно барабана гачи 10050 (двойной клик открывает ЕГО)
#include "UI/AsdaPremiumGachaSubsystem.h"  // ★ гейт `sub_922D5D@0xB6F370` — гейты №15/№15b применения лопат
#include "UI/AsdaPremiumShopSubsystem.h"   // ★ 29.09.2026: `g_pPartyTargetObj` = элитный магазин (гейты №15/№15b лопат)
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"   // ★ 08.11.2026: гейт «персонаж занят» (`objStateAllowsRender` = `IsLocalPlayerBusy`) премиум-канала
#include "Engine/DataTable.h"              // сырые поля шаблона предмета (DataHex) для гейтов панели
#include "World/AsdaPlayerCharacter.h"     // ★ 09.10.2026 (п. 5): состояние лопаты 40/43 своего персонажа (оружие за спину)
#include "Resources/AsdaTextureSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"   // DoubleClickTime — интервал двойного клика Slate
#include "HAL/IConsoleManager.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaScrollCallSubsystem.h"     // ★ волна 2-Г: свитки перемещения/призыва (окно 1552)
#include "UI/AsdaCharRenameSubsystem.h"     // ★ 09.11.2026: предмет смены имени (подтип 111, окно 7900, 6138/6140)
#include "UI/AsdaSkillTreeResetSubsystem.h" // ★ 25.10.2026: свитки сброса навыков (вопрос 12866, окно ветки 16500)
#include "UI/AsdaUpgradeStoneSubsystem.h"   // ★ 31.10.2026: камни переноса улучшения 204/206 (взвод, выбор вещи, 6551)
#include "UI/AsdaUpgradeSplitSubsystem.h"   // ★ 31.10.2026: пустые камни 203/205 → окно разделения улучшения 50500 [ПОРТ]
#include "AsdaInventorySubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaInventorySubsystem*.cpp (LogAsdaInven, чистые хелперы)
// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущему — только ход и предметы (тело — AsdaPlayerCharacter_Move.cpp).
bool AsdaSwimActionLocked(const class AAsdaPlayerCharacter* Ch);
bool AsdaDuelLoserLocked(const class AAsdaPlayerCharacter* Ch);   // ★ 31.10.2026: жест поражения дуэли


static ASDA_DEV_CVAR(int32, CVarLegacyTrashNoConfirm,
	TEXT("asda.Inven.LegacyTrashNoConfirm"),
	0,
	TEXT("Корзина 405: 0 = спрашиваем подтверждение эталонным окном TWOBUT_MSGBOX перед 5006 (Д3); "
	     "1 = как эталон — уничтожаем сразу, без вопроса."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 26.10.2026 (баг игроков «Dragging an item outside of the inventory window fails to delete or drop it»):
//   бросок предмета ВНЕ ВСЕХ ОКОН. Порт молча возвращал предмет (комментарий «эталон ничего не делает» был ложным «нет»:
//   смотрели только ветку `Field91`, а ветку `a3 == nullptr` — нет). Эталон и разбор — `DropOutsideWindows`.
static ASDA_DEV_CVAR(int32, CVarDropOutsideDelete,
	TEXT("asda.Inven.DropOutsideDelete"),
	1,
	TEXT("Бросок предмета сумки/расходников вне всех окон: 1 = как оригинал — вопрос 2059 «Предмет будет уничтожен…» (стек расходников — "
	     "сначала окно количества), «Да» шлёт 5006; привязанный предмет сумки и предмет без записи item_list — окно 2004; золото — ничего; "
	     "0 = прежнее поведение порта (предмет молча возвращается на место)."),
	ECVF_Default);

// ★ [ОРИГИНАЛ] 26.10.2026: количество 5006 у предметов с `flagWord176` (`netSendItemActionSecure@0xD14DE0`, network.c:18019-18022).
static ASDA_DEV_CVAR(int32, CVarDumpFlag176One,
	TEXT("asda.Inven.DumpFlag176One"),
	1,
	TEXT("5006 (уничтожение): 1 = как оригинал — у предмета с flagWord176 количество в пакете всегда 1; 0 = прежнее (+9 записи / введённое)."),
	ECVF_Default);

// ★ [ОРИГИНАЛ] 26.10.2026: корзина и транспорт (`sowelHandleSlotSelect`, item_inventory.c:41249-41262) — у `DestroyItem`.
static ASDA_DEV_CVAR(int32, CVarTrashVehicleGate,
	TEXT("asda.Inven.TrashVehicleGate"),
	1,
	TEXT("Корзина 405: 1 = как оригинал — предмет сумки special_itemlist с подтипом 15 на транспорте не уничтожается (окно 2040); "
	     "0 = прежнее поведение порта (вопрос 2005)."),
	ECVF_Default);

// ★ [ОРИГИНАЛ] 26.10.2026: гейты сообщений 5/7 окна инвентаря (`CGameWindow_OnCommand`, ui_core.c:47768-47800) — `DragGateRefuses`.
static ASDA_DEV_CVAR(int32, CVarDragGates,
	TEXT("asda.Inven.DragGates"),
	1,
	TEXT("Захват и бросок предмета в окне инвентаря: 1 = как оригинал — при открытом барабане бустера (окно 10050) нотис 13106 в чат, "
	     "при активном свитке вызова хранилища — 10309, действие не выполняется; 0 = прежнее поведение порта (гейтов нет)."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 26.10.2026 (баг игроков «Second ring slot is non-functional»): выбор слота пары колец 5/6.
//   Порт слал всегда `dstSlot = itemType` (= 5) и игнорировал слот-цель броска ⇒ второе кольцо лишь МЕНЯЛОСЬ местами с первым
//   (журнал игрока 25.10: «обмен местами (2:9 → 3:5)» трижды при пустом слоте 6). Эталон — `EquipItemToSlot` ниже.
static ASDA_DEV_CVAR(int32, CVarRingSlotPick,
	TEXT("asda.Inven.RingSlotPick"),
	1,
	TEXT("Кольца (itemType 5/6) из контейнера 2: 1 = как оригинал — двойной клик в первый свободный из слотов 5/6 (оба заняты — замена в 5), "
	     "бросок на слот 5/6 — ровно в него; 0 = прежнее поведение порта (всегда слот 5, цель броска не учитывается)."),
	ECVF_Default);

// Слот пары колец по правилу эталона. `DropEquipSlot` — слот манекена под курсором броска (−1 — двойной клик).
//   Бросок: 5 → 5, 6 → 6 (`netItem_SendUseRequest@0xD19A70`: `useModeHint == 605/606`, network.c:18635-18642), бросок на
//   другой слот манекена и двойной клик — «первый свободный» (network.c:18643-18652 и item_inventory.c:37940-37956):
//   слот 5 занят ? (слот 6 занят ? 5 : 6) : 5. Занятость эталон берёт по коду предмета (`*(int*)(char+57·slot+292) > 0`).
static int32 AsdaRingPairSlot(const UAsdaInventorySubsystem& Inv, int32 DropEquipSlot)
{
	if (DropEquipSlot == 5 || DropEquipSlot == 6) { return DropEquipSlot; }
	const FAsdaItemInstance* S5 = Inv.GetCell(3, 5);
	const FAsdaItemInstance* S6 = Inv.GetCell(3, 6);
	const bool b5 = S5 && S5->ItemCode > 0;   // слот 5 занят
	const bool b6 = S6 && S6->ItemCode > 0;   // слот 6 занят
	return b5 ? (b6 ? 5 : 6) : 5;
}

// Слот надевания (`dstSlot` пакета 5000, dstType 3). Обычно `itemType` (сервер сверяет: `sub_4CDAA0@0x4CDAA0`, отказ 11);
//   кольцо из контейнера 2 — правило пары (сервер пускает `itemType 5` при `+27 == 0` в слоты 5 и 6, GameServer.c:81998).
//   ⚠ Контейнер 1 эталон правилом пары НЕ обслуживает: и двойной клик (`CInventory_OnSlotUse`, ветка `m_nSelKind == 1`,
//   item_inventory.c:38216-38217), и бросок (`:41707` → тот же `CInventory_OnSlotUse`) шлют `itemType`.
static int32 AsdaEquipDstSlot(const UAsdaInventorySubsystem& Inv, int32 SrcContainer, const FAsdaItemRecord& Rec, int32 DropEquipSlot)
{
	const bool bRing = (Rec.ItemType == 5 || Rec.ItemType == 6);
	if (bRing && SrcContainer == 2 && CVarRingSlotPick.GetValueOnGameThread() != 0)
	{
		return AsdaRingPairSlot(Inv, DropEquipSlot);
	}
	return Rec.ItemType;
}

// ══════════════════════════════ СЕТЬ: ОТПРАВКА ═════════════════════════════════════════════════

bool UAsdaInventorySubsystem::SendMovePacket(const FAsdaItemInstance& Src, int32 DstSlot, int32 DstType, const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	if (bActionPending)
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] %s: ждём ответ 5001 на предыдущее действие — пропуск"), Why);
		return false;
	}
	if (Src.IsEmpty() || Src.Raw.Num() < FAsdaItemInstance::kRecordSize) { return false; }
	// ⚠ Конверт «8 токен + 16 MD5 + payload» собирает ОБЩИЙ SendSecured внутри Net — своей копии нет.
	if (!Net->SendItemMove5000(Src.Raw, DstSlot, DstType)) { return false; }
	bActionPending = true;   // эталон `g_bInventoryActionPending`
	ActionSentAtS = FPlatformTime::Seconds();
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] %s: 5000 отправлен (ждём 5001)"), Why);
	return true;
}

bool UAsdaInventorySubsystem::MoveItem(int32 SrcContainer, int32 SrcCell, int32 DstContainer, int32 DstCell)
{
	const FAsdaItemInstance* Src = GetCell(SrcContainer, SrcCell);
	if (!Src || Src->IsEmpty()) { return false; }
	// Сервер: перестановка разрешена только 1→1, 2→2, 5→5, 4→4; 1↔2 ветки НЕТ вовсе.
	if (SrcContainer != DstContainer)
	{
		Notice(2014);
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] перенос %d→%d запрещён сервером (в CharItemMove такой ветки нет)"),
			SrcContainer, DstContainer);
		return false;
	}
	if (SrcCell == DstCell) { return false; }
	if (DstCell < 0 || DstCell >= GetContainerSize(DstContainer)) { return false; }
	return SendMovePacket(*Src, DstCell, DstContainer, TEXT("перемещение"));
}

// ★★★ «РУКИ ЗАНЯТЫ ДВУРУЧНЫМ» — поле шаблона предмета **+0xA8** (i32).
//   Эталон читает его дважды и оба раза сравнивает с 2:
//     • `itemUseCheckBagFull@0xD12370` (item_inventory.c:40440) — у НАДЕВАЕМОГО предмета;
//     • `net_sendInventoryItemUse@0xD11B20` (network.c:17856)   — у УЖЕ надетого оружия.
//   Отдельной колонки у этого поля в порту нет — читаем сырой байт-блок записи (`ItemRawI32`).
static constexpr int32 kItemRawTwoHandOfs = 0xA8;

// ★★★ ЭТАЛОННЫЙ ГЕЙТ `itemUseCheckBagFull@0xD12370` (item_inventory.c:40423).
//   СМЫСЛ: надеваем ДВУРУЧНОЕ (+0xA8 == 2), а в руках сейчас И оружие (слот 9), И щит (слот 8) ⇒
//   на сервере ДВЕ вещи поедут в сумку, а слот освободится только один. Клиент считает занятые
//   ячейки контейнера 1 и при 30 из 30 даёт чат-нотис **1011** «Нет места в рюкзаке.» и НЕ шлёт пакет.
//   Нет записи в item-БД вовсе ⇒ чат-нотис **501** и тоже отказ.
//   ⚠ Адресация слотов доказана арифметикой эталона: `localPlayer->gap_636[448]` = 57·8+292 = слот 8
//   (щит), `gap_7FE[49]` = 57·9+292 = слот 9 (оружие) — тот же массив, что `gap_50A[57·slot+292]`.
bool UAsdaInventorySubsystem::CheckBagRoomForTwoHand(int32 ItemCode) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!Rec)
	{
		if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			Msg->PostLoc(EAsdaMsgCategory::ItemRefused, 501);
		}
		return false;
	}
	if (ItemRawI32(IT, ItemCode, kItemRawTwoHandOfs) != 2) { return true; }   // не двуручное — гейта нет
	const FAsdaItemInstance* Shield = GetCell(3, 8);
	const FAsdaItemInstance* Weapon = GetCell(3, 9);
	const bool bBothHeld = Shield && !Shield->IsEmpty() && IT->GetItem(Shield->ItemCode)
	                    && Weapon && !Weapon->IsEmpty() && IT->GetItem(Weapon->ItemCode);
	if (!bBothHeld) { return true; }
	if (FindFreeSlot(1) >= 0) { return true; }
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		Msg->PostLoc(EAsdaMsgCategory::ItemRefused, 1011);
	}
	UE_LOG(LogAsdaInven, Log,
		TEXT("[инвентарь] надеть %d: двуручное при занятых слотах 8 и 9, а сумка полна — гейт `itemUseCheckBagFull`, нотис 1011"),
		ItemCode);
	return false;
}

bool UAsdaInventorySubsystem::EquipItem(int32 SrcContainer, int32 SrcCell)
{
	return EquipItemToSlot(SrcContainer, SrcCell, /*DropEquipSlot: двойной клик — цели нет*/ -1);
}

bool UAsdaInventorySubsystem::EquipItemToSlot(int32 SrcContainer, int32 SrcCell, int32 DropEquipSlot)
{
	const FAsdaItemInstance* Src = GetCell(SrcContainer, SrcCell);
	if (!Src || Src->IsEmpty()) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Src->ItemCode) : nullptr;
	if (!Rec)
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь] надеть %d: записи в item-БД нет"), Src->ItemCode);
		return false;
	}
	// ── ГЕЙТЫ ЭТАЛОНА `net_sendInventoryItemUse@0xD11B20` (network.c:17806-17878), тем же порядком ──
	//   №1 (:17845) `itemUseCheckBagFull(code)` — двуручное при занятых обеих руках и полной сумке.
	if (!CheckBagRoomForTwoHand(Src->ItemCode)) { return false; }
	//   №2 (:17847) самоцвет либо `itemType == 11` (расходник) — молча, нотиса у эталона нет.
	if (Rec->ItemType == 11 || (IT && IT->IsSowelCode(Src->ItemCode)))
	{
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] надеть %d не шлём: расходник/самоцвет (гейт `net_sendInventoryItemUse` №2, нотиса нет)"),
			Src->ItemCode);
		return false;
	}
	//   №3 (:17856) ЩИТ (itemType 8) поверх НАДЕТОГО ДВУРУЧНОГО — молча ничего не делаем:
	//     `if (weapon > 0 && GetItemRecord(weapon)->+0xA8 == 2 && ItemRecord->itemType == 8) → выход`.
	if (Rec->ItemType == 8)
	{
		const FAsdaItemInstance* Weapon = GetCell(3, 9);
		if (Weapon && !Weapon->IsEmpty() && ItemRawI32(IT, Weapon->ItemCode, kItemRawTwoHandOfs) == 2)
		{
			UE_LOG(LogAsdaInven, Log,
				TEXT("[инвентарь] надеть щит %d не шлём: в руках двуручное %d (гейт `net_sendInventoryItemUse` №3, нотиса нет)"),
				Src->ItemCode, Weapon->ItemCode);
			return false;
		}
	}
	//   №4 (:17860) ПРИВЯЗКА ПРИ НАДЕВАНИИ: `!запись[50] && ItemRecord->tooltipFlag200 == 1` ⇒ пакет
	//     НЕ уходит, а показывается подтверждение с текстом **7201** («…он привяжется к тебе… Сделать
	//     это?»), и лишь ответ «ОК» отправляет 5000. Байт записи +50 — «уже привязан».
	if (Rec->TooltipFlag200 == 1 && Src->Raw.IsValidIndex(50) && Src->Raw[50] == 0)
	{
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (WM)
		{
			const int32 Cont = SrcContainer, Cell = SrcCell, Drop = DropEquipSlot;
			AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(LocText(7201),
				[this, Cont, Cell, Drop](bool bOk)
				{
					if (!bOk) { return; }
					ConfirmBindAndEquip(Cont, Cell, Drop);
				}, 0.f, 2288, 2289, /*очередь: эталон (1, 7201) network.c:17865*/7201, 1);
			UE_LOG(LogAsdaInven, Log,
				TEXT("[инвентарь] надеть %d: предмет привязывается — показано подтверждение 7201 (эталон `CItem_BuildTooltipText(…,1,7201,…)`)"),
				Src->ItemCode);
			return false;
		}
		UE_LOG(LogAsdaInven, Warning,
			TEXT("[инвентарь] надеть %d: подсистемы окон нет — подтверждение привязки пропущено"), Src->ItemCode);
	}
	// ★ Номер слота экипировки определяется ПОЛЕМ ТИПА предмета: `dstSlot = ItemRecord->itemType`
	//   (network.c:17875 `*(_WORD *)((char *)&v25[5] + 1) = ItemRecord->itemType`). Сервер это же
	//   значение перепроверяет в `sub_4CDAA0@0x4CDAA0` (`a4 == itemTemplate+26`, иначе отказ 11).
	//   ★ [ОРИГИНАЛ] 26.10.2026: кольцо из контейнера 2 — слот пары 5/6 (`AsdaEquipDstSlot`; сервер это послабление знает).
	const int32 DstSlot = AsdaEquipDstSlot(*this, SrcContainer, *Rec, DropEquipSlot);
	if (DstSlot != Rec->ItemType || DropEquipSlot >= 0)
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] надеть %d (тип %d) из %d:%d: цель броска %d → слот %d"),
			Src->ItemCode, Rec->ItemType, SrcContainer, SrcCell, DropEquipSlot, DstSlot);
	}
	return SendMovePacket(*Src, DstSlot, /*DstType*/ 3, TEXT("надеть"));
}

// Ответ «ОК» на подтверждение привязки (7201). Эталон помечает запись привязанной прямо в клиенте
//   (`inventoryApplySlotItem@0xD60BA0` case 3: `*((_BYTE *)Src + 50) = 1` при `tooltipFlag200 == 1`)
//   и отправляет тот же 5000, что и обычное надевание.
void UAsdaInventorySubsystem::ConfirmBindAndEquip(int32 SrcContainer, int32 SrcCell, int32 DropEquipSlot)
{
	TArray<FAsdaItemInstance>* C = Container(SrcContainer);
	if (!C || !C->IsValidIndex(SrcCell)) { return; }
	FAsdaItemInstance& I = (*C)[SrcCell];
	if (I.IsEmpty()) { return; }
	if (I.Raw.IsValidIndex(50)) { I.Raw[50] = 1; I.SyncFromRaw(); }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(I.ItemCode) : nullptr;
	if (!Rec) { return; }
	// ★ [ОРИГИНАЛ] 26.10.2026: слот пары колец считается в момент «ОК» (за время вопроса слоты 5/6 могли смениться).
	SendMovePacket(I, AsdaEquipDstSlot(*this, SrcContainer, *Rec, DropEquipSlot), /*DstType*/ 3, TEXT("надеть (привязка подтверждена)"));
}

bool UAsdaInventorySubsystem::UnequipItem(int32 EquipSlot)
{
	if (!IsEquipSlotUsable(EquipSlot)) { return false; }
	const FAsdaItemInstance* Src = GetCell(3, EquipSlot);
	if (!Src || Src->IsEmpty()) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Src->ItemCode) : nullptr;
	// Эталон `itemAction_SendUseEquipPacket@0xC69900`: dstType = 2 при equipSlot ∈ {5,6,7} или
	//   `ItemRecord->flagWord176`, иначе 1. dstSlot = 0 — сервер сам берёт первый свободный.
	const bool bToSpend = (EquipSlot >= 5 && EquipSlot <= 7) || (Rec && Rec->FlagWord176 != 0);
	const int32 DstType = bToSpend ? 2 : 1;
	++DebugUnequipRequests;   // ★ волна 11: самотест видит, что двойной клик дошёл до «снять» (сеть может отказать)
	// ★★★ УБРАН ВЫДУМАННЫЙ ГЕЙТ (2026-08-26). Здесь стоял «нет свободной ячейки ⇒ нотис 13106 и не
	//   шлём». Это была ОТСЕБЯТИНА, и она же — вторая причина бага «вещь снять невозможно»: сумка
	//   игрока была заполнена (лог 2026-08-25: «контейнер 1 получен целиком: занято 30 из 30»),
	//   поэтому снятие ЛЮБОЙ брони отбивалось молча ещё до сети, да ещё и чужим текстом
	//   («Ничего не выйдет, пока идет работа с бустером» — loc 13106 не про место в сумке).
	//   Эталон в `itemAction_SendUseEquipPacket` НИКАКОЙ проверки места НЕ ДЕЛАЕТ: пакет уходит
	//   всегда, а полную сумку отбивает СЕРВЕР ответом 5001 result=0 → наш нотис **2014** «Нет места.»
	//   (таблица в `ApplyMoveResult`). Авторитет — сервер, гадать на клиенте нечего.
	const bool bSent = SendMovePacket(*Src, /*DstSlot*/ 0, DstType, TEXT("снять"));
	if (bSent) { PendingUnequipSlot = EquipSlot; }   // ★ 07.11.2026: `asda.Mount.DismountOnUnequip` — решение по ответу 5001
	return bSent;
}

bool UAsdaInventorySubsystem::UseConsumable(int32 ContainerId, int32 CellIndex, int32 TargetUserIdx)
{
	const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
	if (!Src || Src->IsEmpty()) { return false; }
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Duel.LoserLock`): пока свой персонаж доигрывает жест поражения дуэли (51) — предметы не
	//   применяются, МОЛЧА (у оригинала гейта нет — жест обрывался; разбор у `AsdaDuelLoserLocked`).
	{
		const UAsdaWorldSubsystem* DlW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (DlW && AsdaDuelLoserLocked(Cast<AAsdaPlayerCharacter>(DlW->LocalPlayerActor.Get())))
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] %s: жест поражения дуэли — предмет не применён"), ANSI_TO_TCHAR(__FUNCTION__));
			return false;
		}
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Src->ItemCode) : nullptr;

	// ═══════════ ★★★ ПРЕД-ПРОВЕРКИ ЭТАЛОНА `Item_UseSelectedAndSend@0xD0A060` ═══════════════════
	//   Полный список — 15 гейтов, `item_inventory.c:38709-38936`, в этом же порядке.
	//   Порядок важен: эталон выходит на ПЕРВОМ сработавшем, поэтому и нотис виден только один.
	//
	//   №1 (:38712) слот пуст — сделано выше (`Src->IsEmpty()`).
	//   №2 (:38715) `!ItemRecord || itemType != 11` — сделано ниже по `Rec`.
	//   №3 (:38717) код самоцвета или 20551 (золото) — нотиса нет, просто выход.
	if (!Rec || Rec->ItemType != 11)
	{
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] 5028 не отправлен: у предмета %d нет записи item_list или itemType != 11 ")
			TEXT("(эталон `Item_UseSelectedAndSend`, гейт №2 — нотиса нет)"), Src->ItemCode);
		return false;
	}
	if (IT && (IT->IsSowelCode(Src->ItemCode) || Src->ItemCode == 20551))
	{
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] 5028 не отправлен: %d — самоцвет или золото (гейт №3, нотиса нет)"), Src->ItemCode);
		return false;
	}
	//   №4 (:38720) нет локального игрока (`CharManager_GetLocalPlayer`). У порта эквивалент —
	//     известный `SelfUserNum` (он приходит пакетом 4003, память `movement-block-solved`).
	if (!Net->IsSelfUserNumKnown())
	{
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] 5028 не отправлен: свой userNum ещё не известен (гейт №4)"));
		return false;
	}
	//   №5 (:38722-38727) ★ [ОРИГИНАЛ] 05.11.2026 — ЗАКРЫТ: персонаж в ограничивающем состоянии (оледенение, окаменение,
	//     ловушка, оглушение, паника, сон — `listProbeEntryTypesSetState@0xA38B40`) и код вне 1…2000 → плашка 7007, 5028
	//     не уходит. Общая точка `AsdaItemUseStateGate` (ключ `asda.Item.StateLockGate`).
	if (AsdaItemUseStateGate::RefuseIfStateLocked(GI, Src->ItemCode, TEXT("расходник 5028 (Item_UseSelectedAndSend, гейт №5)")))
	{
		return false;
	}
	// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026: минимальный уровень предмета — у эталона среди 15 гейтов его нет, сток сервера
	//   тоже не проверяет (сервер — P95). Ниже уровня → плашка 104012, 5028 не уходит. Ключ `asda.Item.UseLevelGate`.
	if (AsdaItemLevelGate::RefuseBelowLevel(GI, Src->ItemCode, AsdaItemLevelGate::EUse::Consumable))
	{
		return false;
	}
	//   №13 (:38788-38798) `itemType == 11 && itemSubType == 33 && listContainsByTypeId(состояния, 15, 2)`
	//     → чат-нотис **2040** и выход. Состояние (id 15, type 2) — это «на транспорте»: тот самый
	//     тип 2, который вешает `charStatusAddType2`, а id 15 = подтип транспорта `special_itemlist`.
	//     Смысл: с лопатой на маунте не работают.
	//   ★ 29.09.2026 (волна О2 торговли, ревизия О1 МЕЛК-5): у эталона та же проверка стоит и для АВТО-лопаты
	//     (`itemType == 11 && itemSubType == 34`, item_inventory.c:38815-38820 → LABEL_48 = 2040) — первой в её ветке,
	//     ДО гейта 9001. Раньше порт проверял «верхом» только у подтипа 33: верхом при открытом элитном магазине авто-лопата
	//     давала 9001 вместо 2040. Условие `itemType == 11` — как у эталона (у обеих веток).
	//   ★ 09.10.2026 (автолопата): три гейта лопат (2040 / 9002 / 9001) считает ОДНА точка `ShovelUseGate` — её же зовёт повтор
	//     автолопаты (`SendAutoDigRepeat`, эталон `inventory_SendUseItemRequest`): порядок у всех трёх мест эталона один.
	const int32 ShovelGate = (Rec->ItemType == 11 && (Rec->ItemSubType == 33 || Rec->ItemSubType == 34)) ? ShovelUseGate() : 0;   // loc отказа, 0 — пройдено
	// ★ 28.10.2026 [ПОРТ] (решение владельца): лопата и автолопата — действие с анимацией копания; плывущему — МОЛЧА нельзя.
	//   Прочие предметы сумки — можно. Ключ `asda.Swim.ActionGate`.
	if (Rec->ItemType == 11 && (Rec->ItemSubType == 33 || Rec->ItemSubType == 34))
	{
		const UAsdaWorldSubsystem* SwW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (SwW && AsdaSwimActionLocked(Cast<AAsdaPlayerCharacter>(SwW->LocalPlayerActor.Get())))
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] 5028 не отправлен: лопата %d — персонаж плывёт"), Src->ItemCode);
			return false;
		}
	}
	if (ShovelGate == 2040)
	{
		Notice(2040);   // ★ 09.10.2026 (МЕЛК-2); [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] 5028 не отправлен: лопата %d при активном состоянии (15,2) — гейт №13, нотис 2040"),
			Src->ItemCode);
		return false;
	}
	//   ★★ №14 (:38800-38804 / :38823-38826; двойной клик :40810-40814 / :40829-40835) — ★ 09.10.2026 (ревизия six_fixes
	//     СРЕДН-1): `Net_GetCurrentMapType(LP) == 40 || == 43` (миснейм — состояние действия `+47551`: лопата/авто-лопата) →
	//     чат-нотис **9002** «уже копаете» и `return 0`, 5028 НЕ уходит. Без гейта сервер (`CharSpendItemUse`, GameServer.c:86043)
	//     своё состояние не проверяет и на повторное применение снова шлёт 5429 — лишняя добыча (двойной клик во время копки).
	//     Источник состояния — общая точка `AAsdaPlayerController::KbEtalonActionState` (40/43 — `GetEtalonShovelState`).
	if (ShovelGate == 9002)
	{
		Notice(9002);
		UE_LOG(LogAsdaInven, Log,
			TEXT("[инвентарь] 5028 не отправлен: лопата %d (подтип %d) во время копания 40/43 — гейт №14, нотис 9002"),
			Src->ItemCode, Rec->ItemSubType);
		return false;
	}
	//   ★★★ №15 (:38807) и №15b (:38827) — `subType == 33` / `subType == 34` при
	//     `g_pPartyTargetObj || sub_922D5D(g_pGachaScene)` → чат-нотис **9001** и `return 0`.
	//     ГАЧА-ПОЛОВИНА ГЕЙТА ТЕПЕРЬ ЕСТЬ: `UAsdaPremiumGachaSubsystem::IsSceneOpen()` — порт
	//     `sub_922D5D` → `sub_B6F370@0xB6F370` (`ui_core.c:125071`). Вторая половина
	//     ★ ПОПРАВКА 29.09.2026 (волна О0 торговли, реестр №37②): `g_pPartyTargetObj` (0x2BE779C) — МИСНЕЙМ: это НЕ
	//     «идёт обмен/приглашение», а `g_pShopWnd(0x2BE5730)+0x206C` = поле 2075 = «открыт ЭЛИТНЫЙ МАГАЗИН (Shell Item
	//     Store)» (писатели `uiInventoryOpenWindow@0xC3DCB0` → 1, `uiInventoryResetAndLeave@0xC3DE00` → 0). Прежняя
	//     фраза «системы обмена нет, взводить нечем» ошибочна: предикат в порту есть — `UAsdaPremiumShopSubsystem::IsOpen`,
	//     и вторая половина гейта теперь проверяется ниже.
	//     Тот же гейт эталон повторяет в `itemUseDoubleClick@0xD12580` (:40818 для подтипа 33,
	//     :40836 для подтипа 34) — в порту двойной клик по расходнику сходится СЮДА ЖЕ
	//     (`UseByDoubleClick` → `UseConsumable`), поэтому обе точки закрыты одной проверкой.
	//   ★ Третья точка того же гейта у эталона — повтор авто-лопаты по таймеру (`inventory_SendUseItemRequest@0xD13EA0`,
	//     item_inventory.c:41038, из цикла game_main.c:2291: 2040 / 9002 / 9001 + `resetActiveAndCounter`).
	//     ★ 09.10.2026 (автолопата, реестр О2-5 закрыт): повтор есть — `SendAutoDigRepeat`, гейты — общая `ShovelUseGate`.
	if (ShovelGate == 9001)
	{
		const UAsdaPremiumGachaSubsystem* Gacha = GI->GetSubsystem<UAsdaPremiumGachaSubsystem>();
		const UAsdaPremiumShopSubsystem* EliteShop = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();   // `g_pPartyTargetObj` (МИСНЕЙМ)
		{
			Notice(9001);
			UE_LOG(LogAsdaInven, Log,
				TEXT("[инвентарь] 5028 не отправлен: предмет %d подтипа %d при %s ")
				TEXT("(эталон гейт №%s, нотис 9001)"),
				Src->ItemCode, Rec->ItemSubType,
				(EliteShop && EliteShop->IsOpen()) ? TEXT("ОТКРЫТОМ элитном магазине (g_pPartyTargetObj)") : TEXT("ОТКРЫТОЙ сцене премиум-гачи"),
				Rec->ItemSubType == 33 ? TEXT("15") : TEXT("15b"));
			return false;
		}
	}
	//
	//   ⚠ ЧЕСТНО О НЕПОРТИРОВАННОМ. Гейты №5, №7…№12, №16 опираются на состояние, которого в
	//   порту ПОКА НЕТ ни в каком виде (это не упрощение, а отсутствующий источник данных):
	//     №5  (:38723) ★ ЗАКРЫТ 05.11.2026 (выше, `AsdaItemUseStateGate`) — `listProbeEntryTypesSetState@0xA38B40` — «персонаж в ограничивающем состоянии»
	//                  (типы 0,1,2,9,3,14,4,8 в списке `gap_96A[128]`) + код вне 1…2000 → нотис
	//                  **7007** в ОБЩУЮ ПЛАШКУ (`CControl_SetTooltipText`), не в чат;
	//     №7…№10 (:38742-38760) `linkCode 7/8` при `Net_GetCurrentMapType() == 6` и при флагах
	//                  `this[23]`/`this[24]` → нотис **2047** (только для `[24]`);
	//     №11 (:38763) `itemSubType == 16 && objStateAllowsRender(localPlayer)` → нотис **2049**;
	//     №12 (:38773) `itemSubType == 15 && g_nInputControlState` → **2034** (нет выбранного члена
	//                  группы) либо **2035**;
	//     №16 (:38931) `itemSubType ∈ 0x21…0x2A && *(BYTE*)(g_pLocalEntity+68) == 1` → **2020**.
	//   (№15/№15b выше больше не в этом списке — гача-половина реализована; №14 «9002» — тоже: ★ 09.10.2026, ревизия
	//    six_fixes СРЕДН-1, источник состояния 40/43 — `KbEtalonActionState`; прежняя пометка «нет источника» устарела.)
	//   Не хватает: типа карты (`Net_GetCurrentMapType`), флага режима прицеливания
	//   (`g_nInputControlState`), состояния рендера персонажа (`objStateAllowsRender`) и
	//   байта `g_pLocalEntity+68`. Все они — авторитет СЕРВЕРА: на каждый из этих случаев сервер
	//   отвечает 5029 своим кодом отказа, и нотис игрок всё равно увидит (таблица `ApplyUseRefusal`).
	//
	// ★★★ ПРЕД-ПРОВЕРКА ОТКАТА ДО ОТПРАВКИ. Эталон `Item_UseSelectedAndSend@0xD0A060` (проверка №6):
	//   `if (lookupClassStatusByKey(g_pSkillWnd, itemCode, containerType, slotIndex) <= 1)` → чат-нотис
	//   **9565** и `return 0`, пакет НЕ уходит. Это оптимизация клиента: авторитет у сервера, который
	//   на тот же случай отвечает 5029 с кодом результата 3 (и тем же нотисом 9565).
	if (UAsdaHotbarSubsystem* Hb = GI->GetSubsystem<UAsdaHotbarSubsystem>())
	{
		if (Hb->GetItemCooldownState(Src->ItemCode, ContainerId, CellIndex) <= 1)
		{
			UE_LOG(LogAsdaInven, Log,
				TEXT("[инвентарь] применение %d отклонено ЛОКАЛЬНО: откат ещё идёт, осталось %.1f с ")
				TEXT("(эталон `Item_UseSelectedAndSend@0xD0A060`, нотис 9565 — 5028 не отправляем)"),
				Src->ItemCode, Hb->GetItemCooldownRemainSec(Src->ItemCode, ContainerId, CellIndex));
			Notice(9565);
			return false;
		}
	}

	// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (просьба владельца, `World/AsdaSkillCastLock`): во время своего навыка (заявка 25, клип 24, каст
	//   14/13/27) расходник не применяется — запоминается и применяется сразу после навыка (у эталона гейта нет: `Item_UseSelectedAndSend`
	//   и `itemUseDoubleClick` состояние навыка не проверяют). Стоит ПОСЛЕ всех пред-проверок:
	//   их отказы (откат 9565, 7007, уровень, лопата) видны сразу; откладывается только то, что ушло бы 5028. Возврат true — нажатие принято.
	if (AsdaSkillCastLock::GateItemUse(this, ContainerId, CellIndex, TargetUserIdx, Src->ItemCode)) { return true; }
	// ★ Цель заполняется ТОЛЬКО у предметов с `itemSubType == 15` (применение на другого игрока).
	// ★ ИСПРАВЛЕНО: во всех прочих случаях на проводе должен быть НОЛЬ, а не −1. Эталон
	//   `Item_UseSelectedAndSend@0xD0A060` делает `memset(useItemPacket, 0, 5u)`
	//   (`item_inventory.c:38708`) и пишет `g_nTargetUserIdx` в `useItemPacket[3]` ТОЛЬКО в ветке
	//   `itemSubType == 15` (`:38786`). Прежнее −1 уходило как `0xFFFF` — отличие от оригинала
	//   в теле пакета (сервер трактует это поле как i16 userNum).
	const int32 Target = (Rec && Rec->ItemSubType == 15) ? TargetUserIdx : 0;
	// ★ 09.10.2026 (автолопата): эталон для 11/34 после гейтов 2040/9002/9001 запоминает слот и контейнер инструмента в таймере
	//   (`sub_935458(g_pActionTimer, слот)` / `sub_9229AC(g_pActionTimer, контейнер)`, item_inventory.c:38829-38830 и :40838-40839) —
	//   по ним цикл повторяет применение (`inventory_SendUseItemRequest`). Запись безусловна (и при ключе цикла 0 — безвредна).
	if (Rec->ItemType == 11 && Rec->ItemSubType == 34)
	{
		AutoDig.Slot = CellIndex;
		AutoDig.Cont = ContainerId;
	}
	const bool bOk = Net->SendSpendItemUse5028(ContainerId, CellIndex, Target);
	if (bOk) { bActionPending = true; ActionSentAtS = FPlatformTime::Seconds(); }
	return bOk;
}

// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 16, баг 3 «игрок садится на транспорт во время боя»). Оба входа эталона в премиум-канал —
//   двойной щелчок по сумке (`CInventory_OnSlotUse@0xD07460`, AsdaStory_game.c:341566-341582) и панель быстрого доступа
//   (`Hotbar_SlotActivate@0xDEDDD0`, ветка `else if (Table2Record)`, ui_core.c:67403-67420) — перед
//   `CItemManager_UseInventoryItem` спрашивают `objStateAllowsRender(свой)` («персонаж занят/в бою»: состояние действия боя,
//   либо покой с живой целью атаки). Занят — проходят ТОЛЬКО подтипы `inventoryItemIsBoosterCategory@0xD1ED30`
//   (42, 43, 44, 45, 47, 52, 53 — зелья восстановления), остальное МОЛЧА: ни пакета, ни нотиса. Транспорт (подтип 15)
//   в бою не вызывается, как и «слезть» (тумблер стоит за этим же гейтом). Сервер (`Char_FunctionItem_Use@0x4ED780`,
//   case 0xF) боя не проверяет — только тип карты (код 23) и флаги 6001, поэтому гейт обязан стоять на клиенте.
//   Порт этот гейт пропустил. «Занят» — общая точка порта `UAsdaSystemOptionSubsystem::IsLocalPlayerBusy`.
//   Окно смерти (`UAsdaAfterDeathSubsystem`) зовёт `UseSpecialItemDispatch` напрямую — его гейт не касается (у эталона там
//   своя ветка). 0 — прежний порт (гейта нет).
static ASDA_DEV_CVAR(int32, CVarPremiumBusyGate,
	TEXT("asda.Mount.BusyGate"), 1,
	TEXT("[ОРИГИНАЛ 08.11.2026] 1 = в бою (objStateAllowsRender) премиум-предметы, кроме подтипов 42-45/47/52/53, не применяются "
	     "(транспорт не вызывается) — молча, как оригинал; 0 = прежний порт (без гейта)."),
	ECVF_Default);

// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (решение владельца «Да, нотис»): посадка/слезание с транспорта (подтип 15), отклонённые гейтом
//   «в бою», показывают loc 1157 «Не во время боя.» / «Cannot use during battle.». У оригинала здесь отказ МОЛЧА; сама строка
//   у оригинала выводится только таблицей отказов навыка `dword_15B5D78` (код 28) в `Recv_SkillCast` / `recvOtherCharSkillUse`
//   (network.c:44966 / :44637) — `CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)`, то есть плашка (золото, 10 с), на КАЖДЫЙ
//   отказ без подавления повторов. Канал и повтор взяты оттуда. Прочие премиум-предметы в бою и отказ по сидению/копанию
//   (7/8/9/40/43, автолопата — `itemUseDispatch`, своей строки нет) остаются молчаливыми. 0 — молча, как оригинал.
static ASDA_DEV_CVAR(int32, CVarMountBusyNotice,
	TEXT("asda.Mount.BusyNotice"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 08.11.2026] 1 = посадка/слезание с транспорта, отклонённые в бою, показывают плашку 1157 «Не во время боя.»; "
	     "0 = молча, как оригинал."),
	ECVF_Default);

// Включён ли ключ — для гейта состояния тумблера транспорта в `UsePremiumItem` (`AsdaInventorySubsystem_Net.cpp`).
bool AsdaPremiumBusyGateOn() { return CVarPremiumBusyGate.GetValueOnGameThread() != 0; }

// Общая точка гейта для двух входов (сумка — здесь, панель — `AsdaInventorySubsystem_View.cpp`). true — отказ.
bool AsdaPremiumUseBusyBlocked(UGameInstance* GI, int32 ItemCode)
{
	if (CVarPremiumBusyGate.GetValueOnGameThread() == 0 || !GI) { return false; }
	const UAsdaSystemOptionSubsystem* So = GI->GetSubsystem<UAsdaSystemOptionSubsystem>();
	if (!So || !So->IsLocalPlayerBusy()) { return false; }
	const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	FAsdaSpecialItemlistRow Sp;
	if (!IT || !IT->GetSpecialItem(ItemCode, Sp)) { return false; }   // не премиум — гейт не его (эталон: `inventoryItemIsBoosterCategory` → 0 только для Table2)
	switch (Sp.SubType)
	{
	case 42: case 43: case 44: case 45: case 47: case 52: case 53:
		return false;   // `inventoryItemIsBoosterCategory` = 1 — в бою можно
	default:
		break;
	}
	const bool bNotice = (Sp.SubType == 15) && CVarMountBusyNotice.GetValueOnGameThread() != 0;
	if (bNotice)
	{
		AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::ItemRefused, 1157, AsdaNoticeColor::Gold, 10000.f, 1157);
	}
	UE_LOG(LogAsdaInven, Log, TEXT("[премиум] предмет %d (подтип %d): персонаж в бою (objStateAllowsRender) — не применяется, %s"),
		ItemCode, Sp.SubType, bNotice ? TEXT("плашка 1157") : TEXT("молча (как оригинал)"));
	return true;
}

// ★ 31.10.2026: «двойной щелчок пришёл из окна сумки» (`CInventory_OnSlotUse@0xD07460`), а не с панели быстрого доступа.
//   Ставит `UseFromBagWindow` на время вызова, читает и гасит `UseByDoubleClick` (только игровой поток).
static bool GAsdaUseFromBagWindow = false;

// ★ 05.11.2026 [ОРИГИНАЛ] (баг игроков «несколько бустеров подряд — ячейки пустые до перезапуска», волна booster-loot):
//   эталон `CGameWindow_OnCommand` (ui_core.c:47996-48001), сообщение 0xD (двойной щелчок по ячейке сумки): `if (self[202])
//   { if (CObject_GetField254(inventory)) → 13106; if (CObject_GetField5581(g_pWarehouseWnd)) → 10309; CInventory_OnSlotUse }` —
//   пока открыт барабан бустера 10050 (поле +0x3F8 = `UAsdaBoosterPanelSubsystem::IsWindowOpen`), предмет из сумки НЕ применяется,
//   в чат строка 13106 «Ничего не выйдет, пока идет работа с бустером.». Порт ставил этот гейт только на захват и бросок (5/7),
//   а двойной щелчок по ВТОРОМУ бустеру снова звал `OpenWindow` — тот обнулял буфер 5469 первого (приз не ложился в сумку,
//   потраченный бустер оставался «призраком»). Гейт — общая точка `DragGateRefuses` (тот же нотис и канал). 0 — прежний порт.
static ASDA_DEV_CVAR(int32, CVarBagUseBoosterGate,
	TEXT("asda.Booster.BagUseGate"),
	1,
	TEXT("Двойной щелчок по предмету сумки при открытом барабане бустера 10050 (или активном свитке вызова хранилища): "
	     "1 = как оригинал — нотис 13106 (10309) в чат, предмет не применяется; 0 = прежний порт — применяется (второй бустер "
	     "перезапускал окно и терял ещё не применённый приз первого)."),
	ECVF_Default);

bool UAsdaInventorySubsystem::UseFromBagWindow(int32 ContainerId, int32 CellIndex)
{
	if (CVarBagUseBoosterGate.GetValueOnGameThread() != 0 && DragGateRefuses(TEXT("применение предмета двойным щелчком")))
	{
		return false;
	}
	// `CInventory_OnSlotUse@0xD07460`, ветка `m_nSelKind == 0` (расходники, item_inventory.c:37821-37856): ПЕРВЫМИ после
	//   гейта `sub_93723F(g_pUIManager)` стоят свитки ремесла. Разбор и отправка — у модели ремесла (одна точка).
	if (ContainerId == 2)
	{
		// Гейт `sub_93723F(g_pUIManager)` = `get_o15` (+84) — «ответ на действие с предметом ещё не пришёл» (ставят
		//   отправители 5000/5028 — `sub_922033(g_pUIManager, 1)`, снимает ответ); у порта это `bActionPending`. Эталон
		//   гасит им ВСЮ ветку расходников; здесь — ветки свитков (остальные развилки ниже живут своими гейтами, как было).
		if (bActionPending)
		{
			const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
			const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			const FAsdaItemRecord* Rec = (Src && IT) ? IT->GetItem(Src->ItemCode) : nullptr;
			if (Rec && (Rec->LinkCode == 101 || Rec->LinkCode == 102))
			{
				UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] свиток ремесла %d: ответ на прошлое действие с предметом ещё не пришёл (гейт sub_93723F) — молча"), Src->ItemCode);
				return false;
			}
		}
		if (UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr)
		{
			if (Craft->TryUseCraftScroll(ContainerId, CellIndex)) { return true; }
		}
		// ★ Рыбалка, волна В3 (08.10.2026): КНИГА-ПРЕДМЕТ (подтип 210) — `CInventory_OnSlotUse@0xD07460` (item_inventory.c,
		//   g:341273-341277): сразу после свитков ремесла и ДО материалов снасти 208/209 → `itemTooltipSetCountAndRefresh(
		//   g_pUpgradeWindow, запись)` — запомнить запись и показать диалог 79 «Зарегистрировать рыбу…» (111018), пакета нет.
		//   Ветка есть ТОЛЬКО здесь (расходники окна сумки); у хотбара (`itemUseDoubleClick@0xD12580`) её нет — книга с панели
		//   уходит обычным использованием, как у оригинала. Гейт `sub_93723F` эталон ставит перед всей веткой — молча.
		{
			const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
			const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			const FAsdaItemRecord* Rec = (Src && IT) ? IT->GetItem(Src->ItemCode) : nullptr;
			if (Rec && Rec->ItemSubType == 210)
			{
				if (bActionPending)
				{
					UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] книга рыбака %d: ответ на прошлое действие с предметом ещё не пришёл (гейт sub_93723F) — молча"), Src->ItemCode);
					return false;
				}
				if (UAsdaFishBookSubsystem* Book = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishBookSubsystem>() : nullptr)
				{
					return Book->OnUseBookItem(ContainerId, CellIndex);
				}
				return false;
			}
		}
	}
	// Признак «щелчок пришёл из окна сумки» для развилки камней ниже (у хотбара `itemUseDoubleClick@0xD12580` ветки нет).
	TGuardValue<bool> FromBagGuard(GAsdaUseFromBagWindow, true);
	return UseByDoubleClick(ContainerId, CellIndex);
}

bool UAsdaInventorySubsystem::UseByDoubleClick(int32 ContainerId, int32 CellIndex)
{
	// Читаем и сразу гасим признак окна сумки: вложенные вызовы (например, из развилок ниже) — уже не «из окна».
	const bool bFromBagWindow = GAsdaUseFromBagWindow;
	TGuardValue<bool> FromBagReset(GAsdaUseFromBagWindow, false);
	const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
	if (!Src || Src->IsEmpty()) { return false; }
	UGameInstance* GI = GetGameInstance();
	// ★★★ ЕДИНСТВЕННАЯ ТОЧКА ВХОДА ОКНА «Взять рыболовную снасть» (15900). Эталон разбирает эти
	//   ветки ПРЯМО ЗДЕСЬ, в двойном клике по ячейке (`item_inventory.c:37864-37872` — материал
	//   208/209 открывает окно; `:38118-38139` — удочка подтверждает, любой другой предмет снимает
	//   взвод). Логика окна — в `UAsdaFishingToolSubsystem`, здесь только развилка.
	if (UAsdaFishingToolSubsystem* Fish = GI ? GI->GetSubsystem<UAsdaFishingToolSubsystem>() : nullptr)
	{
		if (Fish->OnInventoryDoubleClick(ContainerId, CellIndex)) { return true; }
	}
	// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг игроков «камни улучшения не применяются»): КАМЕНЬ ПЕРЕНОСА УЛУЧШЕНИЯ.
	//   Эталон `CInventory_OnSlotUse@0xD07460`, ветка сумки (item_inventory.c:38124-38136) — сразу после удочки:
	//   камень 204/206 взводится (курсор P0), следующий двойной щелчок по вещи сумки открывает вопрос 110322, а любой
	//   другой — снимает взвод. Раньше порт отправлял камень общим использованием спец-предмета (5450), сервер его
	//   так не применяет — игрок не видел ничего. Логика и ключ отката `asda.UpgradeStone.Enable` — `UAsdaUpgradeStoneSubsystem`.
	if (ContainerId == 1 && bFromBagWindow)
	{
		if (UAsdaUpgradeStoneSubsystem* Stone = GI ? GI->GetSubsystem<UAsdaUpgradeStoneSubsystem>() : nullptr)
		{
			bool bStoneResult = false;
			if (Stone->OnBagDoubleClick(CellIndex, bActionPending, bStoneResult)) { return bStoneResult; }
		}
		// ★★★ 31.10.2026 [ПОРТ] (решение владельца): ПУСТОЙ КАМЕНЬ УЛУЧШЕНИЯ (special_itemlist 203/205) открывает окно
		//   «Разделение улучшения» 50500. В оригинале окно в файлах есть, но входа в него нет ни у одной функции клиента
		//   (шапка `UAsdaUpgradeSplitSubsystem.h`). Ключ `asda.UpgradeSplit.Enable 0` — прежний путь (общее использование).
		if (UAsdaUpgradeSplitSubsystem* Split = GI ? GI->GetSubsystem<UAsdaUpgradeSplitSubsystem>() : nullptr)
		{
			if (Split->OnBagDoubleClick(CellIndex)) { return true; }
		}
	}
	// ★★★ ВТОРАЯ РАЗВИЛКА ТОГО ЖЕ ДВОЙНОГО КЛИКА — «СВЯТОСТЬ» НА САМОЦВЕТ (5031).
	//   Эталон `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37905-37927) разбирает её здесь же:
	//   подтип 44…53 / 8 / 9 / 12 при `durationOrEnchant <= 0` ВЗВОДИТ режим (курсор «bottle»), а
	//   следующий клик по камню шлёт 5031. Логика — в `UAsdaSowelSubsystem`, здесь только развилка.
	if (ContainerId == 2)
	{
		if (UAsdaSowelSubsystem* Sowel = GI ? GI->GetSubsystem<UAsdaSowelSubsystem>() : nullptr)
		{
			if (Sowel->OnSpendCellUse(CellIndex)) { return true; }
		}
	}
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Src->ItemCode) : nullptr;
	if (ContainerId == 3)
	{
		return UnequipItem(CellIndex);   // двойной клик по надетому = СНЯТЬ
	}
	// ★★★ ВОЛНА 2, РАЗВИЛКА ТРЕТЬЯ — КОРОБКИ И ПАКЕТЫ (5 045 кодов).
	//   Эталон `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37873-37878 для сумки и :38139-38140
	//   для расходников) на `itemSubType ∈ {200, 201}` НЕ шлёт 5028, а открывает панель состава.
	//   Проверка стоит РАНЬШЕ ветки `itemType == 11`, потому что у коробок именно этот тип, и без
	//   неё они уходили в 5028, где сервер (`linkCode == 0`) отвечает отказом.
	if (Rec && (Rec->ItemSubType == 200 || Rec->ItemSubType == 201))
	{
		return OpenPackageItem(ContainerId, CellIndex);
	}
	// ★★★ ВОЛНА 3 ПИТОМЦЕВ, РАЗВИЛКА — ИНКУБАТОР (окно 42000). Эталон разбирает её ЗДЕСЬ ЖЕ,
	//   сразу после коробок: `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37881-37884) и
	//   `itemUseDoubleClick@0xD12580` (:40584-40588) на `itemRec->itemSubType == 107` зовут
	//   `petWindowOpen(g_pPetWindow, pSlotItem, nullptr)` — третий аргумент 0, то есть ОБЫЧНОЕ
	//   оформление инкубатора. Сам предмет 107 — это КАТАЛИЗАТОР: он не расходуется здесь, а
	//   запоминается окном (`pSrcSlotFeed`) и уходит первым предметом в пакет 6102.
	if (Rec && Rec->ItemSubType == 107)
	{
		if (UAsdaPetHatcherySubsystem* Hatch = GI ? GI->GetSubsystem<UAsdaPetHatcherySubsystem>() : nullptr)
		{
			return Hatch->OpenHatchery(ContainerId, CellIndex, /*bPremiumMood*/ false);
		}
		return false;
	}
	// ★★★ ВОЛНА 6 ПИТОМЦЕВ, РАЗВИЛКА — КОРМ (`itemSubType == 106`). Эталон разбирает её в ОБОИХ
	//   обработчиках использования, сразу после катализатора 107:
	//     • `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37885-37888);
	//     • `itemUseDoubleClick@0xD12580` (:40589-40592).
	//   Оба зовут `netSendItemPairRequest_Default(pSlotItem)` = `netSendItemPairRequest(g_PetData,
	//   слот, 0)` (network.c:26150-26154), то есть ПЕРВЫЙ путь 6108 с `useEnhanceSlot == 0`.
	//   ★ Гейт этого пути — ПРИЗВАННЫЙ питомец (`refineWnd[155]`, :25732), при его отсутствии
	//   пакет НЕ уходит и печатается нотис 101105 (:25860-25863). И гейт, и нотис живут в модели
	//   (`UAsdaPetSubsystem::RequestFeed(..., bFromWindowSlot = false)`) и в окне 43000 — здесь
	//   только развилка, второй копии правил не заводим (закон `check-existing-before-creating`).
	if (Rec && Rec->ItemSubType == 106)
	{
		if (UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
		{
			// ★ Сумку и ячейку берём ИЗ САМОЙ ЗАПИСИ (+4/+5), как эталон
			//   (`LOBYTE(payload[2]) = *(BYTE*)(itemSlot + 4)`, network.c:25753-25754), а не из
			//   аргументов клика: сервер ищет предмет ровно по этим полям.
			return Pet->RequestFeed(Src->ContainerType, Src->SlotIndex, /*bFromWindowSlot*/ false);
		}
		return false;
	}
	// ★★★ [ОРИГИНАЛ] 05.11.2026: гейт 7007 обработчика ПАНЕЛИ БЫСТРОГО ДОСТУПА `itemUseDoubleClick@0xD12580`
	//   (item_inventory.c:40594-40600) — сразу после 107/106: ограничивающее состояние и код вне 1…2000 → плашка 7007, дальше
	//   ничего. У окна сумки (`CInventory_OnSlotUse`) такого гейта перед надеванием нет — там он только в расходнике (№5).
	if (!bFromBagWindow && AsdaItemUseStateGate::RefuseIfStateLocked(GI, Src->ItemCode, TEXT("панель быстрого доступа (itemUseDoubleClick)")))
	{
		return false;
	}
	// ★★★ РАЗВИЛКА — ОБМЕН ПО ТАБЛИЦЕ СОБЫТИЙ (`itemSubType == 202`: праздничные жетоны, осколки,
	//   фрагменты и т. п.). ★ ИСПРАВЛЕНО 27.09.2026 (itemsfix, СРЕДН-3 ревизии): эталон ветки — ОДНА функция,
	//   обработчик окна сумки `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37711-40485), две её ветки:
	//     • расходники, контейнер 2 (`m_nSelKind == 0`) — item_inventory.c:37890-37896 (строка 37894);
	//     • сумка — :38171-38176 (строка 38173).
	//   Прежний комментарий приписывал вторую ветку `itemUseDoubleClick@0xD12580` — это НЕВЕРНО: у того
	//   (:40486-40964, обработчик панели быстрого доступа, его зовёт `Hotbar_SlotActivate`, ui_core.c:67396/
	//   67428) ветки 202 НЕТ, после 107/106 идёт обычное использование → 5028.
	//   `idx = objFindRecordIndexByField(код)` → при `idx >= 0` `npcExchangeBuyRequest(g_pNpcWnd, idx, −1)`
	//   (проверка пяти пар и 6098 с NpcId −1); в обоих случаях — «обработано», обычного
	//   использования (5028) НЕТ. Раньше порт отправлял такие предметы в 5028 как расходник,
	//   и обмен праздничных предметов не работал вовсе (`items_consistency.md`, 27.09.2026).
	//   ★★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] ПАНЕЛЬ БЫСТРОГО ДОСТУПА. Порт сводит оба эталонных обработчика
	//   в `UseByDoubleClick`, и хотбар (`AsdaHotbarSubsystem.cpp`, контейнер расходников) приходит СЮДА ЖЕ —
	//   то есть с панели 202 тоже открывает обмен. Оригинал с панели отправил бы 5028, а сервер его
	//   отвергает: `CharSpendItemUse` (GameServer.c:85820-86260) у всех 59 предметов 202 видит linkCode
	//   (+12, ток. 8 `item_table.txt`) = 0 и подтип (+27) вне своих веток → `v23 == 0` → 5029 код 0,
	//   предмет не тратится (сверено по живой таблице). Итог оригинала — пустой отказ; итог порта — тот
	//   же обмен, что по двойному клику в сумке, и под тем же сторожем сервера (ниже). Отступление
	//   безопасно: отдельной «хотбарной» логики нет, запрос тот же 6098 и только для строк, совпадающих
	//   с сервером. Вернуть поведение оригинала (5028 с панели) — не нужно: это был бы заведомо пустой
	//   пакет. [ПОРТ]
	//   ★ Гейт `sub_93723F(g_pUIManager)` («ответ на прошлое действие с предметом ещё не пришёл», у порта
	//   `bActionPending`) эталон ставит перед ВСЕЙ веткой, где живёт 202 (item_inventory.c:37823 и :38087),
	//   и перед обычным использованием в `itemUseDoubleClick` (:40563) — поэтому он стоит и здесь: молча, как эталон.
	if (Rec && Src->ItemCode > 0 && Rec->ItemSubType == 202)
	{
		if (bActionPending)
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] предмет %d (подтип 202): ответ на прошлое действие с предметом ещё не пришёл (гейт sub_93723F) — молча"), Src->ItemCode);
			return false;
		}
		// Сторож «строка обмена совпадает с сервером» — внутри `RequestEventExchange` (СРЕДН-4), одна точка
		//   и для кнопок NPC; при расхождении 6098 не уходит, игрок видит нотис 7502.
		if (UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr)
		{
			const int32 Row = Dlg->FindEventExchangeRowByItem(Src->ItemCode);
			if (Row >= 0) { Dlg->RequestEventExchange(Row, /*NpcId*/ -1); }
			else
			{
				UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] предмет %d (подтип 202): строки обмена в DT_EventItem нет — как эталон, ничего"), Src->ItemCode);
			}
		}
		return true;
	}
	// ★ 06.11.2026 [ОРИГИНАЛ] КАМЕНЬ ДУШИ: `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37901) → `inventoryTryEquipSelectedItem@0xD22B90`,
	//   в расходниках, при `durationOrEnchant <= 0`, после гейта `sub_93723F`. Проверки, окно 8157 и 5419 — `UAsdaSoulStoneSubsystem`.
	//   [ИСПРАВЛЕНИЕ] У панели быстрого доступа этой ветки в эталоне нет: она шлёт 5028, и сервер списывает активированный камень
	//   с накопленным опытом (`CharSpendItemUse`, ветка 32). Поэтому оба пути идут сюда. Ключ `asda.Soul.StoneBind 0` — прежнее поведение.
	if (AsdaSoulStoneOn() && Rec && Src->ItemCode > 0 && ContainerId == 2 && Rec->ItemSubType == 32 && Rec->DurationOrEnchant <= 0)
	{
		if (bActionPending) { return false; }
		UAsdaSoulStoneSubsystem* Stone = GI ? GI->GetSubsystem<UAsdaSoulStoneSubsystem>() : nullptr;
		if (Stone) { Stone->TryUseStone(ContainerId, CellIndex); }
		return true;
	}
	// ★★★ РАЗВИЛКА ЧЕТВЁРТАЯ — ГАЧА («бустер»): у предмета есть строка `booster_list`
	//   (эталон `itemBoosterWnd_Open@0xD1F4A0`: нет строки ⇒ обычное использование).
	if (Rec && IT && IT->IsBoosterItem(Src->ItemCode))
	{
		return UseGachaItem(ContainerId, CellIndex);
	}
	// Эталон `itemUseDoubleClick`: расходник (`ItemRecord->itemType == 11`) → 5028, иначе 5000 dstType=3.
	if (Rec && Rec->ItemType == 11) { return UseConsumable(ContainerId, CellIndex, -1); }
	// ★★★ РАЗВИЛКА ПЯТАЯ — ПРЕМИУМ/ФУНКЦИОНАЛЬНЫЙ ПРЕДМЕТ. Записи в `item_list` у него НЕТ вовсе
	//   (коды 1…558 — отдельное пространство), поэтому эталон уходит в `CItemManager_UseInventoryItem@0xA0FDF0`
	//   → `itemUseDispatch@0xA105A0`. Раньше порт в этом случае пытался НАДЕТЬ предмет пакетом 5000.
	if (!Rec && IT && IT->IsSpecialItemCode(Src->ItemCode))
	{
		// ★★★ ВОЛНА 3 ПИТОМЦЕВ: ПРЕМИУМ-КАТАЛИЗАТОР ИНКУБАТОРА. Эталон ловит его в премиум-ветке
		//   использования (`item_inventory.c:38113-38116`): `table2Rec->subType == 107` →
		//   `petWindowOpen(g_pPetWindow, pSlotItem, (_BYTE*)1)` — третий аргумент **1**, то есть
		//   ПРЕМИУМ-оформление окна (фон 42031, створки 42032/42033). Пакет 5450 при этом НЕ
		//   отправляется: предмет не «используется», он становится катализатором в окне.
		FAsdaSpecialItemlistRow SpRow;
		const bool bHaveSp = IT->GetSpecialItem(Src->ItemCode, SpRow);
		if (bHaveSp && SpRow.SubType == 107)
		{
			if (UAsdaPetHatcherySubsystem* Hatch = GI ? GI->GetSubsystem<UAsdaPetHatcherySubsystem>() : nullptr)
			{
				return Hatch->OpenHatchery(ContainerId, CellIndex, /*bPremiumMood*/ true);
			}
			return false;
		}
		// ★★★ ВОЛНА 6 ПИТОМЦЕВ: РАСШИРИТЕЛЬ ЯЧЕЕК ПИТОМЦЕВ (`special_itemlist.subType == 104`).
		//   Эталон ловит его в той же премиум-ветке использования, НА ОДНУ ПРОВЕРКУ ВЫШЕ
		//   катализатора 107: `if (table2Rec && table2Rec->subType == 104)` →
		//   `netSendVerifiedPacket6121(g_PetData, pSlotItem)` (item_inventory.c:38108-38112).
		//   Пакет 5450 при этом НЕ отправляется — предмет «используется» через свой опкод 6121.
		//   ★ ОТЛИЧИЕ ОТ 106/107, КОТОРОЕ НАДО ЗНАТЬ: у расширителя ветка есть ТОЛЬКО в
		//   `CInventory_OnSlotUse@0xD07460`; во втором обработчике `itemUseDoubleClick@0xD12580`
		//   (item_inventory.c:40485-40580) ветки `subType == 104` НЕТ вовсе — проверено сплошным
		//   поиском по телу функции. В порту оба эталонных обработчика сведены в один
		//   `UseByDoubleClick` (так же поступила волна 3 с премиум-катализатором 107), поэтому
		//   развилка стоит здесь и работает и по двойному клику. Это ОБЪЕДИНЕНИЕ двух эталонных
		//   обработчиков, а не выдумка: обе ветки взяты дословно, ни одна не добавлена.
		if (bHaveSp && SpRow.SubType == 104)
		{
			if (UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
			{
				// Сумка и ячейка — из записи (+4/+5), как эталон
				//   (`LOBYTE(payload[1]) = *(BYTE*)(srcCmd + 4)`, network.c:26188-26190).
				return Pet->RequestSlotUp(Src->ContainerType, Src->SlotIndex);
			}
			return false;
		}
		// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 16, баг 3 «сел на транспорт в бою»): гейт «персонаж занят» эталона — после веток 104/107,
		//   до `CItemManager_UseInventoryItem` (AsdaStory_game.c:341566-341582). Разбор у `AsdaPremiumUseBusyBlocked`.
		if (AsdaPremiumUseBusyBlocked(GI, Src->ItemCode)) { return false; }
		// ★ 07.10.2026 (волна 2-Г, ревизия С-1): развилка 38/64 → 1552 и `UsePremiumItem` — общая точка (и для панели).
		return UseSpecialItemDispatch(ContainerId, CellIndex);
	}
	return EquipItem(ContainerId, CellIndex);
}

// ★★★ 06-07.10.2026 (волна 2-Г): СВИТКИ ПЕРЕМЕЩЕНИЯ (`special_itemlist.SubType` 38) И ПРИЗЫВА (64). Эталон
//   `CItemManager_UseInventoryItem@0xA0FDF0` — `switch (subType − 35)`: case 3 (item_inventory.c:3042-3046) →
//   окно ввода имени 1552 вид 60 с loc 12832; case 29 (:3224-3228) → вид 59 с loc 104000. Пакета на этом шаге нет
//   (6134/5458 уходят кнопкой 1554). Окно, отправка и ответы 6135/5459/6136 — `UAsdaScrollCallSubsystem`.
bool UAsdaInventorySubsystem::UseSpecialItemDispatch(int32 ContainerId, int32 CellIndex)
{
	const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
	if (!Src || Src->IsEmpty()) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FAsdaSpecialItemlistRow SpRow;
	if (IT && IT->GetSpecialItem(Src->ItemCode, SpRow) && (SpRow.SubType == 38 || SpRow.SubType == 64))
	{
		if (UAsdaScrollCallSubsystem* Sc = GI ? GI->GetSubsystem<UAsdaScrollCallSubsystem>() : nullptr)
		{
			return Sc->OpenNameBox(ContainerId, CellIndex, SpRow.SubType);
		}
		return false;
	}
	// ★★★ 09.11.2026 (пачка 20 баг 8): ПРЕДМЕТ «СМЕНА ИМЕНИ ПЕРСОНАЖА» (`special_itemlist.SubType` 111, код 357). Тот же
	//   `switch (subType − 35)` эталона, case 76 (AsdaStory_game.c:75223) → вопрос (1, 65), окно 7900, 6138/6140 —
	//   `UAsdaCharRenameSubsystem`. Прежде предмет уходил в 5450: сервер (`Char_FunctionItem_Use`, ветка default) списывал его,
	//   имя не менялось. Откат: asda.Rename.Flow 0.
	if (IT && IT->GetSpecialItem(Src->ItemCode, SpRow) && SpRow.SubType == 111 && UAsdaCharRenameSubsystem::IsFlowOn())
	{
		if (UAsdaCharRenameSubsystem* Rn = GI ? GI->GetSubsystem<UAsdaCharRenameSubsystem>() : nullptr)
		{
			return Rn->BeginUse(ContainerId, CellIndex, Src->ItemCode);
		}
		return false;
	}
	// ★★★ 30.10.2026 (CampWar целиком): ПРЕДМЕТ «СБРОС АРМИИ» (`special_itemlist.SubType` 113). Тот же `switch (subType − 35)`
	//   эталона, case 78 (AsdaStory_game.c:75226-75237): запомнить charId/код/сумку/ячейку и TWOBUT 10141 «Сменить армию?»;
	//   «Да» (`case 0x279D`) → 6704; ответ 6705 — `UAsdaCampWarSubsystem` (сервер `User_Game_SquadReset` проверяет тот же подтип 113).
	if (IT && IT->GetSpecialItem(Src->ItemCode, SpRow) && SpRow.SubType == 113)
	{
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return false; }
		const int32 ItemCode = Src->ItemCode;
		const uint8 Bag = (uint8)Src->ContainerType;
		const int32 Cell = Src->SlotIndex;
		TWeakObjectPtr<UGameInstance> WGI(GI);
		WM->ShowGameConfirm(AsdaNotice::Loc(GI, 10141), [WGI, ItemCode, Bag, Cell](bool bYes)
		{
			UAsdaCampWarSubsystem* CW = (bYes && WGI.IsValid()) ? WGI->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr;
			if (CW) { CW->OnSquadResetItemConfirmed(ItemCode, Bag, Cell); }
		}, /*Gauge*/ 0.f, AsdaLoc::kBtnOk, 2289, /*EtalonNoticeId*/ 10141, /*EtalonKind*/ 1);
		return true;
	}
	// ★★★ 25.10.2026 (жалоба Player52): СВИТКИ СБРОСА НАВЫКОВ. Тот же `switch (subType − 35)` эталона
	//   (`CItemManager_UseInventoryItem@0xA0FDF0`, item_inventory.c:2934-2969):
	//   case 0/19/20 (подтипы 35/54/55: полный сброс 196…) → гейт «профессия» + вопрос 12866 → по «Да» 5450;
	//   case 1 (подтип 36: сброс ОДНОЙ ветки 197/470…) → гейт + окно выбора ветки 16500 → 12865 → 5450 с веткой.
	//   Прежде порт слал 5450 сразу: у подтипа 36 сервер сбрасывал ветку 0, свиток тратился впустую.
	//   Окно, вопросы и отправка — `UAsdaSkillTreeResetSubsystem`. Откат: asda.SkillReset.Dialogs 0.
	if (IT && IT->GetSpecialItem(Src->ItemCode, SpRow) && UAsdaSkillTreeResetSubsystem::HandlesSubType(SpRow.SubType)
		&& AsdaSkillResetDialogsOn())
	{
		if (UAsdaSkillTreeResetSubsystem* Tr = GI ? GI->GetSubsystem<UAsdaSkillTreeResetSubsystem>() : nullptr)
		{
			return Tr->BeginUse(ContainerId, CellIndex, Src->ItemCode, SpRow.SubType);
		}
		return false;
	}
	return UsePremiumItem(ContainerId, CellIndex, FString());
}

bool UAsdaInventorySubsystem::DestroyItem(int32 ContainerId, int32 CellIndex)
{
	// ★ [ОРИГИНАЛ] 26.10.2026: первая ветка корзины `sowelHandleSlotSelect@0xD14A90` (item_inventory.c:41249-41262) — предмет
	//   СУМКИ из special_itemlist с `subType == 15` при состоянии своего (15, 2) «на транспорте» не уничтожается: окно 2040
	//   «Покинь транспортное средство.» (`CItem_BuildTooltipText(ui, 0, −1, текст, 1)` = ONEBUT 1500). Ключ `asda.Inven.TrashVehicleGate`.
	if (ContainerId == 1 && CVarTrashVehicleGate.GetValueOnGameThread() != 0)
	{
		const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
		UGameInstance* GI = GetGameInstance();
		const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		FAsdaSpecialItemlistRow Sp;
		if (Src && !Src->IsEmpty() && IT && IT->GetSpecialItem(Src->ItemCode, Sp) && Sp.SubType == 15)
		{
			const UAsdaEffectsSubsystem* Eff = GI->GetSubsystem<UAsdaEffectsSubsystem>();
			if (Eff && Eff->FindByTypeSub(AsdaSelf::GetUserIdxOr(GI, INDEX_NONE), /*Id*/ 15, /*Type*/ 2))
			{
				UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] корзина: предмет %d (special subType 15) на транспорте — окно 2040 (эталон)"), Src->ItemCode);
				AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::ItemRefused, 2040, AsdaLoc::kBtnOk);
				return true;
			}
		}
	}
	// Корзина 405 и прежние вызывающие: вопрос 2005 (`sowelHandleSlotSelect`, item_inventory.c:41266/41287).
	return DestroyItemAsk(ContainerId, CellIndex, 2005);
}

bool UAsdaInventorySubsystem::DestroyItemAsk(int32 ContainerId, int32 CellIndex, int32 AskLocId)
{
	const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
	if (!Src || Src->IsEmpty()) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }

	const int32 Code = Src->ItemCode;
	const int32 Cont = ContainerId, Cell = CellIndex;
	// Qty <= 0 — весь стек (эталонная ветка без окна количества); Qty > 0 — ровно столько единиц.
	auto DoDestroy = [this, Cont, Cell](int32 Qty)
	{
		UGameInstance* G = GetGameInstance();
		UAsdaNetSubsystem* N = G ? G->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const FAsdaItemInstance* S = GetCell(Cont, Cell);
		if (!N || !S || S->IsEmpty()) { return; }
		// ★ [ОРИГИНАЛ] 26.10.2026: `netSendItemActionSecure@0xD14DE0` (network.c:18019-18022) — у предмета с `flagWord176`
		//   количество в 5006 ВСЕГДА 1, что бы ни лежало в +9 записи и что бы ни ввёл игрок. Ключ `asda.Inven.DumpFlag176One` 0 — прежнее.
		int32 SendQty = Qty;
		if (CVarDumpFlag176One.GetValueOnGameThread() != 0)
		{
			const UAsdaItemTableSubsystem* T = G->GetSubsystem<UAsdaItemTableSubsystem>();
			const FAsdaItemRecord* R = T ? T->GetItem(S->ItemCode) : nullptr;
			if (R && R->FlagWord176 != 0) { SendQty = 1; }
		}
		if (N->SendItemDump5006(S->Raw, SendQty))
		{
			bDumpPending = true; bActionPending = true; ActionSentAtS = FPlatformTime::Seconds();
			// ★ ЗВУК ВЫБРОСА (требование юзера 2026-08-14). В КЛИЕНТЕ ОРИГИНАЛА его нет: у корзины
			//   `sowelHandleSlotSelect@0xD14A90` звуковых вызовов нет вовсе (проверено телом функции),
			//   а «выбросить на землю» там не реализовано в принципе. Поэтому берём РОДНОЙ звук
			//   падения того же предмета — таблица `Sod_drop_*` (`CItemManager_InitDropSounds@0x9C4610`),
			//   которой уже озвучен наземный лут. Общая точка: `UAsdaWorldSubsystem::PlayItemDropSound`.
			if (UAsdaWorldSubsystem* WS = G->GetSubsystem<UAsdaWorldSubsystem>()) { WS->PlayItemDropSound(S->ItemCode); }
		}
	};

	// ★★★ ИСПРАВЛЕНО ПОНИМАНИЕ (прежний Д3 утверждал, что эталон уничтожает БЕЗ вопроса):
	//   эталон СПРАШИВАЕТ. `sowelHandleSlotSelect` вызывает `CItem_BuildTooltipText(g_pItemTooltipUI,
	//   1, 2005, …)` — это двухкнопочный msgbox 1523 с нотисом 2005; ОК-ветка
	//   (`ui_core.c:57060-57065`, контекст 0x7D5/0x80B/0x213B) зовёт `netSendItemActionSecure`,
	//   который и шлёт 5006. Наш диалог = ровно этот шаг. Ключ оставлен для сверки.
	if (CVarLegacyTrashNoConfirm.GetValueOnGameThread() != 0)
	{
		DoDestroy(-1);
		return true;
	}
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	FString Name = IT ? IT->GetItemName(Code) : FString();
	if (Name.IsEmpty() || Name == TEXT("-")) { Name = FString::Printf(TEXT("%d"), Code); }
	const int32 Cnt = Src->Amount;

	// Подтверждение (нотис 2005 корзины / 2059 броска вне окон) — общий хвост обеих веток: количество называем прямо в тексте.
	auto AskConfirm = [this, WM, Name, DoDestroy, AskLocId](int32 Qty, int32 Total)
	{
		// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): хвосты с именем — строки 990471 «%s» × %d (из %d)», 990472 «%s» × %d (весь стек)»,
		//   990505 «%s»» (кавычки — часть строки языка); перенос строки остаётся в коде.
		FString Msg = LocText(AskLocId);
		if (Qty > 0 && Qty < Total)
		{
			Msg += TEXT("\n") + UAsdaTextSubsystem::FormatPrintf(LocText(990471), { Name, FString::FromInt(Qty), FString::FromInt(Total) });
		}
		else if (Total > 1)
		{
			Msg += TEXT("\n") + UAsdaTextSubsystem::FormatPrintf(LocText(990472), { Name, FString::FromInt(Total) });
		}
		else
		{
			Msg += TEXT("\n") + UAsdaTextSubsystem::FormatPrintf(LocText(990505), { Name });
		}
		// Очередь: эталонная пара (1, 2005) — item_inventory.c:41266; (1, 2059) — item_inventory.c:40011/42782, qtyDialog :53529.
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Msg, [DoDestroy, Qty](bool bYes) { if (bYes) { DoDestroy(Qty); } }, 0.f, 2288, 2289, AskLocId, 1);
	};

	if (WM)
	{
		// ★★★ ОКНО ВВОДА КОЛИЧЕСТВА 1410 — эталон `sowelHandleSlotSelect@0xD14A90`
		//   (item_inventory.c:41283): режим 0, контекст **2**. Открывается НЕ всегда, а ровно при
		//   наборе условий (item_inventory.c:40014-40028 — тот же гейт, там контекст 1):
		//     • слот РАСХОДНИКОВ (эталонный `m_nSelKind == 0`; у нас это контейнер 2 — вкладка 0),
		//       для любого слота СУМКИ эталон окно НЕ показывает;
		//     • предмет не золото (20551) — эту ветку мы отсекли выше (`return false`);
		//     • код НЕ в диапазоне самоцветов (`g_SowelCodeRangeLo..Hi`);
		//     • в стеке БОЛЬШЕ ОДНОЙ единицы;
		//     • предмет торгуемый (`tradableFlag`) и без `flagWord176`.
		//   Иначе — сразу подтверждение на весь стек, как было.
		//   Порядок эталона: количество → ПОДТВЕРЖДЕНИЕ (2005) → пакет 5006. Не наоборот.
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
		const bool bAskQuantity =
			Cont == 2 && Cnt > 1 && Rec != nullptr &&
			Rec->TradableFlag != 0 && Rec->FlagWord176 == 0 &&
			!(IT && IT->IsSowelCode(Code));
		if (bAskQuantity)
		{
			UE_LOG(LogAsdaInven, Log,
				TEXT("[инвентарь] уничтожение (вопрос %d): стек %d × «%s» — спрашиваем количество (окно 1410, контекст %d)"),
				AskLocId, Cnt, *Name, AskLocId == 2059 ? 1 : 2);
			WM->ShowNumPad(Cnt, [AskConfirm, Cnt](bool bOk, int32 Qty)
			{
				if (!bOk) { return; }        // отмена/вне диапазона — эталон операцию не запускает
				AskConfirm(Qty, Cnt);
			});
			return true;
		}
		AskConfirm(-1, Cnt);
		return true;
	}
	DoDestroy(-1);
	return true;
}

// ★★★ КЛИК/БРОСОК НА КОРЗИНУ 405 — эталон `sowelHandleSlotSelect@0xD14A90` (item_inventory.c:41234).
//   Правила эталона, ВОСПРОИЗВЕДЕНЫ 1:1:
//     • выбран слот СУМКИ (m_nSelKind == 1)     → окно-подтверждение (нотис 2005) → 5006;
//     • выбран слот РАСХОДНИКОВ (m_nSelKind == 0):
//         – код 20551 (ЗОЛОТО) → `return 0`, корзина деньги НЕ трогает;
//         – стек > 1 у обычного тор­гуемого предмета (`!flagWord176`) → окно ввода количества
//           (`g_pNumPadDialog`) → 5006 с введённым числом;
//         – иначе → окно-подтверждение (нотис 2005) → 5006.
//   ⚠ ОКНА ВВОДА КОЛИЧЕСТВА В ПОРТЕ ЕЩЁ НЕТ (`uiOpenContextPopupWithHandlers@0xDA55A0`, отдельная
//   подсистема: им же пользуются магазин/склад/разделение стека). До его появления стек уничтожается
//   ЦЕЛИКОМ, и подтверждение честно называет количество.
bool UAsdaInventorySubsystem::DestroySelected()
{
	if (SelCell >= 0)
	{
		const int32 Cont = GetGridContainer();
		const FAsdaItemInstance* I = GetCell(Cont, SelCell);
		if (!I || I->IsEmpty()) { return false; }
		if (I->ItemCode == kGoldItemCode)
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] корзина: золото не уничтожается (эталон return 0)"));
			return false;
		}
		return DestroyItem(Cont, SelCell);
	}
	// Надетое корзина не принимает: эталон в этой ветке смотрит только слоты сетки.
	Notice(2004);
	return false;
}

bool UAsdaInventorySubsystem::DropItemToGround(int32 ContainerId, int32 CellIndex)
{
	const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
	if (!Src || Src->IsEmpty()) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	UE_LOG(LogAsdaInven, Warning,
		TEXT("[инвентарь] 5002 «выбросить»: в КЛИЕНТЕ ОРИГИНАЛА отправителя нет (дефект §8.3), "
		     "серверный обработчик CharItemDrop полный — шлём для проверки серверной ветки"));
	const int32 DropCode = Src->ItemCode;
	const bool bSent = Net->SendItemDrop5002(Src->Raw);
	// ★ Тот же звук падения, что у корзины и у наземного лута (см. комментарий в `DestroyItem`).
	if (bSent)
	{
		if (UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>()) { WS->PlayItemDropSound(DropCode); }
	}
	return bSent;
}

// ★★★ БРОСОК ВНЕ ВСЕХ ОКОН — эталон `inventoryWin_OnUiCommand@0xD17A80`, ветка `if (!a3)` (item_inventory.c:41656-41690).
//   `a3` — контрол под курсором в момент отпускания (сообщение 7, ui_core.c:47780-47802); nullptr = под курсором нет НИ ОДНОГО
//   контрола интерфейса, то есть предмет брошен в мир. Наш аналог «a3 != nullptr» — общий хит-тест `IsCursorOverAnyUiWindow`
//   (его же использует хотбар при броске наружу). Ветки эталона, воспроизведены 1:1:
//     • слот СУМКИ (`m_nSelKind == 1`, контейнер 1) → `invSlotCheckItemValid@0xD1E3C0` (item_inventory.c:42755):
//         запись пуста / тип контейнера (+4) ≠ 1 / записи item_list нет / байт +50 («привязан») ≠ 0 → 0 ⇒ окно **2004**
//         (`CItem_BuildTooltipText(ui, 0, 2004, …)` = ONEBUT 1500); иначе 2 ⇒ `invSlotHoverShowTooltip@0xD1E490` ⇒ вопрос
//         **2059** (`CItem_BuildTooltipText(ui, 1, 2059, …)` = TWOBUT 1523). Окна количества у ветки сумки нет.
//         (Ветка «1 ⇒ нотис 6951 про самоцвет» в теле есть, но `invSlotCheckItemValid` её не возвращает: цикл проверки
//          гнёзд у оригинала пустой — `for (i = 0; i < 4; ++i);`. Не добавляем.)
//     • слот РАСХОДНИКОВ (`m_nSelKind == 0`, контейнер 2) → `invSlotShowTooltipOrPopup@0xD0E000` (item_inventory.c:39999):
//         золото 20551 → `return 0` (ничего, иконка на место); стек > 1, не самоцвет, есть запись, `tradableFlag`,
//         `!flagWord176` ⇒ окно количества (режим 0, КОНТЕКСТ 1) → `qtyDialog_OnAmountConfirm` (item_inventory.c:53524) ⇒
//         вопрос 2059 с числом; иначе сразу вопрос 2059.
//     • прочие виды выбора (2/3 — гнёзда самоцветов) ветки `!a3` не имеют — ничего.
//   «Да» вопроса 2059 — `OnSendItemUpgrade` ветка 1523 / 0x80B → `netSendItemActionSecure` → **5006** (ui_core.c:57062-57065),
//   ровно как у корзины (0x7D5). Ответ — 5007 (`ApplyDumpResult`). «Нет» — предмет остаётся на месте.
//   ⚠ ВЫБРОСА НА ЗЕМЛЮ (5002) У ОРИГИНАЛА НЕТ вовсе — владелец 26.10.2026: «выбрасывать на землю НЕ надо». Сервер
//   (`CharItemDump@0x4D3410`) сам привязку не проверяет: для контейнера 2 отказывает только золоту, для контейнера 1 —
//   предметам вне диапазонов `sub_532B10`/`sub_532A90` (5007 result 0 ⇒ нотис 6956), занятому персонажу (result 2 ⇒ 8013).
bool UAsdaInventorySubsystem::DropOutsideWindows(int32 SrcContainer, int32 SrcCell)
{
	if (CVarDropOutsideDelete.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogAsdaInven, Verbose, TEXT("[инвентарь] бросок вне окон — ключ 0, прежнее поведение: предмет на место"));
		return false;
	}
	const FAsdaItemInstance* I = GetCell(SrcContainer, SrcCell);
	if (!I || I->IsEmpty()) { return false; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const int32 Code = I->ItemCode;
	if (SrcContainer == 1)
	{
		const bool bTypeOk = (I->Raw.IsValidIndex(4) ? I->Raw[4] : I->ContainerType) == 1;
		const bool bHaveRec = IT && IT->GetItem(Code) != nullptr;
		const bool bBound = I->Raw.IsValidIndex(50) && I->Raw[50] != 0;
		if (!bTypeOk || !bHaveRec || bBound)
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] бросок вне окон: предмет сумки %d (1:%d) удалить нельзя — %s ⇒ окно 2004 "
				"(эталон `invSlotCheckItemValid` = 0)"), Code, SrcCell,
				!bTypeOk ? TEXT("тип контейнера записи ≠ 1") : (!bHaveRec ? TEXT("записи item_list нет") : TEXT("привязан (+50 ≠ 0)")));
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::ItemRefused, 2004, AsdaLoc::kBtnOk, /*очередь: эталон (0, 2004)*/ 2004);
			return true;
		}
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] бросок вне окон: предмет сумки %d (1:%d) — вопрос 2059"), Code, SrcCell);
		return DestroyItemAsk(SrcContainer, SrcCell, 2059);
	}
	if (SrcContainer == 2)
	{
		if (Code == kGoldItemCode)
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] бросок вне окон: золото не уничтожается (эталон `invSlotShowTooltipOrPopup` return 0)"));
			return true;
		}
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] бросок вне окон: расходник %d ×%d (2:%d) — вопрос 2059"), Code, I->Amount, SrcCell);
		return DestroyItemAsk(SrcContainer, SrcCell, 2059);
	}
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] бросок вне окон: контейнер %d ветки `!a3` у эталона не имеет — ничего"), SrcContainer);
	return false;
}

// ★ Гейты сообщений 5 (захват) и 7 (бросок) окна инвентаря — `CGameWindow_OnCommand`, ui_core.c:47768-47800:
//   `if (CObject_GetField254(inventory)) → 13106; if (CObject_GetField5581(g_pWarehouseWnd)) → 10309` — строкой в чат
//   (`CChat_AddTokenizedLines(…, −11514, 10000, 0)`), действие не выполняется. Поле +0x3F8 — открыт барабан бустера 10050
//   (`UAsdaBoosterPanelSubsystem::IsWindowOpen`, разбор у гейта 4 диалога NPC); +0x5734 — активный свиток вызова хранилища
//   (`UAsdaWarehouseSubsystem::IsRecallActive`, там же гейт 5). Нотис — `Notice` (журнал чата, категория ItemRefused).
bool UAsdaInventorySubsystem::DragGateRefuses(const TCHAR* Where) const
{
	if (CVarDragGates.GetValueOnGameThread() == 0) { return false; }
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return false; }
	int32 Loc = 0;
	if (const UAsdaBoosterPanelSubsystem* Bo = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>())
	{
		if (Bo->IsWindowOpen()) { Loc = 13106; }
	}
	if (Loc == 0)
	{
		if (const UAsdaWarehouseSubsystem* Wh = GI->GetSubsystem<UAsdaWarehouseSubsystem>())
		{
			if (Wh->IsRecallActive()) { Loc = 10309; }
		}
	}
	if (Loc == 0) { return false; }
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] %s запрещён: %s ⇒ нотис %d (эталон `CGameWindow_OnCommand`)"), Where,
		Loc == 13106 ? TEXT("открыт барабан бустера 10050") : TEXT("активен свиток вызова хранилища"), Loc);
	Notice(Loc);
	return true;
}

void UAsdaInventorySubsystem::RequestWarehousePage(int32 PageNo)
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SendStoreHousePageReq4050(FMath::Max(1, PageNo));
	}
}
