// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна города `Town_as_WIN` (JUI 79300). Весь разбор эталона, поля объекта и поток — в шапке
//   `AsdaTownWarSubsystem.h` и в отчёте `Reports/owner_1006/wave2D_gift_townwar.md` §2.
//   Структурный дамп ассета (каждый контрол) — `Reports/owner_1006/w2d/wbp_tree.txt`.

#include "UI/AsdaTownWarSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaNotice.h"            // фасад нотисов (Loc / MsgBox)
#include "UI/AsdaWindowSubsystem.h"

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Net/NetOpcodes.h"
#include "AsdaOutgoingGate.h"         // офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Login/AsdaButtonRelay.h"
#include "World/AsdaClanFlag.h"       // общая точка «знак лагеря» (`CampWar_MarkImage`, `uiMakeAxisVec2`)

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTownWar, Log, All);

// ═════════════════════════════ КЛЮЧИ ОТКАТА ИСПРАВЛЕНИЙ (разбор — в шапке .h) ═════════════════════
// ★★ [ОРИГИНАЛ] 30.10.2026 (волна bugs8, «нет цвета в текстах»): нотисы окна 79300 (111900 «не глава», 111919/111920
//   итог ставки, 111921 подтверждение) эталон отдаёт окну-сообщению С ТЕГАМИ `<FC=FF99FF99>…</F>` (ui_core.c:17578-17588,
//   chat.c:5831) — значения подсвечены зелёным. Порт брал `Loc` (теги срезаны). 1 = нотисы с тегами, 0 = прежний срез.
//   Тело окна 79303 (111909/111910) — обычный текстовый контрол, остаётся без цвета (см. итог волны).
static ASDA_DEV_CVAR(int32, CVarMsgKeepTags,
	TEXT("asda.TownWar.MsgKeepTags"), 1,
	TEXT("1 = нотисы окна города (111900/111919/111920/111921) с тегами цвета, как оригинал; 0 = теги срезаются (прежний порт)."),
	ECVF_Default);

// Строка нотиса окна города: с тегами (ключ выше) или прежняя срезанная.
static FString TownWarMsgLoc(const UGameInstance* GI, int32 LocId)
{
	return (CVarMsgKeepTags.GetValueOnGameThread() != 0) ? AsdaNotice::LocTagged(GI, LocId) : AsdaNotice::Loc(GI, LocId);
}

static ASDA_DEV_CVAR(int32, CVarFixTextArgs,
	TEXT("asda.TownWar.FixTextArgs"), 1,
	TEXT("Дефект оригинала Д-ТВ-1: 1 = строка 111909 получает (процент, город, клан) по смыслу своих полей; "
	     "0 = порядок кода оригинала (город, клан, процент): RU-клиент оригинала на месте %d печатает адрес строки города, "
	     "а второй %s читает ставку как адрес — при ставке ≠ 0 падение, при 0 CRT печатает «(null)»; у порта — сдвиг полей."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarFixTrackPage,
	TEXT("asda.TownWar.FixTrackPage"), 1,
	TEXT("Дефект оригинала Д-ТВ-2: 1 = клик по жёлобу полосы 79314 листает «страницей» по X; "
	     "0 = как оригинал (сравнение по Y — у горизонтальной полосы клик по жёлобу ничего не делает)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarFixRawCode,
	TEXT("asda.TownWar.FixRawCode"), 1,
	TEXT("Дефект оригинала Д-ТВ-3: 1 = ответ 6745 с кодом 2/4 показывает строку 111900; 0 = как оригинал (голая цифра кода)."),
	ECVF_Default);

namespace
{
	// Окно и его виджет. Ассет уже импортирован заходом JUI (не перегенерировать!).
	const FName  kWindowId(TEXT("Town_as_WIN"));
	const TCHAR* kWindowWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_Town_as_WIN.WBP_Town_as_WIN_C");

	// IDNum контролов (`.JUI` и `uiCampWarPanelBindControls`).
	constexpr int32 kIdTitle  = 79301;   // заголовок
	constexpr int32 kIdBody   = 79303;   // тело
	constexpr int32 kIdTax    = 79310;   // «Ставка налога»
	constexpr int32 kIdTaxV   = 79311;   // «+ N%»
	constexpr int32 kIdDisc   = 79312;   // «Скидка»
	constexpr int32 kIdDiscV  = 79313;   // «- N%»
	constexpr int32 kIdPer    = 79315;   // «N%» у полосы
	constexpr int32 kIdOk     = 79321;   // «Нажми»
	constexpr int32 kIdNo     = 79322;   // «Отмена»
	constexpr int32 kIdX      = 79329;   // X

	// Событие полосы → `settings_DispatchFlagById` (ветвь окна сообщества: a4 ∈ (0x11000…0x11002] или ≥ 0x11005).
	constexpr int32 kEvLineUp   = 69633;   // ← (0x11001)
	constexpr int32 kEvLineDown = 69634;   // → (0x11002)
	constexpr int32 kEvPageUp   = 69635;   // жёлоб, к началу (0x11003) — в диспетчер окна НЕ попадает
	constexpr int32 kEvPageDown = 69636;   // жёлоб, к концу (0x11004) — в диспетчер окна НЕ попадает
	constexpr int32 kEvDrag     = 69637;   // перетаскивание бегунка (0x11005)
	constexpr int32 kEvSetPos   = 69638;   // `uiScrollbarSetPos` (0x11006)

	// SS2 стрелки → (сдвиг UV −1 тексель атласа 512 по обеим осям: ES2 SS0 «0.414 0.438…» → SS2 «0.412 0.436…»).
	constexpr float kPressedUvShift = -0.002f;
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaTownWarSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HPacket = Net->OnWave2DPacket.AddLambda([this](int32 Op, const TArray<uint8>& B)
		{
			if (Op == OP_C_CHAT_SHOW_ITEM_SUBNOTICE) { PendingResults.Push(B); }
		});
	}
	UE_LOG(LogAsdaTownWar, Log, TEXT("[город] подсистема окна 79300 поднята"));
}

void UAsdaTownWarSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnWave2DPacket.Remove(HPacket);
	}
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaTownWarSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Окно оригинала — контрол сообщества; перезагрузка интерфейсов прячет его (корень 79300 `Visible = 0`).
	if (bOpen) { CloseWindow(); }
	bThumbGrab = false;
	PressedArrow = 0;
	PendingResults.Clear();
	Mouse.Reset();
	UE_LOG(LogAsdaTownWar, Log, TEXT("[город] сброс сессии (%s): окно скрыто, очередь 6745 пуста"), AsdaSessionResetReasonName(Reason));
}

void UAsdaTownWarSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.TownWar");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// 6745 разбираем из Tick (закон no-reentrant-net-callbacks).
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива.
	if (TArray<TArray<uint8>> Batch; PendingResults.Drain(Batch))
	{
		for (const TArray<uint8>& B : Batch) { OnTaxResult(B); }
	}
	if (!bOpen || !bControlsReady) { return; }
	Mouse.Update();
	// ★ Модальный гейт: под модальным окном (TWOBUT 86, нотисы) эталон мышь окну ниже не отдаёт.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (bThumbGrab || PressedArrow) { OnScrollRelease(); }
		Mouse.Reset();
		return;
	}
	TickMouse();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaTownWarSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kWindowZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZones();
		},
		[]() { UE_LOG(LogAsdaTownWar, Error, TEXT("[город] WBP_Town_as_WIN не загрузился (%s)"), kWindowWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaTownWarSubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	bControlsReady = false;
	bOpen = false;
	TxtTitle = TxtBody = TxtTax = TxtTaxV = TxtDisc = TxtDiscV = TxtPer = TxtOk = TxtNo = nullptr;
	BtnX = BtnOk = BtnNo = nullptr;
	MarkImg = Track = ArrowL = ArrowR = Thumb = nullptr;
	Relays.Reset();
	Mouse.Reset();
	bThumbGrab = false;
	PressedArrow = 0;
}

void UAsdaTownWarSubsystem::RegisterZones()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — девять планов `Town_as_WIN_0_0…8` (скин `Black_BottomRound_White2Border`, дамп ассета).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("Town_as_WIN_0_%d"), 9);
}

void UAsdaTownWarSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("Town_as_okbtn")) { BtnOk = B; }   // 79321
			if (N == TEXT("Town_as_nobtn")) { BtnNo = B; }   // 79322
			if (N == TEXT("Town_as_x"))     { BtnX  = B; }   // 79329
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Town_as_movetext"))   { TxtTitle = T; }   // 79301
			if (N == TEXT("Town_as_text_1"))     { TxtBody  = T; }   // 79303
			if (N == TEXT("Town_as_text_2"))     { TxtTax   = T; }   // 79310
			if (N == TEXT("Town_as_text_3"))     { TxtTaxV  = T; }   // 79311
			if (N == TEXT("Town_as_text_4"))     { TxtDisc  = T; }   // 79312
			if (N == TEXT("Town_as_text_5"))     { TxtDiscV = T; }   // 79313
			if (N == TEXT("Town_as_text_per"))   { TxtPer   = T; }   // 79315
			if (N == TEXT("Town_as_okbtn_Text")) { TxtOk    = T; }   // подпись 79321
			if (N == TEXT("Town_as_nobtn_Text")) { TxtNo    = T; }   // подпись 79322
			continue;
		}
		if (UImage* I = Cast<UImage>(W))
		{
			if (N == TEXT("Town_as_mark_18_0"))  { MarkImg = I; }   // 79302 план `NoImage_icon` 64×64
			if (N == TEXT("Town_as_slide_26_0")) { Track   = I; }   // 79314 ES0 жёлоб
			if (N == TEXT("Town_as_slide_26_1")) { ArrowL  = I; }   // 79314 ES1 ←
			if (N == TEXT("Town_as_slide_26_2")) { ArrowR  = I; }   // 79314 ES2 →
			if (N == TEXT("Town_as_slide_26_3")) { Thumb   = I; }   // 79314 ES3 бегунок
		}
	}
	// UV стрелок/бегунка в обычном состоянии — из кисти АССЕТА (правка владельца в ассете не откатывается нажатием).
	UImage* Planes[3] = { ArrowL, ArrowR, Thumb };
	for (int32 K = 0; K < 3; ++K)
	{
		ArrowUvNormal[K] = Planes[K] ? FBox2f(Planes[K]->GetBrush().GetUVRegion()) : FBox2f(ForceInit);
	}

	// ── Кнопки: ветка 66561 обработчика сообщества (ui_core.c:15484-15488). Звука клика у окна НЕТ. ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnOk, kIdOk, [this]() { OnOkClicked(); });
	Bind(BtnNo, kIdNo, [this]() { CloseWindow(); });
	Bind(BtnX,  kIdX,  [this]() { CloseWindow(); });

	// ── Статичные подписи `uiCampWarPanelBindControls@0xBCDDA0` (ui_core.c:17436-17462) ──
	SetLabel(kIdTitle, Loc(AsdaTownWarLoc::kTitle));        // 79301 ← 111907
	SetLabel(kIdTax,   Loc(AsdaTownWarLoc::kTaxCaption));   // 79310 ← 111908
	SetLabel(kIdDisc,  Loc(AsdaTownWarLoc::kDiscCaption));  // 79312 ← 111903
	SetLabel(kIdOk,    Loc(AsdaTownWarLoc::kBtnOk));        // 79321 ← 111904 (`CWidget_SetTextAll4`)
	SetLabel(kIdNo,    Loc(AsdaTownWarLoc::kBtnClose));     // 79322 ← 5035
	// 79303: TextPos «4 4», TextAlign 0 (влево-вверх), rect 282×72 — перенос по ширине (AutoWrap в канвасе запрещён,
	//   закон ui-autowrap-canvas-gotcha).
	if (TxtBody) { TxtBody->SetAutoWrapText(false); TxtBody->SetWrapTextAt(282.f - 4.f); }

	FoundControls = 0; TotalControls = 0;
	auto Count = [this](const UObject* O) { ++TotalControls; if (O) { ++FoundControls; } };
	Count(TxtTitle); Count(BtnX); Count(MarkImg); Count(TxtBody); Count(TxtTax); Count(TxtTaxV); Count(TxtDisc); Count(TxtDiscV);
	Count(Track); Count(ArrowL); Count(ArrowR); Count(Thumb); Count(TxtPer); Count(BtnOk); Count(TxtOk); Count(BtnNo); Count(TxtNo);
	bControlsReady = (BtnOk && BtnNo && BtnX && Track && ArrowL && ArrowR && Thumb && TxtPer && TxtBody);
	UE_LOG(LogAsdaTownWar, Log, TEXT("%s"), *DumpControls());
	if (!bControlsReady)
	{
		UE_LOG(LogAsdaTownWar, Error, TEXT("[город] ключевые контролы не разобраны (найдено %d/%d)"), FoundControls, TotalControls);
	}
}

