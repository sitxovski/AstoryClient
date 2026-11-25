// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна инкубатора 42000 `Pet_Hatchery_Win` (волна 3 порта питомцев).
//   Весь реверс с адресами — в шапке `UI/AsdaPetHatcherySubsystem.h`
//   и в `Reports/pet_windows_re_report.md` §2, §4.5, §6.2, §6.3.
#include "UI/AsdaPetHatcherySubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaAnimatedPlane.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Net/NetOpcodes.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetHatch, Log, All);

// ── Идентификатор окна и путь к ассету (реестр окон: `AsdaWindowSubsystem.cpp`, idnum 42000) ──
static const FName  kWinId(TEXT("Pet_Hatchery_Win"));
static const TCHAR* kWinWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_Hatchery_Win.WBP_Pet_Hatchery_Win_C");

// ══════════════════════════════════════════════════════════════════════════════════════════════
//  LOC-ИДЕНТИФИКАТОРЫ. Каждый снят по СВОЕМУ обработчику эталона (закон
//  `loc-per-handler-not-by-family`), файл:строка — рядом с каждой строкой.
//  Полная таблица «событие → loc → канал» — в шапке `.h`.
// ══════════════════════════════════════════════════════════════════════════════════════════════
namespace PetHatchLoc
{
	// Подписи окна — `petWindowBuildUI@0xDBFF70` (npc_mob_pet.c:4028-4069)
	constexpr int32 kTitle     = 100400;  // :4031  42001 «Инкубатор питомца»
	constexpr int32 kGuide0    = 100500;  // :4041  42041 стартовая подсказка
	constexpr int32 kEggTab    = 100404;  // :4045  42040 «Выбери яйцо питомца»
	constexpr int32 kToolTab   = 100407;  // :4050  42090 «Бустер»
	constexpr int32 kBtnFeed   = 100405;  // :4055  42098 «Вывести»
	constexpr int32 kBtnCancel = 100406;  // :4058  42099 «отмена»

	// Подсказка 42041 по тику 2000 мс — `CPetWindow_Update@0xDC08A0`
	constexpr int32 kGuideNoEgg   = 100500;  // :4211  яйцо не выбрано
	constexpr int32 kGuideEggOnly = 100501;  // :4216  яйцо есть, бустера нет
	constexpr int32 kGuideBoth    = 100502;  // :4221  яйцо и бустер есть

	// Гейт одноразовости — `petWindowUpdateFeed:4325` и `petWindowOnMessage:4415`
	constexpr int32 kOneShot = 101119;

	// Приём 6103 — `CPetWin_OnResult@0xDC2CA0`
	constexpr int32 kHatchOk      = 101106;  // :5093  код 1
	constexpr int32 kNoPetSlots   = 100430;  // :5115  код 2
	constexpr int32 kCatalystErr  = 100431;  // :5120  коды 3 и 7
	constexpr int32 kEggErr       = 100432;  // :5125  коды 4 и 8
	constexpr int32 kBoosterErr   = 100433;  // :5129  код 5
	constexpr int32 kHatchFail    = 101107;  // :5133  код 9

	// Подпись «ОК» однокнопочного нотиса
	constexpr int32 kBtnOk = 2288;
}

// Звуки окна (`CSoundFlag_PlaySound2D`, имена — дословно из эталона).
namespace PetHatchWav
{
	static const TCHAR* kClose   = TEXT("S_e_pet_close");    // :4257  створки закрылись
	static const TCHAR* kShit    = TEXT("S_e_pet_shit");     // :4290  пошёл пар
	static const TCHAR* kSuccess = TEXT("S_e_pet_success");  // :5092  код 1
	static const TCHAR* kFailure = TEXT("S_e_pet_failure");  // :5152  код 9
	static const TCHAR* kOpen    = TEXT("S_e_pet_open");     // :5161  эпилог любого кода
}

// ═════════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════

void UAsdaPetHatcherySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	EggCells.SetNum(kGridCells);
	ToolCells.SetNum(kGridCells);
	// Страницы сеток — массивы ФИКСИРОВАННОЙ длины 6; только `Init`, никогда `Reset`
	//   (закон `tarray-reset-breaks-fixed-length`).
	EggPage.Init(FAsdaHatchItemRef(), kGridCells);
	ToolPage.Init(FAsdaHatchItemRef(), kGridCells);

	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// Событие 21 эталона (`petWindow_MsgProc` case 0x15 → `petWindow_Close`, :4855-4857).
		WM->RegisterEscCloser(kWinId, FSimpleDelegate::CreateUObject(this, &UAsdaPetHatcherySubsystem::CloseHatchery));
	}
	if (UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ событие в очередь.
		HPetEvent = P->OnPetEvent.AddLambda([this](int32 Op, int32 Res, int32 Uid)
		{
			PetEvents.Add(TTuple<int32, int32, int32>(Op, Res, Uid));
		});
	}
	UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] подсистема поднята (окно 42000)"));
}

void UAsdaPetHatcherySubsystem::Deinitialize()
{
	if (UAsdaPetSubsystem* P = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
	{
		P->OnPetEvent.Remove(HPetEvent);
	}
	ResetWindowWidget();
	Super::Deinitialize();
}

void UAsdaPetHatcherySubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ★ Окно инкубатора привязано к КОНКРЕТНОЙ ячейке катализатора. После пересоздания сокета
	//   (и после смены карты — сервер перевыдаёт инвентарь) номер ячейки уже ничего не значит,
	//   поэтому окно закрывается целиком, а не «сохраняет выбор».
	CloseHatchery();
	bInvDirty  = false;
	FeedState  = 0;
	FeedLatch.Disarm();   // ★ волна 8: засов ожидания 6103 — вещь СЕССИИ
	AnimState  = 0;
	ResultCode = 0;
	ResultModel = -1;
	FeedDurationMs = 0;
	FeedStartTime = 0.0;
	AnimStepTime = 0.0;
	GuideLeft = 0.f;
	GuideShownLoc = -1;
	Catalyst.Clear();
	Egg.Clear();
	Booster.Clear();
	EggList.Reset();          // реестры переменной длины — `Reset` на них штатен
	ToolList.Reset();
	EggPage.Init(FAsdaHatchItemRef(), kGridCells);
	ToolPage.Init(FAsdaHatchItemRef(), kGridCells);
	EggPageIndex = ToolPageIndex = 0;
	EggPageCount = ToolPageCount = 0;
	TipId = -1;
	Press.Clear();
	PetEvents.Reset();
	Mouse.Reset();
	(void)Reason;
}

void UAsdaPetHatcherySubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.PetHatchery");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPetEvents();

	// ★★★ ВОЛНА 8 (пункт 1 ревизии): ТАЙМАУТ ОЖИДАНИЯ 6103. Крутится ДО гейта «окно активно»:
	//   окно могли закрыть с висящим запросом, и засов не должен пережить закрытие.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, S2): общая точка `FAsdaReplyLatch` — срок стенными часами, ключ читается на тике (было `Tick(DeltaTime)` — игровой тик).
	if (FeedLatch.TickTimeout(AsdaPetUi::SendTimeoutSec()))
	{
		if (FeedState == 3)
		{
			ReturnFeedToReady(*FString::Printf(
				ASDA_DEV_TEXT(TEXT("ответа 6103 нет %.0f с (ключ asda.Pet.SendTimeoutSec)"), TEXT("ответа 6103 нет %.0f с")), AsdaPetUi::SendTimeoutSec()));
		}
	}

	if (!bActive || !bReady) { return; }

	Mouse.Update();
	TickWindow(DeltaTime);
	TickMouse();
	TickTooltips();
}

// ═══════════════════════════ ЗАГРУЗКА И РАЗБОР ОКНА ═══════════════════════════════════════════

bool UAsdaPetHatcherySubsystem::EnsureWindow()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWinId, kWinWbp, kHatcheryZ).Ensure(
		GetGameInstance(), Win, bReady,
		[this](UAsdaWindowSubsystem&) { ResetWindowWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			// Зона окна — девять планов рамки `Pet_Hatchery_Win_0_0 … _0_8` (дамп, idx 0).
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kWinId, this, Win, bActive,
				TEXT("Pet_Hatchery_Win_0_%d"), 9);
		},
		[]()
		{
			UE_LOG(LogAsdaPetHatch, Error, TEXT("[инкубатор] WBP_Pet_Hatchery_Win не загрузился (%s)"), kWinWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

void UAsdaPetHatcherySubsystem::ResetWindowWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWinId);
		WM->CloseWindow(kWinId);
	}
	Win = nullptr;
	TxtTitle = TxtEggName = TxtEggSlot = TxtToolSlot = TxtGradeTxt = nullptr;
	TxtEggTab = TxtGuide = TxtToolTab = TxtFeed = TxtCancel = nullptr;
	BtnClose = BtnFeed = BtnCancel = nullptr;
	ImgEggSlot = ImgToolSlot = BarTrack = BarFill = nullptr;
	Door0a = Door0b = Door1a = Door1b = DoneIcon = SuccessGlow = Steam = nullptr;
	BgNormal = BgPremium = EggSelFrame = ToolSelFrame = nullptr;
	for (FAsdaHatchCell& C : EggCells)  { C = FAsdaHatchCell(); }
	for (FAsdaHatchCell& C : ToolCells) { C = FAsdaHatchCell(); }
	Relays.Reset();
	BarBaseLeft = 0.f;
	BarFullW    = 0.f;
	bReady = false;
	TipId  = -1;
	Press.Clear();
	Mouse.Reset();
}

