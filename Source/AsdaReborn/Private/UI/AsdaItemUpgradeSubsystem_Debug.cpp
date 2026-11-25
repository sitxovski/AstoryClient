// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemUpgradeSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaItemUpgradeSubsystem.cpp, дословный перенос): дампы, отладочные помощники, дев-команды (сим. итога, эффект, снимки панели).
//   Общее для частей — в AsdaItemUpgradeSubsystem_Private.h; шапка и реверс — в AsdaItemUpgradeSubsystem.cpp.
#include "UI/AsdaItemUpgradeSubsystem.h"


#include "AsdaItemUpgradeSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaItemUpgradeSubsystem*.cpp

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════
FString UAsdaItemUpgradeSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно улучшения снаряжения 20000: разобранные контролы ===\n");
	int32 Found = 0, Total = 0;
	auto Line = [&S, &Found, &Total](const TCHAR* Id, const UObject* W)
	{
		++Total; if (W) { ++Found; }
		S += FString::Printf(TEXT("  %-40s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ"));
	};
	Line(TEXT("20001 Title"),               TxtTitle);
	Line(TEXT("20002 CloseBtn"),            BtnClose);
	Line(TEXT("20003 Icon (план 64×64)"),   TargetIcon);
	Line(TEXT("20006 Name (rich)"),         FmtName.Node);
	Line(TEXT("20007 Grade «Уровень»"),     TxtLevelLbl);
	Line(TEXT("20008 GradeBefore"),         TxtLevelBef);
	Line(TEXT("20009 GradeAfter"),          TxtLevelAft);
	Line(TEXT("20010 Power «Эффект»"),      TxtEffectLbl);
	Line(TEXT("20011 PowerBefore"),         TxtEffectBef);
	Line(TEXT("20012 PowerAfter"),          TxtEffectAft);
	Line(TEXT("20013 GuideTxt (rich)"),     FmtGuide.Node);
	Line(TEXT("20014 Move — ручка окна"),   DragHandle20014);
	Line(TEXT("20015 Chance «Попыток»"),    TxtTriesLbl);
	Line(TEXT("20016 hanceBefore"),         TxtTriesBef);
	Line(TEXT("20017 hanceAfter"),          TxtTriesAft);
	Line(TEXT("20058 ScrollSel"),           FrameScrollSel);
	Line(TEXT("20060 ItemSel"),             FrameItemSel);
	Line(TEXT("20065 ScrollTitle"),         TxtTabScroll);
	Line(TEXT("20066 ItemSec"),             TxtTabGear);
	Line(TEXT("20080 SubMat — блок помощн."),Group20080);
	Line(TEXT("20140 SubSel0"),             FrameSubSel0);
	Line(TEXT("20141 SubSel1"),             FrameSubSel1);
	Line(TEXT("20142 SubTitle"),            TxtTabHelpers);
	for (int32 K = 0; K < 3; ++K)
	{
		Line(*FString::Printf(TEXT("%d MatIcon%d"), 20150 + 3 * K, K),   CardIcons.IsValidIndex(K) ? CardIcons[K].Get() : nullptr);
		Line(*FString::Printf(TEXT("%d MatTitle%d"), 20151 + 3 * K, K),  TxtCardTitle.IsValidIndex(K) ? TxtCardTitle[K].Get() : nullptr);
		Line(*FString::Printf(TEXT("%d MatClaBtn%d"), 20152 + 3 * K, K), BtnCardClear.IsValidIndex(K) ? BtnCardClear[K].Get() : nullptr);
	}
	Line(TEXT("20190 Btn «Улучшить»"),      BtnUpgrade);
	Line(TEXT("20191 ClsBtn2 «Отмена»"),    BtnCancel);
	Line(TEXT("20192 p_text «+N»"),         TxtPanelValue);
	Line(TEXT("20193 полоса «+N%»"),        Panel20193);
	Line(TEXT("20194 GFONT_item_S"),        Panel20194);
	Line(TEXT("20195 Box_Brown (подсказка)"),Panel20195);
	Line(TEXT("20196 Option_st_w"),         Panel20196);
	Line(TEXT("20197 Option_st «Особый»"),  BtnSpecial);
	static const TCHAR* kFxName[(int32)EAsdaUpgradeFx::Count] = {
		TEXT("20070 Effect1"), TEXT("20071 Effect2"), TEXT("20072 Click"),
		TEXT("20004 Success"), TEXT("20005 Fail_Ani"), TEXT("20073 Fail") };
	for (int32 F = 0; F < (int32)EAsdaUpgradeFx::Count; ++F)
	{
		const bool bHas = FxGroups.IsValidIndex(F) && FxGroups[F].Num() > 0;
		++Total; if (bHas) { ++Found; }
		S += FString::Printf(TEXT("  %-40s %s (узлов %d, анимированный план %s)\n"), kFxName[F], bHas ? TEXT("есть") : TEXT("НЕТ"),
			FxGroups.IsValidIndex(F) ? FxGroups[F].Num() : 0,
			(FxPlanes.IsValidIndex(F) && FxPlanes[F]) ? TEXT("да") : TEXT("нет"));
	}
	auto CountOf = [](const auto& Arr) { int32 N = 0; for (const auto& P : Arr) { if (P) { ++N; } } return N; };
	const int32 Gi = CountOf(GridIcons), Gl = CountOf(GridLabels), Si = CountOf(ScrollIcons), Sl = CountOf(ScrollLabels);
	const int32 Ai = CountOf(H97Icons), Al = CountOf(H97Labels), Bi = CountOf(H98Icons), Bl = CountOf(H98Labels);
	int32 Masks = 0; for (const auto& G : DisableGroups) { if (G.Num() > 0) { ++Masks; } }
	S += FString::Printf(TEXT("  20020…20049 сетка: иконки %d/30, подписи %d/30 · 20100…20129 затемнители %d/30\n"), Gi, Gl, Masks);
	S += FString::Printf(TEXT("  20050…20055 свитки: иконки %d/6, подписи %d/6\n"), Si, Sl);
	S += FString::Printf(TEXT("  20081…20083 помощники 97: %d/3 (%d) · 20090…20092 помощники 98: %d/3 (%d)\n"), Ai, Al, Bi, Bl);
	Found += Gi + Gl + Si + Sl + Ai + Al + Bi + Bl + Masks;
	Total += 30 + 30 + 6 + 6 + 3 + 3 + 3 + 3 + 30;
	S += FString::Printf(TEXT("  ИТОГО: найдено %d/%d\n"), Found, Total);
	return S;
}

