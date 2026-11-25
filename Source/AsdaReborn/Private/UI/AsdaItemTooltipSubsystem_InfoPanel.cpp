// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTooltipSubsystem_InfoPanel.cpp — ★ R2 10.11.2026 (разрез AsdaItemTooltipSubsystem.cpp, дословный перенос): инфо-панель: стили, вёрстка, плоскости, иконки, сокеты, FillInfoPanel.
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

// ★ ДЕФЕКТЫ ОРИГИНАЛА В ВЕТКЕ ЗАПИСЕЙ ВТОРОЙ ТАБЛИЦЫ (`special_itemlist`) — ИСПРАВЛЕНЫ.
//   Д6. Ветка `if (itemTable2Record_2 && pItemSlot)` (item_inventory.c:36668-36790) пишет ТОЛЬКО
//       11 контролов из ~30, а сброса текстов у панели нет вовсе (`uiInventoryDetail_Reset@0xD17300`
//       трогает лишь вкладку/страницу/скролл). Поэтому после клика по обычному предмету и затем по
//       премиум-предмету на панели ОСТАЮТСЯ чужие строки (профессия, гнёзда, опции, вес…).
//       Мы гасим панель перед заполнением ветки.
//   Д7. В ветке `subType == 15` строка «не передаётся» (контрол 10005) собирается в буфер `lineBuf`
//       через `switch (flag_124)`, а у ветки `default` буфер НЕ перезаписывается — и в контрол
//       уходит содержимое, лежавшее там раньше, то есть ИМЯ ПРЕДМЕТА (`sprintf(lineBuf,"%s",name)`
//       на строке 36675). Мы в этом случае пишем пусто.
//   Вернуть оба эталонных дефекта разом: `asda.ItemTip.LegacySpecialDefects 1`.
static ASDA_DEV_CVAR(int32, CVarLegacySpecialDefects,
	TEXT("asda.ItemTip.LegacySpecialDefects"), 0,
	TEXT("1 — воспроизводить дефекты оригинала в ветке special_itemlist: не гасить панель + утечка имени в строку 10005"),
	ECVF_Default);

// ★★★ ОТКАТ ОФОРМЛЕНИЯ ИНФО-ПАНЕЛИ ПО `TextOutLine` СКИНА.
//   `TextOutLine` (JuiSkin.ini → поле контрола this+3700) — это НЕ обводка, а РЕЖИМ РИСОВАНИЯ
//   ТЕКСТА: ui_core.c:110743 `if (field925 == 1) flags |= 0x2010; else flags |= 0x100`, где
//   0x10 = DT_WORDBREAK (перенос по ширине rect), 0x100 = DT_NOCLIP (одна строка, без обрезки —
//   `textFontCountWrapLines`, font_text.c: ветка `if (a5 & 0x100)` считает ТОЛЬКО явные \n).
//   Импортёр JUI трактовал флаг как обводку (`_apply_text_outline`) и брал перенос из «h >= 28».
//   1 — вернуть поведение импортёра, 0 — эталонное (перенос+клип у скинов с TextOutLine = 1).
static ASDA_DEV_CVAR(int32, CVarLegacyInfoTextOutline,
	TEXT("asda.ItemTip.LegacyInfoTextOutline"), 0,
	TEXT("1 — оформление текста панели как у импортёра (обводка, перенос по высоте); 0 — по TextOutLine скина"),
	ECVF_Default);

// ★★★ КРУГ 5, Д1. ПАНЕЛЬ ТОЛЬКО ДЛЯ ОРУЖИЯ / БРОНИ / ЩИТА (ТРЕБОВАНИЕ ПОЛЬЗОВАТЕЛЯ).
//   ЭТАЛОН НЕ ТАК: `CInventoryUI_OnSlotClick@0xD01F50` (item_inventory.c:36268-36343) наполняет
//   правую панель ЛЮБЫМ предметом вкладки «Снаряжение» — в том числе записью `special_itemlist`
//   (для неё там даже отдельная ветка с `uiWindowResetAllFields` + повторным `PopulateInfoPanel`).
//   Живая проверка (лог `Saved/Logs/AsdaReborn.log`, 2026-08-09 11:36:52): хотбар-строка
//   «подтип=1 ячейка=3 кол-во=23 id=500» — в СУМКЕ (контейнер 1, вкладка «Снаряжение») лежит
//   спец-предмет 500 «Свиток ремонта брони (30)», и клик по нему раскрывал панель.
//   Пользователь требует: панель показывает ТОЛЬКО оружие/броню/щит, для остальных — СКРЫВАТЬ окно.
//   Предикат берём эталонный (`IsEquipInfoPanelType`, см. .h), поведение — по требованию.
//   Вернуть эталон: `asda.ItemTip.LegacyInfoPanelAnyItem 1`.
static ASDA_DEV_CVAR(int32, CVarLegacyInfoPanelAnyItem,
	TEXT("asda.ItemTip.LegacyInfoPanelAnyItem"), 0,
	TEXT("1 — правую панель наполнять любым предметом вкладки «Снаряжение» (эталон); 0 — только оружие/броня/щит"),
	ECVF_Default);

// ★★★ КРУГ 6, Д1. ПАНЕЛЬ БЕЗ ВЫБРАННОГО ПРЕДМЕТА — СВЁРНУТА (ТРЕБОВАНИЕ ПОЛЬЗОВАТЕЛЯ).
//   ЭТАЛОН НЕ ТАК: смену вкладки обслуживает `inventoryItemDetailSetVisible@0xD173A0`
//   (item_inventory.c:41540): вкладка 1 → `uiInventoryDetail_Reset` + `PopulateInfoPanel(this,1,-1)`
//   + `uiPanelExpand`, то есть панель раскрывается САМА и стоит ПУСТОЙ, пока не кликнут предмет
//   (`PopulateInfoPanel` со слотом −1 только гасит поля). Ровно на это и жалоба: «открыл инвентарь,
//   ничего не выбирал, переключился в снарягу — и оно отображено пустое».
//   ТРЕБОВАНИЕ: правая панель раскрыта ТОЛЬКО когда в ней лежит предмет подходящего типа
//   (предикат `IsEquipInfoPanelType`, Д1 круга 5). Нет выбора — окно свёрнуто (ширина 228).
//   РЕАЛИЗАЦИЯ: панель раскрывает `UAsdaInventorySubsystem` (его файл не правим), поэтому мы держим
//   собственный признак наполнения `bInfoPanelFilled` и гасим окно из `ClearInfoPanel` +
//   отложенной свёрткой в `Tick` (тот же механизм `bPendingPanelCollapse`, что и у Д1 круга 5).
//   ⚠ РУЧНАЯ стрелка 408 под правило НЕ подпадает: это явное действие пользователя, а не
//   самопроизвольное раскрытие; эталонный гейт вкладки у неё свой (`SetPanelExpanded(bFromUser=true)`).
//   Вернуть эталон: `asda.ItemTip.LegacyInfoPanelNoSelection 1`.
static ASDA_DEV_CVAR(int32, CVarLegacyInfoPanelNoSelection,
	TEXT("asda.ItemTip.LegacyInfoPanelNoSelection"), 0,
	TEXT("1 — правая панель может стоять раскрытой без выбранного предмета (эталон: вкладка «Снаряжение» раскрывает её сама); 0 — без выбора свёрнута"),
	ECVF_Default);

// ★★★ КРУГ 5, Д3. ФОРМАТ ПРАВОЙ ПАНЕЛИ: НЕ ВЫХОДИТЬ ЗА РАМКУ И НЕ ТЕРЯТЬ ХВОСТ.
//   ЭТАЛОН: текст контрола рисуется с DT_WORDBREAK и БЕЗ DT_NOCLIP (`TextOutLine = 1`), то есть
//   переносится по ширине rect и ОБРЕЗАЕТСЯ рамкой ЭТОГО КОНТРОЛА. Но сами rect'ы правой колонки
//   шире деревянной подложки: `INVEN_SOLLTXT05/06`, `INVEN_ArmoryOp3/4/5`, `INVEN_Primium_Option_
//   text/2` имеют ширину 265 при левом крае 572 (абс.) — правый край 837, а подложка
//   `Inven_RightBase` кончается на 759 (Origin 227+39, Size 418×428). Отсюда «строки вылезают за
//   рамку». А контролы высотой 14 px при переносе прячут ВТОРУЮ строку целиком — отсюда
//   «Прочность: Неограниченно» обрывалось/наезжало.
//   ПОРТ: (1) ширина слота ужимается до правой границы рамки; (2) однострочные контролы НЕ
//   переносятся, а УЖИМАЮТ КЕГЛЬ, пока строка не влезет (вместо потери хвоста); (3) клип по рамке
//   контрола остаётся эталонным. Вернуть эталонное поведение: `asda.ItemTip.LegacyInfoOverflow 1`.
static ASDA_DEV_CVAR(int32, CVarLegacyInfoOverflow,
	TEXT("asda.ItemTip.LegacyInfoOverflow"), 0,
	TEXT("1 — рамку и кегль панели не трогать (эталон: rect шире подложки, хвост обрезается); 0 — ужимать"),
	ECVF_Default);

// ★★★ КРУГ 5, Д3. ПОЗИЦИЯ 463 `INVEN_attack_speed`.
//   Эталон в инициализации окна двигает контрол: `CControl_SetField74_75(pInvenWnd[73], 14, 191)`
//   (item_inventory.c:35346; `SetField74_75` = `posX/posY`, ui_core.c:1602). В .JUI у 463 Origin
//   «14 119» — там же, где 491 `INVEN_LEVEL`, поэтому сдвиг +72 по Y эталону НУЖЕН. Но точка 191
//   попадает НА РЯД ПЛАШЕК самоцветов: 419 `INVEN_SOLLTXT01` — Origin «25 189», размер 140×17
//   (то есть 189…206). Оба контрола показываются одновременно (463 — у любого itemType 0…9 кроме
//   5/6/7; плашка — когда в гнёздах 1…3 есть камни), и строка «Скорость атаки» ложится на плашку.
//   Это ДЕФЕКТ ОРИГИНАЛА (закон `fix-original-defects`): переносим 463 в свободную полосу НИЖЕ
//   обеих строк плашек и ВЫШЕ 10005 (в координатах WBP: плашки 245…283, 10005 на 302).
//   Вернуть эталонные (14, 191): `asda.ItemTip.LegacyAttackSpeedPos 1`.
static ASDA_DEV_CVAR(int32, CVarLegacyAttackSpeedPos,
	TEXT("asda.ItemTip.LegacyAttackSpeedPos"), 0,
	TEXT("1 — 463 на эталонной позиции (14,191), где она ложится на плашку самоцвета; 0 — в свободную полосу"),
	ECVF_Default);

static constexpr uint32 kColInfoBound = 0xFFF70000;  // инфо-панель: «не передаётся»
static constexpr uint32 kColSollTxt00 = 0xFF478AEF;  // инфо-панель 418: SetField923(-12088593)

static constexpr int32 kLocPowerRange     = 110642;  // диапазон возможностей (инфо-панель, контрол 476)

static constexpr int32 kLocShellLevel     = 109901;  // требуемый уровень у shell-предмета (инфо-панель)

static constexpr int32 kLocDurabilityNA   = 110309;  // прочность у shell-предмета (инфо-панель)

static constexpr int32 kLocOptNormal      = 99200;   // заголовок «Обычное улучшение» (инфо-панель 426)
static constexpr int32 kLocOptUpgrade     = 99201;   // «Улучшение» (427)
static constexpr int32 kLocOptCraft       = 99202;   // «Крафт» (428)
static constexpr int32 kLocOptChargeA     = 111052;  // заголовки 427/428 у «зарядного» предмета
static constexpr int32 kLocOptChargeB     = 111053;

static constexpr int32 kLocAttackSpeed    = 111405;  // строка контрола 463 (%d = linkedGroupCode)