// Разбор ВСЕХ контролов окна (закон `full-structural-analysis-always`).
//   ★ АДРЕСАЦИЯ ПО НОМЕРУ, А НЕ ПО ИМЕНИ СКИНА. Имена в `.JUI` — это имена СКИНОВ
//   (`NoImage_icon`, `TabBtn_Img_M`, `Slot_AniSelect`), они повторяются десятки раз. Карта
//   «номер JUI → имя виджета в ассете» снята живым дампом путём игрока
//   (`Reports/pet/ui_dump/42000_Pet_Hatchery_Win.md` и `_ID_CHECK.md`) и зафиксирована здесь.
//   Коллизий имён внутри 42000 НЕТ: все 88 узлов эталона нашлись в ассете 1:1, лишних 0.
void UAsdaPetHatcherySubsystem::SetupControls()
{
	bReady = false;
	if (!Win || !Win->WidgetTree) { return; }

	TArray<UWidget*> All;
	Win->WidgetTree->GetAllWidgets(All);

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("Pet_Hatchery_ClsBtn")) { BtnClose  = B; continue; }   // 42002
			if (N == TEXT("PetHatchery_GOBtn"))   { BtnFeed   = B; continue; }   // 42098
			if (N == TEXT("PetHatchery_CanBtn"))  { BtnCancel = B; continue; }   // 42099
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Pet_Hatchery_Title"))          { TxtTitle    = T; continue; }  // 42001
			if (N == TEXT("Pet_Hatchery_EggName"))        { TxtEggName  = T; continue; }  // 42011
			if (N == TEXT("Pet_Hatchery_EggIcon"))        { TxtEggSlot  = T; continue; }  // 42010 подпись
			if (N == TEXT("Pet_Hatchery_UsedItem"))       { TxtToolSlot = T; continue; }  // 42012 подпись
			if (N == TEXT("Pet_Hatchery_GradeTxt"))       { TxtGradeTxt = T; continue; }  // 42013 (эталон не пишет)
			if (N == TEXT("PetHatchery_EggSelTitle_Text")){ TxtEggTab   = T; continue; }  // 42040
			if (N == TEXT("PetHatchery_Guide"))           { TxtGuide    = T; continue; }  // 42041
			if (N == TEXT("PetHatchery_ItemTitle_Text"))  { TxtToolTab  = T; continue; }  // 42090
			if (N == TEXT("PetHatchery_GOBtn_Text"))      { TxtFeed     = T; continue; }  // 42098 подпись
			if (N == TEXT("PetHatchery_CanBtn_Text"))     { TxtCancel   = T; continue; }  // 42099 подпись
			// Подписи ячеек сеток: `PetHatchery_Egg0N` / `PetHatchery_Item0N` (в оригинале пустые).
			bool bTaken = false;
			for (int32 i = 0; i < kGridCells && !bTaken; ++i)
			{
				if (N == FString::Printf(TEXT("PetHatchery_Egg%02d"),  i)) { EggCells[i].Label  = T; bTaken = true; }
				if (N == FString::Printf(TEXT("PetHatchery_Item%02d"), i)) { ToolCells[i].Label = T; bTaken = true; }
			}
			continue;
		}

		// Группа планов пара 42026 — у неё видимостью управляет именно КАНВАС `_grp`
		//   (в нём лежат четыре плана `Steam_Small`), а одноимённый `TextBlock` — подпись узла.
		if (N == TEXT("Pet_Hatchery_Steam_grp")) { Steam = W; continue; }

		// Планы: `{idc}_{juiIdx}_{plane}`.
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx)) { continue; }

		// Рамки выбора и анимированные планы — это `UAsdaAnimatedPlane` (подкласс `UImage`),
		//   поэтому их надо разобрать ДО общей ветки `UImage`.
		if (Idc == TEXT("PetHatchery_EggSel"))            { EggSelFrame  = W; continue; }   // 42059
		if (Idc == TEXT("PetHatchery_ItemSel"))           { ToolSelFrame = W; continue; }   // 42079
		if (Idc == TEXT("Pet_Hatchery_ClickAni"))         { DoneIcon     = W; continue; }   // 42022
		if (Idc == TEXT("Pet_Hatchery_SuccessEffect"))    { SuccessGlow  = W; continue; }   // 42024
		if (Idc == TEXT("Pet_Hatchery_Door1"))            { Door0a       = W; continue; }   // 42020
		if (Idc == TEXT("Pet_Hatchery_Door2"))            { Door0b       = W; continue; }   // 42021
		if (Idc == TEXT("Pet_PHatchery_Door1"))           { Door1a       = W; continue; }   // 42032
		if (Idc == TEXT("Pet_PHatchery_Door2"))           { Door1b       = W; continue; }   // 42033
		if (Idc == TEXT("Pet_Hatchery_BaseBG"))           { BgNormal     = W; continue; }   // 42030
		if (Idc == TEXT("Pet_PHatchery_BaseBG"))          { BgPremium    = W; continue; }   // 42031

		if (UImage* Img = Cast<UImage>(W))
		{
			if (Idc == TEXT("Pet_Hatchery_EggIcon"))  { ImgEggSlot  = Img; continue; }      // 42010 иконка
			if (Idc == TEXT("Pet_Hatchery_UsedItem")) { ImgToolSlot = Img; continue; }      // 42012 иконка
			// 42015 — полоса `Soul_Violet_Bar`: план 0 жёлоб, план 1 заливка.
			if (Idc == TEXT("Pet_Hatchery_ProBar"))
			{
				(PlaneIdx == 0 ? BarTrack : BarFill) = Img;
				continue;
			}
			for (int32 i = 0; i < kGridCells; ++i)
			{
				if (Idc == FString::Printf(TEXT("PetHatchery_Egg%02d"),  i)) { EggCells[i].Icon  = Img; break; }
				if (Idc == FString::Printf(TEXT("PetHatchery_Item%02d"), i)) { ToolCells[i].Icon = Img; break; }
			}
		}
	}

	// Базовая геометрия полосы 42015 снимается с ЖЁЛОБА один раз (как у всех полос порта).
	if (BarTrack)
	{
		FVector2D Pos, Size;
		if (AsdaUi::SlotRect(BarTrack, Pos, Size))
		{
			BarBaseLeft = (float)Pos.X;
			BarFullW    = (float)Size.X;
		}
	}

	// ── Подписи: ровно те же loc, что ставит `petWindowBuildUI` (:4031-4058) ──
	if (TxtTitle)   { TxtTitle->SetText(FText::FromString(Loc(PetHatchLoc::kTitle))); }
	if (TxtGuide)   { TxtGuide->SetText(FText::FromString(Loc(PetHatchLoc::kGuide0))); GuideShownLoc = PetHatchLoc::kGuide0; }
	if (TxtEggTab)  { TxtEggTab->SetText(FText::FromString(Loc(PetHatchLoc::kEggTab))); }
	if (TxtToolTab) { TxtToolTab->SetText(FText::FromString(Loc(PetHatchLoc::kToolTab))); }
	if (TxtFeed)    { TxtFeed->SetText(FText::FromString(Loc(PetHatchLoc::kBtnFeed))); }
	if (TxtCancel)  { TxtCancel->SetText(FText::FromString(Loc(PetHatchLoc::kBtnCancel))); }
	// 42012 эталон очищает прямо в `petWindowBuildUI` (:4036 `CControl_SetText(pIconTool, &Src)`).
	if (TxtToolSlot) { TxtToolSlot->SetText(FText::GetEmpty()); }
	if (TxtEggSlot)  { TxtEggSlot->SetText(FText::GetEmpty()); }
	// ★ 42013 `Pet_Hatchery_GradeTxt`: эталон в него НЕ пишет НИЧЕГО (ссылок в разобранном срезе
	//   нет — ни по номеру, ни по idc). Оставляем как есть в ассете; текст не трогаем.

	// ── Кнопки. Это единственные три настоящих `UButton` окна (дамп: 42002, 42098, 42099) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	// 42002 — крестик. Эталон ловит его событием 0xB в `petWindowOnMessage` (:4417-4418),
	//   то есть гейт одноразовости отрабатывает РАНЬШЕ (пролог той же функции).
	Bind(BtnClose, 42002, [this](int32) { if (!ConsumeOneShotGate()) { CloseHatchery(); } });
	// 42098 «Вывести» — команда окна 0x10401 → `petWindowFeedMsgHandler` (:4384-4386).
	Bind(BtnFeed, 42098, [this](int32) { OnFeedButton(); });
	// 42099 «отмена» — та же команда; закрывает ТОЛЬКО при `feedState == 0` (:4391-4394).
	Bind(BtnCancel, 42099, [this](int32)
	{
		if (FeedState != 0)
		{
			UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] 42099: автомат в состоянии %d — эталон отмену игнорирует"), FeedState);
			return;
		}
		CloseHatchery();
	});

	bReady = (EggCells[0].Icon != nullptr && ToolCells[0].Icon != nullptr && BtnFeed != nullptr);
	UE_LOG(LogAsdaPetHatch, Log,
		TEXT("[инкубатор] 42000 разобрано: яиц-ячеек %d, бустер-ячеек %d, рамки 42059/42079 %s/%s, ")
		TEXT("полоса 42015 %s, двери 42020/42021/42032/42033 %s/%s/%s/%s, 42022 %s, 42024 %s, ")
		TEXT("пар 42026 %s, фоны 42030/42031 %s/%s, 42013 %s"),
		EggCells.Num(), ToolCells.Num(),
		EggSelFrame ? TEXT("есть") : TEXT("НЕТ"), ToolSelFrame ? TEXT("есть") : TEXT("НЕТ"),
		(BarTrack && BarFill) ? TEXT("есть") : TEXT("НЕТ"),
		Door0a ? TEXT("есть") : TEXT("НЕТ"), Door0b ? TEXT("есть") : TEXT("НЕТ"),
		Door1a ? TEXT("есть") : TEXT("НЕТ"), Door1b ? TEXT("есть") : TEXT("НЕТ"),
		DoneIcon ? TEXT("есть") : TEXT("НЕТ"), SuccessGlow ? TEXT("есть") : TEXT("НЕТ"),
		Steam ? TEXT("есть") : TEXT("НЕТ"),
		BgNormal ? TEXT("есть") : TEXT("НЕТ"), BgPremium ? TEXT("есть") : TEXT("НЕТ"),
		TxtGradeTxt ? TEXT("есть") : TEXT("НЕТ"));
}

// ═══════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ══════════════════════════════════════════════

bool UAsdaPetHatcherySubsystem::OpenHatchery(int32 CatalystContainer, int32 CatalystCell, bool bPremiumMood)
{
	if (!EnsureWindow()) { return false; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	// ★ ТУМБЛЕРА НЕТ: `if (LOBYTE(this->bActive) != 1)` — окно уже открыто ⇒ выход БЕЗ действий
	//   (npc_mob_pet.c:4088). Второй катализатор окно не переоткрывает и не закрывает.
	if (bActive)
	{
		UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] окно уже открыто — эталон повторное открытие игнорирует"));
		return false;
	}

	// ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (КРИТ-2): ВЫХОД ИЗ СОСТОЯНИЯ 3 ПРИ ОТКРЫТИИ ОКНА.
	//   ЧТО БЫЛО НЕ ТАК. Засов `FeedLatch` снимался четырьмя путями из заявленных в реестре:
	//   сброс сессии, таймаут, приход 6103 — но НЕ открытием и НЕ закрытием окна (в отличие от
	//   43500 и 44200, где это сделано). При разрешённом откате `asda.Pet.SendTimeoutSec 0`
	//   таймаута нет вовсе, и автомат, застрявший в состоянии 3 («запрос в сети»), не выходил
	//   из него НИКОГДА: 42099 игнорируется, сетки заблокированы, 42022 спрятан.
	//   ПОЧЕМУ ИМЕННО ТАК, А НЕ «ЧИСТИТЬ ВСЕГДА». `petWindow_Close` и `petWindowOpen` эталона
	//   `feedState` НЕ ТРОГАЮТ (npc_mob_pet.c:4088-4097 и :4136-4142) — это воспроизводится 1:1,
	//   и слепая чистка при открытии позволила бы игроку отправить ВТОРОЙ 6102, пока первый
	//   честно летит. Поэтому условие узкое: возвращаем автомат в 2, только если ждать УЖЕ НЕЧЕГО —
	//   засов не взведён ЛИБО взведён без живого таймаута (тот самый случай отката ключом).
	// ★ S2 (В2-A): «живой таймаут» = ключ `asda.Pet.SendTimeoutSec` > 0 сейчас (прежде — остаток `Left`, зафиксированный взводом).
	if (FeedState == 3 && !(FeedLatch.IsArmed() && AsdaPetUi::SendTimeoutSec() > 0.f))
	{
		ReturnFeedToReady(TEXT("окно 42000 открыто заново, а ждать ответа 6103 уже нечем "
		                       "(таймаут выключен ключом asda.Pet.SendTimeoutSec 0 либо засов снят)"));
	}

	// `petWindowOpen` (:4090-4097): ветка оформления, катализатор, признаки, сброс выбора.
	MoodBranch = bPremiumMood ? 1 : 0;
	Catalyst.Container = CatalystContainer;
	Catalyst.Cell      = CatalystCell;
	Catalyst.ItemCode  = CellCode(CatalystContainer, CatalystCell);
	bActive     = true;
	bInvDirty   = false;
	Egg.Clear();
	Booster.Clear();
	ResultCode  = 0;
	ResultModel = -1;

	WM->SetWindowVisible(kWinId, true);

	// Ветка оформления (:4099-4119). Обычная: фон 42030 + дверь 42020; премиум: 42031 + 42032.
	if (MoodBranch == 0)
	{
		AsdaUi::SetShownDecor(BgNormal,  true);
		AsdaUi::SetShownDecor(BgPremium, false);
		AsdaUi::SetShownDecor(Door0a,    true);
		AsdaUi::SetShownDecor(Door0b,    false);
		AsdaUi::SetShownDecor(Door1a,    false);
		AsdaUi::SetShownDecor(Door1b,    false);
	}
	else
	{
		AsdaUi::SetShownDecor(BgNormal,  false);
		AsdaUi::SetShownDecor(BgPremium, true);
		AsdaUi::SetShownDecor(Door0a,    false);
		AsdaUi::SetShownDecor(Door0b,    false);
		AsdaUi::SetShownDecor(Door1a,    true);
		AsdaUi::SetShownDecor(Door1b,    false);
	}
	// Пар, «готово» и свечение гасятся безусловно (:4120-4125).
	AsdaUi::SetShownDecor(Steam,       false);
	AsdaUi::SetShownDecor(DoneIcon,    false);
	AsdaUi::SetShownDecor(SuccessGlow, false);

	// `CClass_MapInsert(g_pWindowMgr, pRootWindow)` (:4126) — поднять окно наверх стопки.
	WM->BringWindowToFront(kWinId);

	// Подсказка пересчитывается ближайшим тиком; счётчик сбрасываем, чтобы это случилось сразу.
	GuideLeft = 0.f;
	Press.Clear();
	Mouse.Reset();
	RefreshUI();

	UE_LOG(LogAsdaPetHatch, Log,
		TEXT("[инкубатор] 42000 открыто: катализатор (контейнер %d, ячейка %d, код %d), ")
		TEXT("оформление %s, яиц в списке %d, бустеров %d"),
		Catalyst.Container, Catalyst.Cell, Catalyst.ItemCode,
		MoodBranch == 1 ? TEXT("ПРЕМИУМ (42031)") : TEXT("обычное (42030)"),
		EggList.Num(), ToolList.Num());
	return true;
}

void UAsdaPetHatcherySubsystem::CloseHatchery()
{
	if (!bActive) { return; }
	bActive = false;
	TipId = -1;
	Press.Clear();
	Mouse.Reset();
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::PetHatchery);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWinId, false);
	}
	// ⚠ `petWindow_Close` НЕ трогает ни `feedState`, ни выбранные слоты, ни `animState`
	//   (:4136-4142 — только признак и видимость). Воспроизводим 1:1: повторное открытие
	//   обнулит выбор само (`petWindowOpen`), а автомат доживёт до ответа сервера.
	// ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (КРИТ-2): ЧТО ДЕЛАЕТСЯ С ЗАСОВОМ ПРИ ЗАКРЫТИИ — ЯВНО.
	//   Засов `FeedLatch` — НАША вещь, не эталонная, и закрытие окна его НЕ снимает СОЗНАТЕЛЬНО:
	//   он единственный, кто может вернуть автомат из состояния 3, а `Tick` крутит его ДО гейта
	//   «окно активно» именно затем, чтобы спасение сработало и у закрытого окна.
	//   НО если таймаута нет (ключ `asda.Pet.SendTimeoutSec 0`), взведённый засов — пустой флаг:
	//   он ничего не сторожит и только мешает `OpenHatchery` понять, что ждать нечего. Снимаем.
	if (FeedLatch.IsArmed() && AsdaPetUi::SendTimeoutSec() <= 0.f)   // ★ S2 (В2-A): засов без таймаута — по ключу сейчас
	{
		FeedLatch.Disarm();
		UE_LOG(LogAsdaPetHatch, Warning,
			TEXT("[инкубатор] 42000 закрыто с засовом БЕЗ таймаута (asda.Pet.SendTimeoutSec 0) — ")
			TEXT("засов снят: сторожить им нечего. Автомат останется в %d, и следующее открытие ")
			TEXT("вернёт его в состояние 2."), FeedState);
	}
	UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] 42000 закрыто (автомат %d, засов=%s, одноразовость %s)"),
		FeedState,
		FeedLatch.IsArmed() ? *FString::Printf(TEXT("взведён, %.1f с"), FeedLatch.RemainingSec(AsdaPetUi::SendTimeoutSec())) : TEXT("снят"),
		bInvDirty ? TEXT("взведена") : TEXT("нет"));
}

// ═════════════════════════════ ПОКАДРОВЫЙ АВТОМАТ ═════════════════════════════════════════════

