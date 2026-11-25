// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHotbarSubsystem_Use.cpp — ★ R2 10.11.2026 (разрез AsdaHotbarSubsystem.cpp, дословный перенос): оружейный слот, откаты предметов/навыков, страницы с сервера, назначение/обмен и активация слотов.
//   Общее для частей — в AsdaHotbarSubsystemInternal.h; шапка и реверс — в AsdaHotbarSubsystem.cpp.
#include "UI/AsdaHotbarSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaSoulmateSubsystem.h"    // ★ 06.11.2026: пара и «Друг в игре» — из карточки (`asda.Soul.SlotTruth`)
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ 06.11.2026: перерисовка лотков действий при смене пары
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaItemCountStyle.h"   // ★ 26.10.2026: обводка числа стопки — общая точка (ключ asda.UI.ItemCountOutline)
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "UI/AsdaUiHelpers.h"   // ★ D2/D4: единые UI-хелперы (дедуп 2026-08-08)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"        // один общий тултип на HUD (эталон держит один g_ItemTooltip)
#include "UI/AsdaItemTooltipSubsystem.h"      // владелец общей плашки (порт g_ItemTooltip)
// ★ ТОЛЬКО ЗАГОЛОВОК, поведение не меняется: ниже (мост к окну навыков) уже используется
//   `UAsdaSkillWindowSubsystem`, но #include отсутствовал — тип «доезжал» через unity-сборку.
//   Как только этот файл попадает в отдельную единицу трансляции, компиляция падала.
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"     // сводная сверка шторок отката (`asda.Cooldown.Dump`)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ гейт `CObject_GetField254(g_pInventoryWnd)` = ОКНО ГАЧИ 10050
#include "UI/AsdaChatWindowSubsystem.h"    // ★ гейт `Input_IsTextInputFocused(g_pChatInputWnd)`
#include "UI/AsdaSkillDatabase.h"          // ★ Аудит №3 (A13): имя иконки навыка — только отсюда
#include "UI/AsdaAnimatedPlane.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — заголовки подсказок по id
#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaKeybindConfig.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"   // ★ 17.10.2026 GEngine->Exec(quit) в самотесте иконок эмоций
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ObjectKey.h"       // ★ 26.10.2026: TObjectKey — ревизия списка навыков по экземпляру панели
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт слотов и значков заявки 4/5
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» в `IsActivationBlocked`
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaHelpTopics.h"             // ★ 30.10.2026: справочные темы кнопок листания и пустого слота оружия
#include "UI/AsdaDeathInputLock.h"          // ★ 05.11.2026: меню смерти запирает слоты (эталон CUIManager+0x44, ui_core.c:67351)
#include "AsdaHotbarSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaHotbarSubsystem*.cpp

// ★★★ [ПОРТ / ИСПРАВЛЕНИЕ 05.11.2026] Баг игроков «маунт в панели быстрого доступа показывает чужую иконку, в части слотов —
//   правильную». Причина — дефект ПОРТА (у эталона флага нет: тип 1 всегда рисуется клеткой `texX/texY/size` записи,
//   ui_core.c:66445-66449). `RebuildOneSlot` сбрасывал имя/клетку/размер иконки, но НЕ флаг `bIconWholeTexture`, который
//   ставят только ветки навыка/эмоции. Слот, где раньше лежал навык или эмоция, после броска предмета (`SetSlotContent`)
//   или прихода страницы 5059 (перезапись без `Clear`) рисовал АТЛАС предмета «целиком» — левый-верхний квадрат, то есть
//   клетку 0 атласа: одна и та же чужая иконка во всех таких слотах. То же для пункта доп. меню (тип 5).
//   1 = флаг сбрасывается вместе с остальными полями иконки; 0 = прежнее поведение порта (флаг переживает смену типа).
// ★★★ [ИСПРАВЛЕНИЕ 07.11.2026] Баг владельца: «перенос предмета в сумке снимает его с панели или подменяет соседом». Дефект порта,
//   шаг оригинала был потерян: `itemMoveApply@0xD607C0` (item_inventory.c:49966, :49985) ДО применения ячеек перепривязывает слоты
//   панели — обмен `hotbarSetSlotsHighlight@0xDF1360`, переезд `hotbarSetSlotHighlightByKey@0xDF11C0` (имена IDA обманчивы: они пишут
//   контейнер и ячейку записи слота). Порт только перечитывал ячейку: пусто — слот снят, обмен — в слоте сосед.
//   Отличия от оригинала: реестр `CHotbar+6117` не нужен (идём по всем слотам), поэтому следуют и спецпредметы с `flag_100 != 1`, которых
//   оригинал в реестр не заносит; раскладка уходит 5067 сразу, а не только при уходе с карты (`hotbar_SendAllLayouts`).
//   1 = слот следует за предметом; 0 = прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarQuickFollowBagMove,
	TEXT("asda.Quick.FollowBagMove"),
	1,
	TEXT("[ИСПРАВЛЕНИЕ] Перенос предмета в сумке, расходниках, на тело и с тела (5001): 1 = слот панели следует за предметом; "
	     "0 = прежнее — слот остаётся на старой ячейке (снят, если она опустела, или показывает соседа)."),
	ECVF_Default);

// Эталон `hotbarUseActiveSlotItem@0xDF2670`: колонка 12 текущей страницы, только Type=1 SubType=1.
void UAsdaHotbarSubsystem::UseActiveWeaponSlot()
{
	const int32 Abs = kColsPerPage * CurrentPage + kWeaponCol;
	if (!Slots.IsValidIndex(Abs)) { return; }
	const FAsdaHotbarSlot& S = Slots[Abs];
	if (S.Type != (uint8)EAsdaHotbarSlotType::Item || S.SubType != (uint8)EAsdaHotbarContainer::Bag) { return; }
	if (Inventory.UseFromBag)
	{
		Inventory.UseFromBag(S.Index);
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] слот оружия страницы %d применён автоматически (ячейка сумки %d)"),
			CurrentPage + 1, S.Index);
	}
	else
	{
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] слот оружия страницы %d: автоприменение отложено — "
			"нет системы инвентаря (ячейка сумки %d, itemCode %d)"), CurrentPage + 1, S.Index, S.Id);
	}
}

// Эталон `hotbarSlotItemIsType9@0xDF2730`: в колонку 12 кладут ТОЛЬКО предмет с itemType == 9.
//   Пустой слот и «предметы второй таблицы» проходят/не проходят так же, как в оригинале.
bool UAsdaHotbarSubsystem::SlotItemIsWeapon(int32 AbsIndex) const
{
	if (!Slots.IsValidIndex(AbsIndex)) { return false; }
	const FAsdaHotbarSlot& S = Slots[AbsIndex];
	if (S.Type == 0) { return true; }                                        // пусто — разрешено
	if (S.Type != (uint8)EAsdaHotbarSlotType::Item) { return false; }
	// ★ [ИСПРАВЛЕНИЕ 07.11.2026] Эталон принимает и подтип 3 (надетое оружие): такая запись рождается, когда слот следует за предметом
	//   на тело (`CVarQuickFollowBagMove`). С ключом 0 — прежнее: только сумка.
	const bool bEquipped = (S.SubType == (uint8)EAsdaHotbarContainer::EquipSet) && CVarQuickFollowBagMove.GetValueOnGameThread() != 0;
	if (S.SubType != (uint8)EAsdaHotbarContainer::Bag && !bEquipped) { return false; }
	if (Inventory.IsBound())
	{
		const FAsdaHotbarItemEntry E = Inventory.GetEntry((int32)S.SubType, S.Index);
		return E.bValid && E.ItemType == 9;
	}
	// Инвентаря нет — тип берём из таблицы предметов по кэшированному itemCode.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>())
		{
			return IT->GetItemType(S.Id) == 9;
		}
	}
	return false;
}

