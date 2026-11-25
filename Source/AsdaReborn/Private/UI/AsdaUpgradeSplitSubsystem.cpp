// Copyright (c) 2026 @sitxovski (Discord)
// Окно «Разделение улучшения» (JUI 50500) — поток, поля и отступления в шапке UI/AsdaUpgradeSplitSubsystem.h.
#include "UI/AsdaUpgradeSplitSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaDevConsole.h"           // ASDA_DEV_CONSOLE — дев-команда состояния вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // каркас окон JUI
#include "UI/AsdaNotice.h"            // Loc / MsgBox
#include "UI/AsdaLocIds.h"            // AsdaLoc::kBtnOk
#include "UI/AsdaTipPanel.h"          // EAsdaTipOwner::UpgradeSplit
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaAnimatedPlane.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Login/AsdaButtonRelay.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaUpgradeSplit, Log, All);
static_assert(UAsdaUpgradeSplitSubsystem::kListMax == UAsdaInventorySubsystem::kShellSlots, "длина списка разошлась с ёмкостью сумки");
static_assert(UAsdaUpgradeSplitSubsystem::kCols * UAsdaUpgradeSplitSubsystem::kRows == UAsdaUpgradeSplitSubsystem::kCells, "сетка 10×3");

// [ПОРТ] Вход в окно двойным щелчком по пустому камню (решение владельца 31.10.2026); 0 — окна нет, как в оригинале.
static ASDA_DEV_CVAR(int32, CVarUpgradeSplitEnable,
	TEXT("asda.UpgradeSplit.Enable"), 1,
	TEXT("Окно «Разделение улучшения» 50500: 1 — открывается двойным щелчком по пустому камню улучшения (203/205) в сумке "
	     "[ПОРТ]; 0 — как в оригинале: окна нет, у пустого камня обычное использование."),
	ECVF_Default);
// [ИСПРАВЛЕНИЕ] Число пустых камней — по живой сумке (у эталона — только по кнопке и ответу 6550).
static ASDA_DEV_CVAR(int32, CVarUpgradeSplitLiveCounts,
	TEXT("asda.UpgradeSplit.LiveCounts"), 1,
	TEXT("Окно 50500: 1 — число пустых камней пересчитывается при любом изменении сумки [ИСПРАВЛЕНИЕ]; 0 — как эталон "
	     "(только кнопкой «Разделение» и ответом 6550)."),
	ECVF_Default);
// [ИСПРАВЛЕНИЕ] Код 6 ответа 6550 (нет места в сумке) эталон молча глотал.
static ASDA_DEV_CVAR(int32, CVarUpgradeSplitCode6Notice,
	TEXT("asda.UpgradeSplit.Code6Notice"), 1,
	TEXT("Окно 50500: 1 — код 6 ответа 6550 («нет свободной ячейки», сервер Recv_UserUpgradeExtraction(10)) показывает 108518 "
	     "[ИСПРАВЛЕНИЕ]; 0 — как эталон: молча."),
	ECVF_Default);