FString UAsdaItemUpgradeSubsystem::DumpState() const
{
	auto EntryStr = [this](const FAsdaItemUpgradeEntry& E) -> FString
	{
		const FAsdaItemInstance* It = LiveOf(E);
		if (!It || It->IsEmpty()) { return TEXT("—"); }
		return FString::Printf(TEXT("код=%d к=%d яч=%d сл=%d ступ=%d попыт=%d/%d %%=%d кол=%d"), It->ItemCode, E.Container, E.CellIndex,
			(int32)It->SlotIndex, (int32)It->GemGrade, RecByte(*It, 25), RecByte(*It, 26), It->EnchantPercent, It->Amount);
	};
	const double Now = FPlatformTime::Seconds();
	FString S = FString::Printf(TEXT("  20013 гайд: «%s»\n"), *FmtGuide.LastRaw);   // ★ Д-17: сырой текст подсказки — для самотеста
	S += FString::Printf(
		TEXT("=== окно улучшения снаряжения: состояние ===\n")
		TEXT("  открыто=%d живо(+3392)=%d контролы=%d свободен(+3394)=%d взведён(+3404)=%d диалог72=%d\n")
		TEXT("  замок N-2: в полёте=%d (%.1f с) · сохранено: свиток к=%d сл=%d, цель сл=%d · буфер результата «%s» ждёт=%d\n")
		TEXT("  попап 19100 открыт=%d\n"),
		bOpen ? 1 : 0, bLive ? 1 : 0, bControlsReady ? 1 : 0, bIdle ? 1 : 0, bArmed ? 1 : 0, bConfirmOpen ? 1 : 0,
		ReplyLatch.IsArmed() ? 1 : 0, ReplyLatch.IsArmed() ? Now - ReplyLatch.ArmedAt : 0.0, SavedScrollContainer, SavedScrollSlot, SavedTargetSlot,
		*ResultText, bResultPending ? 1 : 0, bPopupOpen ? 1 : 0);
	S += FString::Printf(TEXT("  цель [%d]: %s\n  свиток [%d]: %s\n  помощник97 [%d]: %s\n  помощник98 [%d]: %s\n"),
		TargetIdx, *EntryStr(Target), ScrollIdx, *EntryStr(Scroll), H97Idx, *EntryStr(Helper97), H98Idx, *EntryStr(Helper98));
	S += FString::Printf(TEXT("  списки: сетка %d, свитки %d, помощники97 %d, помощники98 %d\n"),
		GridList.Num(), ScrollList.Num(), H97List.Num(), H98List.Num());
	for (int32 I = 0; I < GridList.Num(); ++I) { S += FString::Printf(TEXT("   сетка[%2d] %s\n"), I, *EntryStr(GridList[I])); }
	for (int32 I = 0; I < ScrollList.Num(); ++I) { S += FString::Printf(TEXT("   свиток[%d] %s\n"), I, *EntryStr(ScrollList[I])); }
	for (int32 F = 0; F < (int32)EAsdaUpgradeFx::Count; ++F)
	{
		const bool bVis = FxGroups.IsValidIndex(F) && FxGroups[F].Num() > 0 && FxGroups[F][0]
			&& FxGroups[F][0]->GetVisibility() != ESlateVisibility::Collapsed;
		S += FString::Printf(TEXT("   эффект %d: виден=%d срок=%.2f\n"), F, bVis ? 1 : 0, FxDeadline[F] > 0.0 ? FxDeadline[F] - Now : 0.0);
	}
	return S;
}