// ══════════════════════════════ БЛОКИРОВКА / ВИДИМОСТЬ ═════════════════════════════════════════
void UAsdaHotbarSubsystem::SetPanelActive(bool bActive)
{
	bPanelActive = bActive;
	// Эталон: «активен» = затемняющие оверлеи СКРЫТЫ (uiHidePanelControls), «заблокирован» = показаны.
	for (const FAsdaHotbarSlotUi& U : MainUi) { SetVis(U.Dim, !bActive); }
	for (const FAsdaHotbarSlotUi& U : TUi)    { SetVis(U.Dim, !bActive); }
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] панель %s (оверлеи затемнения %s)"),
		bActive ? TEXT("активна") : TEXT("заблокирована"), bActive ? TEXT("сняты") : TEXT("показаны"));
}

void UAsdaHotbarSubsystem::SetSecondRowVisible(bool bVisible)
{
	bSecondRowVisible = bVisible;
	// Эталон `uiToggleChildPairVisible`: SetVisible(a2) контролам 73160 и 73195.
	SetVis(RowTBase, bVisible);
	SetVis(RowTAnchor, bVisible);
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] второй ряд (опция g_bOptChildPairVisible): %s"),
		bVisible ? TEXT("показан") : TEXT("скрыт"));
}

// ══════════════════════════════ ПЕРЕЗАРЯДКА ════════════════════════════════════════════════════
// ★★★ ВЗВОД ОТКАТА ПО ЗАПИСИ ПРЕДМЕТА — эталон `hotbarPushItemCooldown@0xE48FD0`.
//   Эталон сам лезет в `CItemManager_GetItemRecord`, сам проверяет гейт подтипа/linkCode и сам
//   читает `u16 @0xD0`; вызывающий (`OnRecv_UseItemResult`) передаёт только код+контейнер+ячейку.
//   Ровно это здесь и делается — источник длительности и группы у нас `UAsdaItemTableSubsystem`.
void UAsdaHotbarSubsystem::PushItemCooldown(int32 ItemCode, int32 ContainerType, int32 SlotIndex)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT)
	{
		UE_LOG(LogAsdaQuick, Warning,
			TEXT("[хотбар] откат предмета %d: таблица предметов недоступна — длительность взять неоткуда"),
			ItemCode);
		return;
	}
	int32 Group = 0, DurationMs = 0;
	if (!IT->GetItemCooldown(ItemCode, Group, DurationMs))
	{
		// Не дефект: у 11 027 расходников из 11 081 отката НЕТ ВООБЩЕ (ненулевой `u16 @0xD0` только
		//   у 54 записей таблицы). Эталон здесь просто выходит из функции.
		UE_LOG(LogAsdaQuick, Verbose, TEXT("[хотбар] предмет %d: отката в записи нет — шторку не ставим"), ItemCode);
		return;
	}
	PushItemCooldownRaw(ItemCode, ContainerType, SlotIndex, Group, DurationMs);
}

void UAsdaHotbarSubsystem::PushItemCooldownRaw(int32 ItemCode, int32 ContainerType, int32 SlotIndex,
                                               int32 Group, int32 DurationMs)
{
	if (ItemCode <= 0 || DurationMs <= 0) { return; }   // эталон: длительность 0 → кулдауна нет
	// ⚠ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (раньше нигде не было помечено).
	//   `hotbarPushItemCooldown@0xE48FD0` заводит запись отката ВНУТРИ цикла по 78 ячейкам панели и
	//   только для тех, где реально лежит предмет той же группы (бинарь `0xE490C5: cmp [ebp-0x50],0x4E`,
	//   `0xE490EE: cmp eax,1 / jne`). То есть если зелье НЕ вынесено на панель быстрого доступа,
	//   оригинальный клиент отката вообще не помнит, пред-проверка пропускает 5028, и отказ приходит
	//   от сервера кодом 3 (нотис 9565). Мы заводим запись ВСЕГДА — это строже оригинала и совпадает
	//   с серверной правдой (сервер ведёт откат по группе независимо от панели, `GameServer.c:85811-85820`).
	PruneItemCooldowns();
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)

	// Одна запись на (группу | код+контейнер): повторное применение ПЕРЕЗАПИСЫВАЕТ её, а не плодит
	//   вторую — в эталоне вектор тоже сперва ищет совпадение и обновляет найденное.
	FItemCooldownRec* R = nullptr;
	for (FItemCooldownRec& C : ItemCooldowns)
	{
		const bool bSameGroup = (Group != 0 && C.Group == Group);
		// −1 с любой стороны = «контейнер неизвестен» (запись пришла пакетом 5465) — не мешает слиянию.
		const bool bSameItem  = (C.ItemCode == ItemCode
			&& (C.Container < 0 || ContainerType < 0 || C.Container == ContainerType));
		if (bSameGroup || bSameItem) { R = &C; break; }
	}
	if (!R) { R = &ItemCooldowns.AddDefaulted_GetRef(); }
	R->ItemCode  = ItemCode;
	R->Container = ContainerType;
	R->SlotIndex = SlotIndex;
	R->Group     = Group;
	R->TotalS    = (double)DurationMs / 1000.0;
	R->EndS      = Now + R->TotalS;

	UE_LOG(LogAsdaQuick, Log,
		TEXT("[хотбар] откат предмета %d (контейнер %d, ячейка %d): %d мс, группа %d%s"),
		ItemCode, ContainerType, SlotIndex, DurationMs, Group,
		Group != 0 ? TEXT(" — гаснут ВСЕ слоты этой группы (правило сервера)") : TEXT(""));
	RefreshPage();
}

void UAsdaHotbarSubsystem::PruneItemCooldowns()
{
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)
	ItemCooldowns.RemoveAll([Now](const FItemCooldownRec& C) { return C.EndS <= Now; });
}

const UAsdaHotbarSubsystem::FItemCooldownRec* UAsdaHotbarSubsystem::FindItemCooldownRec(
	int32 ItemCode, int32 Container) const
{
	if (ItemCode <= 0) { return nullptr; }
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	// ★ ИСПРАВЛЕНО: группу надо брать из ТОЙ таблицы, где предмет лежит. `itemColorTypeMatch@0xDF67C0`
	//   (`item_inventory.c:55522-55552`) выбирает таблицу ПО ДИАПАЗОНУ КОДА, а не по удаче поиска:
	//       если код ∈ [g_ItemCodeFirst..g_ItemCodeLast]   → группа = `ItemRecord+0xCC`;
	//       иначе если ∈ [g_Table2CodeFirst..g_Table2CodeLast] → группа = `SpecialItemRecord+0x75`;
	//       иначе — совпадение безусловное (`return 1`).
	//   Сравнение ПЕРЕКРЁСТНОЕ: +0xCC и +0x75 — одно пространство номеров групп.
	//   Раньше группа искалась только в `item_list`, а премиум-коды (1…558) там отсутствуют, поэтому
	//   у них группа всегда выходила 0 и групповое гашение для них не работало вовсе.
	int32 Group = 0;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
	if (Rec)
	{
		Group = Rec->CooldownGroup;
	}
	else if (IT && IT->IsSpecialItemCode(ItemCode))
	{
		FAsdaSpecialItemlistRow Sp;
		if (IT->GetSpecialItem(ItemCode, Sp)) { Group = Sp.LinkGroupId; }
	}

	const FItemCooldownRec* Best = nullptr;
	for (const FItemCooldownRec& C : ItemCooldowns)
	{
		if (C.EndS <= Now) { continue; }                       // истекла — считается снятой
		// ★ Совпадение по ГРУППЕ — правило И СЕРВЕРА, И КЛИЕНТА ОРИГИНАЛА (прежде здесь было
		//   написано, что группа — правило только сервера, а клиент якобы гасит по коду+контейнеру;
		//   это ОПРОВЕРГНУТО телом `itemColorTypeMatch@0xDF67C0`, `item_inventory.c:55522-55552`:
		//   третий аргумент там не используется вообще, сравниваются только группы).
		//   Сервер: `sub_4DA990`/`sub_4DA920`, слот таблицы = НОМЕР ГРУППЫ (`CUser+0x6F + 7*группа`),
		//   и вызывается это лишь при ненулевой группе (`GameServer.c:85811`).
		//   Ветка по коду+контейнеру оставлена как страховка для записей с группой 0.
		const bool bSameGroup = (Group != 0 && C.Group == Group);
		// ★ Контейнер сравниваем, только если он известен У ОБЕИХ сторон: −1 у запроса означает
		//   «любой», −1 у ЗАПИСИ — «контейнер не пришёл» (откат, восстановленный пакетом 5465,
		//   где в записи 6 б есть только код). Без второй половины условия восстановленные с
		//   сервера откаты не находились НИ ОДНИМ слотом панели.
		const bool bSameItem  = (C.ItemCode == ItemCode
			&& (Container < 0 || C.Container < 0 || C.Container == Container));
		if (!bSameGroup && !bSameItem) { continue; }
		if (!Best || C.EndS > Best->EndS) { Best = &C; }       // показываем самый долгий из накрывших
	}
	return Best;
}