// Цвет «+N%» под иконками — как в инвентаре порта (по уровню улучшения); 0 — эталонный 0xFFFFF200.
// [ПОРТ] Пустая сетка (нет вещей с +27 ≥ 88) — в поле 50541 вместо предупреждения 108509 показать правило оригинала 110112
//   («сила улучшения выше +88%…», подсказка инвентаря эталона ui_core.c:47825). У эталона окно молча пустое.
// [ПОРТ] Решение владельца 01.11.2026: «уберём ограничение по улучшению — переносить любые улучшения, главное — чтобы улучшение
//   было». Эталон (`invGridRefreshEnchanted@0xB68140`) брал в сетку только вещи с +27 ≥ 88; сервер (`Recv_UserUpgradeExtraction`)
//   этого порога не знает и требует лишь уровень +24 ≠ 0 (иначе код 4). Теперь порог — ключ: 0 (умолчание) — любая вещь с
//   уровнем улучшения > 0; 88 — как в оригинале.
static ASDA_DEV_CVAR(int32, CVarUpgradeSplitMinPercent,
	TEXT("asda.UpgradeSplit.MinPercent"), 0,
	TEXT("Окно 50500, отбор вещей в сетку: минимальная сила улучшения +27 (в процентах). 0 — любая вещь с уровнем улучшения > 0 "
	     "(решение владельца 01.11, [ПОРТ]); 88 — как оригинал (invGridRefreshEnchanted)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarUpgradeSplitEmptyHint,
	TEXT("asda.UpgradeSplit.EmptyHint"), 1,
	TEXT("Окно 50500: 1 — при пустой сетке (нет вещей с силой улучшения ≥ +88%) поле 50541 показывает правило 110112 [ПОРТ]; "
	     "0 — как эталон: всегда предупреждение 108509."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarUpgradeSplitGradeColor,
	TEXT("asda.UpgradeSplit.EnchantTextGradeColor"), 1,
	TEXT("Окно 50500, цвет «+N%»: 1 — по уровню улучшения (как инвентарь и окно 50300); 0 — эталон, 0xFFFFF200 (SetField923(-3584))."),
	ECVF_Default);

namespace AsdaSplitLoc
{
	constexpr int32 kTitle      = 108500;   // 50501 «Разделение улучшения»
	constexpr int32 kItemLabel  = 108501;   // 50510 «Предмет для разделения»
	constexpr int32 kLevel      = 108502;   // 50512 / 50532 «Уровень улучшения»
	constexpr int32 kValue      = 108503;   // 50514 / 50534 «Сила улучшения»
	constexpr int32 kNeedStone  = 108504;   // 50520 «Необходим пустой камень»
	constexpr int32 kDoneStone  = 108505;   // 50530 «Готовый камень улучшения»
	constexpr int32 kBag        = 108506;   // 50540 «Рюкзак»
	constexpr int32 kBtnSplit   = 108507;   // 50582
	constexpr int32 kBtnCancel  = 108508;   // 50583
	constexpr int32 kWarn       = 108509;   // 50541 и вопрос (1, 68)
	constexpr int32 kRule88     = 110112;   // правило «выше +88%» (подсказка инвентаря эталона) — 50541 при пустой сетке [ПОРТ]
	constexpr int32 kAskOk      = 108510;   // кнопки вопроса 68
	constexpr int32 kAskNo      = 108511;
	constexpr int32 kShort      = 108517;   // «не хватает %d»
	constexpr int32 kNoSpace    = 108518;   // нет места в сумке
	constexpr int32 kOwnedNeed  = 108520;   // 50521 «есть %d / нужно %d»
	constexpr int32 kWeaponCnt  = 108521;   // 50522
	constexpr int32 kArmorCnt   = 108522;   // 50524
	constexpr int32 kErrParam   = 108600;   // коды 6550
	constexpr int32 kErrStone   = 108602;
	constexpr int32 kErrItem    = 108603;
	constexpr int32 kErrNotUpg  = 108604;
	constexpr int32 kErrShort   = 108605;
	constexpr int32 kErrDb      = 108606;
	constexpr int32 kErrProc    = 108607;
	constexpr int32 kOkWeapon   = 109904;   // исход 0/1, оружие
	constexpr int32 kOkArmor    = 109905;   // исход 0/1, доспех
	constexpr int32 kBadWeapon  = 109906;   // исход 2/3, оружие
	constexpr int32 kBadArmor   = 109907;   // исход 2/3, доспех
	constexpr int32 kAskId      = 68;       // пара очереди (1, 68)
}

namespace
{
	const FName  kWindowId(TEXT("Strength_Part_Win"));
	const TCHAR* kWindowWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Strength_Part_Win.WBP_Strength_Part_Win_C");
	constexpr int32 kIdItem = 50516, kIdStone = 50526, kIdResult = 50536, kIdCell0 = 50551;
	constexpr uint32 kColEnchantLegacy = 0xFFFFF200;   // `CControl_SetField923(-3584)`

	// Имя подписи → idnum (дамп ассета и `.JUI` 50500).
	struct FSplitTextId { const TCHAR* Name; int32 Id; };
	const FSplitTextId kTexts[] = {
		{ TEXT("Strength_Part_Top_Text"), 50501 },
		{ TEXT("Strength_Part_1_Text_1"), 50510 }, { TEXT("Strength_Part_1_Text_2"), 50511 },
		{ TEXT("Strength_Part_1_Text_3"), 50512 }, { TEXT("Strength_Part_1_Text_4"), 50513 },
		{ TEXT("Strength_Part_1_Text_5"), 50514 }, { TEXT("Strength_Part_1_Text_6"), 50515 },
		{ TEXT("Strength_Part_1_Mark"),   50516 },
		{ TEXT("Strength_Part_2_Text_1"), 50520 }, { TEXT("Strength_Part_2_Text_2"), 50521 },
		{ TEXT("Strength_Part_2_Text_3"), 50522 }, { TEXT("Strength_Part_2_Text_4"), 50523 },
		{ TEXT("Strength_Part_2_Text_5"), 50524 }, { TEXT("Strength_Part_2_Text_6"), 50525 },
		{ TEXT("Strength_Part_2_Mark1"),  50526 },
		{ TEXT("Strength_Part_3_Text_1"), 50530 }, { TEXT("Strength_Part_3_Text_3"), 50532 },
		{ TEXT("Strength_Part_3_Text_4"), 50533 }, { TEXT("Strength_Part_3_Text_5"), 50534 },
		{ TEXT("Strength_Part_3_Text_6"), 50535 }, { TEXT("Strength_Part_3_Mark"),   50536 },
		{ TEXT("Strength_Part_4_Text2"),  50540 }, { TEXT("Strength_Part_4_Text1"),  50541 },
		{ TEXT("Strength_Part_OK_Text"),  50582 }, { TEXT("Strength_Part_NO_Text"),  50583 },
	};

	// Эталонный `sprintf(fmt, a, b)` над loc-строкой: `%d` по порядку, `%%` → «%».
	FString FormatD(const FString& Fmt, int32 A, int32 B = 0)
	{
		FString Out;
		int32 N = 0;
		for (int32 i = 0; i < Fmt.Len(); ++i)
		{
			if (Fmt[i] == TEXT('%') && i + 1 < Fmt.Len())
			{
				if (Fmt[i + 1] == TEXT('%')) { Out.AppendChar(TEXT('%')); ++i; continue; }
				if (Fmt[i + 1] == TEXT('d') && N < 2) { Out.AppendInt(N == 0 ? A : B); ++N; ++i; continue; }
			}
			Out.AppendChar(Fmt[i]);
		}
		return Out;
	}
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaUpgradeSplitSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только кладём тело в очередь.
		HResult = Net->OnUpgradeExtractionResult.AddLambda([this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ESC — msg 21 → `sub_932F64` (скрыть). Признак «закрываюсь по ESC» ставит `helper_setByteField552(50500, 1)`,
		//   окно стоит в таблице `kEscCloseWindows` (AsdaWindowSubsystem_Close.cpp).
		WM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaUpgradeSplitSubsystem::CloseWindow));
		WM->RegisterWindowCloser(kWindowId, FSimpleDelegate::CreateWeakLambda(this, [this]() { if (bOpen) { CloseWindow(); } }));
		// [ПОРТ] Колесо над сеткой — по рядам (сумка ×2).
		WM->RegisterWheelHandler(kWindowId, FAsdaUiWheelHandler::CreateWeakLambda(this, [this](int32 Notches) -> bool
		{
			if (!bOpen || !bControlsReady) { return false; }
			bool bOver = false;
			for (const TObjectPtr<UImage>& I : CellIcons) { if (I && AsdaUi::IsCursorOverWidget(I)) { bOver = true; break; } }
			if (!bOver) { return false; }
			const int32 NewRow = FMath::Clamp(Row - Notches, 0, MaxRow());
			if (NewRow != Row) { Row = NewRow; TickTooltip(-1); RebuildGrid(); }
			return true;
		}));
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// Состав сумки поменялся извне — перерисуем в Tick (из колбэка только метим).
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32, int32) { bDirty = true; });
	}
}

void UAsdaUpgradeSplitSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnUpgradeExtractionResult.Remove(HResult); }
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { Inv->OnCellChanged.Remove(HInvCell); }
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterEscCloser(kWindowId);
		WM->UnregisterWindowCloser(kWindowId);
		WM->UnregisterWheelHandler(kWindowId);
	}
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaUpgradeSplitSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseWindow();
	SelCell = -1; SelLevel = 0; StoneCells.Reset(); ResultCell = -1; ShownEffect = -1;
	List.Reset(); Row = 0; WeaponEmpty = ArmorEmpty = 0; bDirty = false;
	TArray<TArray<uint8>> Drop;
	PendingResults.Drain(Drop);
	Mouse.Reset();
	CellDbl.Reset();
	UE_LOG(LogAsdaUpgradeSplit, Verbose, TEXT("[разделение улучшения] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
}

void UAsdaUpgradeSplitSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.UpgradeSplit");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (TArray<TArray<uint8>> Local; PendingResults.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnResult(B); }
	}
	if (!bOpen || !bControlsReady) { return; }
	if (bDirty)
	{
		bDirty = false;
		if (CVarUpgradeSplitLiveCounts.GetValueOnGameThread() != 0) { CountMaterials(); }
		RefreshAll();
	}
	Mouse.Update();
	// Модальный гейт: под окном-сообщением эталон не отдаёт мышь окнам ниже.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		TickTooltip(-1);
		Mouse.Reset();
		return;
	}
	TickMouse();
}

// ═════════════════════════════ ВХОД / ОКНО ════════════════════════════════════════════════════

bool UAsdaUpgradeSplitSubsystem::OnBagDoubleClick(int32 CellIndex)
{
	if (CVarUpgradeSplitEnable.GetValueOnGameThread() == 0) { return false; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* Src = Bag(CellIndex);
	FAsdaSpecialItemlistRow Sp;
	if (!IT || !Src || Src->ItemCode <= 0 || !IT->GetSpecialItem(Src->ItemCode, Sp) || !IsEmptyStoneSubType(Sp.SubType)) { return false; }
	OpenWindow();
	UE_LOG(LogAsdaUpgradeSplit, Log, TEXT("[разделение улучшения] двойной щелчок по пустому камню %d (подтип %d, ячейка %d) — окно 50500 [ПОРТ]"),
		Src->ItemCode, Sp.SubType, CellIndex);
	return true;
}

bool UAsdaUpgradeSplitSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kWindowZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			// Корпус окна — девять планов рамки `Strength_Part_Win_0_0…8` (дамп ассета).
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kWindowId, this, Widget, bOpen, TEXT("Strength_Part_Win_0_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaUpgradeSplit, Error, TEXT("[разделение улучшения] WBP_Strength_Part_Win не загрузился (%s)"), kWindowWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaUpgradeSplitSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	Grid = GridItem = GridStone = GridResult = nullptr;
	CellIcons.Reset(); CellLabels.Reset(); Texts.Reset(); Effects.Reset(); Relays.Reset();
	IconItem = IconStone = IconResult = nullptr;
	SelFrame = nullptr;
	BtnSplit = BtnCancel = BtnClose = nullptr;
	bControlsReady = false;
	bOpen = false;
	TipId = -1;
	Mouse.Reset();
}

void UAsdaUpgradeSplitSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	CellIcons.SetNum(kCells); CellLabels.SetNum(kCells); Effects.SetNum(kEffects);
	TArray<FName> CellNames; CellNames.SetNum(kCells);
	FName ItemName, StoneName, ResultName;

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("Strength_Part_OK"))  { BtnSplit = Btn; }
			if (N == TEXT("Strength_Part_NO"))  { BtnCancel = Btn; }
			if (N == TEXT("Strength_Part_0_X")) { BtnClose = Btn; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			for (const FSplitTextId& T : kTexts) { if (N == T.Name) { Texts.Add(T.Id, Tb); } }
			for (int32 C = 0; C < kCells; ++C)
			{
				if (N == FString::Printf(TEXT("Strength_Part_4_BOX_%d"), C + 1))
				{
					CellLabels[C] = Tb;
					CellBaseColor = Tb->GetColorAndOpacity();
					Tb->SetText(FText::GetEmpty());
				}
			}
			continue;
		}
		// 50584 «Отлично» — группа из двух планов (`Gfont_Great`), остальные три — одиночные планы.
		if (N == TEXT("Strength_Part_Win_Gfont1_grp")) { Effects[0] = W; continue; }
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { continue; }
		if (Idc == TEXT("Strength_Part_4_Box_Click")) { SelFrame = Cast<UAsdaAnimatedPlane>(W); continue; }
		if (Idc == TEXT("Strength_Part_Win_Gfont3")) { Effects[1] = W; continue; }
		if (Idc == TEXT("Strength_Part_Win_Gfont4")) { Effects[2] = W; continue; }
		if (Idc == TEXT("Strength_Part_Win_Gfont5")) { Effects[3] = W; continue; }
		UImage* Img = Cast<UImage>(W);
		if (!Img || Plane != 0) { continue; }
		if (Idc == TEXT("Strength_Part_1_Mark"))  { IconItem = Img;   ItemName = FName(*N); continue; }
		if (Idc == TEXT("Strength_Part_2_Mark1")) { IconStone = Img;  StoneName = FName(*N); continue; }
		if (Idc == TEXT("Strength_Part_3_Mark"))  { IconResult = Img; ResultName = FName(*N); continue; }
		for (int32 C = 0; C < kCells; ++C)
		{
			if (Idc == FString::Printf(TEXT("Strength_Part_4_BOX_%d"), C + 1)) { CellIcons[C] = Img; CellNames[C] = FName(*N); }
		}
	}

	// ── Иконки — общая точка `UAsdaIconCellGrid` (закон `port-shared-points`); клик и наведение ловим сами (`TickMouse`) ──
	auto MakeGrid = [this](const TArray<FName>& Names, float Px, TFunction<int32(int32)>&& Code) -> UAsdaIconCellGrid*
	{
		UAsdaIconCellGrid* G = NewObject<UAsdaIconCellGrid>(this);
		G->SetCellSizePx(Px);
		G->SetClickOverlayEnabled(false);
		G->bClearEmptyCells = true;   // эталон затирает иконку пустой текстурой 32×32 в начале прохода
		G->SetupNamed(Widget, Names, /*bMale*/true, MoveTemp(Code), [](int32) {});
		return G;
	};
	auto CodeOf = [this](int32 Cell) { const FAsdaItemInstance* It = Bag(Cell); return (It && !It->IsEmpty()) ? It->ItemCode : 0; };
	Grid = MakeGrid(CellNames, 32.f, [this, CodeOf](int32 V) { const int32 L = Row * kCols + V; return List.IsValidIndex(L) ? CodeOf(List[L]) : 0; });
	auto One = [](FName N) { TArray<FName> A; A.Add(N); return A; };
	if (!ItemName.IsNone())   { GridItem   = MakeGrid(One(ItemName),   32.f, [this, CodeOf](int32) { return CodeOf(SelCell); }); }
	if (!StoneName.IsNone())  { GridStone  = MakeGrid(One(StoneName),  32.f, [this, CodeOf](int32) { return StoneCells.Num() ? CodeOf(StoneCells[0]) : 0; }); }
	if (!ResultName.IsNone()) { GridResult = MakeGrid(One(ResultName), 64.f, [this, CodeOf](int32) { return CodeOf(ResultCell); }); }

	// ── Кнопки (ветка 66561) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (Btn) { AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays); }
	};
	Bind(BtnSplit,  50582, [this]() { OnSplitClicked(); });
	Bind(BtnCancel, 50583, [this]() { OnCancelClicked(); });
	Bind(BtnClose,  50589, [this]() { OnCancelClicked(); });

	// ── Статичные подписи и стиль `clanWindowInitControls` (0x200000 — псевдо-жирный) ──
	auto Label = [this](int32 Id, int32 LocId, bool bBold)
	{
		if (const TObjectPtr<UTextBlock>* Tb = Texts.Find(Id))
		{
			SetText(*Tb, Loc(LocId));
			if (bBold) { AsdaUi::ApplyPseudoBold(*Tb); }
		}
	};
	using namespace AsdaSplitLoc;
	Label(50501, kTitle, true);      Label(50510, kItemLabel, true);
	Label(50512, kLevel, false);     Label(50514, kValue, false);
	Label(50520, kNeedStone, true);  Label(50522, kWeaponCnt, false);  Label(50524, kArmorCnt, false);
	Label(50530, kDoneStone, false); Label(50532, kLevel, false);      Label(50534, kValue, false);
	Label(50541, kWarn, false);      Label(50540, kBag, true);
	Label(50582, kBtnSplit, false);  Label(50583, kBtnCancel, false);
	// Перенос предупреждения 50541 задан в ассете (`WrapTextAt = 656`, Tools/asda_fix_strength_part_owned_text.py).
	// Значения — пустые до первой перерисовки (в ассете лежат образцы `.JUI`: «11/12~», имена подписей).
	for (int32 Id : { 50511, 50513, 50515, 50516, 50521, 50523, 50525, 50526, 50533, 50535, 50536 })
	{
		if (const TObjectPtr<UTextBlock>* Tb = Texts.Find(Id)) { (*Tb)->SetText(FText::GetEmpty()); }
	}
	for (TObjectPtr<UWidget>& E : Effects) { if (E) { E->SetVisibility(ESlateVisibility::Collapsed); } }
	if (SelFrame) { SelFrame->SetVisibility(ESlateVisibility::Collapsed); }

	int32 Icons = 0;
	for (const TObjectPtr<UImage>& I : CellIcons) { if (I) { ++Icons; } }
	bControlsReady = (Icons == kCells && BtnSplit && BtnCancel && IconItem && IconStone && IconResult);
	UE_LOG(LogAsdaUpgradeSplit, Log, TEXT("%s"), *DumpControls());
	if (!bControlsReady) { UE_LOG(LogAsdaUpgradeSplit, Error, TEXT("[разделение улучшения] ключевые контролы окна 50500 не разобраны")); }
}