// ═══════════════════ правая инфо-панель окна инвентаря ═══════════════════
// Прочитать `Justification` контрола: свойство protected и C++-геттера у UTextLayoutWidget нет.
//   ★ Реализация переехала в ОБЩУЮ ТОЧКУ `AsdaUi::ReadJustification` (второй потребитель — баннер
//   599 в `UAsdaNpcDialogSubsystem`); здесь остаётся только псевдоним, чтобы не плодить копию.
void UAsdaItemTooltipSubsystem::CaptureInfoStyles()
{
	if (!InvenWindow || !InvenWindow->WidgetTree) { return; }
	// ⚠ Снимать МОЖНО только пока контролы ещё `UTextBlock`. Если окно перепривязывают повторно
	//   (тот же виджет, уже прошедший через `ApplyFormatted`), `UTextBlock` не найдётся ни одного —
	//   тогда СТАРЫЙ снимок оставляем, иначе потеряем оформление насовсем.
	TMap<FName, FAsdaInfoTextStyle> Fresh;
	TArray<UWidget*> All;
	InvenWindow->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		UTextBlock* T = Cast<UTextBlock>(W);
		if (!T) { continue; }
		FAsdaInfoTextStyle S;
		S.Style.SetFont(T->GetFont());                              // кегль 8 + обводка скина
		S.Style.SetColorAndOpacity(T->GetColorAndOpacity());        // цвет из `EXS TextColor`
		S.Style.SetShadowOffset(T->GetShadowOffset());
		S.Style.SetShadowColorAndOpacity(T->GetShadowColorAndOpacity());
		S.Justify = AsdaUi::ReadJustification(T);                       // из `EXS TextAlign`
		S.bWrap   = T->GetAutoWrapText();                           // импортёр: перенос только при h >= 28
		S.bValid  = true;
		// ★ Д3 (КРУГ 5): габариты слота — ширина нужна для ужатия кегля, высота отличает
		//   единственный МНОГОСТРОЧНЫЙ контрол панели (422 `INVEN_SHELLCONTENS`, 346×86).
		if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(T))
		{
			const FVector2D Sz = CS->GetSize();
			S.MaxWidth   = (Sz.X > 1.f) ? (float)Sz.X : 0.f;
			S.bMultiLine = (Sz.Y >= 28.f);
		}
		Fresh.Add(T->GetFName(), S);
	}
	if (Fresh.Num() > 0) { InfoStyles = MoveTemp(Fresh); }

	// ★★★ ПЕРЕНОС/КЛИП И ОБВОДКА — ПО `TextOutLine` СКИНА, А НЕ ПО ВЫСОТЕ КОНТРОЛА.
	//   Разбор (дважды, по данным и по коду):
	//     • `JuiSkin.ini` → `TextOutLine` → `sub_12E4D50@0x12E4D50` пишет его в `CTextControl+3700`
	//       (поле `this[925]`), stl_runtime.c:53569;
	//     • label-рендер, ui_core.c:110743: `if (this[925] == 1) flags |= this[926] | 0x2010;
	//       else flags |= this[926] | 0x100;`
	//     • `textFontCountWrapLines@0x12C7900` (font_text.c): ветка `if (a5 & 0x100)` считает ТОЛЬКО
	//       явные переводы строки, ветка без 0x100 (то есть при 0x2010, где 0x10 = DT_WORDBREAK)
	//       меряет ширину глифов и ПЕРЕНОСИТ по ширине rect.
	//   ⇒ `TextOutLine = 1` — это «переносить по ширине и обрезать рамкой», а НЕ обводка.
	//   Обводку эталон ставит отдельным механизмом — биты стиля шрифта `CClass_SetField926`
	//   (0x200000 жирный · 0x400000 тень · 0x800000 обводка); в этом окне она стоит ТОЛЬКО у
	//   414 `INVEN_ITEMNAME` (item_inventory.c:35392). Импортёр JUI трактовал `TextOutLine` как
	//   обводку и раздал её всем контролам панели, а перенос брал из эвристики «h >= 28» — отсюда
	//   «не те обводки» и «перенос текста отсутствует». Перегенерация WBP запрещена, поэтому
	//   правим оформление В РАНТАЙМЕ, при захвате стилей.
	//   Ниже — контролы панели `INVEN_WIN` с их скинами (сверено с `gamemain_jui.ini` + `JuiSkin.ini`):
	//     NoImage / NoImage_icon (TextOutLine = 1) → перенос + клип;
	//     Sowel_M / Map_Empty / Map_Empty_2 (TextOutLine = 0) → одна строка, БЕЗ клипа (DT_NOCLIP).
	if (CVarLegacyInfoTextOutline.GetValueOnGameThread() == 0)
	{
		static const TCHAR* kNoWrapNoClip[] = {          // скины Sowel_M / Map_Empty / Map_Empty_2
			TEXT("INVEN_SOLLTXT01"), TEXT("INVEN_SOLLTXT02"), TEXT("INVEN_SOLLTXT03"),
			TEXT("INVEN_SOLLTXT04_1"),
			TEXT("Inven_NormalOption"), TEXT("Inven_UpgadingOption"), TEXT("Inven_CraftingOption"),
			TEXT("INVEN_Primium_Option"),
		};
		TSet<FName> NoWrap;
		for (const TCHAR* N : kNoWrapNoClip) { NoWrap.Add(FName(N)); }
		const bool bFitText = (CVarLegacyInfoOverflow.GetValueOnGameThread() == 0);
		int32 Wrapped = 0;
		for (TPair<FName, FAsdaInfoTextStyle>& P : InfoStyles)
		{
			const bool bTextOutLine = !NoWrap.Contains(P.Key);
			// ★★★ Д3 КРУГА 5. Эталонный `DT_WORDBREAK` осмыслен ТОЛЬКО там, где в rect помещается
			//   больше одной строки. У всех контролов панели, кроме 422 (`INVEN_SHELLCONTENS`, 86 px),
			//   высота 14…17 px — перенос там ПРЯЧЕТ вторую строку под клипом, то есть просто съедает
			//   хвост («Прочность: Неограниченно» → «Прочность:»). Поэтому перенос оставляем только
			//   многострочным, а однострочные ужимаем кеглем (см. `SetInfoText`).
			P.Value.bMultiLine = bTextOutLine && P.Value.bMultiLine;
			P.Value.bWrap = bFitText ? P.Value.bMultiLine : bTextOutLine;
			if (P.Value.bWrap) { ++Wrapped; }
			// Обводку снимаем у ВСЕХ; 414 получит её обратно в `ApplyEtalonInfoLayout`
			//   (там же 418 — жирный), ровно как её ставит инициализация окна в эталоне.
			FSlateFontInfo F = P.Value.Style.Font;
			F.OutlineSettings.OutlineSize = 0;
			P.Value.Style.SetFont(F);
		}
		UE_LOG(LogAsdaItemTip, Log,
			TEXT("[инфо-панель] режим текста по `TextOutLine` скина: перенос+клип у %d из %d контролов, обводка снята"),
			Wrapped, InfoStyles.Num());
	}
	UE_LOG(LogAsdaItemTip, Log, TEXT("[инфо-панель] снято оформление текст-контролов: %d"), InfoStyles.Num());
}

// ★★★ Д3 (КРУГ 5). УЖАТЬ ТЕКСТ-КОНТРОЛЫ ПАНЕЛИ ПО ПРАВОЙ ГРАНИЦЕ ДЕРЕВЯННОЙ ПОДЛОЖКИ.
//   Данные (дамп WBP, `UAsdaUmgBuilder::DumpWidgetTree`, и `gamemain_jui.ini` окна 400):
//     `Inven_RightBase_grp`   L=226  W=496   ← контейнер правой половины окна
//       `Inven_RightBase_55_0` L=1   W=418   ← САМА подложка Box_Brown ⇒ правый край = 1+418 = 419
//       `Inven_ItemInfo_Win_grp` L=21 W=475  ← блок текста инфо-панели
//         `Inven_NormalOption_grp` L=210 → дети L=0 W=265  ⇒ правый край 210+265 = 475 > 398 (=419−21)
//   То есть у эталона rect правой колонки на ~77 px шире подложки; строки «Критический удар
//   (баллиста) +8» и «Снижение потери опыта убитым +10%» физически рисуются ЗА рамкой. Клип по
//   собственному rect (эталонный `TextOutLine = 1` без DT_NOCLIP) этого НЕ лечит — режет по 265.
//   Уменьшаем ширину слота до границы подложки (только уменьшаем — вызов идемпотентен) и
//   запоминаем её в `InfoStyles[..].MaxWidth` для ужатия кегля.
void UAsdaItemTooltipSubsystem::ClampInfoTextToFrame()
{
	if (!InvenWindow || !InvenWindow->WidgetTree) { return; }
	if (CVarLegacyInfoOverflow.GetValueOnGameThread() != 0) { return; }

	// Абсолютная (в системе координат корневого канваса окна) левая граница виджета.
	auto AbsLeft = [](const UWidget* W) -> float
	{
		float X = 0.f;
		for (const UWidget* Cur = W; Cur; Cur = Cur->GetParent())
		{
			if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Cur)) { X += (float)CS->GetOffsets().Left; }
		}
		return X;
	};

	// Правая граница подложки: ищем план `Inven_RightBase_{idx}_0` (имя даёт импортёр JUI).
	UWidget* Frame = nullptr;
	InvenWindow->WidgetTree->ForEachWidget([&Frame](UWidget* W)
	{
		if (Frame || !Cast<UImage>(W)) { return; }
		const FString N = W->GetName();
		if (N.StartsWith(TEXT("Inven_RightBase_")) && N.EndsWith(TEXT("_0"))) { Frame = W; }
	});
	if (!Frame) { UE_LOG(LogAsdaItemTip, Warning, TEXT("[инфо-панель] подложка Inven_RightBase не найдена — ужатие по рамке пропущено")); return; }
	const UCanvasPanelSlot* FrameSlot = AsdaUi::SlotOf(Frame);
	if (!FrameSlot) { return; }
	const float FrameRight = AbsLeft(Frame) + (float)FrameSlot->GetSize().X;

	UWidget* const FrameRoot = Frame->GetParent();      // `Inven_RightBase_grp` — контейнер правой половины
	// Правим ТОЛЬКО потомков подложки: слева от неё лежит сетка инвентаря (счётчики стака и т.п.),
	//   она не наша зона и ширину слотов ей менять нельзя.
	auto IsUnderFrame = [FrameRoot](const UWidget* W) -> bool
	{
		for (const UWidget* Cur = W; Cur; Cur = Cur->GetParent()) { if (Cur == FrameRoot) { return true; } }
		return false;
	};

	int32 Clamped = 0;
	for (TPair<FName, FAsdaInfoTextStyle>& P : InfoStyles)
	{
		UWidget* W = Info(*P.Key.ToString());
		if (!W || !IsUnderFrame(W)) { continue; }
		UCanvasPanelSlot* CS = AsdaUi::SlotOf(W);
		if (!CS) { continue; }
		const float Left = AbsLeft(W);
		const FVector2D Sz = CS->GetSize();
		const float Avail = FrameRight - Left;
		if (Avail > 8.f && Sz.X > Avail + 0.5f)
		{
			CS->SetSize(FVector2D(Avail, Sz.Y));
			P.Value.MaxWidth = Avail;
			++Clamped;
		}
		else if (Sz.X > 1.f)
		{
			P.Value.MaxWidth = (float)Sz.X;
		}
	}
	UE_LOG(LogAsdaItemTip, Log,
		TEXT("[инфо-панель] ширина по рамке (правый край %.0f): ужато контролов %d из %d"),
		FrameRight, Clamped, InfoStyles.Num());
}

// Подобрать кегль, при котором строка целиком влезает в MaxWidth. Эталон в этом месте просто
//   обрезал хвост рамкой (`TextOutLine = 1` ⇒ нет DT_NOCLIP) — правка помечена как отступление,
//   откат `asda.ItemTip.LegacyInfoOverflow 1`.
int32 UAsdaItemTooltipSubsystem::FitFontSizeToWidth(const FString& Plain, const FSlateFontInfo& Font, float MaxWidth)
{
	const int32 Base = FMath::Max(1, (int32)Font.Size);
	if (MaxWidth <= 1.f || Plain.IsEmpty() || !FSlateApplication::IsInitialized()) { return Base; }
	const TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	// Мерить надо САМУЮ ДЛИННУЮ строку: `Measure` не разбирает \n и посчитал бы всё в одну строку.
	TArray<FString> Lines;
	Plain.ParseIntoArray(Lines, TEXT("\n"), /*CullEmpty=*/true);
	if (Lines.Num() == 0) { return Base; }
	// Минимальный кегль — 5 px: ниже шрифт UI (arial/arialbd, см. AsdaUmgBuilder) нечитаем; дальше
	//   остаётся эталонный клип.
	for (int32 Size = Base; Size >= 5; --Size)
	{
		FSlateFontInfo F = Font;
		F.Size = Size;
		float Widest = 0.f;
		for (const FString& L : Lines) { Widest = FMath::Max(Widest, (float)FM->Measure(L, F).X); }
		if (Widest <= MaxWidth) { return Size; }
	}
	return 5;
}