void UAsdaPetHatcherySubsystem::TickWindow(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();

	// ── Подсказка 42041 раз в 2000 мс (:4206-4223) ──
	GuideLeft -= DeltaSeconds;
	if (GuideLeft <= 0.f)
	{
		GuideLeft = kGuidePollSec;
		// Эталон печатает подсказку в той же последовательности трёх независимых `if`:
		//   нет яйца → 100500; яйцо есть, бустера нет → 100501; оба есть → 100502.
		const int32 WantLoc = !Egg.IsSet()       ? PetHatchLoc::kGuideNoEgg
		                    : (!Booster.IsSet()  ? PetHatchLoc::kGuideEggOnly
		                                         : PetHatchLoc::kGuideBoth);
		if (TxtGuide && WantLoc != GuideShownLoc)
		{
			TxtGuide->SetText(FText::FromString(Loc(WantLoc)));
			GuideShownLoc = WantLoc;
		}
	}

	// ── Анимация створок и пара (:4224-4295) ──
	UWidget* const DoorA = (MoodBranch == 0) ? Door0a.Get() : Door1a.Get();
	UWidget* const DoorB = (MoodBranch == 0) ? Door0b.Get() : Door1b.Get();
	switch (AnimState)
	{
		case 0:
			// Створки спрятаны, пар спрятан, отсчёт обнулён (:4224-4240).
			AsdaUi::SetShownDecor(DoorA, false);
			AsdaUi::SetShownDecor(DoorB, false);
			AnimStepTime = Now;
			AsdaUi::SetShownDecor(Steam, false);
			break;
		case 1:
			// Створка A видна; через 500 мс — состояние 2 и звук закрытия (:4241-4259).
			AsdaUi::SetShownDecor(DoorA, true);
			AsdaUi::SetShownDecor(DoorB, false);
			if (Now - AnimStepTime > (double)kDoorStepSec)
			{
				AnimStepTime = Now;
				AnimState = 2;
				PlayWav(PetHatchWav::kClose);
			}
			break;
		case 2:
			// Створка B видна (закрыто); отсчёт держится обнулённым (:4260-4273).
			AsdaUi::SetShownDecor(DoorA, false);
			AsdaUi::SetShownDecor(DoorB, true);
			AnimStepTime = Now;
			break;
		case 3:
			// Створка A видна; через 500 мс — состояние 0, звук и ПАР (:4274-4295).
			AsdaUi::SetShownDecor(DoorA, true);
			AsdaUi::SetShownDecor(DoorB, false);
			if (Now - AnimStepTime > (double)kDoorStepSec)
			{
				AnimStepTime = Now;
				AnimState = 0;
				PlayWav(PetHatchWav::kShit);
				AsdaUi::SetShownDecor(Steam, true);
			}
			break;
		default:
			break;
	}

	// ── Полоса прогресса и переход 1 → 2 (:4296-4307) ──
	if (FeedState == 1)
	{
		const double Elapsed = (Now - FeedStartTime) * 1000.0;
		const float Frac = (FeedDurationMs > 0) ? (float)FMath::Clamp(Elapsed / (double)FeedDurationMs, 0.0, 1.0) : 0.f;
		FAsdaGaugeFill::Clip(BarFill, BarBaseLeft, BarFullW, Frac);
		if (Elapsed > (double)FeedDurationMs)
		{
			FeedState = 2;
			AsdaUi::SetShownDecor(DoneIcon, true);
			// Анимированный план «готово» прокручивается заново при каждом показе.
			if (UAsdaAnimatedPlane* Ani = Cast<UAsdaAnimatedPlane>(DoneIcon.Get())) { Ani->RestartAnim(); }
			UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] таймер вышел (%d мс) — показан 42022, ждём клика"), FeedDurationMs);
		}
	}
}

// ═════════════════════════════════ МЫШЬ ═══════════════════════════════════════════════════════

void UAsdaPetHatcherySubsystem::TickMouse()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// Чужой всплывающий слой перехватывает клик первым.
		if (WM->IsPointerBlockedByTopmost(kWinId)) { Press.Clear(); return; }
		// ★★★ МОДАЛЬНЫЙ ГЕЙТ. Эталонный `uiIsAnyModalWindowOpen@0xB79520`: пока открыт модальный
		//   нотис (1500 `ONEBUT_MSGBOX`), захваченный контрол забирает ВЕСЬ диспетч мыши, и нижние
		//   окна событий не получают вовсе. Без этой строки окно ловило бы клики сквозь
		//   собственный нотис — и живой прогон 21.09 это показал: пока висела плашка «Питомец
		//   успешно вылупился», кнопка 42098 (настоящий `UButton`) клик не получала, а опрос
		//   курсора его засчитывал. Расхождение снято в пользу эталона.
		// ★ Дедуп №2, В1-B (U9, 11.10.2026): общая точка модального гейта (ключ отката `asda.UI.ModalGatePolling`).
		if (AsdaUi::IsModalDispatchBlocked(GetGameInstance())) { Press.Clear(); return; }
	}
	// ★★★ КАКОЕ СОБЫТИЕ JUI ЗДЕСЬ ВОСПРОИЗВОДИТСЯ. Эталон разбирает ВСЕ клики окна в
	//   `petWindowOnMessage@0xDC1020`, которую `petWindow_MsgProc` зовёт на событии **11**
	//   (npc_mob_pet.c:4861-4862).
	//   ★ 11 — ЭТО ОБЫЧНОЕ НАЖАТИЕ ЛКМ, А НЕ ДВОЙНОЙ КЛИК. Имя `CControl_OnLButtonDblClick`
	//   (`ui_core.c:107474`) — МИСНЕЙМ. Доказательство прямое, не по имени:
	//     • событие 11 поднимает только `CControl_OnLButtonDblClick`, и она же ЕДИНСТВЕННАЯ во
	//       всём бинаре ставит флаг `0x400` (`ui_core.c:107486`);
	//     • `CControl_OnLButtonDown@0x12DB870` ОТКАЗЫВАЕТСЯ работать без `0x400`
	//       (`ui_core.c:107281`) и только потом поднимает 5 и 6 (`:107294-107295`);
	//     ⇒ 11 обязано срабатывать ПЕРЕД КАЖДЫМ обычным нажатием, иначе нажатий не было бы вовсе.
	//   Второй, независимый метод — состав веток самой `petWindowOnMessage`: в ней сидит КРЕСТИК
	//   ОКНА 42002 (:4417-4418). Крестик, закрывающий окно только по двойному клику, — не
	//   поведение оригинала ни в одном другом окне.
	//   ★ Для полноты: НАСТОЯЩИЙ двойной клик — это событие **13** (`CControl_OnKeyDown`, тоже
	//   миснейм: его поднимает второй насос ввода, `AsdaStory_game.c:987382`). Именно на 13
	//   инвентарь зовёт `CInventory_OnSlotUse` (`ui_core.c:48000`), то есть «применить предмет»,
	//   а на 11 — `CInventoryUI_OnSlotClick`, то есть «выделить». Если бы номера стояли наоборот,
	//   одиночный клик по сумке применял бы предмет. Отсюда и точка входа в это окно: катализатор
	//   107 открывает инкубатор ДВОЙНЫМ кликом (событие 13), а внутри окна всё — одиночные (11).
	//
	// ★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ: эталон действует на НАЖАТИИ, мы — на отпускании НАД ТЕМ ЖЕ узлом.
	//   Причина конкретная: окно открывается двойным кликом по сумке, и в кадре открытия кнопка
	//   мыши ещё зажата. Сработай мы на нажатии — первый же кадр отдал бы «клик» по узлу, который
	//   случайно оказался под курсором. Пара «нажал над узлом + отпустил над ним же» это снимает
	//   и совпадает с тем, как мышь опрашивают остальные окна порта.
	if (Mouse.bLmbDown)
	{
		// ★ ВОЛНА 8 (пункт 6 ревизии): общая точка `FAsdaPressLatch` — флаг `0x10` эталона.
		// ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (КРИТ-1): ТОЧКА КУРСОРА ЗАПОМИНАЕТСЯ. Без неё
		//   `ReleaseOverOrStill` не сможет отличить «геометрии нет, но курсор стоял на месте»
		//   от «курсор ушёл», и запасной путь не сработает.
		Press.Press(HitTestNode(), -1, Mouse.Cursor);
		return;
	}
	if (!Mouse.bLmbUp) { return; }
	// СО СВЕРКОЙ: клик засчитывается, только если отпустили НАД ТЕМ ЖЕ узлом (флаг `0x20`).
	// ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (КРИТ-1): ОКНО 42000 БОЛЕЛО ТЕМ ЖЕ, ЧТО И 43000.
	//   БЫЛО `Press.ReleaseOver(HitTestNode())` — сверка по СВЕЖЕМУ хит-тесту кадра отпускания.
	//   Механизм болезни ОБЩИЙ и от окна не зависит: `UAsdaWindowSubsystem` при нажатии в зону
	//   окна, у которого есть ручка перетаскивания `_DRAG`, ставит `PendingRaiseWindowId`
	//   (`AsdaWindowSubsystem.cpp:1282`) и поднимает окно НА ОТПУСКАНИИ (`:1161-1165`);
	//   `SetWindowZOrder` перевешивает окно во вьюпорте, и в ЭТОМ ЖЕ кадре у виджетов нет
	//   кэшированной геометрии — `IsCursorOverWidget` врёт, `HitTestNode` отдаёт −1.
	//   `BringWindowToFront` срабатывает, только если окно ещё НЕ наверху (`:1063`), поэтому
	//   страдает ровно ПЕРВЫЙ клик по телу окна. Ручка у 42000 есть: `PetHatchery_Move_85_DRAG`
	//   (живой дамп `42000_Pet_Hatchery_Win.md:98`).
	//   ВОСПРОИЗВЕДЕНО настоящей мышью Slate (`Reports/pet/v8/game_hatch_click_BEFORE.log`):
	//     • 1-й клик (723,430) по `PetHatchery_Egg01` — «ОТПУСКАНИЕ» :4150 и в ТОМ ЖЕ кадре 680
	//       «[z-порядок] «Pet_Hatchery_Win»: z 736 → 746 (перевешено в вьюпорте)» :4151 ⇒ состояние
	//       после клика «яйцо: контейнер 0 ячейка −1 код 0» — КЛИК ПОТЕРЯН;
	//     • 2-й клик (756,430) — перевешивания в кадре отпускания уже нет ⇒ «яйцо выбрано:
	//       ячейка сетки 2 → контейнер 2, ячейка 3».
	//   СТАЛО: `ReleaseOverOrStill` — свежий хит-тест, если он валиден (этого и требует эталон:
	//   `CControl_OnRButtonUp`/`OnLButtonUp` не работают при `a2 == -1`, `ui_core.c:107391`),
	//   а при −1 — защёлка, но ТОЛЬКО если курсор не уходил дальше порога от точки нажатия.
	const int32 Down = Press.ReleaseOverOrStill(HitTestNode(), Mouse.Cursor);
	if (Down < 0) { return; }

	//   ПОРЯДОК ВЕТОК — как в эталоне (:4405-4427): пролог-гейт, сетки, слоты, крестик, «готово».
	if (ConsumeOneShotGate()) { return; }
	ConsoleClick(Down);
}

// Что сейчас под курсором: номер JUI либо −1.
// ★★★ ПОРЯДОК ЗДЕСЬ — НЕ ПОРЯДОК ВЕТОК ЭТАЛОНА, А ПОРЯДОК ОТРИСОВКИ, И ЭТО ПРИНЦИПИАЛЬНО.
//   `petWindowOnMessage` перебирает ветки в порядке 42050… → 42070… → 42010 → 42012 → 42002 →
//   42022 (npc_mob_pet.c:4418-4426), но ей уже ПРИШЁЛ номер ОДНОГО контрола: хит-тест сделал
//   движок и отдал верхний по отрисовке. У нас же курсор опрашивается перебором, и порядок
//   перебора становится хит-тестом.
//   ★ 42022 `Pet_Hatchery_ClickAni` (743,229 55×54) ЛЕЖИТ ВНУТРИ 42010 `Pet_Hatchery_EggIcon`
//   (740,233 64×64) — единственная пара пересекающихся узлов окна. В `.JUI` 42022 стоит ПОЗЖЕ
//   (узел 23 против 17, дамп `Reports/pet/ui_dump/42000_*.md`), то есть рисуется ПОВЕРХ и
//   хит-тест отдаёт его. Живой прогон 21.09 это подтвердил от обратного: при «эталонном»
//   порядке перебора клик по «готово» уходил в 42010 и пакет 6102 не отправлялся никогда.
//   ⇒ сначала 42022, потом всё остальное. Прочие узлы окна не пересекаются вовсе.
int32 UAsdaPetHatcherySubsystem::HitTestNode() const
{
	// 42022 ловит клик, только когда план ПОКАЗАН (эталон прячет его вне состояния 2).
	if (DoneIcon && DoneIcon->GetVisibility() != ESlateVisibility::Collapsed
	             && AsdaUi::IsCursorOverWidget(DoneIcon))
	{
		return 42022;
	}
	for (int32 i = 0; i < kGridCells; ++i)
	{
		if (EggCells[i].Icon && AsdaUi::IsCursorOverWidget(EggCells[i].Icon)) { return 42050 + i; }
	}
	for (int32 j = 0; j < kGridCells; ++j)
	{
		if (ToolCells[j].Icon && AsdaUi::IsCursorOverWidget(ToolCells[j].Icon)) { return 42070 + j; }
	}
	if (ImgEggSlot  && AsdaUi::IsCursorOverWidget(ImgEggSlot))  { return 42010; }
	if (ImgToolSlot && AsdaUi::IsCursorOverWidget(ImgToolSlot)) { return 42012; }
	return -1;
}

// ★ Ключ отката к ДОСЛОВНОМУ поведению оригинала в гейте одноразовости (см. ниже).
static ASDA_DEV_CVAR(int32, CVarHatchOneShotFallthrough,
	TEXT("asda.Pet.HatchOneShotFallthrough"), 0,
	TEXT("0 (по умолчанию) — гейт одноразовости СЪЕДАЕТ сообщение: окно закрыто, дальше ничего. ")
	TEXT("1 — дословно как оригинал: после закрытия и нотиса 101119 обработка ПРОДОЛЖАЕТСЯ ")
	TEXT("(npc_mob_pet.c:4321-4347 и :4411-4432 после гейта не возвращаются)."),
	ECVF_Default);

// Пролог `petWindowOnMessage` (:4411-4416) и `petWindowUpdateFeed` (:4321-4326): катализатор
//   израсходован ⇒ закрыть окно с нотисом 101119.
//
// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (закон `fix-original-defects`), НАЙДЕНО РЕВИЗИЕЙ ВОЛНЫ 3.
//   Оригинал после гейта НЕ ВОЗВРАЩАЕТСЯ: в `petWindowUpdateFeed` следом идёт `if (this->pItemFeed)`
//   и таймер стартует на УЖЕ ЗАКРЫТОМ окне (:4327-4345); в `petWindowOnMessage` следом выполняются
//   `petWindowSelectMessage` / `ApplyMessage` / `ClearFeed` / `ClearTool` и ветки 42002 и 42022
//   (:4417-4432) — то есть закрытое окно может ещё и ОТПРАВИТЬ 6102 тем же кликом.
//   Мы сообщение съедаем: закрыли, показали нотис, вышли. Поведение отличается от 1:1 осознанно.
//   КЛЮЧ ОТКАТА: `asda.Pet.HatchOneShotFallthrough 1` возвращает дословный проход дальше.
bool UAsdaPetHatcherySubsystem::ConsumeOneShotGate()
{
	if (!bInvDirty) { return false; }
	bInvDirty = false;
	CloseHatchery();
	NoticeMsgBox(PetHatchLoc::kOneShot);
	const bool bFallthrough = CVarHatchOneShotFallthrough.GetValueOnGameThread() != 0;
	UE_LOG(LogAsdaPetHatch, Log,
		TEXT("[инкубатор] гейт одноразовости: катализатор списан сервером — окно закрыто, нотис %d; ")
		TEXT("сообщение %s (ключ asda.Pet.HatchOneShotFallthrough=%d)"),
		PetHatchLoc::kOneShot,
		bFallthrough ? TEXT("ПРОПУЩЕНО ДАЛЬШЕ, как в оригинале") : TEXT("съедено (исправление дефекта)"),
		bFallthrough ? 1 : 0);
	return !bFallthrough;
}

