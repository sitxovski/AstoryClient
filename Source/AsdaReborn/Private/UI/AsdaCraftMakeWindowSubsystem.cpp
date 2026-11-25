// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна изготовления `ITEM_making_Mark_WIN` (JUI 77500). Разбор эталона, поля `CMakeingItem`, поток, нотисы
//   и ключи — в шапке `AsdaCraftMakeWindowSubsystem.h`; отчёт волны — `Reports/craft/wave4.md`. Строки эталона:
//   ui_core.c:101596-101867, item_inventory.c:69558-69740, item_enhance.c:10217-10300 и 10335-10775, network.c:50080-50230.
#include "UI/AsdaCraftMakeWindowSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaJuiWindowHandle.h"            // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaAnimatedPlane.h"              // `Ani_Soul2` ступеней
#include "Login/AsdaButtonRelay.h"
#include "Items/AsdaItemDbRows.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "AsdaCraftMakeWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCraftMakeWindowSubsystem*.cpp

// ★ 08.11.2026 (пачка 19): полная остановка своего персонажа из клика UI (World/AsdaUiActionMoveHold.cpp, ключ `asda.Move.UiActionStop`).
void AsdaFullStopSelfForUiAction(class UGameInstance* GI, const TCHAR* Why);

DEFINE_LOG_CATEGORY(LogAsdaCraftMake);   // ★ R3-c 12.11.2026: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ═════════════════════════════ КЛЮЧИ ОТКАТА ИСПРАВЛЕНИЙ ═════════════════════════════════════════
// ★ К-3 (+ ревизия реверса Р-4). Сервер при провале на первой ступени шлёт 6603 КОД 1 со ступенью +10 = 0 и нулевым
//   продуктом (кода 2 не шлёт никогда). Эталон на ступени 0 честно показывает крестик и 5578, но итог ведёт веткой
//   `[40] == 1`: `GetItemRecord(0)` = NULL ⇒ статус, лента и плашка получают ПУСТУЮ строку (item_enhance.c:10692-10719), а
//   финальная подсветка идёт на слот `[42] + база − 1` — у 7-ступенчатой записи это −1 (FMark_2 77502 и ани 7-й ступени).
static ASDA_DEV_CVAR(int32, CVarCraftFixZeroTierResult,
	TEXT("asda.Craft.FixZeroTierResult"), 1,
	TEXT("Дефект оригинала К-3: 1 = провал на первой ступени изготовления показывается как провал — крестик, «Не удалось "
	     "создать '…'» (имя предмета проваленной ступени) в статусе, ленте и плашке, без финальной подсветки; 0 = как "
	     "оригинал — ветка «получен» с пустой строкой и подсветкой слота «база − 1» (у 7-ступенчатого рецепта это −1)."),
	ECVF_Default);

namespace {
	const FName  kWindowId(UAsdaCraftSubsystem::kMakeWindowId);
	const TCHAR* kWindowWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ITEM_making_Mark_WIN.WBP_ITEM_making_Mark_WIN_C");
	const TCHAR* kFramePlanes = TEXT("Box_White1Stroke_BlackFill128Ot_1_%d");   // рамка 230×252 (9 планов) — зона окна

	// Цвета эталона (int32 `CControl_SetField923` / `set_o329` / чат).
	const FColor kColWhite (0xFF, 0xFF, 0xFF, 0xFF);   // −1
	const FColor kColBlue  (0x09, 0x8A, 0xFF, 0xFF);   // −16151809 = 0xFF098AFF — 5576/5577/5579/5580
	const FColor kColRed   (0xFF, 0x00, 0x00, 0xFF);   // −65536    = 0xFFFF0000 — 5578, поле при нехватке
	const FColor kColChat  (0xFF, 0xD3, 0x06, 0xFF);   // −11514    = 0xFFFFD306 — лента и плашка итога

	// `g_UpgradeSlotBaseIndexTable@0x15B5F38` (байты бинаря): центровка ступеней по их числу 0..7 (слот = k + база).
	constexpr int32 kSlotBase[8] = { 0, 2, 2, 2, 2, 1, 1, 0 };

	// Показ контрола JUI: интерактив (кнопка/поле) — Visible, декор — SelfHitTestInvisible (общая точка по классу узла).
	void Show(UWidget* W, bool bShown) { AsdaUi::SetShown(W, bShown); }
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════
void UAsdaCraftMakeWindowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaCraftSubsystem::StaticClass());
	IconCodes.Init(0, kTiers);
	Product57.Init(0, 57);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ `uiCloseAllWindows@0xD61960` (ui_core.c:54385-54386): `if (isBoundControlVisible(g_pItemQtyMakeDlg)) itemQtyDlgReset`.
		//   ESC-закрывателя НЕТ (флага `CControl+0x228` в init окна нет).
		WM->RegisterWindowCloser(kWindowId, FSimpleDelegate::CreateWeakLambda(this, [this]() { if (bOpen) { ResetDialog(TEXT("uiCloseAllWindows")); } }));
	}
	if (UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr)
	{
		HEnter = Craft->OnEnterResult.AddUObject(this, &UAsdaCraftMakeWindowSubsystem::OnEnterResult);
		HTimeout = Craft->OnRequestTimedOut.AddUObject(this, &UAsdaCraftMakeWindowSubsystem::OnRequestTimedOut);
	}
	// ★ Волна В8 (ревизия В7 МЕЛК-4): своей подписки на смерть больше нет — строку entity_char.c:16910-16911 исполняет блок
	//   смерти диалога NPC (`UAsdaNpcDialogSubsystem::Tick` → `ResetAfterSelfDeath`), после 76000; до/после 5045 — ключ К-9.
	UE_LOG(LogAsdaCraftMake, Log, TEXT("[ремесло-изготовление] подсистема окна 77500 поднята"));
}

void UAsdaCraftMakeWindowSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr)
	{
		Craft->OnEnterResult.Remove(HEnter);
		Craft->OnRequestTimedOut.Remove(HTimeout);
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->UnregisterWindowCloser(kWindowId); }
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaCraftMakeWindowSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Визуальный слой сессии: окно прячется БЕЗ хвоста `uiExpandPartyPanel`/восстановления окон (снимок чистит оконная
	//   подсистема; переоткрывать окна посреди травела/разрыва нельзя). Продукт идущей анимации не кладём: после разрыва
	//   сумку пришлёт сервер заново.
	//   ★ Волна В8 (финальная ревизия В7, С-1): открытие 77500 блокировало HUD (`uiCloseAllWindows` → хотбар заблокирован,
	//   трей свёрнут и запрещён), а сброс сессии его не возвращал — после разрыва/портала посреди изготовления хотбар и
	//   кнопки меню оставались мёртвыми. У эталона этого пути нет вовсе: при разрыве он закрывает клиент (памятка
	//   `original-quits-client-on-disconnect`), поэтому сброс — защита порта. Возвращаем ТОЛЬКО HUD (`uiExpandPartyPanel`
	//   = `RestoreAfterSceneExit`), окна снимка НЕ переоткрываем (их флаги снимает оконная подсистема). Так же поступают
	//   свои сбросы гачи (`CloseScene`) и диалога NPC (`Close`) — чужие сцены этим не задеты: каждая возвращает HUD сама.
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bWasOpen = bOpen;                                                     // окно было на экране — HUD заблокирован им
	if (bOpen && bControlsReady && WM)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	if (bWasOpen && WM)
	{
		WM->RestoreAfterSceneExit();                                                 // HUD — да, окна снимка — нет
		JournalAdd(FString::Printf(TEXT("сброс сессии (%s): HUD возвращён, окна снимка не переоткрывались"), AsdaSessionResetReasonName(Reason)));
	}
	bOpen = false;
	bAnimating = bCancelAfter = bTierPlayed = bBonusByte = bAwaitingReply = false;
	ResultCode = Reached = BonusFlag = TierCount = CurTier = 0;
	Remaining = 1;
	RecipeId = -1;
	bRateValid = false;
	// ★ Волна В5 (ревизия В4 МЕЛК-2): виджет может пережить разрыв (сессия сброшена, мир тот же) — окно не должно
	//   открыться с выключенными кнопками, спрятанной «Закрыть» и старым статусом ожидания.
	ResetVisuals();
	ClearTierFlags();
	UE_LOG(LogAsdaCraftMake, Verbose, TEXT("[ремесло-изготовление] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
}

void UAsdaCraftMakeWindowSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.CraftMakeWindow");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// `upgradeAnimDlg_tick` зовётся каждый кадр и сам проверяет байт 256.
	if (bAnimating) { TickAnim(); }
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════
bool UAsdaCraftMakeWindowSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kWinZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&) { SetupControls(); RegisterZone(); },
		[]() { UE_LOG(LogAsdaCraftMake, Error, TEXT("[ремесло-изготовление] WBP_ITEM_making_Mark_WIN не загрузился (%s)"), kWindowWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaCraftMakeWindowSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	bControlsReady = false;
	bOpen = false;
	TxtMove = TxtStatus = TxtCreate = TxtClose = TxtQtyLabel = EditQtyCaption = TxtMax = nullptr;
	DragHandle = nullptr;
	BtnCreate = BtnClose = BtnUp = BtnDown = nullptr;
	EditQty = nullptr;
	FMark.Reset();
	FMarkText.Reset();
	Tiers.Reset();
	Bar = FAsdaMiniBarFill();
	IconGrid = nullptr;
	Relays.Reset();
	FoundControls = 0;
}

void UAsdaCraftMakeWindowSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — девять планов `Box_White1Stroke_BlackFill128Ot` (230×252 — вся площадь окна с нижним блоком кнопок).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, kFramePlanes, 9);
}

void UAsdaCraftMakeWindowSubsystem::SetupControls()
{
	// `CMakeingItem_BindControls@0xFE47B0` (ui_core.c:101648-101745).
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	Tiers.SetNum(kTiers);
	FMark.Init(nullptr, 2);
	FMarkText.Init(nullptr, 2);
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	// Номер ступени 1..7 из `ITEM_making_z_mark_<N>` (+ хвост `Suffix`: "", "_b", "x", "a"); −1 — не ступень.
	auto TierOf = [](const FString& Idc, const TCHAR* Suffix) -> int32
	{
		static const FString P(TEXT("ITEM_making_z_mark_"));
		if (!Idc.StartsWith(P) || Idc.Len() != P.Len() + 1 + FCString::Strlen(Suffix)) { return -1; }
		const TCHAR C = Idc[P.Len()];
		if (C < TEXT('1') || C > TEXT('7')) { return -1; }
		if (FCString::Strcmp(*Idc.Mid(P.Len() + 1), Suffix) != 0) { return -1; }
		return (int32)(C - TEXT('1'));
	};
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("ITEM_making_BOX_Ingbtn")) { BtnCreate = Btn; }   // 77533
			if (N == TEXT("ITEM_making_Mark_Xbtn"))  { BtnClose = Btn; }    // 77534
			if (N == TEXT("ITEM_making_BOX_Ar_h"))   { BtnUp = Btn; }       // 77538
			if (N == TEXT("ITEM_making_BOX_Ar_d"))   { BtnDown = Btn; }     // 77539
			continue;
		}
		if (UEditableText* Ed = Cast<UEditableText>(W))
		{
			if (N == TEXT("ITEM_making_BOX_edit_ea")) { EditQty = Ed; }     // 77537
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("ITEM_making_movetext"))       { TxtMove = Tb; continue; }       // 77535
			if (N == TEXT("ITEM_making_M_text"))         { TxtStatus = Tb; continue; }     // 77532
			if (N == TEXT("ITEM_making_BOX_Ingbtn_Text")) { TxtCreate = Tb; continue; }
			if (N == TEXT("ITEM_making_Mark_Xbtn_Text")) { TxtClose = Tb; continue; }
			if (N == TEXT("ITEM_making_BOX_text_ea"))    { TxtQtyLabel = Tb; continue; }   // 77536
			if (N == TEXT("ITEM_making_BOX_edit_ea_Text")) { EditQtyCaption = Tb; continue; }
			if (N == TEXT("ITEM_making_BOX_text_max"))   { TxtMax = Tb; continue; }        // 77548
			if (N == TEXT("ITEM_making_FMark_1"))        { FMarkText[0] = Tb; continue; }   // 77501
			if (N == TEXT("ITEM_making_FMark_2"))        { FMarkText[1] = Tb; continue; }   // 77502
			const int32 T = TierOf(N, TEXT(""));
			if (T >= 0) { Tiers[T].IconText = Tb; }                                        // подпись 7751N
			continue;
		}
		if (N.EndsWith(TEXT("_DRAG")))
		{
			if (N.StartsWith(TEXT("ITEM_making_movetext"))) { DragHandle = W; }
			continue;
		}
		if (N.EndsWith(TEXT("_grp")))
		{
			const FString Base = N.LeftChop(4);
			const int32 T = TierOf(Base, TEXT(""));
			if (T >= 0) { Tiers[T].Group = W; continue; }                                  // 7751N
			const int32 Tb = TierOf(Base, TEXT("_b"));
			if (Tb >= 0) { Tiers[Tb].Back = W; continue; }                                  // 7755N
			continue;
		}
		// Планы `<idc>_<juiIdx>_<plane>`.
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { continue; }
		if (Idc == TEXT("ITEM_making_Mark_Mbar"))   // 77531 `Soul_Violet_Bar`: ES0 подложка / ES1 заливка
		{
			if (Plane == 0) { Bar.Back = Cast<UImage>(W); }
			if (Plane == 1) { Bar.Fill = Cast<UImage>(W); }
			continue;
		}
		if (Idc == TEXT("ITEM_making_FMark_1") && Plane == 0) { FMark[0] = W; continue; }
		if (Idc == TEXT("ITEM_making_FMark_2") && Plane == 0) { FMark[1] = W; continue; }
		int32 T = TierOf(Idc, TEXT(""));
		if (T >= 0 && Plane == 0) { Tiers[T].Icon = Cast<UImage>(W); Tiers[T].IconName = FName(*N); continue; }
		T = TierOf(Idc, TEXT("x"));
		if (T >= 0) { Tiers[T].Fail = W; continue; }                                           // 7754N
		T = TierOf(Idc, TEXT("a"));
		if (T >= 0) { Tiers[T].Ani = Cast<UAsdaAnimatedPlane>(W); continue; }                 // 7752N
	}

	// ── Полоса 77531: полная ширина = подложка ES0 (206 из `.JUI`); у ES1 в ассете образец 110. ──
	{
		FVector2D P, S;
		if (Bar.Fill && UAsdaMiniStateSubsystem::GetSlotRect(Bar.Fill, P, S)) { Bar.BaseLeft = P.X; Bar.FullW = S.X; }
		if (Bar.Back && UAsdaMiniStateSubsystem::GetSlotRect(Bar.Back, P, S)) { Bar.FullW = S.X; }
	}

	// ── Иконки ступеней — общая сетка иконок; атлас пола персонажа (`g_ItemImgOptionVariant` в `uiUpgradeSlotsRedraw`). ──
	TArray<FName> Names;
	for (const FAsdaCraftTierUi& U : Tiers) { Names.Add(U.IconName); }
	IconCodes.Init(0, kTiers);
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	IconGrid = NewObject<UAsdaIconCellGrid>(this);
	IconGrid->SetCellSizePx(32.f);
	IconGrid->SetClickOverlayEnabled(false);
	IconGrid->bClearEmptyCells = true;   // пустая текстура, как `CControl_DrawTextureRect(…, пусто, 0, 0, 32, 32)`
	IconGrid->SetupNamed(Widget, Names, /*bMale*/ !Net || Net->ResolveSelfGender() != 2, [this](int32 C) -> int32
	{
		return IconCodes.IsValidIndex(C) ? FMath::Max(0, IconCodes[C]) : 0;
	}, [](int32) {});

	// ── Кнопки (msg 66561 `itemQtyDlg_OnCommand`; своих звуков у окна нет) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnCreate, 77533, [this]() { OnCreateClicked(); });
	Bind(BtnClose,  77534, [this]() { OnCloseClicked(); });
	Bind(BtnUp,     77538, [this]() { OnQtyUp(); });
	Bind(BtnDown,   77539, [this]() { OnQtyDown(); });
	if (EditQty)
	{
		// Поле 77537: notify 67585…67600 (набор текста) → `atoi` → [44] → `craftWndUpdateAffordability`.
		EditQty->OnTextChanged.AddUniqueDynamic(this, &UAsdaCraftMakeWindowSubsystem::OnQtyTextChanged);
	}

	// ── Подписи и начальные значения — `CMakeingItem_BindControls` ──
	AsdaUi::SetTextIf(TxtStatus, FString());                                        // 77532 = "" (образец ассета «Благодаря…» стирается)
	AsdaUi::SetTextIf(TxtCreate, Loc(AsdaCraftLoc::kMakeBtnCreate));                // 77533 = 5533
	AsdaUi::SetTextIf(TxtClose, Loc(AsdaCraftLoc::kMakeBtnClose));                  // 77534 = 5035
	AsdaUi::SetTextIf(TxtQtyLabel, Loc(AsdaCraftLoc::kMakeQtyLabel));               // 77536 = 5520
	AsdaUi::SetTextIf(EditQtyCaption, FString());                                   // подпись поля (пустая)
	SetQtyText(1);                                                                  // 77537 = "1", лимит 2 символа
	BarFrac = 0.f;
	if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, 0.f); }             // `SetRange(0, 2000)`, `SetValue(0)`

	// ── Итог разбора: 41 контрол с IDNum группы 775 ──
	int32 Found = Widget ? 1 : 0;   // 77500 — корень
	auto Has = [&Found](const UObject* O) { if (O) { ++Found; } };
	Has(TxtMove); Has(BtnClose);                                                    // 77535 77534
	Has(FMark[0]); Has(FMark[1]);                                                   // 77501 77502
	if (Bar.Fill && Bar.Back) { ++Found; }                                          // 77531
	for (const FAsdaCraftTierUi& U : Tiers)                                         // 7 × (7751N 7755N 7754N 7752N)
	{
		if (U.Group && U.Icon) { ++Found; }
		Has(U.Back); Has(U.Fail); Has(U.Ani);
	}
	Has(TxtStatus); Has(BtnCreate); Has(EditQty); Has(BtnUp); Has(BtnDown); Has(TxtQtyLabel); Has(TxtMax);   // 77532 77533 77537 77538 77539 77536 77548
	FoundControls = Found;
	bool bTiersOk = true;
	for (const FAsdaCraftTierUi& U : Tiers) { bTiersOk &= (U.Group && U.Icon && U.Back && U.Fail && U.Ani); }
	bControlsReady = bTiersOk && BtnCreate && BtnClose && EditQty && BtnUp && BtnDown && TxtStatus && Bar.IsValid();
	UE_LOG(LogAsdaCraftMake, Log, TEXT("%s"), *DumpControls());
}

