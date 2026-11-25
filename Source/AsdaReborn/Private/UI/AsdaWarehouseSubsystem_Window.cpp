// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWarehouseSubsystem_Window.cpp — ★ R3-c 12.11.2026 (разрез AsdaWarehouseSubsystem.cpp, дословный перенос): окно и отзыв, режимы, подписи категорий, вкладки, сумки, ячейки-позиции, запрос вкладки.
//   Общее для частей — в AsdaWarehouseSubsystem_Private.h; шапка и реверс — в AsdaWarehouseSubsystem.cpp.
#include "UI/AsdaWarehouseSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)


#include "Components/CanvasPanelSlot.h"
#include "Login/AsdaButtonRelay.h"
#include "AsdaWarehouseSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWarehouseSubsystem*.cpp

static const TCHAR* kRecallWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_Warehouse_Win.WBP_Warehouse_Win_C");

namespace WareLoc {
	constexpr int32 kHdrTake      = 2248;     // 4038 «Item to get»
	constexpr int32 kHdrKeep      = 2249;     // 4038 «Item to keep»

	// Окно 5500.
	constexpr int32 kRecallTitle  = 10301;    // 5501 «Recall the Warehouse»
	constexpr int32 kRecallTake   = 10303;    // 5502 «Retrieve Item»
	constexpr int32 kRecallKeep   = 10304;    // 5503 «Store Item»
	constexpr int32 kRecallCancel = 5035;     // 5504 «Close»
	constexpr int32 kRecallAsk    = 10302;    // модальное ДА/НЕТ «Прекратить использование свитка?»
}

// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D14. `Ware_Title_Output` / `Ware_Title_Input` — заготовки
//   подписи режима в заголовке окна (`.JUI`, `Visible = 0`, текст «: 보관»), которые код так и не
//   включил, поэтому заголовок 4000 не меняется между режимами. 1 = ПОКАЗЫВАТЬ подпись режима.
//   По умолчанию 0 — как в оригинале (заголовок без суффикса), правка косметическая.
static ASDA_DEV_CVAR(int32, CVarWareModeTitle,
	TEXT("asda.Warehouse.ModeTitle"), 0,
	TEXT("1 = дописывать в заголовок окна 4000 подпись режима (заготовки D14 оригинала); 0 = как в эталоне."),
	ECVF_Default);

// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D19. В режиме «ХРАНИТЬ» эталон гасит блок вкладок, но рамку
//   вокруг него (`Ware_BrownBox_grp`, две коричневые полосы с 69 px пустоты между ними) оставляет
//   на месте — под заголовком зияет чёрная полоса ~104 px. Мы её схлопываем и поднимаем всё, что
//   ниже. 1 = вернуть эталонное поведение с пустой полосой.
static ASDA_DEV_CVAR(int32, CVarWareLegacyEmptyTabArea,
	TEXT("asda.Warehouse.LegacyEmptyTabArea"), 0,
	TEXT("1 = эталонный дефект D19: пустая полоса на месте блока вкладок в режиме «хранить»; 0 = схлопывать."),
	ECVF_Default);

bool UAsdaWarehouseSubsystem::GetKeepListAbsRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	// Левый верх области 4189 = левый верх ячейки 0 (как у полосы, `LayoutScrollBar`); размер — из `.JUI`
	//   (kListWidth × kListHeight) в единицах канваса, переведённых масштабом живой геометрии ячейки.
	if (Mode != kModeKeep || !Widget || !GridUi.IsValidIndex(0) || !GridUi[0].Icon) { return false; }
	const UImage* Cell0 = GridUi[0].Icon.Get();                       // ячейка 0 сетки — якорь области списка
	if (!Cell0->IsVisible()) { return false; }
	const FGeometry& G = Cell0->GetCachedGeometry();                  // живая геометрия последнего кадра
	const float S = G.GetAccumulatedLayoutTransform().GetScale();      // единицы канваса → абсолютные пиксели
	if (S <= 0.f || G.GetLocalSize().IsNearlyZero()) { return false; }
	OutMin = G.GetAbsolutePosition();
	OutMax = OutMin + FVector2D(kListWidth, kListHeight) * S;
	return true;
}