int32 UAsdaHotbarSubsystem::GetItemCooldownState(int32 ItemCode, int32 ContainerType, int32 SlotIndex) const
{
	// Эталон `lookupClassStatusByKey@0xE4A0C0`: записи нет ⇒ 3 (можно применять), иначе поле +20
	//   (0 → 0, 1 → 1, 2 → 2; гейт вызывающего — `<= 1`, то есть значение 2 проходит).
	//
	// ★★★ СНЯТО ЛОЖНОЕ УТВЕРЖДЕНИЕ (второй заход, 2026-08-13). Прежде здесь было написано, что
	//   «ни ячейка, ни контейнер в сравнении эталона НЕ участвуют, единственный критерий —
	//   `itemColorTypeMatch`». ЭТО НЕВЕРНО, и опровергается телом самой функции
	//   (`item_inventory.c:56742`):
	//       if ( rec+12 == a2 && rec+16 == a3 && rec+18 == a4 && *(WORD*)rec == 2 )
	//   — сравниваются ВСЕ ТРИ ключа: код (a2), контейнер (a3) и ячейка (a4), плюс тип записи 2.
	//   `itemColorTypeMatch@0xDF67C0` работает СОВСЕМ В ДРУГОМ МЕСТЕ — внутри
	//   `hotbarPushItemCooldown@0xE48FD0`, где решает, ПО КАКИМ СЛОТАМ ПАНЕЛИ размазать шторку
	//   (и там третий аргумент действительно не используется — это утверждение верно).
	//
	// ⚠ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ, ТЕПЕРЬ ЧЕСТНО ПОМЕЧЕНО: наш `FindItemCooldownRec` ищет по ГРУППЕ,
	//   то есть пред-проверка СТРОЖЕ оригинальной (эталон локально блокирует лишь тот же самый
	//   слот, а на «другое зелье той же группы» шлёт 5028 и получает от сервера отказ кодом 3).
	//   Для игрока разницы нет: и там и там виден нотис 9565 — меняется только источник отказа.
	//   Второй источник строгости описан в `PushItemCooldownRaw` (запись заводится всегда, а не
	//   только для предметов, вынесенных на панель).
	(void)SlotIndex;
	const FItemCooldownRec* R = FindItemCooldownRec(ItemCode, ContainerType);
	return R ? 1 : 3;
}

float UAsdaHotbarSubsystem::GetItemCooldownRemainSec(int32 ItemCode, int32 ContainerType, int32 SlotIndex) const
{
	(void)SlotIndex;
	const FItemCooldownRec* R = FindItemCooldownRec(ItemCode, ContainerType);
	if (!R) { return 0.f; }
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)
	return (float)FMath::Max(0.0, R->EndS - Now);
}

// ★★★ ВОССТАНОВЛЕНИЕ ОТКАТОВ ПРЕМИУМ-ПРЕДМЕТОВ С СЕРВЕРА (второй блок пакета 5465, смещение +180).
//   Порт второго цикла `Recv_SkillCoolTime@0xE4B180` (`network.c:28806-28832`):
//       for (j = 0; j < 30 && *(i32*)(a2 + 6*j + 180) != -1; ++j) {
//           rec = CItemManager_GetTable2Record(code);            // ТОЛЬКО таблица 2 (премиум)
//           if (!rec) continue;                                  // эталон: `if (Table2Record)`
//           pct = (rec->cooldownBase - остаток*1000) / rec->cooldownBase * 100;
//           entry: kind=2, +12 itemCode, +20 state=1, +24 pct, +32 timeGetTime(), +36 cooldownBase;
//           vectorPushBack(hotbar+56, entry);                     // тот же реестр, что и у 5029
//           shopItemListPopulateColorVariants(...);               // размазать по слотам той же группы
//       }
//   ⚠ Поля +16 (контейнер) и +18 (ячейка) эталон здесь НЕ заполняет — в записи пакета их нет.
//   Поэтому кладём Container = −1 («любой»), см. `FindItemCooldownRec`.
//   ⚠ ЕДИНИЦЫ: в пакете остаток В СЕКУНДАХ, а `cooldownBase` эталон подставляет в поле длительности
//   КАК ЕСТЬ, то есть трактует его как МИЛЛИСЕКУНДЫ (та же трактовка, что в `hotbarPushItemCooldown`).
void UAsdaHotbarSubsystem::ApplyServerItemCooldowns(const TArray<uint8>& Block30x6)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT)
	{
		UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] 5465: блок откатов предметов пришёл, но таблицы предметов нет"));
		return;
	}
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)

	int32 Applied = 0;
	for (int32 Off = 0; Off + 6 <= Block30x6.Num() && Off < 30 * 6; Off += 6)
	{
		const int32 Code = AsdaBytes::RdI32(Block30x6.GetData() + Off);
		if (Code == -1) { break; }                       // эталонный терминатор `!= -1`
		const int32 RemainSec = AsdaBytes::RdI16(Block30x6.GetData() + Off + 4);
		FAsdaSpecialItemlistRow Sp;
		if (!IT->GetSpecialItem(Code, Sp)) { continue; } // эталон: `if (Table2Record)` — иначе пропуск
		if (Sp.CooldownBase <= 0 || RemainSec <= 0) { continue; }

		PruneItemCooldowns();
		FItemCooldownRec* R = nullptr;
		for (FItemCooldownRec& C : ItemCooldowns)
		{
			if (C.ItemCode == Code) { R = &C; break; }
		}
		if (!R) { R = &ItemCooldowns.AddDefaulted_GetRef(); }
		R->ItemCode  = Code;
		R->Container = -1;                               // эталон поле +16 такой записи не пишет
		R->SlotIndex = -1;                               // и +18 тоже
		R->Group     = Sp.LinkGroupId;
		R->TotalS    = (double)Sp.CooldownBase / 1000.0;
		R->EndS      = Now + (double)RemainSec;
		// Полная длительность не может быть меньше остатка — иначе доля пройденного уйдёт в минус
		//   и шторка стартует с кадра 0 при почти истёкшем откате (эталон делит на cooldownBase
		//   без защиты, у нас клампим: числа сервера и таблицы могут разойтись на округлении).
		R->TotalS    = FMath::Max(R->TotalS, (double)RemainSec);
		++Applied;
		UE_LOG(LogAsdaQuick, Log,
			TEXT("[хотбар] 5465 восстановлен откат премиум-предмета %d: остаток %d с из %d мс, группа %d"),
			Code, RemainSec, Sp.CooldownBase, Sp.LinkGroupId);
	}
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] 5465: откатов предметов восстановлено %d"), Applied);
	if (Applied > 0) { RefreshPage(); }
}

// ★ Приём 5452 `recvItemRemoveById@0xF34B30` — сервер сообщает, что откат предмета отпущен.
//   Эталон здесь снимает УЗЕЛ активного эффекта (`listRemoveNodeById@0xF25FB0`), а сам откат
//   гаснет по клиентскому таймеру. У нас это ещё и точка синхронизации с сервером: сервер —
//   авторитет по откату (отказ 5451 кодом 8 идёт из его таблицы `user+244801`).
void UAsdaHotbarSubsystem::ClearItemCooldownByCode(int32 ItemCode)
{
	if (ItemCode <= 0) { return; }
	const int32 Removed = ItemCooldowns.RemoveAll([ItemCode](const FItemCooldownRec& C)
		{ return C.ItemCode == ItemCode; });
	if (Removed > 0)
	{
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] 5452: откат предмета %d снят сервером (записей %d)"),
			ItemCode, Removed);
		RefreshPage();
	}
}