// ═════════════════════════════ ОТКРЫТИЕ / СБРОС ═══════════════════════════════════════════════
bool UAsdaCraftMakeWindowSubsystem::OpenForRecipe(int32 InRecipeId, const TCHAR* Source)
{
	// `uiItemUpgradeWindowOpen@0xFE4D70` (ui_core.c:101749-101771).
	UGameInstance* GI = GetGameInstance();
	UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	const FAsdaMakingRecipeRow* R = Craft ? Craft->FindRecipe(InRecipeId) : nullptr;
	if (bOpen || !R) { return false; }                                               // `IsVisible([1]) || !a3` → 0
	// `[63] = shopFindRecordByItemId(g_pItemMainWnd, *(i16*)(a3 + 124))` — ключ MakeIndex (+124, в DT — `IdCopy`).
	const FAsdaMakingRateRow* Rate = Craft->FindRate(R->IdCopy);
	if (!Rate)
	{
		UE_LOG(LogAsdaCraftMake, Log, TEXT("[ремесло-изготовление] рецепт %d: записи rate %d нет — окно не открывается (как эталон)"), InRecipeId, R->IdCopy);
		JournalAdd(FString::Printf(TEXT("не открыто: нет записи rate %d (рецепт %d)"), R->IdCopy, InRecipeId));
		return false;
	}
	if (bDryOpen)
	{
		// Самотест окна 77000: переход подтверждён, окно не показывается (его проверяет `asda.Craft.SelfTestMake`).
		JournalAdd(FString::Printf(TEXT("открытие без показа (самотест 77000) — рецепт %d (%s)"), InRecipeId, Source));
		return true;
	}
	if (!EnsureWidget()) { return false; }
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	if (!WM) { return false; }
	bRateValid = true;
	for (int32 K = 0; K < kTiers; ++K)
	{
		RateOutItems[K] = DebugRateOut.IsValidIndex(K) ? DebugRateOut[K] : UAsdaCraftSubsystem::RateOutItem(*Rate, K);
	}
	RateFlag2 = Rate->Flag2;
	// `uiCloseAllWindows(g_pUIManager)` + `uiWindowHideAndCleanup(g_pItemMainWnd)`. Сброс 76000 сбрасывает и окно состава
	//   77000 — его снимок открытых окон должен ДОЖИТЬ до сброса этого окна (К-7 его иначе снял бы): защёлка на время вызова.
	UAsdaCraftRecipeWindowSubsystem* Rw = GI->GetSubsystem<UAsdaCraftRecipeWindowSubsystem>();
	if (Rw) { Rw->SetHoldSnapshotForMake(true); }
	WM->CloseAllPlayerWindows();
	if (UAsdaCraftWindowSubsystem* W76 = GI->GetSubsystem<UAsdaCraftWindowSubsystem>()) { W76->HideAndCleanup(); }
	if (Rw) { Rw->SetHoldSnapshotForMake(false); }
	bTierPlayed = false;                                                             // `filler[259] = 0`
	TierCount = CountActiveSlots();                                                  // [45]
	RecipeId = InRecipeId;                                                           // [62]
	Remaining = 1;                                                                   // [44]
	SetQtyText(1);                                                                   // 77537 = "1"
	BarFrac = 0.f;
	if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, 0.f); }             // 77531 = 0
	HideGroup7();
	SetCtrlVisible(kDwMake + 5, false);                                              // `filler[36]` = [9] = 77516 (иконка бонуса)
	RedrawSlots();
	Show(BtnClose, true);                                                            // `filler[156]` = 77534
	Show(TxtClose, true);
	bOpen = true;
	WM->SetWindowVisible(kWindowId, true);                                           // `SetVisible([1], 1)`
	WM->BringWindowToFront(kWindowId);                                               // `CClass_MapInsert`
	JournalAdd(FString::Printf(TEXT("открыто — рецепт %d (%s), ступеней %d, rate %d"), RecipeId, Source, TierCount, R->IdCopy));
	return true;
}