void UAsdaUpgradeSplitSubsystem::OpenWindow()
{
	if (!EnsureWidget()) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// [ПОРТ] Открытие (шапка): чистый выбор, свежие счётчики, перерисовка, показ поверх.
	ResetSlots();
	Row = 0;
	CountMaterials();
	bOpen = true;
	RefreshAll();
	WM->SetWindowVisible(kWindowId, true);
	WM->BringWindowToFront(kWindowId);
	Mouse.Reset();
	CellDbl.Reset();
	TipId = -1;
	UE_LOG(LogAsdaUpgradeSplit, Log, TEXT("[разделение улучшения] окно открыто: вещей %d, пустых камней оружия %d / доспеха %d"),
		List.Num(), WeaponEmpty, ArmorEmpty);
}

void UAsdaUpgradeSplitSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	bOpen = false;
	TickTooltip(-1);
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);   // `sub_B68660`: `CControl_SetVisible(окно, 0)` — и всё
	}
	UE_LOG(LogAsdaUpgradeSplit, Log, TEXT("[разделение улучшения] окно закрыто"));
}

// ═════════════════════════════ МОДЕЛЬ ═════════════════════════════════════════════════════════

const FAsdaItemInstance* UAsdaUpgradeSplitSubsystem::Bag(int32 Cell) const
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return (Inv && Cell >= 0) ? Inv->GetCell(1, Cell) : nullptr;
}

void UAsdaUpgradeSplitSubsystem::ResetSlots()
{
	SelCell = -1;
	StoneCells.Reset();
	ResultCell = -1;
	ShownEffect = -1;
	for (TObjectPtr<UWidget>& E : Effects) { if (E) { E->SetVisibility(ESlateVisibility::Collapsed); } }
}

void UAsdaUpgradeSplitSubsystem::RefreshAll()
{
	RebuildGrid();
	RefreshMainItem();
	RefreshStonePanel();
	RefreshResult();
}

void UAsdaUpgradeSplitSubsystem::SetText(UTextBlock* Tb, const FString& S) const
{
	if (Tb) { Tb->SetText(FText::FromString(S)); }
}