// ════════════════════════════ ВЫБОР В СЕТКАХ И СЛОТАХ ═════════════════════════════════════════

void UAsdaPetHatcherySubsystem::OnEggCellClicked(int32 CellIndex)
{
	// `petWindowSelectMessage` (:4147-4171): работает ТОЛЬКО при `feedState == 0`,
	//   пустая ячейка игнорируется, выбор гасит иконки «готово» и «успех».
	if (FeedState != 0) { return; }
	if (!EggPage.IsValidIndex(CellIndex) || !EggPage[CellIndex].IsSet()) { RefreshUI(); return; }
	Egg = EggPage[CellIndex];
	// ★ Эталон здесь ставит `resultCode = 1` и `resultModel = -1` (:4157-4158). Это НЕ «успех»:
	//   при `resultModel == -1` ветка отрисовки результата (:4630-4640) строку `pettable` не
	//   находит и ничего не рисует, поэтому единственный видимый эффект — сброс показанного
	//   ранее результата. Воспроизводим дословно, включая значение 1.
	ResultCode  = 1;
	ResultModel = -1;
	AsdaUi::SetShownDecor(DoneIcon,    false);
	AsdaUi::SetShownDecor(SuccessGlow, false);
	RefreshUI();
	UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] яйцо выбрано: ячейка сетки %d → контейнер %d, ячейка %d, код %d"),
		CellIndex, Egg.Container, Egg.Cell, Egg.ItemCode);
}

void UAsdaPetHatcherySubsystem::OnBoosterCellClicked(int32 CellIndex)
{
	// `petWindowApplyMessage` (:4175-4191): то же, но без сброса результата.
	if (FeedState != 0) { return; }
	if (!ToolPage.IsValidIndex(CellIndex) || !ToolPage[CellIndex].IsSet()) { RefreshUI(); return; }
	Booster = ToolPage[CellIndex];
	RefreshUI();
	UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] бустер выбран: ячейка сетки %d → контейнер %d, ячейка %d, код %d"),
		CellIndex, Booster.Container, Booster.Cell, Booster.ItemCode);
}

void UAsdaPetHatcherySubsystem::OnEggSlotClicked()
{
	// `petWindowOnMsgClearFeed` (:4350-4363): клик по 42010 СНИМАЕТ яйцо, только при `feedState == 0`.
	if (FeedState != 0) { return; }
	Egg.Clear();
	RefreshUI();
	UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] яйцо снято со слота 42010"));
}

void UAsdaPetHatcherySubsystem::OnBoosterSlotClicked()
{
	// `petWindowOnMsgClearTool` (:4366-4379): клик по 42012 СНИМАЕТ бустер.
	if (FeedState != 0) { return; }
	Booster.Clear();
	RefreshUI();
	UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] бустер снят со слота 42012"));
}

// ═══════════════════════════════ КНОПКА «ВЫВЕСТИ» ═════════════════════════════════════════════

void UAsdaPetHatcherySubsystem::OnFeedButton()
{
	// `petWindowUpdateFeed@0xDC0D60` (:4313-4345). Порядок: гейт одноразовости → яйцо → автомат.
	if (ConsumeOneShotGate()) { return; }

	if (!Egg.IsSet())
	{
		// ★ Эталон здесь МОЛЧИТ: `if (this->pItemFeed)` — нет яйца, функция просто выходит
		//   (:4330). Нотиса в этой точке НЕТ, выдумывать его нельзя.
		UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] 42098: яйцо не выбрано — эталон в этой точке молчит"));
		return;
	}
	if (FeedState > 0)
	{
		UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] 42098: автомат уже в состоянии %d — повторный запуск запрещён"), FeedState);
		return;
	}

	FeedState = 1;
	FeedDurationMs = FeedDurationMsFor(Egg);
	FAsdaGaugeFill::Clip(BarFill, BarBaseLeft, BarFullW, 0.f);
	AnimState = 1;
	FeedStartTime = FPlatformTime::Seconds();
	AnimStepTime  = FeedStartTime;
	UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] 42098: таймер запущен на %d мс (яйцо код %d)"),
		FeedDurationMs, Egg.ItemCode);
}

void UAsdaPetHatcherySubsystem::OnDoneIconClicked()
{
	// Ветка `a2 == 42022` в `petWindowOnMessage` (:4419-4426): спрятать «готово», автомат → 3,
	//   отправить 6102, перезапустить отсчёт.
	AsdaUi::SetShownDecor(DoneIcon, false);
	FeedState = 3;
	bool bOwnGate = false;
	const bool bSent = SendIncubateRequest(bOwnGate);
	FeedStartTime = FPlatformTime::Seconds();
	// ★ СРЕДН-2 ревизии волны 3: тупик состояния 3 унаследован от эталона (:4424-4432 — выхода из
	//   него нет нигде, кроме прихода 6103), и мы его воспроизводим. Но добавлять СВОИХ путей в
	//   этот тупик нельзя: если отказал НАШ гейт (`SelectedCharId <= 0`), которого у оригинала
	//   нет, возвращаем автомат в состояние 2 и снова показываем «готово».
	//
	// ★★★ ВОЛНА 8 (пункт 1 сквозной ревизии) — ДВЕ ПОПРАВКИ К ЭТОМУ МЕСТУ.
	//   ПОПРАВКА А. `FeedState = 3` ставилось по факту приёма В ОЧЕРЕДЬ, а не отправки. Очередь
	//     крутит `Tick` модели, и если связи нет, пакет из неё не уйдёт НИКОГДА — автомат
	//     оставался в 3 навсегда: 42099 игнорируется (:342), сетки заблокированы (:675-714),
	//     42022 спрятан, а 6103 не придёт. Теперь «пакет заведомо не уйдёт» — такой же повод
	//     вернуться в 2, как и отказ нашего гейта. Условие связи — общая точка
	//     `AsdaPetUi::IsSendLinkUp` (та же, что у окон 43500 и 44200).
	//   ПОПРАВКА Б. На случай, когда связь БЫЛА, пакет ушёл, а ответа нет, взводится засов с
	//     таймаутом (`FeedLatch`, ключ `asda.Pet.SendTimeoutSec`); по его истечении `Tick`
	//     возвращает автомат в 2. Оба пути ведут в ОДНО состояние — «готово, нажми ещё раз».
	const bool bLink = AsdaPetUi::IsSendLinkUp(GetGameInstance());
	if (!bSent && (bOwnGate || !bLink))
	{
		ReturnFeedToReady(bOwnGate
			? TEXT("отказал НАШ гейт (не эталонный)")
			: TEXT("пакет отклонён моделью"));
		return;
	}
	if (!bLink)
	{
		ReturnFeedToReady(TEXT("связи нет — 6102 из очереди не уйдёт"));
		return;
	}
	// ★★★ ВОЛНА 9: засов взводится С КЛЮЧОМ ЭХА. Ответ 6103 несёт «контейнер+ячейку» трёх
	//   материалов (`GameServer.c:36740-36744`), и ячейка ЯЙЦА — то, про что был запрос.
	//   По ней поздний ответ на ПРЕДЫДУЩИЙ 6102 отличается от ответа на текущий.
	FeedLatch.Arm(6102, AsdaPetUi::EchoKeyBagSlot(Egg.Container, Egg.Cell));   // ★ S2 (В2-A): опкод запроса; таймаут — ключ на каждом тике
}

// ★★★ ВОЛНА 8 (пункт 1 ревизии): ЕДИНСТВЕННЫЙ ВЫХОД ИЗ СОСТОЯНИЯ 3 БЕЗ ОТВЕТА СЕРВЕРА.
//   Вынесено отдельной функцией, потому что путей сюда стало три (отказ нашего гейта, отказ
//   модели/связи, таймаут), а действие у всех трёх одно: автомат в 2, «готово» 42022 показать
//   снова с перезапуском анимации. Второй копии этого блока не заводим (закон `port-shared-points`).
void UAsdaPetHatcherySubsystem::ReturnFeedToReady(const TCHAR* Why)
{
	FeedLatch.Disarm();
	FeedState = 2;
	AsdaUi::SetShownDecor(DoneIcon, true);
	if (UAsdaAnimatedPlane* Ani = Cast<UAsdaAnimatedPlane>(DoneIcon.Get())) { Ani->RestartAnim(); }
	UE_LOG(LogAsdaPetHatch, Warning,
		TEXT("[инкубатор] %s — автомат возвращён в состояние 2, 42022 показан снова; ")
		TEXT("тупика состояния 3 не создаём (закон no-state-latch-before-shown)"), Why);
}

// ═══════════════════════════════ ОТПРАВКА 6102 ════════════════════════════════════════════════

bool UAsdaPetHatcherySubsystem::SendIncubateRequest(bool& bOutOwnGate)
{
	bOutOwnGate = false;
	// `petWindow_SendFeedRequest@0xDC2570` (объявление :4892, отправка :5037). ТРИ гейта, ВСЕ
	//   молчаливые — ни один не печатает нотиса (закон `never-invent-only-original`).
	//   ★ Номера строк исправлены ревизией волны 3: О1 :4919, О2 :4922, О3 :4926.
	if (!Egg.IsSet())
	{
		UE_LOG(LogAsdaPetHatch, Warning, TEXT("[инкубатор] 6102 О1: яйца нет (`pItemFeed == 0`, :4919) — пакет не ушёл"));
		return false;
	}
	if (!Catalyst.IsSet())
	{
		UE_LOG(LogAsdaPetHatch, Warning, TEXT("[инкубатор] 6102 О2: катализатора нет (`pSrcSlotFeed == 0`, :4922) — пакет не ушёл"));
		return false;
	}
	UAsdaPetSubsystem* P = Pets();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!P || !Net || Net->SelectedCharId <= 0)
	{
		// О3 эталона — `CharManager_GetLocalPlayer()` вернул 0 (:4926). У нас признак «свой
		//   персонаж известен» — это `SelectedCharId` сетевой подсистемы: именно он уходит в
		//   первое поле тела 6102 (`localPlayer->userIdx`, :4929).
		// ★★★ ЭТО НАШ ГЕЙТ, А НЕ ЭТАЛОННЫЙ. У оригинала локальный игрок в игре есть всегда, и
		//   ветка О3 практически недостижима; у нас же `SelectedCharId == 0` — штатное состояние
		//   до входа в мир. Если оставить автомат в состоянии 3 (как делает эталон), окно
		//   встанет в ТУПИК: 42099 игнорируется, сетки заблокированы, 42022 спрятан, а 6103 не
		//   придёт — ответа-то не будет. Поэтому сообщаем вызывающему «отказал НАШ гейт», и он
		//   возвращает автомат в состояние 2 (закон `no-state-latch-before-shown`).
		bOutOwnGate = true;
		UE_LOG(LogAsdaPetHatch, Warning,
			TEXT("[инкубатор] 6102 О3: своего персонажа нет (`GetLocalPlayer() == 0`, :4926) — пакет не ушёл"));
		return false;
	}

	// ПОРЯДОК ПОЛЕЙ — ПО СЕРВЕРУ: катализатор, яйцо, бустер (см. шапку `.h`).
	//   Бустер необязателен: `RequestIncubate` с `Slot3 == -1` кладёт в тело умолчание
	//   конструктора эталона (сумка 0, слот 0xFFFF) — ровно то, что там оказывается, когда
	//   `pItemTool == nullptr` (`sub_92A878` на :4928 = `CPetSlot_Reset@0xDC2C30`, :5048-5056).
	const bool bOk = P->RequestIncubate(
		Catalyst.Container, Catalyst.Cell,
		Egg.Container,      Egg.Cell,
		Booster.IsSet() ? Booster.Container : 0,
		Booster.IsSet() ? Booster.Cell      : -1);

	UE_LOG(LogAsdaPetHatch, Log,
		TEXT("[инкубатор] 6102 %s: катализатор (%d,%d) код %d · яйцо (%d,%d) код %d · бустер %s"),
		bOk ? TEXT("поставлен в очередь") : TEXT("ОТКЛОНЁН моделью"),
		Catalyst.Container, Catalyst.Cell, Catalyst.ItemCode,
		Egg.Container, Egg.Cell, Egg.ItemCode,
		Booster.IsSet() ? *FString::Printf(TEXT("(%d,%d) код %d"), Booster.Container, Booster.Cell, Booster.ItemCode)
		                : TEXT("нет"));
	return bOk;
}

// ═══════════════════════════════ ПРИЁМ 6103 ═══════════════════════════════════════════════════

void UAsdaPetHatcherySubsystem::DrainPetEvents()
{
	// ★ ВОЛНА 8 (дубли, пункт 7): слив очереди — общая точка `AsdaPetUi::DrainEvents`
	//   (это тело было скопировано в четыре окна). Хвост у каждого окна свой — он ниже.
	AsdaPetUi::DrainEvents(PetEvents,
		[this](int32 Opcode, int32 Result, int32 /*Uid*/)
		{
			// ★ Инкубатор реагирует ТОЛЬКО на 6103. Соседние пет-пакеты (6106 список, 6122/6123
			//   счётчик ячеек) окно 42000 не обрабатывает: в эталоне их приёмники лежат в
			//   `item_enhance.c`/`network.c` и трогают окно 43000, а не это. Проверено поимённо по
			//   диспетчеру `network.c:552-556` — на `g_pPetWindow` завязан один-единственный 6103.
			//   ⚠ Было `continue` внутри цикла — здесь это тело лямбды, и пропуск делает `return`.
			if (Opcode != OP_PET_INCUBATE_RESULT) { return; }
			OnIncubateResult(Result);
		});
}

