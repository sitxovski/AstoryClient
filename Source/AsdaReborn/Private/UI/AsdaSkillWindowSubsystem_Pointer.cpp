// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillWindowSubsystem_Pointer.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillWindowSubsystem.cpp, дословный перенос): курсор, перетаскивание на быстрые слоты, подсказки навыков.
//   Общее для частей — в AsdaSkillWindowSubsystem_Private.h; шапка и реверс — в AsdaSkillWindowSubsystem.cpp.
#include "UI/AsdaSkillWindowSubsystem.h"

#include "Items/AsdaItemTableSubsystem.h"   // таблицы soulmateskilltable/soulmateskill для подсказки навыка соулмейта
#include "Items/AsdaItemDbRows.h"
#include "UI/AsdaShopDblClick.h"   // ★ U1 (В2-A дедупа №2): FAsdaDblClick::WindowSec — общий порог Windows

#if PLATFORM_WINDOWS
// ★ Системный интервал двойного клика: оригинал получает событие 0xD от самой Windows
//   (`WM_LBUTTONDBLCLK`), то есть по значению «Панель управления → Мышь». В Slate такого запроса нет
//   (`FSlateApplication` умеет только маршрутизировать уже готовое событие), поэтому берём его у ОС
//   тем же вызовом, каким его берёт оконная процедура оригинала.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "AsdaSkillWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillWindowSubsystem*.cpp

// ★ Аудит №3 (A1): своего `kLocMsgBoxOk` (2288) здесь больше нет — подпись кнопки ОБЩЕГО окна
//   `ONEBUT_MSGBOX` живёт в `AsdaLoc::kBtnOk` (`Public/UI/AsdaLocIds.h`). Нотисы этого окна остаются
//   здесь, нотисы общих окон — там: одинаковых имён с разными значениями больше не будет.
// ── Подсказка навыка (все id вычитаны из `CSkillTooltip_BuildAndRender@0xEA7260`) ──
static constexpr int32 kLocLevelShort   = 12215;  // «Ур.»
static constexpr int32 kLocSecUnit      = 6824;   // «сек.»
static constexpr int32 kLocMinUnit      = 6825;   // «мин.» (единица минут в длительностях [sdt*])
static constexpr int32 kLocTipJobStep   = 2297;   // «Профессия»
static constexpr int32 kLocTipWeapon    = 2307;   // «Оружие»
static constexpr int32 kLocTipMpCost    = 2308;   // «Затраты ОМ»
static constexpr int32 kLocTipAtLeast   = 2331;   // «минимум»
static constexpr int32 kLocTipNone      = 2335;   // «Нет»
static constexpr int32 kLocTipReqStats  = 9428;   // «Необходимые параметры»
static constexpr int32 kLocTipNotActive = 9450;   // «Не активирован»
static constexpr int32 kLocTipCooldown  = 9474;   // «Откат»
static constexpr int32 kLocTipAtkStyle  = 9475;   // «Стиль атаки»
static constexpr int32 kLocTipCastSel   = 9482;   // «Выбранная цель»
static constexpr int32 kLocTipTargetSel = 9488;   // «Указать цель»
static constexpr int32 kLocTipAttr      = 9496;   // «Атрибут»
static constexpr int32 kLocTipCanLearn  = 9505;   // «Ты можешь овладеть этим навыком»
static constexpr int32 kLocTipSkillKind = 9506;   // «Тип навыка»
static constexpr int32 kLocTipReqSkill  = 9553;   // «Необходимый навык»
static constexpr int32 kLocTipReqPoints = 9554;   // «Необходимо очков»

static constexpr int32 kLocSoulTipLevel     = 110502;  // подпись строки ступени в подсказке панели
static constexpr int32 kLocSoulTipTree      = 110503;  // подпись строки дерева в подсказке панели
static constexpr int32 kLocSoulTipDesc      = 110506;  // подпись «Описание» в подсказке панели

// ★ ДЕФЕКТ ОРИГИНАЛА: `CSkillInfo_HasStatTokens@0xEB2880` проверяет «[sdtt2]» ДВАЖДЫ и «[sdtt3]»
//   ни разу — описание, где есть только [sdtt3], остаётся с сырым токеном. Порт его подставляет.
static ASDA_DEV_CVAR(int32, CVarSkillLegacyTokenLoop,
	TEXT("asda.Skill.LegacyTokenLoop"), 0,
	TEXT("1 = воспроизводить дефект оригинала: не подставлять токен [sdtt3] в описании навыка."),
	ECVF_Default);