// «+N%» под иконкой: при уровне +24 > 0 — значение +27 цветом улучшения, иначе пусто (эталон: `sprintf(buf, "")`).
void UAsdaUpgradeSplitSubsystem::SetPercentLabel(UTextBlock* Tb, const FAsdaItemInstance* It, const FSlateColor& Base) const
{
	if (!Tb) { return; }
	if (It && !It->IsEmpty() && It->GemGrade != 0)
	{
		Tb->SetText(FText::FromString(FString::Printf(TEXT("+%d%%"), It->EnchantPercent)));
		const uint32 Argb = (CVarUpgradeSplitGradeColor.GetValueOnGameThread() != 0)
			? UAsdaItemTooltipSubsystem::EnhanceColorArgb((int32)It->GemGrade) : kColEnchantLegacy;
		Tb->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbHexToLinear(FString::Printf(TEXT("%08X"), Argb))));
	}
	else
	{
		Tb->SetText(FText::GetEmpty());
		Tb->SetColorAndOpacity(Base);
	}
}

void UAsdaUpgradeSplitSubsystem::RebuildGrid()
{
	List.Reset();
	const int32 MinPercent = FMath::Clamp(CVarUpgradeSplitMinPercent.GetValueOnGameThread(), 0, 1000);   // порог силы (0 — любой)
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (Inv && IT)
	{
		const int32 Total = FMath::Min(Inv->GetContainerSize(1), kListMax);
		for (int32 C = 0; C < Total; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(1, C);
			if (!It || It->IsEmpty()) { continue; }
			const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);
			if (!R || R->ItemSubType == 66 || R->ItemSubType == 200 || R->ItemSubType == 201) { continue; }
			// Главное — чтобы улучшение было (+24 > 0); порог силы — ключ `asda.UpgradeSplit.MinPercent` (0 — любой, эталон 88 = kMinPercent).
			if (It->GemGrade == 0 || (int32)(int16)It->EnchantPercent < MinPercent) { continue; }
			List.Add(C);
		}
	}
	Row = FMath::Clamp(Row, 0, MaxRow());
	for (int32 V = 0; V < kCells; ++V)
	{
		const int32 L = Row * kCols + V;
		SetPercentLabel(CellLabels.IsValidIndex(V) ? CellLabels[V].Get() : nullptr, List.IsValidIndex(L) ? Bag(List[L]) : nullptr, CellBaseColor);
	}
	if (Grid) { Grid->Refresh(); }
	PlaceSelFrame();
	// [ПОРТ] 50541: пустая сетка — правило отбора 110112, иначе предупреждение 108509 (эталон — всегда 108509).
	if (const TObjectPtr<UTextBlock>* Warn = Texts.Find(50541))
	{
		// Правило 110112 говорит о «+88%» — показываем его только при пороге оригинала (> 0); без порога пустая сетка = улучшенных вещей нет.
		const bool bRule = List.Num() == 0 && CVarUpgradeSplitEmptyHint.GetValueOnGameThread() != 0 && CVarUpgradeSplitMinPercent.GetValueOnGameThread() > 0;
		SetText(*Warn, Loc(bRule ? AsdaSplitLoc::kRule88 : AsdaSplitLoc::kWarn));
	}
}

void UAsdaUpgradeSplitSubsystem::PlaceSelFrame()
{
	// Рамка 50581 — у ячейки ВЫБРАННОЙ вещи (`pBagSlot == self[94]`), иначе скрыта.
	if (!SelFrame) { return; }
	const int32 L = List.IndexOfByKey(SelCell);
	const int32 V = (L != INDEX_NONE) ? L - Row * kCols : -1;
	if (SelCell < 0 || V < 0 || V >= kCells || !CellIcons[V]) { SelFrame->SetVisibility(ESlateVisibility::Collapsed); return; }
	AsdaUi::PlaceFrameOver(SelFrame, CellIcons[V], /*bRestartAnim*/true);
}

void UAsdaUpgradeSplitSubsystem::RefreshMainItem()
{
	// `if (self[94] && !*self[94]) self[94] = 0` — запись ушла из ячейки ⇒ выбора нет.
	const FAsdaItemInstance* It = Bag(SelCell);
	if (SelCell >= 0 && (!It || It->IsEmpty())) { SelCell = -1; It = nullptr; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const bool bHave = It && IT && IT->GetItem(It->ItemCode);
	auto T = [this](int32 Id) -> UTextBlock* { const TObjectPtr<UTextBlock>* P = Texts.Find(Id); return P ? P->Get() : nullptr; };
	SetPercentLabel(T(50516), bHave ? It : nullptr, CellBaseColor);
	SetText(T(50511), bHave ? IT->GetItemName(It->ItemCode) : FString());
	SetText(T(50513), bHave ? FString::Printf(TEXT("+%d%%"), It->EnchantPercent) : FString());
	SetText(T(50515), bHave ? FString::Printf(TEXT("%d(%%)"), It->EnchantPercent) : FString());
	if (GridItem) { GridItem->Refresh(); }
}

int32 UAsdaUpgradeSplitSubsystem::SelectedItemType() const
{
	const FAsdaItemInstance* It = Bag(SelCell);
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = (It && !It->IsEmpty() && IT) ? IT->GetItem(It->ItemCode) : nullptr;
	return R ? R->ItemType : -1;
}

int32 UAsdaUpgradeSplitSubsystem::CollectStones(int32 Budget)
{
	// `inventoryCountAmmoForBudget@0xB651D0`: камни сбрасываются; вид по `itemType` вещи (≤ 8 — доспех 203, 9 — оружие
	//   205, иное — ни один); стопки берутся, пока набрано < бюджета и их меньше 8; возврат — ВСЕ подходящие единицы.
	StoneCells.Reset();
	const int32 Type = SelectedItemType();
	if (Type < 0) { return 0; }
	const int32 Want = (Type <= 8) ? 203 : (Type == 9 ? 205 : -1);
	if (Want < 0) { return 0; }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Inv || !IT) { return 0; }
	int32 Sum = 0;
	const int32 Total = FMath::Min(Inv->GetContainerSize(1), kListMax);
	for (int32 C = 0; C < Total; ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(1, C);
		FAsdaSpecialItemlistRow Sp;
		if (!It || It->IsEmpty() || !IT->GetSpecialItem(It->ItemCode, Sp) || Sp.SubType != Want) { continue; }
		if (StoneCells.Num() < kStonesMax && Sum < Budget) { StoneCells.Add(C); }
		Sum += It->Amount;
	}
	return Sum;
}