void UAsdaItemTooltipSubsystem::ApplyEtalonInfoLayout()
{
	if (!InvenWindow) { return; }
	// ⚠ СДВИГИ КООРДИНАТ — строго один раз на окно (иначе сдвинем контролы дважды), а флаги стиля
	//   шрифта (пункт 3) переставляем при КАЖДОМ захвате: `CaptureInfoStyles` их обнуляет.
	const bool bDoLayout = !bInfoLayoutFixed;
	bInfoLayoutFixed = true;
	if (bDoLayout)
	{

	// ★ 1. `INVEN_attack_speed` (463). Эталон в инициализации окна двигает его:
	//      `CControl_SetField74_75(pInvenWnd[73], 14, 191)` (item_inventory.c:35346).
	//   В .JUI у контрола Origin = "14 119", то есть эталон опускает его на +72 по Y. Без этого он
	//   лежит РОВНО поверх `INVEN_LEVEL` (491), у которого в .JUI та же строка, — и в порту обе
	//   строки печатались одна на другой (жалоба «строки наезжают»).
	//   ★★★ КРУГ 5, Д3: эталонная точка (14, 191) попадает НА РЯД ПЛАШЕК САМОЦВЕТОВ — 419
	//   `INVEN_SOLLTXT01` стоит в .JUI на «25 189» размером 140×17 (то есть занимает 189…206).
	//   Контролы показываются одновременно, отсюда жалоба «Скорость атаки наезжает на полосу».
	//   Дефект оригинала: опускаем 463 в свободную полосу МЕЖДУ вторым рядом плашек и 10005.
	//   В координатах слота WBP: первый ряд плашек T=245 (h 17), второй T=266 (h 17), 10005 T=302 —
	//   свободно 283…302, ставим 284. Откат: `asda.ItemTip.LegacyAttackSpeedPos 1`.
	if (UWidget* Spd = Info(TEXT("INVEN_attack_speed")))
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Spd))
		{
			const FMargin O = S->GetOffsets();
			const float NewTop = (CVarLegacyAttackSpeedPos.GetValueOnGameThread() != 0)
				? (O.Top + 72.f)     // эталон: (14, 191) в системе .JUI
				: 284.f;             // порт: свободная полоса под плашками самоцветов
			S->SetOffsets(FMargin(O.Left, NewTop, O.Right, O.Bottom));
			UE_LOG(LogAsdaItemTip, Log,
				TEXT("[инфо-панель] 463 `INVEN_attack_speed`: Y %.0f → %.0f (%s)"), O.Top, NewTop,
				(CVarLegacyAttackSpeedPos.GetValueOnGameThread() != 0) ? TEXT("эталон +72") : TEXT("свободная полоса"));
		}
	}
	// ★ 2. Строки сет-бонуса 488/489: эталон опускает КАЖДУЮ на +14
	//      (`SetField74_75(ctl, x, y + 14)`, item_inventory.c:35433).
	for (const TCHAR* N : { TEXT("INVEN_Primium_Option_text"), TEXT("INVEN_Primium_Option_text2") })
	{
		if (UWidget* W = Info(N))
		{
			if (UCanvasPanelSlot* S = AsdaUi::SlotOf(W))
			{
				const FMargin O = S->GetOffsets();
				S->SetOffsets(FMargin(O.Left, O.Top + 14.f, O.Right, O.Bottom));
			}
		}
	}
	}   // ← конец блока однократных сдвигов координат
	// ★ 3. Флаги стиля шрифта, которые эталон ставит в инициализации (`CClass_SetField926`,
	//   биты font_text.c: 0x200000 жирный · 0x400000 тень · 0x800000 обводка):
	//      414 `INVEN_ITEMNAME`  → 0x800000 (обводка)   — item_inventory.c:35392
	//      418 `INVEN_SOLLTXT00` → 0x200000 (жирный)    — item_inventory.c:35317
	//   плюс `CControl_SetField923(pInvenWnd[65], -12088593)` = цвет 0xFF478AEF у 418.
	if (FAsdaInfoTextStyle* S = InfoStyles.Find(FName(TEXT("INVEN_ITEMNAME"))))
	{
		FSlateFontInfo F = S->Style.Font;
		F.OutlineSettings.OutlineSize  = 1;
		F.OutlineSettings.OutlineColor = FLinearColor::Black;
		S->Style.SetFont(F);
	}
	if (FAsdaInfoTextStyle* S = InfoStyles.Find(FName(TEXT("INVEN_SOLLTXT00"))))
	{
		// ★ 2026-09-24: флаг 926 = 0x200000 — ПСЕВДО-жирный (второй проход глифа ±1 px), а НЕ
		//   Bold-гарнитура: жирного начертания в оригинале нет (разбор — `AsdaUi::ApplyPseudoBold`).
		const FLinearColor SollCol = AsdaUi::SrgbU32ToLinear(kColSollTxt00);
		S->Style.SetColorAndOpacity(FSlateColor(SollCol));
		FVector2D PbOff; FLinearColor PbCol;
		AsdaUi::PseudoBoldShadow(SollCol, PbOff, PbCol);
		S->Style.SetShadowOffset(PbOff);
		S->Style.SetShadowColorAndOpacity(PbCol);
	}
	UE_LOG(LogAsdaItemTip, Log, TEXT("[инфо-панель] эталонные правки раскладки применены (463 +72Y, 488/489 +14Y, 414 обводка, 418 жирный)"));
}

void UAsdaItemTooltipSubsystem::BindInfoPanel(UUserWidget* InWindow)
{
	// Новое окно (пересоздали WBP) ⇒ эталонные сдвиги надо применить заново; то же окно ⇒ нельзя,
	//   иначе сдвинем контролы ВТОРОЙ раз.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (ревизия В2 дедупа №2): «новое окно» — против окна, к которому сдвиги УЖЕ применены
	//   (`InfoLayoutWindow`, сбросом сессии не трогается), а не против `InvenWindow`: сброс сессии без смены мира обнуляет
	//   `InvenWindow`, окно INVEN_WIN при этом то же — прежде сдвиги 488/489 (+14) повторялись на каждом сбросе.
	const bool bNewWindow = (InfoLayoutWindow.Get() != InWindow);
	InvenWindow = InWindow;
	if (bNewWindow) { bInfoLayoutFixed = false; InfoLayoutWindow = InWindow; }
	bInfoBound = (InvenWindow != nullptr);
	UE_LOG(LogAsdaItemTip, Log, TEXT("[инфо-панель] привязка к окну инвентаря: %s"), bInfoBound ? TEXT("ОК") : TEXT("НЕТ ОКНА"));
	if (bInfoBound)
	{
		// ⚠ ПОРЯДОК: снять оформление НАДО до первого `SetInfoText` — он подменяет UTextBlock на rich.
		CaptureInfoStyles();
		ApplyEtalonInfoLayout();
		// ★ Д3 (КРУГ 5): ужать по рамке ПОСЛЕ сдвигов раскладки — иначе считали бы старые координаты.
		ClampInfoTextToFrame();
		BindInfoPlanes();
		ClearInfoPanel();
	}
}

UWidget* UAsdaItemTooltipSubsystem::Info(const TCHAR* Name) const
{
	return InvenWindow ? InvenWindow->GetWidgetFromName(FName(Name)) : nullptr;
}

// ★★★ Планы (image-контролы) точного имени НЕ имеют: импортёр JUI зовёт их `{idc}_{ctrlIdx}_{planeJ}`
//   (закон `jui-importer-widget-naming`). Ищем по префиксу `{idc}_` + хвосту `_{planeJ}`, при этом
//   середина (ctrlIdx) должна быть числом — иначе `INVEN_EQUIICON_` поймал бы ещё и
//   `INVEN_EQUIICON_Base_107_0`, и `INVEN_EQUIICON_BAR_110_0`.
UImage* UAsdaItemTooltipSubsystem::InfoPlane(const TCHAR* Idc, int32 PlaneIdx) const
{
	if (!InvenWindow || !InvenWindow->WidgetTree) { return nullptr; }
	const FString Prefix = FString(Idc) + TEXT("_");
	const FString Suffix = FString::Printf(TEXT("_%d"), PlaneIdx);
	UImage* Found = nullptr;
	InvenWindow->WidgetTree->ForEachWidget([&](UWidget* W)
	{
		if (Found) { return; }
		UImage* Img = Cast<UImage>(W);
		if (!Img) { return; }
		const FString N = Img->GetName();
		if (!N.StartsWith(Prefix) || !N.EndsWith(Suffix)) { return; }
		// Между префиксом и хвостом должен остаться ровно числовой индекс контрола.
		const FString Mid = N.Mid(Prefix.Len(), N.Len() - Prefix.Len() - Suffix.Len());
		if (Mid.IsEmpty() || !Mid.IsNumeric()) { return; }
		Found = Img;
	});
	return Found;
}

// Привязка планов панели. Зовётся из `BindInfoPanel` — до первого заполнения.
void UAsdaItemTooltipSubsystem::BindInfoPlanes()
{
	IconPlane = DuriBack = DuriFill = CondBack = CondFill = nullptr;
	DuriFullW = DuriLeft = CondFullW = CondLeft = 0.f;
	if (!InvenWindow) { return; }

	IconPlane = InfoPlane(TEXT("INVEN_EQUIICON"), 0);          // 423
	DuriBack  = InfoPlane(TEXT("INVEN_EQUIICON_BAR"), 0);      // 10008 ES0 (подложка)
	DuriFill  = InfoPlane(TEXT("INVEN_EQUIICON_BAR"), 1);      // 10008 ES1 (заливка)
	CondBack  = InfoPlane(TEXT("INVEN_INVASIONGAUGE"), 0);     // 424 ES0
	CondFill  = InfoPlane(TEXT("INVEN_INVASIONGAUGE"), 1);     // 424 ES1

	// «100 %» = ширина ПОДЛОЖКИ; если её нет (скин с одним планом) — ширина самой заливки.
	auto Measure = [](const UImage* Back, const UImage* Fill, float& OutFullW, float& OutLeft)
	{
		const UWidget* Ref = Back ? static_cast<const UWidget*>(Back) : static_cast<const UWidget*>(Fill);
		if (const UCanvasPanelSlot* S = Ref ? AsdaUi::SlotOf(Ref) : nullptr)
		{
			OutFullW = S->GetOffsets().Right;   // POINT-анкор импортёра: R/B = размер
		}
		if (const UCanvasPanelSlot* FS = Fill ? AsdaUi::SlotOf(Fill) : nullptr)
		{
			OutLeft = FS->GetOffsets().Left;
		}
	};
	Measure(DuriBack, DuriFill, DuriFullW, DuriLeft);
	Measure(CondBack, CondFill, CondFullW, CondLeft);

	UE_LOG(LogAsdaItemTip, Log,
		TEXT("[инфо-панель] планы: иконка 423=%s · прочность 10008 ES0=%s/ES1=%s (ширина %.0f) · состояние 424 ES0=%s/ES1=%s (ширина %.0f)"),
		IconPlane ? TEXT("да") : TEXT("НЕТ"),
		DuriBack ? TEXT("да") : TEXT("НЕТ"), DuriFill ? TEXT("да") : TEXT("НЕТ"), DuriFullW,
		CondBack ? TEXT("да") : TEXT("НЕТ"), CondFill ? TEXT("да") : TEXT("НЕТ"), CondFullW);
}

// Видимость плана. SelfHitTestInvisible: планы панели ничего не должны отбирать у мыши —
//   наведение на полосу прочности инвентарь ловит опросом курсора, а не хит-тестом виджета.
// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
static void SetVisPlane(UWidget* W, bool bShow)
{
	AsdaUi::SetShownDecor(W, bShow);
}

// Клип заливки ES1: закон `ui-progressbar-animate-existing` — двигаем ширину слота плана,
//   а не подменяем материал/браш.
void UAsdaItemTooltipSubsystem::ClipBarFill(UImage* Fill, float BaseLeft, float FullW, float Frac)
{
	// ★ Волна 3: тело перенесено без изменений в общую точку `FAsdaGaugeFill::Clip`.
	FAsdaGaugeFill::Clip(Fill, BaseLeft, FullW, Frac);
}