void UAsdaCraftMakeWindowSubsystem::ResetAfterSelfDeath()
{
	// `if (isBoundControlVisible(g_pItemQtyMakeDlg)) itemQtyDlgReset(g_pItemQtyMakeDlg)` (entity_char.c:16910-16911).
	if (bOpen) { ResetDialog(TEXT("смерть своего персонажа (CCharacter_SetAction, entity_char.c:16910)")); }
}

void UAsdaCraftMakeWindowSubsystem::ResetDialog(const TCHAR* Why)
{
	// `itemQtyDlgReset@0xFE4F50` (item_inventory.c:69558-69591).
	UGameInstance* GI = GetGameInstance();
	const bool bWasOpen = bOpen;
	// Идёт анимация кодом 1 — продукт в сумку сразу (`OnRecv_ItemUpdate(filler+188)`).
	if (bAnimating && Product57.Num() >= 57 && AsdaBytes::RdI32(Product57.GetData()) > 0 && ResultCode == 1)
	{
		if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			Inv->ApplyExternalRecord(Product57);
			JournalAdd(FString::Printf(TEXT("сброс посреди анимации: продукт %d положен"), AsdaBytes::RdI32(Product57.GetData())));
		}
	}
	bAnimating = false;
	ResultCode = Reached = BonusFlag = TierCount = CurTier = 0;
	Remaining = 1;
	RecipeId = -1;
	bRateValid = false;
	bCancelAfter = false;
	bTierPlayed = false;
	bBonusByte = false;
	AnimStartS = 0.0;
	bAwaitingReply = false;
	ResetVisuals();                                                                  // визуальная часть (общая с `ResetSessionState`)
	bOpen = false;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM && Widget) { WM->SetWindowVisible(kWindowId, false); }                  // `SetVisible([1], 0)`
	if (bWasOpen) { JournalAdd(FString::Printf(TEXT("сброс (itemQtyDlgReset) — %s"), Why)); }
	if (!WM) { return; }
	// Хвост `uiExpandPartyPanel(g_pUIManager)` + `uiCloseAllToggleWindows(g_pNpcWnd)`.
	//   ★ К-1: окно уже было закрыто (ответ-отказ пришёл после сброса) — эталон всё равно разблокирует HUD и переоткрывает
	//   окна снимка посреди чего угодно; исправленный порт хвост не повторяет (ключ `asda.Craft.FixLateResult`).
	if (!bWasOpen && UAsdaCraftSubsystem::IsLateResultFixEnabled())
	{
		UE_LOG(LogAsdaCraftMake, Verbose, TEXT("[ремесло-изготовление] сброс уже закрытого окна (%s) — хвост HUD/снимка не повторяется (К-1)"), Why);
		return;
	}
	// ★ Защита порта (ключ `asda.UI.WindowSnapshotGuards`): внутри чужого «закрыть всё» окна не переоткрываются и HUD не
	//   возвращается (его закроет/заблокирует сам цикл), снимаются только флаги окна состава.
	if (WM->IsClosingAllPlayerWindows() && UAsdaWindowSubsystem::AreSnapshotGuardsEnabled())
	{
		WM->DropCapturedWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::Craft, TEXT("сброс 77500 внутри CloseAllPlayerWindows"));
		return;
	}
	WM->RestoreAfterSceneExit();                                                     // `uiExpandPartyPanel`
	const int32 N = WM->ReopenCapturedWindows(TEXT("сброс окна изготовления 77500 (itemQtyDlgReset)"));   // `uiCloseAllToggleWindows`
	if (bWasOpen) { JournalAdd(FString::Printf(TEXT("HUD возвращён, окон снимка переоткрыто %d"), N)); }
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════
void UAsdaCraftMakeWindowSubsystem::OnCreateClicked()
{
	// 77533 (item_inventory.c:69653-69679).
	if (!bOpen) { return; }
	// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 19, баг 6): крафт начинается — персонаж встаёт полностью (4008 раньше 6602), дальше ход держит
	//   общая точка остановки до конца анимации (разбор — World/AsdaUiActionMoveHold.cpp). Эталон кнопкой ход не трогает.
	AsdaFullStopSelfForUiAction(GetGameInstance(), TEXT("«Создать» 77533"));
	HideGroup7();
	SetStatus(FString(), kColWhite);                                                 // `CControl_SetText([33], "")`, цвет −1
	if (Remaining <= 0)
	{
		Remaining = 1;
		SendOne();
	}
	else
	{
		const int32 Max = CalcMaxMakeable();
		if (Max < Remaining)
		{
			Remaining = Max;                                                         // урезать до максимума по материалам
			SetQtyText(Remaining);
			UpdateAffordability();
		}
		SendOne();
	}
	CurTier = 0;                                                                     // `savedThis[46] = 0`
	JournalAdd(FString::Printf(TEXT("77533 «Создать»: осталось %d"), Remaining));
}

void UAsdaCraftMakeWindowSubsystem::OnCloseClicked()
{
	// 77534 (item_inventory.c:69695-69705): анимация идёт — отмена после текущей и кнопка прячется; иначе сброс.
	if (bAnimating)
	{
		bCancelAfter = true;
		Show(BtnClose, false);
		Show(TxtClose, false);
		JournalAdd(TEXT("77534: отмена после текущей штуки"));
		return;
	}
	ResetDialog(TEXT("кнопка 77534"));
}

void UAsdaCraftMakeWindowSubsystem::OnQtyUp()
{
	// 77538: `if (++[44] > 99) [44] = 99`, текст поля, цвет.
	if (++Remaining > kQtyMax) { Remaining = kQtyMax; }
	SetQtyText(Remaining);
	UpdateAffordability();
}

void UAsdaCraftMakeWindowSubsystem::OnQtyDown()
{
	// 77539: `if (--[44] < 1) [44] = 1`.
	if (--Remaining < 1) { Remaining = 1; }
	SetQtyText(Remaining);
	UpdateAffordability();
}