void UAsdaTownWarSubsystem::OpenFromNpc()
{
	// `CUniconWar_UpdateFactionInfoPanel@0xBCE060` (ui_core.c:17468-17546).
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	int32 Slot = AsdaUi::WarCitySlot(GI);   // карта 3 → 0, 0 → 1, 7 → 2
	int32 TitleLoc = (Slot == 0) ? 603 : (Slot == 1) ? 600 : (Slot == 2) ? 707 : 0;
	if (Slot < 0) { Slot = 0; }             // [ОРИГИНАЛ] вне города — слот 0 и заголовок loc 0 (недостижимо, см. .h)
	const UAsdaNetSubsystem::FAsdaCityOwner& C = Net->CityOwner[Slot];
	if (C.OwnerFaction == 3)
	{
		Notice(AsdaTownWarLoc::kNeutral, Loc(AsdaTownWarLoc::kNeutral));   // ONEBUT 111913, окно не открывается
		UE_LOG(LogAsdaTownWar, Log, TEXT("[город] открытие: город слота %d не занят — 111913"), Slot);
		return;
	}
	if (!EnsureWidget()) { return; }
	OpenSlot = Slot;
	bOpen = true;   // +2248 = 1
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kWindowId, true);
		WM->BringWindowToFront(kWindowId);
	}
	const int32 Pct = C.TaxPercent;
	SetLabel(kIdTaxV,  FString::Printf(TEXT("+ %d%%"), Pct));   // 79311
	SetLabel(kIdDiscV, FString::Printf(TEXT("- %d%%"), Pct));   // 79313
	SetLabel(kIdPer,   FString::Printf(TEXT("%d%%"), Pct));     // 79315
	SetScrollPos(Pct);                                          // `uiScrollbarSetPos(war[558], N)` → 69638 → «N%»
	// Знак лагеря-хозяина 79302: `uiMakeAxisVec2(faction)` + `DrawTextureRect("CampWar_MarkImage.tex", u, v, 64, 64)`.
	UAsdaClanFlag::ApplyCampMark(MarkImg, C.OwnerFaction, 64.f);
	// Тело 79303 = `sprintf("%s%s%s", 111909(…), 111910(…), 111911)`.
	const FString City    = (TitleLoc > 0) ? Loc(TitleLoc) : Loc(0);
	const FString Faction = Loc(C.OwnerFaction + AsdaTownWarLoc::kFactionBase);
	TArray<FString> A1;
	if (CVarFixTextArgs.GetValueOnGameThread() != 0) { A1 = { FString::FromInt(Pct), City, C.ClanName }; }   // [ПОРТ] Д-ТВ-1
	else                                              { A1 = { City, C.ClanName, FString::FromInt(Pct) }; }   // порядок кода оригинала
	const FString L1 = AsdaUi::FormatPrintfArgs(Loc(AsdaTownWarLoc::kBody1), A1);
	const FString L2 = AsdaUi::FormatPrintfArgs(Loc(AsdaTownWarLoc::kBody2), { Faction, C.MasterName });
	const FString L3 = Loc(AsdaTownWarLoc::kBody3);
	SetLabel(kIdBody, L1 + L2 + L3);
	UE_LOG(LogAsdaTownWar, Log, TEXT("[город] окно 79300 открыто: слот %d (loc %d), фракция %d, налог %d%%, клан «%s», глава «%s» (charId %d, индекс %d)"),
		Slot, TitleLoc, C.OwnerFaction, Pct, *C.ClanName, *C.MasterName, C.MasterCharId, C.MasterCharIdx);
}

void UAsdaTownWarSubsystem::CloseWindow()
{
	// `sub_BCE470`: +2248 = 0, `CControl_SetVisible(79300, 0)`.
	bOpen = false;
	bThumbGrab = false;
	if (PressedArrow) { SetArrowPressed(PressedArrow, false); PressedArrow = 0; }
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	Mouse.Reset();
	UE_LOG(LogAsdaTownWar, Log, TEXT("[город] окно 79300 закрыто"));
}

// ═════════════════════════════ «НАЖМИ» И СТАВКА ═══════════════════════════════════════════════

