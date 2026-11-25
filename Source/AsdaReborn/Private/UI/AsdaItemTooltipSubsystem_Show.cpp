// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTooltipSubsystem_Show.cpp — ★ R2 10.11.2026 (разрез AsdaItemTooltipSubsystem.cpp, дословный перенос): показ подсказок: главная панель, наведение, слоты (превью/штамп/гача/магазин/квест), сравнение.
//   Общее для частей — в AsdaItemTooltipSubsystemInternal.h; шапка и реверс — в AsdaItemTooltipSubsystem.cpp.
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaItemTipStandalone.h"   // ★ [ПОРТ] 29.10.2026: «отдельный комплект» просмотрщика предметов редактора
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)

#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"   // ★ U17: синтетическая запись предмета (57 б) одной точкой
#include "UI/AsdaNpcShopSubsystem.h"         // потолок цены продажи сервера (`ServerSellUnitCap`) — одна точка
#include "UI/AsdaSowelSubsystem.h"           // источник события «нет свободных гнёзд» (SOWELL_SELBOX)
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaMarkup.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaTextureSubsystem.h"   // атлас иконки инфо-панели (423)
#include "Text/AsdaTextSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/DataTable.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"              // кнопки окна SOWELL_SELBOX (1532/1533)
#include "Components/ProgressBar.h"
#include "Components/CanvasPanelSlot.h"     // эталонные сдвиги контролов инфо-панели
#include "Components/TextWidgetTypes.h"     // UTextLayoutWidget — чтение Justification рефлексией
#include "UObject/UnrealType.h"             // FByteProperty
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"              // ★ Д3 круга 5: подбор кегля под ширину контрола панели
#include "HAL/IConsoleManager.h"
#include "UObject/Class.h"        // StaticEnum<EAsdaTipOwner> для диагностических строк
#include "Text/AsdaLang.h"         // [ПОРТ] 09.10.2026 (мультиязычность): формат параметра на языке сессии
#include "AsdaItemTooltipSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaItemTooltipSubsystem*.cpp

// ★ ДЕФЕКТ ОРИГИНАЛА Д5 (ИСПРАВЛЕН). `CItemManager_InitDropColors@0x9C4510` инициализирует ТОЛЬКО
//   индексы 0..8, а `CItemManager_GetDropColor@0x9C45C0` пропускает индекс 9 — то есть у грейда 9
//   цвет читается из НЕинициализированной памяти. Мы отдаём элемент 0 (белый): воспроизводить
//   мусор нельзя. Флага отката нет — «мусорный цвет» невоспроизводим по определению.
// ★ ДИАГНОСТИКА ТУЛТИПА. 1 — каждая попытка показа печатается как Display (длины текста, класс и
//   размер виджета, видимость, позиция, владелец). Нужна была, чтобы поймать «тултип не работает»
//   без единой строки в логе; оставлена как штатный инструмент.
static ASDA_DEV_CVAR(int32, CVarTipDebug,
	TEXT("asda.ItemTip.Debug"), 0,
	TEXT("1 — печатать в лог каждую попытку показа тултипа (текст, виджет, размер, видимость, владелец)"),
	ECVF_Default);

// ★ 25.10.2026 [ПОРТ]: подсказка карточки премиум-магазина / магазина бустеров для кодов второй таблицы (special_itemlist) —
//   веткой второй таблицы `ItemTooltip_Build`, как у предмета в сумке (см. ShowShopSlotTooltip). 0 — как прежде (не показывалась).
static ASDA_DEV_CVAR(int32, CVarShopCardTable2,
	TEXT("asda.ItemTip.ShopCardTable2"), 1,
	TEXT("1 — подсказка карточки премиум-магазина и магазина бустеров показывается и для функциональных предметов (special_itemlist, ")
	TEXT("свитки сброса навыков 196/197 и т. п.); 0 — как прежде: для них подсказки нет."),
	ECVF_Default);

// Аксессор ключа `asda.ItemTip.ShopCardTable2` (имя ключа в рабочем коде не повторяем — Shipping).
static bool AsdaItemTipShopCardTable2On()
{
	return CVarShopCardTable2.GetValueOnGameThread() != 0;
}

// ★★★ ОТКАТ ГЕЙТА ОКНА СРАВНЕНИЯ ПО ПАЙПЛАЙНУ.
//   `canCompareEquipped` живёт ТОЛЬКО внутри `ItemTooltip_Build@0xEA5260` и только в ветке
//   обычного предмета (item_inventory.c:60451-60469): простой билдер
//   `CItem_BuildSowelTooltipText@0xEA2EC0` и ветка записей ВТОРОЙ таблицы окно 5050 не открывают
//   ВООБЩЕ. Порт считал сравнение после ЛЮБОГО билдера — отсюда «слева коротко, справа 18 секций».
static ASDA_DEV_CVAR(int32, CVarLegacyCompareAnyPipeline,
	TEXT("asda.ItemTip.LegacyCompareAnyPipeline"), 0,
	TEXT("1 — окно сравнения показывать при любом пайплайне (как до правки); 0 — только полный билдер"),
	ECVF_Default);

// ── ассеты окон (генератор Tools/asda_jui_generic_umg.py) ────────────────────────────────────────
static const TCHAR* kTipWbp     = TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_ITEM_TIPWIN.WBP_ITEM_TIPWIN_C");
static const TCHAR* kCompareWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_ItemCompareTip_Win.WBP_ItemCompareTip_Win_C");

static const FName  kCompareWindowId(TEXT("ItemCompareTip_Win"));

static constexpr int32 kCompareZ = 9099;   // эталон рисует его СПРАВА, вплотную; z чуть ниже основного

static constexpr uint32 kColTitleDef  = 0xFFF4D9A4;  // дефолтный цвет заголовка ДО грейда (titleDefColor)