void UAsdaCraftMakeWindowSubsystem::OnQtyTextChanged(const FText& Text)
{
	// notify 67585…67600 поля 77537: `GetWindowText` → `atoi` → [44] → `craftWndUpdateAffordability` (он и пишет в поле "%d").
	if (bSettingQtyText) { return; }
	FString S = Text.ToString();
	if (S.Len() > kQtyEditLen) { S = S.Left(kQtyEditLen); }                         // `EM_LIMITTEXT 2` — третий символ не входит
	Remaining = FCString::Atoi(*S);                                                  // C `atoi`: ведущие цифры, иначе 0
	UpdateAffordability();
}

// ═════════════════════════════ ОТПРАВКА И ОТВЕТ ═══════════════════════════════════════════════
bool UAsdaCraftMakeWindowSubsystem::SendOne()
{
	// `netSendItemCombineReq@0xFE5E30` (network.c:50080-50130).
	UGameInstance* GI = GetGameInstance();
	UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	const FAsdaMakingRecipeRow* R = Craft ? Craft->FindRecipe(RecipeId) : nullptr;
	if (!R) { return false; }
	SetFourEnabled(false);                                                           // `uiUpgradeSlotsDisable`
	TArray<int32> Slots; Slots.Init(-1, 6);                                          // `upgradeSlotsInit`: слоты −1, контейнеры 0
	TArray<uint8> Conts; Conts.Init(0, 6);
	for (int32 I = 0; I < 6; ++I)
	{
		const int32 Code = UAsdaCraftSubsystem::RecipeMatCode(*R, I);
		if (Code <= 0) { continue; }
		const int32 Need = UAsdaCraftSubsystem::RecipeMatQty(*R, I);
		int32 Slot = -1; uint8 Cont = 0;
		// Эталон: `CInventory_CountItemById` (сумма) >= нужно ×1, затем слот ПОСЛЕДНЕГО стека. ★ К-6 (`FixStackPick 1`):
		//   первый стек, где хватает ОДНОГО стека (сервер списывает только из него); нет такого — отказ 5566 до отправки.
		const bool bOk = Craft->PickMaterialStack(Code, Need, Slot, Cont) && Craft->UsableMaterialCount(Code) >= Need;
		if (!bOk)
		{
			Craft->NoticeMsgBox(AsdaCraftLoc::kLackMaterials);                       // ONEBUT 5566
			JournalAdd(FString::Printf(TEXT("6602 не отправлен: материала %d меньше %d (5566)"), Code, Need));
			ResetDialog(TEXT("нет материалов ×1 (netSendItemCombineReq)"));
			return false;
		}
		Slots[I] = Slot;
		Conts[I] = Cont;
	}
	if (!Craft->SendCraftEnter(RecipeId, Slots, Conts))
	{
		// Порт: пакет не ушёл (замок «операция в полёте» — прошлый запрос ремесла без ответа, или нет сети). Эталон шлёт
		//   всегда; оставлять кнопки выключенными без запроса нельзя (закон `no-state-latch-before-shown`).
		SetFourEnabled(true);
		JournalAdd(TEXT("6602 не ушёл (замок/сеть) — кнопки возвращены"));
		return false;
	}
	bAwaitingReply = true;
	JournalAdd(FString::Printf(TEXT("6602 отправлен: рецепт %d"), RecipeId));
	return true;
}

void UAsdaCraftMakeWindowSubsystem::OnEnterResult(const FAsdaCraftEnterResult& R)
{
	// `upgradeDlg_onResultPacket@0xFE65E0`, ветка кодов 1/2 (item_enhance.c:10378-10463). Модель уже применила материалы,
	//   вес и уровень/опыт; окно забирает продукт и запускает анимацию, только если ЖДЁТ ответа на свой 6602 (★ К-1: ответ
	//   в сброшенное/закрытое окно продукт кладёт модель сразу; при `FixLateResult 0` — забирается при любом видимом окне).
	const bool bClaim = bOpen && (bAwaitingReply || !UAsdaCraftSubsystem::IsLateResultFixEnabled());
	if (!bClaim)
	{
		JournalAdd(FString::Printf(TEXT("6603 код %d не забран окном (окно %s, ожидание %d)"), R.Code, bOpen ? TEXT("открыто") : TEXT("закрыто"), bAwaitingReply ? 1 : 0));
		return;
	}
	UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	if (Craft) { Craft->ClaimEnterResult(); }
	bAwaitingReply = false;
	BarFrac = 0.f;
	if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, 0.f); }             // `CProgressBar_SetValue([32], 0)`
	ResultCode = R.Code;                                                             // [40] = p[0]
	BonusFlag = R.BonusFlag;                                                         // [43] = p[11]
	Product57 = R.Product57;                                                         // [47..61] = p+12 (57 б)
	if (Product57.Num() < 57) { Product57.SetNumZeroed(57); }
	Reached = R.ReachedTier;                                                         // [42] = p[10]
	bBonusByte = (BonusFlag == 1);                                                   // байт 258
	AnimStartS = FPlatformTime::Seconds();                                           // [65] = timeGetTime()
	bAnimating = true;                                                               // байт 256
	bTierPlayed = false;                                                             // байт 259
	SetStatus(Loc(AsdaCraftLoc::kMakeInProgress), kColWhite);                        // 5575 белым
	JournalAdd(FString::Printf(TEXT("6603 код %d: ступень %d из %d, +1 шт. %d, продукт %d"), ResultCode, Reached, TierCount, BonusFlag,
		AsdaBytes::RdI32(Product57.GetData())));
}

void UAsdaCraftMakeWindowSubsystem::OnRequestTimedOut(int32 Opcode)
{
	if (Opcode != 6602) { return; }
	// Порт (см. шапку): замок снят таймаутом — ответа на этот 6602 не будет; кнопки назад, окно остаётся.
	ReturnButtons(TEXT("таймаут ответа 6603"));
}