void UAsdaTownWarSubsystem::OnOkClicked()
{
	// `uiCampWarPanelTick@0xBCE4B0` (ui_core.c:17550-17587).
	const double NowMs = FPlatformTime::Seconds() * 1000.0;
	if (LastOkMs >= 0.0 && (NowMs - LastOkMs) <= kOkThrottleMs)
	{
		UE_LOG(LogAsdaTownWar, Log, TEXT("[город] «Нажми» отбит замком 2000 мс (+2256), молча — как эталон"));
		return;
	}
	LastOkMs = NowMs;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	int32 Slot = AsdaUi::WarCitySlot(GI);
	if (Slot < 0) { Slot = 0; }   // [ОРИГИНАЛ] `v5 = 0` вне городов
	const UAsdaNetSubsystem::FAsdaCityOwner& C = Net->CityOwner[Slot];
	// `dword_29087FA[91·i] == g_LocalPlayer+1052 (charId) && byte_29087FE[91·i] == g_LocalPlayer+1076 (индекс персонажа)`.
	const bool bMaster = AsdaSelf::IsSelfCharId(Net, C.MasterCharId) && (C.MasterCharIdx == (int32)Net->LastSelectedRawSlot);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	if (!bMaster)
	{
		const FString Faction = Loc(C.OwnerFaction + AsdaTownWarLoc::kFactionBase);
		Notice(AsdaTownWarLoc::kNotMaster, AsdaUi::FormatPrintfArgs(TownWarMsgLoc(GI, AsdaTownWarLoc::kNotMaster), { Faction, C.MasterName }));
		UE_LOG(LogAsdaTownWar, Log, TEXT("[город] «Нажми»: не глава (глава charId %d/%d, я %d/%d) — 111900"),
			C.MasterCharId, C.MasterCharIdx, Net->SelectedCharId, (int32)Net->LastSelectedRawSlot);
		return;
	}
	// TWOBUT (1, 86): «111921 %d» — число = позиция полосы НА МОМЕНТ «Нажми» (`sub_9331D5(v8[558])`).
	const FString Text = AsdaUi::FormatPrintfArgs(TownWarMsgLoc(GI, AsdaTownWarLoc::kConfirm), { FString::FromInt(ScrollPos) });
	NoticeTrace.Add(AsdaTownWarLoc::kConfirm);
	LastNoticeText = Text;
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		TWeakObjectPtr<UAsdaTownWarSubsystem> Self(this);
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [Self](bool bYes)
		{
			UAsdaTownWarSubsystem* S = Self.Get();
			if (!S || !bYes) { return; }   // «Отмена» (1524) ветки 86 в эталоне нет — ничего
			UGameInstance* G = S->GetGameInstance();
			UAsdaNetSubsystem* N = G ? G->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			// `case 0x56: AntiTamper_SendHeartbeatPacket(g_pCommunityWnd)` (ui_core.c:57161): позиция полосы — НА МОМЕНТ «Да».
			if (N) { N->SendTownTaxChange6744(N->SelectedCharId, S->ScrollPos); }
		}, /*GaugeGateSec*/0.f, AsdaTownWarLoc::kConfirmOk, AsdaTownWarLoc::kConfirmNo, /*EtalonNoticeId*/86, /*EtalonKind*/1);
	}
	UE_LOG(LogAsdaTownWar, Log, TEXT("[город] «Нажми» главой: подтверждение TWOBUT (1, 86), ставка %d%%"), ScrollPos);
}

void UAsdaTownWarSubsystem::OnTaxResult(const TArray<uint8>& B)
{
	// `CChat_ShowItemSubnotice@0xBCE6B0` (chat.c:5805-5837): +0 код, +1 ставка.
	if (B.Num() < 2) { return; }
	const int32 Code = B[0];
	const int32 Rate = B[1];
	if (Code == 1)
	{
		Notice(AsdaTownWarLoc::kResultOk, AsdaUi::FormatPrintfArgs(TownWarMsgLoc(GetGameInstance(), AsdaTownWarLoc::kResultOk), { FString::FromInt(Rate) }));
	}
	else if (Code == 3)
	{
		Notice(AsdaTownWarLoc::kResultLater, AsdaUi::FormatPrintfArgs(TownWarMsgLoc(GetGameInstance(), AsdaTownWarLoc::kResultLater), { FString::FromInt(Rate) }));
	}
	else if ((Code == 2 || Code == 4) && CVarFixRawCode.GetValueOnGameThread() != 0)
	{
		// [ПОРТ] Д-ТВ-3: та же строка, что клиент печатает при своём отказе «Нажми» (111900: фракция, глава города).
		UGameInstance* GI = GetGameInstance();
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		int32 Slot = AsdaUi::WarCitySlot(GI);
		if (Slot < 0) { Slot = 0; }
		const FString Faction = Net ? Loc(Net->CityOwner[Slot].OwnerFaction + AsdaTownWarLoc::kFactionBase) : FString();
		const FString Master  = Net ? Net->CityOwner[Slot].MasterName : FString();
		Notice(AsdaTownWarLoc::kNotMaster, AsdaUi::FormatPrintfArgs(TownWarMsgLoc(GI, AsdaTownWarLoc::kNotMaster), { Faction, Master }));
	}
	else
	{
		Notice(-1, FString::Printf(TEXT("%d"), Code));   // `sprintf(Buffer, "%d", v6[0])` — как оригинал
	}
	UE_LOG(LogAsdaTownWar, Log, TEXT("[город] 6745: код %d, ставка %d%%"), Code, Rate);
}

// ═════════════════════════════ ПОЛОСА 79314 ════════════════════════════════════════════════════