// ★★★ [ИСПРАВЛЕНИЕ] 05.11.2026 (сессия gold-boast; жалоба «12Seconds seconds» в EN-описании Deep Piercing). Эталонный подставщик
//   длительности `[sdt1..3]`/`[sdtt1..3]` (skill_buff.c:37448-37472) уже приписывает к числу единицу — нотис 6824 «сек.»/«Seconds»
//   или 6825 «мин.»/«Minutes» (`"%d%s"`). А тексты 13 описаний `Skilltooltip.BIN` RU (502, 503, 515, 526, 560, 594×2, 604, 607, 705,
//   722, 757, 956, 1016) и 10 EN — EN-ОРИГИНАЛ, оверлей ему равен — пишут единицу ЕЩЁ РАЗ сразу за токеном («на [sdt1] сек.»,
//   «for [sdt1] seconds»): в оригинале «на 12сек. сек.», «for 12Seconds seconds». Дефект данных оригинала. 1 — слово-единица,
//   стоящее сразу после подставленной длительности (через пробелы), снимается; точка за ним остаётся, если число само не
//   кончается точкой (EN «12Seconds.» — конец фразы; RU «12сек.» — точка уже есть); 0 — как эталон (повтор единицы).
static ASDA_DEV_CVAR(int32, CVarSkillDurUnitDedup,
	TEXT("asda.Skill.DurUnitDedup"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — в описании навыка не повторять единицу времени после [sdt*]/[sdtt*] («12Seconds seconds» → «12Seconds»); ")
	TEXT("0 — как эталон: единица из текста описания стоит после уже подставленной"),
	ECVF_Default);

// Слова-единицы, которые описания пишут за токеном длительности (EN и RU; регистр EN — без учёта, кириллица — явными формами,
//   `ESearchCase::IgnoreCase` UE кириллицу не сворачивает). Длинные формы раньше коротких — берётся самое длинное совпадение.
static const TCHAR* const kDurUnitWords[] = {
	TEXT("seconds"), TEXT("second"), TEXT("secs"), TEXT("sec"), TEXT("minutes"), TEXT("minute"), TEXT("mins"), TEXT("min"),
	TEXT("секунды"), TEXT("секунду"), TEXT("секунд"), TEXT("сек"), TEXT("минуты"), TEXT("минуту"), TEXT("минут"), TEXT("мин"),
	TEXT("Секунды"), TEXT("Секунду"), TEXT("Секунд"), TEXT("Сек"), TEXT("Минуты"), TEXT("Минуту"), TEXT("Минут"), TEXT("Мин") };

// Снять повтор единицы сразу за подставленной длительностью: S — строка, After — индекс сразу за значением, Val — значение.
static void AsdaSkillDropDupUnit(FString& S, int32 After, const FString& Val)
{
	if (CVarSkillDurUnitDedup.GetValueOnGameThread() == 0) { return; }
	int32 i = After;
	while (i < S.Len() && S[i] == TEXT(' ')) { ++i; }
	if (i == After || i >= S.Len()) { return; }                  // единица отделена пробелом — без пробела это не повтор
	for (const TCHAR* W : kDurUnitWords)
	{
		const int32 N = FCString::Strlen(W);
		if (i + N > S.Len() || !S.Mid(i, N).Equals(W, ESearchCase::IgnoreCase)) { continue; }
		const int32 End = i + N;
		if (End < S.Len() && FChar::IsAlnum(S[End])) { continue; }   // начало другого слова («secondary»), не единица
		int32 Cut = End;
		if (Cut < S.Len() && S[Cut] == TEXT('.') && Val.EndsWith(TEXT("."))) { ++Cut; }   // «сек.» за «12сек.» — точка уже есть
		S.RemoveAt(After, Cut - After);
		return;
	}
}

// ══════════════════════════════ КУРСОР, ПОДСКАЗКА, КЛИК ════════════════════════════════════════
// ★ Аудит №3 (A1): своего `IsCursorOver` здесь больше нет — хит-тест «курсор над виджетом» у всего
//   порта один: `AsdaUi::IsCursorOverWidget` (D1 аудита №2). Тело прежней копии совпадало с общим
//   ПОБАЙТОВО: скрытый (`Collapsed`/`Hidden`) виджет курсор не ловит, ни разу не отрисованный
//   (нулевая геометрия) — тоже, границы включительные, Slate не поднят ⇒ false.
void UAsdaSkillWindowSubsystem::TickPointer()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;

	// ★★★ ПЕРЕТАСКИВАНИЕ обслуживаем ПЕРВЫМ и ДО выхода по перекрытию: при захвате курсор уходит из
	//   окна навыков на панель быстрого доступа, и если выйти раньше — бросок никогда не случится.
	if (TickSkillDrag()) { return; }

	// Выпадающие списки/модалки поверх нас забирают указатель первыми.
	if (WM && WM->IsPointerBlockedByTopmost(kSkillWinId)) { return; }

	int32 NewHover = -1;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FAsdaSkillSlotWidgets& S = Slots[i];
		// ★ Эталон адресует сообщения узла КОНТРОЛУ ИКОНКИ 32x32 (12012, 12018, …), а не рамке 42x42:
		//   `m_aSkillSlot[17*slot + 4]`. Значит и зона наведения/клика — иконка.
		if (S.SkillCode < 0 || !S.Icon) { continue; }
		if (AsdaUi::IsCursorOverWidget(S.Icon)) { NewHover = i; break; }
	}

	if (NewHover != HoverSlot)
	{
		HoverSlot = NewHover;
		if (HoverSlot >= 0) { ShowSlotTooltip(HoverSlot); }
		else                { HideOwnTooltip(); }
	}

	// Подсказки левой панели (эталон msg 9 контролов 56302/56304/56308/56310).
	int32 NewSoulHover = -1; bool bNewSoulBig = false;
	if (HoverSlot < 0)
	{
		for (int32 k = 0; k < SoulRows.Num(); ++k)
		{
			const FAsdaSoulPreviewRow& R = SoulRows[k];
			if (R.GuardIndex < 0) { continue; }
			if (R.Illust   && AsdaUi::IsCursorOverWidget(R.Illust))   { NewSoulHover = k; bNewSoulBig = true;  break; }
			if (R.NextIcon && AsdaUi::IsCursorOverWidget(R.NextIcon)) { NewSoulHover = k; bNewSoulBig = false; break; }
		}
	}
	if (NewSoulHover != SoulHoverRow || bNewSoulBig != bSoulHoverBig)
	{
		SoulHoverRow = NewSoulHover; bSoulHoverBig = bNewSoulBig;
		if (SoulHoverRow >= 0) { ShowSoulTooltip(SoulHoverRow, bSoulHoverBig); }
		else if (HoverSlot < 0) { HideOwnTooltip(); }
	}

	// Полоса прокрутки. Клик по узлу теперь обрабатывает `TickSkillDrag` (на ОТПУСКАНИИ, если
	//   перетаскивания не случилось) — иначе press+move успевал бы открыть подтверждение изучения.
	if (!FSlateApplication::IsInitialized()) { return; }
	// ★ Аудит №3 (B3): опрос и фронт полосы — общая точка, СВОЙ экземпляр (см. поле `ScrollMouse`):
	//   в кадры активного перетаскивания сюда не доходит управление, и латч полосы не обновляется.
	ScrollMouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9) — ВТОРОЙ экземпляр кадра мыши этого окна, поэтому и гейт
	//   свой. Обоснование, порядок и ключ отката `asda.UI.ModalGatePolling` — в шапке
	//   `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ★ Наведение разбирается ВЫШЕ (порядок функции не трогаем), поэтому плашку узла/Хранителя,
	//     зажжённую этим же кадром, гасим здесь — иначе она повисла бы под нотисом.
	if (AsdaUi::IsModalDispatchBlocked(GI))
	{
		HoverSlot = -1; SoulHoverRow = -1; bSoulHoverBig = false;
		HideOwnTooltip();
		// Латч захвата бегунка снимает кадр «кнопка отпущена» (он же вернёт стрелке обычный вид).
		TickScrollBar(/*bLmb*/false, /*bLmbDownEdge*/false);
		ScrollMouse.Reset();
		return;
	}
	TickScrollBar(ScrollMouse.bLmb, ScrollMouse.bLmbDown);
}