// ═════════════════════════════ АНИМАЦИЯ СТУПЕНЕЙ ══════════════════════════════════════════════
void UAsdaCraftMakeWindowSubsystem::TickAnim()
{
	// `upgradeAnimDlg_tick@0xFE6E00` (item_enhance.c:10497-10775), построчно.
	const double ElapsedMs = (FPlatformTime::Seconds() - AnimStartS) * 1000.0;
	int32 SlotBase = CurTier + kSlotBase[FMath::Clamp(TierCount, 0, 7)];            // `&self[46][table[self[45]]]`
	if (ElapsedMs <= kTierMs)
	{
		SetCtrlVisible(SlotBase + kDwBack, false);                                   // подложка текущей ступени снята
		BarFrac = (float)(ElapsedMs / kTierMs);
		if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, BarFrac); }
		return;
	}
	BarFrac = 1.f;
	if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, 1.f); }             // `SetValue(2000)`
	if (!bTierPlayed)
	{
		const int32 OutCode = RateOut(CurTier);                                      // `self[63][4*self[46] + 8]`
		if (Reached == TierCount)
		{
			// Пройдены ВСЕ ступени: «пройдено» на слоте; бонус «+1 шт.» — у рецептов с `rate[2] == 0`.
			SetCtrlVisible(SlotBase + kDwAni, true);
			ShowAni(SlotBase + kDwAni);
			SetCtrlVisible(SlotBase + kDwBack, false);
			FString Msg;
			if (BonusFlag != 1 || !RateBonusFlagZero())
			{
				Msg = FormatLocName(AsdaCraftLoc::kMakeTierDone, OutCode);           // 5576
			}
			else
			{
				// Иконка бонуса — в 77516 (`self[9]`), её анимация (`self[16]`), без подложки (`self[23]`); 5579.
				const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
				if (IT && IT->GetItem(OutCode))
				{
					IconCodes[5] = OutCode;
					if (IconGrid) { IconGrid->Refresh(); }
					SetCtrlVisible(kDwMake + 5, true);
					SetCtrlVisible(kDwAni + 5, true);
					ShowAni(kDwAni + 5);
					SetCtrlVisible(kDwBack + 5, false);
					Msg = FormatLocName(AsdaCraftLoc::kMakeBonusTier, OutCode);      // 5579
				}
			}
			SetStatus(Msg, kColBlue);
			JournalAdd(FString::Printf(TEXT("ступень %d: пройдена%s — «%s»"), CurTier, (BonusFlag == 1 && RateBonusFlagZero()) ? TEXT(" (+1 шт.)") : TEXT(""), *Msg));
		}
		else if (Reached > CurTier)
		{
			SetCtrlVisible(SlotBase + kDwAni, true);
			ShowAni(SlotBase + kDwAni);
			SetCtrlVisible(SlotBase + kDwBack, false);
			const FString Msg = FormatLocName(AsdaCraftLoc::kMakeTierDone, OutCode); // 5576
			SetStatus(Msg, kColBlue);
			JournalAdd(FString::Printf(TEXT("ступень %d: пройдена — «%s»"), CurTier, *Msg));
		}
		else
		{
			SetCtrlVisible(SlotBase + kDwAni, false);
			CloseHideAni(SlotBase + kDwAni);
			SetCtrlVisible(SlotBase + kDwBack, false);
			SetCtrlVisible(SlotBase + kDwFail, true);                                // крестик NEW_st_X
			const FString Msg = FormatLocName(AsdaCraftLoc::kMakeTierFail, OutCode); // 5578
			SetStatus(Msg, kColRed);
			JournalAdd(FString::Printf(TEXT("ступень %d: провал — «%s»"), CurTier, *Msg));
		}
		bTierPlayed = true;                                                          // байт 259
	}
	if (ElapsedMs > kTierEndMs)
	{
		for (int32 S = 0; S < kTiers; ++S) { SetCtrlVisible(S + kDwBack, true); }    // все 7 подложек (дети видимых иконок)
		if (bTierPlayed)
		{
			int32 RemainingSlots = Reached - CurTier;
			if (Reached == TierCount) { RemainingSlots = Reached - 1 - CurTier; }
			if (RemainingSlots > 0)
			{
				bTierPlayed = false;
				BarFrac = 0.f;
				if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, 0.f); }
				AnimStartS = FPlatformTime::Seconds();
				++CurTier;
				SetTierFlag(SlotBase + kDwMake, false);
				SlotBase = CurTier + kSlotBase[FMath::Clamp(TierCount, 0, 7)];
				SetTierFlag(SlotBase + kDwMake, true);
				SetCtrlVisible(SlotBase + kDwBack, false);
				return;
			}
		}
		FinishPiece(SlotBase);
	}
}

void UAsdaCraftMakeWindowSubsystem::FinishPiece(int32 SlotBase)
{
	// Хвост `upgradeAnimDlg_tick` (item_enhance.c:10686-10772).
	UGameInstance* GI = GetGameInstance();
	bAnimating = false;                                                              // байт 256 = 0
	SetTierFlag(SlotBase + kDwMake, false);
	const int32 FinalSlot = Reached + kSlotBase[FMath::Clamp(TierCount, 0, 7)] - 1;  // `&self[42][table[self[45]] − 1]`
	const int32 ProductCode = Product57.Num() >= 4 ? AsdaBytes::RdI32(Product57.GetData()) : 0;
	// ★ К-3: код 1 при ступени 0 — это провал (сервер не шлёт кода 2).
	const bool bZeroFix = ResultCode == 1 && Reached == 0 && CVarCraftFixZeroTierResult.GetValueOnGameThread() != 0;
	if (!bZeroFix)
	{
		// Финальная подсветка «достигнутой» ступени. При `[42] == 0` это слот «база − 1»: у 5-ступенчатого — пустой слот 0,
		//   у 7-ступенчатого — индекс −1 (флаг +0x24F уходит на FMark_2 77502, прячется ани 7-й ступени) — порт повторяет
		//   ровно это через `ControlByDword` и не падает.
		SetTierFlag(FinalSlot + kDwMake, true);
		SetCtrlVisible(FinalSlot + kDwBack, false);
	}
	FString Msg;
	if (ResultCode == 1 && !bZeroFix)
	{
		// `CItemManager_NotifyItemInfo(self+47)` + `OnRecv_ItemUpdate(self+188)`, 5577/5580 синим + лента/плашка −11514.
		//   ★ Волна В5 (ревизия В4 МЕЛК-4): эталон зовёт оба БЕЗУСЛОВНО (item_enhance.c:10693-10695). При ключе К-3 = 0
		//   (`bZeroFix` ложен) порт тоже зовёт их и на пустой записи (код 0, слот −1): анонс её не находит, сумка
		//   пропускает слот вне модели — как оригинал, без падения. При ключе 1 сюда пустая запись не доходит.
		if (Product57.Num() >= 57 && (ProductCode > 0 || CVarCraftFixZeroTierResult.GetValueOnGameThread() == 0))
		{
			if (UAsdaHudSubsystem* Hud = GI ? GI->GetSubsystem<UAsdaHudSubsystem>() : nullptr)
			{
				Hud->NotifyItemInfo(ProductCode, /*bForce*/false, (int32)Product57[4], (int32)AsdaBytes::RdI16(Product57.GetData() + 5));
			}
			if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
			{
				Inv->ApplyExternalRecord(Product57);
			}
		}
		if (ProductCode <= 0)
		{
			JournalAdd(TEXT("итог: запись продукта пустая (ступень 0) — анонс и сумка её пропускают, как оригинал"));
		}
		Msg = FormatLocName((BonusFlag != 1 || !RateBonusFlagZero()) ? AsdaCraftLoc::kMakeGot : AsdaCraftLoc::kMakeGotBonus, ProductCode);
		SetStatus(Msg, kColBlue);
		ChatAndPlank(Msg);
		JournalAdd(FString::Printf(TEXT("итог: получен — «%s»%s"), *Msg, Msg.IsEmpty() ? TEXT(" (пустая строка — как оригинал, К-3 выкл.)") : TEXT("")));
	}
	else if (ResultCode == 2 || bZeroFix)
	{
		// Ветка «провал»: крестик на слоте, 5578 красным + лента/плашка. ★ К-3: слот и имя — проваленной ступени (у эталона
		//   ветка берёт слот «база − 1» и имя продукта, который при провале пуст).
		const int32 FailSlot = bZeroFix ? SlotBase : FinalSlot;
		SetCtrlVisible(FailSlot + kDwAni, false);
		CloseHideAni(FailSlot + kDwAni);
		SetCtrlVisible(FailSlot + kDwBack, false);
		SetCtrlVisible(FailSlot + kDwFail, true);
		Msg = FormatLocName(AsdaCraftLoc::kMakeTierFail, bZeroFix ? RateOut(CurTier) : ProductCode);
		SetStatus(Msg, kColRed);
		ChatAndPlank(Msg);
		JournalAdd(FString::Printf(TEXT("итог: провал — «%s»%s"), *Msg, bZeroFix ? TEXT(" (исправление К-3)") : TEXT("")));
	}
	if (Product57.Num() >= 4) { AsdaBytes::WrI32(Product57.GetData(), -1); }        // `self[47] = −1`
	ResultCode = 0;                                                                  // `self[40] = 0`
	if (UAsdaCraftWindowSubsystem* W76 = GI ? GI->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr) { W76->RefreshSummary(); }
	--Remaining;                                                                     // `--self[44]`
	CurTier = 0;                                                                     // `self[46] = 0`
	SetQtyText(Remaining);
	if (bCancelAfter)
	{
		ResetDialog(TEXT("отмена после текущей штуки (77534)"));
		return;
	}
	if (Remaining <= 0)
	{
		SetFourEnabled(true);                                                        // `sub_9237C6`
		SetQtyText(1);                                                               // поле «1» ([44] остаётся 0)
		JournalAdd(TEXT("серия окончена — кнопки включены"));
		return;
	}
	// Серия «N штук = N пакетов»: следующий 6602 — только ПОСЛЕ ответа на предыдущий и после анимации (P35: дубль под
	//   серверным замком остаётся без ответа).
	SendOne();
	HideGroup7();
	if (BonusFlag == 1 && RateBonusFlagZero()) { SetCtrlVisible(kDwMake + 5, false); }   // иконка бонуса 77516
}