// ★ ИКОНКА 423 (`ItemTooltip_PopulateInfoPanel`, item_inventory.c:36781/36807).
//   Эталон рисует В ПЛАН контрола прямоугольник атласа:
//     • запись ВТОРОЙ таблицы  — `Grid_IndexToCellOffset8x64(iconCell)` + `iconPath`, src 64×64;
//     • обычный предмет        — `GetImgOptionPtr/Value/Mask` над item_img_table, src 32 или 64
//       (маска = 32, если у записи изображения флаг «мелкая иконка»).
//   `planeFrame_setUV(u, v, u+w, v+h)` задаёт ИСТОЧНИК, приёмник — собственный прямоугольник
//   контрола (64×64), т.е. мелкая иконка растягивается. Ровно это и делает наш браш:
//   `ImageSize` = размер плана, `UVRegion` = ячейка атласа. Разрешение ячейки — общий
//   `GetItemIcon` (он же у ячеек сумки, хотбара и списка удочек: три пространства кодов).
bool UAsdaItemTooltipSubsystem::ApplyInfoIcon(int32 ItemCode)
{
	if (!IconPlane || ItemCode <= 0) { return false; }
	if (!Items()) { return false; }
	// Размер приёмника = размер самого плана из .JUI (64×64), не размер ячейки-источника.
	FVector2D DrawSize(64.f, 64.f);
	if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(IconPlane))
	{
		const FMargin O = S->GetOffsets();
		if (O.Right > 1.f && O.Bottom > 1.f) { DrawSize = FVector2D(O.Right, O.Bottom); }
	}
	// ★ Волна 1 (14.09.2026, В1): атлас, UV и заворот по модулю (D3DTADDRESS_WRAP) — общая точка.
	if (!AsdaUi::ApplyItemIcon(GetGameInstance(), IconPlane, ItemCode, DrawSize)) { return false; }
	IconPlane->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	return true;
}

void UAsdaItemTooltipSubsystem::ClearInfoIcon()
{
	if (!IconPlane) { return; }
	FSlateBrush B = IconPlane->GetBrush();
	B.SetResourceObject(nullptr);
	B.DrawAs = ESlateBrushDrawType::NoDrawType;
	IconPlane->SetBrush(B);
}

// ★★★ Д1 (КРУГ 5): свернуть правое окно доп.информации.
//   Владелец окна — `UAsdaInventorySubsystem` (ширина окна 648 ↔ 228, кнопки-стрелки 408/412);
//   правим ТОЛЬКО через его публичный API `SetPanelExpanded(false, /*bFromUser=*/false)`, файл
//   инвентаря не трогаем. ⚠ Клик по ячейке зовёт мост `FillInfoPanel` РАНЬШЕ, чем свой
//   `AutoExpandOnSelect` (AsdaInventorySubsystem.cpp:2640 и 2648), поэтому одной свёртки мало —
//   ставим флаг и повторяем на ближайшем `Tick`.
void UAsdaItemTooltipSubsystem::CollapseInfoPanelWindow()
{
	bPendingPanelCollapse = true;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>())
		{
			if (Inv->IsPanelExpanded()) { Inv->SetPanelExpanded(false, /*bFromUser=*/false); }
		}
	}
}

// ★★★ Д1 (КРУГ 6): панель НАПОЛНЕНА — свёртку, назначенную на этот кадр, отменяем.
//   Порядок внутри одного клика: `ClearInfoPanel` (гасит поля и ставит свёртку) → наполнение →
//   `AutoExpandOnSelect` (раскрывает). Без отмены отложенная свёртка схлопнула бы ЖИВУЮ панель.
void UAsdaItemTooltipSubsystem::MarkInfoPanelFilled()
{
	bInfoPanelFilled      = true;
	bPendingPanelCollapse = false;
}

// ★★★ Д1 (КРУГ 6): ЗАПОМНИТЬ ИСТОЧНИК показанного предмета.
//   Нужен, чтобы заметить пропажу выбора там, где инвентарь наружу ничего не сообщает:
//     • прокрутка сетки — `SetScrollRow` сбрасывает `SelCell = −1` (AsdaInventorySubsystem.cpp:1087),
//       но панель НЕ чистит, и на экране остаётся описание предмета, который уже не выбран;
//     • приход пакета — предмет из показанной ячейки может исчезнуть (см. `OnInventoryCellChanged`).
//   Зовётся ПОСЛЕ `FillInfoPanel`: внутри него есть ветки с `ClearInfoPanel`, которые источник сбрасывают.
void UAsdaItemTooltipSubsystem::NoteInfoPanelSource(int32 Container, int32 Cell)
{
	if (!bInfoPanelFilled) { return; }
	InfoPanelSrcCont = Container;
	InfoPanelSrcCell = Cell;
	// У НАДЕТОГО слота (контейнер 3) прокрутки нет — ряд не сторожим.
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	InfoPanelSrcRow = (Container == 3 || !Inv) ? -1 : Inv->GetScrollRow();
}

// ★★★ Д1 (КРУГ 6): ПРЕДМЕТ В ПОКАЗАННОЙ ЯЧЕЙКЕ ИЗМЕНИЛСЯ ПАКЕТОМ.
//   Подписка на `UAsdaInventorySubsystem::OnCellChanged` (Container == −1 ⇒ «перестроить всё»
//   после батчей 4045/4048/4049). Если ячейка опустела — показывать больше нечего, панель гасим и
//   сворачиваем. Наполнять её заново НЕ пытаемся: эталон панель по пакету не перерисовывает
//   (наполнение живёт только в `CInventoryUI_OnSlotClick`), выдумывать поведение нельзя.
void UAsdaItemTooltipSubsystem::OnInventoryCellChanged(int32 Container, int32 Cell)
{
	if (!bInfoPanelFilled || InfoPanelSrcCont < 0) { return; }
	if (Container != -1 && (Container != InfoPanelSrcCont || Cell != InfoPanelSrcCell)) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }
	const FAsdaItemInstance* It = Inv->GetCell(InfoPanelSrcCont, InfoPanelSrcCell);
	if (It && !It->IsEmpty()) { return; }
	UE_LOG(LogAsdaItemTip, Log,
		TEXT("[инфо-панель] предмет источника (контейнер %d ячейка %d) исчез после пакета — панель очищена и свёрнута"),
		InfoPanelSrcCont, InfoPanelSrcCell);
	ClearInfoPanel();
}

// ★★★ Д1 (КРУГ 6): ИНВАРИАНТ «НЕТ ВЫБРАННОГО ПРЕДМЕТА ⇒ ПАНЕЛЬ СВЁРНУТА» — покадровая доводка.
//   Раскрыть панель САМИ могут ровно два места инвентаря (оба с `bFromUser = false`):
//     • `SetTab` → `SetPanelExpanded(CurrentTab == 1, false)` (AsdaInventorySubsystem.cpp:1073) —
//       это и есть жалоба пользователя;
//     • `AutoExpandOnSelect` (там же:1040) — только по НЕПУСТОЙ ячейке.
//   Оба зовутся ПОСЛЕ моста в тултип, поэтому свёртка обязана быть отложенной на кадр.
//   Дополнительно здесь ловятся два случая «выбор снялся, а панель осталась»: смена вкладки и
//   прокрутка сетки (инвентарь сбрасывает `SelCell`, но панель не чистит).
void UAsdaItemTooltipSubsystem::EnforceInfoPanelSelectionRule()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { bPendingPanelCollapse = false; return; }

	if (CVarLegacyInfoPanelNoSelection.GetValueOnGameThread() == 0
	    && bInfoPanelFilled && InfoPanelSrcCont == 1 && Inv->IsWindowOpen())
	{
		const bool bTabLost = (Inv->GetTab() != 1);                                        // ушли с «Снаряжения»
		const bool bRowLost = (InfoPanelSrcRow >= 0 && Inv->GetScrollRow() != InfoPanelSrcRow);
		if (bTabLost || bRowLost)
		{
			UE_LOG(LogAsdaItemTip, Log,
				TEXT("[инфо-панель] выбор снят (%s) — панель очищена и свёрнута (требование пользователя, откат asda.ItemTip.LegacyInfoPanelNoSelection 1)"),
				bTabLost ? TEXT("сменили вкладку") : TEXT("прокрутили сетку"));
			ClearInfoPanel();   // он же поставит отложенную свёртку
		}
	}

	if (bPendingPanelCollapse)
	{
		bPendingPanelCollapse = false;
		if (Inv->IsPanelExpanded()) { Inv->SetPanelExpanded(false, /*bFromUser=*/false); }
	}
}

// ★★★ Д5 (КРУГ 5, доработка): НЕЗАВИСИМЫЙ ХИТ-ТЕСТ СЕТКИ.
//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): сетка стала 12 × 5 = 60 контролов `INVEN_SET0…59` (`kGridCells`/`kGridCols`),
//   абсолютная ячейка = видимая + 12·страница. Ниже — описание ЭТАЛОНА (6 × 5).
//   Сетка сумки — 30 контролов `INVEN_SET0…29`, IDNum 10010…10039 (проверено по дампу .JUI: имя и
//   IDNum совпадают один в один, `INVEN_SET{N}` ⇔ 10010+N, 6 колонок × 5 строк, шаг 33 px, размер
//   32×32 — то есть между ячейками зазор 1 px и НИ ОДНА пара не перекрывается). Абсолютная ячейка
//   эталона — `(idnum − 10010) + 6·страница`. Хит-тестим ПЛАН иконки `INVEN_SET{N}_{idx}_0`
//   (закон `jui-importer-widget-naming`), тем же способом, что и инвентарь.
int32 UAsdaItemTooltipSubsystem::HitTestGridCellVisual() const
{
	if (!InvenWindow) { return -1; }
	for (int32 N = 0; N < UAsdaInventorySubsystem::kGridCells; ++N)   // ★ 29.10.2026 (инвентарь ×2): 60 контролов
	{
		const FString Idc = FString::Printf(TEXT("INVEN_SET%d"), N);
		const UWidget* W = InfoPlane(*Idc, 0);
		if (W && IsCursorOverWidget(W)) { return N; }
	}
	return -1;
}