// ══════════════════════════════ ПЕРЕТАСКИВАНИЕ НАВЫКА В ПАНЕЛЬ ═════════════════════════════════
//
// ★★★ ПОЛНЫЙ ПОТОК ЭТАЛОНА (`skillWndOnUICommand@0xE4A8E0`, сообщения 5 и 7):
//
//  ШАГ 1. ЗАХВАТ — msg 5 → `skillCheckResetEligible@0xE55DA0`:
//         по контролу узла находится слот, из него берётся `skillCode` и `уровень`;
//         ГЕЙТЫ (их ровно три, других нет):
//            • запись навыка существует;
//            • `skillSubType != 3` и `!= 4` (пассивный и автоматический не таскаются);
//            • `уровень > 0` (невыученный не таскается).
//         При успехе код навыка запоминается в `this->gap_E58`, функция возвращает 2
//         (у предмета аналогичный обработчик возвращает 1 — разные режимы захвата движка).
//
//  ШАГ 2. ПРИЗРАК — рисует движок: плоскость иконки в режиме FillMode 5 («рисовать у курсора»).
//         У нас это явный UImage с alpha 40 % — ровно как у предмета и у слота панели, чтобы
//         поведение было единообразным.
//
//  ШАГ 3. БРОСОК — msg 7 → `Skill_SendSkillReset@0xE55EA0` (имя в IDA ошибочно, это ДИСПЕТЧЕР БРОСКА):
//            if (контрол под курсором == nullptr) → НИЧЕГО (ни очистки, ни пакета);
//            idnum 0x11D33…0x11D3E (73011…73022) → колонка = idnum − 0x11D33, основной ряд;
//            idnum 0x11DC9…0x11DD4 (73161…73172) → колонка = idnum − 0x11DC9, T-ряд;
//            любой другой контрол                → НИЧЕГО.
//         ⚠ Верхние границы на ОДНУ КОЛОНКУ короче, чем у предмета (0x11D3F / 0x11DD5):
//         КОЛОНКА 12 — слот оружия — навык не принимает.
//
//  ШАГ 4. УКЛАДКА — `Skill_SendLearnOrLevelUp@0xE55FB0`:
//            slot = колонка + 13·текущая_страница   (основной ряд)
//            slot = колонка + 65                    (T-ряд)
//            запись 10 б: Type = 3, SubType = 0, Index = 0, Count = 0, Id = skillCode
//         → `Hotbar_DragDrop_SetSlot@0xDE9DF0`: запись ПОВЕРХ содержимого (занятый слот — ЗАМЕНА),
//           пересборка иконки/тултипа, перерисовка и `Hotbar_SendLayout(slot / 13)`.
//
//  ШАГ 5. СЕТЬ — C→S **5067** (`OP_RECV_DB_QUICK_SLOT_INFO_SAVE`, 155 б = 8 анти-реплей + 16 MD5 +
//         131 = `u8 page` + 13×10). Подтверждения сервер НЕ шлёт, применение оптимистичное.
//
//  ⇒ Обратного направления «из панели в окно навыков» НЕ СУЩЕСТВУЕТ: слот панели хранит только код
//    навыка, из книги ничего не изымается. Снятие навыка со слота — это бросок слота панели В ПУСТОТУ
//    (мимо всего интерфейса), он уже реализован в `UAsdaHotbarSubsystem::EndDrag` → `ClearSlot`.
//    Перетаскивание навыка МЕЖДУ слотами панели — тот же `SwapSlots`, что и у предмета (обмен
//    десятками + отправка обеих страниц); правило слота оружия там действует в обе стороны.
//
// Возвращает true, если событие мыши уже обслужено перетаскиванием и остальной обработке не нужно.
bool UAsdaSkillWindowSubsystem::TickSkillDrag()
{
	if (!FSlateApplication::IsInitialized()) { return false; }
	// ★ Аудит №3 (B3): опрос, фронты и курсор — общая точка (свой экземпляр `DragMouse`; латч
	//   пишется здесь же, как и раньше — прежняя строка `bMouseWasDown = bDown` стояла ровно тут).
	DragMouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал (здесь это укладка навыка в панель либо изучение узла).
	//   ★ ВОЗВРАТ `false` — «перетаскивание не обслужено», как у раннего выхода по неподнятому
	//     Slate выше. `true` означал бы «бросок сделан» и увёл бы `TickPointer` из ЕГО гейта,
	//     оставив латч полосы прокрутки (`ScrollMouse`) незакрытым.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		// Только ВИЗУАЛ захвата: `EndSkillDrag` выполнил бы укладку, а под модалкой это отмена.
		if (bDragActive) { CancelSkillDragVisual(); }
		DragArm.Disarm();
		LastDownSlot = -1; LastDownTimeS = 0.0;   // накопитель двойного клика — тоже защёлка
		DragMouse.Reset();
		return false;
	}
	const bool bDown     = DragMouse.bLmb;
	const bool bDownEdge = DragMouse.bLmbDown;
	const bool bUpEdge   = DragMouse.bLmbUp;
	const FVector2D Cur  = DragMouse.Cursor;

	// Нажатие над занятым узлом — «взводим» захват (сам захват начнётся при смещении курсора).
	if (bDownEdge && Slots.IsValidIndex(HoverSlot) && Slots[HoverSlot].SkillCode >= 0)
	{
		// ★ Взвод — общая точка `FAsdaDragArm` (прежние поля `DragArmSlot`/`DragArmPos` были
		//   единственной из трёх реализаций порога; теперь условие одно на все окна).
		DragArm.Arm(HoverSlot, -1, Cur);
		// ★★★ T5: РАСПОЗНАЁМ ЭТАЛОННОЕ СОБЫТИЕ 0xD (ДВОЙНОЙ ЛЕВЫЙ КЛИК) — доказательство в шапке
		//   `OnSlotClicked`. Второе нажатие по ТОМУ ЖЕ узлу в пределах системного интервала.
		const double NowS = FPlatformTime::Seconds();
		const double DblS = FAsdaDblClick::WindowSec();   // ★ U1 (В2-A дедупа №2): общая точка порога Windows (то же значение, что у оригинала)
		if (LastDownSlot == HoverSlot && (NowS - LastDownTimeS) <= DblS)
		{
			// ⚠ ВАЖНО: событие 0xC (отпускание = выбор узла) у эталона ПОСЛЕ дубля тоже приходит —
			//   `CControl_OnDrag@0x12DC2D0` пускает его при взведённом бите `0x1000`, а этот бит
			//   ставит как раз обработчик 0xD (`CControl_OnKeyDown@0x12DC240`). Поэтому ниже мы
			//   отпускание НЕ подавляем: 1:1 с оригиналом это «применить + выбрать».
			OnSlotDoubleClicked(HoverSlot);      // = `skillDispatchUseByBaseId@0xE57CB0`
			LastDownSlot  = -1;                  // третье нажатие снова считается первым (как у Windows)
			LastDownTimeS = 0.0;
		}
		else
		{
			LastDownSlot  = HoverSlot;
			LastDownTimeS = NowS;
		}
	}
	else if (bDownEdge)
	{
		LastDownSlot = -1;                       // нажали мимо узла — цепочка дубля рвётся
	}

	// ── «ЗАЖАЛИ И СДВИНУЛИ» ⇒ вот теперь захват. Порог живёт в общей точке `FAsdaDragArm`, а не
	//   здесь: до правки это было ЕДИНСТВЕННОЕ из трёх окон, где порог вообще был.
	if (bDown && !bDragActive && DragArm.ShouldStart(Cur))
	{
		BeginSkillDrag(DragArm.KeyA);
		DragArm.MarkStarted();
	}
	if (bDragActive) { UpdateSkillDragGhost(); }

	if (bUpEdge)
	{
		const int32 Armed = DragArm.IsArmed() ? DragArm.KeyA : -1;
		DragArm.Disarm();
		if (bDragActive)
		{
			EndSkillDrag();
			return true;   // бросок обслужен — изучение по этому же нажатию не запускаем
		}
		// Перетаскивания не было ⇒ это обычный клик по узлу (эталон msg 0xC).
		if (Armed >= 0 && HoverSlot == Armed) { OnSlotClicked(Armed); }
	}
	return bDragActive;
}

void UAsdaSkillWindowSubsystem::BeginSkillDrag(int32 SlotIdx)
{
	if (!Slots.IsValidIndex(SlotIdx)) { return; }
	const int32 Code = Slots[SlotIdx].SkillCode;
	// ШАГ 1: гейты `skillCheckResetEligible` — ровно три, других в эталоне нет.
	if (!CanDragSkill(Code))
	{
		const FAsdaSkillDef* D = Db ? Db->Get(Code) : nullptr;
		UE_LOG(LogAsdaSkillWin, Log,
			TEXT("[навык→панель] захват отклонён: навык %d, подтип %d, уровень %d (эталон требует подтип не 3/4 и уровень > 0)"),
			Code, D ? D->SubType : -1, GetLearnedLevel(Code));
		DragArm.Disarm();
		return;
	}
	DragSkillCode = Code;
	bDragActive = true;
	HideOwnTooltip();

	// ШАГ 2: призрак под курсором — та же иконка с alpha 40 %, как у предмета и у слота панели.
	//   ★ Аудит №3 (B4): создание/раскладка/показ — общая точка `FAsdaDragGhost` (третий из трёх
	//   экземпляров этого кода в порту).
	if (Win && Win->WidgetTree)
	{
		if (UCanvasPanel* Root = Cast<UCanvasPanel>(Win->WidgetTree->RootWidget))
		{
			// ⚠ Нет иконки — нет и призрака (так же гейтят хотбар и инвентарь: `if (U.Icon && …)`).
			//   Ветка недостижима: `DragArm.KeyA` берётся из `HoverSlot`, а наведение в `TickPointer`
			//   пропускает узлы без `Icon`. Прежний код в этом случае показал бы призрак с ПУСТЫМ
			//   брашем — то есть невидимый прямоугольник; поведение приведено к двум другим окнам.
			if (UImage* Src = Slots[SlotIdx].Icon.Get())
			{
				DragGhost.Begin(Win, Root, Src->GetBrush(), FVector2D(IconDrawSize, IconDrawSize),
					DragGhostOpacity, DragGhostCursorOffset, TEXT("SKILL_DragGhost"),
					GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
			}
		}
	}
	// Исходный узел приглушаем, пока навык «в руке» (эталон убирает плоскость в режим у курсора).
	if (UImage* Ic = Slots[SlotIdx].Icon.Get())
	{
		Ic->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, DragSourceOpacity));
	}
	DragSrcSlot = SlotIdx;
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навык→панель] захват навыка %d из узла %02d (призрак alpha %.0f%%)"),
		Code, SlotIdx, DragGhostOpacity * 100.f);
}