void UAsdaHotbarSubsystem::PushSkillCooldown(int32 SkillId, int32 DurationMs)
{
	if (SkillId <= 0 || DurationMs <= 0) { return; }
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)
	FCooldownRec R; R.TotalS = (double)DurationMs / 1000.0; R.EndS = Now + R.TotalS;
	SkillCooldowns.Add(SkillId, R);
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] перезарядка навыка %d: %d мс"), SkillId, DurationMs);
	RefreshPage();
}

void UAsdaHotbarSubsystem::ClearAllCooldowns()
{
	ItemCooldowns.Reset();
	SkillCooldowns.Reset();
	RefreshPage();
}

void UAsdaHotbarSubsystem::ClearSkillCooldowns()
{
	SkillCooldowns.Reset();
	RefreshPage();
}

// ★ Общий хвост обоих запросов: остаток+длительность → эталонная 20-байтовая структура состояния.
//   Вынесен, чтобы формула стартового кадра существовала в ОДНОМ экземпляре.
FAsdaHotbarCooldown UAsdaHotbarSubsystem::MakeCooldownState(double RemainS, double TotalS) const
{
	FAsdaHotbarCooldown Out;
	if (RemainS <= 0.0) { return Out; }
	Out.State = 1;
	Out.RemainMs = (float)(RemainS * 1000.0);
	// Текущий кадр шторки = доля пройденного (эталон отдаёт готовый iconId из реестра).
	const double Frac = (TotalS > 0.0) ? FMath::Clamp(1.0 - RemainS / TotalS, 0.0, 1.0) : 0.0;
	Out.IconFrame = (int32)(Frac * (double)(CurtainTotalFrames - 1));
	return Out;
}

FAsdaHotbarCooldown UAsdaHotbarSubsystem::QueryItemCooldown(int32 ItemCode, int32 Container) const
{
	const FItemCooldownRec* R = FindItemCooldownRec(ItemCode, Container);
	if (!R) { return FAsdaHotbarCooldown(); }
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)
	return MakeCooldownState(R->EndS - Now, R->TotalS);
}

// Локальный реестр перезарядок панели (живёт здесь, пока/если книга навыков мост не поставила).
FAsdaHotbarCooldown UAsdaHotbarSubsystem::LocalSkillCooldown(int32 Id) const
{
	const FCooldownRec* R = SkillCooldowns.Find(Id);
	if (!R) { return FAsdaHotbarCooldown(); }
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: единые часы реестра (ключ asda.Cooldown.RealClock)
	return MakeCooldownState(R->EndS - Now, R->TotalS);
}

FAsdaHotbarCooldown UAsdaHotbarSubsystem::QuerySkillCooldown(int32 SkillId) const
{
	// Книга навыков — авторитет по кулдаунам навыков (эталон: реестр в `CSkillWnd`, вектор +56).
	//   Она же и спрашивает нас — через этот метод, — а мы возвращаемся к ней через мост: лямбда
	//   моста читает свою `TMap` НАПРЯМУЮ, рекурсии нет. Так вход в реестр один на все окна.
	if (Skills.GetCooldown) { return Skills.GetCooldown(SkillId); }
	return LocalSkillCooldown(SkillId);
}

FAsdaHotbarCooldown UAsdaHotbarSubsystem::FindCooldown(const FAsdaHotbarSlot& S) const
{
	if (S.Type == 3 || S.Type == 4) { return QuerySkillCooldown(S.Id); }

	if (S.Type == (int32)EAsdaHotbarSlotType::Item)
	{
		// ★★★ ПОЛНЫЙ код предмета слота. `S.Id` хранится 16-битным (столько отведено в записи слота
		//   и в пакете 5059/5067), а коды `item_list` доходят до 79428 — по усечённому коду откат
		//   попадал бы не на тот предмет (тот же класс дефектов, что и «иконка самоцвета», см.
		//   `RebuildOneSlot`). Полный код есть у моста инвентаря — берём его.
		int32 FullCode = S.Id;
		if (Inventory.IsBound())
		{
			const FAsdaHotbarItemEntry E = Inventory.GetEntry((int32)S.SubType, S.Index);
			if (E.bValid && E.ItemCode > 0) { FullCode = E.ItemCode; }
		}
		return QueryItemCooldown(FullCode, (int32)S.SubType);
	}
	// Прочие типы слотов (эмоции и т.п.) — ЛОКАЛЬНЫЙ реестр по `Id`, ровно как было до выделения
	//   общих точек: мост книги навыков этой ветке никогда не задавался.
	return LocalSkillCooldown(S.Id);
}

// ══════════════════════════════ СЕТЬ ═══════════════════════════════════════════════════════════
// ★★★ КОЛБЭК ПАКЕТА. Закон no-reentrant-net-callbacks: тут НЕЛЬЗЯ трогать UI и рвать сеть —
//   реентрантность рушит DrainFrames. Копируем 130 байт в очередь и уходим; применит Tick.
void UAsdaHotbarSubsystem::OnHotbarPageReceived(uint8 Page, const TArray<uint8>& Raw130)
{
	FPendingPage P;
	P.Page = Page;
	P.Raw = Raw130;
	// ★ Аудит №3 (B10): замок и `Add` — внутри общей очереди.
	PendingPages.Push(MoveTemp(P));
}

void UAsdaHotbarSubsystem::ApplyPendingPages()
{
	TArray<FPendingPage> Batch;
	// ★ Аудит №3 (B10): `Drain` = прежнее «под замком забрать всё разом и очистить»;
	//   false ⇒ очередь пуста ⇒ ранний выход, как и раньше.
	if (!PendingPages.Drain(Batch)) { return; }
	for (const FPendingPage& P : Batch)
	{
		if (P.Page >= kPageCount || P.Raw.Num() < kColsPerPage * kWireSlotBytes)
		{
			UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] 5059: страница %u вне 0..5 или тело коротко (%d)"),
				P.Page, P.Raw.Num());
			continue;
		}
		// Эталон `Hotbar_LoadPage@0xDEA3D0`: pageBase = 13*page; 13 записей по 10 байт ПЕРЕЗАПИСЫВАЮТ
		//   локальные (без слияния), затем hotbarRebuildSlots + CHotbar_RefreshSlots.
		const int32 Base = kColsPerPage * (int32)P.Page;
		for (int32 I = 0; I < kColsPerPage; ++I)
		{
			const int32 Abs = Base + I;
			if (!Slots.IsValidIndex(Abs)) { continue; }
			const uint8* R = P.Raw.GetData() + I * kWireSlotBytes;
			FAsdaHotbarSlot& S = Slots[Abs];
			S.Type    = R[0];
			S.SubType = R[1];
			S.Index   = (int32)((uint16)R[2] | ((uint16)R[3] << 8));
			S.Count   = (int32)((uint32)R[4] | ((uint32)R[5] << 8) | ((uint32)R[6] << 16) | ((uint32)R[7] << 24));
			S.Id      = (int32)((uint16)R[8] | ((uint16)R[9] << 8));
			// Сервер пишет пустые слоты как index=0xFFFF, id=0xFFFF — приводим к «пусто».
			if (S.Type == 0) { S.Clear(); }
			SentSnapshot[Abs] = S;   // пришло с сервера ⇒ считаем сохранённым
			RebuildOneSlot(Abs);
		}
		// ★ ДИАГНОСТИКА (по требованию проверки): печатаем КАЖДЫЙ непустой слот пришедшей страницы —
		//   тип/подтип/ячейку/кол-во/id и то, какая иконка для него разрешилась. Так сразу видно,
		//   приходят ли вообще эмоции (тип 2) и навыки (типы 3/4) и есть ли для них текстура.
		int32 NonEmpty = 0;
		for (int32 I = 0; I < kColsPerPage; ++I)
		{
			const int32 Abs = Base + I;
			if (!Slots.IsValidIndex(Abs) || Slots[Abs].IsEmpty()) { continue; }
			++NonEmpty;
			const FAsdaHotbarSlot& S = Slots[Abs];
			static const TCHAR* kTypeName[6] = { TEXT("пусто"), TEXT("предмет"), TEXT("эмоция"),
				TEXT("навык"), TEXT("навык соулмейта"), TEXT("доп-меню") };
			UE_LOG(LogAsdaQuick, Log,
				TEXT("[хотбар] 5059 слот %-2d (стр.%u кол.%-2d): тип=%d (%s) подтип=%d ячейка=%d кол-во=%d id=%d → иконка '%s'%s"),
				Abs, P.Page, I, S.Type, (S.Type < 6 ? kTypeName[S.Type] : TEXT("?")),
				(int32)S.SubType, S.Index, S.Count, S.Id,
				S.IconAtlas.IsEmpty() ? TEXT("НЕТ") : *S.IconAtlas,
				S.bIconWholeTexture ? TEXT(" (текстура целиком)") : TEXT(""));
		}
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] 5059 применён: страница %u (слоты %d..%d), непустых %d"),
			P.Page, Base, Base + kColsPerPage - 1, NonEmpty);
	}
	RefreshPage();
}