void UAsdaTownWarSubsystem::SetScrollPos(int32 Pos)
{
	// `uiScrollbarSetPos@0x12FF800`: событие 69638 → позиция с клампом → раскладка бегунка.
	RaiseScrollEvent(kEvSetPos, Pos);
	ScrollPos = FMath::Clamp(Pos, 0, kRangeMax);
	LayoutThumb();
}

void UAsdaTownWarSubsystem::RaiseScrollEvent(int32 EventId, int32 Value)
{
	// Окно сообщества пропускает в `settings_DispatchFlagById` события (0x11000…0x11002] и ≥ 0x11005 (ui_core.c:15462-15475);
	//   для 79314 диспетчер пишет +2252 = значение и «%d%%» в 79315 (game_main.c:8505-8510). 69635/69636 туда не доходят.
	const bool bDispatched = (EventId > 0x11000 && EventId <= 0x11002) || EventId >= 0x11005
		|| ((EventId == kEvPageUp || EventId == kEvPageDown) && CVarFixTrackPage.GetValueOnGameThread() != 0);   // [ПОРТ] Д-ТВ-2
	if (!bDispatched) { return; }
	StoredRate = Value;
	SetLabel(kIdPer, FString::Printf(TEXT("%d%%"), Value));
}

void UAsdaTownWarSubsystem::LayoutThumb()
{
	// `uiScrollbarLayoutThumb@0x12FEA40`, ветвь горизонтали (+609 == 0): левый край = жёлоб.left + (жёлоб − бегунок)·pos/max.
	if (!Thumb) { return; }
	UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Thumb->Slot);
	if (!S) { return; }
	FMargin O = S->GetOffsets();
	O.Left = (float)GetThumbLocalX();
	S->SetOffsets(O);
}

int32 UAsdaTownWarSubsystem::GetThumbLocalX() const
{
	const int32 Off = (kTrackWidth - kThumbWidth) * ScrollPos / kRangeMax;   // целочисленное деление, как эталон
	return kTrackLeft + Off;
}

void UAsdaTownWarSubsystem::OnScrollButton(int32 ButtonId, float CursorAbsX)
{
	// `uiScrollbarOnButtonEvent@0x12FF2C0`.
	switch (ButtonId)
	{
	case 1:   // ← : pos − 6, событие 69633
	{
		const int32 P = FMath::Max(0, ScrollPos - kLineStep);
		RaiseScrollEvent(kEvLineUp, P);
		ScrollPos = P;
		LayoutThumb();
		break;
	}
	case 2:   // → : pos + 6, событие 69634
	{
		const int32 P = FMath::Min(kRangeMax, ScrollPos + kLineStep);
		RaiseScrollEvent(kEvLineDown, P);
		ScrollPos = P;
		LayoutThumb();
		break;
	}
	case 3:   // бегунок схвачен (+608 = 1)
		bThumbGrab = true;
		break;
	case 0:   // жёлоб
	{
		if (CVarFixTrackPage.GetValueOnGameThread() == 0)
		{
			// [ОРИГИНАЛ] сравнение по Y: курсор на жёлобе горизонтальной полосы всегда в Y-полосе бегунка → `return 0`.
			UE_LOG(LogAsdaTownWar, Log, TEXT("[город] клик по жёлобу 79314: как оригинал — ничего (сравнение по Y)"));
			break;
		}
		// [ПОРТ] Д-ТВ-2: та же логика по X — левее бегунка «страница» к началу, правее — к концу.
		FVector2D Mn, Mx;
		if (!AsdaUi::WidgetAbsRect(Thumb, Mn, Mx)) { break; }
		if (CursorAbsX >= Mn.X && CursorAbsX <= Mx.X) { break; }   // на бегунке — `return 0`
		if (CursorAbsX < Mn.X)
		{
			const int32 P = FMath::Max(0, ScrollPos - kPageStep);
			RaiseScrollEvent(kEvPageUp, P);
			ScrollPos = P;
		}
		else
		{
			const int32 P = FMath::Min(kRangeMax, ScrollPos + kPageStep);
			RaiseScrollEvent(kEvPageDown, P);
			ScrollPos = P;
		}
		LayoutThumb();
		break;
	}
	default: break;
	}
}