// ═══════════════════ показ ═══════════════════
UAsdaTipPanel* UAsdaItemTooltipSubsystem::GetMainPanel()
{
	// ★★★ `Bind` ЗОВЁТСЯ КАЖДЫЙ РАЗ, а не только при создании объекта.
	//   Причина: `UAsdaWindowSubsystem::CloseAll()` (порт `uiCloseAllWindows`) выбрасывает плашку
	//   из вьюпорта и из реестра окон — например, при входе в сцену премиум-гачи. Внутри `Bind`
	//   стоит проверка «окно ещё живо»: пока живо — это дешёвый ранний возврат, а после `CloseAll`
	//   окно открывается заново и все узлы перепривязываются. Без этого повторного вызова тултип
	//   после первого же `CloseAll` переставал показываться ВООБЩЕ, молча и без ошибок в логе.
	const bool bFirst = (MainPanel == nullptr);
	if (bFirst) { MainPanel = NewObject<UAsdaTipPanel>(this); }
	// Вторая/третья плашка окна и болты рамки к телу тултипа не относятся: эталон включает их
	//   отдельно (`itemTipWinSetModeVisible`) только у shell-предметов.
	const TArray<FString> Hide = { TEXT("ITEM_TIP_2"), TEXT("ITEM_TIP_3"), TEXT("ITEM_TIP_Premium"), TEXT("Wood_Bolt") };
	if (!MainPanel->Bind(GetGameInstance(), kTipWindowId, kTipWbp, kTipZ,
	                     TEXT("ITEM_TIPTITLE"), TEXT("ITEM_TIPCONTENS"), TEXT("ITEM_TIPWIN_0_"), Hide))
	{
		UE_LOG(LogAsdaItemTip, Warning, TEXT("[тултип] плашка не привязана (WBP_ITEM_TIPWIN не загрузился)"));
		return nullptr;
	}
	if (bFirst)
	{
		// Подвал «элитного» предмета: значок 5007 + плашка 5005 с текстом 5006 (нотис 109832).
		//   При ПЕРЕпривязке после `CloseAll` подвал восстанавливает сам `Bind` по сохранённым именам.
		MainPanel->BindFooter(TEXT("ITEM_TIP_Premium_icon"), TEXT("ITEM_TIP_3_"), TEXT("ITEM_TIP_3_Text"));
	}
	return MainPanel;
}

void UAsdaItemTooltipSubsystem::ShowTextTip(EAsdaTipOwner Owner, const FString& Title, const FString& Body,
                                            const FAsdaTipMetrics& Metrics)
{
	UAsdaTipPanel* P = GetMainPanel();
	if (!P)
	{
		UE_LOG(LogAsdaItemTip, Warning, TEXT("[тултип] показ текста отменён: плашка не привязана (WBP_ITEM_TIPWIN)"));
		return;
	}
	bCompareShown = false;
	if (ComparePanel) { ComparePanel->Hide(); }
	P->SetOwner(Owner);
	P->SetSourceWindow(TipSourceWindowUnderCursor(GetGameInstance(), P, ComparePanel));   // ★ 31.10.2026: окно-источник показа
	P->Show(Title, 0xFFFFFFFF, Body, Metrics);
	P->LayoutAtCursor();
	LogShow(TEXT("текст"), 0, Title, Body, P);
}

bool UAsdaItemTooltipSubsystem::HideTooltipFor(EAsdaTipOwner Owner)
{
	if (!MainPanel || !MainPanel->HideFor(Owner)) { return false; }
	if (ComparePanel) { ComparePanel->Hide(); }
	bCompareShown = false;
	return true;
}

// ★ Дедуп №2, В1-B (U18): шапка трекера подсказки окна — перенос построчно одинакового начала `TickTooltip` пяти окон.
UAsdaItemTooltipSubsystem* UAsdaItemTooltipSubsystem::BeginHoverTip(UGameInstance* GI, EAsdaTipOwner Owner,
	int32& CurKind, int32& CurIdx, int32 Kind, int32 Idx)
{
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return nullptr; }
	if (Kind == CurKind && Idx == CurIdx) { return nullptr; }
	CurKind = Kind; CurIdx = Idx;
	if (Kind < 0) { Tip->HideTooltipFor(Owner); return nullptr; }   // msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`
	return Tip;
}

UAsdaItemTooltipSubsystem* UAsdaItemTooltipSubsystem::BeginHoverTip(UGameInstance* GI, EAsdaTipOwner Owner, int32& CurId, int32 Id)
{
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return nullptr; }
	if (Id == CurId) { return nullptr; }
	CurId = Id;
	if (Id < 0) { Tip->HideTooltipFor(Owner); return nullptr; }   // msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`
	return Tip;
}

EAsdaTipOwner UAsdaItemTooltipSubsystem::GetTipOwner() const
{
	return MainPanel ? MainPanel->GetOwner() : EAsdaTipOwner::None;
}

FString UAsdaItemTooltipSubsystem::DebugTipState() const
{
	FString S = TEXT("[тултип] СОСТОЯНИЕ\n  основная плашка (ITEM_TIPWIN 5000): ");
	S += MainPanel ? MainPanel->DebugState() : TEXT("НЕ СОЗДАНА (ни один потребитель ещё не просил тултип)");
	S += TEXT("\n  плашка сравнения (ItemCompareTip_Win 5050): ");
	S += ComparePanel ? ComparePanel->DebugState() : TEXT("НЕ СОЗДАНА");
	S += FString::Printf(TEXT("\n  сравнение показано=%s · мост в инвентарь=%s · инфо-панель=%s"),
		bCompareShown ? TEXT("да") : TEXT("нет"),
		bBridgeBound ? TEXT("привязан") : TEXT("НЕТ"),
		bInfoBound ? TEXT("привязана") : TEXT("НЕТ"));
	// Д1 (КРУГ 6): видно, почему панель свёрнута — «наполнена=нет» и есть источник выбора.
	S += FString::Printf(TEXT("\n  правая панель: наполнена=%s · источник контейнер=%d ячейка=%d ряд=%d · свёртка отложена=%s"),
		bInfoPanelFilled ? TEXT("да") : TEXT("нет"),
		InfoPanelSrcCont, InfoPanelSrcCell, InfoPanelSrcRow,
		bPendingPanelCollapse ? TEXT("да") : TEXT("нет"));
	return S;
}