// Эталон `Hotbar_SendLayout@0xDF2070`: одна страница на пакет; вызывается на КАЖДОЕ изменение.
// ★ Все страницы разом — эталонный `hotbar_SendAllLayouts` перед любым переходом между картами
//   и каналами. Страницу, не менявшуюся с прошлой отправки, SendPageLayout пропускает сам.
void UAsdaHotbarSubsystem::SendAllPageLayouts()
{
	for (int32 Page = 0; Page < kPageCount; ++Page) { SendPageLayout(Page); }
}

void UAsdaHotbarSubsystem::SendPageLayout(int32 Page)
{
	if (Page < 0 || Page >= kPageCount) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	TArray<uint8> Wire;
	Wire.SetNumZeroed(kColsPerPage * kWireSlotBytes);
	const int32 Base = kColsPerPage * Page;
	bool bChanged = false;
	for (int32 I = 0; I < kColsPerPage; ++I)
	{
		const int32 Abs = Base + I;
		if (!Slots.IsValidIndex(Abs)) { continue; }
		const FAsdaHotbarSlot& S = Slots[Abs];
		uint8* R = Wire.GetData() + I * kWireSlotBytes;
		R[0] = S.Type;
		R[1] = S.SubType;
		const uint16 Idx = (uint16)S.Index;  R[2] = (uint8)(Idx & 0xFF); R[3] = (uint8)(Idx >> 8);
		const uint32 Cnt = (uint32)S.Count;  R[4] = (uint8)(Cnt & 0xFF); R[5] = (uint8)((Cnt >> 8) & 0xFF);
		                                     R[6] = (uint8)((Cnt >> 16) & 0xFF); R[7] = (uint8)((Cnt >> 24) & 0xFF);
		const uint16 Id  = (uint16)S.Id;     R[8] = (uint8)(Id & 0xFF);  R[9] = (uint8)(Id >> 8);
		if (!SentSnapshot[Abs].WireEquals(S)) { bChanged = true; }
	}
	if (!bChanged)
	{
		UE_LOG(LogAsdaQuick, Verbose, TEXT("[хотбар] страница %d не изменилась — 5067 не шлём"), Page);
		return;
	}
	// ★ ДОКАЗАТЕЛЬСТВО В ЛОГЕ: что именно уходит на сервер. Формат записи (обе стороны одинаковы,
	//   сверено с `Recv_DBQuickSlotInfo_Save@0x4DAD60` и `recvDBQuickSlotInfo@0x4DAB10`):
	//   [0]=тип, [1]=подтип, [2..3]=индекс ячейки u16, [4..7]=количество u32, [8..9]=itemCode u16.
	{
		FString Dump;
		for (int32 I = 0; I < kColsPerPage; ++I)
		{
			const uint8* R = Wire.GetData() + I * kWireSlotBytes;
			Dump += FString::Printf(TEXT(" %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X"),
				R[0], R[1], R[2], R[3], R[4], R[5], R[6], R[7], R[8], R[9]);
		}
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] →5067 страница %d, тело 131 б (page + 13×10):%s"), Page, *Dump);
	}
	if (!Net || !Net->SendHotbarLayout5067((uint8)Page, Wire))
	{
		// ★ Снимок обновляем ТОЛЬКО при удачной отправке: иначе несохранённая страница считалась бы
		//   сохранённой и следующее изменение соседнего слота уже не увезло бы её целиком.
		UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] 5067 не отправлен (нет соединения) — страница %d "
			"останется несохранённой до следующего изменения"), Page);
		return;
	}
	for (int32 I = 0; I < kColsPerPage; ++I)
	{
		const int32 Abs = Base + I;
		if (Slots.IsValidIndex(Abs)) { SentSnapshot[Abs] = Slots[Abs]; }
	}
}

// ══════════════════════════════ ИЗМЕНЕНИЕ СЛОТОВ ═══════════════════════════════════════════════
// Эталон `Hotbar_DragDrop_SetSlot@0xDE9DF0` — единая точка входа для инвентаря/книги навыков/эмоций.
bool UAsdaHotbarSubsystem::SetSlotContent(int32 AbsIndex, uint8 Type, uint8 SubType, int32 Index, int32 Count, int32 Id)
{
	if (!Slots.IsValidIndex(AbsIndex)) { return false; }
	// ★ Правило слота оружия: в колонку 12 любой страницы кладут только предмет с itemType == 9.
	const bool bWeaponSlot = (AbsIndex < kColsPerPage * kMainPages) && (AbsIndex % kColsPerPage == kWeaponCol);
	if (bWeaponSlot && Type == (uint8)EAsdaHotbarSlotType::Item)
	{
		FAsdaHotbarSlot Probe;
		Probe.Type = Type; Probe.SubType = SubType; Probe.Index = Index; Probe.Id = Id;
		const int32 Save = AbsIndex;
		FAsdaHotbarSlot Backup = Slots[Save];
		Slots[Save] = Probe;
		const bool bOk = SlotItemIsWeapon(Save);
		Slots[Save] = Backup;
		if (!bOk)
		{
			UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] слот оружия: предмет %d не itemType==9 — отказ"), Id);
			return false;
		}
	}
	FAsdaHotbarSlot& S = Slots[AbsIndex];
	S.Type = Type; S.SubType = SubType; S.Index = Index; S.Count = Count; S.Id = Id;
	RebuildOneSlot(AbsIndex);
	RefreshPage();
	SendPageLayout(AbsIndex / kColsPerPage);
	return true;
}

int32 UAsdaHotbarSubsystem::ColumnOfAbsSlot(int32 AbsIndex) const
{
	if (!Slots.IsValidIndex(AbsIndex)) { return -1; }
	const int32 MainCount = kColsPerPage * kMainPages;
	return (AbsIndex < MainCount) ? (AbsIndex % kColsPerPage) : (AbsIndex - MainCount);
}