// ★ Аудит №3 (B4): тело слежения переехало в `FAsdaDragGhost::Tick` — здесь и в инвентаре оно
//   совпадало ПОБАЙТОВО (внутри — тот же D2-перевод «экран → канвас окна»).
void UAsdaSkillWindowSubsystem::UpdateSkillDragGhost()
{
	if (!bDragActive || !Win) { return; }
	DragGhost.Tick(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
}

void UAsdaSkillWindowSubsystem::CancelSkillDragVisual()
{
	DragGhost.Cancel();   // ★ Аудит №3 (B4): гашение призрака — общая точка
	if (Slots.IsValidIndex(DragSrcSlot))
	{
		if (UImage* Ic = Slots[DragSrcSlot].Icon.Get()) { Ic->SetColorAndOpacity(FLinearColor::White); }
	}
	DragSrcSlot = -1;
	bDragActive = false;
}

// Единая точка укладки: ею пользуются и бросок мышью, и консольная команда.
//   Возвращает false с объяснением в лог, если не прошёл любой из гейтов эталона.
bool UAsdaSkillWindowSubsystem::DropSkillToHotbarSlot(int32 SkillCode, int32 AbsSlot)
{
	if (!CanDragSkill(SkillCode))
	{
		const FAsdaSkillDef* D = Db ? Db->Get(SkillCode) : nullptr;
		UE_LOG(LogAsdaSkillWin, Log,
			TEXT("[навык→панель] отказ: навык %d, подтип %d, уровень %d (эталон требует подтип не 3/4 и уровень > 0)"),
			SkillCode, D ? D->SubType : -1, GetLearnedLevel(SkillCode));
		return false;
	}
	UAsdaHotbarSubsystem* Hot = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hot)
	{
		UE_LOG(LogAsdaSkillWin, Warning, TEXT("[навык→панель] подсистема панели недоступна"));
		return false;
	}
	return Hot->AssignSkillToSlot(AbsSlot, SkillCode);
}

void UAsdaSkillWindowSubsystem::EndSkillDrag()
{
	const int32 Code = DragSkillCode;
	DragSkillCode = -1;
	// Призрак снимаем ДО хит-теста: иначе он сам оказался бы «контролом под курсором».
	CancelSkillDragVisual();
	if (Code < 0) { return; }

	UAsdaHotbarSubsystem* Hot = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hot)
	{
		UE_LOG(LogAsdaSkillWin, Warning, TEXT("[навык→панель] подсистема панели недоступна — бросок навыка %d потерян"), Code);
		return;
	}
	// ШАГ 3: «контрол под курсором». У эталона это idnum слота панели; у нас — хит-тест ячеек.
	const int32 Abs = Hot->HitTestSlotUnderCursor();
	if (Abs < 0)
	{
		// Эталон: `if (a2)` — при промахе мимо слотов панели (и мимо всего) НЕ ДЕЛАЕТСЯ НИЧЕГО.
		//   ⚠ В частности, навык НЕ снимается ниоткуда и пакет НЕ уходит.
		UE_LOG(LogAsdaSkillWin, Log, TEXT("[навык→панель] бросок навыка %d мимо панели — эталон ничего не делает"), Code);
		return;
	}
	// ШАГ 4 и 5 внутри: запись 10 б Type=3, перерисовка и пакет 5067.
	DropSkillToHotbarSlot(Code, Abs);
}

// ══════════════════════════════ ПОДСКАЗКА НАВЫКА (1:1 с эталоном) ═════════════════════════════
//
// ★★★ ГДЕ ЭТО В ЭТАЛОНЕ
//   `skillWnd_showSlotTooltip@0xE53EB0` собирает запись {код, уровень, максимум, «доступен»} и зовёт
//   `CSkillTooltip_BuildAndRender@0xEA7260`. Тот пишет ЗАГОЛОВОК в один контрол и ТЕЛО — в другой.
//   Второй аргумент (`allowNextLevelPreview`) = 1 из окна навыков, 0 при недоступной записи.
//
// РАЗБОР ЗАПИСИ-АРГУМЕНТА (смещения внутри неё, восстановлены по сумме обращений):
//   +0  i16 skillCode · +2 u8 текущий уровень · +34 u8 максимум (`.sif` +0x2B) · +35 u8 «доступен».
//
// КАТЕГОРИЯ (она же определяет цвет заголовка и строку «Тип навыка»):
//   skillKind == 2                    → 0, цвет FFEA9311, нотис 9507 «Навык атаки»
//   skillKind == 1 && skillSubType==3 → 2, цвет FF23ADEB, нотис 9509 «Пассивный навык»
//   skillKind == 1 && skillSubType==4 → 3, цвет FF20A43E, нотис 9510 «Автоматический навык»
//   иначе                             → 1, цвет FFF0EB1E, нотис 9508 «Навык поддержки»
//   ★ Если запись НЕ помечена доступной — заголовок целиком красный FFFF0000.
//   ⚠ Это и объясняет, почему подтипы 3/4 нельзя перетащить в хотбар (§3.4): они не активные.
//
// СТРОКИ ТЕЛА (каждая — «подпись : значение»; у НЕВЫУЧЕННОГО навыка вся строка серая FFC1C1BE):
//   Профессия (2297)      : 8427 + `Skilltooltip.BIN` +500          «Не ниже первой/второй/…»
//   Тип навыка (9506)     : 9507 + категория
//   Оружие (2307)         : перечисление нотисов 9455 + номер бита маски +0x83; пусто → 2335 «Нет»
//   Атрибут (9496)        : +0x8C == 6 ? 9497 «Нет» : 9498 + значение
//   Откат (9474)          : cooldownMs/1000 + 6824 «сек.»
//   Затраты ОМ (2308)     : уровень·mpCostPerLevel + mpCostBase, «MP»
//   Стиль атаки (9475)    : resourceType 1/2/3 → 9476/9478/9477      (только у категории 0)
//   Выбранная цель (9482) : 9483 + castType                          (кроме категории 2)
//   Указать цель (9488)   : 9488 + targetSlotType, если тот не 0     (кроме категории 2)
//   Необходимый навык (9553) : цепочка `skillGetEffectValues` (доп-скиллы +0xC8/+0xCB) либо 2335
//   Необходимо очков (9554)  : «9551 {gradeNum} {9399+grade}» либо 9552 / 9555
//   Требования (9428)     : только у навыков 4-й профессии (grade >= 10)
//   ⚠ Строки «Площадь атаки» (9479) и «Дальность атаки» (9504) эталон СОБИРАЕТ, но НИ В ОДНУ
//     сборку строки не подставляет — мёртвый код в срезе. Здесь их тоже нет.
//
// ПОРЯДОК И СОСТОЯНИЯ (switch по категории → 4 варианта раскладки):
//   A «обычный»       (доступен, уровень > 0, уровень < максимума): строки, описание,
//                      затем пустой блок и превью СЛЕДУЮЩЕГО уровня (заголовок + серое описание);
//   D «максимум»      (доступен, уровень >= максимума): строки + описание для максимума, без превью;
//   H «состояние»     (не выучен либо недоступен): СНАЧАЛА строка-состояние
//                      (9505 «Ты можешь овладеть этим навыком» либо 9450 «Не активирован»),
//                      затем заголовок «имя (Ур. N)», затем строки и серое описание для уровня 1.
//   Категории 2 и 3 (пассив/авто) печатают КОРОТКИЙ набор: профессия, тип, оружие, атрибут — без
//   отката, ОМ, цели и стиля атаки.
// Раскраска строки: невыученный навык эталон печатает целиком серым FFC1C1BE.
static FString SkTipRow(bool bGrey, const FString& Text)
{
	return bGrey ? FString::Printf(TEXT("<FC=FFC1C1BE>%s</F>"), *Text) : Text;
}