// Единая диагностическая строка показа (★ требование задачи: «наведение → текст длиной L →
//   виджет класса C, размер WxH, видим=да/нет»). По умолчанию Verbose, при `asda.ItemTip.Debug 1`
//   печатается на экран/в лог как Display — чтобы ловить проблему без пересборки.
void UAsdaItemTooltipSubsystem::LogShow(const TCHAR* Kind, int32 ItemCode, const FString& Title,
                                        const FString& Body, const UAsdaTipPanel* P) const
{
	const bool bLoud = CVarTipDebug.GetValueOnGameThread() != 0;
	const UEnum* E = StaticEnum<EAsdaTipOwner>();
	const FString Msg = FString::Printf(
		TEXT("[тултип] ПОКАЗ (%s код=%d): заголовок %d симв., тело %d симв. → %s | владелец=%s"),
		Kind, ItemCode, Title.Len(), Body.Len(),
		P ? *P->DebugState() : TEXT("плашки нет"),
		(P && E) ? *E->GetNameStringByValue((int64)P->GetOwner()) : TEXT("?"));
	if (bLoud) { UE_LOG(LogAsdaItemTip, Display, TEXT("%s"), *Msg); }
	else       { UE_LOG(LogAsdaItemTip, Verbose, TEXT("%s"), *Msg); }
}

bool UAsdaItemTooltipSubsystem::ResolveCompareTarget(const FAsdaItemInstance& It, FAsdaItemInstance& OutEquipped) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = Items();
	if (!Inv || !IT) { return false; }
	const FAsdaItemRecord* R = IT->GetItem(It.ItemCode);
	if (!R) { return false; }
	const int32 Slot = R->ItemType;                              // эталон: слот = ItemRecord->itemType
	if (Slot < 0 || Slot > 9) { return false; }
	const FAsdaItemInstance* Eq = Inv->GetCell(3, Slot);         // контейнер 3 = НАДЕТОЕ
	if (!Eq || Eq->IsEmpty()) { return false; }
	const FAsdaItemRecord* ER = IT->GetItem(Eq->ItemCode);
	bool bCan = true;
	if (ER)
	{
		if (R->ItemType != ER->ItemType) { bCan = false; }
		if (Slot == 9 && R->ItemSubType != ER->ItemSubType) { bCan = false; }
		// ★ Два кода принудительно включают сравнение (эталон, item_inventory.c:60465-60468).
		if (R->ItemCode == 22819 || R->ItemCode == 22818) { bCan = true; }
		if (ER->ItemCode == 22819 || ER->ItemCode == 22818) { bCan = true; }
	}
	if (!bCan) { return false; }
	OutEquipped = *Eq;
	return true;
}