// ═════════════════════════════ ПОМОЩНИКИ ЭТАЛОНА ══════════════════════════════════════════════
int32 UAsdaCraftMakeWindowSubsystem::CountActiveSlots() const
{
	// `craftCountActiveSlots@0xFE4ED0`: сколько `rate.OutItem[k] > 0`.
	if (!bRateValid) { return 0; }
	int32 N = 0;
	for (int32 K = 0; K < kTiers; ++K) { if (RateOutItems[K] > 0) { ++N; } }
	return N;
}

int32 UAsdaCraftMakeWindowSubsystem::CalcMaxMakeable() const
{
	// `craftCalcMaxMakeableCount@0xFE5C40`: min по материалам с количеством > 0 из (есть / нужно), от 100; >= 100 ⇒ 99.
	//   ★ К-6 (`FixStackPick 1`): «есть / нужно» — по стекам (`CraftableFromStacks`).
	const UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	const FAsdaMakingRecipeRow* R = Craft ? Craft->FindRecipe(RecipeId) : nullptr;
	int32 V = 100;
	if (R)
	{
		for (int32 I = 0; I < 6; ++I)
		{
			const int32 Need = UAsdaCraftSubsystem::RecipeMatQty(*R, I);
			if (Need > 0) { V = FMath::Min(V, Craft->CraftableFromStacks(UAsdaCraftSubsystem::RecipeMatCode(*R, I), Need)); }
		}
	}
	return V >= 100 ? kQtyMax : V;
}

void UAsdaCraftMakeWindowSubsystem::UpdateAffordability()
{
	// `craftWndUpdateAffordability@0xFE5AA0`: текст поля = "%d" [44]; хватает ли на [44] штук каждого из 6 материалов
	//   (`CInventory_CountItemById < [44] × нужно` ⇒ нет) — белый (−1) / красный (−65536). ★ К-6 — по стекам.
	const UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	const FAsdaMakingRecipeRow* R = Craft ? Craft->FindRecipe(RecipeId) : nullptr;
	if (!R) { return; }
	SetQtyText(Remaining);
	bool bOk = true;
	for (int32 I = 0; I < 6; ++I)
	{
		const int32 Code = UAsdaCraftSubsystem::RecipeMatCode(*R, I);
		const int32 Need = UAsdaCraftSubsystem::RecipeMatQty(*R, I);
		if (Need <= 0) { continue; }                                                 // пустой слот (−1): `0 < N·(−1)` ложно
		if (Craft->CraftableFromStacks(Code, Need) < Remaining) { bOk = false; }
	}
	EditColor = bOk ? kColWhite : kColRed;
	if (EditQty)
	{
		FEditableTextStyle St = EditQty->WidgetStyle;
		St.ColorAndOpacity = FSlateColor(FLinearColor(EditColor));
		EditQty->SetWidgetStyle(St);
	}
}

void UAsdaCraftMakeWindowSubsystem::RedrawSlots()
{
	// `uiUpgradeSlotsRedraw@0xFE57B0` (ui_core.c:101785-101850).
	if (!bRateValid) { return; }
	for (int32 S = 0; S < kTiers; ++S)
	{
		SetCtrlVisible(S + kDwMake, false);
		SetCtrlVisible(S + kDwBack, false);
		SetCtrlVisible(S + kDwAni, false);
		IconCodes[S] = 0;
	}
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	for (int32 K = 0; K < kTiers; ++K)
	{
		if (RateOutItems[K] <= 0) { continue; }
		const int32 Slot = K + kSlotBase[FMath::Clamp(TierCount, 0, 7)];
		if (Slot < 0 || Slot >= kTiers) { continue; }
		if (IT && IT->GetItem(RateOutItems[K]))
		{
			IconCodes[Slot] = RateOutItems[K];
			SetCtrlVisible(Slot + kDwMake, true);
			SetCtrlVisible(Slot + kDwBack, true);
		}
		else
		{
			IconCodes[Slot] = 0;                                                     // пустая текстура 32×32, скрыто
			SetCtrlVisible(Slot + kDwMake, false);
			SetCtrlVisible(Slot + kDwBack, false);
		}
	}
	if (IconGrid) { IconGrid->Refresh(); }
}

void UAsdaCraftMakeWindowSubsystem::HideGroup7()
{
	// `uiHideControlGroup7@0xFE5D20`: ани и крестики скрыты; подложка показана у видимой иконки; флаг +0x24F снят.
	for (int32 S = 0; S < kTiers; ++S)
	{
		SetCtrlVisible(S + kDwAni, false);
		SetCtrlVisible(S + kDwFail, false);
		if (IsShown(ControlByDword(S + kDwMake))) { SetCtrlVisible(S + kDwBack, true); }
		SetTierFlag(S + kDwMake, false);
	}
}

void UAsdaCraftMakeWindowSubsystem::SetFourEnabled(bool bEnabled)
{
	// `uiUpgradeSlotsDisable` (0) / `chat_EnableFourIcons` (1): 77537 поле, 77533 «Создать», 77538/77539 стрелки.
	if (BtnCreate) { BtnCreate->SetIsEnabled(bEnabled); }
	if (BtnUp)     { BtnUp->SetIsEnabled(bEnabled); }
	if (BtnDown)   { BtnDown->SetIsEnabled(bEnabled); }
	if (EditQty)   { EditQty->SetIsEnabled(bEnabled); }
}

void UAsdaCraftMakeWindowSubsystem::SetStatus(const FString& Text, FColor Color)
{
	StatusColor = Color;
	if (!TxtStatus) { return; }
	TxtStatus->SetText(FText::FromString(Text));
	TxtStatus->SetColorAndOpacity(FSlateColor(FLinearColor(Color)));
}