UWidget* UAsdaItemUpgradeSubsystem::DebugCellWidget(int32 Kind, int32 Idx) const
{
	// Самотест видов свитков (04.11.2026): ячейка для клика настоящей мышью — те же виджеты, что опрашивает `TickMouse`.
	auto Pick = [Idx](const TArray<TObjectPtr<UImage>>& A) -> UWidget* { return A.IsValidIndex(Idx) ? A[Idx].Get() : nullptr; };
	switch (Kind)
	{
	case 0: return Pick(GridIcons);
	case 1: return Pick(ScrollIcons);
	case 2: return Pick(H97Icons);
	case 3: return Pick(H98Icons);
	case 4: return Pick(CardIcons);
	case 5: return BtnUpgrade;
	case 6: return TargetIcon;
	default: return nullptr;
	}
}

void UAsdaItemUpgradeSubsystem::DebugSelection(int32& OutTarget, int32& OutScroll, int32& OutH97, int32& OutH98, FString& OutEffectAfter) const
{
	// Самотест видов свитков: что выбрано (код живой ячейки) и что написано в «эффект после» 20012.
	auto CodeOf = [this](const FAsdaItemUpgradeEntry& E) { const FAsdaItemInstance* It = LiveOf(E); return (It && !It->IsEmpty()) ? It->ItemCode : 0; };
	OutTarget = CodeOf(Target); OutScroll = CodeOf(Scroll); OutH97 = CodeOf(Helper97); OutH98 = CodeOf(Helper98);
	OutEffectAfter = TxtEffectAft ? TxtEffectAft->GetText().ToString() : FString();
}

TArray<int32> UAsdaItemUpgradeSubsystem::DebugScrollCodes() const
{
	// Самотест видов свитков: весь `ScrollList` (коды записей списка).
	TArray<int32> Out;
	for (const FAsdaItemUpgradeEntry& E : ScrollList) { Out.Add(E.ItemCode); }
	return Out;
}