// ★★★ Порт `Skill_SendLearnOrLevelUp@0xE55FB0` (диспетчер броска — `Skill_SendSkillReset@0xE55EA0`).
//   Запись слота у навыка: Type = 3, SubType = 0, Index = 0, Count = 0, Id = skillCode
//   (эталон: `memset(msg, 0, 10); msg[1] = 0; msg[4] = skillCode; LOBYTE(msg[0]) = 3;` — `msg` это
//   `_WORD[6]`, значит `msg[4]` = байтовое смещение +8, то есть то же поле, где у предмета itemCode).
bool UAsdaHotbarSubsystem::AssignSkillToSlot(int32 AbsIndex, int32 SkillCode)
{
	if (!Slots.IsValidIndex(AbsIndex))
	{
		UE_LOG(LogAsdaQuick, Log, TEXT("[навык→панель] отказ: слот %d вне диапазона 0..%d"), AbsIndex, Slots.Num() - 1);
		return false;
	}
	if (SkillCode <= 0)
	{
		UE_LOG(LogAsdaQuick, Log, TEXT("[навык→панель] отказ: код навыка %d"), SkillCode);
		return false;
	}
	// ⚠ Слот оружия навык не принимает — у эталона он просто не входит в диапазон idnum броска.
	if (ColumnOfAbsSlot(AbsIndex) == kWeaponCol)
	{
		UE_LOG(LogAsdaQuick, Log,
			TEXT("[навык→панель] отказ: слот %d — колонка оружия (диапазон броска навыка на неё не распространяется)"),
			AbsIndex);
		return false;
	}
	const bool bOk = SetSlotContent(AbsIndex, (uint8)EAsdaHotbarSlotType::Skill, 0, 0, 0, SkillCode);
	UE_LOG(LogAsdaQuick, Log, TEXT("[навык→панель] навык %d → слот %d (страница %d уходит пакетом 5067): %s"),
		SkillCode, AbsIndex, AbsIndex / kColsPerPage, bOk ? TEXT("OK") : TEXT("отказ"));
	return bOk;
}

bool UAsdaHotbarSubsystem::AssignSoulSkillToSlot(int32 AbsIndex, int32 SkillId)
{
	if (!Slots.IsValidIndex(AbsIndex) || SkillId <= 0 || ColumnOfAbsSlot(AbsIndex) == kWeaponCol)
	{
		UE_LOG(LogAsdaQuick, Log, TEXT("[навык Дружбы→панель] отказ: слот %d, навык %d"), AbsIndex, SkillId);
		return false;
	}
	const bool bOk = SetSlotContent(AbsIndex, (uint8)EAsdaHotbarSlotType::SkillAuto, 0, 0, 0, SkillId);
	UE_LOG(LogAsdaQuick, Log, TEXT("[навык Дружбы→панель] навык %d → слот %d: %s"), SkillId, AbsIndex, bOk ? TEXT("OK") : TEXT("отказ"));
	return bOk;
}

// ★★★ [ИСПРАВЛЕНИЕ 07.11.2026] Слот следует за предметом (разбор — у ключа `asda.Quick.FollowBagMove`). Один проход: запись слота либо
//   src, либо dst, поэтому обмен не нужен в два этапа. Только запись слота, без пересборки: зовут ДО перестановки ячеек, пересборка идёт
//   следом из `NotifyCell`. Сервер раскладку при переносе не трогает (`CharItemMove@0x4CE3E0`), её пишет только 5067.
void UAsdaHotbarSubsystem::FollowItemMove(int32 SrcSubType, int32 SrcCell, int32 DstSubType, int32 DstCell, bool bSwap)
{
	if (CVarQuickFollowBagMove.GetValueOnGameThread() == 0) { return; }
	// Склад (5) и журнал заданий (4) в запись слота не кодируются — такого переноса панель не касается.
	auto Addressable = [](int32 V) { return V >= 1 && V <= 3; };
	if (!Addressable(SrcSubType) || !Addressable(DstSubType)) { return; }
	uint32 PagesTouched = 0;
	for (int32 Abs = 0; Abs < Slots.Num(); ++Abs)
	{
		FAsdaHotbarSlot& S = Slots[Abs];
		if (S.Type != (uint8)EAsdaHotbarSlotType::Item) { continue; }
		if (S.SubType == (uint8)SrcSubType && S.Index == SrcCell)
		{
			S.SubType = (uint8)DstSubType; S.Index = DstCell;
		}
		else if (bSwap && S.SubType == (uint8)DstSubType && S.Index == DstCell)
		{
			S.SubType = (uint8)SrcSubType; S.Index = SrcCell;
		}
		else { continue; }
		PagesTouched |= 1u << (Abs / kColsPerPage);
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] слот %d следует за предметом %d: теперь контейнер %d, ячейка %d"),
			Abs, S.Id, (int32)S.SubType, S.Index);
	}
	for (int32 Page = 0; Page < kPageCount; ++Page)
	{
		if (PagesTouched & (1u << Page)) { SendPageLayout(Page); }
	}
}

// Эталон `hotbarSlotRemoveAndSync@0xDEA650`.
void UAsdaHotbarSubsystem::ClearSlot(int32 AbsIndex)
{
	if (!Slots.IsValidIndex(AbsIndex)) { return; }
	Slots[AbsIndex].Clear();
	HideSlotTooltip();
	RefreshPage();
	SendPageLayout(AbsIndex / kColsPerPage);
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] слот %d очищен (снят перетаскиванием наружу)"), AbsIndex);
}

// Эталон `CHotbar_SwapSlots@0xDEA470`: своп 10-байтовых записей + отправка ОБЕИХ затронутых страниц.
void UAsdaHotbarSubsystem::SwapSlots(int32 AbsA, int32 AbsB)
{
	if (!Slots.IsValidIndex(AbsA) || !Slots.IsValidIndex(AbsB) || AbsA == AbsB) { return; }
	// Правило слота оружия действует в ОБЕ стороны (эталон hotbarWnd_HandleSlotClick).
	const bool bAWeapon = (AbsA < kColsPerPage * kMainPages) && (AbsA % kColsPerPage == kWeaponCol);
	const bool bBWeapon = (AbsB < kColsPerPage * kMainPages) && (AbsB % kColsPerPage == kWeaponCol);
	if ((bAWeapon && !SlotItemIsWeapon(AbsB)) || (bBWeapon && !SlotItemIsWeapon(AbsA)))
	{
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] своп с колонкой оружия отклонён: предмет не itemType==9"));
		return;
	}
	Swap(Slots[AbsA], Slots[AbsB]);
	RebuildOneSlot(AbsA);
	RebuildOneSlot(AbsB);
	RefreshPage();
	const int32 PA = AbsA / kColsPerPage, PB = AbsB / kColsPerPage;
	SendPageLayout(PA);
	if (PB != PA) { SendPageLayout(PB); }
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] своп слотов %d ↔ %d (страницы %d/%d)"), AbsA, AbsB, PA, PB);
}