void UAsdaPetHatcherySubsystem::OnIncubateResult(int32 Result)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	const FAsdaPetIncubateEcho& Echo = P->GetLastIncubateEcho();

	// ★★★ ВОЛНА 9, ПОПРАВКА РЕВИЗИИ: СВЕРКА КОРРЕЛЯЦИОННОГО ЭХА — ПЕРВОЙ СТРОКОЙ, ДО ЛЮБЫХ
	//   ИЗМЕНЕНИЙ СОСТОЯНИЯ. Прежняя запись «номера запроса в телах 6102/6103 НЕТ» была ЛОЖНЫМ
	//   «нет»: ответ 6103 несёт контейнер+ячейку трёх материалов (`GameServer.c:36740-36744`),
	//   то есть эхо того, ПРО ЧТО был запрос.
	//   Если эхо разошлось с ключом отправленного запроса — это ответ на ПРЕДЫДУЩИЙ 6102:
	//   ни засов снимать, ни автомат трогать нельзя, наш запрос ещё в полёте.
	//   ⚠ ЧЕСТНЫЙ ОСТАТОК: два ОДИНАКОВЫХ запроса (то же яйцо, та же ячейка) дают одинаковое
	//   эхо и по-прежнему неразличимы — это и есть настоящий предел протокола.
	if (FeedLatch.IsForeignEcho(AsdaPetUi::EchoKeyBagSlot(Echo.EggBag, Echo.EggSlot)))
	{
		UE_LOG(LogAsdaPetHatch, Warning,
			TEXT("[инкубатор] 6103 относится к ДРУГОМУ запросу: эхо ячейки яйца (сумка %d ячейка %d) ")
			TEXT("не совпало с отправленным — это ответ на ПРЕДЫДУЩИЙ 6102. Пакет не применяем."),
			Echo.EggBag, Echo.EggSlot);
		return;
	}

	// ★ Эталон запоминает породу ДО разбора кода — по ЕЩЁ ВЫБРАННОМУ яйцу (:5078-5086):
	//   обычное яйцо даёт `item_list.LinkedGroupCode`, cash-яйцо — `special_itemlist.ResultModel`.
	if (Egg.IsSet())
	{
		const int32 M = ResultModelFor(Egg);
		if (M >= 0) { ResultModel = M; }
	}
	ResultCode = 0;

	UAsdaInventorySubsystem* IV = Inv();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	// Общий для кодов 1 и 9 блок «списать предметы и обновить вес» (:5096-5111 и :5136-5151).
	auto ConsumeSlots = [&]()
	{
		if (Net) { Net->SetSelfWeight(Echo.WeightCur, -1, TEXT("6103")); }
		// ★ МЕЛКОЕ-3 ревизии: эталон чистит ТРИ ячейки и лишь потом делает ОДНУ перерисовку
		//   `CInventoryUI_ResetAndRefresh` (npc_mob_pet.c:5111 и :5151). Каждая наша `ClearCell`
		//   тянула полную пересборку кэша хотбара — три вместо одной. Батч это снимает.
		//   Ранних выходов в этой лямбде нет, поэтому пара `Begin`/`End` замкнута по построению.
		if (IV) { IV->BeginCellBatch(); }
		// Катализатор: сумка 2 → контейнер 2, сумка 1 → контейнер 1. Именно эта ветка взводит
		//   одноразовость (:5099 / :5104) — флаг ставится ЗДЕСЬ, а не по нажатию кнопки.
		if ((Echo.CatalystBag == 1 || Echo.CatalystBag == 2) && Echo.CatalystSlot >= 0)
		{
			if (IV) { IV->ClearCell(Echo.CatalystBag, Echo.CatalystSlot); }
			bInvDirty = true;
		}
		// Яйцо (:5106-5109).
		if ((Echo.EggBag == 1 || Echo.EggBag == 2) && Echo.EggSlot >= 0)
		{
			if (IV) { IV->ClearCell(Echo.EggBag, Echo.EggSlot); }
		}
		// ★★★ БУСТЕР — ЗДЕСЬ ИСПРАВЛЕН ДЕФЕКТ Д-ИНК ОРИГИНАЛА (разбор — в шапке `.h`).
		//   Эталон пишет `memset(&g_InventoryBag + 57 * packet[6], 0, 0x39)` БЕЗУСЛОВНО (:5110),
		//   и при отсутствии бустера `packet[6] == 0xFFFF` даёт запись за пределами массива.
		//   Чистим только по НАЗВАННОЙ сумке и ДЕЙСТВИТЕЛЬНОМУ номеру ячейки.
		if ((Echo.BoosterBag == 1 || Echo.BoosterBag == 2) && Echo.BoosterSlot >= 0)
		{
			if (IV) { IV->ClearCell(Echo.BoosterBag, Echo.BoosterSlot); }
		}
		else if (Echo.BoosterSlot < 0)
		{
			// ★ МЕЛКОЕ-6 ревизии — ТОЧНАЯ ФОРМУЛИРОВКА. За границу массива писал бы ОРИГИНАЛ
			//   (`memset(&g_InventoryBag + 57 * 0xFFFF, …)`, npc_mob_pet.c:5110). В ПОРТУ такой
			//   записи не было бы и без этой ветки: `ClearCell` сперва проверяет `IsValidIndex`
			//   (AsdaInventorySubsystem.cpp:1663), а `RdI16(0xFFFF)` даёт −1. Здесь исправляется
			//   ПОВЕДЕНИЕ (не трогаем чужую ячейку) и остаётся след в логе — не падение.
			UE_LOG(LogAsdaPetHatch, Log,
				TEXT("[инкубатор] Д-ИНК: бустера не было (сумка %d, слот %d) — очистку пропустили. ")
				TEXT("В оригинале безусловный memset (npc_mob_pet.c:5110) ушёл бы за границу массива; ")
				TEXT("в порту ClearCell отбила бы индекс сама, так что это правка ПОВЕДЕНИЯ, не падения"),
				Echo.BoosterBag, Echo.BoosterSlot);
		}
		if (IV) { IV->EndCellBatch(); }   // ОДНА пересборка кэша хотбара на весь батч
	};

	switch (Result)
	{
		case 1:
			// :5088-5112 — свечение, звук, нотис, вес, списание, `resultCode = 1`.
			// ★ Свечение 42024 (скин `Light_64`) — ОДНОРАЗОВЫЙ план: отыграв свой прогон, он
			//   ПРЯЧЕТ СЕБЯ САМ (`UAsdaAnimatedPlane::TickAnim`, порт `ui_core.c` L108885-108887).
			//   Поэтому окно его только показывает и перезапускает; гасить его вручную не надо и
			//   нельзя — в живом прогоне 21.09 дерево через 2 с после успеха видит его уже
			//   свёрнутым, но с посчитанной геометрией (740,233)-(804,297), то есть он отыграл.
			AsdaUi::SetShownDecor(SuccessGlow, true);
			if (UAsdaAnimatedPlane* Ani = Cast<UAsdaAnimatedPlane>(SuccessGlow.Get())) { Ani->RestartAnim(); }
			PlayWav(PetHatchWav::kSuccess);
			NoticeMsgBox(PetHatchLoc::kHatchOk);
			ConsumeSlots();
			ResultCode = 1;
			break;
		case 2:
			NoticeMsgBox(PetHatchLoc::kNoPetSlots);    // :5115 «Больше питомцев иметь нельзя»
			break;
		case 3:
		case 7:
			NoticeMsgBox(PetHatchLoc::kCatalystErr);   // :5120 ошибка катализатора (пара 3/7)
			break;
		case 4:
		case 8:
			NoticeMsgBox(PetHatchLoc::kEggErr);        // :5125 ошибка яйца (пара 4/8)
			break;
		case 5:
			NoticeMsgBox(PetHatchLoc::kBoosterErr);    // :5129 ошибка вспомогательного средства
			break;
		case 9:
			// :5131-5155 — нотис, вес, списание, звук провала, `resultCode = 9`.
			NoticeMsgBox(PetHatchLoc::kHatchFail);
			ConsumeSlots();
			PlayWav(PetHatchWav::kFailure);
			ResultCode = 9;
			break;
		default:
			// :5156 — неизвестный код: ничего, кроме общего эпилога.
			UE_LOG(LogAsdaPetHatch, Warning, TEXT("[инкубатор] 6103 код %d не разбирается эталоном — только эпилог"), Result);
			break;
	}

	// ЭПИЛОГ ДЛЯ ВСЕХ КОДОВ (:5158-5163): слоты пусты, створки в состояние 3, звук, автомат в 0.
	Egg.Clear();
	Booster.Clear();
	AnimState = 3;
	AnimStepTime = FPlatformTime::Seconds();
	PlayWav(PetHatchWav::kOpen);
	FeedState = 0;
	if (FeedLatch.ConsumeTimedOut())
	{
		UE_LOG(LogAsdaPetHatch, Warning,
			TEXT("[инкубатор] 6103 пришёл ПОСЛЕ таймаута, но эхо ячейки яйца СОВПАЛО — ответ наш. ")
			TEXT("Неразличим лишь повтор ТОГО ЖЕ запроса (то же яйцо, та же ячейка)."));
	}
	FeedLatch.Disarm();   // ★ волна 8: ответ пришёл — ждать больше нечего
	RefreshUI();

	UE_LOG(LogAsdaPetHatch, Log,
		TEXT("[инкубатор] 6103 код=%d: катализатор (%d,%d) яйцо (%d,%d) бустер (%d,%d) вес=%d; ")
		TEXT("порода результата %d, одноразовость %s"),
		Result, Echo.CatalystBag, Echo.CatalystSlot, Echo.EggBag, Echo.EggSlot,
		Echo.BoosterBag, Echo.BoosterSlot, Echo.WeightCur, ResultModel,
		bInvDirty ? TEXT("взведена") : TEXT("нет"));
}

// ═══════════════════════════════ ПЕРЕРИСОВКА ══════════════════════════════════════════════════

void UAsdaPetHatcherySubsystem::RefreshUI()
{
	if (!bReady) { return; }
	UGameInstance* GI = GetGameInstance();

	// ── Слот ЯЙЦА 42010 и подпись 42011 (:4587-4629) ──
	AsdaUi::ClearImageBrush(ImgEggSlot);
	if (TxtEggName) { TxtEggName->SetText(FText::GetEmpty()); }
	if (Egg.IsSet())
	{
		AsdaUi::ApplyItemIcon(GI, ImgEggSlot, Egg.ItemCode, FVector2D((float)kBigPx, (float)kBigPx));
		if (TxtEggName) { TxtEggName->SetText(FText::FromString(ItemDisplayName(Egg.ItemCode))); }
	}
	// Иконка РЕЗУЛЬТАТА рисуется ПОВЕРХ того же слота (:4630-4650) — успех и провал разные.
	if (ResultCode == 1) { DrawResultIcon(ResultModel, /*bSuccess*/ true);  }
	if (ResultCode == 9) { DrawResultIcon(ResultModel, /*bSuccess*/ false); }

	// ── Слот БУСТЕРА 42012 (:4652-4681) ──
	AsdaUi::ClearImageBrush(ImgToolSlot);
	if (Booster.IsSet())
	{
		AsdaUi::ApplyItemIcon(GI, ImgToolSlot, Booster.ItemCode, FVector2D((float)kCellPx, (float)kCellPx));
	}

	// ── Полоса 42015 в состоянии простоя: диапазон 0..100, значение 0 (:4676-4680) ──
	if (FeedState == 0) { FAsdaGaugeFill::Clip(BarFill, BarBaseLeft, BarFullW, 0.f); }

	// ── Списки кандидатов и страницы (:4660-4706) ──
	BuildBoosterList();
	BuildEggList();
	EggPageCount  = FMath::DivideAndRoundUp(EggList.Num(),  kGridCells);
	ToolPageCount = FMath::DivideAndRoundUp(ToolList.Num(), kGridCells);

	// ── Сетка ЯИЦ 42050…42055 (:4707-4790) ──
	//   Эталон сначала ГАСИТ все шесть ячеек и рамку, затем заполняет, начиная со страницы.
	AsdaUi::SetShownDecor(EggSelFrame, false);
	for (int32 i = 0; i < kGridCells; ++i)
	{
		AsdaUi::ClearImageBrush(EggCells[i].Icon);
		if (EggCells[i].Label) { EggCells[i].Label->SetText(FText::GetEmpty()); }
		EggPage[i].Clear();
	}
	{
		// `feedSkipCount = 6 * (nFeedCurPage - 1)` при `nFeedCurPage >= 2` (:4724-4725).
		//   Менять страницу в оригинале нечем, поэтому пропуск всегда 0 — но формула та же.
		const int32 Skip = (EggPageIndex >= 2) ? kGridCells * (EggPageIndex - 1) : 0;
		int32 Put = 0;
		for (int32 Idx = Skip; Idx < EggList.Num() && Put < kGridCells; ++Idx, ++Put)
		{
			EggPage[Put] = EggList[Idx];
			AsdaUi::ApplyItemIcon(GI, EggCells[Put].Icon, EggList[Idx].ItemCode,
				FVector2D((float)kCellPx, (float)kCellPx));
			// Рамка выбора встаёт на ячейку, если это ТОТ ЖЕ предмет: сверка по трём полям
			//   записи — код, контейнер, слот (:4760).
			if (Egg.IsSet() && Egg.Same(EggList[Idx]))
			{
				PlaceSelFrame(EggSelFrame, EggCells[Put].Icon);
			}
		}
	}

	// ── Сетка БУСТЕРОВ 42070…42075 (:4791-4844) ──
	AsdaUi::SetShownDecor(ToolSelFrame, false);
	for (int32 j = 0; j < kGridCells; ++j)
	{
		AsdaUi::ClearImageBrush(ToolCells[j].Icon);
		if (ToolCells[j].Label) { ToolCells[j].Label->SetText(FText::GetEmpty()); }
		ToolPage[j].Clear();
	}
	{
		const int32 Skip = (ToolPageIndex >= 2) ? kGridCells * (ToolPageIndex - 1) : 0;
		int32 Put = 0;
		for (int32 Idx = Skip; Idx < ToolList.Num() && Put < kGridCells; ++Idx, ++Put)
		{
			ToolPage[Put] = ToolList[Idx];
			AsdaUi::ApplyItemIcon(GI, ToolCells[Put].Icon, ToolList[Idx].ItemCode,
				FVector2D((float)kCellPx, (float)kCellPx));
			if (Booster.IsSet() && Booster.Same(ToolList[Idx]))
			{
				PlaceSelFrame(ToolSelFrame, ToolCells[Put].Icon);
			}
		}
	}
}

