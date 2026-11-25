// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна состава рецепта `ITEM_making_R_BOX` (JUI 77000) и под-окон подрецептов 77100 / 77200.
//   Весь разбор эталона, поля `CMakeItem`, кнопки/нотисы и ключи — в шапке `AsdaCraftRecipeWindowSubsystem.h`;
//   отчёт волны — `Reports/craft/wave3.md`. Строки эталона: ui_core.c:100981-101590, item_enhance.c:10059-10211,
//   item_inventory.c:69542-69554.

#include "UI/AsdaCraftRecipeWindowSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaCraftSubsystem.h"         // модель: рецепты, уровень, изученные, материалы по стекам (К-6), нотисы
#include "UI/AsdaCraftWindowSubsystem.h"   // окно 76000: `uiPanelHideAndReset(g_pItemMainWnd)` из `itemTooltipWndReset`
#include "UI/AsdaCraftMakeWindowSubsystem.h"   // окно изготовления 77500 (волна В4): переход из кнопок 77068/77151/77251
#include "UI/AsdaJuiWindowHandle.h"        // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaNotice.h"                 // Loc
#include "UI/AsdaIconCellGrid.h"           // иконки (общая точка сеток)
#include "UI/AsdaWindowSubsystem.h"        // реестр окон, приоритетная полоса, снимок открытых окон
#include "UI/AsdaInventorySubsystem.h"     // самотест: материалы в сумку, сумка для снимка
#include "UI/AsdaHotbarSubsystem.h"        // самотест: гейт хотбара «ремесло открыто»
#include "UI/AsdaCommunitySubsystem.h"     // самотест: восстановление снимка (слот 14)
#include "Npc/AsdaNpcDialogSubsystem.h"    // самотест: снимок при входе в разговор и восстановление при уходе
#include "Login/AsdaButtonRelay.h"
#include "World/AsdaPlayerController.h"
#include "AsdaSessionReset.h"
#include "AsdaNetSubsystem.h"                 // пол персонажа — атлас иконок (волна В4)
#include "Test/AsdaSelfTestRunGate.h"   // Р-2: RefuseDeferredStartWhenOnline — повторная проверка офлайна на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaCVarSave.h"                    // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотестов — запомнить и вернуть (значение + SetBy)
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"             // FScreenshotRequest — именованный кадр самотеста

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCraftRecipe, Log, All);

// ═════════════════════════════ КЛЮЧИ ОТКАТА ИСПРАВЛЕНИЙ ═════════════════════════════════════════

// ★ К-7. Снимок открытых окон (`uiCaptureOpenWindowsState`) при открытии 77000 только ставит флаги; снимает их лишь
//   восстановление (`uiCloseAllToggleWindows`, ui_core.c:60967). Окно состава закрыли без изготовления — флаги живут до
//   чужого восстановления (уход от NPC `UI_resetAllWindowsAndLeave_5045` / сброс 77500) и переоткрывают окна, открытые
//   ещё в момент выбора рецепта.
static ASDA_DEV_CVAR(int32, CVarCraftFixStaleSnapshot,
	TEXT("asda.Craft.FixStaleSnapshot"), 1,
	TEXT("Дефект оригинала К-7: 1 = окно состава 77000 закрыто без перехода в 77500 — его флаги снимка открытых окон "
	     "снимаются (снимок диалога NPC не трогается); 0 = как оригинал — флаги живут до чужого восстановления и "
	     "переоткрывают давно закрытые окна."),
	ECVF_Default);

// ★ Д-В3-1 (найден волной В3, доказан байтами `0xFE4158…0xFE4205`). В `CCraftRecipe_RefreshSlotUI` строка M2..M4 под-окна 1
//   С подрецептом затемняется «не хватает ИЛИ уровень мал» (как строки 77000), а БЕЗ подрецепта — «не хватает И уровень
//   ДОСТАТОЧЕН»: при низком уровне ремесла затемнения нет вовсе, окно выглядит «можно изготовить».
static ASDA_DEV_CVAR(int32, CVarCraftFixSubRowDarken,
	TEXT("asda.Craft.FixSubRowDarken"), 1,
	TEXT("Дефект оригинала (под-окно 77100, строки без подрецепта): 1 = затемнение «материала не хватает ИЛИ уровень "
	     "ремесла ниже нужного» — как у всех строк; 0 = как оригинал — «не хватает И уровень достаточен» (при низком "
	     "уровне строка не затемняется)."),
	ECVF_Default);

// ★ Д-В3-2 (найден волной В3). 77152/77159 закрывают ТОЛЬКО под-окно 1 (`uiItemWndClearSlot(this, 0)` +
//   `uiHideTooltipControlReset`), под-окно 2 с подрецептом второго уровня остаётся на экране без родителя; при этом
//   клик по строке 77000 всегда закрывает под-окно 2 первым (`uiItemWndClearSlot(this, 1)`) — «2 без 1» код сам не допускает.
static ASDA_DEV_CVAR(int32, CVarCraftFixOrphanSubWindow,
	TEXT("asda.Craft.FixOrphanSubWindow"), 1,
	TEXT("Дефект оригинала (под-окна подрецептов): 1 = закрытие под-окна 1 (77152/77159) закрывает и под-окно 2; "
	     "0 = как оригинал — под-окно 2 остаётся без родителя."),
	ECVF_Default);

namespace
{
	const FName  kWindowId(UAsdaCraftSubsystem::kRecipeWindowId);
	const TCHAR* kWindowWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ITEM_making_R_BOX.WBP_ITEM_making_R_BOX_C");
	const FName  kSubZone[2] = { FName(TEXT("ITEM_making_B_tooltip_1")), FName(TEXT("ITEM_making_B_tooltip_2")) };
	const TCHAR* kSubFramePlanes[2] = { TEXT("ITEM_making_B_tooltip_1_91_%d"), TEXT("ITEM_making_B_tooltip_2_125_%d") };

	// Loc-строки окна (`CMakeItemSub_InitControls` / `BindControls` / `uiCraftPanel_Refresh` / `uiItemCommandDispatch`).
	constexpr int32 kLocTitle      = 5530;   // 77001 «Ингредиенты»
	constexpr int32 kLocSubTitle1  = 5531;   // 77101 (77201 = 5532: `i + 5531`)
	constexpr int32 kLocMake       = 5521;   // 77068 / 77151 / 77251
	constexpr int32 kLocClose      = 5035;   // 77067 / 77152 / 77252
	constexpr int32 kLocNowNeed    = 5550;   // t2 / s2 строк
	constexpr int32 kLocNoList     = 5570;   // «Список не выбран»
	constexpr int32 kLocLackMat    = 5569;   // «Недостаточно предметов»
	constexpr int32 kLocLowLevel   = 5584;   // «Неверный ур. ремесла»
	constexpr int32 kLocNoRecipe   = 5585;   // «Нет инструкции…»

	// Рамка 77154: `.JUI` Origin 64 6 (в `ITEM_making_B_BOX_L`), код — `SetField74_75(64, 40*i + 7)` в координатах того же
	//   родителя; в ассете группа стоит в той же системе (L64 T6) ⇒ позиция = база ассета + (код − Origin JUI).
	constexpr float kMainFrameJuiX = 64.f, kMainFrameJuiY = 6.f;
	constexpr float kMainFrameCodeX = 64.f, kMainFrameCodeY0 = 7.f, kMainFrameStep = 40.f;
	// Рамка 77153 (ревизия МЕЛК-1): Origin 38 79 в 77100, код — `SetField74_75(40, 34*idx + 48)`, idx 1..3. У рамки в
	//   ассете нет своей группы — её 8 планов лежат прямо в группе под-окна, поэтому сдвигается каждый план.
	constexpr float kSubFrameJuiX = 38.f, kSubFrameJuiY = 79.f;
	constexpr float kSubFrameCodeX = 40.f, kSubFrameCodeY0 = 48.f, kSubFrameStep = 34.f;

	// ★ Ревизия В3 МЕЛК-11: помощники — ОБЩИЕ точки (`AsdaUi::SetTextIf/SetShownDecorAll/AnyShown`), здесь только имена.
	void SetTxt(UTextBlock* T, const FString& S) { AsdaUi::SetTextIf(T, S); }
	void SetGroupShown(const TArray<TObjectPtr<UWidget>>& G, bool bShown) { AsdaUi::SetShownDecorAll(G, bShown); }
	bool IsShown(const UWidget* W) { return AsdaUi::IsShown(W); }   // ревизия В4 МЕЛК-6: общая точка `AsdaUi::IsShown`
	bool GroupShown(const TArray<TObjectPtr<UWidget>>& G) { return AsdaUi::AnyShown(G); }
	// Курсор над узлом, только если узел и вся цепочка предков показаны (скрытые контролы JUI событий не получают).
	bool CursorOver(const UWidget* W)
	{
		for (const UWidget* P = W; P; P = P->GetParent()) { if (!IsShown(P)) { return false; } }
		return W && AsdaUi::IsCursorOverWidget(W);
	}

	// Поля строки рецепта по индексу — ОДНА точка у модели (`UAsdaCraftSubsystem::Recipe*`, ревизия В3 МЕЛК-11).
	int32 MatCodeOf(const FAsdaMakingRecipeRow& R, int32 I) { return UAsdaCraftSubsystem::RecipeMatCode(R, I); }
	int32 MatQtyOf(const FAsdaMakingRecipeRow& R, int32 I) { return UAsdaCraftSubsystem::RecipeMatQty(R, I); }
	int32 SubOf(const FAsdaMakingRecipeRow& R, int32 I) { return UAsdaCraftSubsystem::RecipeSubRecipe(R, I); }
	int32 ReqLevelOf(const FAsdaMakingRecipeRow& R) { return UAsdaCraftSubsystem::RecipeReqLevel(R); }   // `*(char *)(rec + 67)`
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaCraftRecipeWindowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaCraftSubsystem::StaticClass());
	IconCodes.Init(0, 1 + kMatRows + kSubRows * kSubWins);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr)
	{
		HModel = Craft->OnModelChanged.AddUObject(this, &UAsdaCraftRecipeWindowSubsystem::OnModelChanged);
	}
	// ESC-закрывателя и строки в `uiCloseAllWindows` у окна НЕТ (флага `CControl+0x228` нет; закрывается вместе с 76000).
	UE_LOG(LogAsdaCraftRecipe, Log, TEXT("[ремесло-состав] подсистема окна 77000 поднята"));
}

void UAsdaCraftRecipeWindowSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr) { Craft->OnModelChanged.Remove(HModel); }
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaCraftRecipeWindowSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Окно — визуальный слой сессии. Снимок окон при сбросе чистит сама `UAsdaWindowSubsystem` (без восстановления),
	//   поэтому здесь только своё состояние; окно 76000 сбрасывается своим `ResetSessionState`.
	if (bOpen && bControlsReady)
	{
		ClearSlot(1);
		ClearSlot(0);
		HideTooltipControlReset();
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SetWindowVisible(kWindowId, false);
		}
	}
	bOpen = false;
	RecipeId = -1;
	MainSel = -1;
	MainOver = -1;
	SubOver = -1;
	for (int32 K = 0; K < kSubWins; ++K) { SubOpen[K] = false; SubSel[K] = -1; SubId[K] = -1; bSubRecNull[K] = false; }
	bHoldSnapshotForMake = false;
	Mouse.Reset();
	UE_LOG(LogAsdaCraftRecipe, Verbose, TEXT("[ремесло-состав] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
}

void UAsdaCraftRecipeWindowSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.CraftRecipeWindow");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (!bOpen || !bControlsReady) { return; }
	Mouse.Update();
	// Модальный гейт (П8-9): под модальным нотисом (ONEBUT 5569/5584…) эталон мышь окнам ниже не отдаёт.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		SetMainOver(-1);
		SetSubOver(-1);
		Mouse.Reset();
		return;
	}
	TickMouse();
}

void UAsdaCraftRecipeWindowSubsystem::OnModelChanged(int32 Opcode)
{
	// 6601 код 1 (`craftRemoveSelectedItemSlot@0xFE0C60`, item_enhance.c:10039): рецепт забыт — `itemTooltipWndReset(this+80)`.
	if (Opcode == 6601) { ResetWindow(TEXT("6601 код 1 (craftRemoveSelectedItemSlot)")); }
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaCraftRecipeWindowSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kWinZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&) { SetupControls(); RegisterZones(); },
		[]() { UE_LOG(LogAsdaCraftRecipe, Error, TEXT("[ремесло-состав] WBP_ITEM_making_R_BOX не загрузился (%s)"), kWindowWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaCraftRecipeWindowSubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->UnregisterUiZone(kSubZone[0]);
		WM->UnregisterUiZone(kSubZone[1]);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	bControlsReady = false;
	bOpen = false;
	TxtTitle = TxtName = ResultText = TxtCancel = TxtMake = nullptr;
	DragHandle = MainFrame = nullptr;
	ResultIcon = nullptr;
	BtnCancel = BtnMake = BtnExit = nullptr;
	MatRows.Reset();
	Subs.Reset();
	SubFrame.Reset();
	SubFrameBase.Reset();
	IconGrid = nullptr;
	Relays.Reset();
	FoundControls = 0;
	bSubRaised = false;
	for (int32 K = 0; K < kSubWins; ++K) { SubOpen[K] = false; }
	Mouse.Reset();
}

void UAsdaCraftRecipeWindowSubsystem::RegisterZones()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — девять планов `ITEM_making_R_BOX_0_0…8` (скин `Black_BottomRound_White2Border`); у под-окон —
	//   свои девять планов скина `Tool_Tip` (они вылезают за рамку 77000 справа, клик по ним не должен уходить в мир).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("ITEM_making_R_BOX_0_%d"), 9);
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kSubZone[0], this, Widget, SubOpen[0], kSubFramePlanes[0], 9);
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kSubZone[1], this, Widget, SubOpen[1], kSubFramePlanes[1], 9);
}

void UAsdaCraftRecipeWindowSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	MatRows.SetNum(kMatRows);
	Subs.SetNum(kSubWins);
	for (FAsdaCraftSubWinUi& S : Subs) { S.Rows.SetNum(kSubRows); }
	SubFrame.Init(nullptr, 8);
	SubFrameBase.Init(FVector2D::ZeroVector, 8);

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	// Строка D{i} (1…6) из имени `ITEM_making_B_BOX_D<i>…`; −1 — не строка.
	auto MainRowOf = [](const FString& N) -> int32
	{
		static const FString P(TEXT("ITEM_making_B_BOX_D"));
		if (!N.StartsWith(P) || N.Len() <= P.Len()) { return -1; }
		const TCHAR C = N[P.Len()];
		if (C < TEXT('1') || C > TEXT('6')) { return -1; }
		if (N.Len() > P.Len() + 1 && N[P.Len() + 1] != TEXT('_')) { return -1; }
		return (int32)(C - TEXT('1'));
	};
	// Под-окно k (0/1) и строка M{j} (0…3) из имени `ITEM_making_B_tooltip_<k+1>_M<j+1>…`.
	auto SubRowOf = [](const FString& N, int32& OutK, int32& OutJ) -> bool
	{
		static const FString P(TEXT("ITEM_making_B_tooltip_"));
		if (!N.StartsWith(P) || N.Len() < P.Len() + 4) { return false; }
		const TCHAR Ck = N[P.Len()];
		if ((Ck != TEXT('1') && Ck != TEXT('2')) || N[P.Len() + 1] != TEXT('_') || N[P.Len() + 2] != TEXT('M')) { return false; }
		const TCHAR Cj = N[P.Len() + 3];
		if (Cj < TEXT('1') || Cj > TEXT('4')) { return false; }
		if (N.Len() > P.Len() + 4 && N[P.Len() + 4] != TEXT('_')) { return false; }
		OutK = (int32)(Ck - TEXT('1'));
		OutJ = (int32)(Cj - TEXT('1'));
		return true;
	};

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("ITEM_making_B_BOX_Xbtn")) { BtnCancel = Btn; continue; }   // 77067
			if (N == TEXT("ITEM_making_Mark_ok"))    { BtnMake = Btn; continue; }     // 77068
			if (N == TEXT("ITEM_making_B_BOX_XX"))   { BtnExit = Btn; continue; }     // 77069
			for (int32 K = 0; K < kSubWins; ++K)
			{
				if (N == FString::Printf(TEXT("ITEM_making_B_tooltip_%d_okbtn"), K + 1)) { Subs[K].BtnOk = Btn; }   // 77151/77251
				if (N == FString::Printf(TEXT("ITEM_making_B_tooltip_%d_xbtn"), K + 1))  { Subs[K].BtnX = Btn; }    // 77152/77252
				if (N == FString::Printf(TEXT("ITEM_making_B_tooltip_%d_XX"), K + 1))    { Subs[K].BtnXX = Btn; }   // 77159/77259
			}
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("ITEM_making_B_BOX_textmove")) { TxtTitle = Tb; continue; }       // 77001
			if (N == TEXT("ITEM_making_B_BOX_t1"))       { TxtName = Tb; continue; }        // 77002
			if (N == TEXT("ITEM_making_B_BOX_Smark"))    { ResultText = Tb; continue; }     // подпись 77003
			if (N == TEXT("ITEM_making_B_BOX_Xbtn_Text")) { TxtCancel = Tb; continue; }
			if (N == TEXT("ITEM_making_Mark_ok_Text"))   { TxtMake = Tb; continue; }
			for (int32 K = 0; K < kSubWins; ++K)
			{
				if (N == FString::Printf(TEXT("ITEM_making_B_tooltip_%d_top1"), K + 1))       { Subs[K].Title = Tb; }    // 77101/77201
				// 77102 в ассете — `ITEM_making_B_tooltip1_top2` (без «_» перед номером, как в `.JUI`), 77202 —
				//   `ITEM_making_B_tooltip_2_top2`: имена взяты из `gamemain_jui.ini` как есть, принимаем оба написания.
				if (N == FString::Printf(TEXT("ITEM_making_B_tooltip%d_top2"), K + 1) || N == FString::Printf(TEXT("ITEM_making_B_tooltip_%d_top2"), K + 1))
				{
					Subs[K].Parent = Tb;   // 77102/77202
				}
				if (N == FString::Printf(TEXT("ITEM_making_B_tooltip_%d_okbtn_Text"), K + 1)) { Subs[K].TxtOk = Tb; }
				if (N == FString::Printf(TEXT("ITEM_making_B_tooltip_%d_xbtn_Text"), K + 1))  { Subs[K].TxtX = Tb; }
			}
			const int32 R = MainRowOf(N);
			if (R >= 0)
			{
				FAsdaCraftMatRowUi& U = MatRows[R];
				if (N == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_t1"), R + 1))    { U.Name = Tb; }
				if (N == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_t2"), R + 1))    { U.Sub = Tb; }
				if (N == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_t3"), R + 1))    { U.Num = Tb; }
				if (N == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_mark"), R + 1))  { U.IconText = Tb; }
				if (N == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_black"), R + 1)) { U.Black.Add(Tb); }
			}
			int32 K = 0, J = 0;
			if (SubRowOf(N, K, J))
			{
				FAsdaCraftMatRowUi& U = Subs[K].Rows[J];
				const FString Base = FString::Printf(TEXT("ITEM_making_B_tooltip_%d_M%d"), K + 1, J + 1);
				if (N == Base)                     { U.IconText = Tb; }
				if (N == Base + TEXT("_s1"))       { U.Name = Tb; }
				if (N == Base + TEXT("_s2"))       { U.Sub = Tb; }
				if (N == Base + TEXT("_s3"))       { U.Num = Tb; }
				if (N == Base + TEXT("_black"))    { U.Black.Add(Tb); }
			}
			continue;
		}
		// Группы: рамка 77154, корни под-окон, строки под-окон, строки 77000.
		if (N == TEXT("ITEM_making_B_select_grp"))
		{
			MainFrame = W;
			FVector2D P, S;
			if (AsdaUi::SlotRect(W, P, S)) { MainFrameBase = P; }
			continue;
		}
		if (N.EndsWith(TEXT("_DRAG")))
		{
			if (N.StartsWith(TEXT("ITEM_making_B_BOX_textmove"))) { DragHandle = W; }
			for (int32 K = 0; K < kSubWins; ++K) { if (N.StartsWith(FString::Printf(TEXT("ITEM_making_B_tooltip_%d_top1"), K + 1))) { Subs[K].Drag = W; } }
			continue;
		}
		if (N.EndsWith(TEXT("_grp")))
		{
			for (int32 K = 0; K < kSubWins; ++K)
			{
				if (N == FString::Printf(TEXT("ITEM_making_B_tooltip_%d_grp"), K + 1)) { Subs[K].Root = W; }
				for (int32 J = 0; J < kSubRows; ++J)
				{
					if (N == FString::Printf(TEXT("ITEM_making_B_tooltip_%d_M%d_grp"), K + 1, J + 1)) { Subs[K].Rows[J].Group = W; }
				}
			}
			const int32 R = MainRowOf(N);
			if (R >= 0 && N == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_grp"), R + 1)) { MatRows[R].Group = W; }
			continue;
		}
		// HIT-планы подписей (`…_t1_11_HIT`, `…_M2_s1_101_HIT`) — зона строки.
		if (N.EndsWith(TEXT("_HIT")))
		{
			const int32 R = MainRowOf(N);
			if (R >= 0 && N.StartsWith(FString::Printf(TEXT("ITEM_making_B_BOX_D%d_t"), R + 1))) { MatRows[R].Hit.Add(W); }
			int32 K = 0, J = 0;
			if (SubRowOf(N, K, J) && N.StartsWith(FString::Printf(TEXT("ITEM_making_B_tooltip_%d_M%d_s"), K + 1, J + 1))) { Subs[K].Rows[J].Hit.Add(W); }
			continue;
		}
		// Планы `<idc>_<juiIdx>_<plane>`.
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { continue; }
		if (Idc == TEXT("ITEM_making_B_BOX_Smark") && Plane == 0) { ResultIcon = Cast<UImage>(W); ResultIconName = FName(*N); continue; }
		if (Idc == TEXT("ITEM_making_B_tooltip_1_select") && Plane >= 0 && Plane < 8)
		{
			SubFrame[Plane] = W;
			FVector2D P, S;
			if (AsdaUi::SlotRect(W, P, S)) { SubFrameBase[Plane] = P; }
			continue;
		}
		const int32 R = MainRowOf(Idc);
		if (R >= 0)
		{
			FAsdaCraftMatRowUi& U = MatRows[R];
			if (Idc == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_mark"), R + 1) && Plane == 0) { U.Icon = Cast<UImage>(W); U.IconName = FName(*N); U.Hit.Add(W); }
			if (Idc == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_black"), R + 1)) { U.Black.Add(W); }
			if (Idc == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_over"), R + 1))  { U.Over.Add(W); }
			if (Idc == FString::Printf(TEXT("ITEM_making_B_BOX_D%d_plus"), R + 1))  { U.Plus.Add(W); }
			continue;
		}
		int32 K = 0, J = 0;
		if (SubRowOf(Idc, K, J))
		{
			FAsdaCraftMatRowUi& U = Subs[K].Rows[J];
			const FString Base = FString::Printf(TEXT("ITEM_making_B_tooltip_%d_M%d"), K + 1, J + 1);
			if (Idc == Base && Plane == 0)     { U.Icon = Cast<UImage>(W); U.IconName = FName(*N); U.Hit.Add(W); }
			if (Idc == Base + TEXT("_black"))  { U.Black.Add(W); }
			if (Idc == Base + TEXT("_over"))   { U.Over.Add(W); }
			if (Idc == Base + TEXT("_plus"))   { U.Plus.Add(W); }
		}
	}

	// ── Иконки: 15 ячеек одной общей сетки (пустая — пустая текстура, как `CControl_DrawTextureRect(…, пусто, 0,0,32,32)`) ──
	TArray<FName> Names;
	Names.Add(ResultIconName);
	for (const FAsdaCraftMatRowUi& U : MatRows) { Names.Add(U.IconName); }
	for (const FAsdaCraftSubWinUi& S : Subs) { for (const FAsdaCraftMatRowUi& U : S.Rows) { Names.Add(U.IconName); } }
	IconCodes.Init(0, Names.Num());
	IconGrid = NewObject<UAsdaIconCellGrid>(this);
	IconGrid->SetCellSizePx(32.f);
	IconGrid->SetClickOverlayEnabled(false);
	IconGrid->bClearEmptyCells = true;
	// ★ Волна В4: атлас пола персонажа — эталон рисует иконки через `g_ItemImgOptionVariant` (ui_core.c:101502).
	const UAsdaNetSubsystem* NetG = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	IconGrid->SetupNamed(Widget, Names, /*bMale*/ !NetG || NetG->ResolveSelfGender() != 2, [this](int32 C) -> int32
	{
		return IconCodes.IsValidIndex(C) ? FMath::Max(0, IconCodes[C]) : 0;
	}, [](int32) {});

	// ── Кнопки (ветка msgType 66561 `uiItemCommandDispatch`; своих звуков у окна нет) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnCancel, 77067, [this]() { OnCloseClicked(); });
	Bind(BtnExit,   77069, [this]() { OnCloseClicked(); });
	Bind(BtnMake,   77068, [this]() { OnMakeClicked(); });
	for (int32 K = 0; K < kSubWins; ++K)
	{
		Bind(Subs[K].BtnOk, 77151 + 100 * K, [this, K]() { OnSubMakeClicked(K); });
		Bind(Subs[K].BtnX,  77152 + 100 * K, [this, K]() { OnSubCloseClicked(K); });
		Bind(Subs[K].BtnXX, 77159 + 100 * K, [this, K]() { OnSubCloseClicked(K); });
	}

	// ── Подписи init — `CMakeItemSub_InitControls` (ui_core.c:100995-101059) и `BindControls` (:101083-101103) ──
	SetTxt(TxtTitle, Loc(kLocTitle));        // 77001 = 5530
	SetTxt(TxtCancel, Loc(kLocClose));       // 77067 = 5035 (подпись ассета «Быстрее» — образец)
	SetTxt(TxtMake, Loc(kLocMake));          // 77068 = 5521
	AsdaUi::SetShownDecor(MainFrame, false); // 77154 `SetVisible(0)`
	for (int32 K = 0; K < kSubWins; ++K)
	{
		SetTxt(Subs[K].Title, Loc(kLocSubTitle1 + K));   // 77101 = 5531, 77201 = 5532
		SetTxt(Subs[K].TxtOk, Loc(kLocMake));             // 77151 / 77251 = 5521
		SetTxt(Subs[K].TxtX, Loc(kLocClose));             // 77152 / 77252 = 5035
	}
	// 77153 в ассете и так Collapsed. ★ Поправка ревизии В3 МЕЛК-7: `BindControls` (байты 0xFE1BA6-0xFE1BFB) второй раз
	//   прячет не 77153, а `[5]` = 77154; прятать 77153 при разборе безвредно (по поведению то же), поэтому оставлено.
	for (UWidget* P : SubFrame) { AsdaUi::SetShownDecor(P, false); }

	// ── Итог разбора: 104 контрола с IDNum групп 770/771/772 ──
	int32 Found = Widget ? 1 : 0;   // 77000 — корень
	auto Has = [&Found](const UObject* O) { if (O) { ++Found; } };
	auto HasG = [&Found](const TArray<TObjectPtr<UWidget>>& G) { if (G.Num() > 0) { ++Found; } };
	Has(TxtTitle); Has(TxtName); Has(ResultIcon);                                  // 77001 77002 77003
	for (const FAsdaCraftMatRowUi& U : MatRows)                                    // 77010+10i … 77016+10i (7 × 6)
	{
		Has(U.Icon); Has(U.Name); Has(U.Sub); Has(U.Num); HasG(U.Black); HasG(U.Over); HasG(U.Plus);
	}
	Has(MainFrame); Has(BtnCancel); Has(BtnMake); Has(BtnExit);                    // 77154 77067 77068 77069
	for (int32 K = 0; K < kSubWins; ++K)
	{
		const FAsdaCraftSubWinUi& S = Subs[K];
		Has(S.Root); Has(S.Title); Has(S.Parent); Has(S.BtnXX); Has(S.BtnOk); Has(S.BtnX);   // 77x00 77x01 77x02 77x59 77x51 77x52
		for (int32 J = 0; J < kSubRows; ++J)
		{
			const FAsdaCraftMatRowUi& U = S.Rows[J];
			Has(U.Icon); Has(U.Name); Has(U.Sub); Has(U.Num);                     // 77x10+10j … 77x13+10j
			if (K == 0 && J > 0) { HasG(U.Black); HasG(U.Over); HasG(U.Plus); }   // 77124…77146: только под-окно 1, M2..M4
		}
	}
	{
		bool bFrame = true;
		for (const UWidget* P : SubFrame) { bFrame &= (P != nullptr); }
		if (bFrame) { ++Found; }                                                   // 77153 (8 планов)
	}
	FoundControls = Found;
	bControlsReady = (MatRows[0].Icon != nullptr && MatRows[0].Num != nullptr && BtnMake != nullptr && MainFrame != nullptr
		&& Subs[0].Root != nullptr && Subs[1].Root != nullptr);
	UE_LOG(LogAsdaCraftRecipe, Log, TEXT("%s"), *DumpControls());
}

// ═════════════════════════════ ОТКРЫТИЕ / СБРОС ═══════════════════════════════════════════════

bool UAsdaCraftRecipeWindowSubsystem::OpenForRecipe(int32 InRecipeId, const TCHAR* Source)
{
	// `uiCraftWnd_OpenForItem@0xFE20D0` (ui_core.c:101157-101177).
	if (InRecipeId < 0 || !RecipeRow(InRecipeId)) { return false; }            // `if (!a2) return 0`
	if (RecipeId >= 0 && RecipeId == InRecipeId) { return false; }            // `*(i16*)[137] == *a2` — тот же рецепт
	if (!EnsureWidget()) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	RecipeId = InRecipeId;                                                    // `[137] = rec`
	RefreshPanel();                                                           // `uiCraftPanel_Refresh`
	ClearSlot(1);                                                             // `uiItemWndClearSlot(this, 1)`
	ClearSlot(0);                                                             // `uiItemWndClearSlot(this, 0)`
	SetMainOver(-1);                                                          // [58] → SetVisible(0), [58] = 0
	AsdaUi::SetShownDecor(MainFrame, false);                                  // [5] рамка 77154
	MainSel = -1;                                                             // [1] = −1
	bOpen = true;
	Mouse.Reset();
	WM->SetWindowVisible(kWindowId, true);                                    // `SetVisible([2], 1)`
	WM->BringWindowToFront(kWindowId);                                        // `CClass_MapInsert(g_pWindowMgr, [2])`
	// `set_o229([2], 1)` — AlwaysTop: верхний список менеджера окон ⇒ полоса приоритетных окон порта (см. шапку .h).
	WM->SetWindowPriority(kWindowId, true);
	// `uiCaptureOpenWindowsState(g_pNpcWnd)` — снимок открытых окон (общая точка, владелец `Craft`).
	const int32 Captured = WM->CaptureOpenWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::Craft, TEXT("окно состава 77000"));
	JournalAdd(FString::Printf(TEXT("открыто — рецепт %d (%s), снимок: флагов %d"), RecipeId, Source, Captured));
	return true;
}

bool UAsdaCraftRecipeWindowSubsystem::IsStaleSnapshotFixEnabled()
{
	return CVarCraftFixStaleSnapshot.GetValueOnGameThread() != 0;
}

void UAsdaCraftRecipeWindowSubsystem::ResetWindow(const TCHAR* Why)
{
	// `itemTooltipWndReset@0xFE2200` (item_inventory.c:69542-69554).
	const bool bWas = bOpen;
	if (bControlsReady)
	{
		ClearSlot(1);
		ClearSlot(0);
	}
	RecipeId = -1;                                                            // `[137] = 0`
	SetMainOver(-1);                                                          // [58]
	if (MainFrame) { AsdaUi::SetShownDecor(MainFrame, false); }               // [5]
	MainSel = -1;                                                             // [1] = −1
	bOpen = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (Widget) { WM->SetWindowVisible(kWindowId, false); }              // `SetVisible([2], 0)`
		// ★ К-7: окно закрыто без перехода в 77500 — снять СВОИ флаги снимка (у эталона они живут до чужого восстановления).
		//   ★ Волна В8 (ревизия В7 С-2): пока ОТКРЫТО 77500, флаги владельца `Craft` принадлежат ему — их вернёт
		//   `itemQtyDlgReset` (сброс 77500). Путь игрока: сумка → 76000 → рецепт → 77500 → Z (76000 поверх 77500, гейта нет и у
		//   эталона) → закрыть 76000 ⇒ прежде здесь снимались флаги, и после изготовления сумка не возвращалась (у эталона
		//   вернулась бы). Защёлка `bHoldSnapshotForMake` покрывает только момент перехода (77500 ещё не открыто).
		const UAsdaCraftMakeWindowSubsystem* Make = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr;
		const bool bMakeOwnsSnapshot = Make && Make->IsOpen();
		//   ★ W8-2 (исправление К-9): закрытие из блока смерти ДО 5045 (`bHoldSnapshotForDeath`) — снимок тоже не снимается:
		//   5045 восстанавливает его в том же блоке (entity_char.c:16900), он не «протух». Прежде К-7 снимал здесь флаги
		//   владельца `Craft`, и при смерти с открытым 77000 5045 находил снимок пустым — не возвращалось ни одно окно.
		if (IsStaleSnapshotFixEnabled() && !bHoldSnapshotForMake && !bHoldSnapshotForDeath && !bMakeOwnsSnapshot)
		{
			WM->DropCapturedWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::Craft, Why);
		}
	}
	Mouse.Reset();
	// `uiPanelHideAndReset(g_pItemMainWnd)` — хвост эталона (снять выбор строки 76000, over, пересборка).
	if (UAsdaCraftWindowSubsystem* W76 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr)
	{
		W76->OnRecipeWindowReset();
	}
	if (bWas) { JournalAdd(FString::Printf(TEXT("закрыто (itemTooltipWndReset) — %s"), Why)); }
}

void UAsdaCraftRecipeWindowSubsystem::RefreshAll()
{
	// `uiCraftPanelRefreshAll@0xFE4460`: состав + оба под-окна (у закрытого под-окна запись [92+39k] = 0 — ничего).
	if (!bControlsReady) { return; }
	RefreshPanel();
	RefreshSlotUI(0);
	RefreshSlotUI(1);
}

// ═════════════════════════════ СОСТАВ И ПОД-ОКНА ══════════════════════════════════════════════

void UAsdaCraftRecipeWindowSubsystem::FillMatRow(FAsdaCraftMatRowUi& Ui, int32 Code, int32 Need, int32& OutHave)
{
	// Имя — `ItemData_FindByCode(itemCode)`, t2/s2 — loc 5550, t3/s3 — `wsprintfA("%d / %d", есть, нужно)`.
	//   «Есть» — `CInventory_CountItemById` (★ К-6: при `asda.Craft.FixStackPick 1` — наибольший стек, как спишет сервер).
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	OutHave = Craft ? Craft->UsableMaterialCount(Code) : 0;
	SetTxt(Ui.Name, IT ? IT->GetItemName(Code) : FString());
	SetTxt(Ui.Sub, Loc(kLocNowNeed));
	SetTxt(Ui.Num, FString::Printf(TEXT("%d / %d"), OutHave, Need));
}

void UAsdaCraftRecipeWindowSubsystem::ClearMatRow(FAsdaCraftMatRowUi& Ui)
{
	SetTxt(Ui.Name, FString());
	SetTxt(Ui.Num, FString());
	SetTxt(Ui.Sub, FString());
}

bool UAsdaCraftRecipeWindowSubsystem::RefreshPanel()
{
	// `uiCraftPanel_Refresh@0xFE3160` (ui_core.c:101462-101582).
	const FAsdaMakingRecipeRow* R = RecipeRow(RecipeId);
	if (!R || !bControlsReady) { return false; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Result = IT ? IT->GetItem(R->ResultCode) : nullptr;
	if (!Result)
	{
		// Нет записи предмета-результата: пустая иконка 77003, остальное не трогается, `return 0`.
		IconCodes[0] = 0;
		if (IconGrid) { IconGrid->Refresh(); }
		return false;
	}
	IconCodes[0] = R->ResultCode;                                             // 77003 — иконка результата (+72)
	const UAsdaCraftSubsystem* CraftS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;   // [ПОРТ] 09.10.2026 (мультиязычность)
	SetTxt(TxtName, CraftS ? CraftS->RecipeName(R) : R->RecipeName);          // 77002 — имя рецепта (+2), язык сессии
	const int32 Lvl = CraftLevel();
	for (int32 I = 0; I < kMatRows && I < MatRows.Num(); ++I)
	{
		FAsdaCraftMatRowUi& U = MatRows[I];
		const int32 Code = MatCodeOf(*R, I);
		if (Code >= 0)
		{
			const FAsdaItemRecord* M = IT ? IT->GetItem(Code) : nullptr;
			if (!M) { continue; }   // записи предмета нет — эталон строку не трогает (`if (matItemRecord)`)
			MatPresent[I] = true;                                             // `[50+i] = 1`
			SetGroupShown(U.Over, false);                                     // `SetVisible([14+7i], 0)`
			IconCodes[1 + I] = Code;
			int32 Have = 0;
			const int32 Need = MatQtyOf(*R, I);
			FillMatRow(U, Code, Need, Have);
			// black — не хватает ИЛИ уровень ремесла ниже `rec+67`; plus — у материала есть подрецепт (+112+2i >= 0).
			const bool bDark = Have < Need || ReqLevelOf(*R) > Lvl;
			SetGroupShown(U.Black, bDark);
			SetGroupShown(U.Plus, SubOf(*R, I) >= 0);
		}
		else
		{
			// Пустой слот: пустая текстура, тексты пусты, over скрыт, black ПОКАЗАН, plus скрыт, `[50+i] = 0`.
			IconCodes[1 + I] = 0;
			ClearMatRow(U);
			SetGroupShown(U.Over, false);
			SetGroupShown(U.Black, true);
			SetGroupShown(U.Plus, false);
			MatPresent[I] = false;
		}
	}
	if (IconGrid) { IconGrid->Refresh(); }
	return true;
}

bool UAsdaCraftRecipeWindowSubsystem::RefreshSlotUI(int32 K)
{
	// `CCraftRecipe_RefreshSlotUI@0xFE3920` (item_enhance.c:10059-10211).
	if (K < 0 || K >= kSubWins || !bControlsReady) { return false; }
	const FAsdaMakingRecipeRow* Sub = SubRecord(K);                          // `apSubSelHi[39k + 2]`
	if (!Sub) { return false; }
	// Родитель: для под-окна 1 — рецепт 77000 и его выбранная строка [1]; для под-окна 2 — подрецепт под-окна 1 и его
	//   выбранная строка M2..M4 ([90] − 1 = индекс материала подрецепта).
	const FAsdaMakingRecipeRow* Parent = (K == 0) ? RecipeRow(RecipeId) : SubRecord(0);
	const int32 Slot = (K == 0) ? MainSel : (SubSel[0] - 1);
	FAsdaCraftSubWinUi& S = Subs[K];
	if (!Parent || Slot < 0 || Slot >= kMatRows) { return false; }            // эталон разыменовал бы NULL — порт не падает
	const UAsdaCraftSubsystem* CraftS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;   // [ПОРТ] 09.10.2026 (мультиязычность)
	SetTxt(S.Parent, CraftS ? CraftS->RecipeName(Parent) : Parent->RecipeName);   // 77102/77202 = `pRecipeData + 2` (РОДИТЕЛЬ), язык сессии
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const int32 MainCode = MatCodeOf(*Parent, Slot);
	if (!IT || !IT->GetItem(MainCode)) { return false; }                      // `if (!pMainItemRec) return 0`
	// M1 — сам материал родителя: «есть / нужно РОДИТЕЛЮ».
	const int32 Base = 1 + kMatRows + kSubRows * K;
	IconCodes[Base] = MainCode;
	{
		int32 Have = 0;
		FillMatRow(S.Rows[0], MainCode, MatQtyOf(*Parent, Slot), Have);
	}
	const int32 Lvl = CraftLevel();
	const bool bFixDark = CVarCraftFixSubRowDarken.GetValueOnGameThread() != 0;
	// M2..M4 — ПЕРВЫЕ ТРИ материала подрецепта (у всех подрецептов данных ≤ 3 материала, отчёт §3.2).
	for (int32 J = 1; J < kSubRows; ++J)
	{
		FAsdaCraftMatRowUi& U = S.Rows[J];
		const int32 Code = MatCodeOf(*Sub, J - 1);
		if (Code >= 0)
		{
			if (!IT->GetItem(Code)) { continue; }                            // записи нет — строка не трогается
			AsdaUi::SetShownDecor(U.Group, true);                             // `SetVisible(icon, 1)` (дети — внутри группы)
			if (K == 0) { SetGroupShown(U.Black, false); SetGroupShown(U.Over, false); }
			IconCodes[Base + J] = Code;
			int32 Have = 0;
			const int32 Need = MatQtyOf(*Sub, J - 1);
			FillMatRow(U, Code, Need, Have);
			if (K == 0)
			{
				const bool bLevelLow = ReqLevelOf(*Sub) > Lvl;                // `(char)sub+67 > sub_929CF7(g_pItemMainWnd)`
				if (SubOf(*Sub, J - 1) >= 0)
				{
					SetGroupShown(U.Plus, true);
					SetGroupShown(U.Black, Have < Need || bLevelLow);          // `0xFE40D6…0xFE414E`
				}
				else
				{
					SetGroupShown(U.Plus, false);
					// ★ Д-В3-1: эталон (`0xFE418D…0xFE4205`) — «не хватает И уровень достаточен»; исправлено — как у всех.
					SetGroupShown(U.Black, bFixDark ? (Have < Need || bLevelLow) : (Have < Need && !bLevelLow));
				}
			}
		}
		else
		{
			// Пустой материал: пустая текстура, тексты пусты; у под-окна 1 — over скрыт, black показан; строка скрыта.
			IconCodes[Base + J] = 0;
			ClearMatRow(U);
			if (K == 0) { SetGroupShown(U.Over, false); SetGroupShown(U.Black, true); }
			AsdaUi::SetShownDecor(U.Group, false);
		}
	}
	if (IconGrid) { IconGrid->Refresh(); }
	return true;
}

bool UAsdaCraftRecipeWindowSubsystem::SetSlotItem(int32 K, int32 InSubId)
{
	// `uiCraftWndSetSlotItem@0xFE23B0` (ui_core.c:101208-101228).
	if (K < 0 || K >= kSubWins || !bControlsReady) { return false; }
	if (K == 0)
	{
		SetSubOver(-1);                                                       // `sub0_pSelHi` → SetVisible(0)
		for (UWidget* P : SubFrame) { AsdaUi::SetShownDecor(P, false); }      // рамка 77153
	}
	if (SubId[K] == InSubId) { return false; }                                // тот же подрецепт — ничего
	// ★ Ревизия В3 МЕЛК-8: эталон ПИШЕТ запись `[92+39k] = findRecordById16(id)` и при NULL выходит, не меняя `[91+39k]`
	//   (ui_core.c:101217-101219): запись под-окна становится пустой — кнопка 77151/77251 даёт 5570. Порт держит то же
	//   признаком `bSubRecNull` (запись = NULL, id прежний). В данных ссылок на отсутствующий подрецепт нет (260 ссылок,
	//   все в таблице — `craft_recipes_decoded.csv`), ветка латентна.
	if (!RecipeRow(InSubId)) { bSubRecNull[K] = true; return false; }         // `findRecordById16` = NULL
	bSubRecNull[K] = false;
	SubId[K] = InSubId;                                                       // [91+39k], запись [92+39k]
	RefreshSlotUI(K);
	if (K == 0) { SubOver = -1; }                                             // [93+39k] = 0
	SubSel[K] = -1;                                                           // [90+39k] = −1
	SubOpen[K] = true;
	AsdaUi::SetShownDecor(Subs[K].Root, true);                                // `SetVisible(aSubWin[39k], 1)`
	// `CClass_MapInsert(g_pWindowMgr, aSubWin[0])` — ВСЕГДА под-окно 1 и при КАЖДОМ открытии (ревизия МЕЛК-2, 1:1; ревизия
	//   В3 МЕЛК-9: прежний порт поднимал один раз): поднять его над под-окном 2 (важно, если под-окна перетащены внахлёст).
	if (Subs[0].Root)
	{
		if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Subs[0].Root->Slot))
		{
			if (UCanvasPanelSlot* CS2 = Subs.IsValidIndex(1) && Subs[1].Root ? Cast<UCanvasPanelSlot>(Subs[1].Root->Slot) : nullptr)
			{
				CS->SetZOrder(FMath::Max(CS->GetZOrder(), CS2->GetZOrder() + 1));
			}
			else { CS->SetZOrder(FMath::Max(CS->GetZOrder(), 1)); }
			bSubRaised = true;
		}
	}
	JournalAdd(FString::Printf(TEXT("под-окно %d открыто: подрецепт %d"), K + 1, InSubId));
	return true;
}

void UAsdaCraftRecipeWindowSubsystem::ClearSlot(int32 K)
{
	// `uiItemWndClearSlot@0xFE2550` (ui_core.c:101232-101246).
	if (K < 0 || K >= kSubWins) { return; }
	if (K == 0)
	{
		SetSubOver(-1);                                                       // [93] → SetVisible(0)
		for (UWidget* P : SubFrame) { AsdaUi::SetShownDecor(P, false); }      // [62] рамка 77153
	}
	SubSel[K] = -1;                                                           // [90+39k] = −1
	if (Subs.IsValidIndex(K)) { AsdaUi::SetShownDecor(Subs[K].Root, false); } // `SetVisible([59+39k], 0)`
	SubOpen[K] = false;
	SubId[K] = -1;                                                            // [92+39k] = 0, [91+39k] = −1
	bSubRecNull[K] = false;
}

void UAsdaCraftRecipeWindowSubsystem::HideTooltipControlReset()
{
	// `uiHideTooltipControlReset@0xFE22B0`.
	SetMainOver(-1);
	AsdaUi::SetShownDecor(MainFrame, false);
	MainSel = -1;
}

void UAsdaCraftRecipeWindowSubsystem::CraftSlotHideAndReset()
{
	// `uiCraftSlotHideAndReset@0xFE2320`.
	SetSubOver(-1);
	for (UWidget* P : SubFrame) { AsdaUi::SetShownDecor(P, false); }
	SubSel[0] = -1;
}

void UAsdaCraftRecipeWindowSubsystem::PlaceMainFrame(int32 Row)
{
	// `CControl_SetField74_75([5], 64, 40*[1] + 7)` — координаты родителя в `.JUI`; в ассете смещение «база − Origin JUI» = 0.
	if (!MainFrame) { return; }
	const FVector2D Pos(MainFrameBase.X + (kMainFrameCodeX - kMainFrameJuiX), MainFrameBase.Y + (kMainFrameCodeY0 + kMainFrameStep * Row - kMainFrameJuiY));
	AsdaUi::SetSlotPos(MainFrame, Pos);
}

void UAsdaCraftRecipeWindowSubsystem::PlaceSubFrame(int32 Idx)
{
	// `CControl_SetField74_75([62], 40, 34*[90] + 48)` — рамка 77153; каждый из 8 планов сдвигается на (код − Origin JUI).
	const FVector2D D(kSubFrameCodeX - kSubFrameJuiX, kSubFrameCodeY0 + kSubFrameStep * Idx - kSubFrameJuiY);
	for (int32 P = 0; P < SubFrame.Num(); ++P)
	{
		if (SubFrame[P]) { AsdaUi::SetSlotPos(SubFrame[P], SubFrameBase[P] + D); }
	}
}

void UAsdaCraftRecipeWindowSubsystem::SetMainOver(int32 Row)
{
	if (Row == MainOver) { return; }
	if (MatRows.IsValidIndex(MainOver)) { SetGroupShown(MatRows[MainOver].Over, false); }
	MainOver = Row;
	if (MatRows.IsValidIndex(Row)) { SetGroupShown(MatRows[Row].Over, true); }
}

void UAsdaCraftRecipeWindowSubsystem::SetSubOver(int32 Idx)
{
	if (Idx == SubOver) { return; }
	if (Subs.IsValidIndex(0) && Subs[0].Rows.IsValidIndex(SubOver)) { SetGroupShown(Subs[0].Rows[SubOver].Over, false); }
	SubOver = Idx;
	if (Subs.IsValidIndex(0) && Subs[0].Rows.IsValidIndex(Idx)) { SetGroupShown(Subs[0].Rows[Idx].Over, true); }
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

void UAsdaCraftRecipeWindowSubsystem::OnMainRowClicked(int32 Row)
{
	// msg 0xB по 77010…77066 (ui_core.c:101370-101392).
	if (Row < 0 || Row >= kMatRows || !MatPresent[Row]) { return; }           // `idx >= 6` / `![50+idx]` → 0
	const FAsdaMakingRecipeRow* R = RecipeRow(RecipeId);
	if (!R) { return; }
	MainSel = Row;                                                            // [1]
	PlaceMainFrame(Row);
	AsdaUi::SetShownDecor(MainFrame, true);                                   // 77154
	const int32 Sub = SubOf(*R, Row);                                         // `*(i16*)([137] + 2*idx + 112)`
	ClearSlot(1);
	if (Sub < 0) { ClearSlot(0); }
	else { SetSlotItem(0, Sub); }
	JournalAdd(FString::Printf(TEXT("клик 0xB строка состава %d (материал %d, подрецепт %d)"), Row, MatCodeOf(*R, Row), Sub));
}

void UAsdaCraftRecipeWindowSubsystem::OnSubRowClicked(int32 Idx)
{
	// msg 0xB по 77120…77150 под-окна 1 (ui_core.c:101393-101410). Под-окно 2 кликов строк не обрабатывает.
	SetSubOver(-1);                                                           // `if ([93]) SetVisible(0)` — ДО проверки индекса
	if (Idx < 1 || Idx >= kSubRows) { return; }
	const FAsdaMakingRecipeRow* Sub0 = SubRecord(0);
	if (!Sub0) { return; }
	SubSel[0] = Idx;                                                          // [90]
	PlaceSubFrame(Idx);
	for (UWidget* P : SubFrame) { AsdaUi::SetShownDecor(P, true); }           // 77153
	const int32 Sub2 = SubOf(*Sub0, Idx - 1);                                 // `*(i16*)([92] + 2*idx + 110)`
	if (Sub2 < 0) { ClearSlot(1); }
	else { SetSlotItem(1, Sub2); }
	JournalAdd(FString::Printf(TEXT("клик 0xB строка M%d под-окна 1 (подрецепт второго уровня %d)"), Idx + 1, Sub2));
}

void UAsdaCraftRecipeWindowSubsystem::OnMakeClicked()
{
	// 0x12D0C = 77068 (ui_core.c:101277-101299): материалы → уровень → окно 77500.
	const FAsdaMakingRecipeRow* R = RecipeRow(RecipeId);
	UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	if (!R || !Craft) { return; }                                             // `if ([137])`
	if (!Craft->CheckRequiredItems(*R)) { Craft->NoticeMsgBox(kLocLackMat); JournalAdd(TEXT("77068: нет материалов → 5569")); return; }
	if (ReqLevelOf(*R) > CraftLevel())  { Craft->NoticeMsgBox(kLocLowLevel); JournalAdd(TEXT("77068: уровень мал → 5584")); return; }
	OpenMakeWindow(R->Id, TEXT("77068"));
}

void UAsdaCraftRecipeWindowSubsystem::OnSubMakeClicked(int32 K)
{
	// 0x12D5F = 77151 (запись [92]) / 0x12DC3 = 77251 (запись [131]) — одна логика (ui_core.c:101300-101350):
	//   записи нет → 5570; нет материалов или уровень мал → 5584, если уровень мал, иначе 5569; не изучен → 5585; иначе 77500.
	if (K < 0 || K >= kSubWins) { return; }
	UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	if (!Craft) { return; }
	const FAsdaMakingRecipeRow* Sub = SubRecord(K);
	const int32 BtnId = 77151 + 100 * K;
	if (!Sub || !Craft->CheckRequiredItems(*Sub) || ReqLevelOf(*Sub) > CraftLevel())
	{
		if (!Sub)                                { Craft->NoticeMsgBox(kLocNoList);   JournalAdd(FString::Printf(TEXT("%d: записи нет → 5570"), BtnId)); }
		else if (ReqLevelOf(*Sub) <= CraftLevel()) { Craft->NoticeMsgBox(kLocLackMat);  JournalAdd(FString::Printf(TEXT("%d: нет материалов → 5569"), BtnId)); }
		else                                     { Craft->NoticeMsgBox(kLocLowLevel); JournalAdd(FString::Printf(TEXT("%d: уровень мал → 5584"), BtnId)); }
		return;
	}
	if (!Craft->IsLearned(Sub->Id))                                          // `arrayContainsValue(g_pItemMainWnd, id)`
	{
		Craft->NoticeMsgBox(kLocNoRecipe);
		JournalAdd(FString::Printf(TEXT("%d: подрецепт %d не изучен → 5585"), BtnId, Sub->Id));
		return;
	}
	OpenMakeWindow(Sub->Id, *FString::Printf(TEXT("%d"), BtnId));
}

void UAsdaCraftRecipeWindowSubsystem::OnSubCloseClicked(int32 K)
{
	if (K == 0)
	{
		// 0x12D60 / 0x12D67 = 77152 / 77159: `uiItemWndClearSlot(this, 0)` + `uiHideTooltipControlReset`.
		ClearSlot(0);
		HideTooltipControlReset();
		// ★ Д-В3-2: под-окно 2 без родителя не оставляем (см. ключ).
		if (CVarCraftFixOrphanSubWindow.GetValueOnGameThread() != 0 && SubOpen[1]) { ClearSlot(1); }
		JournalAdd(TEXT("под-окно 1 закрыто, выбор строки состава снят"));
	}
	else if (K == 1)
	{
		// 0x12DC4 / 0x12DCB = 77252 / 77259: `uiItemWndClearSlot(this, 1)` + `uiCraftSlotHideAndReset`.
		ClearSlot(1);
		CraftSlotHideAndReset();
		JournalAdd(TEXT("под-окно 2 закрыто, выбор строки под-окна 1 снят"));
	}
}

void UAsdaCraftRecipeWindowSubsystem::OnCloseClicked()
{
	// 0x12D0B / 0x12D0D = 77067 / 77069 → `itemTooltipWndReset(this)`.
	ResetWindow(TEXT("кнопка закрытия 77067/77069"));
}

void UAsdaCraftRecipeWindowSubsystem::OpenMakeWindow(int32 ForRecipeId, const TCHAR* Source)
{
	// `uiItemUpgradeWindowOpen@0xFE4D70` (миснейм) — окно изготовления 77500 (волна В4, `UAsdaCraftMakeWindowSubsystem`).
	//   Проверки кнопки уже пройдены эталонно. Защёлку снимка (`SetHoldSnapshotForMake`) взводит само окно 77500 на время
	//   своего «закрыть всё» (сброс 76000 → сброс этого окна не должен снимать снимок — его вернёт сброс 77500).
	UAsdaCraftMakeWindowSubsystem* Make = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr;
	const bool bOpened = Make && Make->OpenForRecipe(ForRecipeId, Source);
	UE_LOG(LogAsdaCraftRecipe, Log, TEXT("[ремесло-состав] %s: рецепт %d прошёл проверки — окно изготовления 77500 %s"),
		Source, ForRecipeId, bOpened ? TEXT("открыто") : TEXT("НЕ открыто (нет записи rate / уже открыто — см. лог 77500)"));
	JournalAdd(FString::Printf(TEXT("77500 %s — рецепт %d (%s)"), bOpened ? TEXT("открыто") : TEXT("не открыто"), ForRecipeId, Source));
}

// ═════════════════════════════ МЫШЬ ═══════════════════════════════════════════════════════════

void UAsdaCraftRecipeWindowSubsystem::TickMouse()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM && (WM->IsPointerBlockedByTopmost(kWindowId) || WM->IsCursorOverHigherWindow(kWindowId)))
	{
		SetMainOver(-1);
		SetSubOver(-1);
		Mouse.Reset();
		return;
	}
	// Зона строки = иконка + подписи t1..t3 (+ black/over/plus, когда видны: это тоже контролы диапазона строки,
	//   и в JUI они — верхние в строке, поэтому видимый over/black ловит мышь на всю ширину строки).
	auto RowHit = [](const FAsdaCraftMatRowUi& U) -> bool
	{
		for (const TObjectPtr<UWidget>& W : U.Hit)   { if (CursorOver(W)) { return true; } }
		for (const TObjectPtr<UWidget>& W : U.Black) { if (CursorOver(W)) { return true; } }
		for (const TObjectPtr<UWidget>& W : U.Over)  { if (CursorOver(W)) { return true; } }
		for (const TObjectPtr<UWidget>& W : U.Plus)  { if (CursorOver(W)) { return true; } }
		return false;
	};
	int32 MainRow = -1, SubRow = -1;
	// Под-окно 1 лежит поверх окна (правее рамки) — проверяется первым.
	if (SubOpen[0] && Subs.IsValidIndex(0))
	{
		for (int32 J = 0; J < kSubRows && SubRow < 0; ++J) { if (RowHit(Subs[0].Rows[J])) { SubRow = J; } }
	}
	if (SubRow < 0)
	{
		for (int32 I = 0; I < MatRows.Num() && MainRow < 0; ++I) { if (RowHit(MatRows[I])) { MainRow = I; } }
	}
	// msg 9 / 0xA: over строки 77000 — только у строки с материалом (`[50+idx]`); у под-окна 1 — строки M2..M4.
	SetMainOver((MainRow >= 0 && MatPresent[MainRow]) ? MainRow : -1);
	SetSubOver((SubRow >= 1) ? SubRow : -1);
	if (Mouse.bLmbDown)
	{
		if (MainRow >= 0) { OnMainRowClicked(MainRow); }
		else if (SubRow >= 0) { OnSubRowClicked(SubRow); }
	}
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaCraftRecipeWindowSubsystem::Loc(int32 Id) const
{
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

const FAsdaMakingRecipeRow* UAsdaCraftRecipeWindowSubsystem::RecipeRow(int32 Id) const
{
	if (Id < 0) { return nullptr; }
	const UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	return Craft ? Craft->FindRecipe(Id) : nullptr;
}

const FAsdaMakingRecipeRow* UAsdaCraftRecipeWindowSubsystem::SubRecord(int32 K) const
{
	// Запись под-окна `[92+39k]`: пустая, если последнее открытие не нашло подрецепт (МЕЛК-8), иначе — по id `[91+39k]`.
	if (K < 0 || K >= kSubWins || bSubRecNull[K]) { return nullptr; }
	return RecipeRow(SubId[K]);
}

int32 UAsdaCraftRecipeWindowSubsystem::CraftLevel() const
{
	const UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	return Craft ? Craft->GetCraftLevel() : 0;
}

void UAsdaCraftRecipeWindowSubsystem::JournalAdd(const FString& Line)
{
	Journal.Add(Line);
	UE_LOG(LogAsdaCraftRecipe, Log, TEXT("[ремесло-состав] %s"), *Line);
}

FString UAsdaCraftRecipeWindowSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно состава рецепта 77000 (+ под-окна 77100/77200): разобранные контролы ===\n");
	auto Line = [&S](const FString& Id, bool bOk) { S += FString::Printf(TEXT("  %-52s %s\n"), *Id, bOk ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("77000 ITEM_making_R_BOX (корень)"), Widget != nullptr);
	Line(TEXT("77001 B_BOX_textmove «Ингредиенты» + ручка _DRAG"), TxtTitle != nullptr && DragHandle != nullptr);
	Line(TEXT("77002 B_BOX_t1 (имя рецепта)"), TxtName != nullptr);
	Line(TEXT("77003 B_BOX_Smark (иконка результата, план + подпись)"), ResultIcon != nullptr && ResultText != nullptr);
	for (int32 I = 0; I < MatRows.Num(); ++I)
	{
		const FAsdaCraftMatRowUi& U = MatRows[I];
		Line(FString::Printf(TEXT("%d D%d_mark (иконка + подпись)"), 77010 + 10 * I, I + 1), U.Icon != nullptr && U.IconText != nullptr);
		Line(FString::Printf(TEXT("%d D%d_t1 (имя)"), 77011 + 10 * I, I + 1), U.Name != nullptr);
		Line(FString::Printf(TEXT("%d D%d_t2 (5550)"), 77012 + 10 * I, I + 1), U.Sub != nullptr);
		Line(FString::Printf(TEXT("%d D%d_t3 («есть / нужно»)"), 77013 + 10 * I, I + 1), U.Num != nullptr);
		Line(FString::Printf(TEXT("%d D%d_black (узлов %d)"), 77014 + 10 * I, I + 1, U.Black.Num()), U.Black.Num() > 0);
		Line(FString::Printf(TEXT("%d D%d_over (узлов %d)"), 77015 + 10 * I, I + 1, U.Over.Num()), U.Over.Num() > 0);
		Line(FString::Printf(TEXT("%d D%d_plus (узлов %d)"), 77016 + 10 * I, I + 1, U.Plus.Num()), U.Plus.Num() > 0);
		Line(FString::Printf(TEXT("       зона строки D%d (иконка + HIT t1..t3: узлов %d)"), I + 1, U.Hit.Num()), U.Hit.Num() == 4);
	}
	Line(TEXT("77154 B_select (рамка выбора, группа)"), MainFrame != nullptr);
	Line(TEXT("77067 B_BOX_Xbtn «Закрыть»"), BtnCancel != nullptr && TxtCancel != nullptr);
	Line(TEXT("77068 Mark_ok «Создание предмета»"), BtnMake != nullptr && TxtMake != nullptr);
	Line(TEXT("77069 B_BOX_XX"), BtnExit != nullptr);
	for (int32 K = 0; K < Subs.Num(); ++K)
	{
		const FAsdaCraftSubWinUi& Sw = Subs[K];
		const int32 B = 77100 + 100 * K;
		Line(FString::Printf(TEXT("%d B_tooltip_%d (корень под-окна)"), B, K + 1), Sw.Root != nullptr);
		Line(FString::Printf(TEXT("%d top1 (%d) + ручка _DRAG"), B + 1, 5531 + K), Sw.Title != nullptr && Sw.Drag != nullptr);
		Line(FString::Printf(TEXT("%d top2 (имя рецепта-родителя)"), B + 2), Sw.Parent != nullptr);
		Line(FString::Printf(TEXT("%d XX"), B + 59), Sw.BtnXX != nullptr);
		Line(FString::Printf(TEXT("%d okbtn (5521)"), B + 51), Sw.BtnOk != nullptr && Sw.TxtOk != nullptr);
		Line(FString::Printf(TEXT("%d xbtn (5035)"), B + 52), Sw.BtnX != nullptr && Sw.TxtX != nullptr);
		for (int32 J = 0; J < Sw.Rows.Num(); ++J)
		{
			const FAsdaCraftMatRowUi& U = Sw.Rows[J];
			const int32 R = B + 10 + 10 * J;
			Line(FString::Printf(TEXT("%d M%d (иконка + подпись) / %d s1 / %d s2 / %d s3"), R, J + 1, R + 1, R + 2, R + 3),
				U.Icon != nullptr && U.IconText != nullptr && U.Name != nullptr && U.Sub != nullptr && U.Num != nullptr);
			if (K == 0 && J > 0)
			{
				Line(FString::Printf(TEXT("%d/%d/%d M%d black/over/plus"), R + 4, R + 5, R + 6, J + 1), U.Black.Num() > 0 && U.Over.Num() > 0 && U.Plus.Num() > 0);
			}
			Line(FString::Printf(TEXT("       группа строки M%d под-окна %d (зона: узлов %d)"), J + 1, K + 1, U.Hit.Num()), U.Group != nullptr && U.Hit.Num() == 4);
		}
	}
	{
		int32 N = 0; for (const UWidget* P : SubFrame) { if (P) { ++N; } }
		Line(FString::Printf(TEXT("77153 B_tooltip_1_select (рамка, планов %d/8)"), N), N == 8);
	}
	S += FString::Printf(TEXT("  ИТОГ: найдено %d/%d контролов с IDNum групп 770/771/772\n"), FoundControls, kControlsTotal);
	return S;
}

FString UAsdaCraftRecipeWindowSubsystem::DumpState() const
{
	FString S = FString::Printf(TEXT("[ремесло-состав] открыто=%d рецепт=%d строка=%d over=%d · под-окно1: %d подрецепт=%d строка=%d over=%d · под-окно2: %d подрецепт=%d\n"),
		bOpen ? 1 : 0, RecipeId, MainSel, MainOver, SubOpen[0] ? 1 : 0, SubId[0], SubSel[0], SubOver, SubOpen[1] ? 1 : 0, SubId[1]);
	for (int32 I = 0; I < MatRows.Num(); ++I)
	{
		const FAsdaCraftMatRowUi& U = MatRows[I];
		S += FString::Printf(TEXT("  D%d: код %d · «%s» «%s» · black=%d plus=%d\n"), I + 1, IconCodes.IsValidIndex(1 + I) ? IconCodes[1 + I] : -1,
			U.Name ? *U.Name->GetText().ToString() : TEXT("-"), U.Num ? *U.Num->GetText().ToString() : TEXT("-"), GroupShown(U.Black) ? 1 : 0, GroupShown(U.Plus) ? 1 : 0);
	}
	return S;
}

FString UAsdaCraftRecipeWindowSubsystem::DebugText(int32 IdNum) const
{
	auto T = [](const UTextBlock* B) { return B ? B->GetText().ToString() : FString(TEXT("<нет>")); };
	switch (IdNum)
	{
	case 77001: return T(TxtTitle);
	case 77002: return T(TxtName);
	case 77067: return T(TxtCancel);
	case 77068: return T(TxtMake);
	default: break;
	}
	if (IdNum >= 77010 && IdNum <= 77066)
	{
		const int32 R = (IdNum - 77010) / 10, Kd = (IdNum - 77010) % 10;
		if (MatRows.IsValidIndex(R)) { const FAsdaCraftMatRowUi& U = MatRows[R]; if (Kd == 1) { return T(U.Name); } if (Kd == 2) { return T(U.Sub); } if (Kd == 3) { return T(U.Num); } }
	}
	for (int32 K = 0; K < Subs.Num(); ++K)
	{
		const int32 B = 77100 + 100 * K;
		const FAsdaCraftSubWinUi& Sw = Subs[K];
		if (IdNum == B + 1) { return T(Sw.Title); }
		if (IdNum == B + 2) { return T(Sw.Parent); }
		if (IdNum == B + 51) { return T(Sw.TxtOk); }
		if (IdNum == B + 52) { return T(Sw.TxtX); }
		if (IdNum >= B + 10 && IdNum <= B + 43)
		{
			const int32 J = (IdNum - B - 10) / 10, Kd = (IdNum - B - 10) % 10;
			if (Sw.Rows.IsValidIndex(J)) { const FAsdaCraftMatRowUi& U = Sw.Rows[J]; if (Kd == 1) { return T(U.Name); } if (Kd == 2) { return T(U.Sub); } if (Kd == 3) { return T(U.Num); } }
		}
	}
	return FString();
}

bool UAsdaCraftRecipeWindowSubsystem::DebugShown(int32 IdNum) const
{
	if (IdNum == 77154) { return IsShown(MainFrame); }
	if (IdNum == 77153) { return SubFrame.Num() > 0 && IsShown(SubFrame[0]); }
	if (IdNum == 77100 || IdNum == 77200) { const int32 K = (IdNum - 77100) / 100; return Subs.IsValidIndex(K) && IsShown(Subs[K].Root); }
	if (IdNum >= 77010 && IdNum <= 77066)
	{
		const int32 R = (IdNum - 77010) / 10, Kd = (IdNum - 77010) % 10;
		if (!MatRows.IsValidIndex(R)) { return false; }
		if (Kd == 4) { return GroupShown(MatRows[R].Black); }
		if (Kd == 5) { return GroupShown(MatRows[R].Over); }
		if (Kd == 6) { return GroupShown(MatRows[R].Plus); }
	}
	if (IdNum >= 77110 && IdNum <= 77146 && Subs.IsValidIndex(0))
	{
		const int32 J = (IdNum - 77110) / 10, Kd = (IdNum - 77110) % 10;
		if (!Subs[0].Rows.IsValidIndex(J)) { return false; }
		const FAsdaCraftMatRowUi& U = Subs[0].Rows[J];
		if (Kd == 0) { return IsShown(U.Group); }
		if (Kd == 4) { return GroupShown(U.Black); }
		if (Kd == 5) { return GroupShown(U.Over); }
		if (Kd == 6) { return GroupShown(U.Plus); }
	}
	if (IdNum >= 77210 && IdNum <= 77240 && Subs.IsValidIndex(1))
	{
		const int32 J = (IdNum - 77210) / 10;
		return Subs[1].Rows.IsValidIndex(J) && IsShown(Subs[1].Rows[J].Group);
	}
	return false;
}

FVector2D UAsdaCraftRecipeWindowSubsystem::DebugSlotPos(int32 IdNum) const
{
	FVector2D P = FVector2D::ZeroVector, S;
	if (IdNum == 77154 && MainFrame) { AsdaUi::SlotRect(MainFrame, P, S); }
	if (IdNum == 77153 && SubFrame.Num() > 0 && SubFrame[0]) { AsdaUi::SlotRect(SubFrame[0], P, S); }
	return P;
}

int32 UAsdaCraftRecipeWindowSubsystem::DebugIconCode(int32 IdNum) const
{
	int32 C = -1;
	if (IdNum == 77003) { C = 0; }
	else if (IdNum >= 77010 && IdNum <= 77060 && (IdNum - 77010) % 10 == 0) { C = 1 + (IdNum - 77010) / 10; }
	else if (IdNum >= 77110 && IdNum <= 77140 && (IdNum - 77110) % 10 == 0) { C = 1 + kMatRows + (IdNum - 77110) / 10; }
	else if (IdNum >= 77210 && IdNum <= 77240 && (IdNum - 77210) % 10 == 0) { C = 1 + kMatRows + kSubRows + (IdNum - 77210) / 10; }
	return IconCodes.IsValidIndex(C) ? IconCodes[C] : -1;
}

bool UAsdaCraftRecipeWindowSubsystem::DebugPressButton(int32 IdNum)
{
	UButton* Btn = nullptr;
	switch (IdNum)
	{
	case 77067: Btn = BtnCancel; break;
	case 77068: Btn = BtnMake; break;
	case 77069: Btn = BtnExit; break;
	default:
		for (int32 K = 0; K < Subs.Num(); ++K)
		{
			if (IdNum == 77151 + 100 * K) { Btn = Subs[K].BtnOk; }
			if (IdNum == 77152 + 100 * K) { Btn = Subs[K].BtnX; }
			if (IdNum == 77159 + 100 * K) { Btn = Subs[K].BtnXX; }
		}
		break;
	}
	if (!Btn) { return false; }
	Btn->OnClicked.Broadcast();   // тот же путь, что клик игрока: делегат кнопки → реле → обработчик
	return true;
}

bool UAsdaCraftRecipeWindowSubsystem::DebugRowCenter(int32 Kind, int32 Row, FVector2D& Out) const
{
	const FAsdaCraftMatRowUi* U = nullptr;
	if (Kind == 0 && MatRows.IsValidIndex(Row)) { U = &MatRows[Row]; }
	if (Kind == 1 && Subs.IsValidIndex(0) && Subs[0].Rows.IsValidIndex(Row)) { U = &Subs[0].Rows[Row]; }
	if (!U) { return false; }
	// Центр подписи имени (t1 / s1 — её HIT-план): самый широкий узел строки.
	for (const TObjectPtr<UWidget>& W : U->Hit)
	{
		FVector2D Mn, Mx;
		if (W && (W->GetName().Contains(TEXT("_t1_")) || W->GetName().Contains(TEXT("_s1_"))) && AsdaUi::WidgetAbsRect(W, Mn, Mx)) { Out = (Mn + Mx) * 0.5f; return true; }
	}
	return false;
}

// ═════════════════════════════ САМОТЕСТ ═══════════════════════════════════════════════════════

namespace
{
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftRecipeState(
		TEXT("asda.Craft.RecipeState"),
		TEXT("asda.Craft.RecipeState — контролы и состояние окна состава 77000 и под-окон, снимок открытых окон."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaCraftRecipeWindowSubsystem* Rw = GI ? GI->GetSubsystem<UAsdaCraftRecipeWindowSubsystem>() : nullptr;
			UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			if (!Rw || !WM) { return; }
			UE_LOG(LogAsdaCraftRecipe, Log, TEXT("%s%s%s"), *Rw->DumpControls(), *Rw->DumpState(), *WM->DumpWindowSnapshot());
		}));
#endif // ASDA_DEV_CONSOLE

	// ★ САМОТЕСТ ПУТЁМ ИГРОКА (-game), волна В3. Сервера нет: 6597 синтезируется и идёт БОЕВЫМ приёмником, материалы
	//   кладутся в сумку отладочной записью, клики по строкам — настоящим кликом Slate по живой геометрии, кнопки —
	//   делегатом `OnClicked` (путь игрока). `delay=N` — сдвиг шагов, `shot` — кадры (префикс `w3_`), `quit` — выход.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftRecipeSelfTest(
		TEXT("asda.Craft.SelfTestRecipe"),
		TEXT("Окно 77000 и под-окна 77100/77200: разбор 104 контролов, состав, «есть/нужно» и выбор стека (К-6), рамки 77154/77153, "
		     "под-окна двух уровней, нотисы 5569/5570/5584/5585, закрытия, снимок открытых окон (К-7), гейты «ремесло открыто». "
		     "`delay=N` `shot` `quit`."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaCraftRecipeWindowSubsystem* Rw = GI ? GI->GetSubsystem<UAsdaCraftRecipeWindowSubsystem>() : nullptr;
			UAsdaCraftWindowSubsystem* Win = GI ? GI->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr;
			UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
			UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			if (!Rw || !Win || !Craft || !Inv || !WM) { return; }
			bool bQuit = false, bShot = false;
			float Delay = 0.f;
			FString ShotPrefix(TEXT("w3_"));
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
				if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
				if (A.StartsWith(TEXT("prefix="))) { ShotPrefix = A.Mid(7); }
			}
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix-b 13.11.2026 (третий заход): `delay=N` — общая точка (была ветка цикла аргументов)
			// ★ Волна В8 (ревизия В7 С-3): только офлайн — при живом соединении отказ, иначе окно защиты отправителей.
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Craft.SelfTestRecipe"), KitArgs)) { return; }
			struct FStat { int32 Ok = 0; int32 Fail = 0; };
			TSharedRef<FStat> Stat = MakeShared<FStat>();
			TWeakObjectPtr<UWorld> WGate(W);   // мир прогона (для отказа на отложенном старте)
			// ★ Р-2 (10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим таймером;
			//   при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
			// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Craft.SelfTestRecipe"), bQuit);   // общий флаг отказа всех шагов прогона
			auto GateRefused = [Gate, WGate]() { return !Gate->Pass(WGate.Get()); };   // true — прогон отменён; проверка один раз, первым сработавшим шагом
			auto At = [W, Rw, Delay, GateRefused](float T, TFunction<void()> Fn)
			{
				FTimerHandle H;
				W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Rw, [GateRefused, Fn = MoveTemp(Fn)]()
				{
					if (GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
					Fn();
				}), T + Delay, false);
			};
			// ★ Ревизия В3 (мелкое): префикс имён кадров — параметр `prefix=` (умолчание `w3_`), чтобы ревизор не перетирал кадры волны.
			auto Shot = [bShot, ShotPrefix](const TCHAR* Name)
			{
				if (!bShot) { return; }
				FString N(Name);
				if (N.StartsWith(TEXT("w3_"))) { N = ShotPrefix + N.Mid(3); }
				UE_LOG(LogAsdaCraftRecipe, Warning, TEXT("САМОТЕСТ-77000: КАДР → %s.png"), *N);
				FScreenshotRequest::RequestScreenshot(N, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			};
			auto Check = [Stat](bool bOk, const FString& What)
			{
				if (bOk) { ++Stat->Ok; } else { ++Stat->Fail; }
				UE_LOG(LogAsdaCraftRecipe, Warning, TEXT("САМОТЕСТ-77000 %s %s"), bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
			};
			auto Dump = [Rw, Craft, WM](const TCHAR* Step)
			{
				UE_LOG(LogAsdaCraftRecipe, Warning, TEXT("САМОТЕСТ-77000: %s\n%s%s"), Step, *Rw->DumpState(), *WM->DumpWindowSnapshot());
				for (const FString& L : Rw->GetJournal()) { UE_LOG(LogAsdaCraftRecipe, Warning, TEXT("  журнал 77000: %s"), *L); }
				for (const FString& L : Craft->GetNoticeJournal()) { UE_LOG(LogAsdaCraftRecipe, Warning, TEXT("  журнал нотисов: %s"), *L); }
				Rw->ClearJournal(); Craft->ClearNoticeJournal();
			};
			auto NoticeHas = [Craft](const TCHAR* What) { for (const FString& L : Craft->GetNoticeJournal()) { if (L.Contains(What)) { return true; } } return false; };
			auto JournalHas = [Rw](const TCHAR* What) { for (const FString& L : Rw->GetJournal()) { if (L.Contains(What)) { return true; } } return false; };
			auto SetCVar = [](const TCHAR* Name, int32 V) { AsdaCVarSave::SetForTest(Name, V); };   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходное запоминается
			auto LocS = [GI](int32 Id) { return AsdaNotice::Loc(GI, Id); };
			// Материал в сумку — в контейнер ЕГО класса (как считает `CInventory_CountItemById`): снаряжение 1 (ячейки
			//   20…29 теста), прочее 2 (ячейки 40…59 теста). Cell — номер ячейки теста 0…9.
			auto Put = [GI, Inv](int32 Cell, int32 Code, int32 Qty)
			{
				const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
				const bool bEquip = IT && Code > 0 && IT->IsWeaponOrEquipClass(Code);
				Inv->DebugPutItem(bEquip ? 1 : 2, (bEquip ? 20 : 40) + Cell, Code, Qty);
			};
			auto ItemName = [GI](int32 Code) { const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>(); return IT ? IT->GetItemName(Code) : FString(); };
			auto ClickRow = [Rw, WM, Check](int32 Kind, int32 Row, const TCHAR* What)
			{
				FVector2D C;
				const bool bOk = Rw->DebugRowCenter(Kind, Row, C);
				Check(bOk, FString::Printf(TEXT("центр строки (%s) по живой геометрии"), What));
				if (bOk) { WM->SimulateClickAt(C, 3); }
			};
			// Цепочка данных (проверено по `craft_recipes_decoded.csv`): 76 «одноручный меч Зейлы» (ур. 4) = 72731×1 (подрецепт
			//   214) + 20957×4; 214 «форма д/создания оружия ур. C» (ур. 2) = 72746×10 (подрецепт 218) + 20961×10 + 20962×10
			//   (подрецепт 1); 218 «комб. камень ур. C» (ур. 2) = 20914×5 + 20924×1 + 20918×1; 1 «абразив» = 20931×3.
			static const TArray<int32> kLearned = { 76, 1, 3 };   // 214 и 218 НЕ изучены — для 5585

			// ── Шаг 1: модель и сумка; сумку открыть — её должен снять снимок ──
			At(1.0f, [Craft, Inv, WM, Put, GI]()
			{
				// ★ Волна В4: окно 77500 существует — этот проход проверяет КНОПКИ 77000, поэтому переход в 77500 здесь идёт
				//   «без показа» (иначе его «закрыть всё» закрыло бы 77000 посреди шагов). Само окно — `asda.Craft.SelfTestMake`.
				if (UAsdaCraftMakeWindowSubsystem* Mk = GI->GetSubsystem<UAsdaCraftMakeWindowSubsystem>()) { Mk->DebugSetDryOpen(true); }
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 30, 5, 100, 10));
				for (int32 C = 40; C < 60; ++C) { Inv->DebugPutItem(2, C, 0, 0); }
				for (int32 C = 20; C < UAsdaInventorySubsystem::kShellSlots; ++C) { Inv->DebugPutItem(1, C, 0, 0); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
				Put(0, 20957, 2);    // К-6: два стека по 2 (нужно 4)
				Put(1, 20957, 2);
				Put(2, 20961, 10);   // M3 под-окна 1 — хватает
				Put(3, 20962, 10);   // M4 — хватает
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
			});
			// ── Шаг 2: 76000 → клик по строке рецепта 76 → окно 77000 ──
			At(1.4f, [Win, Check]()
			{
				Win->GateAndToggle(TEXT("самотест 77000"));
				Check(Win->IsOpen(), TEXT("окно 76000 открыто"));
			});
			// Клик — через кадр: живая геометрия строк появляется после первой раскладки открытого окна.
			At(1.6f, [Win, Rw, Inv, WM, Check]()
			{
				int32 Row76 = -1;
				for (int32 R = 0; R < UAsdaCraftWindowSubsystem::kRows; ++R) { if (Win->GetRowRecipe(R) == 76) { Row76 = R; } }
				Check(Row76 >= 0, FString::Printf(TEXT("рецепт 76 на странице 1 (строка %d)"), Row76));
				FVector2D C = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
				const bool bC = Row76 >= 0 && Win->DebugRowCenter(Row76, C);
				Check(bC, TEXT("центр строки 76000 по живой геометрии"));
				if (bC) { WM->SimulateClickAt(C, 3); }
				Check(Inv->IsWindowOpen(), TEXT("сумка открыта до окна состава (её снимет снимок)"));
			});
			At(1.9f, [Rw, Win, WM, Check, W]()
			{
				Check(Rw->IsOpen() && Rw->GetRecipeId() == 76 && Win->GetSelectedRecipe() == 76,
					FString::Printf(TEXT("клик Slate по строке 76000 открыл 77000: рецепт %d"), Rw->GetRecipeId()));
				UE_LOG(LogAsdaCraftRecipe, Warning, TEXT("%s"), *Rw->DumpControls());
				Check(Rw->GetFoundControls() == UAsdaCraftRecipeWindowSubsystem::kControlsTotal,
					FString::Printf(TEXT("разбор контролов 77000/77100/77200: %d/%d"), Rw->GetFoundControls(), UAsdaCraftRecipeWindowSubsystem::kControlsTotal));
				Check(WM->IsWindowSlotCaptured(0) && WM->GetWindowSlotOwners(0) == (uint8)UAsdaWindowSubsystem::EWindowSnapshotOwner::Craft,
					TEXT("снимок при открытии 77000: слот 0 (сумка) поднят владельцем «ремесло»"));
				Check(!WM->IsWindowSlotCaptured(5), TEXT("слот 5 (ремесло 76000) в снимок не пишется — как эталон"));
				Check(WM->GetWindowZOrder(FName(UAsdaCraftSubsystem::kRecipeWindowId)) >= UAsdaWindowSubsystem::kPriorityZ,
					FString::Printf(TEXT("77000 в верхнем слое (set_o229 → приоритетная полоса): z=%d"), WM->GetWindowZOrder(FName(UAsdaCraftSubsystem::kRecipeWindowId))));
				GEngine->Exec(W, TEXT("asda.UI.TreeGeom w3_recipe_open WBP_ITEM_making_R_BOX"));
			});
			At(2.1f, [Shot]() { Shot(TEXT("w3_recipe_open")); });
			At(2.3f, [Rw, LocS, ItemName, Check, SetCVar, Dump]()
			{
				Check(Rw->DebugText(77001) == LocS(5530), FString::Printf(TEXT("77001 = «%s» (loc 5530)"), *Rw->DebugText(77001)));
				Check(Rw->DebugText(77068) == LocS(5521) && Rw->DebugText(77067) == LocS(5035), TEXT("подписи 77068 = 5521, 77067 = 5035"));
				Check(!Rw->DebugText(77002).IsEmpty(), FString::Printf(TEXT("77002 имя рецепта = «%s»"), *Rw->DebugText(77002)));
				Check(Rw->DebugIconCode(77003) > 0 && Rw->DebugIconCode(77010) == 72731 && Rw->DebugIconCode(77020) == 20957,
					FString::Printf(TEXT("иконки: результат %d, D1 %d, D2 %d"), Rw->DebugIconCode(77003), Rw->DebugIconCode(77010), Rw->DebugIconCode(77020)));
				Check(Rw->DebugText(77011) == ItemName(72731) && Rw->DebugText(77012) == LocS(5550) && Rw->DebugText(77013) == TEXT("0 / 1"),
					FString::Printf(TEXT("D1: «%s» «%s» «%s»"), *Rw->DebugText(77011), *Rw->DebugText(77012), *Rw->DebugText(77013)));
				Check(Rw->DebugShown(77016) && Rw->DebugShown(77014), TEXT("D1: plus (подрецепт 214) и black (0 < 1) показаны"));
				Check(Rw->DebugText(77023) == TEXT("2 / 4") && Rw->DebugShown(77024) && !Rw->DebugShown(77026),
					FString::Printf(TEXT("К-6 (1): D2 20957 два стека по 2 — «%s», затемнено, без plus"), *Rw->DebugText(77023)));
				Check(Rw->DebugIconCode(77030) == 0 && Rw->DebugText(77033).IsEmpty() && Rw->DebugShown(77034) && !Rw->DebugShown(77036),
					TEXT("D3 пустой слот: пустая иконка/текст, black показан, plus скрыт"));
				// К-6 как оригинал: сумма стеков.
				SetCVar(TEXT("asda.Craft.FixStackPick"), 0);
				Rw->RefreshAll();
				Check(Rw->DebugText(77023) == TEXT("4 / 4") && !Rw->DebugShown(77024), FString::Printf(TEXT("К-6 (0): D2 сумма стеков «%s», не затемнено"), *Rw->DebugText(77023)));
				SetCVar(TEXT("asda.Craft.FixStackPick"), 1);
				Rw->RefreshAll();
				Dump(TEXT("после открытия 77000"));
			});
			// ── Шаг 3: выбор строки D1 настоящим кликом → рамка 77154 и под-окно 1 (подрецепт 214) ──
			At(2.6f, [ClickRow]() { ClickRow(0, 0, TEXT("D1 окна 77000")); });
			At(3.0f, [Rw, ItemName, Check, W]()
			{
				Check(Rw->GetSelRow() == 0 && Rw->DebugShown(77154), FString::Printf(TEXT("клик по D1: строка %d, рамка 77154 видна"), Rw->GetSelRow()));
				const FVector2D P = Rw->DebugSlotPos(77154);
				Check(FMath::IsNearlyEqual((float)P.X, 64.f) && FMath::IsNearlyEqual((float)P.Y, 7.f),
					FString::Printf(TEXT("рамка 77154 в канвасе (%.0f, %.0f) = база ассета (64,6) + код (64, 40·0+7) − JUI (64,6)"), P.X, P.Y));
				Check(Rw->IsSubOpen(0) && Rw->GetSubRecipeId(0) == 214 && Rw->DebugShown(77100), TEXT("под-окно 1 открыто подрецептом 214"));
				Check(!Rw->DebugText(77102).IsEmpty() && Rw->DebugText(77102) == Rw->DebugText(77002),
					FString::Printf(TEXT("77102 = имя рецепта-РОДИТЕЛЯ «%s» (pRecipeData + 2)"), *Rw->DebugText(77102)));
				Check(Rw->DebugIconCode(77110) == 72731 && Rw->DebugText(77111) == ItemName(72731) && Rw->DebugText(77113) == TEXT("0 / 1"),
					FString::Printf(TEXT("M1 = материал родителя 72731 «%s»"), *Rw->DebugText(77113)));
				Check(Rw->DebugIconCode(77120) == 72746 && Rw->DebugText(77123) == TEXT("0 / 10") && Rw->DebugShown(77126) && Rw->DebugShown(77124),
					TEXT("M2 = 72746 «0 / 10», plus (подрецепт 218), затемнено"));
				Check(Rw->DebugIconCode(77130) == 20961 && Rw->DebugText(77133) == TEXT("10 / 10") && !Rw->DebugShown(77136) && !Rw->DebugShown(77134),
					TEXT("M3 = 20961 «10 / 10», без plus, не затемнено"));
				Check(Rw->DebugIconCode(77140) == 20962 && Rw->DebugShown(77146), TEXT("M4 = 20962, plus (подрецепт 1)"));
				Check(Rw->DebugText(77101) == AsdaNotice::Loc(Rw->GetGameInstance(), 5531) && Rw->DebugText(77201) == AsdaNotice::Loc(Rw->GetGameInstance(), 5532),
					TEXT("заголовки под-окон 5531 / 5532"));
				GEngine->Exec(W, TEXT("asda.UI.TreeGeom w3_recipe_sub1 WBP_ITEM_making_R_BOX"));
			});
			At(3.2f, [Shot]() { Shot(TEXT("w3_recipe_sub1")); });
			// ── Шаг 4: Д-В3-1 — строка без подрецепта при низком уровне ремесла ──
			At(3.4f, [Rw, Craft, SetCVar, Check]()
			{
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 30, 1, 0, 0));   // ур. ремесла 1 < 2
			});
			At(3.6f, [Rw, SetCVar, Check]()
			{
				Rw->RefreshAll();
				Check(Rw->DebugShown(77134), TEXT("Д-В3-1 (1): M3 без подрецепта при уровне 1 < 2 — затемнено"));
				SetCVar(TEXT("asda.Craft.FixSubRowDarken"), 0);
				Rw->RefreshAll();
				Check(!Rw->DebugShown(77134), TEXT("Д-В3-1 (0): как оригинал — M3 не затемнено при низком уровне"));
				SetCVar(TEXT("asda.Craft.FixSubRowDarken"), 1);
			});
			At(3.7f, [Craft]() { Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 30, 5, 100, 10)); });
			// ── Шаг 5: клик по M2 под-окна 1 → под-окно 2 (подрецепт 218), рамка 77153 ──
			At(3.9f, [Rw, ClickRow]() { Rw->RefreshAll(); ClickRow(1, 1, TEXT("M2 под-окна 1")); });
			At(4.3f, [Rw, ItemName, Check, W]()
			{
				Check(Rw->GetSubSelRow(0) == 1 && Rw->DebugShown(77153), FString::Printf(TEXT("клик по M2: строка под-окна %d, рамка 77153 видна"), Rw->GetSubSelRow(0)));
				const FVector2D P = Rw->DebugSlotPos(77153);
				Check(FMath::IsNearlyEqual((float)P.X, 40.f) && FMath::IsNearlyEqual((float)P.Y, 82.f),
					FString::Printf(TEXT("рамка 77153 (план 0) в группе под-окна (%.0f, %.0f) = база (38,79) + код (40, 34·1+48) − JUI (38,79)"), P.X, P.Y));
				Check(Rw->IsSubOpen(1) && Rw->GetSubRecipeId(1) == 218 && Rw->DebugShown(77200), TEXT("под-окно 2 открыто подрецептом 218"));
				Check(!Rw->DebugText(77202).IsEmpty() && Rw->DebugText(77202) != Rw->DebugText(77102),
					FString::Printf(TEXT("77202 = имя родителя (подрецепт 214) «%s»"), *Rw->DebugText(77202)));
				Check(Rw->DebugIconCode(77210) == 72746 && Rw->DebugText(77213) == TEXT("0 / 10"), TEXT("M1 под-окна 2 = материал 72746 «0 / 10»"));
				Check(Rw->DebugIconCode(77220) == 20914 && Rw->DebugIconCode(77230) == 20924 && Rw->DebugIconCode(77240) == 20918,
					TEXT("M2..M4 под-окна 2 = 20914 / 20924 / 20918"));
				GEngine->Exec(W, TEXT("asda.UI.TreeGeom w3_recipe_sub2 WBP_ITEM_making_R_BOX"));
			});
			At(4.5f, [Shot]() { Shot(TEXT("w3_recipe_sub2")); });
			// ── Шаг 6: нотисы кнопок по обработчикам ──
			At(4.7f, [Rw, Craft, Put, Check, NoticeHas, JournalHas, Dump]()
			{
				Craft->ClearNoticeJournal(); Rw->ClearJournal();
				Rw->DebugPressButton(77068);   // 76: нет 72731 → 5569
				Check(NoticeHas(TEXT("msgbox 5569")), TEXT("77068: материалов нет → ONEBUT 5569"));
				Rw->DebugPressButton(77151);   // 214: 72746 0/10, уровень 2 <= 5 → 5569
				Check(NoticeHas(TEXT("msgbox 5569")) && JournalHas(TEXT("77151: нет материалов")), TEXT("77151: подрецепт 214 без материалов → 5569"));
				// 218: положить материалы — рецепт не изучен → 5585.
				Put(4, 20914, 5); Put(5, 20924, 1); Put(6, 20918, 1);
				Craft->ClearNoticeJournal();
				Rw->DebugPressButton(77251);
				Check(NoticeHas(TEXT("msgbox 5585")), TEXT("77251: подрецепт 218 не изучен → ONEBUT 5585"));
				Dump(TEXT("после нотисов 5569/5585"));
			});
			At(5.0f, [Rw, Craft, Put, Check, NoticeHas, JournalHas]()
			{
				// M4 (подрецепт 1 «абразив», изучен): клик → под-окно 2 = рецепт 1; песка нет → 5569; песок есть → 77500 (В4).
				Rw->OnSubRowClicked(3);
				Check(Rw->GetSubRecipeId(1) == 1, FString::Printf(TEXT("M4 → под-окно 2 = подрецепт %d"), Rw->GetSubRecipeId(1)));
				Craft->ClearNoticeJournal();
				Rw->DebugPressButton(77251);
				Check(NoticeHas(TEXT("msgbox 5569")), TEXT("77251: абразив без песка → 5569"));
				Put(7, 20931, 3);
				Rw->ClearJournal(); Craft->ClearNoticeJournal();
				Rw->DebugPressButton(77251);
				Check(JournalHas(TEXT("77500 открыто — рецепт 1")) && Craft->GetNoticeJournal().Num() == 0,
					TEXT("77251: все проверки пройдены → переход в окно 77500 (проверка без показа — окно ведёт `asda.Craft.SelfTestMake`), нотиса нет"));
				// 77068: материалы есть, уровень мал → 5584.
				Put(8, 72731, 1); Put(1, 0, 0); Put(0, 20957, 4);
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 30, 3, 0, 0));   // ур. 3 < 4
			});
			At(5.3f, [Rw, Craft, Check, NoticeHas, JournalHas]()
			{
				Craft->ClearNoticeJournal(); Rw->ClearJournal();
				Rw->DebugPressButton(77068);
				Check(NoticeHas(TEXT("msgbox 5584")), TEXT("77068: материалы есть, уровень 3 < 4 → 5584"));
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 30, 5, 100, 10));
			});
			At(5.5f, [Rw, Craft, Check, NoticeHas, JournalHas]()
			{
				Craft->ClearNoticeJournal(); Rw->ClearJournal();
				Rw->DebugPressButton(77068);
				Check(JournalHas(TEXT("77500 открыто — рецепт 76")) && !NoticeHas(TEXT("msgbox")), TEXT("77068: всё есть → переход в окно 77500 (проверка без показа)"));
				// 5570: под-окно 2 закрыто (записи [131] нет) — ветка без видимой кнопки, зовём обработчик напрямую.
				Rw->DebugPressButton(77252);
				Check(!Rw->IsSubOpen(1) && Rw->GetSubSelRow(0) == -1 && !Rw->DebugShown(77153), TEXT("77252: под-окно 2 закрыто, выбор под-окна 1 и рамка 77153 сняты"));
				Craft->ClearNoticeJournal();
				Rw->OnSubMakeClicked(1);
				Check(NoticeHas(TEXT("msgbox 5570")), TEXT("кнопка под-окна 2 без записи → 5570 «Список не выбран»"));
			});
			// ── Шаг 7: закрытия под-окон, Д-В3-2 ──
			At(5.8f, [Rw, SetCVar, Check]()
			{
				Rw->OnMainRowClicked(0); Rw->OnSubRowClicked(1);
				Check(Rw->IsSubOpen(0) && Rw->IsSubOpen(1), TEXT("цепочка 77000 → 77100 → 77200 открыта заново"));
				Rw->DebugPressButton(77159);
				Check(!Rw->IsSubOpen(0) && !Rw->IsSubOpen(1) && Rw->GetSelRow() == -1 && !Rw->DebugShown(77154),
					TEXT("Д-В3-2 (1): 77159 закрыл под-окно 1 и под-окно 2, выбор строки 77000 и рамка сняты"));
				SetCVar(TEXT("asda.Craft.FixOrphanSubWindow"), 0);
				Rw->OnMainRowClicked(0); Rw->OnSubRowClicked(1);
				Rw->DebugPressButton(77152);
				Check(!Rw->IsSubOpen(0) && Rw->IsSubOpen(1), TEXT("Д-В3-2 (0): как оригинал — 77152 закрыл только под-окно 1, под-окно 2 осталось"));
				SetCVar(TEXT("asda.Craft.FixOrphanSubWindow"), 1);
				Rw->DebugPressButton(77259);
				Check(!Rw->IsSubOpen(1), TEXT("77259 закрыл под-окно 2"));
			});
			// ── Шаг 8: гейты «ремесло открыто» в чужих системах (часть мест — ревизия В0–В2 С-1) ──
			At(6.0f, [GI, Inv, WM, Check]()
			{
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));   // нотисы шагов 6-7 модальны — не мешать гейтам ниже
				if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
				Inv->OpenWindow();
				Check(!Inv->IsWindowOpen(), TEXT("гейт uiOpenInventoryWindow (ui_core.c:46608): при открытом 76000 сумка не открылась"));
				FString Why;
				const UAsdaHotbarSubsystem* HB = GI->GetSubsystem<UAsdaHotbarSubsystem>();
				Check(HB && HB->IsActivationBlockedForCheck(Why) && Why.Contains(TEXT("ремесла")), FString::Printf(TEXT("гейт Hotbar_SlotActivate (ui_core.c:67375): «%s»"), *Why));
			});
			// ── Шаг 9: К-7 — окно состава закрыто без изготовления: флаги снимка сняты (1) / живут (0) ──
			At(6.2f, [Rw, Win, WM, Inv, SetCVar, Check]()
			{
				Rw->DebugPressButton(77067);
				Check(!Rw->IsOpen() && Win->GetSelRow() == -1 && Win->IsOpen(), TEXT("77067: окно состава закрыто, выбор строки 76000 снят, 76000 открыто"));
				Check(!WM->IsWindowSlotCaptured(0), TEXT("К-7 (1): флаги снимка окна состава сняты при закрытии без изготовления"));
				// Как оригинал: флаг живёт; чужое восстановление переоткрывает сумку.
				SetCVar(TEXT("asda.Craft.FixStaleSnapshot"), 0);
				Win->HideAndCleanup();
				Inv->OpenWindow();
				Win->GateAndToggle(TEXT("самотест К-7 (0)"));
				Rw->OpenForRecipe(76, TEXT("самотест К-7 (0)"));
				Rw->DebugPressButton(77069);
				Check(!Rw->IsOpen() && WM->IsWindowSlotCaptured(0), TEXT("К-7 (0): как оригинал — после закрытия 77000 флаг сумки жив"));
				Win->HideAndCleanup();
				Inv->CloseWindow();
				const int32 Reopened = WM->ReopenCapturedWindows(TEXT("самотест: чужое восстановление"));
				Check(Reopened == 1 && Inv->IsWindowOpen(), TEXT("К-7 (0): чужое восстановление переоткрыло давно закрытую сумку (дефект воспроизведён)"));
				Check(!WM->IsWindowSlotCaptured(0), TEXT("восстановление обнулило все флаги (memset 0x3C)"));
				SetCVar(TEXT("asda.Craft.FixStaleSnapshot"), 1);
				Inv->CloseWindow();
			});
			// ── Шаг 10: снимок/восстановление — игровые функции открытия (сумка + книга навыков + сообщество) ──
			At(6.5f, [GI, WM, Inv, Check]()
			{
				UAsdaCommunitySubsystem* Cm = GI->GetSubsystem<UAsdaCommunitySubsystem>();
				Inv->OpenWindow();
				if (Cm && !Cm->IsWindowOpen()) { Cm->OpenWindow(); }
				const bool bCm = Cm && Cm->IsWindowOpen();
				Check(Inv->IsWindowOpen() && bCm, TEXT("сумка и сообщество открыты вместе (гейт сообщества — барабан гачи, не сумка)"));
				WM->CaptureOpenWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::NpcDialog, TEXT("самотест: вход в разговор"));
				WM->CloseAllPlayerWindows();
				Check(!Inv->IsWindowOpen() && !(Cm && Cm->IsWindowOpen()), TEXT("uiCloseAllWindows закрыл сумку и сообщество"));
				const int32 N = WM->ReopenCapturedWindows(TEXT("самотест: уход от NPC"));
				WM->RestoreAfterSceneExit();
				Check(N == 2 && Inv->IsWindowOpen() && Cm && Cm->IsWindowOpen(), FString::Printf(TEXT("восстановление снимка: переоткрыто окон %d (сумка + сообщество, их игровыми «открыть»)"), N));
				Inv->CloseWindow();
				if (Cm) { Cm->CloseWindow(); }
			});
			// ── Шаг 10б: БОЕВОЙ путь диалога NPC — вход снимает окна и закрывает их, уход возвращает ──
			At(6.65f, [GI, WM, Inv, Check]()
			{
				UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
				Inv->OpenWindow();
				// NPC 1 «lua» (NpcQuestNpc.csv: сценарий есть в NpcNsrHeader, тип 0).
				const bool bDlg = Dlg && Dlg->OpenForNpcId(1);
				Check(bDlg && Dlg->IsDialogOpen(), TEXT("диалог NPC 1 открыт (regionWindow_OpenAndPathMove)"));
				Check(WM->IsWindowSlotCaptured(0) && WM->GetWindowSlotOwners(0) == (uint8)UAsdaWindowSubsystem::EWindowSnapshotOwner::NpcDialog,
					TEXT("вход в разговор: слот 0 (сумка) поднят владельцем «диалог NPC» (ui_core.c:61117)"));
				Check(!Inv->IsWindowOpen(), TEXT("вход в разговор: uiCloseAllWindows закрыл сумку"));
				if (Dlg) { Dlg->Close(); }
				Check(Inv->IsWindowOpen() && !WM->IsWindowSlotCaptured(0), TEXT("уход от NPC (UI_resetAllWindowsAndLeave_5045): сумка переоткрыта, снимок обнулён"));
				Inv->CloseWindow();
			});
			// ── Шаг 11: 6601 код 1 закрывает окно состава; закрытие 76000 закрывает 77000 ──
			At(6.8f, [Rw, Win, Craft, Check]()
			{
				if (!Win->IsOpen()) { Win->GateAndToggle(TEXT("самотест: финал")); }
				Rw->OpenForRecipe(76, TEXT("самотест: финал"));
				Check(Rw->IsOpen(), TEXT("77000 открыто для финала"));
				Check(!Rw->OpenForRecipe(76, TEXT("повтор")), TEXT("тот же рецепт повторно — ничего (uiCraftWnd_OpenForItem)"));
				Win->HideAndCleanup();
				Check(!Rw->IsOpen() && Rw->GetRecipeId() == -1, TEXT("закрытие 76000 (uiWindowHideAndCleanup) сбросило 77000 (itemTooltipWndReset)"));
				Win->GateAndToggle(TEXT("самотест: 6601"));
				Rw->OpenForRecipe(1, TEXT("самотест: 6601"));
				TArray<uint8> D; D.SetNumZeroed(3); D[0] = 1; D[1] = 1; Craft->DebugInject(6601, D);
			});
			At(7.1f, [Rw, Win, Check, Dump]()
			{
				Check(!Rw->IsOpen() && !Win->IsOpen(), TEXT("6601 код 1: окно состава сброшено (craftRemoveSelectedItemSlot), 76000 закрыто"));
				Dump(TEXT("финал"));
			});
			At(7.5f, [Stat, bQuit, W]()
			{
				// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Craft.FixStackPick/FixSubRowDarken/FixOrphanSubWindow/FixStaleSnapshot) — к исходному значению И приоритету SetBy
				//   (прежде — константа 1 с SetByCode).
				AsdaCVarSave::RestoreAll(TEXT("asda.Craft.SelfTestRecipe"));
				if (UAsdaCraftMakeWindowSubsystem* Mk = W->GetGameInstance()->GetSubsystem<UAsdaCraftMakeWindowSubsystem>()) { Mk->DebugSetDryOpen(false); }
				AsdaSelfTestKit::Summary(LogAsdaCraftRecipe, TEXT("САМОТЕСТ-77000"), Stat->Ok, Stat->Fail, TEXT(""), true);   // ★ 11.10.2026 (В3 S1): единый итог
				AsdaSelfTestKit::Quit(W, bQuit);
			});
		}));
#endif // ASDA_DEV_CONSOLE
}