// ★★★ ПОДСТАНОВКА ПЛЕЙСХОЛДЕРОВ ОПИСАНИЯ — `CSkillInfo_BuildSkillDescription@0xEB2D50`.
//   Описание в `Skilltooltip.BIN` хранится с токенами; движок гоняет цикл, пока в строке остаётся
//   хоть один токен (`CSkillInfo_HasStatTokens@0xEB2880`), и заменяет их посчитанными числами.
//   Перед токеном может стоять префикс «[NNN]» — идентификатор ДРУГОГО навыка, из чьей записи брать
//   числа (`skillDesc_parseBracketTag@0xEB2B80`: смотрит 5 символов левее и стирает их).
//
//   | токен | формула (L = уровень, поля записи `.sif`) |
//   |---|---|
//   | [ms]    | L·msPerLevel + msBase; ★ у skillKind == 2 к результату ДОБАВЛЯЕТСЯ 100 |
//   | [s1]    | L·s1PerLevel + s1Base |
//   | [s2]    | L·s2PerLevel + s2Base |
//   | [s3]    | L·s3PerLevel + s3Base |
//   | [ms_p] [s1_p] [s2_p] [s3_p] | те же величины, делённые на 1000 |
//   | [sr]    | L·srPerLevel + srBase |
//   | [sr_c]  | (L·srPerLevel + srBase) / srCostDivisor |
//   | [sdt1..3] | buffNDuration + L·buffNDurationPerLevel (мс) → «Nмин Mсек» либо «Nсек» |
//   | [sdtt1..3]| buffNCooldown / 1000 → «Nсек» |
//
//   ⚠ ДЕФЕКТ ОРИГИНАЛА: в `CSkillInfo_HasStatTokens` строка «[sdtt2]» проверяется ДВАЖДЫ, а
//   «[sdtt3]» — ни разу. Описание, где есть ТОЛЬКО [sdtt3], в цикл не входит и токен остаётся сырым.
//   Здесь дефект исправлен (CVar `asda.Skill.LegacyTokenLoop 1` возвращает поведение эталона).
FString UAsdaSkillWindowSubsystem::ExpandSkillDescription(int32 SkillCode, int32 Level) const
{
	const FAsdaSkillDef* D0 = Db ? Db->Get(SkillCode) : nullptr;
	if (!D0) { return FString(); }
	FString S = D0->Description;
	if (S.IsEmpty()) { return TEXT("Not Exist Explanation"); }

	// Нотисы единиц времени берём из таблицы (6824 «сек.», 12869 «мин.» — см. ниже по коду).
	const FString SecUnit = LocRaw(kLocSecUnit);
	const FString MinUnit = LocRaw(kLocMinUnit);

	static const TCHAR* kTokens[] = {
		TEXT("[ms]"), TEXT("[s1]"), TEXT("[s2]"), TEXT("[s3]"),
		TEXT("[ms_p]"), TEXT("[s1_p]"), TEXT("[s2_p]"), TEXT("[s3_p]"),
		TEXT("[sr]"), TEXT("[sr_c]"),
		TEXT("[sdt1]"), TEXT("[sdt2]"), TEXT("[sdt3]"),
		TEXT("[sdtt1]"), TEXT("[sdtt2]"), TEXT("[sdtt3]") };
	const int32 kTokenNum = UE_ARRAY_COUNT(kTokens);

	// Цикл, как в эталоне: пока в строке остаётся хоть один токен. Ограничитель прогонов — защита
	//   от «неразрешимого» токена (в эталоне такой случай приводил бы к вечному циклу).
	for (int32 Guard = 0; Guard < 64; ++Guard)
	{
		int32 TokIdx = INDEX_NONE, Pos = INDEX_NONE;
		for (int32 i = 0; i < kTokenNum; ++i)
		{
			// ⚠ Дефект оригинала: [sdtt3] в проверке цикла отсутствует.
			if (CVarSkillLegacyTokenLoop.GetValueOnGameThread() != 0 && i == kTokenNum - 1) { continue; }
			const int32 P = S.Find(kTokens[i], ESearchCase::CaseSensitive, ESearchDir::FromStart);
			if (P != INDEX_NONE && (Pos == INDEX_NONE || P < Pos)) { Pos = P; TokIdx = i; }
		}
		if (TokIdx == INDEX_NONE) { break; }

		// Префикс «[NNN]» ровно за 5 символов до токена — взять числа из ДРУГОГО навыка.
		const FAsdaSkillDef* D = D0;
		if (Pos >= 5)
		{
			const FString Pre = S.Mid(Pos - 5, 5);
			if (Pre.StartsWith(TEXT("[")) && Pre.EndsWith(TEXT("]")))
			{
				const FString Digits = Pre.Mid(1, 3);
				if (Digits.IsNumeric())
				{
					const int32 Linked = FCString::Atoi(*Digits);
					if (const FAsdaSkillDef* LD = Db->Get(Linked)) { D = LD; }
					S.RemoveAt(Pos - 5, 5);
					Pos -= 5;
				}
			}
		}

		// Длительность в мс → «Nмин Mсек» / «Nмин» / «Nсек» (эталонная развилка по минутам).
		auto FmtDuration = [&SecUnit, &MinUnit](int32 Ms) -> FString
		{
			const int32 Minutes = Ms / 60000;
			if (Minutes <= 0) { return FString::Printf(TEXT("%d%s"), Ms / 1000, *SecUnit); }
			const int32 Rest = (Ms % 60000) / 1000;
			if (Rest <= 0)   { return FString::Printf(TEXT("%d%s"), Minutes, *MinUnit); }
			return FString::Printf(TEXT("%d%s %d%s"), Minutes, *MinUnit, Rest, *SecUnit);
		};

		FString Val;
		switch (TokIdx)
		{
		case 0: { int32 V = Level * D->MsPerLevel + D->MsBase; if (D->Kind == 2) { V += 100; } Val = FString::FromInt(V); } break;
		case 1:  Val = FString::FromInt(Level * D->S1PerLevel + D->S1Base); break;
		case 2:  Val = FString::FromInt(Level * D->S2PerLevel + D->S2Base); break;
		case 3:  Val = FString::FromInt(Level * D->S3PerLevel + D->S3Base); break;
		case 4:  Val = FString::FromInt((Level * D->MsPerLevel + D->MsBase) / 1000); break;
		case 5:  Val = FString::FromInt((Level * D->S1PerLevel + D->S1Base) / 1000); break;
		case 6:  Val = FString::FromInt((Level * D->S2PerLevel + D->S2Base) / 1000); break;
		case 7:  Val = FString::FromInt((Level * D->S3PerLevel + D->S3Base) / 1000); break;
		case 8:  Val = FString::FromInt(Level * D->SrPerLevel + D->SrBase); break;
		case 9:  Val = FString::FromInt((Level * D->SrPerLevel + D->SrBase) / FMath::Max(1, D->SrCostDivisor)); break;
		case 10: Val = FmtDuration(D->Buff0Duration + Level * D->Buff0DurationPerLevel); break;
		case 11: Val = FmtDuration(D->Buff1Duration + Level * D->Buff1DurationPerLevel); break;
		case 12: Val = FmtDuration(D->Buff2Duration + Level * D->Buff2DurationPerLevel); break;
		case 13: Val = FString::Printf(TEXT("%d%s"), D->Buff0Cooldown / 1000, *SecUnit); break;
		case 14: Val = FString::Printf(TEXT("%d%s"), D->Buff1Cooldown / 1000, *SecUnit); break;
		case 15: Val = FString::Printf(TEXT("%d%s"), D->Buff2Cooldown / 1000, *SecUnit); break;
		default: break;
		}
		S.RemoveAt(Pos, FCString::Strlen(kTokens[TokIdx]));
		S.InsertAt(Pos, Val);
		// ★ [ИСПРАВЛЕНИЕ] 05.11: у длительностей (токены 10..15) — снять повтор единицы из текста (`asda.Skill.DurUnitDedup`).
		if (TokIdx >= 10) { AsdaSkillDropDupUnit(S, Pos + Val.Len(), Val); }
	}
	return S;
}

// Категория навыка (0..3) — она же индекс цвета заголовка и подписи «Тип навыка».
int32 UAsdaSkillWindowSubsystem::SkillCategoryOf(const FAsdaSkillDef& D)
{
	if (D.Kind == 2) { return 0; }
	if (D.Kind == 1)
	{
		if (D.SubType == 3) { return 2; }
		if (D.SubType == 4) { return 3; }
	}
	return 1;
}