// Сбор списка ЯИЦ: обычные (`item_list.itemSubType == 109`) из контейнера 2 + cash
//   (`special_itemlist.subType == 109`) из контейнера 1 (:4683-4700). Порядок обхода — эталонный:
//   сначала все 60 ячеек расходников, потом все 30 ячеек премиум-сумки.
void UAsdaPetHatcherySubsystem::BuildEggList()
{
	EggList.Reset();
	UAsdaInventorySubsystem* IV = Inv();
	if (!IV) { return; }
	for (int32 i = 0; i < UAsdaInventorySubsystem::kSpendSlots; ++i)
	{
		const int32 Code = CellCode(2, i);
		if (Code > 0 && NormalSubType(Code) == kSubTypeEgg)
		{
			FAsdaHatchItemRef R; R.Container = 2; R.Cell = i; R.ItemCode = Code;
			EggList.Add(R);
		}
	}
	for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i)
	{
		const int32 Code = CellCode(1, i);
		if (Code > 0 && SpecialSubType(Code) == kSubTypeEgg)
		{
			FAsdaHatchItemRef R; R.Container = 1; R.Cell = i; R.ItemCode = Code;
			EggList.Add(R);
		}
	}
}

// Сбор списка БУСТЕРОВ: ТОЛЬКО контейнер 1 и ТОЛЬКО `special_itemlist.subType == 108` (:4660-4670).
//   ★ Число 108 снято прямо с кода — в отчёте реверса его не было.
void UAsdaPetHatcherySubsystem::BuildBoosterList()
{
	ToolList.Reset();
	UAsdaInventorySubsystem* IV = Inv();
	if (!IV) { return; }
	for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i)
	{
		const int32 Code = CellCode(1, i);
		if (Code > 0 && SpecialSubType(Code) == kSubTypeBooster)
		{
			FAsdaHatchItemRef R; R.Container = 1; R.Cell = i; R.ItemCode = Code;
			ToolList.Add(R);
		}
	}
}

// Рамка выбора встаёт РОВНО на ячейку. Родитель рамки и родитель ячейки в `.JUI` совпадают, но
//   полагаться на это нельзя: считаем позицию в координатах КОРНЯ и вычитаем позицию родителя
//   рамки — так формула верна при любой иерархии (`Class_GetVec2_74` + `CControl_SetField74_75`).
void UAsdaPetHatcherySubsystem::PlaceSelFrame(UWidget* Frame, const UWidget* OverCell)
{
	if (!Frame || !OverCell) { return; }
	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U19): общая точка рамки выбора. Родитель — тот же расчёт (CanvasRootPos(родитель) ≡
	//   CanvasRootPos(рамка) − её смещение); видимость — `HitTestInvisible` вместо `SelfHitTestInvisible` (рамка-лист мышь не
	//   ловит в обоих случаях; правило NoSelect=1 одно на все окна). Перезапуск кадров — как было у этого окна.
	AsdaUi::PlaceFrameOver(Frame, OverCell, /*bRestartAnim*/true);
}

// ═══════════════════════════════ ТУЛТИПЫ ПРЕДМЕТА ═════════════════════════════════════════════

void UAsdaPetHatcherySubsystem::TickTooltips()
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }

	// `petWindowBuildItemTooltipByMsgId@0xDC1140` (:4429-4480): тултип строится по НОМЕРУ узла
	//   под курсором — 42010 (яйцо в слоте), 42012 (бустер в слоте), 42050…42055 и 42070…42075.
	//   Номер даёт тот же хит-тест, что и клики (`HitTestNode`), поэтому пересечение 42022/42010
	//   разрешается одинаково в обоих местах, а не двумя разными порядками перебора.
	//   ★ Модальный нотис забирает весь диспетч — при нём плашку не строим и гасим показанную.
	int32 Over = HitTestNode();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ Дедуп №2, В1-B (U9, 11.10.2026): общая точка модального гейта (ключ отката `asda.UI.ModalGatePolling`).
		if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()) || WM->IsPointerBlockedByTopmost(kWinId)) { Over = -1; }
	}
	int32 Code = 0;
	if (Over == 42010) { Code = Egg.IsSet()     ? Egg.ItemCode     : 0; }
	if (Over == 42012) { Code = Booster.IsSet() ? Booster.ItemCode : 0; }
	if (Over >= 42050 && Over <= 42055)
	{
		const int32 i = Over - 42050;
		Code = EggPage.IsValidIndex(i) && EggPage[i].IsSet() ? EggPage[i].ItemCode : 0;
	}
	if (Over >= 42070 && Over <= 42075)
	{
		const int32 j = Over - 42070;
		Code = ToolPage.IsValidIndex(j) && ToolPage[j].IsSet() ? ToolPage[j].ItemCode : 0;
	}
	// Узел 42022 предметом не владеет — эталон на нём тултипа не строит (:4429-4447).
	if (Over == 42022) { Over = -1; }

	if (Over == TipId) { return; }
	TipId = Over;
	if (Over < 0 || Code <= 0)
	{
		// Событие 0xA эталона — `CControl_KillFocus(&g_ItemTooltip)` (:4864-4865).
		Tip->HideTooltipFor(EAsdaTipOwner::PetHatchery);
		return;
	}
	Tip->ShowItemTooltipByCode(Code, EAsdaTipOwner::PetHatchery);
}

// ═══════════════════════════════ ДАННЫЕ И СПРАВОЧНИКИ ════════════════════════════════════════

UAsdaPetSubsystem* UAsdaPetHatcherySubsystem::Pets() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
}

UAsdaInventorySubsystem* UAsdaPetHatcherySubsystem::Inv() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
}

UAsdaItemTableSubsystem* UAsdaPetHatcherySubsystem::Items() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
}

int32 UAsdaPetHatcherySubsystem::CellCode(int32 Container, int32 Cell) const
{
	const UAsdaInventorySubsystem* IV = Inv();
	if (!IV || Cell < 0) { return 0; }
	return IV->GetCellItemCode(Container, Cell);
}

int32 UAsdaPetHatcherySubsystem::NormalSubType(int32 ItemCode) const
{
	const UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return -1; }
	const FAsdaItemRecord* Rec = IT->GetItem(ItemCode);
	return Rec ? Rec->ItemSubType : -1;
}

int32 UAsdaPetHatcherySubsystem::SpecialSubType(int32 ItemCode) const
{
	const UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return -1; }
	if (!IT->IsSpecialItemCode(ItemCode)) { return -1; }
	FAsdaSpecialItemlistRow Sp;
	if (!IT->GetSpecialItem(ItemCode, Sp)) { return -1; }
	return Sp.SubType;
}

FString UAsdaPetHatcherySubsystem::ItemDisplayName(int32 ItemCode) const
{
	const UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return FString(); }
	// У cash-предмета эталон берёт имя из тултип-таблицы (`itemGetTooltipById(...)->name`, :4626).
	if (IT->IsSpecialItemCode(ItemCode))
	{
		FAsdaSpecialitemTooltiplistRow Tt;
		if (IT->GetSpecialItemTooltip(ItemCode, Tt) && !Tt.Name.IsEmpty()) { return Tt.Name; }
	}
	// У обычного — `ItemData_FindByCode(itemCode)` (:4614).
	return IT->GetItemName(ItemCode);
}

int32 UAsdaPetHatcherySubsystem::FeedDurationMsFor(const FAsdaHatchItemRef& EggRef) const
{
	const UAsdaItemTableSubsystem* IT = Items();
	if (!IT || !EggRef.IsSet()) { return kCashFeedMs; }

	// CASH-яйцо: константа 3000 мс (:4339). Проверяем ПЕРВЫМ, потому что у эталона эта ветка
	//   перезаписывает предыдущую, а записи `item_list` у cash-кода нет вовсе.
	if (IT->IsSpecialItemCode(EggRef.ItemCode)) { return kCashFeedMs; }

	// Обычное яйцо: `pettable[ItemRecord->itemVariant] + 0x190` (:4337). Строка `pettable`
	//   адресуется ВАРИАНТОМ предмета, а не его кодом; таблица 472-байтовая, поле +0x190
	//   отдельной колонкой в генерируемом заголовке не выделено — читаем из сырой записи
	//   (`DataHex`) общей точкой `AsdaBytes` (закон `itemdb-header-generated-not-manual`:
	//   правку колонок делает только `decode_item_db.py`).
	const FAsdaItemRecord* Rec = IT->GetItem(EggRef.ItemCode);
	if (!Rec) { return kCashFeedMs; }
	const UDataTable* Dt = IT->GetDataTable(TEXT("pettable"));
	if (!Dt) { return kCashFeedMs; }
	const FAsdaPetRow* Row = Dt->FindRow<FAsdaPetRow>(FName(*FString::FromInt(Rec->ItemVariant)),
		TEXT("PetHatchFeedMs"), false);
	if (!Row) { return kCashFeedMs; }
	const int32 Ms = AsdaBytes::HexI32At(Row->DataHex, kPetRowFeedMsOfs);
	// Нулевая или отрицательная длительность у эталона означала бы мгновенную готовность —
	//   поведение сохраняем, но в лог пишем, чтобы странная строка таблицы была видна.
	if (Ms <= 0)
	{
		UE_LOG(LogAsdaPetHatch, Warning,
			TEXT("[инкубатор] pettable[%d]+0x190 = %d — длительность таймера не положительна"),
			Rec->ItemVariant, Ms);
	}
	return Ms;
}

int32 UAsdaPetHatcherySubsystem::ResultModelFor(const FAsdaHatchItemRef& EggRef) const
{
	const UAsdaItemTableSubsystem* IT = Items();
	if (!IT || !EggRef.IsSet()) { return -1; }
	if (IT->IsSpecialItemCode(EggRef.ItemCode))
	{
		FAsdaSpecialItemlistRow Sp;
		if (IT->GetSpecialItem(EggRef.ItemCode, Sp)) { return Sp.ResultModel; }   // :5084
		return -1;
	}
	const FAsdaItemRecord* Rec = IT->GetItem(EggRef.ItemCode);
	return Rec ? Rec->LinkedGroupCode : -1;                                        // :5081
}

bool UAsdaPetHatcherySubsystem::DrawResultIcon(int32 PetIndex, bool bSuccess)
{
	if (PetIndex < 0 || !ImgEggSlot) { return false; }
	const UAsdaItemTableSubsystem* IT = Items();
	const UDataTable* Dt = IT ? IT->GetDataTable(TEXT("pettable")) : nullptr;
	if (!Dt) { return false; }
	const FAsdaPetRow* Row = Dt->FindRow<FAsdaPetRow>(FName(*FString::FromInt(PetIndex)),
		TEXT("PetHatchResultIcon"), false);
	if (!Row) { return false; }

	FString Atlas;
	int32 U = 0, V = 0;
	if (bSuccess)
	{
		// :4632-4638 — путь +0x118, U +0x138, V +0x13C; все три вынесены в колонки.
		Atlas = Row->ResultIcon0;
		U = Row->ResultIcon0U;
		V = Row->ResultIcon0V;
	}
	else
	{
		// :4643-4649 — путь +0x140, U +0x160, V +0x164. Колонка есть только у пути, U и V
		//   генерируемый заголовок не выделил — читаем из сырой записи.
		Atlas = Row->ResultIcon1;
		U = AsdaBytes::HexI32At(Row->DataHex, kPetRowFailUOfs);
		V = AsdaBytes::HexI32At(Row->DataHex, kPetRowFailVOfs);
	}
	if (Atlas.IsEmpty()) { return false; }
	// `CControl_DrawTextureRect(ctrl, tex, U, V, 64, 64)` — клетка 64×64 атласа в контроле 64×64.
	return AsdaUi::ApplyJuiIconCell(ImgEggSlot, Atlas, U, V, kBigPx, (float)kBigPx);
}

// ═══════════════════════════════ НОТИСЫ И ЗВУК ════════════════════════════════════════════════

FString UAsdaPetHatcherySubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaPetHatcherySubsystem::NoticeMsgBox(int32 LocId) const
{
	// Канал A отчёта §6.1: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` —
	//   однокнопочное модальное окно, кнопка = loc 2288 «ОК».
	//   ★ Чат у питомцев НЕ используется (доказано двумя методами, отчёт §6.1).
	// ★ ВОЛНА 8 (дубли, пункт 7): тело — в общей точке `AsdaPetUi::NoticeMsgBox`
	//   (там же и единственная константа подписи кнопки 2288).
	AsdaPetUi::NoticeMsgBox(GetGameInstance(), LocId);
}

void UAsdaPetHatcherySubsystem::PlayWav(const TCHAR* WavName) const
{
	if (!WavName) { return; }
	UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	// ★ МЕЛКОЕ-5 ревизии: строка в логе обязательна. Прогоны идут с `-nosound`, и без неё даже
	//   ФАКТ вызова звука ничем не подтверждается — проверить «эталонный звук в эталонной точке»
	//   было бы нечем.
	UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] звук «%s.wav» %s"),
		WavName, Snd ? TEXT("запрошен") : TEXT("НЕ запрошен: звук-менеджера нет"));
	if (Snd) { Snd->PlaySound2D(FName(WavName)); }
}

// ══════════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════