// ═════════════════════════ ОКНО 5500: СБОРКА ══════════════════════════════════════════════════
bool UAsdaWarehouseSubsystem::EnsureRecallWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kRecallWindowId, kRecallWbp, kRecallZ).Ensure(GetGameInstance(), RecallWidget, RcBtnTake != nullptr,
		[](UAsdaWindowSubsystem&) {},   // как было: протухшее окно отдельно не сносится
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.SetWindowPriority(kRecallWindowId, true);
			SetupRecallControls();
			WM.RegisterUiZone(kRecallWindowId, FAsdaUiZoneRect::CreateWeakLambda(this,
				[this](FVector2D& Mn, FVector2D& Mx) -> bool
				{
					if (!bRecallActive || !RecallWidget) { return false; }
					return AsdaUi::FramePlanesAbsRect(RecallWidget, TEXT("Warehouse_Win_0_%d"), 9, Mn, Mx);
				}));
		},
		[]()
		{
			UE_LOG(LogAsdaWare, Error, TEXT("[склад] WBP_Warehouse_Win не загрузился (%s)"), kRecallWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && RcBtnTake != nullptr;
}

void UAsdaWarehouseSubsystem::SetupRecallControls()
{
	if (!RecallWidget || !RecallWidget->WidgetTree) { return; }
	TArray<UWidget*> All;
	RecallWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("Warehouse_BT1"))  { RcBtnTake = Btn; continue; }
			if (N == TEXT("Warehouse_BT2"))  { RcBtnKeep = Btn; continue; }
			if (N == TEXT("Warehouse_No_B")) { RcBtnNo   = Btn; continue; }
			if (N == TEXT("Warehouse_X"))    { RcBtnX    = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Warehouse_Top_T"))     { RcTitle   = Tb; continue; }
			if (N == TEXT("Warehouse_BT1_Text"))  { RcCapTake = Tb; continue; }
			if (N == TEXT("Warehouse_BT2_Text"))  { RcCapKeep = Tb; continue; }
			if (N == TEXT("Warehouse_No_B_Text")) { RcCapNo   = Tb; continue; }
		}
	}

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), RecallRelays);   // ★ U20: реле клика — общая точка
	};
	// 5502 → `warehouseUI_BuildAndOpenBag(g_pWarehouseWnd, 0)`, 5503 → тот же вызов с 1
	//   (ui_core.c:78420 и :78456). 5504 и 5509 — ОДНА И ТА ЖЕ ветка: модальное ДА/НЕТ с нотисом
	//   10302; «Да» разбирает `msgBoxOnConfirm@0xD74180` (`case 0x283E`) → `uiHidePanelAndChild`.
	Bind(RcBtnTake, 5502, [this](int32) { OpenWindow(kModeTake); });
	Bind(RcBtnKeep, 5503, [this](int32) { OpenWindow(kModeKeep); });
	auto AskClose = [this](int32)
	{
		if (bConfirmOpen) { return; }
		UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		const FString S = Loc(WareLoc::kRecallAsk);
		if (!WM || S.IsEmpty()) { return; }
		bConfirmOpen = true;
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(S, [this](bool bYes)
		{
			bConfirmOpen = false;
			if (bYes) { CloseRecallPanel(); }
		}, 0.f, 2288, 2289, /*очередь: эталон (1, 10302) ui_core.c:78462*/10302, 1);
	};
	Bind(RcBtnNo, 5504, AskClose);
	Bind(RcBtnX,  5509, AskClose);

	auto SetLoc = [this](UTextBlock* Tb, int32 LocId)
	{
		if (!Tb) { return; }
		const FString S = Loc(LocId);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};
	SetLoc(RcTitle,   WareLoc::kRecallTitle);
	SetLoc(RcCapTake, WareLoc::kRecallTake);
	SetLoc(RcCapKeep, WareLoc::kRecallKeep);
	SetLoc(RcCapNo,   WareLoc::kRecallCancel);
	// ★ Обводку окну 5500 тоже НЕ ставим: по данным оригинала её нет ни у одного его контрола
	//   (см. комментарий у окна 4000 выше). Оформление приведено к оригиналу в самом ассете.

	UE_LOG(LogAsdaWare, Log,
		TEXT("[склад] окно 5500 разобрано: 5502=%s 5503=%s 5504=%s 5509=%s заголовок 5501=%s"),
		RcBtnTake ? TEXT("да") : TEXT("НЕТ"), RcBtnKeep ? TEXT("да") : TEXT("НЕТ"),
		RcBtnNo ? TEXT("да") : TEXT("НЕТ"), RcBtnX ? TEXT("да") : TEXT("НЕТ"),
		RcTitle ? TEXT("да") : TEXT("НЕТ"));
}

// ═════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════════
// `warehouseUI_BuildAndOpenBag@0xE89AC0` (item_inventory.c:58209).
void UAsdaWarehouseSubsystem::OpenWindow(int32 NewMode)
{
	if (NewMode != kModeTake && NewMode != kModeKeep)
	{
		UE_LOG(LogAsdaWare, Warning, TEXT("[склад] OpenWindow: режим %d вне {0,1}"), NewMode);
		return;
	}
	// ★★★ ПАЧКА 20, жалоба 10 — [ИСПРАВЛЕНИЕ]: пока открыт рынок 13000, склад не открывается (молча — своей строки у оригинала нет;
	//   там гейта не было вовсе, и окна ломали друг друга). Ключ `asda.Exchange.ExclusiveWindows` 0 — как в оригинале.
	if (UAsdaExchangeSubsystem::IsMarketWindowOpen(this))
	{
		UE_LOG(LogAsdaWare, Log, TEXT("[склад] OpenWindow(%d): открыт рынок 13000 — склад не открыт (пачка 20, жалоба 10)"), NewMode);
		return;
	}
	if (!EnsureWidget()) { return; }
	// Эталон: если запрошенный режим РАВЕН текущему — не делать ничего.
	if (Mode == NewMode) { return; }
	ApplyMode(NewMode);
}