void UAsdaCraftMakeWindowSubsystem::SetQtyText(int32 V)
{
	if (!EditQty) { return; }
	const FString S = FString::Printf(TEXT("%d"), V);
	if (EditQty->GetText().ToString() == S) { return; }
	TGuardValue<bool> Guard(bSettingQtyText, true);
	EditQty->SetText(FText::FromString(S));
}

UWidget* UAsdaCraftMakeWindowSubsystem::ControlByDword(int32 Idx) const
{
	if (Idx >= kDwFMark && Idx < kDwMake) { return FMark.IsValidIndex(Idx - kDwFMark) ? FMark[Idx - kDwFMark].Get() : nullptr; }
	if (Idx >= kDwMake && Idx < kDwAni)   { return Tiers.IsValidIndex(Idx - kDwMake) ? Tiers[Idx - kDwMake].Group.Get() : nullptr; }
	if (Idx >= kDwAni && Idx < kDwBack)   { return Tiers.IsValidIndex(Idx - kDwAni) ? (UWidget*)Tiers[Idx - kDwAni].Ani.Get() : nullptr; }
	if (Idx >= kDwBack && Idx < kDwFail)  { return Tiers.IsValidIndex(Idx - kDwBack) ? Tiers[Idx - kDwBack].Back.Get() : nullptr; }
	if (Idx >= kDwFail && Idx < kDwFail + kTiers) { return Tiers.IsValidIndex(Idx - kDwFail) ? Tiers[Idx - kDwFail].Fail.Get() : nullptr; }
	return nullptr;
}

void UAsdaCraftMakeWindowSubsystem::SetCtrlVisible(int32 Idx, bool bVisible)
{
	if (UWidget* W = ControlByDword(Idx)) { AsdaUi::SetShownDecor(W, bVisible); }
}

void UAsdaCraftMakeWindowSubsystem::ShowAni(int32 Idx)
{
	// `CControl_Show` — запуск одноразового прогона `Ani_Soul2` (`EFrameStop = 1`).
	if (UAsdaAnimatedPlane* A = Cast<UAsdaAnimatedPlane>(ControlByDword(Idx))) { AsdaUi::SetShownDecor(A, true); A->RestartAnim(); }
}

void UAsdaCraftMakeWindowSubsystem::CloseHideAni(int32 Idx)
{
	// `CControl_CloseHide` — остановить без события «завершено».
	if (UAsdaAnimatedPlane* A = Cast<UAsdaAnimatedPlane>(ControlByDword(Idx))) { A->StopAnim(); AsdaUi::SetShownDecor(A, false); }
}

void UAsdaCraftMakeWindowSubsystem::SetTierFlag(int32 Idx, bool bOn)
{
	// `set_o24F` (+0x24F). ★ Волна В5 (ревизия В4 С-1, поправка ложного «читателя нет» отчёта §14): флаг читает
	//   `CControl_IsAlwaysTopRender@0x12E3CC0`, а его — `CControl_RenderChildren@0x12DF9C0` (ui_core.c:109181): текущая
	//   ступень лесенки (иконки 32×32 со сдвигом 16 px перекрываются наполовину) рисуется ПОВЕРХ соседних. Порт — общей
	//   точкой `AsdaUi::SetTopRender` (ZOrder слота; разметка не трогается).
	if (bOn) { TierFlags.Add(Idx, true); } else { TierFlags.Remove(Idx); }
	AsdaUi::SetTopRender(ControlByDword(Idx), bOn);
}

void UAsdaCraftMakeWindowSubsystem::ClearTierFlags()
{
	// Все флаги +0x24F снимаются вместе с порядком рисования (копия ключей — `SetTierFlag` правит карту).
	TArray<int32> Keys;
	TierFlags.GetKeys(Keys);
	for (int32 K : Keys) { SetTierFlag(K, false); }
	TierFlags.Reset();
}

void UAsdaCraftMakeWindowSubsystem::ResetVisuals()
{
	// Визуальная часть `itemQtyDlgReset@0xFE4F50` (item_inventory.c:69568-69586) без хвоста HUD/снимка: полоса 0, «Закрыть»
	//   77534 видна, группа 7 ступеней в исходном виде, 77516 скрыта, статус пуст, четыре контрола включены.
	if (!bControlsReady) { return; }
	BarFrac = 0.f;
	if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, 0.f); }
	Show(BtnClose, true);                                                        // 77534 снова видна
	Show(TxtClose, true);
	HideGroup7();
	SetCtrlVisible(kDwMake + 5, false);                                          // 77516
	SetStatus(FString(), kColWhite);                                             // 77532 = "", цвет −1
	SetFourEnabled(true);                                                        // `sub_9237C6` = `chat_EnableFourIcons`
}

void UAsdaCraftMakeWindowSubsystem::ReturnButtons(const TCHAR* Why)
{
	// Порт (закон `no-state-latch-before-shown`): ответа на ушедший 6602 не будет (таймаут замка, неизвестный код 6603) —
	//   кнопки назад, окно остаётся открытым.
	if (!bOpen || !bAwaitingReply) { return; }
	bAwaitingReply = false;
	SetFourEnabled(true);
	JournalAdd(FString::Printf(TEXT("%s — кнопки возвращены"), Why));
}

int32 UAsdaCraftMakeWindowSubsystem::RateOut(int32 Tier) const
{
	return (Tier >= 0 && Tier < kTiers) ? RateOutItems[Tier] : -1;
}

bool UAsdaCraftMakeWindowSubsystem::RateBonusFlagZero() const
{
	return (RateFlag2 & 0xFF) == 0;   // `self[63][2]` — байт +2 записи rate
}

FString UAsdaCraftMakeWindowSubsystem::ItemName(int32 ItemCode) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	return IT ? IT->GetItemName(ItemCode) : FString();
}

FString UAsdaCraftMakeWindowSubsystem::FormatLocName(int32 LocId, int32 ItemCode) const
{
	// `CItemManager_GetItemRecord(код)` → нет записи — строка остаётся пустой (`memset`), иначе `wsprintfA(Loc, имя)`.
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT || ItemCode <= 0 || !IT->GetItem(ItemCode)) { return FString(); }
	return Loc(LocId).Replace(TEXT("%s"), *ItemName(ItemCode));
}

void UAsdaCraftMakeWindowSubsystem::ChatAndPlank(const FString& Text)
{
	// `CChat_AddTokenizedLines(g_pChatWnd, текст, −11514, 10000, 0)` + `CControl_SetTooltipText(g_pChatWnd, текст, −11514, 10000)`.
	UGameInstance* GI = GetGameInstance();
	AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Text, kColChat, 10000.f);
	AsdaNotice::Plank(GI, EAsdaMsgCategory::System, Text, kColChat, 10000.f, 6603);
	JournalAdd(FString::Printf(TEXT("chat+plank «%s»"), *Text));
}

void UAsdaCraftMakeWindowSubsystem::JournalAdd(const FString& Line)
{
	Journal.Add(Line);
	UE_LOG(LogAsdaCraftMake, Log, TEXT("[ремесло-изготовление] %s"), *Line);
}

FString UAsdaCraftMakeWindowSubsystem::Loc(int32 Id) const
{
	return AsdaNotice::Loc(GetGameInstance(), Id);
}