void UAsdaTownWarSubsystem::OnScrollDrag(float CursorAbsX)
{
	// `uiScrollbarOnMouseDrag@0x12FF020`, ветвь горизонтали: x = курсор − жёлоб.left − бегунок/2;
	//   v = (int)(max · (x / (жёлоб − бегунок))), кламп 0…max, событие 69637.
	if (!bThumbGrab || !Track) { return; }
	FVector2D Mn, Mx;
	if (!AsdaUi::WidgetAbsRect(Track, Mn, Mx)) { return; }
	const double Scale = (Mx.X > Mn.X) ? (double)(Mx.X - Mn.X) / (double)kTrackWidth : 1.0;
	const double LocalX = (double)(CursorAbsX - Mn.X) / Scale - (double)(kThumbWidth >> 1);
	int32 V = (int32)((double)kRangeMax * (LocalX / (double)(kTrackWidth - kThumbWidth)));   // усечение к нулю (`_ftol`)
	V = FMath::Clamp(V, 0, kRangeMax);
	RaiseScrollEvent(kEvDrag, V);
	ScrollPos = V;
	LayoutThumb();
}

void UAsdaTownWarSubsystem::OnScrollRelease()
{
	bThumbGrab = false;
	if (PressedArrow) { SetArrowPressed(PressedArrow, false); PressedArrow = 0; }
}

void UAsdaTownWarSubsystem::SetArrowPressed(int32 Plane, bool bPressed)
{
	// SS2 скина: у ES2 (→) UV сдвинута на −1 тексель; у ES1/ES3 SS2 совпадает с SS0 — картинка не меняется.
	if (Plane != 2 || !ArrowR) { return; }
	FSlateBrush Br = ArrowR->GetBrush();
	FBox2f Uv = ArrowUvNormal[1];
	if (bPressed) { Uv.Min += FVector2f(kPressedUvShift, kPressedUvShift); Uv.Max += FVector2f(kPressedUvShift, kPressedUvShift); }
	Br.SetUVRegion(FBox2D(FVector2D(Uv.Min), FVector2D(Uv.Max)));
	ArrowR->SetBrush(Br);
}

void UAsdaTownWarSubsystem::TickMouse()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Перетаскивание идёт и вне окна, пока ЛКМ зажата (`+608` эталона снимается только отпусканием).
	if (bThumbGrab && Mouse.bLmb) { OnScrollDrag((float)Mouse.Cursor.X); }
	if (Mouse.bLmbUp) { OnScrollRelease(); }
	// Закон шапки `AsdaWindowSubsystem.h`: чужой верхний слой / окно выше под курсором — нажатия не наши.
	if (WM->IsPointerBlockedByTopmost(kWindowId) || WM->IsCursorOverHigherWindow(kWindowId)) { return; }
	if (!Mouse.bLmbDown) { return; }
	// Порядок проверки — порядок отрисовки сверху вниз (стрелки поверх бегунка, бегунок поверх жёлоба).
	if (ArrowL && AsdaUi::IsCursorOverWidget(ArrowL)) { OnScrollButton(1, (float)Mouse.Cursor.X); return; }
	if (ArrowR && AsdaUi::IsCursorOverWidget(ArrowR)) { PressedArrow = 2; SetArrowPressed(2, true); OnScrollButton(2, (float)Mouse.Cursor.X); return; }
	if (Thumb  && AsdaUi::IsCursorOverWidget(Thumb))  { OnScrollButton(3, (float)Mouse.Cursor.X); return; }
	if (Track  && AsdaUi::IsCursorOverWidget(Track))  { OnScrollButton(0, (float)Mouse.Cursor.X); return; }
}

// ═════════════════════════════ ПРОЧЕЕ ═══════════════════════════════════════════════════════════

void UAsdaTownWarSubsystem::Notice(int32 LocId, const FString& Text)
{
	// Все нотисы окна — однокнопочное окно-сообщение `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)`.
	NoticeTrace.Add(LocId);
	LastNoticeText = Text;
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::War, Text);
}

FString UAsdaTownWarSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaTownWarSubsystem::SetLabel(int32 IdNum, const FString& S)
{
	UTextBlock* T = nullptr;
	switch (IdNum)
	{
	case kIdTitle: T = TxtTitle; break;
	case kIdBody:  T = TxtBody;  break;
	case kIdTax:   T = TxtTax;   break;
	case kIdTaxV:  T = TxtTaxV;  break;
	case kIdDisc:  T = TxtDisc;  break;
	case kIdDiscV: T = TxtDiscV; break;
	case kIdPer:   T = TxtPer;   break;
	case kIdOk:    T = TxtOk;    break;
	case kIdNo:    T = TxtNo;    break;
	default: break;
	}
	if (T) { T->SetText(FText::FromString(S)); }
}