// ★★★ Д4 (КРУГ 5): ПОДСКАЗКА ПРИ НАВЕДЕНИИ НА ГНЁЗДА САМОЦВЕТОВ ПРАВОЙ ПАНЕЛИ (441…444).
//   Это ЭТАЛОННОЕ поведение, а не добавка: `invSlotBuildTooltip@0xD06670`, последняя ветка
//   (item_inventory.c:37484-37492):
//     if (cmdId < 0x1B9 || cmdId > 0x1BC) return 0;            // 441…444
//     statSlotIdx = cmdId % 0x1B9;                             // 0…3 — номер ГНЕЗДА
//     code = m_bStatFromWord2901960 == 1
//          ? *(int16*)((char*)&word_2901960 + 57*m_nSelBagSlot + 2*statSlotIdx)   // выбранная ячейка сумки
//          : *(int16*)&LocalPlayer->gap_636[57*m_nStatSlotIdx + 8 + 2*statSlotIdx]; // надетый слот
//     if (code <= 0) return 0;
//     if (GetSowelRecord(code)) CControl_SetState(&g_ItemTooltip, 1, code, 0, 1, -1, 0);
//   `word_2901960` — авто-имя IDA для `g_InventoryBag + 16`, поэтому байтовое смещение
//   `57*слот + 2*i` даёт ровно гнёзда записи +16/+18/+20/+22 (закон `globals-tsv-offset-shift`:
//   смещение считаем сами, имя — только подсказка). Вид подсказки 1 у `CControl_SetState` — это
//   `CItem_BuildSowelTooltipText`, то есть уже портированный `BuildTooltipSowel`.
//   Коды гнёзд кэшируются в `FillInfoPanel` — там же, где эталон держит `m_nSelBagSlot`.
void UAsdaItemTooltipSubsystem::TickInfoPanelSockets()
{
	if (!InvenWindow) { return; }
	UAsdaTipPanel* P = GetMainPanel();
	if (!P) { return; }
	// Гнёзда живут в правой панели — опрашиваем их, только пока окно открыто и панель раскрыта.
	if (UGameInstance* GI = GetGameInstance())
	{
		const UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
		if (!Inv || !Inv->IsWindowOpen() || !Inv->IsPanelExpanded())
		{
			// ★ 31.10.2026 [ОРИГИНАЛ]: окно закрыто или панель свёрнута, а курсор с гнезда не уходил — прежде плашка гнезда
			//   оставалась на экране (сброс состояния без гашения). Эталон при закрытии окна гасит подсказку (`sowelWindowClose`).
			if (PanelSocketHover >= 0) { HideTooltipFor(EAsdaTipOwner::InfoSocket); }
			PanelSocketHover = -1;
			return;
		}
	}
	// ★ 21.10.2026: камень «в руке» (перенос из гнезда/списка, `UAsdaSowelSubsystem::BeginStoneDrag`) — подсказку
	//   гнезда под курсором не показываем, как инвентарь не показывает подсказку ячейки во время переноса.
	if (FAsdaDragGhost::IsAnyActive())
	{
		if (PanelSocketHover >= 0) { HideTooltipFor(EAsdaTipOwner::InfoSocket); }
		PanelSocketHover = -1;
		return;
	}

	// ★ [ОРИГИНАЛ] 07.11.2026: под открытой модалкой эталон наведение окнам не отдаёт (`ui_core.c:104149-104153`).
	if (CVarSowelHoverTip.GetValueOnGameThread() != 0 && AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (PanelSocketHover >= 0) { HideTooltipFor(EAsdaTipOwner::InfoSocket); }
		PanelSocketHover = -1;
		return;
	}

	// Гнездо под курсором. Кликабельного виджета у гнезда нет — хит-тестим ПЛАН иконки
	//   `INVEN_SOWELL{i}_{idx}_0` (закон `jui-importer-widget-naming`), а если его нет — сам контрол.
	int32 Over = -1;
	for (int32 i = 0; i < 4; ++i)
	{
		const FString Idc = FString::Printf(TEXT("INVEN_SOWELL%d"), i);
		const UWidget* W = InfoPlane(*Idc, 0);
		if (!W) { W = Info(*Idc); }
		if (AsdaUi::IsShown(W) && IsCursorOverWidget(W))   // ★ волна В6 ремесла (МЕЛК-8): общая точка видимости узла
		{
			Over = i;
			break;
		}
	}
	// ★★★ [ОРИГИНАЛ] 07.11.2026 (баг владельца «наведение на совел в Socketable sowel не показывает описание»): та же функция
	//   эталона `invSlotBuildTooltip@0xD06670`, ветка 0x1AE…0x1B3 (item_inventory.c:37470-37479): индекс = колонка + 6·страница,
	//   запись списка есть и код > 0 → `CControl_SetState(&g_ItemTooltip, 1, код, 0, 1, −1, 0)` — та же подсказка камня, что у
	//   гнёзд. Порт лоток не опрашивал. Ключ `Over` у лотка = kTrayHoverBase + КОД камня: лист страницы или пересборка списка под
	//   неподвижным курсором даёт другой код ⇒ подсказка обновится сама.
	static constexpr int32 kTrayHoverBase = 1000;
	if (Over < 0 && CVarSowelHoverTip.GetValueOnGameThread() != 0)
	{
		UGameInstance* TrayGI = GetGameInstance();
		const UAsdaSowelSubsystem* Sw = TrayGI ? TrayGI->GetSubsystem<UAsdaSowelSubsystem>() : nullptr;
		if (Sw)
		{
			const TArray<FAsdaSowelListEntry>& List = Sw->GetList();
			for (int32 Col = 0; Col < 6; ++Col)
			{
				const int32 Idx = Col + 6 * Sw->GetListPage();
				const UImage* Ic = Sw->DebugTrayIcon(Col);
				if (List.IsValidIndex(Idx) && List[Idx].Code > 0 && AsdaUi::IsShown(Ic) && IsCursorOverWidget(Ic))
				{
					Over = kTrayHoverBase + List[Idx].Code;
					break;
				}
			}
		}
	}
	// ⚠ Пересобирать текст на КАЖДОМ кадре не надо, но и «показали один раз» мало: инвентарь на
	//   переходе «курсор ушёл с ячейки» зовёт `HideCellTooltip` (AsdaInventorySubsystem.cpp:2584),
	//   и порядок тиков подсистем не задан — плашку могут погасить уже ПОСЛЕ нашего показа.
	//   Поэтому пока курсор на гнезде, восстанавливаем показ, если плашка вдруг скрыта.
	if (Over == PanelSocketHover
	    && (Over < 0 || (P->IsShown() && P->GetOwner() == EAsdaTipOwner::InfoSocket))) { return; }
	PanelSocketHover = Over;

	if (Over < 0)
	{
		// Уходим с гнезда — гасим ТОЛЬКО свою подсказку (закон `ui-shared-tip-ownership`).
		//   Владелец у гнёзд ОТДЕЛЬНЫЙ (`InfoSocket`): если инвентарь уже успел показать свой
		//   тултип ячейки (владелец `Item`), наш `HideFor` окажется no-op — гонки тиков нет.
		HideTooltipFor(EAsdaTipOwner::InfoSocket);
		return;
	}
	const int32 Code = (Over >= kTrayHoverBase) ? Over - kTrayHoverBase : PanelSowelCode[Over];
	UAsdaItemTableSubsystem* IT = Items();
	// `if (code <= 0) return 0;` + `if (GetSowelRecord(code))` — обе проверки эталона.
	if (Code <= 0 || !IT || !IT->IsSowelCode(Code)) { return; }
	FAsdaTipMetrics M;
	const FAsdaItemTooltipText T = BuildTooltipSowel(Code, M);
	if (!T.bValid)
	{
		UE_LOG(LogAsdaItemTip, Warning, TEXT("[инфо-панель] гнездо %d: камень %d — текст не собран"), Over, Code);
		return;
	}
	bCompareShown = false;
	if (ComparePanel) { ComparePanel->Hide(); }
	P->SetOwner(EAsdaTipOwner::InfoSocket);
	P->SetSourceWindow(TipSourceWindowUnderCursor(GetGameInstance(), P, ComparePanel));   // ★ 31.10.2026: окно-источник показа
	P->Show(T.Title, T.TitleArgb, T.Body, M);
	P->LayoutAtCursor();
	LogShow(TEXT("гнездо панели"), Code, T.Title, T.Body, P);
}

void UAsdaItemTooltipSubsystem::SetInfoText(const TCHAR* Name, const FString& Markup, bool bShow)
{
	UWidget* W = Info(Name);
	if (!W) { return; }
	// Текст инфо-панели тоже несёт inline-теги (цвета «нельзя по уровню», «не передаётся» и т.п.),
	//   но БАЗА — собственное оформление контрола из JUI (кегль/цвет/обводка/выравнивание/перенос).
	//   Без этого rich-контрол рисует всё строкой `Default` (10 px, БЕЛЫЙ, влево) — см. CaptureInfoStyles.
	FAsdaRichApplyOpts Opts;
	FTextBlockStyle    Fitted;                 // копия базового стиля с ужатым кеглем (живёт до вызова)
	if (const FAsdaInfoTextStyle* S = InfoStyles.Find(FName(Name)))
	{
		Opts.BaseStyle   = &S->Style;
		Opts.bSetJustify = true;
		Opts.Justify     = S->Justify;
		Opts.bWrap       = S->bWrap;
		// ★★★ Д3 (КРУГ 5): ОДНОСТРОЧНЫЙ контрол ужимает КЕГЛЬ, пока строка не влезет в свою рамку.
		//   Эталон здесь просто резал хвост рамкой rect'а (`TextOutLine = 1` ⇒ нет DT_NOCLIP), а у
		//   контрола высотой 14 px «перенос» прячет вторую строку целиком. Пользователь просил,
		//   чтобы «красиво отображалось всё», поэтому вместо потери хвоста уменьшаем кегль.
		//   Многострочное описание (422) не трогаем — оно переносится по словам, как в эталоне.
		if (!S->bMultiLine && S->MaxWidth > 1.f && !Markup.IsEmpty()
		    && CVarLegacyInfoOverflow.GetValueOnGameThread() == 0)
		{
			const FString Plain = FAsdaMarkup::StripTags(Markup);
			const int32 Size = FitFontSizeToWidth(Plain, S->Style.Font, S->MaxWidth);
			if (Size != (int32)S->Style.Font.Size)
			{
				Fitted = S->Style;
				FSlateFontInfo F = Fitted.Font;
				F.Size = Size;
				Fitted.SetFont(F);
				Opts.BaseStyle = &Fitted;
			}
		}
	}
	if (UWidget* Rich = UAsdaRichText::ApplyFormatted(W, Markup, &Opts)) { W = Rich; }
	// ★★★ КЛИП ПО РАМКЕ КОНТРОЛА. Эталон рисует текст контрола с флагом 0x100 = DT_NOCLIP только
	//   при `TextOutLine != 1`; при `TextOutLine == 1` флага NOCLIP НЕТ, то есть текст ОБРЕЗАЕТСЯ
	//   рамкой rect (ui_core.c:110743, разбор — в `CaptureInfoStyles`). UMG по умолчанию не режет
	//   ничего, поэтому длинная строка уезжала за край панели и наезжала на соседнюю.
	if (const FAsdaInfoTextStyle* S = InfoStyles.Find(FName(Name)))
	{
		W->SetClipping(S->bWrap ? EWidgetClipping::ClipToBounds : EWidgetClipping::Inherit);
	}
	const ESlateVisibility Vis = (bShow && !Markup.IsEmpty()) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	W->SetVisibility(Vis);
	SetInfoGroupVis(Name, Vis);   // ★ 21.10.2026: планы контрола гаснут/показываются вместе с текстом
}

void UAsdaItemTooltipSubsystem::SetInfoVis(const TCHAR* Name, bool bShow)
{
	const ESlateVisibility Vis = bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (UWidget* W = Info(Name))
	{
		W->SetVisibility(Vis);
	}
	SetInfoGroupVis(Name, Vis);   // ★ 21.10.2026
}

void UAsdaItemTooltipSubsystem::SetInfoGroupVis(const TCHAR* Name, ESlateVisibility Vis)
{
	// Контрол .JUI → группа импортёра с его планами. Только те контролы, что ИМЕЮТ свои планы (дамп дерева
	//   `Reports/_inven_widget_tree.txt`): плашки самоцветов 419/420/421 и 479 (у 479 текст назван импортёром
	//   `INVEN_SOLLTXT04_1` — idc в .JUI дублирован с 497, — а группа осталась `INVEN_SOLLTXT04_grp`), полоса 477 и
	//   заголовки 426/427/428. ⚠ Дети 477 (488/489 `INVEN_Primium_Option_text/2`) лежат в той же группе — сюда они
	//   НЕ входят: их видимость не должна гасить полосу родителя.
	static const TCHAR* const kGroupOf[][2] = {
		{ TEXT("INVEN_SOLLTXT01"),      TEXT("INVEN_SOLLTXT01_grp") },        // 419
		{ TEXT("INVEN_SOLLTXT02"),      TEXT("INVEN_SOLLTXT02_grp") },        // 420
		{ TEXT("INVEN_SOLLTXT03"),      TEXT("INVEN_SOLLTXT03_grp") },        // 421
		{ TEXT("INVEN_SOLLTXT04_1"),    TEXT("INVEN_SOLLTXT04_grp") },        // 479
		{ TEXT("INVEN_Primium_Option"), TEXT("INVEN_Primium_Option_grp") },   // 477 (+ дети 488/489)
		{ TEXT("Inven_NormalOption"),   TEXT("Inven_NormalOption_grp") },     // 426 (+ опции 10000/10001)
		{ TEXT("Inven_UpgadingOption"), TEXT("Inven_UpgadingOption_grp") },   // 427
		{ TEXT("Inven_CraftingOption"), TEXT("Inven_CraftingOption_grp") },   // 428
	};
	if (!Name) { return; }
	for (const auto& P : kGroupOf)
	{
		if (FCString::Strcmp(P[0], Name) != 0) { continue; }
		if (UWidget* G = Info(P[1])) { G->SetVisibility(Vis); }
		return;
	}
}