void UAsdaItemTooltipSubsystem::ShowItemTooltip(const FAsdaItemInstance& It, bool bSuppressCompare,
                                                EAsdaTipOwner Owner, int32 SourceKind)
{
	UAsdaTipPanel* P = GetMainPanel();
	if (!P)
	{
		// ★ Раньше тут был немой `return` — «тултип не работает» и НИ ОДНОЙ строки в логе.
		UE_LOG(LogAsdaItemTip, Warning,
			TEXT("[тултип] предмет %d: плашка не привязана (WBP_ITEM_TIPWIN не загрузился) — показывать нечем"),
			It.ItemCode);
		return;
	}

	UAsdaItemTableSubsystem* IT = Items();
	const FAsdaItemRecord* R = IT ? IT->GetItem(It.ItemCode) : nullptr;

	// ════════ ВЫБОР ПАЙПЛАЙНА (★ГЛАВНОЕ этого захода) ═══════════════════════════════════════════
	//  Эталон гоняет тултип предмета ТРЕМЯ разными путями, и выбирает их НЕ по «типу оформления»:
	//   • вкладка «Снаряжение» (`g_InventoryBag`) → `ItemTooltip_Build@0xEA5260` — полные 18 секций
	//     (`invSlotBuildTooltip@0xD06670`, ветка `gap_1B0[84] == 1`, item_inventory.c:37411-37421);
	//   • вкладка «Расходники» (`g_InventoryArray2`) → `CControl_SetState` →
	//     `CItem_BuildSowelTooltipText@0xEA2EC0` — состав ЗАВИСИТ ОТ `itemType` (37425-37465);
	//   • надетый слот → по `itemType`: 5..7 и 10..11 в простой билдер, остальные — в полный
	//     (`equipSlotUpdateTooltip@0xC6AFB0`, item_inventory.c:22787-22808);
	//   • код в диапазоне `special_itemlist` → ветка ВТОРОЙ таблицы внутри `ItemTooltip_Build`.
	//  Порт до этой правки знал ТОЛЬКО первый путь — отсюда «Эффект ЖК : 0 ~ 0» у свитка и
	//  полное отсутствие тултипа у премиум-предметов.
	const bool bLegacyOne = CVarLegacyOnePipeline.GetValueOnGameThread() != 0;
	// Эталонный аргумент `a4` простого билдера (`invSlotBuildTooltip`, 37447-37457):
	//   бит «привязано» записи (+50) == 3 и subType == 32 → 0; itemType вне 5..7 → −1; иначе (+50).
	const int32 BindByte = RawU8(It, 50);
	int32 BindArg = -1;
	if (R && BindByte == 3 && R->ItemSubType == 32)          { BindArg = 0; }
	else if (R && R->ItemType >= 5 && R->ItemType <= 7)      { BindArg = BindByte; }
	const int32 Quantity = RawI32(It, 9);

	bool bUseSimple = false;
	if (!bLegacyOne && R)
	{
		if (SourceKind == 2)      { bUseSimple = true; }                                  // вкладка «Расходники»
		else if (SourceKind == 3) { bUseSimple = ItemTypeUsesSimpleTooltip(R->ItemType); } // надетое
		// ★ [ИСПРАВЛЕНИЕ] 05.11: золото полным путём (окно хвастовства 28100 и др.) — ветка золота простого билдера.
		if (AsdaGoldUsesAmountTip(It.ItemCode)) { bUseSimple = true; }
	}
	// ★ [ИСПРАВЛЕНИЕ] баг 11: премиум не-снаряжение полным путём — подсказка простого билдера с профессией и привязкой (разбор у ключа).
	const bool bBagNonGear = !bLegacyOne && SourceKind == 1 && !bUseSimple && AsdaBagNonGearUsesSimple(R);

	FAsdaItemTooltipText T;
	FAsdaTipMetrics M;
	// ★★★ ОКНО СРАВНЕНИЯ ОТКРЫВАЕТ ТОЛЬКО ПОЛНЫЙ БИЛДЕР. `canCompareEquipped` вычисляется внутри
	//   `ItemTooltip_Build@0xEA5260` и ТОЛЬКО в ветке обычного предмета (item_inventory.c:60451):
	//   ни `CItem_BuildSowelTooltipText` (простой тултип/самоцвет), ни ветка записей ВТОРОЙ таблицы
	//   к нему не приходят, поэтому 5050 у них не появляется вовсе. Порт считал сравнение после
	//   ЛЮБОГО билдера — отсюда жалоба «слева коротко, а в сравнении Эффект ЖК/Профессия/…».
	bool bFullPipeline = false;
	if (!R && IT && IT->IsSowelCode(It.ItemCode))
	{
		// ★★★ САМОЦВЕТ: `CItem_BuildSowelTooltipText`, первая половина (портирована 2026-08-09).
		T = BuildTooltipSowel(It.ItemCode, M);
	}
	else if (!bLegacyOne && !R && IT && IT->IsSpecialItemCode(It.ItemCode))
	{
		// ★ ДЕФЕКТ 2: спец-предмет (`special_itemlist`). В сумке — ветка ВТОРОЙ таблицы
		//   `ItemTooltip_Build`; на вкладке расходников — ветка `itemTable2Record` простого билдера.
		T = (SourceKind == 2) ? BuildTooltipSimple(It.ItemCode, Quantity, BindArg, M)
		                      : BuildTooltipTable2(It, M);
	}
	else if (bBagNonGear)
	{
		T = BuildTooltipBagNonGear(It, M);
	}
	else if (bUseSimple)
	{
		T = BuildTooltipSimple(It.ItemCode, Quantity, BindArg, M);
	}
	else
	{
		bFullPipeline = true;
		T = BuildTooltip(It);
		// Эталон `ItemTooltip_Build`: у ОБЫЧНОГО предмета ширина ЖЁСТКАЯ 250 (`propMap = 250`),
		//   у shell-предмета (Table2) — clamp(текст, 90, 218) + 38.
		if (R && R->CategoryFlag == 3) { M.FixedWidth = 0.f; M.WidthAdd = 38.f; }
		else                           { M.FixedWidth = 250.f; }
		M.ExtraLines = 2;
		// ★ Эталон: у предмета с `categoryFlag == 3` включается значок премиума
		//   (`itemTipWinSetModeVisible(mgr, 0, 1)`), а укладка добавляет НИЖНЮЮ плашку с нотисом 109832.
		M.bPremiumFooter = (R && R->CategoryFlag == 3);
		M.FooterText = M.bPremiumFooter ? LocRaw(kLocEliteFooter) : FString();
	}

	if (!T.bValid)
	{
		UE_LOG(LogAsdaItemTip, Warning,
			TEXT("[тултип] предмет %d (источник %d): текст не собран (нет строки ни в item_list, ни в special_itemlist) — показ отменён"),
			It.ItemCode, SourceKind);
		return;
	}
	P->SetOwner(Owner);
	P->SetSourceWindow(TipSourceWindowUnderCursor(GetGameInstance(), P, ComparePanel));   // ★ 31.10.2026: окно-источник показа
	P->Show(T.Title, T.TitleArgb, T.Body, M);

	// ── ОКНО СРАВНЕНИЯ (эталон: рекурсивный вызов с useSecondaryWindow = 1) ──
	bCompareShown = false;
	FAsdaItemInstance Eq;
	const bool bCompareAllowed = bFullPipeline
		|| CVarLegacyCompareAnyPipeline.GetValueOnGameThread() != 0;
	if (bCompareAllowed && !bSuppressCompare && ResolveCompareTarget(It, Eq))
	{
		// ★ `Bind` зовём КАЖДЫЙ раз — по той же причине, что и у основной плашки в `GetMainPanel`:
		//   после `UAsdaWindowSubsystem::CloseAll()` окно 5050 выброшено из реестра, и без повторной
		//   привязки окно сравнения молча переставало показываться.
		{
			const bool bFirstCompare = (ComparePanel == nullptr);
			if (bFirstCompare) { ComparePanel = NewObject<UAsdaTipPanel>(this); }
			const TArray<FString> Hide = { TEXT("ItemCompareTip_2"), TEXT("ItemCompareTip_3"),
			                               TEXT("ItemCompareTip_Premium"), TEXT("Wood_Bolt") };
			if (!ComparePanel->Bind(GetGameInstance(), kCompareWindowId, kCompareWbp, kCompareZ,
			                        TEXT("ItemCompareTip_Title"), TEXT("ItemCompareTip_Contents"),
			                        TEXT("ItemCompareTip_Win_0_"), Hide))
			{
				ComparePanel = nullptr;
			}
			else if (bFirstCompare)
			{
				ComparePanel->BindFooter(TEXT("ItemCompareTip_Premium_icon"), TEXT("ItemCompareTip_3_"), TEXT("ItemCompareTip_3_Text"));
			}
		}
		if (ComparePanel)
		{
			const FAsdaItemTooltipText TC = BuildTooltip(Eq);
			if (TC.bValid)
			{
				const FAsdaItemRecord* ER = IT ? IT->GetItem(Eq.ItemCode) : nullptr;
				FAsdaTipMetrics MC = M;
				if (ER && ER->CategoryFlag == 3) { MC.FixedWidth = 0.f; MC.WidthAdd = 38.f; }
				else                             { MC.FixedWidth = 250.f; }
				MC.bPremiumFooter = (ER && ER->CategoryFlag == 3);
				MC.FooterText = MC.bPremiumFooter ? LocRaw(kLocEliteFooter) : FString();
				ComparePanel->Show(TC.Title, ER ? GradeColorArgb(ER->ItemGrade) : kColTitleDef, TC.Body, MC);
				bCompareShown = true;
			}
		}
	}
	if (!bCompareShown && ComparePanel) { ComparePanel->Hide(); }

	P->LayoutAtCursor(bCompareShown && ComparePanel ? (float)ComparePanel->GetLastSize().X : 0.f);
	PlaceComparePanel();
	LogShow(TEXT("предмет"), It.ItemCode, T.Title, T.Body, P);
}