// ══════════════════════════════ ПРИМЕНЕНИЕ СЛОТА ═══════════════════════════════════════════════
// Эталон `Hotbar_SlotActivate@0xDEDDD0` (`ui_core.c:67351-67376`), блок общих блокировок.
//
// ★★★ ИСПРАВЛЕНО 2026-08-15 — ЗДЕСЬ СТОЯЛ ГЕЙТ «ОТКРЫТ ИНВЕНТАРЬ», КОТОРОГО В ОРИГИНАЛЕ НЕТ.
//   Живой лог порта (`Saved/Logs/AsdaReborn.log`, 2026-08-15 08:21:04):
//     «[хотбар] применение слота 3 отклонено: открыт инвентарь (нотис 13105)» —
//   то есть с открытым инвентарём панель не работала НИ мышью, НИ клавишами.
//   В эталоне гейт выглядит так: `if ( CObject_GetField254((CInventoryWindow *)g_pInventoryWnd) )`
//   → нотис 13105. Прежний порт прочитал `GetField254` как «инвентарь открыт». Это НЕ ТАК:
//   `CObject_GetField254@0x95CD10` возвращает dword по +0x3F8 объекта окна, а пишет туда
//   `set_oFE@0xD1F7D0` — тот самый флаг «открыто окно БАРАБАНА ГАЧИ» (JUI 10050 BOOSTING_WIN).
//   Доказано тремя независимыми путями:
//     1) `uiCloseAllWindowsOnLeave@0xD62790` (`ui_core.c:54704`):
//        `if ( CObject_GetField254(g_pInventoryWnd) ) tagBoosterShop_CloseWindow(g_pInventoryWnd);`
//        — по этому флагу закрывают ИМЕННО бустер-шоп;
//     2) соседние поля той же области пишет код гачи: `pBoosterWnd->gap_3F0[40]/[44] = 0`
//        (`item_inventory.c:43102-43103`, конец удачного прокрута);
//     3) наш же порт гачи держит это состояние под тем же именем —
//        `UAsdaBoosterPanelSubsystem::bWindowOpen` («dword 254 `set_oFE`»).
//   Обычные окна (инвентарь 400, книга навыков, карта 1710) панель в оригинале НЕ блокируют:
//   ни один из шести гейтов `Hotbar_SlotActivate` не завязан на их видимость (разбор всех шести —
//   в `Reports/hotbar_input_fix.md`).
bool UAsdaHotbarSubsystem::IsActivationBlocked(FString& OutWhy) const
{
	if (!bPanelActive) { OutWhy = TEXT("панель заблокирована (оверлеи затемнения показаны)"); return true; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM && WM->IsAnyModalOpen()) { OutWhy = TEXT("открыто модальное окно"); return true; }
	// ★ 05.11.2026 [ОРИГИНАЛ] (`asda.Death.InputLock`): меню смерти открыто — эталон `!sub_9323E3(g_pUIManager)` в той же
	//   первой цепочке `Hotbar_SlotActivate` (ui_core.c:67351, `CUIManager+0x44`): ни мышь, ни клавиша слот не применяют. Молча.
	if (AsdaDeathLock::IsInputLocked(GI)) { OutWhy = TEXT("открыто меню смерти (CUIManager+0x44, молча)"); return true; }
	// ★ Набор текста в чате (эталон `Input_IsTextInputFocused(g_pChatInputWnd)` — ПЕРВАЯ цепочка
	//   условий той же функции). Гейт стоит внутри `Hotbar_SlotActivate`, то есть в оригинале он
	//   гасит и клик мышью по слоту, а не только горячую клавишу. Молча, без нотиса.
	const UAsdaChatWindowSubsystem* ChatWnd = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
	if (ChatWnd && ChatWnd->IsInputActive()) { OutWhy = TEXT("активен ввод в чате"); return true; }
	// ★★ Волна В4 рыбалки (08.10.2026): сам `Input_IsTextInputFocused(g_pChatInputWnd)` эталона — МИСНЕЙМ: это
	//   `fishState != 0` (world_anim §5 п.5, вызов 0xDEDE24, game.c:427372). Пока идёт рыбалка, ни один слот (мышь и
	//   клавиша, предмет/навык/эмоция/пункт доп-меню) не срабатывает — МОЛЧА. Чат-гейт выше оставлен (он нужен порту).
	if (UAsdaFishingSubsystem::IsFishingNow(GI))   // ★ В3 S7 (N14): одна точка
	{
		OutWhy = TEXT("идёт рыбалка (fishState ≠ 0, молча, game.c:427372)"); return true;
	}
	// ★ Барабан гачи (эталон `CObject_GetField254(g_pInventoryWnd)` → нотис 13105).
	const UAsdaBoosterPanelSubsystem* Booster = GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;
	if (Booster && Booster->IsWindowOpen())
	{
		OutWhy = TEXT("открыт барабан гачи (нотис 13105)");
		return true;
	}
	// ★ Волна В3 ремесла (ревизия В0–В2 С-1): `Hotbar_SlotActivate@0xDEDDD0` (ui_core.c:67375) — в той же цепочке
	//   `!g_bPartyInviteFlag && !field254 && !tileInteract_isControlVisible(g_pItemMainWnd) && !isBoundControlVisible(
	//   g_pItemQtyMakeDlg)`: пока открыто окно ремесла 76000 или изготовления 77500, НИ ОДИН слот (предмет, навык,
	//   пункт доп-меню) не срабатывает — МОЛЧА, без нотиса.
	// ★ 29.09.2026 (волна 0 торговли): первое звено той же цепочки — `!g_bPartyInviteFlag` = ОКНО обмена 2008 закрыто
	//   (`CExchange+0x2C`, ui_core.c:67375). При открытом окне ни один слот не срабатывает — МОЛЧА. Общая точка
	//   `UAsdaTradeStateSubsystem` (пока окна 2008 нет — false).
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI))
	{
		OutWhy = TEXT("открыто окно обмена 2008 (молча, ui_core.c:67375)");
		return true;
	}
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		OutWhy = TEXT("открыто окно ремесла 76000/77500 (молча, ui_core.c:67375)");
		return true;
	}
	return false;
}

// ── Подписка на клавиши контроллера (делегаты динамические ⇒ UFUNCTION + AddDynamic) ──
void UAsdaHotbarSubsystem::EnsureKeyBinding()
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	AAsdaPlayerController* Pc = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (!Pc || BoundController.Get() == Pc) { return; }
	if (AAsdaPlayerController* Old = BoundController.Get())
	{
		Old->OnHotbarPage.RemoveDynamic(this, &UAsdaHotbarSubsystem::OnKeyHotbarPage);
		Old->OnHotbarSlot.RemoveDynamic(this, &UAsdaHotbarSubsystem::OnKeyHotbarSlot);
	}
	Pc->OnHotbarPage.AddDynamic(this, &UAsdaHotbarSubsystem::OnKeyHotbarPage);
	Pc->OnHotbarSlot.AddDynamic(this, &UAsdaHotbarSubsystem::OnKeyHotbarSlot);
	BoundController = Pc;
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] подписка на клавиши контроллера установлена (cmd 1..5 страницы, 6..29 слоты)"));
}

void UAsdaHotbarSubsystem::OnKeyHotbarPage(int32 PageIndex)
{
	SetPage(PageIndex);
}

void UAsdaHotbarSubsystem::OnKeyHotbarSlot(int32 Column)
{
	ActivateSlotByColumn(Column);
}

void UAsdaHotbarSubsystem::ActivateSlotByColumn(int32 KeyColumn)
{
	// Эталон `Hotbar_ActivateSlotByIndex@0xDEE650`: a3 < 12 → a3 + 13*page; a3 >= 12 → a3 + 53.
	if (KeyColumn < 0 || KeyColumn > 23) { return; }
	if (KeyLatch[KeyColumn] != 0) { return; }        // антидребезг (CHotbar+0x5FA0)
	KeyLatch[KeyColumn] = 1;
	const int32 Abs = (KeyColumn < 12) ? (KeyColumn + kColsPerPage * CurrentPage) : (KeyColumn + 53);
	ActivateSlot(Abs);
}

void UAsdaHotbarSubsystem::ReleaseSlotByColumn(int32 KeyColumn)
{
	if (KeyColumn < 0 || KeyColumn > 23) { return; }
	KeyLatch[KeyColumn] = 0;                          // эталон `objClearArraySlot@0xD78410`
}