// ══ ★★★ ПОДСКАЗКА НАВЫКА СОУЛМЕЙТА — `SkillTooltip_BuildSoulmateTooltip@0xEAF450` ═══════════════
//   (skill_buff.c:36778-37105). Две таблицы:
//     `skillFindEntryById@0xE52980` → SoulMateSkillTable.BIN, 272 б: имя `+4`, иконка `+36`,
//                                     описание `+100`  (в порту DataTable `soulmateskilltable`);
//     `CSkillMgr_GetSoulmateRecord@0xACF080` → таблица 112 б (в порту `soulmateskill`, 45 записей):
//        `+42` ReqLevel · `+46` Cost (÷1000 = ОМ) · `+50` Stat1Type · `+51` Stat2Type ·
//        `+52` Range · `+53` CooldownType · `+76` StatBonus (i16) · `+87` ClassReq.
//   Итоговый текст (эталонный sprintf):
//     "%s\n%s\n%s : %d\n%s : %d%s\n%s\n%s\n%s\n%s\n\n\n%s"
//     класс, уровень, «бонус» : StatBonus, «ОМ» : cost + суффикс, стат1, стат2, дальность, откат, описание
//   Заголовок красный (`<FC=FFFF0000>`), если навык применить НЕЛЬЗЯ: нет соулмейта, не хватает
//   уровня, либо не подходит класс (`+87`: 1 = «тот же класс», 2 = «другой класс»).
bool UAsdaSkillWindowSubsystem::BuildSoulmateSkillTooltip(int32 SkillId, FString& OutTitle, FString& OutBody) const
{
	OutTitle.Reset(); OutBody.Reset();
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	UDataTable* DtName = IT->GetDataTable(FName(TEXT("soulmateskilltable")));
	UDataTable* DtStat = IT->GetDataTable(FName(TEXT("soulmateskill")));
	if (!DtName || !DtStat) { return false; }

	const FAsdaSoulmateskilltableRow* NameRow = nullptr;
	for (const TPair<FName, uint8*>& It : DtName->GetRowMap())
	{
		const FAsdaSoulmateskilltableRow* R = reinterpret_cast<const FAsdaSoulmateskilltableRow*>(It.Value);
		if (R && R->Id == SkillId) { NameRow = R; break; }        // эталон: линейный поиск по record[0]
	}
	const FAsdaSoulmateskillRow* StatRow = nullptr;
	for (const TPair<FName, uint8*>& It : DtStat->GetRowMap())
	{
		const FAsdaSoulmateskillRow* R = reinterpret_cast<const FAsdaSoulmateskillRow*>(It.Value);
		if (R && R->Id == SkillId) { StatRow = R; break; }
	}
	// Эталон: `if (!pSkillData || !pSoulmateRecord || !pInvField142C) return;`
	if (!NameRow || !StatRow) { return false; }

	// ── Имя и описание из 272-байтной записи (поля +4 и +100 читаем из сырого DataHex) ──
	FString Name = NameRow->DisplayName;
	FString Desc;
	{
		TArray<uint8> Raw;
		if (AsdaBytes::HexToBytes(NameRow->DataHex, Raw))
		{
			if (Name.IsEmpty()) { Name = AsdaBytes::AsciiFieldAt(Raw, 4, 32); }
			Desc = AsdaBytes::AsciiFieldAt(Raw, 100, 168);
		}
	}
	// ★ В ЭТОЙ сборке поля +4 и +100 у всех 45 записей пустые (проверено по самому файлу
	//   `soulmateskilltable.bin`) — эталон нарисовал бы подсказку БЕЗ заголовка. Дефект ДАННЫХ
	//   оригинала: подставляем внутреннее имя из 112-байтной таблицы (закон fix-original-defects).
	if (Name.IsEmpty()) { Name = StatRow->Name; }
	// ★ [ПОРТ] 09.10.2026 (мультиязычность): заголовок и описание — на языке сессии. Ключи: `soulmateskilltable` `<_row>.DisplayName`
	//   (описание из сырого поля +100 — `<_row>.Text`), запасное имя — `soulmateskill` `<_row>.Name`.
	if (AsdaLangRows::Active())
	{
		const FString RuName = Name;
		Name = AsdaLangRows::Text(DtName, NameRow, TEXT("DisplayName"), NameRow->DisplayName);
		if (Name.IsEmpty()) { Name = AsdaLangRows::Text(DtStat, StatRow, TEXT("Name"), StatRow->Name); }
		if (Name.IsEmpty()) { Name = RuName; }
		Desc = AsdaLangRows::Text(DtName, NameRow, TEXT("Text"), Desc);
	}

	// ── Годность: уровень, класс, наличие соулмейта ──
	const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	const UAsdaSkillUseSubsystem* Use = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
	const int32 SelfLvl = AsdaSelf::GetLevel(Net);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5c): уровень — общая точка `AsdaSelf::GetLevel` (до 4005/4036 — уровень выбранного персонажа, а не 0)
	// ★ 07.11.2026 [ОРИГИНАЛ] (`asda.Soul.TrayFlow`): эталон сравнивает `rec+42` с `персонаж[2227]` = блок +12 = уровень ДРУЖБЫ (запись 5220 @18), а не с уровнем персонажа.
	const UAsdaSoulmateSubsystem* SmLvl = GI->GetSubsystem<UAsdaSoulmateSubsystem>();
	bool bLevelOk = ((AsdaSoulTrayFlowOn() && SmLvl) ? SmLvl->GetFriendLevel() : SelfLvl) >= StatRow->ReqLevel;
	// Класс: эталон сравнивает СВОЙ класс с классом соулмейта (`[1079]` против `[2239]`).
	//   Пары соулмейтов в порту ещё нет ⇒ условие «класс подходит» не проверяем и это записано.
	const bool bClassOk = true;
	// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Soul.SlotTruth`): `SkillTooltip_BuildSoulmateTooltip` красит заголовок, если не `gap_8B4[6] == 1 && gap_878[47] != −1` — Друг в игре.
	const UAsdaHotbarSubsystem* HbMate = GI->GetSubsystem<UAsdaHotbarSubsystem>();
	const bool bHasMate = AsdaSoulSlotTruthOn() ? (HbMate && HbMate->IsFriendInGame()) : (Use && Use->HasSoulmate());
	const bool bUsable = bLevelOk && bClassOk && bHasMate;

	OutTitle = bUsable ? Name : FString::Printf(TEXT("<FC=FFFF0000>%s</F>"), *Name);

	// ── Строка класса (`+87`) ──
	FString ClassLine;
	{
		const int32 CReq = StatRow->ClassReq;
		const FString Label = LocRaw(2306);
		if (CReq == 1 || CReq == 2)
		{
			const FString Val = LocRaw(CReq == 1 ? 8485 : 8486);
			ClassLine = bClassOk ? FString::Printf(TEXT("%s : %s"), *Label, *Val)
			                     : FString::Printf(TEXT("%s : <FC=FFFF0000>%s</F>"), *Label, *Val);
		}
		else
		{
			ClassLine = FString::Printf(TEXT("%s : %s"), *Label, *LocRaw(8501));
		}
	}
	// ── Строка уровня (`+42`) ──
	const FString LevelBody = FString::Printf(TEXT("%s %d %s %s"),
		*LocRaw(8014), StatRow->ReqLevel, *LocRaw(12215), *LocRaw(2331));
	const FString LevelLine = bLevelOk
		? FString::Printf(TEXT("%s : %s"), *LocRaw(8487), *LevelBody)
		: FString::Printf(TEXT("%s : <FC=FFFF0000>%s</F>"), *LocRaw(8487), *LevelBody);

	// ── Характеристики (`+50` и `+51`) — таблицы значений сняты из эталона один в один ──
	auto StatLine = [this](int32 LabelLoc, const int32* Map, int32 MapN, int32 Type) -> FString
	{
		if (Type < 0 || Type >= MapN) { return FString(); }        // эталонный `default: break;`
		return FString::Printf(TEXT("%s : %s"), *LocRaw(LabelLoc), *LocRaw(Map[Type]));
	};
	static const int32 kStat1[5] = { 2339, 8014, 8489, 2341, 2338 };
	static const int32 kStat2[6] = { 2339, 8014, 8489, 8490, 8491, 2338 };
	const FString Stat1 = StatLine(2332, kStat1, 5, StatRow->Stat1Type);
	const FString Stat2 = StatLine(2333, kStat2, 6, StatRow->Stat2Type);

	// ── Дальность (`+52`, i8) ──
	const FString RangeLine = (StatRow->Range <= 0)
		? FString::Printf(TEXT("%s : %s"), *LocRaw(8492), *LocRaw(8493))
		: FString::Printf(TEXT("%s : %s %d M %s"), *LocRaw(8492), *LocRaw(8499), StatRow->Range, *LocRaw(8498));

	// ── Откат (`+53`: 0 = нет, 1, 2) ──
	FString CoolLine;
	{
		const int32 CT = StatRow->CooldownType;
		const int32 ValLoc = (CT == 0) ? 8494 : (CT == 1 ? 8495 : (CT == 2 ? 8496 : 0));
		if (ValLoc) { CoolLine = FString::Printf(TEXT("%s : %s"), *LocRaw(9475), *LocRaw(ValLoc)); }
	}

	// ── Сборка тела (эталонный формат, включая три пустые строки перед описанием) ──
	// ★ 07.11.2026 [ОРИГИНАЛ]: навыки 30/36/41 лоток показывает своей подсказкой `CSoulmateUI_BuildRecordTooltip@0xEB0230` —
	//   класс, уровень, вторая характеристика, дальность, описание (`SoulmateUI_HandleEvent` msg 8). На панель они не кладутся.
	if (AsdaSoulTrayFlowOn() && (SkillId == 30 || SkillId == 36 || SkillId == 41))
	{
		OutBody = FString::Printf(TEXT("%s\n%s\n%s\n%s\n\n\n%s"), *ClassLine, *LevelLine, *Stat2, *RangeLine, *Desc);
		return true;
	}
	const int32 ManaCost = StatRow->CooldownMs / 1000;                   // эталон: `*(_DWORD*)(rec+46) / 1000`
	OutBody = FString::Printf(TEXT("%s\n%s\n%s : %d\n%s : %d%s\n%s\n%s\n%s\n%s\n\n\n%s"),
		*ClassLine, *LevelLine,
		*LocRaw(8500), StatRow->StatBonus,
		*LocRaw(8488), ManaCost, *LocRaw(6824),
		*Stat1, *Stat2, *RangeLine, *CoolLine, *Desc);
	return true;
}