FBox2D UAsdaItemUpgradeSubsystem::DebugPanelShotMode(int32 Mode, int32 Percent)
{
	// ★ Волна 12: ТОЛЬКО видимость узлов на время кадра (разметка ассета не меняется, закон no-runtime-widget-fixes
	//   не нарушается: после кадра −1 возвращает всё, как ставит боевой код). Ключ панели выставляет самотест.
	UWidget* Frame = Widget ? Widget->GetWidgetFromName(FName(TEXT("Box_White_Ot200_225_0"))) : nullptr;   // рамка строки (без ID, `Visible = 0`)
	const bool bHideSpecial = (Mode == 2 || Mode == 3);
	bDebugShow20194 = (Mode == 3);
	UpdateEnhancePanel(Percent, /*bHasTarget*/ true);   // боевой путь: 20193/20195 по ключу, 20194 — только режим 3
	AsdaUi::SetShownDecor(Panel20196, !bHideSpecial);
	AsdaUi::SetShownHit(BtnSpecial, !bHideSpecial);
	AsdaUi::SetShownNoHit(Frame, Mode == 3);
	// Экранный прямоугольник строки панели: рамка 20195 (436×28) и соседи — для кропа.
	FBox2D Box(ForceInit);
	for (const UWidget* W : { (const UWidget*)Frame, (const UWidget*)Panel20195.Get(), (const UWidget*)Panel20193.Get(),
	                          (const UWidget*)Panel20194.Get(), (const UWidget*)Panel20196.Get() })
	{
		FVector2D Mn, Mx;
		if (W && AsdaUi::WidgetAbsRect(W, Mn, Mx) && Mx.X > Mn.X) { Box += Mn; Box += Mx; }
	}
	UE_LOG(LogAsdaUpgrade, Warning, TEXT("[заточка] кадр панели «+N%%»: режим %d, процент %d, ключ %d → 20193=%d 20194=%d 20195=%d 20196=%d; "
		"строка на экране (%.0f,%.0f)…(%.0f,%.0f); 20192 «%s»"), Mode, Percent, CVarEnhancePanel.GetValueOnGameThread(),
		Panel20193 ? (int32)Panel20193->GetVisibility() : -1, Panel20194 ? (int32)Panel20194->GetVisibility() : -1,
		Panel20195 ? (int32)Panel20195->GetVisibility() : -1, Panel20196 ? (int32)Panel20196->GetVisibility() : -1,
		Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y, TxtPanelValue ? *TxtPanelValue->GetText().ToString() : TEXT("-"));
	return Box;
}

bool UAsdaItemUpgradeSubsystem::DebugPressButton(int32 IdNum)
{
	UButton* Btn = nullptr;
	switch (IdNum)
	{
	case 20002: Btn = BtnClose; break;
	case 20190: Btn = BtnUpgrade; break;
	case 20191: Btn = BtnCancel; break;
	case 20197: Btn = BtnSpecial; break;
	case 20152: Btn = BtnCardClear.IsValidIndex(0) ? BtnCardClear[0].Get() : nullptr; break;
	case 20155: Btn = BtnCardClear.IsValidIndex(1) ? BtnCardClear[1].Get() : nullptr; break;
	case 20158: Btn = BtnCardClear.IsValidIndex(2) ? BtnCardClear[2].Get() : nullptr; break;
	case 19106: Btn = PopOkBtn; break;
	default: break;
	}
	if (!Btn) { return false; }
	Btn->OnClicked.Broadcast();   // тот же путь, что клик игрока: делегат кнопки → реле → звук → обработчик
	return true;
}

void UAsdaItemUpgradeSubsystem::DebugMarkSent()
{
	bIdle = false;
	bArmed = false;
	ReplyLatch.Arm(6504);   // ★ S2 (В2-A дедупа №2, 11.10.2026)
	UE_LOG(LogAsdaUpgrade, Warning, TEXT("[заточка] ОТЛАДКА: состояние «6504 ушёл» выставлено без сервера"));
}