void UAsdaItemTooltipSubsystem::ShowItemTooltipByCode(int32 ItemCode, EAsdaTipOwner Owner)
{
	UAsdaTipPanel* P = GetMainPanel();
	if (!P)
	{
		UE_LOG(LogAsdaItemTip, Warning, TEXT("[тултип] код %d: плашка не привязана — показывать нечем"), ItemCode);
		return;
	}
	// ★★★ [ОРИГИНАЛ] 07.11.2026: код САМОЦВЕТА. `CControl_SetState(&g_ItemTooltip, 1, код, …)` у камня идёт в
	//   `CItem_BuildSowelTooltipText` (порт `BuildTooltipSowel`), а `BuildTooltipByCode` читает только item_list и отказывал:
	//   подсказки не было у лотка окна 1530 (и по щелчку), у разборки и разделения улучшения (`IsSowelCode → ShowItemTooltipByCode`).
	if (CVarSowelHoverTip.GetValueOnGameThread() != 0)
	{
		UAsdaItemTableSubsystem* SowIT = Items();
		if (SowIT && SowIT->IsSowelCode(ItemCode))
		{
			FAsdaTipMetrics SowM;
			const FAsdaItemTooltipText SowT = BuildTooltipSowel(ItemCode, SowM);
			if (!SowT.bValid)
			{
				UE_LOG(LogAsdaItemTip, Warning, TEXT("[тултип] камень %d: текст не собран"), ItemCode);
				return;
			}
			bCompareShown = false;
			if (ComparePanel) { ComparePanel->Hide(); }
			P->SetOwner(Owner);
			P->SetSourceWindow(TipSourceWindowUnderCursor(GetGameInstance(), P, ComparePanel));
			P->Show(SowT.Title, SowT.TitleArgb, SowT.Body, SowM);
			P->LayoutAtCursor();
			LogShow(TEXT("камень по коду"), ItemCode, SowT.Title, SowT.Body, P);
			return;
		}
	}
	const FAsdaItemTooltipText T = BuildTooltipByCode(ItemCode);
	if (!T.bValid)
	{
		UE_LOG(LogAsdaItemTip, Warning, TEXT("[тултип] код %d: записи в таблице предметов нет — показ отменён"), ItemCode);
		return;
	}
	UAsdaItemTableSubsystem* IT = Items();
	const FAsdaItemRecord* R = IT ? IT->GetItem(ItemCode) : nullptr;
	FAsdaTipMetrics M;
	M.WidthAdd = 27.f;       // эталон `itemTooltipBuildByCode`: clamp + 27
	M.ExtraLines = 1;        // и строк = перенос + 1
	bCompareShown = false;
	if (ComparePanel) { ComparePanel->Hide(); }
	P->SetOwner(Owner);
	P->SetSourceWindow(TipSourceWindowUnderCursor(GetGameInstance(), P, ComparePanel));   // ★ 31.10.2026: окно-источник показа
	P->Show(T.Title, R ? GradeColorArgb(R->ItemGrade) : kColTitleDef, T.Body, M);
	P->LayoutAtCursor();
	LogShow(TEXT("по коду"), ItemCode, T.Title, T.Body, P);
}