void UAsdaUpgradeSplitSubsystem::CountMaterials()
{
	// `invCountColorMixMaterials@0xB65400`: суммы единиц 205 (оружие) и 203 (доспех) по сумке.
	WeaponEmpty = ArmorEmpty = 0;
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Inv || !IT) { return; }
	const int32 Total = FMath::Min(Inv->GetContainerSize(1), kListMax);
	for (int32 C = 0; C < Total; ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(1, C);
		FAsdaSpecialItemlistRow Sp;
		if (!It || It->IsEmpty() || !IT->GetSpecialItem(It->ItemCode, Sp)) { continue; }
		if (Sp.SubType == 205) { WeaponEmpty += It->Amount; }
		if (Sp.SubType == 203) { ArmorEmpty += It->Amount; }
	}
}

void UAsdaUpgradeSplitSubsystem::RefreshStonePanel()
{
	// `uiItemInfoRefresh@0xB67C10`: набор камней с бюджетом 1000; иконка первого; «есть / нужно» — только если камень есть.
	const int32 Owned = CollectStones(1000);
	auto T = [this](int32 Id) -> UTextBlock* { const TObjectPtr<UTextBlock>* P = Texts.Find(Id); return P ? P->Get() : nullptr; };
	SetText(T(50526), FString());
	FString Need;
	if (StoneCells.Num() > 0)
	{
		const FAsdaItemInstance* Sel = Bag(SelCell);
		const int32 Level = (Sel && !Sel->IsEmpty()) ? (int32)Sel->GemGrade : 0;
		Need = FormatD(Loc(AsdaSplitLoc::kOwnedNeed), Owned, Level);
	}
	SetText(T(50521), Need);
	SetText(T(50523), FString::FromInt(WeaponEmpty));
	SetText(T(50525), FString::FromInt(ArmorEmpty));
	if (GridStone) { GridStone->Refresh(); }
}

void UAsdaUpgradeSplitSubsystem::RefreshResult()
{
	// `ItemInfoPanel_UpdateTable2Item@0xB67E90`: готовый камень `[103]` — иконка, «+N%», «N(%)».
	const FAsdaItemInstance* It = Bag(ResultCell);
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FAsdaSpecialItemlistRow Sp;
	const bool bHave = It && !It->IsEmpty() && IT && IT->GetSpecialItem(It->ItemCode, Sp);
	auto T = [this](int32 Id) -> UTextBlock* { const TObjectPtr<UTextBlock>* P = Texts.Find(Id); return P ? P->Get() : nullptr; };
	SetPercentLabel(T(50536), bHave ? It : nullptr, CellBaseColor);
	SetText(T(50533), bHave ? FString::Printf(TEXT("+%d%%"), It->EnchantPercent) : FString());
	SetText(T(50535), bHave ? FString::Printf(TEXT("%d(%%)"), It->EnchantPercent) : FString());
	if (GridResult) { GridResult->Refresh(); }
}

bool UAsdaUpgradeSplitSubsystem::HasFreeBagCell() const
{
	// `inventoryBag_hasFreeSlot@0xB689F0`: код 0 или −1 в любой ячейке сумки (у порта — вся сумка ×2).
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return false; }
	const int32 Total = FMath::Min(Inv->GetContainerSize(1), kListMax);
	for (int32 C = 0; C < Total; ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(1, C);
		if (!It || It->ItemCode == 0 || It->ItemCode == -1) { return true; }
	}
	return false;
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

void UAsdaUpgradeSplitSubsystem::OnCellDoubleClick(int32 VisibleCell)
{
	// `uiDispatchMsgByIdTable@0xB676E0`: сброс, затем выбор записи ячейки (если есть), затем перерисовка.
	ResetSlots();
	const int32 L = Row * kCols + VisibleCell;
	if (List.IsValidIndex(L))
	{
		SelCell = List[L];
		const FAsdaItemInstance* It = Bag(SelCell);
		SelLevel = It ? (int32)It->GemGrade : 0;   // `[106] = *(u8*)(rec + 24)`
	}
	RefreshAll();
	UE_LOG(LogAsdaUpgradeSplit, Log, TEXT("[разделение улучшения] выбор: ячейка сетки %d → сумка %d, уровень %d"), VisibleCell, SelCell, SelLevel);
}

void UAsdaUpgradeSplitSubsystem::OnSplitClicked()
{
	using namespace AsdaSplitLoc;
	if (!HasFreeBagCell()) { MsgBox(Loc(kNoSpace)); return; }
	const FAsdaItemInstance* Sel = Bag(SelCell);
	if (!Sel || Sel->IsEmpty()) { return; }   // `if (v12[94])` — без выбора ничего
	CountMaterials();
	RefreshStonePanel();                       // эталон: `invCountColorMixMaterials` перерисовывает панель камней
	const int32 Level = (int32)Sel->GemGrade;
	const int32 Have = CollectStones(Level);
	if (Have >= Level)
	{
		UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return; }
		TWeakObjectPtr<UAsdaUpgradeSplitSubsystem> WThis(this);
		WM->ShowGameConfirm(Loc(kWarn), [WThis](bool bYes) { if (bYes && WThis.IsValid()) { WThis->SendRequest(); } },
			/*Gauge*/ 0.f, kAskOk, kAskNo, kAskId, /*EtalonKind*/ 1);
		UE_LOG(LogAsdaUpgradeSplit, Log, TEXT("[разделение улучшения] вопрос (1, 68): уровень %d, камней %d в %d стопках"), Level, Have, StoneCells.Num());
	}
	else
	{
		MsgBox(FormatD(Loc(kShort), Level - Have));
	}
}

void UAsdaUpgradeSplitSubsystem::OnCancelClicked()
{
	CloseWindow();
}