bool UAsdaItemUpgradeSubsystem::DebugBuildResult(int32 Code, int32 NewLevel, TArray<uint8>& OutBody) const
{
	// Тело 6505 из ТЕКУЩЕГО выбора: записи — копии живых ячеек с поправками, как их правит сервер
	//   (успех: ++ступень, ++попытки; провал: ++попытки; свиток: −1 штука, кончился — код 0).
	OutBody.SetNumZeroed(231);
	OutBody[0] = (uint8)Code;
	auto Put = [&OutBody](int32 Index, const FAsdaItemInstance* It)
	{
		if (!It || It->Raw.Num() < 57) { return; }
		FMemory::Memcpy(OutBody.GetData() + 3 + 57 * Index, It->Raw.GetData(), 57);
	};
	const FAsdaItemInstance* T = LiveOf(Target);
	const FAsdaItemInstance* S = LiveOf(Scroll);
	if (!T || !S) { return false; }
	Put(0, T); Put(1, S); Put(2, LiveOf(Helper97)); Put(3, LiveOf(Helper98));
	uint8* R0 = OutBody.GetData() + 3;
	if (Code == 1) { R0[24] = (uint8)FMath::Clamp(NewLevel, 0, 255); R0[25] = (uint8)FMath::Min(255, (int32)R0[25] + 1); }
	if (Code == 8) { R0[25] = (uint8)FMath::Min(255, (int32)R0[25] + 1); if (NewLevel < 0) { FMemory::Memzero(R0, 4); } }
	uint8* R1 = OutBody.GetData() + 3 + 57;
	const int32 Left = AsdaBytes::RdI32(R1 + 9) - 1;
	if (Left > 0) { AsdaBytes::WrI32(R1 + 9, Left); } else { FMemory::Memzero(R1, 4); }
	AsdaBytes::WrI16(OutBody.GetData() + 1, 0);
	return true;
}