// `uiInventoryBagOpen@0xE8CD60` (ui_core.c:78785) — вся разница режимов.
void UAsdaWarehouseSubsystem::ApplyMode(int32 NewMode)
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Mode = NewMode;
	ScrollRow = 0;

	if (Mode == kModeKeep)
	{
		// Режим «Хранить»: все восемь вкладок принудительно закрываются, 4188 сбрасывается на пункт 0.
		for (int32 k = 0; k < kBags; ++k) { OnBagButtonToggled(k + 1, false); }
		Category = kCatSpend;
		ApplyModeVisibility(/*эталонный режим*/1);
	}
	else
	{
		ApplyModeVisibility(/*эталонный режим*/2);
		// Первое открытие вкладки 0 → запрос 4050 (эталон делает это ровно здесь).
		if (!bTabLoaded[0]) { RequestTab(0); }
	}

	// Надпись 4038: 2248 «Item to get» в режиме 0, 2249 «Item to keep» в режиме 1.
	if (TxtHeader)
	{
		const FString S = Loc(Mode == kModeKeep ? WareLoc::kHdrKeep : WareLoc::kHdrTake);
		if (!S.IsEmpty()) { TxtHeader->SetText(FText::FromString(S)); }
	}
	// ★★★ ПОДПИСИ ОБЕИХ ВКЛАДОК 4188 (правка 2026-08-27 по жалобе владельца «не могу хранить
	//   снаряжение»). ЧТО БЫЛО: в ассете у переключателя ОДИН `TextBlock` шириной 218 — во всю
	//   ширину ОБЕИХ кнопок (`WAREKEEP_2BUT_OPT0` 44…152 и `OPT1` 154…262), и порт менял его текст
	//   на текущую категорию. Игрок видел ОДНУ надпись («Расходники») и не понимал, что рядом есть
	//   вторая вкладка со СНАРЯЖЕНИЕМ, — то есть категория 1 была фактически недоступна.
	//   КАК СТАЛО: подписываем КАЖДУЮ вкладку своим постоянным текстом (1204 «Расходники» /
	//   1205 «Снаряжение»), а выбранную выделяем яркостью. Второй `TextBlock` создаём сами —
	//   в данных его нет (авторский контрол `TabBar_W108_2Btn` рисовал обе подписи сам).
	SetupCategoryCaptions();
	// ★ D14 (по ключу): подпись режима в заголовке — заготовки `Ware_Title_Input/Output`.
	if (CVarWareModeTitle.GetValueOnGameThread() != 0)
	{
		for (const TObjectPtr<UWidget>& W : AlwaysHidden)
		{
			UTextBlock* Tb = Cast<UTextBlock>(W.Get());
			if (!Tb) { continue; }
			const bool bInput = Tb->GetName() == TEXT("Ware_Title_Input");
			AsdaUi::SetShownNoHit(Tb, (bInput == (Mode == kModeKeep)));
		}
	}

	SyncMirrors();
	RefreshAll();
	if (WM)
	{
		WM->SetWindowVisible(kWareWindowId, true);
		WM->BringWindowToFront(kWareWindowId);
	}
	Mouse.Reset();
	UE_LOG(LogAsdaWare, Log, TEXT("[склад] окно 4000 открыто в режиме %d (%s), вкладка 0 %s"),
		Mode, Mode == kModeKeep ? TEXT("хранить") : TEXT("забрать"),
		bTabLoaded[0] ? TEXT("загружена") : TEXT("запрошена"));
}

// ★★★ Подписи ДВУХ вкладок переключателя 4188. В ассете подпись одна на обе кнопки, поэтому
//   вторую создаём сами и разводим обе по своим кнопкам (см. комментарий в месте вызова).
void UAsdaWarehouseSubsystem::SetupCategoryCaptions()
{
	// ★★★ ЗДЕСЬ БОЛЬШЕ НИЧЕГО НЕ СТРОИТСЯ (правка 2026-08-27 по требованию владельца:
	//   «не надо кодом править, надо в ассете виджета»).
	//   ЧТО БЫЛО: код создавал вторую подпись вкладки `TextBlock`-ом и задавал ей геометрию,
	//   шрифт и цвет — то есть оформление жило в C++.
	//   КАК СТАЛО: обе подписи (`WAREKEEP_2BUT_Text` и `WAREKEEP_2BUT_Text2`) лежат В АССЕТЕ —
	//   их кладёт идемпотентный шаг `Tools/asda_ui_tabbar_captions.py` (класс дефектов импортёра
	//   S4: у переключателя с N кнопками он создаёт ОДНУ подпись во всю ширину; затронуто
	//   52 контрола в 31 окне). Оттуда же берётся их БАЗОВЫЙ ЦВЕТ.
	//   В коде осталось только СОСТОЯНИЕ «какая вкладка активна» — оно в ассет не уносится.
	//   ★ ПОДСВЕТКА АКТИВНОЙ вкладки — ОБЩАЯ ТОЧКА `AsdaUi::ApplyTabCaptionState` (закон
	//   `port-shared-points`): своей копии склад больше не держит, базовый цвет она снимает
	//   ИЗ АССЕТА сама, при первом вызове.
	RefreshCategoryCaptions();
}