// ★★★ ПОРТ `uiInventoryShowSlotTooltip@0xB87200` (ui_core.c:10433-10513) — см. шапку объявления
//   в заголовке (там же asm-доказательство каждого смещения синтетической записи).
//   Здесь только СБОРКА записи; выбор построителя дальше делает общая точка `ShowItemTooltip`
//   (закон `port-shared-points`): `GetItem` ⇒ полный `ItemTooltip_Build`, `IsSpecialItemCode` ⇒
//   ветка ВТОРОЙ таблицы того же билдера, `IsSowelCode` ⇒ `CItem_BuildSowelTooltipText`.
//   Эталонные аргументы `(slot, −1, 0, 1, 1, 0, 0)`: `suppressCompare = 0` ⇒ окно сравнения
//   разрешено (как у вещей сумки), `useSecondaryWindow = 0` ⇒ показ в ОСНОВНОЙ плашке.
void UAsdaItemTooltipSubsystem::ShowPreviewSlotTooltip(int32 ItemCode, EAsdaTipOwner Owner)
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return; }

	// Самоцвет — отдельный путь эталона (`CItem_BuildSowelTooltipText` + `uiTooltipShowAndLayout`);
	//   синтетическая запись ему не нужна, `ShowItemTooltip` уводит туда по одному коду.
	FAsdaItemInstance Slot;
	const TArray<uint8> Rec = BuildPreviewSlotRecord(ItemCode);   // ★ U17: сборка — отдельной функцией (ниже)
	Slot.SetRaw(Rec.GetData(), Rec.Num());

	ShowItemTooltip(Slot, /*bSuppressCompare*/ false, Owner, /*SourceKind*/ 1);
	UE_LOG(LogAsdaItemTip, Verbose,
		TEXT("[предпросмотр] код %d: синтетическая запись (прочность %d, вес/применений %d, флаг50 %d) ")
		TEXT("отдана ПОЛНОМУ построителю (эталон `uiInventoryShowSlotTooltip@0xB87200`)"),
		ItemCode, (int32)Rec[13], (int32)AsdaBytes::RdI16(Rec.GetData() + 14), (int32)Rec[50]);
}

// ★ U17 (В1 дедупа №2): запись через `FAsdaItemRecordWriter` (байты те же, что у прежней ручной сборки). Прежнее тело `ShowPreviewSlotTooltip` построчно:
//   `CSlot_Init(v10)` = нули, `[0]` = код; item_list ⇒ +13 прочность, +14 вес, +50 флаг; иначе special_itemlist ⇒ +9, +14, +50.
TArray<uint8> UAsdaItemTooltipSubsystem::BuildPreviewSlotRecord(int32 ItemCode) const
{
	FAsdaItemRecordWriter W;                       // эталонный `CSlot_Init(v10)` = нули
	W.Code(ItemCode);                              // `mov [ebp-0x40], eax`
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT) { return W.ToArray(); }
	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	FAsdaSpecialItemlistRow Sp;
	if (R)
	{
		W.Durability(FMath::Clamp(R->DurabilityMax, 0, 255));      // ItemRecord[+0x44]
		W.StackWeight(R->Weight);                                  // ItemRecord[+0x48]
		W.Flag50(FMath::Clamp(R->TooltipFlag200, 0, 255));         // ItemRecord[+0xC8]
	}
	else if (IT->GetSpecialItem(ItemCode, Sp))
	{
		W.Amount(Sp.Field88);                                      // Table2[+0x58]
		W.StackWeight(Sp.RequiredLevel);                           // Table2[+0x44]
		W.Flag50(FMath::Clamp(Sp.Flag124, 0, 255));                // Table2[+0x7C]
	}
	return W.ToArray();
}

// ★★★ ПОРТ `stampWnd_ShowItemTooltip@0xDB29B0` (item_enhance.c:3680-3741) — панель состава 26500.
//   Отличия от `uiInventoryShowSlotTooltip` перечислены в заголовке; ключевое — РАЗВИЛКА ПО
//   `itemType > 9`, которой у окна «Вид» нет, и пустая (кроме кода) синтетическая запись.
void UAsdaItemTooltipSubsystem::ShowStampSlotTooltip(int32 ItemCode, EAsdaTipOwner Owner)
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return; }

	FAsdaItemInstance Slot;
	const TArray<uint8> Rec = FAsdaItemRecordWriter::ByCode(ItemCode);   // `CSlot_Init(v6)`; `v6[0] = ItemRecord->itemCode` (★ U17)
	Slot.SetRaw(Rec.GetData(), Rec.Num());

	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	// SourceKind 2 = «вкладка расходников» = тот же ПРОСТОЙ билдер, в который эталон уводит
	//   `CControl_SetState(mode 0)`; SourceKind 1 = полный `ItemTooltip_Build`.
	const int32 SourceKind = (R && R->ItemType > 9) ? 2 : 1;
	ShowItemTooltip(Slot, /*bSuppressCompare*/ false, Owner, SourceKind);
	UE_LOG(LogAsdaItemTip, Verbose,
		TEXT("[состав пака] код %d: itemType=%d ⇒ построитель %s (эталон `stampWnd_ShowItemTooltip@0xDB29B0`)"),
		ItemCode, R ? R->ItemType : -1, (SourceKind == 2) ? TEXT("ПРОСТОЙ") : TEXT("ПОЛНЫЙ"));
}