void UAsdaUpgradeSplitSubsystem::SendRequest()
{
	// `antiCheat_sendEquipMaskDigest@0xB664A0` читает `this+94` и `this+95…102` в момент «ОК».
	const FAsdaItemInstance* Sel = Bag(SelCell);
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Sel || Sel->IsEmpty() || !Net) { return; }
	CollectStones((int32)Sel->GemGrade);       // те же стопки, что набрала кнопка (сумка за модальным вопросом не меняется)
	TArray<TArray<uint8>> Stones;
	for (int32 C : StoneCells) { if (const FAsdaItemInstance* S = Bag(C)) { Stones.Add(S->Raw); } }
	Net->SendUpgradeExtraction6549(Stones, Sel->ItemCode, Sel->ContainerType, Sel->SlotIndex);
}

void UAsdaUpgradeSplitSubsystem::OnResult(const TArray<uint8>& Body)
{
	if (Body.Num() < 577) { return; }
	using namespace AsdaSplitLoc;
	const int32 Code = (int32)(int8)Body[0];
	switch (Code)
	{
	case 0:  MsgBox(Loc(kErrParam));  return;
	case 2:  MsgBox(Loc(kErrStone));  return;
	case 3:  MsgBox(Loc(kErrItem));   return;
	case 4:  MsgBox(Loc(kErrNotUpg)); return;
	case 5:  MsgBox(Loc(kErrShort));  return;
	case 98: MsgBox(Loc(kErrDb));     return;
	case 99: MsgBox(Loc(kErrProc));   return;
	case 6:
		if (CVarUpgradeSplitCode6Notice.GetValueOnGameThread() != 0) { MsgBox(Loc(kNoSpace)); }
		UE_LOG(LogAsdaUpgradeSplit, Warning, TEXT("[разделение улучшения] 6550 код 6 — нет свободной ячейки сумки"));
		return;
	case 1: break;
	default:
		UE_LOG(LogAsdaUpgradeSplit, Warning, TEXT("[разделение улучшения] 6550 код %d — текста в эталоне нет (оригинал молчит)"), Code);
		return;
	}
	// ── Успех запроса: надпись исхода, нотис, записи ──
	const int32 Grade = (int32)(int8)Body[575];
	const int32 Used  = (int32)(int8)Body[576];
	ShownEffect = -1;
	for (int32 K = 0; K < kEffects; ++K)
	{
		if (Effects.IsValidIndex(K) && Effects[K]) { AsdaUi::SetShownDecor(Effects[K], K == Grade); }
	}
	if (Grade >= 0 && Grade < kEffects) { ShownEffect = Grade; }
	const int32 Type = SelectedItemType();
	const bool bArmor = (Type >= 0 && Type <= 8);   // эталон: ≤ 8 — доспех, 9 и прочее — оружие
	if (Grade == 0 || Grade == 1) { MsgBox(FormatD(Loc(bArmor ? kOkArmor : kOkWeapon), Used)); }
	if (Grade == 2 || Grade == 3) { MsgBox(FormatD(Loc(bArmor ? kBadArmor : kBadWeapon), Used)); }
	// Вес поставила сеть. Записи — общей точкой модели (та же ветка «контейнер 2 — положить / 1 — очистить и положить
	//   при коде > 0», что у 6554/6552; закон `port-shared-points`), парами по два, как раскладка 119 б.
	int32 Applied = 0;
	if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		for (int32 P = 0; P < 5; ++P)
		{
			TArray<uint8> Pair; Pair.SetNumZeroed(119);
			Pair[0] = 1;
			FMemory::Memcpy(Pair.GetData() + 1,  Body.GetData() + 1 + 57 * (2 * P),     57);
			FMemory::Memcpy(Pair.GetData() + 58, Body.GetData() + 1 + 57 * (2 * P + 1), 57);
			Applied += Inv->ApplyItemOptionTransRecords(Pair, TEXT("[разделение улучшения] 6550"));
		}
	}
	// `if (!i) self[103] = &g_InventoryBag + 57 * слот` — первая запись в сумке = готовый камень.
	{
		const uint8* R0 = Body.GetData() + 1;
		const int32 Slot = AsdaBytes::RdI16(R0 + 5);
		if (Slot != -1 && R0[4] == 1) { ResultCell = Slot; }
	}
	if (bOpen) { RefreshAll(); }
	CountMaterials();
	if (bOpen) { RefreshStonePanel(); }
	UE_LOG(LogAsdaUpgradeSplit, Log, TEXT("[разделение улучшения] 6550 успех: исход %d, камней %d, записей %d, готовый камень — ячейка %d"),
		Grade, Used, Applied, ResultCell);
}

// ═════════════════════════════ МЫШЬ ═══════════════════════════════════════════════════════════

int32 UAsdaUpgradeSplitSubsystem::HoverIdNum() const
{
	for (int32 V = 0; V < kCells; ++V)
	{
		if (CellIcons.IsValidIndex(V) && CellIcons[V] && AsdaUi::IsCursorOverWidget(CellIcons[V])) { return kIdCell0 + V; }
	}
	if (IconItem && AsdaUi::IsCursorOverWidget(IconItem))     { return kIdItem; }
	if (IconStone && AsdaUi::IsCursorOverWidget(IconStone))   { return kIdStone; }
	if (IconResult && AsdaUi::IsCursorOverWidget(IconResult)) { return kIdResult; }
	return -1;
}

void UAsdaUpgradeSplitSubsystem::TickMouse()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || WM->IsPointerBlockedByTopmost(kWindowId) || WM->IsCursorOverHigherWindow(kWindowId) || !WM->IsCursorOverWindow(kWindowId))
	{
		TickTooltip(-1);
		return;
	}
	const int32 Id = HoverIdNum();
	TickTooltip(Id);   // msg 8 / 0xA
	if (!Mouse.bLmbDown || Id < kIdCell0 || Id >= kIdCell0 + kCells) { return; }
	// msg 0xD — двойной ЛКМ по ячейке (порог Windows, общий распознаватель).
	CellDbl.LegacySec = (double)GetDefault<UInputSettings>()->DoubleClickTime;
	if (CellDbl.Click(Id)) { OnCellDoubleClick(Id - kIdCell0); }
}