namespace {
	// Нажать контрол окна БОЕВЫМ обработчиком (закон diagnostic-must-use-real-path).
	void PressControl(UAsdaItemUpgradeSubsystem* S, int32 Id)
	{
		if (Id >= 20020 && Id <= 20049) { S->OnGridCellClicked(S->GridListIndexOfVisible(Id - 20020)); return; }   // ★ 29.10.2026: видимая → позиция списка
		if (Id >= 20050 && Id <= 20055) { S->OnScrollCellClicked(Id - 20050); return; }
		if (Id >= 20081 && Id <= 20083) { S->OnHelper97CellClicked(Id - 20081); return; }
		if (Id >= 20090 && Id <= 20092) { S->OnHelper98CellClicked(Id - 20090); return; }
		switch (Id)
		{
		case 20150: S->OnCardIconClicked(0); break;
		case 20153: S->OnCardIconClicked(1); break;
		case 20156: S->OnCardIconClicked(2); break;
		case 20152: S->OnCardClearClicked(0); break;
		case 20155: S->OnCardClearClicked(1); break;
		case 20158: S->OnCardClearClicked(2); break;
		case 20190: S->OnUpgradeClicked(); break;
		case 20191: case 20002: S->OnCancelClicked(); break;
		case 20003: S->OnIconClicked(); break;
		case 20197: S->OnSpecialClicked(); break;
		case 19106: S->OnPopupOkClicked(); break;
		case 449:   S->ToggleWindow(); break;
		default: UE_LOG(LogAsdaUpgrade, Warning, TEXT("asda.Upgrade.Click: контрол %d не обслуживается"), Id); break;
		}
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdUpgradeWindow(
		TEXT("asda.Upgrade.Window"),
		TEXT("asda.Upgrade.Window — тумблер окна 20000 «Улучшение снаряжения» (порт uiCombineWindow_Close)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			if (UAsdaItemUpgradeSubsystem* S = UpgradeFromWorld(W)) { S->ToggleWindow(); }
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdUpgradeControls(
		TEXT("asda.Upgrade.Controls"),
		TEXT("asda.Upgrade.Controls — печать КАЖДОГО разобранного контрола окна 20000 и итог «найдено N/M»."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			if (UAsdaItemUpgradeSubsystem* S = UpgradeFromWorld(W)) { UE_LOG(LogAsdaUpgrade, Log, TEXT("%s"), *S->DumpControls()); }
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdUpgradeState(
		TEXT("asda.Upgrade.State"),
		TEXT("asda.Upgrade.State — списки, выбор, флаги эталона (+3392/+3394/+3404), замок N-2, эффекты."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			if (UAsdaItemUpgradeSubsystem* S = UpgradeFromWorld(W)) { UE_LOG(LogAsdaUpgrade, Log, TEXT("%s"), *S->DumpState()); }
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdUpgradeClick(
		TEXT("asda.Upgrade.Click"),
		TEXT("asda.Upgrade.Click <IDNum> — нажать контрол окна боевым обработчиком (20020…20049, 20050…20055, "
		     "20081…20083, 20090…20092, 20150/53/56, 20152/55/58, 20190, 20191, 20002, 20003, 20197, 19106, 449)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaItemUpgradeSubsystem* S = UpgradeFromWorld(W);
			if (!S || Args.Num() < 1) { return; }
			PressControl(S, FCString::Atoi(*Args[0]));
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdUpgradeSimResult(
		TEXT("asda.Upgrade.SimResult"),
		TEXT("asda.Upgrade.SimResult <код 0…8> [новая ступень | −1 = предмет уничтожен] — синтез тела 6505 из текущего "
		     "выбора и прогон через БОЕВОЙ приёмник (очередь приёма, как у сети)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6505 подделывает записи цели и свитка — следующая заточка 6504 ушла бы по ним.
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Upgrade.SimResult"))) { return; }
			UAsdaItemUpgradeSubsystem* S = UpgradeFromWorld(W);
			if (!S || Args.Num() < 1) { return; }
			const int32 Code = FCString::Atoi(*Args[0]);
			const int32 Lvl = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 1;
			TArray<uint8> Body;
			if (!S->DebugBuildResult(Code, Lvl, Body)) { UE_LOG(LogAsdaUpgrade, Warning, TEXT("SimResult: нет цели/свитка")); return; }
			S->DebugInjectResult(Body);
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdUpgradeFinishFx(
		TEXT("asda.Upgrade.FinishFx"),
		TEXT("asda.Upgrade.FinishFx <20070|20004|20005> — событие 0x18 «анимация доиграла» (проверка цепочки без отрисовки)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaItemUpgradeSubsystem* S = UpgradeFromWorld(W);
			if (!S || Args.Num() < 1) { return; }
			const int32 Id = FCString::Atoi(*Args[0]);
			if (Id == 20070) { S->DebugFinishEffect(EAsdaUpgradeFx::Effect1); }
			else if (Id == 20004) { S->DebugFinishEffect(EAsdaUpgradeFx::Success); }
			else if (Id == 20005) { S->DebugFinishEffect(EAsdaUpgradeFx::FailAni); }
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
// ★ САМОТЕСТ ПУТЁМ ИГРОКА (-game, не PIE). Шаги разнесены таймерами: живая геометрия готова только
//   после отрисовки (закон ui-check-game-path-geometry). Сервера нет — предметы кладутся в МОДЕЛЬ
//   отладочным путём инвентаря (`DebugPutItem`), ответы 6505 синтезируются и идут боевым приёмником.
//   Запуск: UnrealEditor-Cmd.exe <uproject> "/Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"
//           -game -RenderOffscreen -ExecCmds="asda.Upgrade.SelfTest shot quit"
// ★ Волна 12: КАДРЫ ПАНЕЛИ «+N%» ДЛЯ ВЛАДЕЛЬЦА (путь игрока, -game). Ассет окна 20000 НЕ меняется — только
//   видимость узлов на время кадра (`DebugPanelShotMode`), потом всё возвращается. Кадры:
//   asda_panel_a_off (как сейчас, ключ 0) · asda_panel_b_on (ключ 1, как в разметке — под «Особым») ·
//   asda_panel_c_nospecial (ключ 1, «Особый» 20196/20197 спрятан) · asda_panel_d_full (плюс 20194 и рамка строки).
//   Запуск: -ExecCmds="asda.Upgrade.PanelShots quit" (отдельной .bat-обёртки нет — строка -ExecCmds
//   та же, что у `Tools/run_game_upgrade.bat`, с этой командой вместо самотеста).
//   ★ Волна 13 (М-2 ревизии волны 12): побочных эффектов после команды нет — ячейка сумки 25 (тестовый
//   предмет 22818) возвращается к прежней записи, ключ `asda.Upgrade.EnhancePanel` — к прежнему значению,
//   окно 20000 закрывается, если до команды было закрыто.
//   ★ Волна 14 (МЕЛК-4 ревизии волны 13): команда ТОЛЬКО ДЛЯ ОФЛАЙНА — при живом соединении с сервером она
//   отказывается (иначе её возврат затёр бы пакет сервера для ячейки 25). Запись ячейки 25 и кладётся, и
//   возвращается ШТАТНЫМ путём `ApplyExternalRecord` (→ `PutRecord` → `NotifyCell`): окна узнают о прочности и
//   опциях, а не только о коде и количестве. Если за время команды ячейку сменил кто-то другой, возврат её не трогает.
	FAutoConsoleCommandWithWorldAndArgs GCmdUpgradePanelShots(
		TEXT("asda.Upgrade.PanelShots"),
		TEXT("Окно 20000: выбрать предмет с процентом 40 (< 88) и снять 4 кадра панели «+N%» (выкл / вкл / вкл без «Особого» / "
		     "вкл + 20194 и рамка). Ассет не меняется. `quit` — выход."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaItemUpgradeSubsystem* S = UpgradeFromWorld(W);
			UAsdaInventorySubsystem* Inv = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			if (!S || !Inv || !W) { return; }
			// ★ Волна 14 (МЕЛК-4): офлайн-команда. При живом соединении ячейкой 25 владеет сервер — не трогаем.
			//   ★ С-Р1 (26.09.2026): проверка — общая точка порта `FAsdaOutgoingGate::RefuseDebugWhenOnline`.
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Upgrade.PanelShots"))) { return; }
			const bool bQuit = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
			static constexpr int32 kPct = 40;   // процент цели (+27): ниже порога 88 → «+48»
			// ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте —
			//   общий флаг шагов `At` (проверку делает первый сработавший шаг, при отказе пропускаются все).
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Upgrade.PanelShots"), bQuit);
			auto At = [W, S, Gate](float T, TFunction<void()> Fn)
			{
				FTimerHandle H;
				W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(S,
					[Gate, S, Fn = MoveTemp(Fn)]() { if (!Gate->Pass(S)) { return; } Fn(); }), T, false);
			};
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ панели — через `AsdaCVarSave` (исходные значение и SetBy запоминаются при первом касании).
			auto Key = [](int32 V) { AsdaCVarSave::SetForTest(TEXT("asda.Upgrade.EnhancePanel"), V); };
			auto Shot = [](const TCHAR* Name)
			{
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("ПАНЕЛЬ: КАДР → %s.png"), Name);
				FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			};
			// ★ Волна 13 (М-2): что команда трогает — запомнить до первого шага и вернуть в конце.
			// `Put` — запись, которую положила сама команда (★ волна 14: по ней возврат узнаёт, не сменили ли ячейку).
			struct FSaved { FAsdaItemInstance Cell25; TArray<uint8> Put; int32 PanelKey = 0; bool bWasOpen = false; };
			TSharedRef<FSaved> Saved = MakeShared<FSaved>();
			if (const FAsdaItemInstance* C25 = Inv->GetCell(1, 25)) { Saved->Cell25 = *C25; }
			if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Upgrade.EnhancePanel"))) { Saved->PanelKey = CV->GetInt(); }
			Saved->bWasOpen = S->IsOpen();
			At(2.0f, [S, Inv, Saved]()
			{
				// Запись 57 б: поля кода/контейнера/слота/количества — как у `DebugPutItem`, затем ступень и процент.
				//   ★ Волна 14 (МЕЛК-4): кладётся штатным путём (`ApplyExternalRecord` → `PutRecord` → `NotifyCell`).
				Inv->DebugPutItem(1, 25, 22818, 1);
				if (const FAsdaItemInstance* It = Inv->GetCell(1, 25))
				{
					TArray<uint8> R = It->Raw;
					if (R.Num() >= 57)
					{
						R[24] = 5; R[25] = 4; R[26] = 5;   // ступень 5, попыток 4/5
						AsdaBytes::WrI16(R.GetData() + 27, kPct);
						Inv->ApplyExternalRecord(R);
					}
				}
				if (const FAsdaItemInstance* It = Inv->GetCell(1, 25)) { Saved->Put = It->Raw; }
				if (!S->IsOpen()) { S->ToggleWindow(); }
				S->OnGridCellClicked(0);
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("ПАНЕЛЬ: выбран предмет\n%s"), *S->DumpState());
			});
			const struct { float T; int32 Key; int32 Mode; const TCHAR* Name; } Steps[] = {
				{ 3.0f, 0, 0, TEXT("asda_panel_a_off") },
				{ 4.5f, 1, 1, TEXT("asda_panel_b_on") },
				{ 6.0f, 1, 2, TEXT("asda_panel_c_nospecial") },
				{ 7.5f, 1, 3, TEXT("asda_panel_d_full") },
			};
			for (const auto& St : Steps)
			{
				const int32 K = St.Key, M = St.Mode;
				const TCHAR* N = St.Name;
				At(St.T, [S, Key, K, M]() { Key(K); S->DebugPanelShotMode(M, kPct); });
				// Второй вызов после отрисовки — живая геометрия строки (для кропа), затем кадр.
				At(St.T + 0.5f, [S, M]() { S->DebugPanelShotMode(M, kPct); });
				At(St.T + 0.7f, [Shot, N]() { Shot(N); });
			}
			At(9.0f, [S, Inv, Key, Saved]()
			{
				// Вернуть как было: ключ панели — прежнее значение, видимость — как ставит боевой код.
				// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ EnhancePanel — к исходному значению И приоритету SetBy (прежде — PanelKey с SetByConsole).
				AsdaCVarSave::RestoreAll(TEXT("asda.Upgrade.PanelShots"));
				S->DebugPanelShotMode(-1, kPct);
				// Ячейка сумки 25: прежняя запись целиком (пустая — пустой).
				//   ★ Волна 14 (МЕЛК-4): штатным путём — `ApplyExternalRecord` (→ `PutRecord` → `NotifyCell`), окна видят
				//   и прочность, и опции. Ячейку сменил кто-то другой за время команды — её не трогаем.
				const FAsdaItemInstance& C = Saved->Cell25;
				const FAsdaItemInstance* Now = Inv->GetCell(1, 25);
				if (!Now || Now->Raw != Saved->Put)
				{
					UE_LOG(LogAsdaUpgrade, Warning, TEXT("ПАНЕЛЬ: ячейку 25 за время команды сменили (сейчас %d) — возврат её не трогает"),
						Now ? Now->ItemCode : -1);
				}
				else if (C.IsEmpty() || C.Raw.Num() < 57) { Inv->DebugPutItem(1, 25, 0, 0); }
				else
				{
					// Поля маршрута записи (+4 контейнер, +5 слот) — это адрес самой ячейки (1, 25); по ним штатный путь её и кладёт.
					TArray<uint8> R = C.Raw;
					R[4] = 1;
					AsdaBytes::WrI16(R.GetData() + 5, 25);
					Inv->ApplyExternalRecord(R);
				}
				if (!Saved->bWasOpen && S->IsOpen()) { S->ToggleWindow(); }
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("ПАНЕЛЬ: возвращено — ячейка 25 = %d, ключ EnhancePanel = %d, окно %s"),
					C.IsEmpty() ? 0 : C.ItemCode, Saved->PanelKey, S->IsOpen() ? TEXT("открыто") : TEXT("закрыто"));
			});
			if (bQuit)
			{
				At(9.6f, []()
				{
					UE_LOG(LogAsdaUpgrade, Warning, TEXT("ПАНЕЛЬ: завершено"));
					if (GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
				});
			}
		}));
}
#endif // ASDA_DEV_CONSOLE