FString UAsdaTownWarSubsystem::GetText(int32 IdNum) const
{
	const UTextBlock* T = nullptr;
	switch (IdNum)
	{
	case kIdTitle: T = TxtTitle; break;
	case kIdBody:  T = TxtBody;  break;
	case kIdTax:   T = TxtTax;   break;
	case kIdTaxV:  T = TxtTaxV;  break;
	case kIdDisc:  T = TxtDisc;  break;
	case kIdDiscV: T = TxtDiscV; break;
	case kIdPer:   T = TxtPer;   break;
	case kIdOk:    T = TxtOk;    break;
	case kIdNo:    T = TxtNo;    break;
	default: break;
	}
	return T ? T->GetText().ToString() : FString();
}

bool UAsdaTownWarSubsystem::IsMarkShown() const
{
	return MarkImg && MarkImg->GetBrush().GetResourceObject() != nullptr;
}

UWidget* UAsdaTownWarSubsystem::GetHitWidget(int32 Which) const
{
	switch (Which)
	{
	case 0: return Track;
	case 1: return ArrowL;
	case 2: return ArrowR;
	case 3: return Thumb;
	case 4: return BtnOk;
	case 5: return BtnX;
	case 6: return BtnNo;
	case 7: return Widget;
	default: return nullptr;
	}
}

FString UAsdaTownWarSubsystem::DumpControls() const
{
	auto L = [](const TCHAR* Name, const UObject* O) { return FString::Printf(TEXT("  %-34s %s\n"), Name, O ? *O->GetName() : TEXT("— НЕТ —")); };
	FString S = FString::Printf(TEXT("[город] окно 79300 Town_as_WIN разобрано: %d/%d\n"), FoundControls, TotalControls);
	S += L(TEXT("79301 заголовок"), TxtTitle);
	S += L(TEXT("79329 X"), BtnX);
	S += L(TEXT("79302 знак лагеря (план)"), MarkImg);
	S += L(TEXT("79303 тело"), TxtBody);
	S += L(TEXT("79310 «Ставка налога»"), TxtTax);
	S += L(TEXT("79311 «+ N%»"), TxtTaxV);
	S += L(TEXT("79312 «Скидка»"), TxtDisc);
	S += L(TEXT("79313 «- N%»"), TxtDiscV);
	S += L(TEXT("79314 ES0 жёлоб"), Track);
	S += L(TEXT("79314 ES1 ←"), ArrowL);
	S += L(TEXT("79314 ES2 →"), ArrowR);
	S += L(TEXT("79314 ES3 бегунок"), Thumb);
	S += L(TEXT("79315 «N%»"), TxtPer);
	S += L(TEXT("79321 «Нажми»"), BtnOk);
	S += L(TEXT("79321 подпись"), TxtOk);
	S += L(TEXT("79322 «Отмена»"), BtnNo);
	S += L(TEXT("79322 подпись"), TxtNo);
	return S;
}

// ═════════════════════════════ КОНСОЛЬ ═════════════════════════════════════════════════════════

// Открыть окно так же, как кнопка NPC 30076 (ОФЛАЙН-проверка — при живом соединении команда отказывает).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTownWarOpen(
	TEXT("asda.TownWar.Open"),
	TEXT("asda.TownWar.Open — открыть окно налога города 79300 так же, как кнопка NPC 30076 (данные — последний 6743)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.TownWar.Open"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (UAsdaTownWarSubsystem* S = GI ? GI->GetSubsystem<UAsdaTownWarSubsystem>() : nullptr) { S->OpenFromNpc(); }
	}));
#endif // ASDA_DEV_CONSOLE

// Дамп окна: контролы, позиция полосы, тексты.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GTownWarDump(
	TEXT("asda.TownWar.Dump"),
	TEXT("asda.TownWar.Dump — дамп окна налога города 79300 (контролы, полоса, тексты)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaTownWarSubsystem* S = GI ? GI->GetSubsystem<UAsdaTownWarSubsystem>() : nullptr;
		if (!S) { return; }
		UE_LOG(LogAsdaTownWar, Warning, TEXT("%s"), *S->DumpControls());
		UE_LOG(LogAsdaTownWar, Warning, TEXT("[город] открыто=%d слот=%d полоса=%d (+2252=%d) бегунок X=%d; 79311 «%s» 79313 «%s» 79315 «%s»; тело «%s»"),
			S->IsOpen() ? 1 : 0, S->GetCitySlot(), S->GetScrollPos(), S->GetStoredRate(), S->GetThumbLocalX(),
			*S->GetText(79311), *S->GetText(79313), *S->GetText(79315), *S->GetText(79303));
	}));
#endif // ASDA_DEV_CONSOLE