FString UAsdaPetHatcherySubsystem::DumpControls() const
{
	FString S = TEXT("=== окно 42000 Pet_Hatchery_Win: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W)
	{
		S += FString::Printf(TEXT("  %-46s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ"));
	};
	Line(TEXT("42001 Pet_Hatchery_Title"),            TxtTitle);
	Line(TEXT("42002 Pet_Hatchery_ClsBtn"),           BtnClose);
	Line(TEXT("42010 Pet_Hatchery_EggIcon (иконка)"), ImgEggSlot);
	Line(TEXT("42010 Pet_Hatchery_EggIcon (подпись)"),TxtEggSlot);
	Line(TEXT("42011 Pet_Hatchery_EggName"),          TxtEggName);
	Line(TEXT("42012 Pet_Hatchery_UsedItem (иконка)"),ImgToolSlot);
	Line(TEXT("42012 Pet_Hatchery_UsedItem (подпись)"),TxtToolSlot);
	Line(TEXT("42013 Pet_Hatchery_GradeTxt ★не пишется"), TxtGradeTxt);
	Line(TEXT("42015 Pet_Hatchery_ProBar (жёлоб)"),   BarTrack);
	Line(TEXT("42015 Pet_Hatchery_ProBar (заливка)"), BarFill);
	Line(TEXT("42020 Pet_Hatchery_Door1"),            Door0a);
	Line(TEXT("42021 Pet_Hatchery_Door2"),            Door0b);
	Line(TEXT("42022 Pet_Hatchery_ClickAni"),         DoneIcon);
	Line(TEXT("42024 Pet_Hatchery_SuccessEffect"),    SuccessGlow);
	Line(TEXT("42026 Pet_Hatchery_Steam_grp"),        Steam);
	Line(TEXT("42030 Pet_Hatchery_BaseBG"),           BgNormal);
	Line(TEXT("42031 Pet_PHatchery_BaseBG"),          BgPremium);
	Line(TEXT("42032 Pet_PHatchery_Door1"),           Door1a);
	Line(TEXT("42033 Pet_PHatchery_Door2"),           Door1b);
	Line(TEXT("42040 PetHatchery_EggSelTitle_Text"),  TxtEggTab);
	Line(TEXT("42041 PetHatchery_Guide"),             TxtGuide);
	for (int32 i = 0; i < kGridCells; ++i)
	{
		Line(*FString::Printf(TEXT("%d PetHatchery_Egg%02d (иконка)"), 42050 + i, i), EggCells[i].Icon);
		Line(*FString::Printf(TEXT("%d PetHatchery_Egg%02d (подпись)"), 42050 + i, i), EggCells[i].Label);
	}
	Line(TEXT("42059 PetHatchery_EggSel"),            EggSelFrame);
	for (int32 j = 0; j < kGridCells; ++j)
	{
		Line(*FString::Printf(TEXT("%d PetHatchery_Item%02d (иконка)"), 42070 + j, j), ToolCells[j].Icon);
		Line(*FString::Printf(TEXT("%d PetHatchery_Item%02d (подпись)"), 42070 + j, j), ToolCells[j].Label);
	}
	Line(TEXT("42079 PetHatchery_ItemSel"),           ToolSelFrame);
	Line(TEXT("42090 PetHatchery_ItemTitle_Text"),    TxtToolTab);
	Line(TEXT("42098 PetHatchery_GOBtn"),             BtnFeed);
	Line(TEXT("42098 PetHatchery_GOBtn_Text"),        TxtFeed);
	Line(TEXT("42099 PetHatchery_CanBtn"),            BtnCancel);
	Line(TEXT("42099 PetHatchery_CanBtn_Text"),       TxtCancel);
	S += FString::Printf(TEXT("  полоса 42015: базовый отступ %.1f, полная ширина %.1f\n"), BarBaseLeft, BarFullW);
	return S;
}

FString UAsdaPetHatcherySubsystem::DumpState() const
{
	FString S = TEXT("=== окно 42000: состояние ===\n");
	S += FString::Printf(TEXT("  разобрано=%s открыто=%s одноразовость=%s оформление=%d (%s)\n"),
		bReady ? TEXT("да") : TEXT("нет"), bActive ? TEXT("да") : TEXT("нет"),
		bInvDirty ? TEXT("ВЗВЕДЕНА") : TEXT("нет"), MoodBranch,
		MoodBranch == 1 ? TEXT("премиум 42031") : TEXT("обычное 42030"));
	S += FString::Printf(TEXT("  feedState=%d animState=%d длительность=%d мс resultCode=%d resultModel=%d\n"),
		FeedState, AnimState, FeedDurationMs, ResultCode, ResultModel);
	S += FString::Printf(TEXT("  катализатор: контейнер %d ячейка %d код %d\n"),
		Catalyst.Container, Catalyst.Cell, Catalyst.ItemCode);
	S += FString::Printf(TEXT("  яйцо:        контейнер %d ячейка %d код %d\n"), Egg.Container, Egg.Cell, Egg.ItemCode);
	S += FString::Printf(TEXT("  бустер:      контейнер %d ячейка %d код %d\n"), Booster.Container, Booster.Cell, Booster.ItemCode);
	S += FString::Printf(TEXT("  список яиц %d (страниц %d, текущая %d), бустеров %d (страниц %d, текущая %d)\n"),
		EggList.Num(), EggPageCount, EggPageIndex, ToolList.Num(), ToolPageCount, ToolPageIndex);
	S += TEXT("  ★ смены страницы в оригинале НЕТ: nFeedCurPage/nToolCurPage только читаются (:4724, :4804)\n");
	for (int32 i = 0; i < EggPage.Num(); ++i)
	{
		S += FString::Printf(TEXT("    ячейка яиц %d: контейнер %d ячейка %d код %d\n"),
			i, EggPage[i].Container, EggPage[i].Cell, EggPage[i].ItemCode);
	}
	for (int32 j = 0; j < ToolPage.Num(); ++j)
	{
		S += FString::Printf(TEXT("    ячейка бустеров %d: контейнер %d ячейка %d код %d\n"),
			j, ToolPage[j].Container, ToolPage[j].Cell, ToolPage[j].ItemCode);
	}
	if (const UAsdaPetSubsystem* P = Pets())
	{
		const FAsdaPetIncubateEcho& E = P->GetLastIncubateEcho();
		S += FString::Printf(TEXT("  последний 6103: код=%d кат=(%d,%d) яйцо=(%d,%d) бустер=(%d,%d) вес=%d\n"),
			E.Result, E.CatalystBag, E.CatalystSlot, E.EggBag, E.EggSlot, E.BoosterBag, E.BoosterSlot, E.WeightCur);
		S += FString::Printf(TEXT("  питомцев в модели: %d, доступно ячеек: %d\n"),
			P->GetSlots().Num(), P->GetPossessionCount());
	}
	return S;
}

FString UAsdaPetHatcherySubsystem::DumpNotices() const
{
	// Таблица «событие → loc → канал» с ФАКТИЧЕСКИМИ строками: так видно, дошли ли эти номера
	//   до `DT_GameText` порта (пункт §12.2 отчёта — «сверка нумерации с портом не сделана»).
	struct FRow { const TCHAR* Event; int32 Loc; const TCHAR* Chan; const TCHAR* Where; };
	static const FRow Rows[] =
	{
		{ TEXT("init 42001 заголовок"),             PetHatchLoc::kTitle,        TEXT("C"), TEXT("petWindowBuildUI:4031") },
		{ TEXT("init 42041 подсказка"),             PetHatchLoc::kGuide0,       TEXT("C"), TEXT("petWindowBuildUI:4041") },
		{ TEXT("init 42040 вкладка яиц"),           PetHatchLoc::kEggTab,       TEXT("C"), TEXT("petWindowBuildUI:4045") },
		{ TEXT("init 42090 вкладка бустеров"),      PetHatchLoc::kToolTab,      TEXT("C"), TEXT("petWindowBuildUI:4050") },
		{ TEXT("init 42098 кнопка «Вывести»"),      PetHatchLoc::kBtnFeed,      TEXT("C"), TEXT("petWindowBuildUI:4055") },
		{ TEXT("init 42099 кнопка «отмена»"),       PetHatchLoc::kBtnCancel,    TEXT("C"), TEXT("petWindowBuildUI:4058") },
		{ TEXT("тик 2 с: яйцо не выбрано"),         PetHatchLoc::kGuideNoEgg,   TEXT("C"), TEXT("CPetWindow_Update:4211") },
		{ TEXT("тик 2 с: яйцо есть, бустера нет"),  PetHatchLoc::kGuideEggOnly, TEXT("C"), TEXT("CPetWindow_Update:4216") },
		{ TEXT("тик 2 с: яйцо и бустер"),           PetHatchLoc::kGuideBoth,    TEXT("C"), TEXT("CPetWindow_Update:4221") },
		{ TEXT("одноразовость: кнопка «Вывести»"),  PetHatchLoc::kOneShot,      TEXT("A"), TEXT("petWindowUpdateFeed:4325") },
		{ TEXT("одноразовость: любой клик"),        PetHatchLoc::kOneShot,      TEXT("A"), TEXT("petWindowOnMessage:4415") },
		{ TEXT("6103 код 1 — успех"),               PetHatchLoc::kHatchOk,      TEXT("A"), TEXT("CPetWin_OnResult:5093") },
		{ TEXT("6103 код 2 — нет ячеек питомцев"),  PetHatchLoc::kNoPetSlots,   TEXT("A"), TEXT("CPetWin_OnResult:5115") },
		{ TEXT("6103 коды 3/7 — катализатор"),      PetHatchLoc::kCatalystErr,  TEXT("A"), TEXT("CPetWin_OnResult:5120") },
		{ TEXT("6103 коды 4/8 — яйцо"),             PetHatchLoc::kEggErr,       TEXT("A"), TEXT("CPetWin_OnResult:5125") },
		{ TEXT("6103 код 5 — бустер"),              PetHatchLoc::kBoosterErr,   TEXT("A"), TEXT("CPetWin_OnResult:5129") },
		{ TEXT("6103 код 9 — провал"),              PetHatchLoc::kHatchFail,    TEXT("A"), TEXT("CPetWin_OnResult:5133") },
		{ TEXT("кнопка однокнопочного нотиса"),     PetHatchLoc::kBtnOk,        TEXT("A"), TEXT("item_inventory.c:50866") },
	};
	FString S = TEXT("=== окно 42000: событие → loc → канал → текст ===\n");
	int32 Missing = 0;
	for (const FRow& R : Rows)
	{
		const FString Text = Loc(R.Loc);
		if (Text.IsEmpty()) { ++Missing; }
		S += FString::Printf(TEXT("  %-38s %6d  %s  %-28s  «%s»\n"),
			R.Event, R.Loc, R.Chan, R.Where, Text.IsEmpty() ? TEXT("★ СТРОКИ НЕТ В DT_GameText") : *Text);
	}
	S += FString::Printf(TEXT("  строк без текста: %d из %d\n"), Missing, (int32)UE_ARRAY_COUNT(Rows));
	return S;
}

bool UAsdaPetHatcherySubsystem::ConsoleClick(int32 JuiId)
{
	if (!bReady)
	{
		UE_LOG(LogAsdaPetHatch, Warning, TEXT("[инкубатор] клик %d: окно ещё не разобрано"), JuiId);
		return false;
	}
	// Порядок и состав веток — ровно как в `TickMouse` (то есть как в `petWindowOnMessage`).
	if (JuiId == 42002) { if (!ConsumeOneShotGate()) { CloseHatchery(); } return true; }
	if (JuiId == 42098) { OnFeedButton(); return true; }
	if (JuiId == 42099)
	{
		if (FeedState != 0)
		{
			UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] 42099: автомат в состоянии %d — эталон отмену игнорирует"), FeedState);
			return true;
		}
		CloseHatchery();
		return true;
	}
	if (ConsumeOneShotGate()) { return true; }
	if (JuiId >= 42050 && JuiId <= 42055) { OnEggCellClicked(JuiId - 42050);     return true; }
	if (JuiId >= 42070 && JuiId <= 42075) { OnBoosterCellClicked(JuiId - 42070); return true; }
	if (JuiId == 42010) { OnEggSlotClicked();     return true; }
	if (JuiId == 42012) { OnBoosterSlotClicked(); return true; }
	if (JuiId == 42022) { OnDoneIconClicked();    return true; }
	UE_LOG(LogAsdaPetHatch, Warning, TEXT("[инкубатор] узла %d у окна 42000 нет"), JuiId);
	return false;
}

void UAsdaPetHatcherySubsystem::ConsoleSynthResult(const TArray<FString>& Args)
{
	UAsdaPetSubsystem* P = Pets();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!P || !Net || Args.Num() < 1) { return; }

	const int32 Code = FCString::Atoi(*Args[0]);
	// Без хвоста берём те ячейки, которые окно реально положило в 6102: так синтез повторяет
	//   ответ сервера на ПОСЛЕДНИЙ отправленный запрос, а не выдуманный набор номеров.
	int32 CatBag  = Catalyst.IsSet() ? Catalyst.Container : 0;
	int32 CatSlot = Catalyst.IsSet() ? Catalyst.Cell      : -1;
	int32 EggBag  = Egg.IsSet()      ? Egg.Container      : 0;
	int32 EggSlot = Egg.IsSet()      ? Egg.Cell           : -1;
	int32 BstBag  = Booster.IsSet()  ? Booster.Container  : 0;
	int32 BstSlot = Booster.IsSet()  ? Booster.Cell       : -1;
	int32 Weight  = FMath::Max(0, Net->SelfWeightCur);
	if (Args.Num() >= 7)
	{
		CatBag  = FCString::Atoi(*Args[1]); CatSlot = FCString::Atoi(*Args[2]);
		EggBag  = FCString::Atoi(*Args[3]); EggSlot = FCString::Atoi(*Args[4]);
		BstBag  = FCString::Atoi(*Args[5]); BstSlot = FCString::Atoi(*Args[6]);
	}
	if (Args.Num() >= 8) { Weight = FCString::Atoi(*Args[7]); }

	// Тело 18 байт — раскладка `sub_45B7F0@0x45B7F0` (GameServer.c:36999).
	TArray<uint8> Body;
	Body.SetNumZeroed(18);
	AsdaBytes::WrI32(Body.GetData() + 0,  Net->SelectedCharId);
	Body[4] = (uint8)Code;
	Body[5] = (uint8)CatBag;
	AsdaBytes::WrI16(Body.GetData() + 6,  CatSlot);
	Body[8] = (uint8)EggBag;
	AsdaBytes::WrI16(Body.GetData() + 9,  EggSlot);
	Body[11] = (uint8)BstBag;
	AsdaBytes::WrI16(Body.GetData() + 12, BstSlot);
	AsdaBytes::WrI32(Body.GetData() + 14, Weight);

	UE_LOG(LogAsdaPetHatch, Log,
		TEXT("[инкубатор] синтез 6103: код=%d кат=(%d,%d) яйцо=(%d,%d) бустер=(%d,%d) вес=%d → приёмник модели"),
		Code, CatBag, CatSlot, EggBag, EggSlot, BstBag, BstSlot, Weight);
	// ★ Через НАСТОЯЩИЙ приёмник: он разберёт тело, положит эхо и выстрелит событием, а окно
	//   обработает его в ближайшем `Tick` — то есть тем же путём, что и живой пакет.
	P->OnPetPacket(OP_PET_INCUBATE_RESULT, Body);
}