void UAsdaSkillWindowSubsystem::BuildSkillTooltip(int32 SkillCode, int32 Level, bool bAvailable,
                                                  bool bAllowNextPreview, FString& OutTitle, FString& OutBody) const
{
	OutTitle.Reset(); OutBody.Reset();
	const FAsdaSkillDef* Dp = Db ? Db->Get(SkillCode) : nullptr;
	if (!Dp) { return; }
	const FAsdaSkillDef& D = *Dp;

	const int32 Cat  = SkillCategoryOf(D);
	const int32 Max  = D.MaxLevel;
	const bool  bNotLearned = (Level == 0);

	// ── Заголовок: «имя (Ур. текущий/максимум)» цветом категории, красным при недоступности ──
	{
		static const TCHAR* kCatColor[4] = { TEXT("FFEA9311"), TEXT("FFF0EB1E"), TEXT("FF23ADEB"), TEXT("FF20A43E") };
		// Эталон считает «нормальным» цвет только когда запись доступна И (для активных) уровень > 0.
		const bool bNormal = (Cat == 0) ? bAvailable : (bAvailable && Level > 0);
		const FString Color = bNormal ? kCatColor[Cat] : TEXT("FFFF0000");
		OutTitle = FString::Printf(TEXT("<FC=%s>%s (%s %d/%d)</F>"),
			*Color, *D.Name, *LocRaw(kLocLevelShort), Level, Max);
	}

	// ── Готовим строки тела ──
	auto Row = [this, bNotLearned](int32 CaptionLoc, const FString& Value) -> FString
	{
		return SkTipRow(bNotLearned, FString::Printf(TEXT("%s : %s"), *LocRaw(CaptionLoc), *Value));
	};

	// Профессия (ступень) — `Skilltooltip.BIN` +500, значения 1..5 → нотисы 8428..8432.
	FString RowJob;
	if (D.JobStepReq >= 1 && D.JobStepReq <= 5) { RowJob = Row(kLocTipJobStep, LocRaw(8427 + D.JobStepReq)); }

	// Тип навыка.
	const FString RowKind = Row(kLocTipSkillKind, LocRaw(9507 + Cat));

	// Оружие: перечисление битов маски; ни одного — «Нет».
	FString RowWeapon;
	{
		FString List;
		for (int32 Bit = 0; Bit < 19; ++Bit)
		{
			if ((D.WeaponMask & (int64(1) << Bit)) == 0) { continue; }
			if (!List.IsEmpty()) { List += TEXT(", "); }
			List += LocRaw(9455 + Bit);
		}
		if (List.IsEmpty()) { List = LocRaw(kLocTipNone); }
		RowWeapon = Row(kLocTipWeapon, List);
	}

	// Атрибут (стихия).
	const FString RowAttr = Row(kLocTipAttr, LocRaw(D.ElementAttr == 6 ? 9497 : 9498 + D.ElementAttr));

	// Откат и затраты ОМ — только у категорий 0 и 1.
	const FString RowCool = Row(kLocTipCooldown,
		FString::Printf(TEXT("%d %s"), D.CooldownMs / 1000, *LocRaw(kLocSecUnit)));
	const FString RowMp = SkTipRow(bNotLearned, FString::Printf(TEXT("%s : %d MP"),
		*LocRaw(kLocTipMpCost), Level * D.MpCostPerLevel + D.MpCostBase));

	// Стиль атаки — только у категории 0 (resourceType 1/2/3).
	FString RowStyle;
	{
		int32 L = 0;
		if (D.ResourceType == 1) { L = 9476; } else if (D.ResourceType == 2) { L = 9478; } else if (D.ResourceType == 3) { L = 9477; }
		if (L) { RowStyle = Row(kLocTipAtkStyle, LocRaw(L)); }
	}

	// Тип каста и тип цели — у всех категорий, кроме 2 (пассив).
	FString RowCast, RowTarget;
	if (Cat != 2)
	{
		RowCast = Row(kLocTipCastSel, LocRaw(9483 + D.CastType));
		if (D.TargetSlotType != 0) { RowTarget = Row(kLocTipTargetSel, LocRaw(9488 + D.TargetSlotType)); }
	}

	// Необходимый навык + необходимо очков (одной двухстрочной секцией, как в эталоне).
	FString RowReq;
	{
		// Цепочка: `skillGetEffectValues@0xE57F30` — доп-скиллы `.sif` +0xC8 / +0xCB.
		FString Chain;
		auto ChainPart = [this](int32 Code, int32 Lvl) -> FString
		{
			const FAsdaSkillDef* CD = Db->Get(Code);
			return CD ? FString::Printf(TEXT("%s %d %s %s"), *CD->Name, Lvl,
				*LocRaw(kLocLevelShort), *LocRaw(kLocTipAtLeast)) : FString();
		};
		const bool bC0 = D.EffectSkillId0 > 0 && Db->Get(D.EffectSkillId0) != nullptr;
		const bool bC1 = D.EffectSkillId1 > 0 && Db->Get(D.EffectSkillId1) != nullptr;
		if (bC0 && bC1) { Chain = ChainPart(D.EffectSkillId0, D.EffectLevel0) + TEXT(",") + ChainPart(D.EffectSkillId1, D.EffectLevel1); }
		else if (bC0)   { Chain = ChainPart(D.EffectSkillId0, D.EffectLevel0); }
		if (Chain.IsEmpty()) { Chain = LocRaw(kLocTipNone); }

		// Требуемые очки: «{9551} {gradeNum} {9399 + grade}»; grade 0 → 9552; gradeNum <= 0 → 9555.
		FString Points;
		const int32 GradeLoc = D.Grade + 9399;
		if (GradeLoc < 9400)          { Points = LocRaw(9552); }
		else if (D.GradeNum <= 0)     { Points = LocRaw(9555); }
		else
		{
			Points = FString::Printf(TEXT("%s %d %s"), *LocRaw(9551), D.GradeNum,
				*LocRaw(D.Grade < 10 ? GradeLoc : kLocJob4Button));
		}
		RowReq = SkTipRow(bNotLearned, FString::Printf(TEXT("%s : %s \n%s : %s"),
			*LocRaw(kLocTipReqSkill), *Chain, *LocRaw(kLocTipReqPoints), *Points));
	}

	// Требования 4-й профессии — только при grade >= 10.
	FString RowJob4;
	if (D.Grade >= 10)
	{
		const int32 Band = GetSelfBand();
		if (Band >= 1 && Band <= 3)
		{
			const int32 B = 9400 + 3 * (Band - 1);
			RowJob4 = SkTipRow(bNotLearned, FString::Printf(TEXT("%s : %s,%s,%s %s %s %d %s"),
				*LocRaw(kLocTipReqStats), *LocRaw(B), *LocRaw(B + 1), *LocRaw(B + 2),
				*LocRaw(12539), *LocRaw(9429), 60, *LocRaw(9551)));
		}
	}

	// ── Сборка по состоянию (см. шапку) ──
	TArray<FString> Lines;
	auto Push = [&Lines](const FString& S) { if (!S.IsEmpty()) { Lines.Add(S); } };

	const bool bFull = (Cat == 0 || Cat == 1);           // категории 2/3 печатают короткий набор
	const bool bMaxed = bAvailable && (Level >= Max);
	const bool bStateRow = !bAvailable || (Level == 0);  // варианты H

	FString Header, NextTitle, NextDesc;
	if (bStateRow)
	{
		// 9505 «Ты можешь овладеть этим навыком» — когда окно разрешает превью следующего уровня;
		//   9450 «Не активирован» — когда нет.
		Header = SkTipRow(bNotLearned, LocRaw(bAllowNextPreview ? kLocTipCanLearn : kLocTipNotActive));
	}

	const int32 DescLevel = bStateRow ? 1 : (bMaxed ? Max : Level);
	FString Desc = ExpandSkillDescription(SkillCode, DescLevel);
	if (bStateRow || bNotLearned) { Desc = SkTipRow(true, Desc); }

	if (bStateRow || (!bMaxed && Level > 0))
	{
		NextTitle = FString::Printf(TEXT("<FC=FFC1C1BE>%s (%d %s)</F>"), *D.Name, Level + 1, *LocRaw(kLocLevelShort));
	}
	if (!bStateRow && !bMaxed && Level > 0)
	{
		NextDesc = SkTipRow(true, ExpandSkillDescription(SkillCode, Level + 1));
	}

	if (bStateRow)
	{
		Push(Header);
		Lines.Add(FString());          // пустая строка-разделитель
		Push(NextTitle);
		Lines.Add(FString());
	}
	Push(RowJob);
	Push(RowJob4);
	Push(RowReq);
	Push(RowKind);
	Push(RowWeapon);
	if (bFull)
	{
		Push(RowAttr);
		Push(RowCool);
		Push(RowMp);
		if (Cat == 0) { Push(RowStyle); }
		Push(RowCast);
		Push(RowTarget);
	}
	else
	{
		Push(RowAttr);
	}
	Lines.Add(FString());
	Push(Desc);
	if (!NextDesc.IsEmpty())
	{
		Lines.Add(FString()); Lines.Add(FString()); Lines.Add(FString());
		Push(NextTitle);
		Lines.Add(FString());
		Push(NextDesc);
	}
	OutBody = FString::Join(Lines, TEXT("\n"));
}