// ★★★ ПОРТ `cItemBar_BuildSlotTooltip@0xB6F3C0` (item_inventory.c:5422-5474) — ячейки витрины
//   призов премиум-гачи 68311…68335 (окно 68300) и иконка приза 68453 (окно 68450).
//   Эталон ищет код: по 25 ячейкам сравнивает `a2` с idnum ячейки и берёт её `itemCode`
//   (`v10[3i+22]` / `v10[3i+21]`), а для `a2 == 68453` берёт код приза (`v10[134]`). Дальше —
//   развилка построителей, приведённая в заголовке. Код ячейки ищет ВЫЗЫВАЮЩИЙ (у него модель
//   страницы витрины), сюда приходит уже готовый.
void UAsdaItemTooltipSubsystem::ShowGachaSlotTooltip(int32 ItemCode, EAsdaTipOwner Owner)
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return; }

	FAsdaItemInstance Slot;
	const TArray<uint8> Rec = FAsdaItemRecordWriter::ByCode(ItemCode);   // `CSlot_Init(v6)` = нули; `v6[0] = itemCode ячейки` (★ U17)
	Slot.SetRaw(Rec.GetData(), Rec.Num());

	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	// Эталонный гейт ПОЛНОГО билдера: `itemType <= 4 || (itemType >= 8 && itemType <= 9)
	//   || categoryFlag == 3` (`ItemRecord->categoryFlag` = поле записи +0x9C).
	//   SourceKind 1 = полный `ItemTooltip_Build`, 2 = простой (`CControl_SetState(mode 0)`).
	const bool bFull = !R
		|| (R->ItemType <= 4) || (R->ItemType >= 8 && R->ItemType <= 9) || (R->CategoryFlag == 3);
	ShowItemTooltip(Slot, /*bSuppressCompare*/ false, Owner, bFull ? 1 : 2);
	UE_LOG(LogAsdaItemTip, Verbose,
		TEXT("[витрина гачи] код %d: itemType=%d categoryFlag=%d ⇒ построитель %s ")
		TEXT("(эталон `cItemBar_BuildSlotTooltip@0xB6F3C0`)"),
		ItemCode, R ? R->ItemType : -1, R ? R->CategoryFlag : -1, bFull ? TEXT("ПОЛНЫЙ") : TEXT("ПРОСТОЙ"));
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ ЯЧЕЙКА ВИТРИНЫ МАГАЗИНА NPC — порт `Item_BuildTooltipText_EAC520@0xEAC520`
//  (item_inventory.c:60836). Полное обоснование состава — в объявлении (заголовок).
//  Приход: `itemTooltip_setStateFromRecord@0xE285B0` (vtbl+40 стороны ПОКУПКИ) →
//  `itemTooltipUpdateOnHover@0xEAC430` → сюда, с `itemCount` = потолок стопки, `isBuyMode` = 0,
//  `showCount` = 1.
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaItemTooltipSubsystem::ShowShopSlotTooltip(int32 ItemCode, int32 Count, bool bSellMode,
                                                    EAsdaTipOwner Owner, bool bShowCount)
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return; }

	// Самоцвет — ВТОРАЯ половина той же эталонной функции; в порту это готовый `BuildTooltipSowel`.
	if (IT->IsSowelCode(ItemCode))
	{
		FAsdaTipMetrics M;
		const FAsdaItemTooltipText T = BuildTooltipSowel(ItemCode, M);
		if (T.bValid) { ShowTextTip(Owner, T.Title, T.Body, M); }
		return;
	}

	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	if (!R)
	{
		// ★ 25.10.2026 [ПОРТ]: карточки премиум-магазина и магазина бустеров — почти сплошь коды ВТОРОЙ таблицы
		//   (special_itemlist: свитки сброса 196/197, яйцо 398, бустер 405 …), а `ShowItemTooltipByCode` берёт только item_list —
		//   подсказка карточки молча не показывалась (журнал Player52 22.10 19:49:53: «код 398/405: записи нет»). Подсказка
		//   карточки этих двух витрин — НАША добавка (asda.PremiumMall.Tooltips), у оригинала её нет; показываем её той же
		//   веткой второй таблицы `ItemTooltip_Build`, что у предмета в сумке. Витрины NPC — как прежде (эталон
		//   `Item_BuildTooltipText_EAC520` для кода вне item_list ничего не строит). Откат: asda.ItemTip.ShopCardTable2 0.
		const bool bShopCard = (Owner == EAsdaTipOwner::PremiumShop || Owner == EAsdaTipOwner::BoosterShop);
		if (bShopCard && IT->IsSpecialItemCode(ItemCode) && AsdaItemTipShopCardTable2On())
		{
			FAsdaItemInstance Slot;
			const TArray<uint8> Rec = FAsdaItemRecordWriter::ByCode(ItemCode);   // запись ячейки: код, прочее — нули
			Slot.SetRaw(Rec.GetData(), Rec.Num());
			ShowItemTooltip(Slot, /*bSuppressCompare*/ true, Owner, /*SourceKind*/ 1);
			return;
		}
		// Записи второй таблицы у витрины тоже встречаются — отдаём их готовой ветке порта.
		ShowItemTooltipByCode(ItemCode, Owner);
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	auto Loc = [TS](int32 Id) -> FString { return TS ? TS->GetTextById(Id) : FString(); };

	const int32 Qty = FMath::Max(1, Count);
	// ЦЕНА. `isBuyMode == 0` ⇒ метка 2303 и `ItemRecord->price`; `== 1` ⇒ 2302 и `subCategory`.
	//   У `itemType == 10` количество на цену НЕ умножается (эталон, item_inventory.c:61157/61166).
	//   ★★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] у продажи — потолок сервера `CharItemSell` (цена единицы не выше
	//   «покупка − 25 %»), единая точка `UAsdaNpcShopSubsystem::ServerSellUnitCap`; откат `asda.Shop.ServerSellCap 0`.
	const int32 UnitPrice = bSellMode ? UAsdaNpcShopSubsystem::ServerSellUnitCap(R->SubCategory, R->Price) : R->Price;
	const int32 Price = (R->ItemType == 10) ? UnitPrice : (Qty * UnitPrice);
	const FString PriceLabel = Loc(bSellMode ? 2302 : 2303);
	const FString Gold = Loc(2304);

	// ЗАГОЛОВОК: при `isBuyMode || showCount` ⇒ «имя (N)», иначе просто имя (R-8). Цвет — по грейду.
	FAsdaItemTooltipText T = BuildTooltipByCode(ItemCode);
	if (!T.bValid) { return; }
	const FString Name = T.Title;
	if (bSellMode || bShowCount) { T.Title = FString::Printf(TEXT("%s (%d)"), *Name, Qty); }

	// ЦЕНА КРАСНАЯ, если денег меньше (эталонный `colorPriceWarn`, `<FC=%08x>…</F>`).
	//   ⚠ У режима покупки (`isBuyMode`) эталон эту проверку НЕ делает вовсе — цена всегда обычная.
	const int32 Gold_ = Inv ? Inv->GetGold() : 0;
	const bool bPoor = !bSellMode && (Price > Gold_);
	const FString PriceStr = bPoor
		? FString::Printf(TEXT("<FC=FFFF0000>%s</F>"), *AsdaUi::FormatThousands(Price))
		: AsdaUi::FormatThousands(Price);

	// ТЕЛО. Ветка `case 0xA` (itemType 10) даёт «Вес / Количество / пустая / Цена»,
	//   остальные (`case 0xB` и прочие) — «Вес / пустая / описание / пустая / Цена».
	const FString Desc = T.Body;
	FString Body;
	if (R->ItemType == 10)
	{
		Body = FString::Printf(TEXT("%s : %d\n%s : %d\n\n%s : %s %s"),
			*Loc(2295), R->Weight, *Loc(2305), Qty, *PriceLabel, *PriceStr, *Gold);
	}
	else
	{
		Body = FString::Printf(TEXT("%s : %d\n\n%s\n\n%s : %s %s"),
			*Loc(2295), R->Weight, *Desc, *PriceLabel, *PriceStr, *Gold);
	}
	T.Body = Body;

	// Метрики — как у прочих простых плашек оригинала (ширина по тексту, строк = перенос + 2).
	FAsdaTipMetrics M;
	ShowTextTip(Owner, T.Title, T.Body, M);
	UE_LOG(LogAsdaItemTip, Verbose,
		TEXT("[витрина магазина] код %d ×%d: itemType=%d цена %d (%s), денег %d ⇒ %s"),
		ItemCode, Qty, R->ItemType, Price, bSellMode ? TEXT("продажа") : TEXT("покупка"),
		Gold_, bPoor ? TEXT("красная") : TEXT("обычная"));
}