void UAsdaItemTooltipSubsystem::ClearInfoPanel()
{
	// ★★★ Д1 (КРУГ 6): ЕДИНАЯ ТОЧКА «ПАНЕЛЬ ПУСТА». Сюда сходятся ВСЕ пути, где выбор пропадает:
	//   открытие окна (AsdaInventorySubsystem.cpp:939), закрытие (:969), смена вкладки (:1064),
	//   клик по ПУСТОЙ ячейке и по пустому надетому слоту (мост `FillInfoPanel`), предмет не
	//   оружие/броня/щит (Д1 круга 5), нерезолвнутый код, пропажа предмета по пакету.
	//   Раз панель пуста — окно обязано быть свёрнуто. Состояние сбрасываем ДО early-return по
	//   `InvenWindow`: окно инвентаря может быть ещё не создано, а свёртка от него не зависит.
	bInfoPanelFilled = false;
	InfoPanelSrcCont = -1;
	InfoPanelSrcCell = -1;
	InfoPanelSrcRow  = -1;
	if (CVarLegacyInfoPanelNoSelection.GetValueOnGameThread() == 0)
	{
		CollapseInfoPanelWindow();
	}
	if (!InvenWindow) { return; }
	// Эталон `uiInventoryDetail_Reset@0xD17300` сбрасывает СОСТОЯНИЕ окна (вкладка/страница) — это
	//   зона инвентаря; здесь очищаются только ТЕКСТЫ и картинки панели.
	static const TCHAR* kTexts[] = {
		TEXT("INVEN_ITEMNAME"), TEXT("INVEN_JOB"), TEXT("INVEN_Grade"), TEXT("INVEN_WEIGHT"),
		TEXT("INVEN_LEVEL"), TEXT("INVEN_LEVEL_2"), TEXT("INVEN_LEVEL_text"),
		TEXT("INVEN_SOLLTXT00"), TEXT("INVEN_SOLLTXT01"), TEXT("INVEN_SOLLTXT02"), TEXT("INVEN_SOLLTXT03"),
		TEXT("INVEN_SOLLTXT04"), TEXT("INVEN_SOLLTXT05"), TEXT("INVEN_SOLLTXT06"),
		TEXT("INVEN_SOLLTXT07"), TEXT("INVEN_SOLLTXT08"), TEXT("INVEN_SOLLTXT09"),
		TEXT("INVEN_ArmoryOp3"), TEXT("INVEN_ArmoryOp4"), TEXT("INVEN_ArmoryOp5"),
		TEXT("INVEN_SHELLCONTENS"), TEXT("INVEN_sowell_attack"), TEXT("INVEN_attack_speed"),
		TEXT("INVEN_Primium_Option_text"), TEXT("INVEN_Primium_Option_text2"),
	};
	for (const TCHAR* N : kTexts) { SetInfoText(N, FString(), false); }
	SetInfoVis(TEXT("Inven_NormalOption"), false);
	SetInfoVis(TEXT("Inven_UpgadingOption"), false);
	SetInfoVis(TEXT("Inven_CraftingOption"), false);
	SetInfoVis(TEXT("INVEN_Primium_Option"), false);
	// ★★★ 479 `INVEN_SOLLTXT04_1` — ТЕКСТА НЕ ПОЛУЧАЕТ НИКОГДА. Во всём экспорте единственное
	//   обращение к контролу — `CControl_SetVisible(pTooltip[69], 0)` (item_inventory.c:37253),
	//   поэтому здесь только гасим (и НЕ подменяем его на rich-контрол пустым текстом).
	//   Прежде порт печатал сюда выдуманную строку «Количество использований» — снято по
	//   закону `no-invented-ui` (2026-08-09).
	SetInfoVis(TEXT("INVEN_SOLLTXT04_1"), false);
	// ★ 423 — иконка гаснет вместе с панелью.
	ClearInfoIcon();
	// ★ 10008 — полоса прочности: эталон при пустом/неподходящем предмете зовёт `SetVisible(0)`.
	SetVisPlane(DuriBack, false);
	SetVisPlane(DuriFill, false);
	// ★ 424 — полоса состояния: обнуляем заливку (видимость её группы задана данными .JUI, см. Fill).
	ClipBarFill(CondFill, CondLeft, CondFullW, 0.f);
	// ★ Д4 (КРУГ 5): панель пуста ⇒ гнёзд у неё тоже нет — подсказку камня строить не из чего.
	for (int32 i = 0; i < 4; ++i) { PanelSowelCode[i] = 0; }
	PanelSocketHover = -1;
}

// ═══════════════ ВЕТКА ЗАПИСЕЙ ВТОРОЙ ТАБЛИЦЫ (`special_itemlist`) ═══════════════════════════════
// Эталон `ItemTooltip_PopulateInfoPanel@0xD02C00`, item_inventory.c:36668-36790 — первая ветка
//   развилки (`if (itemTable2Record_2 && pItemSlot)`), она СТАРШЕ ветки обычного предмета.
//   Раскладка (порядок 1:1 с эталоном):
//     414  имя из `SpecialItem_TooltipList` (цвет: сперва 0xFF004080, в конце — цвет грейда
//          `GetDropColor(dropColorIndex)`, строка 36789);
//     415  «6829 : N» при `flag_57 == 2 && i32(запись+9) > 0`, иначе пусто;
//     463  скрыт;  425 скрыт;  497 скрыт;
//     425/497 показываются ТОЛЬКО при `subType == 204 || 206` (нотисы 108512 / 108513);
//     10005 только при `subType == 15`;  496/416 — уровень+вес либо вес+пусто;
//     423  иконка (сетка 8×64 по `iconCell`, атлас `iconPath`);  10008 скрыт;  422 описание.
//   ⚠ Записи ВТОРОЙ таблицы в `item_list` не резолвятся, поэтому `ItemRecord` тут НЕТ вовсе —
//     ни прочности, ни гнёзд, ни статов; все прочие контролы панели ветка не трогает (см. Д6).
void UAsdaItemTooltipSubsystem::FillInfoPanelSpecial(const FAsdaItemInstance& It,
                                                     const FAsdaSpecialItemlistRow& Sp)
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT) { return; }

	// `sowelTooltipRecord = itemGetTooltipById(code); if (!sowelTooltipRecord) { … }` — при отсутствии
	//   строки подсказки эталон НЕ пишет ни одного контрола. Воспроизводим (панель уже погашена).
	FAsdaSpecialitemTooltiplistRow Tip;
	if (!IT->GetSpecialItemTooltip(It.ItemCode, Tip))
	{
		UE_LOG(LogAsdaItemTip, Verbose,
			TEXT("[инфо-панель] спец-предмет %d: строки SpecialItem_TooltipList нет — эталон панель не заполняет"),
			It.ItemCode);
		return;
	}

	const bool  bLegacy   = (CVarLegacySpecialDefects.GetValueOnGameThread() != 0);
	const uint32 GradeCol = GradeColorArgb(Sp.DropColorIndex);   // `GetDropColor(rec->dropColorIndex)`
	const int32 SubType   = Sp.SubType;

	// 414 — имя. Эталон сначала ставит цвет 0xFF004080, пишет текст, а В КОНЦЕ ветки меняет цвет на
	//   грейдовый (`SetField923(pTooltip[57], gradeColor)`, строка 36789) — итог = цвет грейда.
	SetInfoText(TEXT("INVEN_ITEMNAME"), Colored(GradeCol, Tip.Name));

	// 415 — «Количество использований : N». `flag_57` = байт +57 сырой записи second-таблицы
	//   (столбца у него нет — читаем из `DataHex`, как и остальные «безымянные» поля).
	{
		const int32 Flag57 = SpecialRawByte(Sp.DataHex, 57);
		const int32 Count  = RawI32(It, 9);          // `*(int *)(pItemSlot + 9)`
		const FString Line = (Flag57 == 2 && Count > 0)
			? FString::Printf(TEXT("%s : %d"), *LocRaw(kLocUseCountSpecial), Count)
			: FString();
		SetInfoText(TEXT("INVEN_JOB"), Line, !Line.IsEmpty());
	}

	// 463 / 425 / 497 — эталон гасит все три (36689-36691) и показывает 425/497 только у 204/206.
	SetInfoVis(TEXT("INVEN_attack_speed"), false);
	SetInfoText(TEXT("INVEN_Grade"),      FString(), false);
	SetInfoText(TEXT("INVEN_SOLLTXT04"),  FString(), false);
	if (SubType == 204 || SubType == 206)
	{
		const int32 Enh    = RawU8(It, 24);    // `*(_BYTE *)(pItemSlot + 24)`
		const int32 EnhPct = RawI16(It, 27);   // `Util_Identity16(*(_WORD *)(pItemSlot + 27))`
		SetInfoText(TEXT("INVEN_Grade"),
			Enh ? FString::Printf(TEXT("%s + %d"), *LocRaw(kLocEnhLevelLblSpecial), Enh) : FString(), true);
		SetInfoText(TEXT("INVEN_SOLLTXT04"),
			(EnhPct > 0) ? FString::Printf(TEXT("%s + %d%%"), *LocRaw(kLocEnhPowerLblSpecial), EnhPct) : FString(), true);
	}

	if (SubType == 15)
	{
		// 10005 — «не передаётся». Цвет `v164 = -589824` = 0xFFF70000.
		FString Line;
		if (RawU8(It, 50) != 0)
		{
			Line = Colored(kColInfoBound, LocRaw(kLocNoTrade));                 // 8401
		}
		else
		{
			switch (SpecialRawByte(Sp.DataHex, 124))                         // `flag_124`
			{
				case 1:    Line = Colored(kColInfoBound, LocRaw(kLocNoTradeEquip)); break;   // 8503
				case 2:    Line = Colored(kColInfoBound, LocRaw(kLocNoTradeUse));   break;   // 8504
				case 3:    Line = Colored(kColInfoBound, LocRaw(kLocNoTradeUse2));  break;   // 8508
				case 0x0A: Line = Colored(kColInfoBound, LocRaw(kLocNoTrade));      break;   // 8401
				// ★ Д7: у эталона тут `default:` без записи в буфер ⇒ в контрол уходит ИМЯ предмета.
				default:   Line = bLegacy ? Colored(GradeCol, Tip.Name) : FString(); break;
			}
		}
		SetInfoText(TEXT("INVEN_SOLLTXT07"), Line, !Line.IsEmpty());

		// 496 — «Уровень : N минимум» (красным, если персонаж младше `itemLevel` записи).
		{
			const FString L = FString::Printf(TEXT("%s : %d %s"),
				*LocRaw(kLocLevelLabel), Sp.ItemLevel, *LocRaw(kLocLevelSuffix));
			SetInfoText(TEXT("INVEN_LEVEL_2"), (SelfLevel() < Sp.ItemLevel) ? Colored(kColRequire, L) : L);
		}
		// 416 — вес.
		SetInfoText(TEXT("INVEN_WEIGHT"),
			FString::Printf(TEXT("%s : %d"), *LocRaw(kLocWeight), RawI16(It, 14)));
	}
	else
	{
		// ★ У НЕ-15 подтипов вес уходит в 496, а 416 остаётся ПУСТЫМ (эталон 36774-36778) —
		//   контролы «уровень» и «вес» меняются местами. Это не опечатка порта.
		SetInfoText(TEXT("INVEN_LEVEL_2"),
			FString::Printf(TEXT("%s : %d"), *LocRaw(kLocWeight), RawI16(It, 14)));
		SetInfoText(TEXT("INVEN_WEIGHT"), FString(), false);
	}

	// 423 — иконка: `Grid_IndexToCellOffset8x64(iconCell)` + атлас `iconPath`, источник 64×64.
	//   `GetItemIcon` уже знает про пространство кодов second-таблицы и отдаёт ровно эту ячейку.
	if (!ApplyInfoIcon(It.ItemCode)) { ClearInfoIcon(); }
	// 10008 — полоса прочности у записей second-таблицы ВСЕГДА скрыта (36786).
	SetVisPlane(DuriBack, false);
	SetVisPlane(DuriFill, false);
	// 422 — описание из той же строки подсказки.
	SetInfoText(TEXT("INVEN_SHELLCONTENS"), Tip.Tooltip);

	UE_LOG(LogAsdaItemTip, Verbose,
		TEXT("[инфо-панель] спец-предмет %d (subType %d, грейд-цвет %08X) — ветка special_itemlist заполнена"),
		It.ItemCode, SubType, GradeCol);
}