void UAsdaSkillWindowSubsystem::ShowSlotTooltip(int32 SlotIdx)
{
	if (!Slots.IsValidIndex(SlotIdx) || !Db) { return; }
	const int32 Code = Slots[SlotIdx].SkillCode;
	const FAsdaSkillDef* D = Db->Get(Code);
	if (!D) { return; }
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }

	const FAsdaLearnedSkill* L = Learned.Find(Code);
	const int32 Level = L ? L->Level : 0;
	const bool bAvail = L ? L->bAvailable : false;

	FString Title, Body;
	// Эталон зовёт построитель с `allowNextLevelPreview = 1`, если запись помечена доступной.
	BuildSkillTooltip(Code, Level, bAvail, /*bAllowNextPreview*/ bAvail, Title, Body);

	FAsdaTipMetrics M;
	M.FontPx = TipFontPx;
	M.MaxW = (float)TipMaxWidth;
	Tip->ShowTextTip(EAsdaTipOwner::SkillWin, Title, Body, M);
	bTipShown = true;
}

// Подсказка левой панели «Хранитель души». Эталон:
//   56302/56308 (портрет) → `SkillTooltip_ShowSkillTooltipB@0xE59410` — описание ПАССИВНОГО баффа (+12),
//                           заголовок = имя записи SoulGuarderTable (+16);
//   56304/56310 (иконка)  → `SkillTooltip_ShowSkillTooltipA@0xE58CB0` — описание НАВЫКА хранителя (+152),
//                           заголовок = имя навыка.
//   Тело у обеих одинаковое: «110502 <12301/12312/12323 + ступень>», «110503 <9400 + SkillType−1>»,
//   «110506», затем описание навыка из `Skilltooltip.BIN` (с подставленными токенами).
void UAsdaSkillWindowSubsystem::ShowSoulTooltip(int32 RowIdx, bool bBig)
{
	if (!SoulRows.IsValidIndex(RowIdx) || !Db) { return; }
	const FAsdaSoulPreviewRow& R = SoulRows[RowIdx];
	if (R.GuardIndex < 0) { return; }
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }

	const UAsdaSoulGuardSubsystem* Soul = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulGuardSubsystem>() : nullptr;
	const FAsdaSoulGuarderRow* Rec = Soul ? Soul->FindGuarderByIndex(R.GuardIndex) : nullptr;
	if (!Rec) { return; }

	const int32 DescSkill = bBig ? Rec->BuffSkillId : Rec->GuardianSkillId;
	const FAsdaSkillDef* D = Db->Get(DescSkill);

	// `skillTooltipResolveNoticeId@0xE59120`: SkillType 1..3 → 12301+ступень, 4..6 → 12312+, 7..9 → 12323+.
	static const int32 kTierBase[3] = { 12301, 12312, 12323 };
	const int32 TypeGroup = (Rec->SkillType >= 1 && Rec->SkillType <= 9) ? (Rec->SkillType - 1) / 3 : -1;
	const int32 TierLoc = (TypeGroup >= 0 && Rec->RowIndex >= 0 && Rec->RowIndex <= 2)
	                    ? (kTierBase[TypeGroup] + Rec->RowIndex) : 0;
	// `skillResolveNoticeId9400@0xE59070`: SkillType 1..9 → 9400..9408.
	const int32 TreeLoc = (Rec->SkillType >= 1 && Rec->SkillType <= 9) ? (9400 + Rec->SkillType - 1) : 0;

	FString Body;
	if (TierLoc > 0) { Body += FString::Printf(TEXT("%s %s\n"), *LocRaw(kLocSoulTipLevel), *LocRaw(TierLoc)); }
	if (TreeLoc > 0) { Body += FString::Printf(TEXT("%s %s\n"), *LocRaw(kLocSoulTipTree),  *LocRaw(TreeLoc)); }
	Body += LocRaw(kLocSoulTipDesc);
	if (D)
	{
		const FString Desc = ExpandSkillDescription(DescSkill, 1);
		if (!Desc.IsEmpty()) { Body += TEXT("\n") + Desc; }
	}

	const FString Title = bBig ? Rec->Name : (D ? D->Name : Rec->Name);
	FAsdaTipMetrics M; M.FontPx = TipFontPx; M.MaxW = (float)TipMaxWidth;
	Tip->ShowTextTip(EAsdaTipOwner::SkillWin, Title, Body, M);
	bTipShown = true;
}

void UAsdaSkillWindowSubsystem::HideOwnTooltip()
{
	if (!bTipShown) { return; }
	// ★ Закон ui-shared-tip-ownership: гасим ТОЛЬКО свою плашку.
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::SkillWin);
	}
	bTipShown = false;
}