void UAsdaUpgradeSplitSubsystem::TickTooltip(int32 IdNum)
{
	UAsdaItemTooltipSubsystem* Tip = UAsdaItemTooltipSubsystem::BeginHoverTip(GetGameInstance(), EAsdaTipOwner::UpgradeSplit, TipId, IdNum);
	if (!Tip) { return; }
	// `itemTooltip_ShowForControlId@0xB68690`: запись под контролом.
	const FAsdaItemInstance* It = nullptr;
	if (IdNum == kIdItem)   { It = Bag(SelCell); }
	if (IdNum == kIdStone)  { It = StoneCells.Num() ? Bag(StoneCells[0]) : nullptr; }
	if (IdNum == kIdResult) { It = Bag(ResultCell); }
	if (IdNum >= kIdCell0 && IdNum < kIdCell0 + kCells)
	{
		const int32 L = Row * kCols + (IdNum - kIdCell0);
		if (List.IsValidIndex(L)) { It = Bag(List[L]); }
	}
	const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	bool bShown = false;
	if (It && !It->IsEmpty() && IT)
	{
		FAsdaSpecialItemlistRow Sp;
		if (const FAsdaItemRecord* R = IT->GetItem(It->ItemCode))
		{
			// itemType 5…7 и > 9 — простой по коду (`CControl_SetState(…, 0, код, …)`), иначе полный по записи.
			if (R->ItemType > 4 && (R->ItemType < 8 || R->ItemType > 9)) { Tip->ShowItemTooltipByCode(It->ItemCode, EAsdaTipOwner::UpgradeSplit); }
			else { Tip->ShowItemTooltip(*It, /*bSuppressCompare*/false, EAsdaTipOwner::UpgradeSplit, 1); }
			bShown = true;
		}
		else if (IT->IsSowelCode(It->ItemCode)) { Tip->ShowItemTooltipByCode(It->ItemCode, EAsdaTipOwner::UpgradeSplit); bShown = true; }
		else if (IT->GetSpecialItem(It->ItemCode, Sp)) { Tip->ShowItemTooltip(*It, false, EAsdaTipOwner::UpgradeSplit, 1); bShown = true; }
	}
	if (!bShown) { Tip->HideTooltipFor(EAsdaTipOwner::UpgradeSplit); }
	// msg 8 по ячейке сетки эталон завершает полной перерисовкой (`itemInfoPanel_RefreshAll`).
	if (IdNum >= kIdCell0 && IdNum < kIdCell0 + kCells) { RefreshAll(); }
}

// ═════════════════════════════ МЕЛОЧИ И ДИАГНОСТИКА ═══════════════════════════════════════════

void UAsdaUpgradeSplitSubsystem::MsgBox(const FString& Text) const
{
	// `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT 1500.
	if (ShownNotices.Num() >= 16) { ShownNotices.RemoveAt(0); }
	ShownNotices.Add(Text);
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Text);
}

FString UAsdaUpgradeSplitSubsystem::Loc(int32 Id) const
{
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

bool UAsdaUpgradeSplitSubsystem::IsEffectShown(int32 K) const
{
	return Effects.IsValidIndex(K) && Effects[K] && Effects[K]->GetVisibility() != ESlateVisibility::Collapsed
		&& Effects[K]->GetVisibility() != ESlateVisibility::Hidden;
}

FString UAsdaUpgradeSplitSubsystem::GetControlText(int32 IdNum) const
{
	if (IdNum >= kIdCell0 && IdNum < kIdCell0 + kCells)
	{
		const UTextBlock* Tb = CellLabels.IsValidIndex(IdNum - kIdCell0) ? CellLabels[IdNum - kIdCell0].Get() : nullptr;
		return Tb ? Tb->GetText().ToString() : FString();
	}
	const TObjectPtr<UTextBlock>* P = Texts.Find(IdNum);
	return P && *P ? (*P)->GetText().ToString() : FString();
}

FString UAsdaUpgradeSplitSubsystem::DumpState() const
{
	FString S = FString::Printf(TEXT("разделение улучшения: открыто=%s, вещей в сетке %d (ряд %d), выбор — ячейка %d (уровень %d), камней %d, "
		"готовый — ячейка %d, пустых оружия %d / доспеха %d, исход %d"),
		bOpen ? TEXT("да") : TEXT("нет"), List.Num(), Row, SelCell, SelLevel, StoneCells.Num(), ResultCell, WeaponEmpty, ArmorEmpty, ShownEffect);
	return S;
}

FString UAsdaUpgradeSplitSubsystem::DumpControls() const
{
	int32 Icons = 0, Labels = 0, Fx = 0;
	for (const TObjectPtr<UImage>& I : CellIcons) { if (I) { ++Icons; } }
	for (const TObjectPtr<UTextBlock>& T : CellLabels) { if (T) { ++Labels; } }
	for (const TObjectPtr<UWidget>& E : Effects) { if (E) { ++Fx; } }
	return FString::Printf(TEXT("[разделение улучшения] окно 50500 разобрано: ячеек %d/%d (подписей %d), подписей окна %d/%d, иконки 50516/50526/50536 %d/%d/%d, "
		"рамка 50581 %d, надписи исхода %d/%d, кнопки 50582/50583/50589 %d/%d/%d"),
		Icons, (int32)kCells, Labels, Texts.Num(), (int32)UE_ARRAY_COUNT(kTexts), IconItem ? 1 : 0, IconStone ? 1 : 0, IconResult ? 1 : 0,
		SelFrame ? 1 : 0, Fx, (int32)kEffects, BtnSplit ? 1 : 0, BtnCancel ? 1 : 0, BtnClose ? 1 : 0);
}

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GAsdaUpgradeSplitDump(
	TEXT("asda.UpgradeSplit.Dump"),
	TEXT("Состояние окна «Разделение улучшения» 50500 и разобранные контролы."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		const UAsdaUpgradeSplitSubsystem* S = GI ? GI->GetSubsystem<UAsdaUpgradeSplitSubsystem>() : nullptr;
		UE_LOG(LogAsdaUpgradeSplit, Display, TEXT("%s\n%s"), S ? *S->DumpState() : TEXT("подсистемы нет"), S ? *S->DumpControls() : TEXT(""));
	}));
#endif