// ★★★ ПОРТ `uiShowEquipSlotTooltip2@0xD925A0` / `uiShowEquipSlotTooltip@0xD92B30` (ui_core.c:59846,
//   59974) — иконки награды попапа задания `EPISODEPOP_WIN` (540). Полный разбор развилки и
//   обоснование отступления для панели гарантированной награды — в объявлении (заголовок) и в
//   `Reports/quest_reward_tooltip_exp.md` §1. Гейт «вид слота 8…12» делает вызывающий.
void UAsdaItemTooltipSubsystem::ShowQuestRewardSlotTooltip(int32 ItemCode, int32 Count,
                                                           bool bSelectPanel, EAsdaTipOwner Owner)
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return; }

	// Синтетическая запись слота — как у всех окон без 57-байтовой записи (эталон `CSlot_Init`):
	//   код в +0, количество в +9 (его читает `Quantity` простого билдера — `RawI32(It, 9)`).
	FAsdaItemInstance Slot;
	const TArray<uint8> Rec = FAsdaItemRecordWriter::ByCodeQty(ItemCode, FMath::Max(0, Count));   // ★ U17
	Slot.SetRaw(Rec.GetData(), Rec.Num());

	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	// SourceKind 1 = полный `ItemTooltip_Build`, 2 = простой (`CControl_SetState` mode 0).
	//   Самоцвет сюда не приходит: `ShowItemTooltip` уводит его в `BuildTooltipSowel` по коду.
	int32 SourceKind = 1;
	if (!R)
	{
		// Запись ВТОРОЙ таблицы (`special_itemlist`): обе панели эталона отдают её ПРОСТОМУ билдеру.
		//   Самоцвета это не касается — у него своя ветка внутри `ShowItemTooltip`.
		if (!IT->IsSowelCode(ItemCode)) { SourceKind = 2; }
	}
	else if (!bSelectPanel)
	{
		// Эталонное правило КЛИКА панели гарантированной награды (`uiShowEquipSlotTooltip`,
		//   ui_core.c:60026): `itemType > 4 && (itemType < 8 || itemType > 9)` → простой билдер.
		SourceKind = ((R->ItemType > 4) && (R->ItemType < 8 || R->ItemType > 9)) ? 2 : 1;
	}
	// Панель ВЫБОРА: эталон зовёт полный `ItemTooltip_Build` для ЛЮБОЙ записи `item_list` — SourceKind 1.

	ShowItemTooltip(Slot, /*bSuppressCompare*/ false, Owner, SourceKind);
	UE_LOG(LogAsdaItemTip, Verbose,
		TEXT("[награда задания] код %d ×%d, панель %s: itemType=%d ⇒ построитель %s ")
		TEXT("(эталон `uiShowEquipSlotTooltip2@0xD925A0`)"),
		ItemCode, Count, bSelectPanel ? TEXT("ВЫБОРА") : TEXT("гарантированная"),
		R ? R->ItemType : -1, (SourceKind == 2) ? TEXT("ПРОСТОЙ") : TEXT("ПОЛНЫЙ"));
}

// Окно сравнения ставится ВПЛОТНУЮ СПРАВА от основного (эталон `Tooltip_LayoutNearCursor`:
//   `x = width(5000) + x0`). ★ Позиция берётся из `GetLastViewportPos()` — той, что мы САМИ только
//   что выставили, а НЕ из кэш-геометрии: та отстаёт на кадр и живёт в абсолютном пространстве
//   десктопа (смешение этих систем и роняло плашки в угол экрана).
void UAsdaItemTooltipSubsystem::PlaceComparePanel()
{
	if (!bCompareShown || !ComparePanel || !MainPanel) { return; }
	ComparePanel->SetViewportPos(MainPanel->GetLastViewportPos() + FVector2D(MainPanel->GetLastSize().X, 0.f));
}

void UAsdaItemTooltipSubsystem::HideTooltip()
{
	if (MainPanel)    { MainPanel->Hide(); }
	if (ComparePanel) { ComparePanel->Hide(); }
	bCompareShown = false;
}

bool UAsdaItemTooltipSubsystem::IsTooltipShown() const
{
	return MainPanel && MainPanel->IsShown();
}