void UAsdaItemTooltipSubsystem::FillInfoPanel(const FAsdaItemInstance& It, int32 SourceKind)
{
	if (!InvenWindow) { return; }
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || It.IsEmpty()) { ClearInfoPanel(); return; }

	// Эталон гасит всплывающий тултип, когда наполняет панель (`CControl_KillFocus(&g_ItemTooltip)`).
	HideTooltip();

	// ★★★ Д1 (КРУГ 5) — ТРЕБОВАНИЕ ПОЛЬЗОВАТЕЛЯ, ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА.
	//   «Панель показывает ТОЛЬКО оружие/броню/щит; для остальных — СКРЫВАТЬ окно».
	//   Прошлый круг закрыл лишь гейт по КОНТЕЙНЕРУ (вкладка «Расходники» панель не трогает), но на
	//   вкладке «Снаряжение» лежат и не-экипируемые вещи: живой лог 2026-08-09 показывает в сумке
	//   (контейнер 1) ячейку 3 с кодом 500 «Свиток ремонта брони (30)», кол-во 23 — запись
	//   `special_itemlist`, у которой в `item_list` вообще нет строки. Отсюда и жалоба Д5: клик по
	//   свитку наполнял панель, а последующее НАВЕДЕНИЕ на зелье опыта её не меняло (эталон
	//   наполняет панель по КЛИКУ, `CInventoryUI_OnSlotClick`, а не по наведению) — пользователь
	//   видел «описание от другого предмета». Гасим панель и сворачиваем окно.
	//   Эталон: `asda.ItemTip.LegacyInfoPanelAnyItem 1`.
	if (CVarLegacyInfoPanelAnyItem.GetValueOnGameThread() == 0)
	{
		const FAsdaItemRecord* Probe = IT->GetItem(It.ItemCode);
		if (!Probe || !IsEquipInfoPanelType(Probe->ItemType))
		{
			UE_LOG(LogAsdaItemTip, Log,
				TEXT("[инфо-панель] предмет %d (тип %d) не оружие/броня/щит — панель скрыта (требование пользователя, откат asda.ItemTip.LegacyInfoPanelAnyItem 1)"),
				It.ItemCode, Probe ? Probe->ItemType : -1);
			ClearInfoPanel();
			CollapseInfoPanelWindow();
			return;
		}
	}

	// ★★★ РАЗВИЛКА ЭТАЛОНА (item_inventory.c:36668/36792): СНАЧАЛА проверяется запись ВТОРОЙ таблицы
	//   (`CItemManager_GetTable2Record`), и только `else if` — обычный предмет из `item_list`.
	//   Раньше порт знал лишь вторую половину: `GetItem()` для кодов 1…558 возвращает nullptr, и
	//   панель гасилась целиком — «премиум/бустер-предмет тушит правую панель».
	{
		FAsdaSpecialItemlistRow Sp;
		if (IT->GetSpecialItem(It.ItemCode, Sp))
		{
			// Д6: эталон панель НЕ чистит — гасим сами, иначе на ней остаются строки прошлого предмета.
			if (CVarLegacySpecialDefects.GetValueOnGameThread() == 0) { ClearInfoPanel(); }
			FillInfoPanelSpecial(It, Sp);
			MarkInfoPanelFilled();   // Д1 (КРУГ 6): панель наполнена — отложенную свёртку отменяем
			return;
		}
	}

	const FAsdaItemRecord* R = IT->GetItem(It.ItemCode);
	if (!R) { ClearInfoPanel(); return; }
	MarkInfoPanelFilled();   // Д1 (КРУГ 6): дальше идёт безусловное наполнение — панель показывает предмет

	const int32 Enhance = RawU8(It, 24);
	const int32 EnhPct  = RawI16(It, 27);
	const int32 PlayerLv = SelfLevel();
	const bool  bCharged = (R->ItemSubType == 10 && R->ItemVariant == 1);
	const uint32 GradeCol = GradeColorArgb(R->ItemGrade);

	// 414 — имя (базовый цвет 0xFF004080 → затем цвет грейда; теги заточки перекрывают).
	{
		const FString Name = IT->GetItemName(It.ItemCode);
		FString Line;
		if (Enhance != 0)
		{
			const uint32 EC = EnhanceColorArgb(Enhance);
			Line = (Enhance < 5)
				? FString::Printf(TEXT("<FC=%08X>+%d%%</F> <FC=%08X>%s</F>"), EC, EnhPct, GradeCol, *Name)
				: FString::Printf(TEXT("<FC=%08X>+%d%%</F> <FC=%08X>%s</F> <FC=%08X>%s</F>"),
					EC, EnhPct, EC, *LocRaw(kLocEnhanceNameBase + Enhance), GradeCol, *Name);
		}
		else
		{
			Line = Colored(GradeCol, Name);
		}
		SetInfoText(TEXT("INVEN_ITEMNAME"), Line);
	}
	// 415 — профессия (красным, если класс не подходит).
	{
		const FString Line = FString::Printf(TEXT("%s : %s"), *LocRaw(kLocClassLabel), *LocRaw(kLocClassNameBase + R->UsageCategory));
		SetInfoText(TEXT("INVEN_JOB"), IsUsageCategoryReachable(R->UsageCategory) ? Line : Colored(kColRequire, Line));
	}
	// 425 — грейд. ★ ИСПРАВЛЕНО 2026-08-09: в ЭТОЙ ветке эталона (обычный предмет `item_list`,
	//   item_inventory.c:36871-36875) строка ВСЕГДА «1420 : 1421+itemGrade», без вариантов.
	//   Вариант «Уровень + N» (нотис 108512) существует только в ветке записей ВТОРОЙ таблицы
	//   (`special_itemlist`, item_inventory.c:36692-36705) — порт занёс его сюда по ошибке.
	//   Ветка второй таблицы в инфо-панель пока не портирована (см. отчёт `inventory_port.md`).
	SetInfoText(TEXT("INVEN_Grade"), FString::Printf(TEXT("%s : %s"), *LocRaw(kLocGradeLabel), *LocRaw(kLocGradeNameBase + R->ItemGrade)));
	// ★★★ ТРЕБУЕМЫЙ УРОВЕНЬ → IDNum 496, а его idc в .JUI — `INVEN_LEVEL_2` (НЕ `INVEN_LEVEL`).
	//   ИСПРАВЛЕНО 2026-08-09. Эталон адресует контролы по IDNum:
	//     `pInvenWnd[61] = FindControlByID(496)` ← нотисы 2330/2331 (или 109901/12215 у shell) —
	//        item_inventory.c:35306 и 36913;
	//     `pInvenWnd[62] = FindControlByID(491)` ← нотис 110641 «Уровень возможностей» — 36943;
	//     `pInvenWnd[63] = FindControlByID(476)` ← нотис 110642 «Диапазон уровней» — 36972.
	//   В .JUI: 496 = idc `INVEN_LEVEL_2` (Origin y=195), 491 = idc `INVEN_LEVEL` (y=213),
	//   476 = `INVEN_LEVEL_text` (y=231) — то есть ИМЕНА в данных и имена переменных в эталоне
	//   ПЕРЕПУТАНЫ местами. Порт адресует по имени и до сих пор писал требуемый уровень в 491,
	//   где эталон рисует «уровень возможностей», — отсюда «не та строка» и наезд на `INVEN_attack_speed`.
	{
		const FString Line = (R->CategoryFlag == 3)
			? FString::Printf(TEXT("%s : %d%s "), *LocRaw(kLocShellLevel), R->RequiredLevel, *LocRaw(kLocLevelShort))
			: FString::Printf(TEXT("%s : %d %s"), *LocRaw(kLocLevelLabel), R->RequiredLevel, *LocRaw(kLocLevelSuffix));
		SetInfoText(TEXT("INVEN_LEVEL_2"), (PlayerLv < R->RequiredLevel) ? Colored(kColRequire, Line) : Line);
	}
	// 491 (`INVEN_LEVEL`) / 476 (`INVEN_LEVEL_text`) — «уровень возможностей» и его диапазон
	//   (обе строки из pricetable). Эталон СНАЧАЛА гасит 491 и показывает только при наличии записи.
	{
		FAsdaPriceRow Price;
		const bool bHasPrice = IT->GetPrice(It.ItemCode, Price);
		if (bHasPrice && R->CategoryFlag == 3)
		{
			int32 PowerLv;
			if (PlayerLv > Price.dw10) { PowerLv = (PlayerLv < Price.dw11) ? PlayerLv : Price.dw11; }
			else                       { PowerLv = Price.dw10; }
			if (R->ItemGrade == 1) { PowerLv += 3; }
			if (R->ItemGrade == 2) { PowerLv += 6; }
			if (R->ItemGrade == 3) { PowerLv += 9; }
			if (R->ItemGrade == 4) { PowerLv += 12; }
			SetInfoText(TEXT("INVEN_LEVEL"), FString::Printf(TEXT("%s : %d%s"), *LocRaw(kLocPowerLevel), PowerLv, *LocRaw(kLocLevelShort)));
			SetInfoText(TEXT("INVEN_LEVEL_text"), FString::Printf(TEXT("%s : %d~%d%s"), *LocRaw(kLocPowerRange), Price.dw10, Price.dw11, *LocRaw(kLocLevelShort)));
		}
		else if (bHasPrice)
		{
			SetInfoText(TEXT("INVEN_LEVEL"), FString::Printf(TEXT("%s : %d%s"), *LocRaw(kLocPowerLevel), Price.dw12, *LocRaw(kLocLevelShort)));
			SetInfoText(TEXT("INVEN_LEVEL_text"), FString::Printf(TEXT("%s : %d%s"), *LocRaw(kLocPowerRange), Price.dw12, *LocRaw(kLocLevelShort)));
		}
		else
		{
			SetInfoText(TEXT("INVEN_LEVEL"), FString(), false);
			SetInfoText(TEXT("INVEN_LEVEL_text"), FString(), false);
		}
	}
	// 416 — вес.
	SetInfoText(TEXT("INVEN_WEIGHT"), FString::Printf(TEXT("%s : %d"), *LocRaw(kLocWeight), It.StackWeight));
	// 10007 — заряды/прочность.
	{
		const int32 DurCur = RawU8(It, 13);
		const uint32 Col = (DurCur == 0) ? kColDurZero : 0xFF000000;
		FString Line;
		if (R->CategoryFlag == 3 && !bCharged)
		{
			Line = FString::Printf(TEXT("%s %s"), *LocRaw(kLocDurability), *LocRaw(kLocDurabilityNA));
		}
		else
		{
			Line = Colored(Col, FString::Printf(TEXT("%s %d/%d"), *LocRaw(kLocDurability), DurCur, R->DurabilityMax));
		}
		SetInfoText(TEXT("INVEN_SOLLTXT09"), Line);
	}
	// 10006 — «Доступно улучшений» (у «зарядного» предмета строки нет).
	{
		const int32 Cur = RawU8(It, 26) - RawU8(It, 25);
		const int32 Max = RawU8(It, 26);
		SetInfoText(TEXT("INVEN_SOLLTXT08"), bCharged ? FString()
			: FString::Printf(TEXT("%s %d/%d"), *LocRaw(kLocUpgradesLeft), Cur, Max), !bCharged);
	}
	// 10005 — «не передаётся».
	{
		FString Line;
		if (RawU8(It, 50) != 0)          { Line = Colored(kColInfoBound, LocRaw(kLocNoTrade)); }
		else if (R->TooltipFlag200 == 1) { Line = Colored(kColInfoBound, LocRaw(kLocNoTradeEquip)); }
		SetInfoText(TEXT("INVEN_SOLLTXT07"), Line, !Line.IsEmpty());
	}
	// 426/427/428 — заголовки блоков опций (у «зарядного» предмета — свои нотисы 111052/111053).
	SetInfoText(TEXT("Inven_NormalOption"),   LocRaw(kLocOptNormal));
	SetInfoText(TEXT("Inven_UpgadingOption"), LocRaw(bCharged ? kLocOptChargeA : kLocOptUpgrade));
	SetInfoText(TEXT("Inven_CraftingOption"), LocRaw(bCharged ? kLocOptChargeB : kLocOptCraft));
	// 463 — строка «скорости атаки» (эталон: нотис 111405 с linkedGroupCode).
	{
		const bool bShowSpeed = !bCharged && R->ItemType >= 0 && R->ItemType <= 9
		                        && R->ItemType != 5 && R->ItemType != 6 && R->ItemType != 7;
		SetInfoText(TEXT("INVEN_attack_speed"), FormatOneInt(LocRaw(kLocAttackSpeed), R->LinkedGroupCode), bShowSpeed);
	}
	// 10000/10001/10002..10004 — пять опций предмета (пары +29 …, шаг 4).
	{
		static const TCHAR* kOptCtl[5] = { TEXT("INVEN_SOLLTXT05"), TEXT("INVEN_SOLLTXT06"),
		                                   TEXT("INVEN_ArmoryOp3"), TEXT("INVEN_ArmoryOp4"), TEXT("INVEN_ArmoryOp5") };
		for (int32 i = 0; i < 5; ++i)
		{
			const int32 Type = RawI16(It, 29 + 4 * i);
			const int32 Val  = RawI16(It, 31 + 4 * i);
			const FString Line = (Type > 0) ? FormatOptionValue(Type, Val) : FString();
			SetInfoText(kOptCtl[i], Line, !Line.IsEmpty());
		}
	}
	// 477 + 488/489 — сет-бонус (у shell-предмета из pricetable dw06..dw09).
	{
		FAsdaPriceRow Price;
		const bool bSet = !bCharged && R->CategoryFlag == 3 && IT->GetPrice(It.ItemCode, Price);
		SetInfoVis(TEXT("INVEN_Primium_Option"), bSet);
		if (bSet)
		{
			const int32 Pairs[2][2] = { { Price.dw06, Price.dw07 }, { Price.dw08, Price.dw09 } };
			static const TCHAR* kSetCtl[2] = { TEXT("INVEN_Primium_Option_text"), TEXT("INVEN_Primium_Option_text2") };
			for (int32 i = 0; i < 2; ++i)
			{
				const FString L = (Pairs[i][0] > 0) ? FormatOptionValue(Pairs[i][0], Pairs[i][1]) : FString();
				SetInfoText(kSetCtl[i], L.IsEmpty() ? FString() : Colored(kColSpecOpt, L), !L.IsEmpty());
			}
		}
		else
		{
			SetInfoText(TEXT("INVEN_Primium_Option_text"),  FString(), false);
			SetInfoText(TEXT("INVEN_Primium_Option_text2"), FString(), false);
		}
	}
	// 512 — строка бонуса самоцвета гнезда 0; 418..421 — агрегированные строки самоцветов.
	{
		const int32 Sowel0 = RawI16(It, 16);
		FAsdaSowellRow Sow;
		const bool bHasSowel = (Sowel0 > 0) && IT->GetSowell(Sowel0, Sow);
		if (bHasSowel && Sow.BonusStatType >= 0 && Sow.BonusStatType <= 2)
		{
			const int32 LocId = (Sow.BonusStatType == 0) ? kLocSowelAtk
			                  : (Sow.BonusStatType == 1) ? kLocSowelMAtk : kLocSowelDef;
			SetInfoText(TEXT("INVEN_sowell_attack"), FString::Printf(TEXT("%s : %d"), *LocRaw(LocId), Sow.BonusValue));
		}
		else
		{
			SetInfoText(TEXT("INVEN_sowell_attack"), FString(), false);
		}
		// Первая строка (418) — самоцвет гнезда 0, дальше (419..421) — агрегат гнёзд 1..3.
		TArray<FString> Lines;
		if (bHasSowel)
		{
			Lines.Add(FString::Printf(TEXT("%s %s"), *LocRaw(kLocSowelBonusBase + Sow.BonusStatType),
				*FormatSowelStatRange(It, Sow.BonusStatType)));
		}
		TArray<FString> AggLines;
		BuildSowelLines(It).ParseIntoArray(AggLines, TEXT("\n"), /*CullEmpty=*/true);
		Lines.Append(AggLines);   // ★ ParseIntoArray ОЧИЩАЕТ приёмник — собираем через временный массив
		static const TCHAR* kSowCtl[4] = { TEXT("INVEN_SOLLTXT00"), TEXT("INVEN_SOLLTXT01"),
		                                   TEXT("INVEN_SOLLTXT02"), TEXT("INVEN_SOLLTXT03") };
		for (int32 i = 0; i < 4; ++i)
		{
			const FString L = Lines.IsValidIndex(i) ? Lines[i] : FString();
			// 418 (SOLLTXT00) в эталоне окрашен `SetField923(-12088593)` = 0xFF478AEF.
			SetInfoText(kSowCtl[i], L.IsEmpty() ? FString() : (i == 0 ? Colored(kColSollTxt00, L) : L), !L.IsEmpty());
		}
		// ★★★ Д3 (КРУГ 5). ПЛАШКА `Sowel_M` В ПАНЕЛИ ВСЕГДА РОВНО ОДНА.
		//   Эталон `ItemTooltip_PopulateInfoPanel` (item_inventory.c:37247-37268) сперва гасит ВСЕ
		//   четыре — 419 (`pTooltip[66]`), 420 (67), 421 (68), 479 (69), — а затем показывает ПЕРВУЮ
		//   НЕПУСТУЮ из 419/420/421 и на этом останавливается (`if … else if … else if`). Порт
		//   зажигал все три сразу: три плашки в два ряда, наезжающие на описание, — это и есть
		//   «некоторый текст стоит криво».
		{
			bool bShown = false;
			for (int32 i = 1; i < 4; ++i)
			{
				const bool bHasText = Lines.IsValidIndex(i) && !Lines[i].IsEmpty();
				const bool bVisible = (!bShown && bHasText);
				SetInfoVis(kSowCtl[i], bVisible);
				bShown = bShown || bVisible;
			}
		}
		// ★ Д4 (КРУГ 5): запомнить камни ГНЁЗД показанного предмета — по ним строится подсказка при
		//   наведении на 441…444 (эталон читает те же +16/+18/+20/+22 записи, см. `TickInfoPanelSockets`).
		for (int32 i = 0; i < 4; ++i) { PanelSowelCode[i] = RawI16(It, 16 + 2 * i); }
		PanelSocketHover = -1;
	}
	// 497 — «Эффект ЖК : min ~ max (+N%)».
	//   ★ ИСПРАВЛЕНО 2026-08-09: вариант «Сила улучшения + N%» (нотис 108513) в этой ветке эталона
	//     ОТСУТСТВУЕТ — он есть только у записей ВТОРОЙ таблицы (item_inventory.c:36706-36717).
	//     Здесь эталон всегда печатает строку 2360 и всегда показывает контрол
	//     (`SetVisible(pTooltip[71], 1)` — item_inventory.c:37120/37302).
	{
		const double Pct = (double)EnhPct;
		int32 Hi = (int32)(Pct * ((double)R->StatBaseHi / 100.0) + (double)R->StatBaseHi);
		int32 Lo = (int32)(Pct * ((double)R->StatBaseLo / 100.0) + (double)R->StatBaseLo);
		// ★★★ П16 (ЗАКРЫТО 2026-08-09). У shell-предмета (`categoryFlag == 3`) эталон
		//   (item_inventory.c:37282-37290) НЕ печатает базы `ItemRecord->gap_88`, а ищет строку
		//   `inventoryFindMatchingStackSlot@0xD34260` и берёт `+20 / +24`. Что это за структура:
		//   **`./Data/PremiumLevelTable.bin`** — loose-файл (НЕ в Bin.bbz), загрузчик
		//   `PremiumLevelTable_Load@0xD34100`: header 20 б (count в +4) + count × 28 б,
		//   поля (Level, ItemGrade, ItemType, UsageCategory, ItemSubType, StatMin, StatMax).
		//   В порт таблица заведена как DataTable `DT_premiumleveltable` (25 220 строк), поиск —
		//   в `UAsdaItemTableSubsystem::FindPremiumStatRange` (линейный, «первое совпадение», 1:1).
		//   Не нашли — остаются базы `gap_88`, ровно как у эталона (он возвращает 0 и min/max не трогает).
		if (R->CategoryFlag == 3)
		{
			int32 PremMin = 0, PremMax = 0;
			if (IT->FindPremiumStatRange(It.ItemCode, PlayerLv, PremMin, PremMax))
			{
				Hi = PremMin;   // строка[+20]
				Lo = PremMax;   // строка[+24]
			}
		}
		const FString Line = (EnhPct != 0)
			? FString::Printf(TEXT("%s : %d ~ %d (+%d%%)"), *LocRaw(kLocStatEffect), Hi, Lo, EnhPct)
			: FString::Printf(TEXT("%s : %d ~ %d"), *LocRaw(kLocStatEffect), Hi, Lo);
		SetInfoText(TEXT("INVEN_SOLLTXT04"), Line);
	}
	// ★★★ 479 `INVEN_SOLLTXT04_1` — ВСЕГДА СКРЫТ (эталон: единственное обращение к `pTooltip[69]`
	//   во всём экспорте — `CControl_SetVisible(…, 0)`, item_inventory.c:37253). Текста контрол
	//   не получает ни в одной ветке. Прежняя строка «Количество использований : N» была
	//   ВЫДУМАНА портом — снято 2026-08-09 по закону `no-invented-ui` (флага отката нет:
	//   возвращать выдуманный текст незачем).
	SetInfoVis(TEXT("INVEN_SOLLTXT04_1"), false);
	// 422 — описание (+ примечание «запечатан» при заточке >= 30).
	{
		FAsdaItemTooltipRow Tip;
		FString Desc = IT->GetItemTooltip(It.ItemCode, Tip) ? Tip.Description : FString();
		if (Enhance >= 30) { Desc += TEXT("\n") + Colored(kColSealed, LocRaw(kLocSealedNote)); }
		SetInfoText(TEXT("INVEN_SHELLCONTENS"), Desc);
	}
	// ★★★ 423 — ИКОНКА ПРЕДМЕТА в плане `INVEN_EQUIICON_108_0`
	//   (эталон `SetText("")` + `DrawTextureRect(pTooltip[52], …)`, item_inventory.c:36781/36807).
	//   ДОБАВЛЕНО 2026-08-09: раньше контрол 423 не был привязан вовсе — панель показывала
	//   текст без картинки. Атлас/ячейку даёт общий `GetItemIcon` (три пространства кодов).
	if (!ApplyInfoIcon(It.ItemCode)) { ClearInfoIcon(); }

	// ★★★ 10008 — ПОЛОСА ПРОЧНОСТИ под иконкой (эталон item_inventory.c:36812-36821):
	//     `itemType > 4 && (itemType < 8 || itemType > 9)` ⇒ `SetVisible(0)`;
	//     иначе `SetRange(0, ItemRecord->field68)` + `SetValue(запись+13)` + `SetVisible(1)`.
	//   То есть полоса живёт только у надеваемого (типы 0..4, 8, 9) — у расходников её нет.
	//   ★ Смещение прочности +13 посчитано от базы записи самостоятельно (закон
	//     `globals-tsv-offset-shift`: словесные описания в renames/*.tsv завышены на 8).
	//   ДОБАВЛЕНО 2026-08-09: раньше 10008 был только зоной наведения, значение не ставилось.
	{
		const int32 T = R->ItemType;
		const bool bHasDurability = !(T > 4 && (T < 8 || T > 9));
		SetVisPlane(DuriBack, bHasDurability);
		SetVisPlane(DuriFill, bHasDurability);
		if (bHasDurability)
		{
			// `SetRange(0, field68)` + `SetValue(cur)` = доля cur/field68. Знаменатель 0 ⇒ пустая полоса
			//   (у эталона деления нет — полоса просто не наполняется).
			const float MaxD = (float)R->DurabilityMax;
			const float Frac = (MaxD > 0.f) ? ((float)RawU8(It, 13) / MaxD) : 0.f;
			ClipBarFill(DuriFill, DuriLeft, DuriFullW, Frac);
		}
	}

	// 424 — полоса «состояния»: доля прочности от максимума в процентах
	//   (эталон `SetRange(0, 100.0f)` в инициализации + `SetValue((int)(cur/field68*100))`,
	//   item_inventory.c:35385/37320-37323).
	//   ★ ИСПРАВЛЕНО 2026-08-09: код искал `UProgressBar` по имени `INVEN_INVASIONGAUGE`, но такого
	//     виджета в WBP НЕТ — импортёр разложил ProgressBar-скин на два ПЛАНА
	//     `INVEN_INVASIONGAUGE_106_0/_106_1`, поэтому cast всегда давал nullptr и полоса не жила.
	//     Видимость НЕ трогаем: родительская группа `Inven_InvasionBase` в .JUI помечена Vis = 0,
	//     эталон её не показывает — полоса скрыта самими данными.
	{
		const float MaxD = (float)FMath::Max(R->DurabilityMax, 1);
		ClipBarFill(CondFill, CondLeft, CondFullW, (float)RawU8(It, 13) / MaxD);
	}

	UE_LOG(LogAsdaItemTip, Verbose, TEXT("[инфо-панель] предмет %d (источник %d) — панель заполнена"), It.ItemCode, SourceKind);
}