void UAsdaHotbarSubsystem::ActivateSlot(int32 AbsIndex)
{
	// ★ Д4: эталон отбрасывает по условию `a3 > 0x4E` (78), хотя массив 0..77 — индекс 78 читал
	//   за краем. Чиним на строгую границу.
	if (AbsIndex < 0 || AbsIndex >= kSlotCount) { return; }
	FString Why;
	if (IsActivationBlocked(Why))
	{
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] применение слота %d отклонено: %s"), AbsIndex, *Why);
		return;
	}
	const FAsdaHotbarSlot& S = Slots[AbsIndex];
	if (S.IsEmpty()) { return; }

	switch ((EAsdaHotbarSlotType)S.Type)
	{
	case EAsdaHotbarSlotType::Item:
	{
		// ★★★ ИСПРАВЛЕНО ПРИ ПОВТОРНОЙ СВЕРКЕ (второй заход, 2026-08-13).
		//   Прежде здесь стоял ОДИН общий гейт «верхом нельзя» на весь тип «предмет», с
		//   исключением только для подтипа 2. Тело эталона `Hotbar_SlotActivate@0xDEDDD0`
		//   (`ui_core.c:67378-67425`) устроено иначе:
		//     • подтип 1 (СУМКА): гейт `LocalPlayer[1].bMounted → нотис 2040` стоит ВНУТРИ ветки
		//       `if (ItemRecord)`; ветка `else if (Table2Record)` (ПРЕМИУМ-канал) его НЕ имеет;
		//     • подтип 2 (расходники): гейта нет вовсе;
		//     • подтип 3 (надетое): `… == 3 && !LocalPlayer[1].bMounted` — МОЛЧА, без нотиса.
		//   Последствие старого кода было не косметическим: премиум-предмет транспорта
		//   (`itemUseDispatch@0xA105A0`, подтип 15) — это ТОГГЛ, и слезть с маунта кнопкой
		//   панели было нельзя: гейт срубал вызов до отправки 5455.
		//   Проверка вынесена в лямбду и зовётся точно там, где её зовёт эталон.
		auto IsMountedNow = [this]() -> bool
		{
			UGameInstance* GI = GetGameInstance();
			UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			AAsdaPlayerCharacter* PC = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor) : nullptr;
			return PC && PC->IsMounted();
		};
		if (!Inventory.IsBound())
		{
			UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] слот %d: применить предмет нельзя — системы инвентаря нет. "
				"Слот хранит (контейнер=%d, ячейка=%d, itemCode=%d); опкод 5000 соберётся сразу, как только "
				"инвентарь отдаст 57-байтовую запись ячейки"), AbsIndex, (int32)S.SubType, S.Index, S.Id);
			return;
		}
		switch ((EAsdaHotbarContainer)S.SubType)
		{
		case EAsdaHotbarContainer::Bag:
		{
			const FAsdaHotbarItemEntry E = Inventory.GetEntry(1, S.Index);
			// Гейт «верхом» — ТОЛЬКО для предметов со строкой `item_list` (ветка `if (ItemRecord)`).
			if (E.bHasItemRecord && IsMountedNow())
			{
				Notice(2040);
				return;
			}
			if (E.bValid && E.bHasItemRecord && E.ItemSubType == 34 && Inventory.UseByDoubleClick)
			{
				Inventory.UseByDoubleClick(S.Index, true);   // эталон: itemSubType == 34 → двойной клик
			}
			else if (Inventory.UseFromBag)
			{
				// Внутри моста: нет `item_list`, но есть `special_itemlist` ⇒ `CItemManager_UseInventoryItem` (эталонная ветка
				//   `else if (Table2Record)`): ★ 07.10.2026 (волна 2-Г, С-1) свитки 38/64 → окно 1552, прочее → 5450/5455;
				//   иначе 5000 «надеть».
				Inventory.UseFromBag(S.Index);
			}
			break;
		}
		case EAsdaHotbarContainer::Equipped:
			// Эталон (подтип 2 = контейнер расходников): гейта «верхом» здесь НЕТ.
			if (Inventory.UseByDoubleClick) { Inventory.UseByDoubleClick(S.Index, false); }
			break;
		case EAsdaHotbarContainer::EquipSet:
			// Эталон (подтип 3 = надетое): верхом просто НИЧЕГО не делает, БЕЗ нотиса.
			if (IsMountedNow()) { return; }
			if (Inventory.UseFromEquipSet) { Inventory.UseFromEquipSet(S.Index); }
			break;
		default:
			break;
		}
		break;
	}
	case EAsdaHotbarSlotType::Emotion:
		if (Misc.PlayEmotion) { Misc.PlayEmotion(S.Index); }
		else { UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] эмоция %d: окно эмоций не портировано"), S.Index); }
		break;
	case EAsdaHotbarSlotType::Skill:
	{
		// Эталон: глобальный анти-спам 200 мс (g_dwLastSkillDispatchTime).
		UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		const double Now = W ? W->GetTimeSeconds() : 0.0;
		if (LastSkillDispatchS > 0.0 && (Now - LastSkillDispatchS) * 1000.0 <= (double)SkillThrottleMs) { return; }
		LastSkillDispatchS = Now;
		// ★★★ ГЕЙТ ПО ФАКТИЧЕСКИ НУЖНОМУ КОЛБЭКУ, А НЕ ПО `IsBound()`.
		//   `FAsdaHotbarSkillBridge::IsBound()` проверяет ТОЛЬКО `GetSkillIconName` — то есть «мост
		//   отдаёт иконки». Окно навыков (`MakeHotbarBridge`) регистрирует ровно четыре поля —
		//   `GetSkillIconName` / `GetTargetKind` / `GetCooldown` / `ShowTooltip`, — а `UseCombatSkill`
		//   и `UseTargetSkill` остаются пустыми: боевого диспетчера навыков (эталон
		//   `combatHandleSkillUse@0xE4B960`, `skillUseOnTargetDispatch@0xE4E9F0`) в порту ещё нет.
		//   Старый код проходил `IsBound()`, брал `Kind` и молча выходил на `if (… && Skills.Use…)` —
		//   БЕЗ ЕДИНОЙ ЗАПИСИ В ЛОГ. Внешне это и есть «по предмету нажатие срабатывает, по навыку
		//   ничего не происходит»: у предмета мост инвентаря полный (`UseFromBag`/`UseByDoubleClick`/
		//   `UseFromEquipSet`), у навыка ветка обрывается ДО отправки.
		//   Правило `no-simplifications-stubs`: отсутствие подсистемы = честная запись, а не тишина.
		const int32 Kind = Skills.GetTargetKind ? Skills.GetTargetKind(S.Id) : 0;
		if (Kind == 2 && Skills.UseCombatSkill)      { Skills.UseCombatSkill(S.Id, S.Index); }
		else if (Kind == 1 && Skills.UseTargetSkill) { Skills.UseTargetSkill(S.Id); }
		else if (Kind == 0)
		{
			// Эталон `skillinfo_GetByteFieldAt42` вернул 0 (записи навыка нет) — тоже НИЧЕГО не делает.
			UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] навык %d: вид цели не определён (записи навыка нет "
				"в базе книги) — эталон в этом случае тоже ничего не отправляет"), S.Id);
		}
		else
		{
			UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] навык %d (вид цели %d): применить нельзя — мост книги "
				"навыков не отдал %s. Боевой диспетчер навыков (эталон combatHandleSkillUse@0xE4B960 / "
				"skillUseOnTargetDispatch@0xE4E9F0) в порт ещё не заведён; слот хранит данные и уходит на "
				"сервер пакетом 5067 без потерь"),
				S.Id, Kind, Kind == 2 ? TEXT("UseCombatSkill") : TEXT("UseTargetSkill"));
		}
		break;
	}
	case EAsdaHotbarSlotType::SkillAuto:
		// ★ Тот же разбор, что и у Type 3: `IsBound()` здесь ничего не гарантирует — нужен `CastAuto`.
		//   Проверку соулмейта (нотис 8162) держим ПЕРЕД проверкой колбэка: у эталона она первая
		//   (`Hotbar_SlotActivate` case 4 — сперва `CClass_GetField142C` и отказ, потом `skillCastActivate`).
		// ★ 06.11.2026 [ИСПРАВЛЕНИЕ]: условие эталона целиком — `gap_878[47] == −1 || gap_8B4[6] != 1` (ключ `asda.Soul.SlotTruth`).
		if (AsdaSoulSlotTruthOn() ? !IsFriendInGame() : (Skills.HasSoulmate && !Skills.HasSoulmate()))
		{
			Notice(8162);
			return;
		}
		if (Skills.CastAuto) { Skills.CastAuto(S.Id); }
		else
		{
			UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] навык соулмейта %d: применить нельзя — мост книги "
				"навыков не отдал CastAuto (эталон skillCastActivate@0xE4D900); "
				"проверка наличия соулмейта %s"),
				S.Id, Skills.HasSoulmate ? TEXT("выполнена") : TEXT("НЕ выполнена — HasSoulmate тоже пуст"));
		}
		break;
	case EAsdaHotbarSlotType::ExtraMenu:
		if (Misc.OpenExtraMenuItem) { Misc.OpenExtraMenuItem((int32)S.SubType, S.Index); }
		else { UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] пункт доп-меню %d: окно доп-меню не портировано"), S.Index); }
		break;
	default:
		break;
	}
}