// ★★★ КРИТ-1 РЕВИЗИИ ВОЛНЫ 3: ПОДТВЕРЖДЕНИЕ ОТПРАВКИ 6102 НА ЖИВОМ ПРОГОНЕ.
//
// ЧТО БЫЛО НЕ ТАК. Первые два прогона окна 42000 ни разу не отправили 6102: без сервера
//   `SelectedCharId == 0`, срабатывал гейт О3, и в лог шло «своего персонажа нет … пакет не ушёл»
//   (`game_pet_v3.log:4870` и `:5365`). То есть `RequestIncubate` и `SendPetIncubate6102`
//   живьём не исполнялись, а раскладка тела держалась только на чтении кода. Дыра общая для всех
//   пет-волн: так же не подтверждён ни один исходящий пакет питомцев.
//
// КАК ЗАКРЫТО. Модель волны 1 уже умеет ровно то, что нужно (`RunSelfTest`): подменить charId и
//   перехватить тело через `UAsdaNetSubsystem::SendCaptureForStand`. Не заводя второй копии
//   (закон `port-shared-points`), волна 3 вынесла этот механизм наружу
//   (`UAsdaPetSubsystem::BeginSendCapture` / `EndSendCapture`), и здесь он только включается.
//   Путь при этом НАСТОЯЩИЙ и полный: клик мышью по 42022 → `OnDoneIconClicked` →
//   `SendIncubateRequest` → `RequestIncubate` → очередь → `Tick` → `SendRequest` →
//   `SendPetIncubate6102` → конверт «с секретом» → перехватчик.
void UAsdaPetHatcherySubsystem::ConsoleSendCapture(bool bOn, int32 TestCharId)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	if (!bOn)
	{
		P->EndSendCapture();
		UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] перехват исходящих снят"));
		return;
	}
	// ★★★ ВОЛНА 8 (пункт 9 ревизии): ЧУЖОЙ СТЕНД УЖЕ ДЕРЖИТ ПЕРЕХВАТ. Перехватчик исходящих ОДИН
	//   на весь клиент; повторный `Begin` игнорируется. Раньше окно об этом не узнавало и
	//   печатало «перехват взведён», хотя его лямбда не была поставлена.
	if (P->IsSendCaptureArmed())
	{
		UE_LOG(LogAsdaPetHatch, Error,
			TEXT("[инкубатор] перехват НЕ взведён: его уже держит ДРУГОЙ стенд. Дождитесь его ")
			TEXT("завершения либо снимите — два стенда разом дают ложный вердикт."));
		return;
	}
	P->BeginSendCapture(
		[this](int32 Opcode, const TArray<uint8>& Body, bool bPad, TCHAR Env)
		{
			FString Hex;
			for (uint8 B : Body) { Hex += FString::Printf(TEXT("%02X "), B); }
			UE_LOG(LogAsdaPetHatch, Log,
				TEXT("[инкубатор] ★ ПЕРЕХВАЧЕН ИСХОДЯЩИЙ: опкод=%d тело=%d б конверт='%c' пад=%s"),
				Opcode, Body.Num(), Env, bPad ? TEXT("да") : TEXT("нет"));
			UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор]   байты: %s"), *Hex.TrimEnd());
			if (Opcode != OP_PET_INCUBATE_REQ || Body.Num() < 13)
			{
				UE_LOG(LogAsdaPetHatch, Warning, TEXT("[инкубатор]   это не 6102 либо тело короче 13 б"));
				return;
			}
			// Разбор по смещениям СЕРВЕРА (`Recv_IncubatorReq`, GameServer.c:34558-34560,
			//   :34586, :34608, :34645), то есть мимо кода отправителя — иначе проверка
			//   подтверждала бы сама себя.
			UE_LOG(LogAsdaPetHatch, Log,
				TEXT("[инкубатор]   6102: charId=%d · катализатор сумка=%d слот=%d · яйцо сумка=%d слот=%d · ")
				TEXT("бустер сумка=%d слот=%d"),
				AsdaBytes::RdI32(&Body[0]),
				(int32)Body[4],  AsdaBytes::RdI16(&Body[5]),
				(int32)Body[7],  AsdaBytes::RdI16(&Body[8]),
				(int32)Body[10], AsdaBytes::RdI16(&Body[11]));
			// Сверка с тем, что окно ДЕЙСТВИТЕЛЬНО держит в слотах: расхождение ловится сразу.
			const int32 WantBstBag  = Booster.IsSet() ? Booster.Container : 0;
			const int32 WantBstSlot = Booster.IsSet() ? Booster.Cell      : -1;
			const bool bOk =
				(int32)Body[4]  == Catalyst.Container && AsdaBytes::RdI16(&Body[5])  == Catalyst.Cell &&
				(int32)Body[7]  == Egg.Container      && AsdaBytes::RdI16(&Body[8])  == Egg.Cell      &&
				(int32)Body[10] == WantBstBag         && AsdaBytes::RdI16(&Body[11]) == WantBstSlot;
			UE_LOG(LogAsdaPetHatch, Log,
				TEXT("[инкубатор]   сверка с окном: катализатор (%d,%d) яйцо (%d,%d) бустер (%d,%d) → %s"),
				Catalyst.Container, Catalyst.Cell, Egg.Container, Egg.Cell, WantBstBag, WantBstSlot,
				bOk ? TEXT("СОВПАЛО") : TEXT("★ РАЗОШЛОСЬ"));
		},
		TestCharId);
}


// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═══════════════════════════════════════════
// ★ Все команды ходят БОЕВЫМ путём (закон `diagnostic-must-use-real-path`): открытие — через
//   двойной клик инвентаря, клики — через те же обработчики, что зовёт мышь, ответ сервера —
//   синтезом НАСТОЯЩЕГО тела 6103 и прогоном его через приёмник модели. Ни одна из них не пишет
//   в поля окна напрямую — иначе проверка подтверждала бы несуществующее.

static UAsdaPetHatcherySubsystem* HatchFromWorld(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaPetHatcherySubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdHatchControls(
	TEXT("asda.Pet.HatchControls"),
	TEXT("asda.Pet.HatchControls — печать КАЖДОГО разобранного контрола окна 42000."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetHatcherySubsystem* S = HatchFromWorld(W))
		{
			UE_LOG(LogAsdaPetHatch, Log, TEXT("%s"), *S->DumpControls());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdHatchState(
	TEXT("asda.Pet.HatchState"),
	TEXT("asda.Pet.HatchState — состояние окна 42000: автомат, слоты, списки, последний 6103."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetHatcherySubsystem* S = HatchFromWorld(W))
		{
			UE_LOG(LogAsdaPetHatch, Log, TEXT("%s"), *S->DumpState());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdHatchNotices(
	TEXT("asda.Pet.HatchNotices"),
	TEXT("asda.Pet.HatchNotices — таблица «событие → loc → канал» с фактическими строками порта."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetHatcherySubsystem* S = HatchFromWorld(W))
		{
			UE_LOG(LogAsdaPetHatch, Log, TEXT("%s"), *S->DumpNotices());
		}
	}));
#endif // ASDA_DEV_CONSOLE

// Подложить в сумку по одному предмету каждого нужного подтипа и напечатать, что положено.
//   Коды НЕ выдуманы: они ищутся в реальных таблицах порта по подтипу, как это делает сам
//   инкубатор. Без сервера иначе проверить путь игрока нечем.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdHatchFill(
	TEXT("asda.Pet.HatchFill"),
	TEXT("asda.Pet.HatchFill — положить в сумку катализатор (107), яйцо (109) и бустер (108), ")
	TEXT("найдя коды в item_list / special_itemlist. Печатает, что и куда положено."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельные предметы инкубатора — 6102 ушёл бы с поддельными слотами.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.HatchFill"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaInventorySubsystem* IV = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!IV || !IT) { return; }

		// Обычные предметы: коды с нужным подтипом во ВСЁМ `item_list`, не больше `Max`.
		auto FindNormal = [IT](int32 SubType, int32 Max) -> TArray<int32>
		{
			TArray<int32> Out;
			for (const FAsdaItemRecord& R : IT->GetAllItems())
			{
				if (R.ItemSubType == SubType) { Out.Add(R.ItemCode); }
				if (Out.Num() >= Max) { break; }
			}
			return Out;
		};
		// Записи второй таблицы: её кодовое пространство 1…558, перебираем с запасом.
		auto FindSpecial = [IT](int32 SubType, int32 Max) -> TArray<int32>
		{
			TArray<int32> Out;
			for (int32 Code = 1; Code <= 600 && Out.Num() < Max; ++Code)
			{
				FAsdaSpecialItemlistRow Sp;
				if (IT->GetSpecialItem(Code, Sp) && Sp.SubType == SubType) { Out.Add(Code); }
			}
			return Out;
		};

		// Контейнер 2 (расходники, 60 ячеек): обычный катализатор в ячейке 0 и ВОСЕМЬ обычных
		//   яиц в ячейках 1…8 — так сетка заведомо переполняется и видно, что показываются
		//   только первые шесть (страницы в оригинале не листаются).
		const TArray<int32> CatNormal = FindNormal(107, 1);
		const TArray<int32> EggNormal = FindNormal(109, 8);
		if (CatNormal.Num() > 0) { IV->DebugPutItem(2, 0, CatNormal[0], 1); }
		for (int32 i = 0; i < EggNormal.Num(); ++i) { IV->DebugPutItem(2, 1 + i, EggNormal[i], 1); }

		// Контейнер 1 (премиум-сумка, 30 ячеек): cash-катализатор, cash-яйца и бустеры —
		//   именно там их ищет окно (`BuildEggList` / `BuildBoosterList`).
		const TArray<int32> CatSpecial = FindSpecial(107, 1);
		const TArray<int32> EggSpecial = FindSpecial(109, 3);
		const TArray<int32> Boosters   = FindSpecial(108, 6);
		if (CatSpecial.Num() > 0) { IV->DebugPutItem(1, 0, CatSpecial[0], 1); }
		for (int32 i = 0; i < EggSpecial.Num(); ++i) { IV->DebugPutItem(1, 1 + i, EggSpecial[i], 1); }
		for (int32 i = 0; i < Boosters.Num();   ++i) { IV->DebugPutItem(1, 4 + i, Boosters[i],   1); }

		UE_LOG(LogAsdaPetHatch, Log,
			TEXT("[инкубатор] HatchFill: катализатор обычный=%d (2,0), обычных яиц %d (2,1…), ")
			TEXT("катализатор cash=%d (1,0), cash-яиц %d (1,1…), бустеров %d (1,4…)"),
			CatNormal.Num() > 0 ? CatNormal[0] : 0, EggNormal.Num(),
			CatSpecial.Num() > 0 ? CatSpecial[0] : 0, EggSpecial.Num(), Boosters.Num());
	}));
#endif // ASDA_DEV_CONSOLE

// Открыть окно ПУТЁМ ИГРОКА: двойной клик по ячейке с катализатором.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdHatchUse(
	TEXT("asda.Pet.HatchUse"),
	TEXT("asda.Pet.HatchUse [контейнер] [ячейка] — двойной клик по предмету сумки (по умолчанию 2 0). ")
	TEXT("Идёт через UAsdaInventorySubsystem::UseByDoubleClick, то есть боевым путём игрока."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaInventorySubsystem* IV = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!IV) { return; }
		const int32 Cont = (Args.Num() >= 1) ? FCString::Atoi(*Args[0]) : 2;
		const int32 Cell = (Args.Num() >= 2) ? FCString::Atoi(*Args[1]) : 0;
		const bool bOk = IV->UseByDoubleClick(Cont, Cell);
		UE_LOG(LogAsdaPetHatch, Log, TEXT("[инкубатор] HatchUse (%d,%d) → %s"),
			Cont, Cell, bOk ? TEXT("обработано") : TEXT("отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

// Нажатие по узлу окна боевым обработчиком (то же, что делает мышь на событии 0xB / 0x10401).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdHatchClick(
	TEXT("asda.Pet.HatchClick"),
	TEXT("asda.Pet.HatchClick <42002|42010|42012|42022|42050..42055|42070..42075|42098|42099> — ")
	TEXT("нажатие по узлу окна 42000 БОЕВЫМ обработчиком."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetHatcherySubsystem* S = HatchFromWorld(W);
		if (!S || Args.Num() < 1) { return; }
		const int32 Id = FCString::Atoi(*Args[0]);
		S->ConsoleClick(Id);
	}));
#endif // ASDA_DEV_CONSOLE

// Синтез НАСТОЯЩЕГО тела 6103 и прогон через приёмник модели (волна 1) — без сервера.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdHatchSynth(
	TEXT("asda.Pet.HatchResult"),
	TEXT("asda.Pet.HatchResult <код 1..9> [катСумка катСлот яйцоСумка яйцоСлот бустСумка бустСлот вес] — ")
	TEXT("синтезировать тело 6103 (18 б) и скормить его НАСТОЯЩЕМУ приёмнику UAsdaPetSubsystem. ")
	TEXT("Без хвоста берутся ячейки, которые окно реально отправило, и вес −1 не трогается."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6103 снимает замок и расходует ячейки локально.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.HatchResult"))) { return; }
		UAsdaPetHatcherySubsystem* S = HatchFromWorld(W);
		if (!S || Args.Num() < 1) { return; }
		S->ConsoleSynthResult(Args);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdHatchCapture(
	TEXT("asda.Pet.HatchCapture"),
	TEXT("asda.Pet.HatchCapture <0|1> [charId=12345] — взвести/снять ПЕРЕХВАТ исходящих пет-пакетов. ")
	TEXT("Взведённый перехват подменяет SelectedCharId и НЕ ПУСКАЕТ пакеты в сеть, зато печатает ")
	TEXT("тело 6102 побайтово и с разбором полей. Нужен, чтобы живой прогон подтверждал отправку, ")
	TEXT("а не только приём (КРИТ-1 ревизии волны 3)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetHatcherySubsystem* S = HatchFromWorld(W);
		if (!S || Args.Num() < 1) { return; }
		const bool bOn = FCString::Atoi(*Args[0]) != 0;
		// ★ С-Р1 (26.09.2026), офлайн-гейт: бессрочный перехват глушит почти весь исходящий трафик и подменяет charId живой
		//   сессии — ВЗВОДИТЬ его можно только офлайн. Снятие (`0`) разрешено всегда: перехват, взведённый офлайн, должен
		//   сниматься и после подключения.
		if (bOn && FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.HatchCapture 1"))) { return; }
		const int32 CharId = (Args.Num() >= 2) ? FCString::Atoi(*Args[1]) : 12345;
		S->ConsoleSendCapture(bOn, CharId);
	}));
#endif // ASDA_DEV_CONSOLE