// Тексты и подсветка вкладок: у каждой — СВОЙ постоянный текст, активная ярче.
void UAsdaWarehouseSubsystem::RefreshCategoryCaptions()
{
	const FString S0 = Loc(WareLoc::kCatSpend);   // 1204 «Расходники»
	const FString S1 = Loc(WareLoc::kCatShell);   // 1205 «Снаряжение»
	// ★ ЦВЕТА ВКЛАДОК. Базовый (НЕАКТИВНАЯ) берётся ИЗ АССЕТА — его кладёт шаг
	//   `Tools/asda_ui_tabbar_captions.py` (тёмно-серый: скин `TabBar_W108_2Btn` светлый, и
	//   серый текст данных на нём нечитаем — замечание владельца 2026-08-27).
	//   В коде остаётся только подсветка АКТИВНОЙ: это состояние, а не оформление.
	if (TxtCat  && !S0.IsEmpty()) { TxtCat->SetText(FText::FromString(S0)); }
	if (TxtCat2 && !S1.IsEmpty()) { TxtCat2->SetText(FText::FromString(S1)); }
	// Состояние «какая вкладка активна» — через ОБЩУЮ точку: активной ЧЁРНЫЙ, соседней её
	//   базовый цвет ИЗ АССЕТА (первый вызов его и снимает).
	const TArray<UTextBlock*> Caps = { TxtCat.Get(), TxtCat2.Get() };
	AsdaUi::ApplyTabCaptionState(Caps, Category == kCatShell ? 1 : 0, CatIdleColors);
}

// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D19 (жалоба владельца 2026-08-27, скриншот).
//   СИМПТОМ: в режиме «ХРАНИТЬ» под заголовком зияла пустая чёрная полоса высотой ~104 px.
//   ПРИЧИНА (снята живым дампом `Reports/headless_ware_dump.log`, 1024 узла с геометрией):
//     эталон в этом режиме гасит только блок вкладок `WAREKEEP_TOP` (95…203) —
//     `uiWnd_SetModeVisibility@0xE8EB90` меняет ТОЛЬКО видимость и НИЧЕГО не двигает.
//     Но рамка вокруг блока — `Ware_BrownBox_grp` (95…199) — состоит из ДВУХ коричневых полос
//     (верхняя `Ware_BrownBox_12_0` 0…16 и нижняя `Box_Brown_13_0` 85…104), между которыми
//     69 px ПУСТОТЫ. Со скрытым блоком там остаётся голый фон окна `WARE_WIN_0_4` — он тёмный,
//     и получается «дыра». В оригинале она была ровно такой же: это ДЕФЕКТ, а не наша регрессия.
//   ЛЕЧЕНИЕ: в режиме «хранить» прячем рамку целиком и поднимаем всё, что ниже неё, на её высоту,
//     одновременно укорачивая тело окна — пустота исчезает, окно становится компактнее.
//   Откат: `asda.Warehouse.LegacyEmptyTabArea 1` — вернуть эталонную пустую полосу.
void UAsdaWarehouseSubsystem::ApplyTabAreaCollapse(bool bCollapse)
{
	if (CVarWareLegacyEmptyTabArea.GetValueOnGameThread() != 0) { bCollapse = false; }
	if (bCollapse == bTabAreaCollapsed) { return; }
	if (!Widget || !Widget->WidgetTree) { return; }

	// Геометрия рамки — из её же слота, а не константой: если ассет переимпортируют, поедем следом.
	const UCanvasPanelSlot* BoxSlot = AsdaUi::SlotOf(BrownBoxGrp);
	if (!BoxSlot) { return; }
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.BaseShiftKeepsDrag`, самотест `asda.SelfTest.DragRelayout` «склад-режимы»): верх рамки —
	//   из ЗАПОМНЕННОЙ базы, как и якорь ниже. Прежде брался ТЕКУЩИЙ: окно перетащили вниз на 150 → `dY = якорь − верх` < 0 →
	//   выход без возврата полосы, и в режиме «забрать» окно оставалось схлопнутым поверх содержимого.
	const FMargin* BoxBase = AsdaUi::BaseShiftKeepsDrag() ? BaseOffsets.Find(BrownBoxGrp.Get()) : nullptr;
	const FMargin BoxOff = BoxBase ? *BoxBase : BoxSlot->GetOffsets();
	const float Top = BoxOff.Top;             // верх рамки блока вкладок (в дампе 95)

	// ★ Сдвиг считаем НЕ по высоте рамки, а по позиции переключателя категорий 4188: в режиме
	//   «хранить» он — самый верхний ВИДИМЫЙ элемент (в дампе T=181, тогда как рамка идёт до 199).
	//   Пустота — это ровно промежуток «низ заголовка → верх переключателя», то есть 181 − 95 = 86.
	//   Порог сдвига берём оттуда же, иначе сам переключатель остался бы на месте и накрыл сетку.
	// ★★★ ИСПРАВЛЕНО 2026-08-27 (жалоба владельца: «окно не перестраивается, если было открыто
	//   „хранить вещи“, а потом нажимаешь „взять вещи“»).
	//   ПРИЧИНА: якорь брался из ТЕКУЩЕЙ позиции переключателя 4188, а после схлопывания он уже
	//   поднят на `dY` ⇒ при ВОЗВРАТЕ получалось `Anchor == Top`, `dY == 0` и функция выходила
	//   по `return`, не вернув ни сдвинутые узлы, ни высоту окна.
	//   ЛЕЧЕНИЕ: якорь считаем по ИСХОДНОЙ (запомненной) позиции узла, а не по текущей.
	float Anchor = Top + BoxOff.Bottom;       // запасной вариант — низ рамки
	if (UWidget* CatW = BtnCat0.Get())
	{
		if (const FMargin* Base = BaseOffsets.Find(CatW))
		{
			Anchor = Base->Top;               // исходная позиция (до всех схлопываний)
		}
		else if (const UCanvasPanelSlot* CatSlot = AsdaUi::SlotOf(CatW))
		{
			Anchor = CatSlot->GetOffsets().Top;   // первый вызов — узел ещё не двигали
		}
	}
	const float dY = Anchor - Top;
	if (dY <= 0.f) { return; }

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	int32 Moved = 0, Shrunk = 0;
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		UCanvasPanelSlot* CS = AsdaUi::SlotOf(W);
		if (!CS) { continue; }
		// Двигаем только узлы КОРНЕВОГО канваса: у детей координаты относительные, они поедут сами.
		if (W->GetParent() != (WinFrameTL ? WinFrameTL->GetParent() : nullptr)) { continue; }
		const FString N = W->GetName();
		// Окна-сумки и попап свитка живут своей раскладкой — их не трогаем.
		if (N.StartsWith(TEXT("WAREBAG")) || N.StartsWith(TEXT("Warehouse_"))) { continue; }

		// Исходную геометрию запоминаем ОДИН раз, чтобы возврат был точным.
		if (!BaseOffsets.Contains(W)) { BaseOffsets.Add(W, CS->GetOffsets()); }
		const FMargin Base = BaseOffsets[W];

		if (WinBodyParts.Contains(W))
		{
			// Тело окна: укоротить (Bottom здесь — высота, канвас в режиме POINT).
			FMargin O = Base;
			if (bCollapse) { O.Bottom = FMath::Max(0.f, Base.Bottom - dY); }
			AsdaUi::SetOffsetsKeepingDrift(W, O, Base); ++Shrunk;   // ★ 25.10.2026: сдвиг окна игроком не теряется
			continue;
		}
		// Всё, что лежит НА УРОВНЕ переключателя категорий и ниже, поднимаем на `dY`
		//   (включая сам переключатель 4188 и его подписи — они на T = Anchor).
		if (Base.Top >= Anchor - 0.5f)
		{
			FMargin O = Base;
			if (bCollapse) { O.Top = Base.Top - dY; }
			AsdaUi::SetOffsetsKeepingDrift(W, O, Base); ++Moved;   // ★ 25.10.2026: сдвиг окна игроком не теряется
		}
	}
	// Саму рамку прячем/возвращаем.
	if (BrownBoxGrp)
	{
		BrownBoxGrp->SetVisibility(bCollapse ? ESlateVisibility::Collapsed
		                                     : ESlateVisibility::SelfHitTestInvisible);
	}
	bTabAreaCollapsed = bCollapse;
	UE_LOG(LogAsdaWare, Log,
		TEXT("[склад] FIX-ORIG D19: пустая полоса блока вкладок %s (сдвинуто %d узлов, укорочено %d, dY=%.0f)"),
		bCollapse ? TEXT("СХЛОПНУТА") : TEXT("возвращена"), Moved, Shrunk, dY);
}

// Фактическая экранная позиция левого-верхнего угла окна 4000. Окно можно перетащить, поэтому
//   позиции окон-сумок обязаны считаться ОТ НЕЁ, а не от дизайн-координат (39,56).
FVector2D UAsdaWarehouseSubsystem::GetWareWinOrigin() const
{
	if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(WinFrameTL))
	{
		const FMargin O = CS->GetOffsets();
		// ★ 10.10.2026 (волна «наезды текста», `Reports/owner_1010/overlaps.md`): окно 4000 расширено в ассете
		//   симметрично (кнопка «Забрать»/«Хранить» под русскую подпись), а смещения сумок в `.JUI` заданы от левого края
		//   окна ДИЗАЙН-ширины 228. Чтобы сумки по-прежнему стояли справа вплотную, к X добавляется прибавка ширины окна
		//   (правый угол рамки − левый − дизайн-ширина; у нерасширенного окна 0).
		float Extra = 0.f;
		if (const UCanvasPanelSlot* TR = AsdaUi::SlotOf(WinFrameTR))
		{
			Extra = FMath::Max(0.f, (TR->GetOffsets().Left + TR->GetOffsets().Right) - O.Left - kWareWinDesignWidth);
		}
		return FVector2D(O.Left + Extra, O.Top);
	}
	return kWareWinOrigin;   // окно ещё не найдено — дизайн-позиция из `.JUI`
}

// `uiWnd_SetModeVisibility@0xE8EB90` (ui_core.c:79200).
//   Режим 2 («забрать»): скрыть 4188, 4002, 4189; показать блок вкладок 4001 и кнопку 4049.
//   Режим 1 («хранить»): показать 4189, 4188, 4002; скрыть 4001 и 4049.
void UAsdaWarehouseSubsystem::ApplyModeVisibility(int32 EtalonVisMode)
{
	const bool bKeep = (EtalonVisMode == 1);
	auto Show = [](UWidget* W, bool bVis, ESlateVisibility VisKind)
	{
		if (W) { W->SetVisibility(bVis ? VisKind : ESlateVisibility::Collapsed); }
	};
	// ★★★ Кнопкам нужен ЖИВОЙ хит-тест: `SelfHitTestInvisible` снял бы у них приём мыши
	//   (закон `selfhittest-vs-hittest-invisible`).
	Show(BtnKeep.Get(), bKeep,  ESlateVisibility::Visible);
	Show(BtnTake.Get(), !bKeep, ESlateVisibility::Visible);
	Show(CapKeep.Get(), bKeep,  ESlateVisibility::HitTestInvisible);
	Show(CapTake.Get(), !bKeep, ESlateVisibility::HitTestInvisible);
	Show(BtnCat0.Get(), bKeep,  ESlateVisibility::Visible);
	Show(BtnCat1.Get(), bKeep,  ESlateVisibility::Visible);
	Show(TxtCat.Get(),  bKeep,  ESlateVisibility::HitTestInvisible);
	// ★ ИСПРАВЛЕНО 2026-08-27: вторая подпись и РАМКИ вкладок тоже переключаются по режиму.
	//   Без этого в режиме «забрать» подпись «Снаряжение» оставалась висеть поверх блока
	//   вкладок (на скриншоте владельца она наложилась на «Дополнительное хранилище»),
	//   а в режиме «хранить» вкладки рисовались без рамок.
	Show(TxtCat2.Get(), bKeep,  ESlateVisibility::HitTestInvisible);
	for (const TObjectPtr<UWidget>& P : CatPlanes)
	{
		Show(P.Get(), bKeep, ESlateVisibility::SelfHitTestInvisible);
	}
	// Блок вкладок 4001 — ПОДДЕРЕВО целиком.
	ShowTabBlock(!bKeep);
	// ★ FIX-ORIG D19: в режиме «хранить» блок вкладок скрыт — схлопываем и пустую рамку под ним,
	//   иначе под заголовком остаётся чёрный провал (жалоба владельца, скриншот 2026-08-27).
	ApplyTabAreaCollapse(bKeep);
	// Полоса прокрутки 4189 живёт только в режиме «хранить» и только у категории 0.
	//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): и у категории «снаряжение», когда сумка не помещается в 5 рядов (60 ячеек).
	if (!bKeep || GetMaxScrollRow() == 0) { ScrollBar.Hide(); }
}

// ★★★ ПОДДЕРЕВО БЛОКА ВКЛАДОК. `WAREKEEP_TOP` (4001) — контейнер: подпись 4190, обе подложки
//   и обе четвёрки чек-кнопок — его ДЕТИ (иерархия JUI расплющена, §1 отчёта).
void UAsdaWarehouseSubsystem::ShowTabBlock(bool bVisible)
{
	const ESlateVisibility V = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	for (const TObjectPtr<UWidget>& W : TabBlockParts) { if (W) { W->SetVisibility(V); } }
	if (!bVisible) { return; }
	// Чек-кнопкам — ЖИВОЙ хит-тест, иначе они перестанут ловить мышь.
	ApplyBagButtonVisibility();
	// «Вечно скрытые» гасим ПОСЛЕ общего показа (иначе всплывают вместе с поддеревом).
	//   ⚠ Только те, что ЛЕЖАТ В ЭТОМ ПОДДЕРЕВЕ: `Ware_AniGear` и заготовки заголовка живут
	//   в корне окна, и глобальный проход здесь спорил бы с `TickGearHover` и с ключом D14.
	for (const TObjectPtr<UWidget>& W : AlwaysHidden)
	{
		if (W && TabBlockParts.Contains(W)) { W->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

// `uiInventoryBagOpen` ui_core.c:78840-78842: `v6 = вместимость / 30`, показываются кнопки `k = 1 … v6−1`.
void UAsdaWarehouseSubsystem::ApplyBagButtonVisibility()
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// −1 = ёмкость ещё не приходила: эталон в этом случае показал бы 0 кнопок (вместимость 0).
	const int32 Capacity = (Net && Net->SelfStoreSlotsMax > 0) ? Net->SelfStoreSlotsMax : 0;
	const int32 TabsAvail = Capacity / kWareBagCells;   // эталонный `v6`
	for (int32 k = 0; k < kBags; ++k)
	{
		const bool bShown = (k + 1) <= (TabsAvail - 1);
		if (UCheckBox* Cb = Bags[k].Btn.Get())
		{
			AsdaUi::SetShownHit(Cb, bShown);
			Cb->SetIsChecked(bBagOpen[k]);
		}
	}
}

void UAsdaWarehouseSubsystem::CloseWindow()
{
	// `storageUIClearTab(this, 0)` (item_inventory.c:57964-57987): скрыть 4000 и все окна-сумки,
	//   снять чек-кнопки, ВЕРНУТЬ содержимое корзины владельцам, `nMode = −1`. Пакет НЕ шлётся.
	if (Mode == kModeClosed) { return; }
	BasketReturnAll();
	for (int32 k = 0; k < kBags; ++k)
	{
		bBagOpen[k] = false;
		ShowBagWindow(k + 1, false);
		ReleaseSlotPos(k + 1);
		if (UCheckBox* Cb = Bags[k].Btn.Get()) { Cb->SetIsChecked(false); }
	}
	Mode = kModeClosed;
	DragArm.Disarm();
	DragGhost.Cancel();
	DragCtrl = -1;
	TipCtrl = -1;
	HideSelFrames();
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::Warehouse);
	}
	ScrollBar.Hide();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWareWindowId, false);
	}
	UE_LOG(LogAsdaWare, Log, TEXT("[склад] окно 4000 закрыто (пакет не отправлен — как в эталоне)"));
}

// `storageUIResetAll@0xE88BC0` — «уход от NPC» (хвост `UI_resetAllWindowsAndLeave_5045`).
void UAsdaWarehouseSubsystem::ResetAllOnNpcLeave()
{
	CloseWindow();
}

// `uiWindowOpen_EA20@0xE8EA20` (ui_core.c:79225) — вход №3, СВИТОК ВОЗВРАТА СКЛАДА
//   (buff-пакет с `SubType == 112`; коды предметов 382/383/384/406/456/460/514).
void UAsdaWarehouseSubsystem::OpenRecallPanel()
{
	if (!EnsureRecallWidget()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kRecallWindowId, true);
		WM->BringWindowToFront(kRecallWindowId);
	}
	// Эталон принудительно закрывает окно персонажа и окно инвентаря.
	if (UAsdaCharaInfoSubsystem* Ci = GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr)
	{
		Ci->CloseWindow();
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->CloseWindow();
	}
	bRecallActive = true;
	UE_LOG(LogAsdaWare, Log, TEXT("[склад] окно 5500 показано, признак «удалённый склад активен» поднят"));
}

// `uiHidePanelAndChild@0xE8EAB0`: скрыть 5500 И 4000, снять `isOpen`.
void UAsdaWarehouseSubsystem::CloseRecallPanel()
{
	bRecallActive = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kRecallWindowId, false);
	}
	CloseWindow();
	UE_LOG(LogAsdaWare, Log, TEXT("[склад] окно 5500 закрыто, признак снят"));
}

void UAsdaWarehouseSubsystem::NoticeRecallBlocked(int32 LocId) const
{
	// §10: все нотисы 10305…10309 идут в СИСТЕМНЫЙ ЧАТ.
	NoticeChat(LocId);
}

// ═════════════════════════ ВКЛАДКИ ════════════════════════════════════════════════════════════
// `uiWarehouseOpenTab@0xE88F30` / `storageUIClearTab@0xE87E60`.
void UAsdaWarehouseSubsystem::OnBagButtonToggled(int32 Tab, bool bOn)
{
	if (Tab < 1 || Tab > kBags) { return; }
	const int32 k = Tab - 1;
	if (bOn)
	{
		if (bBagOpen[k]) { return; }                 // окно вкладки уже видимо — ничего
		if (!bTabLoaded[Tab]) { RequestTab(Tab); return; }   // иначе запрос 4050 и ждём 4051/4052
		AssignFreeSlotPos(Tab);
		bBagOpen[k] = true;
		ShowBagWindow(Tab, true);
		RefreshBag(Tab);
	}
	else
	{
		if (!bBagOpen[k])
		{
			if (UCheckBox* Cb = Bags[k].Btn.Get()) { Cb->SetIsChecked(false); }
			return;
		}
		bBagOpen[k] = false;
		ShowBagWindow(Tab, false);
		ReleaseSlotPos(Tab);
		// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D13: эталонный `uiWarehouseResetTab` снимает подсветку
		//   `CPlane_SetFillMode` с сетки ВКЛАДКИ 0, какую бы вкладку ни закрывали. Мы снимаем
		//   подсветку только со СВОЕЙ. Откат: `asda.Warehouse.LegacyResetTabGrid 1`.
		RefreshSourceDim();
	}
	if (UCheckBox* Cb = Bags[k].Btn.Get()) { Cb->SetIsChecked(bBagOpen[k]); }
}

void UAsdaWarehouseSubsystem::ShowBagWindow(int32 Tab, bool bVisible)
{
	if (Tab < 1 || Tab > kBags) { return; }
	FAsdaWareBagUi& B = Bags[Tab - 1];
	const ESlateVisibility V = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	for (const TObjectPtr<UWidget>& W : B.Parts) { if (W) { W->SetVisibility(V); } }
	if (!bVisible) { return; }
	// Крестику окна-сумки — ЖИВОЙ хит-тест.
	if (B.Close) { B.Close->SetVisibility(ESlateVisibility::Visible); }
	for (const TObjectPtr<UWidget>& W : B.AlwaysHidden) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
}

// `uiAssignControlToFreeSlot@0xE8D180` (ui_core.c:78878): взять первую свободную из восьми
//   предзаготовленных позиций `aTabSlotPos` и пометить её номером вкладки.
//   ★ D7 оригинала (при `a2` вне 1…8 возвращается неинициализированная `result`) здесь не
//   воспроизводим: диапазон проверен на входе.
int32 UAsdaWarehouseSubsystem::AssignFreeSlotPos(int32 Tab)
{
	if (Tab < 1 || Tab > kBags) { return -1; }
	// Позиция уже занята этой вкладкой — ничего не двигаем.
	for (int32 p = 0; p < kBags; ++p) { if (SlotAssign[p] == Tab) { return p; } }
	for (int32 p = 0; p < kBags; ++p)
	{
		if (SlotAssign[p] != -1) { continue; }
		SlotAssign[p] = Tab;
		// ★ Переставить окно-сумку в свободную позицию `p`. Позиции (`aTabSlotPos`) заданы в `.JUI`
		//   ОТНОСИТЕЛЬНО окна 4000 — сетка 3 колонки × 3 ряда СПРАВА от него, начиная с его же
		//   верхнего края. Экранную позицию считаем от ФАКТИЧЕСКОГО положения окна: его можно
		//   перетащить, и жёсткие дизайн-координаты тогда разъезжаются с ним (жалоба владельца
		//   2026-08-27: окно справа внизу, а сумки — слева вверху).
		if (Bags[Tab - 1].Root)
		{
			const FVector2D Screen = GetWareWinOrigin() + Bags[p].DesignPos;
			AsdaUi::SetSlotRect(Bags[Tab - 1].Root.Get(), Screen, kWareBagLayout[p].Size);
			UE_LOG(LogAsdaWare, Log,
				TEXT("[склад] сумка %d → позиция %d: экран (%.0f,%.0f) = окно (%.0f,%.0f) + смещение (%.0f,%.0f)"),
				Tab, p, Screen.X, Screen.Y,
				GetWareWinOrigin().X, GetWareWinOrigin().Y, Bags[p].DesignPos.X, Bags[p].DesignPos.Y);
		}
		return p;
	}
	return -1;
}

void UAsdaWarehouseSubsystem::ReleaseSlotPos(int32 Tab)
{
	for (int32 p = 0; p < kBags; ++p) { if (SlotAssign[p] == Tab) { SlotAssign[p] = -1; } }
}

// `uiBagSendActionPacket@0xE89B30` (правильное имя — `Send_ClientStoreHouseItemInfo`).
void UAsdaWarehouseSubsystem::RequestTab(int32 Tab)
{
	if (bRequestSent) { return; }                    // гейт эталона `bRequestPending != 1`
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// ★ PageNo = вкладка + 1, диапазон 1…9 (кламп живёт в самой `SendStoreHousePageReq4050`).
	if (Net->SendStoreHousePageReq4050(Tab + 1))
	{
		PendingTab = Tab;
		bRequestSent = true;
		UE_LOG(LogAsdaWare, Log, TEXT("[склад] запрос 4050: вкладка %d (страница %d)"), Tab, Tab + 1);
	}
}

// `warehouseDrawCurrentTab@0xE89D50` (item_inventory.c:58227) — приём пакета 4052 «страница склада
//   выдана целиком»: снять признак ожидания, пометить вкладку загруженной и, если это была вкладка
//   1…8, ПОКАЗАТЬ её окно; вкладка 0 просто перерисовывается в окне 4000.
//   ⚠ Зовётся из `Tick`, а не из колбэка пакета (закон `no-reentrant-net-callbacks`): к этому
//   моменту подсистема инвентаря уже разложила записи в контейнер 5.
void UAsdaWarehouseSubsystem::ApplyTabArrival()
{
	if (!bTabArrived) { return; }
	bTabArrived = false;
	bRequestSent = false;
	// Страница склада приходит и НЕЗАПРОШЕННОЙ — при входе в мир сервер отдаёт страницу 1
	//   (вкладка 0). В этом случае `PendingTab` пуст, и вкладка та же нулевая.
	const int32 Tab = (PendingTab >= 0 && PendingTab < kStoreTabs) ? PendingTab : 0;
	PendingTab = -1;
	bTabLoaded[Tab] = true;
	bDirty = true;
	if (Tab >= 1 && Tab <= kBags && Mode == kModeTake)
	{
		const int32 k = Tab - 1;
		AssignFreeSlotPos(Tab);
		bBagOpen[k] = true;
		ShowBagWindow(Tab, true);
		if (UCheckBox* Cb = Bags[k].Btn.Get()) { Cb->SetIsChecked(true); }
	}
	UE_LOG(LogAsdaWare, Log, TEXT("[склад] 4052: вкладка %d получена целиком"), Tab);
}

// `uiInitSlotGridPanel@0xE8D680` — переключатель 4188.
void UAsdaWarehouseSubsystem::OnCategoryChanged(int32 NewCategory)
{
	if (Mode != kModeKeep) { return; }
	NewCategory = (NewCategory == kCatShell) ? kCatShell : kCatSpend;
	if (Category == NewCategory) { return; }         // эталон: «если индекс изменился»
	Category = NewCategory;
	ScrollRow = 0;
	RefreshCategoryCaptions();
	// Категория 0 → 60 записей, полоса ПОКАЗАНА; категория 1 → 30 записей, полоса СКРЫТА.
	//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): у сумки 60 ячеек полоса нужна и категории 1 (видно 30 из 60) — прячем,
	//   только когда прокручивать нечего.
	if (GetMaxScrollRow() == 0) { ScrollBar.Hide(); }
	SyncMirrors();
	RefreshAll();
}
