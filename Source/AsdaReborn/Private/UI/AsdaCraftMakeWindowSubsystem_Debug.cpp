// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCraftMakeWindowSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaCraftMakeWindowSubsystem.cpp, дословный перенос): дампы, отладочные помощники, самотест asda.Craft.MakeSelfTest.
//   Общее для частей — в AsdaCraftMakeWindowSubsystem_Private.h; шапка и реверс — в AsdaCraftMakeWindowSubsystem.cpp.
#include "UI/AsdaCraftMakeWindowSubsystem.h"

#include "Components/CanvasPanelSlot.h"   // ZOrder ступени (флаг +0x24F, дамп/самотест)
#include "AsdaCraftMakeWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCraftMakeWindowSubsystem*.cpp
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): шаги по событию «ввод доставлен», а не по часам

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════
FString UAsdaCraftMakeWindowSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно изготовления 77500: разобранные контролы ===\n");
	auto Line = [&S](const FString& Id, bool bOk) { S += FString::Printf(TEXT("  %-58s %s\n"), *Id, bOk ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("77500 ITEM_making_Mark_WIN (корень)"), Widget != nullptr);
	Line(TEXT("77535 ITEM_making_movetext + ручка _DRAG"), TxtMove != nullptr && DragHandle != nullptr);
	Line(TEXT("77501 FMark_1 (план + подпись; эталоном не используется)"), FMark.IsValidIndex(0) && FMark[0] && FMarkText.IsValidIndex(0) && FMarkText[0]);
	Line(TEXT("77502 FMark_2 (план + подпись; только индекс −1 К-3)"), FMark.IsValidIndex(1) && FMark[1] && FMarkText.IsValidIndex(1) && FMarkText[1]);
	Line(FString::Printf(TEXT("77531 Mark_Mbar (ES0/ES1, ширина %.0f)"), Bar.FullW), Bar.Fill != nullptr && Bar.Back != nullptr);
	for (int32 T = 0; T < Tiers.Num(); ++T)
	{
		const FAsdaCraftTierUi& U = Tiers[T];
		Line(FString::Printf(TEXT("%d z_mark_%d (группа + план иконки + подпись)"), 77511 + T, T + 1), U.Group && U.Icon && U.IconText);
		Line(FString::Printf(TEXT("%d z_mark_%d_b (подложка «не сыграно»)"), 77551 + T, T + 1), U.Back != nullptr);
		Line(FString::Printf(TEXT("%d z_mark_%dx (крестик NEW_st_X)"), 77541 + T, T + 1), U.Fail != nullptr);
		Line(FString::Printf(TEXT("%d z_mark_%da (Ani_Soul2)"), 77521 + T, T + 1), U.Ani != nullptr);
	}
	Line(TEXT("77532 M_text (статус)"), TxtStatus != nullptr);
	Line(TEXT("77533 BOX_Ingbtn «Создать» + подпись"), BtnCreate != nullptr && TxtCreate != nullptr);
	Line(TEXT("77534 Mark_Xbtn «Закрыть» + подпись"), BtnClose != nullptr && TxtClose != nullptr);
	Line(TEXT("77536 BOX_text_ea «Кол-во»"), TxtQtyLabel != nullptr);
	Line(TEXT("77537 BOX_edit_ea (поле количества)"), EditQty != nullptr);
	Line(TEXT("77538 BOX_Ar_h (+1)"), BtnUp != nullptr);
	Line(TEXT("77539 BOX_Ar_d (−1)"), BtnDown != nullptr);
	Line(TEXT("77548 BOX_text_max (не привязан эталоном, скрыт)"), TxtMax != nullptr);
	S += FString::Printf(TEXT("  ИТОГ: найдено %d/%d контролов с IDNum группы 775\n"), FoundControls, kControlsTotal);
	return S;
}

FString UAsdaCraftMakeWindowSubsystem::DumpState() const
{
	FString S = FString::Printf(TEXT("[ремесло-изготовление] открыто=%d рецепт=%d ступеней=%d · осталось=%d · анимация=%d ступень=%d отыграна=%d отмена=%d · "
		"ждём 6603=%d · код=%d достигнуто=%d +1шт=%d продукт=%d · полоса=%.2f\n"),
		bOpen ? 1 : 0, RecipeId, TierCount, Remaining, bAnimating ? 1 : 0, CurTier, bTierPlayed ? 1 : 0, bCancelAfter ? 1 : 0,
		bAwaitingReply ? 1 : 0, ResultCode, Reached, BonusFlag, Product57.Num() >= 4 ? AsdaBytes::RdI32(Product57.GetData()) : 0, BarFrac);
	S += TEXT("  слоты (иконка/подложка/ани/крестик/код):");
	for (int32 T = 0; T < kTiers; ++T)
	{
		S += FString::Printf(TEXT(" [%d: %d%d%d%d %d]"), T, IsShown(ControlByDword(T + kDwMake)) ? 1 : 0, IsShown(ControlByDword(T + kDwBack)) ? 1 : 0,
			IsShown(ControlByDword(T + kDwAni)) ? 1 : 0, IsShown(ControlByDword(T + kDwFail)) ? 1 : 0, IconCodes.IsValidIndex(T) ? IconCodes[T] : -1);
	}
	S += TEXT("\n  флаги +0x24F (dword):");
	for (const TPair<int32, bool>& F : TierFlags) { S += FString::Printf(TEXT(" %d(z=%d)"), F.Key, DebugZOrder(F.Key)); }
	S += TEXT("\n");
	return S;
}

FString UAsdaCraftMakeWindowSubsystem::DebugText(int32 IdNum) const
{
	auto T = [](const UTextBlock* B) { return B ? B->GetText().ToString() : FString(TEXT("<нет>")); };
	switch (IdNum)
	{
	case 77532: return T(TxtStatus);
	case 77533: return T(TxtCreate);
	case 77534: return T(TxtClose);
	case 77536: return T(TxtQtyLabel);
	case 77537: return EditQty ? EditQty->GetText().ToString() : FString(TEXT("<нет>"));
	default: break;
	}
	return FString();
}

bool UAsdaCraftMakeWindowSubsystem::DebugShown(int32 IdNum) const
{
	if (IdNum == 77534) { return IsShown(BtnClose); }
	if (IdNum >= 77511 && IdNum <= 77517) { return IsShown(ControlByDword(kDwMake + IdNum - 77511)); }
	if (IdNum >= 77521 && IdNum <= 77527) { return IsShown(ControlByDword(kDwAni + IdNum - 77521)); }
	if (IdNum >= 77541 && IdNum <= 77547) { return IsShown(ControlByDword(kDwFail + IdNum - 77541)); }
	if (IdNum >= 77551 && IdNum <= 77557) { return IsShown(ControlByDword(kDwBack + IdNum - 77551)); }
	return false;
}

int32 UAsdaCraftMakeWindowSubsystem::DebugIconCode(int32 Slot) const
{
	return IconCodes.IsValidIndex(Slot) ? IconCodes[Slot] : -1;
}

FLinearColor UAsdaCraftMakeWindowSubsystem::DebugStatusColor() const { return FLinearColor(StatusColor); }
FLinearColor UAsdaCraftMakeWindowSubsystem::DebugEditColor() const { return FLinearColor(EditColor); }

bool UAsdaCraftMakeWindowSubsystem::DebugControlsEnabled() const
{
	return BtnCreate && BtnCreate->GetIsEnabled() && BtnUp && BtnUp->GetIsEnabled() && BtnDown && BtnDown->GetIsEnabled()
		&& EditQty && EditQty->GetIsEnabled();
}

bool UAsdaCraftMakeWindowSubsystem::DebugDrawnAboveNeighbors(int32 DwordIdx, FString& OutWhy) const
{
	// Путь игрока (закон `ui-check-game-path-geometry`): порядок, в котором Slate-канвас окна РЕАЛЬНО раскладывает и рисует
	//   детей (`SConstraintCanvas` сортирует их по ZOrder), — ступень должна идти ПОСЛЕ всех видимых соседних ступеней.
	const UWidget* W = ControlByDword(DwordIdx);
	const UPanelWidget* Parent = W ? W->GetParent() : nullptr;
	const TSharedPtr<SWidget> PW = Parent ? Parent->GetCachedWidget() : nullptr;
	const TSharedPtr<SWidget> Me = W ? W->GetCachedWidget() : nullptr;
	if (!PW.IsValid() || !Me.IsValid()) { OutWhy = TEXT("нет Slate-виджета (окно не построено)"); return false; }
	FArrangedChildren AC(EVisibility::All);
	PW->ArrangeChildren(Parent->GetCachedGeometry(), AC);
	int32 MyIdx = INDEX_NONE, MaxNeighbor = INDEX_NONE, Neighbors = 0;
	for (int32 I = 0; I < AC.Num(); ++I)
	{
		const TSharedRef<SWidget>& C = AC[I].Widget;
		if (C == Me.ToSharedRef()) { MyIdx = I; continue; }
		for (const FAsdaCraftTierUi& T : Tiers)
		{
			if (!T.Group || T.Group == W || !AsdaUi::IsShown(T.Group)) { continue; }
			const TSharedPtr<SWidget> G = T.Group->GetCachedWidget();
			if (G.IsValid() && G == C) { MaxNeighbor = FMath::Max(MaxNeighbor, I); ++Neighbors; }
		}
	}
	OutWhy = FString::Printf(TEXT("порядок рисования канваса: ступень №%d, старший видимый сосед №%d (соседей %d, детей %d), z=%d"),
		MyIdx, MaxNeighbor, Neighbors, AC.Num(), DebugZOrder(DwordIdx));
	return MyIdx != INDEX_NONE && MyIdx > MaxNeighbor;
}

int32 UAsdaCraftMakeWindowSubsystem::DebugZOrder(int32 DwordIdx) const
{
	const UWidget* W = ControlByDword(DwordIdx);
	const UCanvasPanelSlot* CS = W ? Cast<UCanvasPanelSlot>(W->Slot) : nullptr;
	return CS ? CS->GetZOrder() : -1;
}

bool UAsdaCraftMakeWindowSubsystem::DebugTierFlag(int32 DwordIdx) const
{
	return TierFlags.Contains(DwordIdx);
}

bool UAsdaCraftMakeWindowSubsystem::DebugPressButton(int32 IdNum)
{
	UButton* Btn = nullptr;
	switch (IdNum)
	{
	case 77533: Btn = BtnCreate; break;
	case 77534: Btn = BtnClose; break;
	case 77538: Btn = BtnUp; break;
	case 77539: Btn = BtnDown; break;
	default: break;
	}
	if (!Btn || !Btn->GetIsEnabled()) { return false; }   // выключенная кнопка игроку не нажимается
	Btn->OnClicked.Broadcast();   // тот же путь, что клик игрока: делегат кнопки → реле → обработчик
	return true;
}

void UAsdaCraftMakeWindowSubsystem::DebugTypeQty(const FString& Text)
{
	// Путь Slate: текст поля меняется → `OnTextChanged` (у нас — прямой вызов того же обработчика).
	if (!EditQty) { return; }
	{
		TGuardValue<bool> Guard(bSettingQtyText, true);
		EditQty->SetText(FText::FromString(Text));
	}
	OnQtyTextChanged(FText::FromString(Text));
}

void UAsdaCraftMakeWindowSubsystem::DebugOverrideRateOut(const TArray<int32>& OutItems)
{
	DebugRateOut = OutItems;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftMakeState(
		TEXT("asda.Craft.MakeState"),
		TEXT("asda.Craft.MakeState — контролы и состояние окна изготовления 77500."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaCraftMakeWindowSubsystem* Mk = GI ? GI->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr;
			if (Mk) { UE_LOG(LogAsdaCraftMake, Log, TEXT("%s%s"), *Mk->DumpControls(), *Mk->DumpState()); }
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
// ★ САМОТЕСТ ПУТЁМ ИГРОКА (-game), волна В4: окно 77500. Сервера нет: 6597/6603/6599/6605 синтезируются и идут БОЕВЫМИ
//   приёмниками модели; исходящие 6602/6598/6604 ловит узкий перехват (свой опкод, снимается сам); кнопки — делегатом
//   `OnClicked` (путь игрока), вход — кнопкой 77068 окна состава. `delay=N` `shot` `prefix=` (умолчание `w4_`) `quit`.
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftMakeSelfTest(
		TEXT("asda.Craft.SelfTestMake"),
		TEXT("Окно 77500: разбор 41 контрола, вход из 77000, поле количества, все ступени, провал на 1-й (К-3, Р-4), «+1 шт.», "
		     "серия с ожиданием ответа, прерывание, К-1, отказы, таймаут, К-6, свитки 101/102, снимок окон. `delay=N` `shot` `prefix=` `quit`."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaCraftMakeWindowSubsystem* Mk = GI ? GI->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr;
			UAsdaCraftRecipeWindowSubsystem* Rw = GI ? GI->GetSubsystem<UAsdaCraftRecipeWindowSubsystem>() : nullptr;
			UAsdaCraftWindowSubsystem* Win = GI ? GI->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr;
			UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
			UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			if (!Mk || !Rw || !Win || !Craft || !Inv || !WM) { return; }
			bool bQuit = false, bShot = false;
			float Delay = 0.f;
			FString Prefix(TEXT("w4_"));
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
				if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
				if (A.StartsWith(TEXT("prefix="))) { Prefix = A.Mid(7); }
			}
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix-b 13.11.2026 (третий заход): `delay=N` — общая точка (была ветка цикла аргументов)
			// ★ Волна В8 (ревизия В7 С-3): только офлайн — при живом соединении отказ, иначе окно защиты отправителей.
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Craft.SelfTestMake"), KitArgs)) { return; }
			struct FStat { int32 Ok = 0; int32 Fail = 0; };
			TSharedRef<FStat> Stat = MakeShared<FStat>();
			TWeakObjectPtr<UWorld> WGate(W);   // мир прогона (для отказа на отложенном старте)
			// ★ Р-2 (10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим таймером;
			//   при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
			// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Craft.SelfTestMake"), bQuit);   // общий флаг отказа всех шагов прогона
			auto GateRefused = [Gate, WGate]() { return !Gate->Pass(WGate.Get()); };   // true — прогон отменён; проверка один раз, первым сработавшим шагом
			// ★ R5-fix-b 13.11.2026 (заключительный заход) (ревизор третьего захода, зам. 7.2): шаги — общей очередью `AsdaSelfTestSeq` (по порядку расписания,
			//   промежутки те же), а не мировым таймером от старта: ворота шага 9 (`When`) сдвигают и всё, что после них, — шаг К-1
			//   не начинается, пока не дождался предыдущий (прежде при позднем событии ответ 6603 штуки 2 попадал в К-1).
			TSharedRef<TSharedPtr<AsdaSelfTestSeq::FSeq>> SeqBox = MakeShared<TSharedPtr<AsdaSelfTestSeq::FSeq>>();   // очередь прогона
			TWeakObjectPtr<UObject> WMk(Mk);   // владелец шагов (как `CreateWeakLambda(Mk, …)` прежде)
			auto At = [W, WMk, Delay, GateRefused, SeqBox](float T, TFunction<void()> Fn)
			{
				if (AsdaSelfTestSeq::FSeq* Q = AsdaSelfTestSeq::Queue(*SeqBox, W))
				{
					Q->Add(T + Delay, [WMk, GateRefused, Fn = MoveTemp(Fn)]()
					{
						if (!WMk.IsValid() || GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
						Fn();
					});
				}
			};
			// Шаг-ворота: держать очередь, пока Pred() ложно (не дольше Cap с), затем Fn — проверки покажут, чего не случилось.
			auto When = [W, WMk, Delay, GateRefused, SeqBox](float T, TFunction<bool()> Pred, float Cap, TFunction<void()> Fn)
			{
				if (AsdaSelfTestSeq::FSeq* Q = AsdaSelfTestSeq::Queue(*SeqBox, W))
				{
					Q->Add(T + Delay, [WMk, GateRefused, Fn = MoveTemp(Fn)]()
					{
						if (!WMk.IsValid() || GateRefused()) { return; }
						Fn();
					}, [WMk, Pred = MoveTemp(Pred)]() { return !WMk.IsValid() || Pred(); }, Cap);
				}
			};
			auto Shot = [bShot, Prefix](const TCHAR* Name)
			{
				if (!bShot) { return; }
				const FString N = Prefix + Name;
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("САМОТЕСТ-77500: КАДР → %s.png"), *N);
				FScreenshotRequest::RequestScreenshot(N, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			};
			auto Check = [Stat](bool bOk, const FString& What)
			{
				if (bOk) { ++Stat->Ok; } else { ++Stat->Fail; }
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("САМОТЕСТ-77500 %s %s"), bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
			};
			auto Dump = [Mk, Craft](const TCHAR* Step)
			{
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("САМОТЕСТ-77500: %s\n%s"), Step, *Mk->DumpState());
				for (const FString& L : Mk->GetJournal()) { UE_LOG(LogAsdaCraftMake, Warning, TEXT("  журнал 77500: %s"), *L); }
				for (const FString& L : Craft->GetNoticeJournal()) { UE_LOG(LogAsdaCraftMake, Warning, TEXT("  журнал нотисов: %s"), *L); }
				Mk->ClearJournal(); Craft->ClearNoticeJournal();
			};
			auto JHas = [Mk](const TCHAR* What) { for (const FString& L : Mk->GetJournal()) { if (L.Contains(What)) { return true; } } return false; };
			auto NHas = [Craft](const TCHAR* What) { for (const FString& L : Craft->GetNoticeJournal()) { if (L.Contains(What)) { return true; } } return false; };
			auto SetCVar = [](const TCHAR* Name, float V) { AsdaCVarSave::SetForTest(Name, V); };   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходное запоминается
			auto LocS = [GI](int32 Id) { return AsdaNotice::Loc(GI, Id); };
			auto NameOf = [GI](int32 Code) { const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>(); return IT ? IT->GetItemName(Code) : FString(); };
			auto Fmt = [LocS, NameOf](int32 Id, int32 Code) { return LocS(Id).Replace(TEXT("%s"), *NameOf(Code)); };
			auto IsBlue = [](const FLinearColor& C) { return C.Equals(FLinearColor(FColor(0x09, 0x8A, 0xFF, 0xFF)), 0.01f); };
			auto IsRed  = [](const FLinearColor& C) { return C.Equals(FLinearColor(FColor(0xFF, 0x00, 0x00, 0xFF)), 0.01f); };
			auto IsWhite= [](const FLinearColor& C) { return C.Equals(FLinearColor::White, 0.01f); };
			// Перехват 6602/6598/6604 — БОЕВОЙ отправитель до `SendSecured`, тело в `DebugCaptured` (снимается сам).
			auto Arm = [Craft](int32 Op) { Craft->DebugCaptured.Reset(); return Craft->DebugCaptureOnce(Op); };
			auto Captured = [Craft](int32 Op) -> TArray<uint8>
			{
				for (const TPair<int32, TArray<uint8>>& P : Craft->DebugCaptured) { if (P.Key == Op) { return P.Value; } }
				return TArray<uint8>();
			};
			auto Put = [Inv](int32 Cell, int32 Code, int32 Qty) { Inv->DebugPutItem(2, Cell, Code, Qty); };
			auto ClearBag = [Inv]() { for (int32 C = 0; C < 60; ++C) { Inv->DebugPutItem(2, C, 0, 0); } };
			// Данные (`craft_recipes_decoded.csv`, `making_rate.csv`): 122 — 5 ступеней (72174/72197/72220/72243/72266),
			//   72635×3 + 72732×1 + 20959×1, ур. 6; 1 «абразив» — 1 ступень 20962 с «+1 шт.» (rate +2 = 0), 20931×3, ур. 1.
			static const TArray<int32> kLearned = { 1, 76, 122 };
			auto PutMats122 = [Put]() { Put(40, 72635, 3); Put(41, 72732, 1); Put(42, 20959, 1); };
			auto Mats122Used = []() { return TArray<FMat>{ { 72635, 40, 0 }, { 72732, 41, 0 }, { 20959, 42, 0 } }; };
			auto Reply = [Craft](const TArray<uint8>& B) { Craft->DebugInject(6603, B); };
			// ── Шаг 0: ревизия В3 С-4 — сводка 76000 после штуки (`uiCraftPanel_RefreshSummary`) затемняет выбранную строку ──
			At(0.1f, [Craft, Win, Inv, WM, Check, SetCVar, ClearBag, PutMats122]()
			{
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 30, 5, 100, 10));   // ур. 5 < 6 у рецепта 122
				ClearBag(); PutMats122();
			});
			At(0.2f, [Craft, Win, Check, SetCVar]()
			{
				if (!Win->IsOpen()) { Win->GateAndToggle(TEXT("самотест С-4")); }
				int32 Row = -1;
				for (int32 R = 0; R < UAsdaCraftWindowSubsystem::kRows; ++R) { if (Win->GetRowRecipe(R) == 122) { Row = R; } }
				Check(Row >= 0, FString::Printf(TEXT("С-4: рецепт 122 на странице (строка %d)"), Row));
				if (Row < 0) { return; }
				Win->OnRowClicked(Row);
				Win->RefreshSummary();
				Check(Win->DebugRowBlack(Row), TEXT("С-4 (1): материалы есть, уровень 5 < 6 — выбранная строка затемнена (как обычная перерисовка)"));
				SetCVar(TEXT("asda.Craft.FixSummaryRowDarken"), 0);
				Win->RefreshSummary();
				Check(!Win->DebugRowBlack(Row), TEXT("С-4 (0): как оригинал — «хватает ИЛИ уровень выше ⇒ спрятать»: строка НЕ затемнена"));
				SetCVar(TEXT("asda.Craft.FixSummaryRowDarken"), 1);
				Win->HideAndCleanup();
			});
			// ── Шаг 1: модель и сумка; сумка открыта (её снимет снимок окна состава и вернёт сброс 77500) ──
			At(0.5f, [Craft, Inv, WM, ClearBag, PutMats122, Mk, Rw]()
			{
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				Mk->ResetDialog(TEXT("самотест: старт")); Mk->DebugSetDryOpen(false); Mk->ClearJournal();
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 30, 6, 100, 10));
				ClearBag();
				PutMats122();
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
			});
			// ── Шаг 2: 76000 → 77000 (рецепт 122) → 77068 «Создание предмета» → окно 77500 ──
			At(1.0f, [Win, Rw, Inv, Check]()
			{
				Check(Inv->IsWindowOpen(), TEXT("сумка открыта до окна состава"));
				Win->GateAndToggle(TEXT("самотест 77500"));
				Check(Rw->OpenForRecipe(122, TEXT("самотест 77500")), TEXT("окно состава 77000 открыто рецептом 122"));
			});
			At(1.3f, [Rw, Mk, Win, Inv, WM, Check, W]()
			{
				Rw->DebugPressButton(77068);
				Check(Mk->IsOpen() && Mk->GetRecipeId() == 122, FString::Printf(TEXT("77068 → окно 77500 открыто рецептом %d"), Mk->GetRecipeId()));
				Check(!Win->IsOpen() && !Rw->IsOpen(), TEXT("uiItemUpgradeWindowOpen: 76000 и 77000 закрыты"));
				Check(!Inv->IsWindowOpen(), TEXT("uiCloseAllWindows: сумка закрыта"));
				Check(WM->IsWindowSlotCaptured(0), TEXT("снимок окна состава (слот 0 — сумка) дожил до 77500 (защёлка К-7)"));
				Check(WM->ArePlayerWindowsHidden(), TEXT("HUD свёрнут («закрыть всё»: хотбар заблокирован)"));
				Check(UAsdaCraftSubsystem::IsAnyCraftWindowOpen(W->GetGameInstance()), TEXT("предикат «ремесло открыто» видит 77500"));
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("%s"), *Mk->DumpControls());
				Check(Mk->GetFoundControls() == UAsdaCraftMakeWindowSubsystem::kControlsTotal,
					FString::Printf(TEXT("разбор контролов 77500: %d/%d"), Mk->GetFoundControls(), UAsdaCraftMakeWindowSubsystem::kControlsTotal));
				GEngine->Exec(W, TEXT("asda.UI.TreeGeom w4_make_open WBP_ITEM_making_Mark_WIN"));
			});
			At(1.5f, [Shot]() { Shot(TEXT("make_open")); });
			At(1.7f, [Mk, LocS, Check, Dump]()
			{
				Check(Mk->DebugText(77533) == LocS(5533) && Mk->DebugText(77534) == LocS(5035) && Mk->DebugText(77536) == LocS(5520),
					FString::Printf(TEXT("подписи 77533 «%s» / 77534 «%s» / 77536 «%s»"), *Mk->DebugText(77533), *Mk->DebugText(77534), *Mk->DebugText(77536)));
				Check(Mk->DebugText(77537) == TEXT("1") && Mk->DebugText(77532).IsEmpty(), TEXT("поле «1», статус пуст (образец ассета стёрт)"));
				Check(Mk->GetTierCount() == 5, FString::Printf(TEXT("ступеней %d (rate 122: 5 исходов)"), Mk->GetTierCount()));
				// Центровка: 5 ступеней ⇒ база 1 ⇒ слоты 1..5.
				Check(!Mk->DebugShown(77511) && Mk->DebugShown(77512) && Mk->DebugShown(77516) && !Mk->DebugShown(77517),
					TEXT("5 ступеней по центровке g_UpgradeSlotBaseIndexTable: слоты 1..5 видны, 0 и 6 скрыты"));
				Check(Mk->DebugIconCode(1) == 72174 && Mk->DebugIconCode(5) == 72266, FString::Printf(TEXT("иконки ступеней: слот 1 = %d, слот 5 = %d"), Mk->DebugIconCode(1), Mk->DebugIconCode(5)));
				Check(Mk->DebugShown(77552) && Mk->DebugShown(77556) && !Mk->DebugShown(77522) && !Mk->DebugShown(77542),
					TEXT("подложки ступеней показаны, ани/крестики скрыты"));
				Check(Mk->DebugControlsEnabled() && Mk->DebugShown(77534), TEXT("4 контрола включены, «Закрыть» видна"));
				Dump(TEXT("после открытия"));
			});
			// ── Шаг 3: поле количества (стрелки, кламп 1..99, 2 символа, atoi, цвет по материалам) ──
			At(2.0f, [Mk, Check, IsRed, IsWhite]()
			{
				Mk->DebugPressButton(77538); Mk->DebugPressButton(77538); Mk->DebugPressButton(77538);
				Check(Mk->DebugText(77537) == TEXT("4") && Mk->GetRemaining() == 4, FString::Printf(TEXT("77538 ×3 → «%s»"), *Mk->DebugText(77537)));
				Check(IsRed(Mk->DebugEditColor()), TEXT("материалов на 4 штуки нет — поле красное (set_o329 −65536)"));
				for (int32 I = 0; I < 5; ++I) { Mk->DebugPressButton(77539); }
				Check(Mk->DebugText(77537) == TEXT("1") && Mk->GetRemaining() == 1 && IsWhite(Mk->DebugEditColor()), TEXT("77539 ×5 → «1» (кламп 1), поле белое"));
				Mk->DebugTypeQty(TEXT("150"));
				Check(Mk->DebugText(77537) == TEXT("15") && Mk->GetRemaining() == 15, FString::Printf(TEXT("ввод «150» → лимит 2 символа «%s»"), *Mk->DebugText(77537)));
				Mk->DebugTypeQty(TEXT("ab"));
				Check(Mk->DebugText(77537) == TEXT("0") && Mk->GetRemaining() == 0, TEXT("ввод «ab» → atoi 0 → «0»"));
				for (int32 I = 0; I < 120; ++I) { Mk->DebugPressButton(77538); }
				Check(Mk->DebugText(77537) == TEXT("99"), TEXT("77538 до упора → «99» (кламп 99)"));
				Mk->DebugTypeQty(TEXT("5"));
			});
			// ── Шаг 4: «Создать» при 5 → урезано до максимума 1, ОДИН 6602 (тело по эталону), кнопки выключены ──
			At(2.3f, [Mk, Craft, Arm, Captured, Check]()
			{
				Check(Arm(6602), TEXT("перехват 6602 поставлен"));
				Mk->DebugPressButton(77533);
				Check(Mk->DebugText(77537) == TEXT("1") && Mk->GetRemaining() == 1, TEXT("«Создать» при 5: урезано до максимума по материалам 1 (craftCalcMaxMakeableCount)"));
				const TArray<uint8> B = Captured(6602);
				Check(B.Num() == 26, FString::Printf(TEXT("6602 перехвачен: тело %d б"), B.Num()));
				if (B.Num() == 26)
				{
					Check(AsdaBytes::RdI16(&B[0]) == 122, TEXT("6602 +0 рецепт 122"));
					Check(AsdaBytes::RdI16(&B[2]) == 40 && AsdaBytes::RdI16(&B[4]) == 41 && AsdaBytes::RdI16(&B[6]) == 42 && AsdaBytes::RdI16(&B[8]) == -1,
						TEXT("6602 +2 слоты материалов 40/41/42/−1…"));
					Check(B[14] == 2 && B[15] == 2 && B[16] == 2 && B[17] == 0, TEXT("6602 +14 контейнеры 2/2/2/0…"));
					Check(AsdaBytes::RdI16(&B[20]) == -1 && AsdaBytes::RdI16(&B[22]) == -1 && B[24] == 0 && B[25] == 0, TEXT("6602 +20 катализаторы −1/−1, 0/0 (клиент их не заполняет)"));
				}
				Check(!Mk->DebugControlsEnabled() && Mk->IsAwaitingReply() && Craft->IsRequestInFlight(), TEXT("кнопки выключены, ждём 6603, замок «в полёте» взведён"));
			});
			// ── Шаг 5: ПРОВАЛ НА ПЕРВОЙ СТУПЕНИ (сервер: код 1, ступень 0, пустой продукт) — К-3 исправлено ──
			At(2.6f, [Mk, Reply, Mats122Used, LocS, Check, IsWhite, Inv]()
			{
				Reply(Build6603(1, 0, 0, 0, 0, 0, Mats122Used()));
			});
			At(2.8f, [Mk, LocS, Check, IsWhite, Inv, Craft]()
			{
				Check(Mk->IsAnimating() && Mk->DebugText(77532) == LocS(5575) && IsWhite(Mk->DebugStatusColor()), TEXT("6603 код 1: анимация идёт, статус 5575 белым"));
				Check(!Craft->IsRequestInFlight(), TEXT("ответ снял замок «в полёте»"));
				const FAsdaItemInstance* M = Inv->GetCell(2, 40);
				Check(M && M->IsEmpty(), TEXT("материалы count 0 ⇒ ячейки удалены сразу (как эталон)"));
			});
			At(3.6f, [Mk, Check]() { Check(Mk->DebugBarFrac() > 0.2f && Mk->DebugBarFrac() < 0.8f && !Mk->DebugShown(77552), FString::Printf(TEXT("полоса ступени идёт: %.2f, подложка текущей ступени снята"), Mk->DebugBarFrac())); });
			At(4.9f, [Mk, Fmt, Check, IsRed]()
			{
				Check(Mk->DebugShown(77542) && !Mk->DebugShown(77522), TEXT("ступень 0 проиграна: крестик на слоте 1, ани нет"));
				Check(Mk->DebugText(77532) == Fmt(5578, 72174) && IsRed(Mk->DebugStatusColor()), FString::Printf(TEXT("статус «%s» красным"), *Mk->DebugText(77532)));
			});
			At(6.0f, [Mk, Fmt, JHas, Check, IsRed, Dump]()
			{
				Check(!Mk->IsAnimating() && Mk->DebugText(77532) == Fmt(5578, 72174) && IsRed(Mk->DebugStatusColor()),
					FString::Printf(TEXT("К-3 (1): итог провала — статус «%s» (не пустая строка)"), *Mk->DebugText(77532)));
				Check(JHas(*FString::Printf(TEXT("chat+plank «%s»"), *Fmt(5578, 72174))), TEXT("К-3 (1): лента + плашка «Не удалось создать …»"));
				Check(!Mk->DebugTierFlag(4) && !Mk->DebugTierFlag(3), TEXT("К-3 (1): финальная подсветка «база − 1» не ставится"));
				Check(Mk->DebugControlsEnabled() && Mk->DebugText(77537) == TEXT("1") && Mk->GetRemaining() == 0, TEXT("серия (1 шт.) окончена: кнопки включены, поле «1»"));
				Dump(TEXT("провал на первой (исправлено)"));
			});
			At(6.2f, [Shot]() { Shot(TEXT("make_fail_first")); });
			// ── Шаг 6: К-3 как оригинал — пустая строка и подсветка слота «база − 1» ──
			At(6.4f, [Mk, SetCVar, PutMats122, Arm, Check]()
			{
				SetCVar(TEXT("asda.Craft.FixZeroTierResult"), 0);
				PutMats122();
				Check(Arm(6602), TEXT("перехват 6602 (К-3, 0)"));
				Mk->DebugPressButton(77533);
			});
			At(6.6f, [Reply, Mats122Used]() { Reply(Build6603(1, 0, 0, 0, 0, 0, Mats122Used())); });
			At(9.9f, [Mk, SetCVar, JHas, Check, Dump]()
			{
				Check(!Mk->IsAnimating() && Mk->DebugText(77532).IsEmpty(), FString::Printf(TEXT("К-3 (0): как оригинал — итог «получен» с ПУСТОЙ строкой («%s»)"), *Mk->DebugText(77532)));
				Check(JHas(TEXT("пустая строка")), TEXT("К-3 (0): в ленту/плашку ушла пустая строка (журнал)"));
				Check(Mk->DebugTierFlag(4), TEXT("К-3 (0): подсветка на слоте «база − 1» = 0 (dword 4) — как эталон"));
				SetCVar(TEXT("asda.Craft.FixZeroTierResult"), 1);
				Dump(TEXT("провал на первой (оригинал)"));
			});
			// ── Шаг 7: пройдена ступень 0, провал на 1: итог — продукт ступени 0, 5577 синим, лента/плашка ──
			At(10.1f, [Mk, PutMats122, Arm, Check, Inv]()
			{
				Inv->DebugPutItem(2, 52, 0, 0);
				PutMats122();
				Check(Arm(6602), TEXT("перехват 6602 (ступень 1)"));
				Mk->DebugPressButton(77533);
			});
			At(10.3f, [Reply, Mats122Used]() { Reply(Build6603(1, 1, 0, 72174, 52, 1, Mats122Used())); });
			At(12.6f, [Mk, Fmt, Check, IsBlue]()
			{
				Check(Mk->DebugShown(77522) && !Mk->DebugShown(77542), TEXT("ступень 0 пройдена: Ani_Soul2 на слоте 1"));
				Check(Mk->DebugText(77532) == Fmt(5576, 72174) && IsBlue(Mk->DebugStatusColor()), FString::Printf(TEXT("статус «%s» синим"), *Mk->DebugText(77532)));
			});
			At(13.6f, [Mk, Check]()
			{
				Check(Mk->GetCurTier() == 1 && Mk->DebugTierFlag(6) && !Mk->DebugTierFlag(5), TEXT("следующая ступень 1: флаг +0x24F перешёл со слота 1 на слот 2"));
				// ★ Волна В5 (ревизия В4 С-1): флаг +0x24F — «рисовать поверх соседей» (`CControl_RenderChildren`).
				FString Why;
				const bool bTop = Mk->DebugDrawnAboveNeighbors(6, Why);
				Check(bTop, FString::Printf(TEXT("С-1: текущая ступень (слот 2) рисуется поверх соседних — %s"), *Why));
				Check(Mk->DebugZOrder(5) < Mk->DebugZOrder(6), FString::Printf(TEXT("С-1: прежняя ступень (слот 1) вернула свой порядок: z %d < z %d"), Mk->DebugZOrder(5), Mk->DebugZOrder(6)));
			});
			At(15.6f, [Mk, Fmt, Check, IsRed]()
			{
				Check(Mk->DebugShown(77543), TEXT("ступень 1 проиграна: крестик на слоте 2"));
			});
			At(16.6f, [Mk, Inv, Fmt, JHas, Check, IsBlue, Dump]()
			{
				Check(!Mk->IsAnimating() && Mk->DebugText(77532) == Fmt(5577, 72174) && IsBlue(Mk->DebugStatusColor()), FString::Printf(TEXT("итог «%s» синим"), *Mk->DebugText(77532)));
				Check(JHas(*FString::Printf(TEXT("chat+plank «%s»"), *Fmt(5577, 72174))), TEXT("итог: лента −11514 + плашка"));
				const FAsdaItemInstance* P = Inv->GetCell(2, 52);
				Check(P && P->ItemCode == 72174, TEXT("продукт ступени 0 положен в сумку концом анимации (OnRecv_ItemUpdate)"));
				Check(Mk->DebugTierFlag(4 + 1), TEXT("финальная подсветка — слот достигнутой ступени (1)"));
				Dump(TEXT("ступень 0 из 5"));
			});
			At(16.8f, [Shot]() { Shot(TEXT("make_tier1")); });
			// ── Шаг 8: прерывание серии — «Закрыть» во время анимации: кнопка прячется, в конце штуки — сброс,
			//    снимок окон возвращает сумку, HUD развёрнут ──
			At(17.0f, [Mk, Put, Arm, Check]()
			{
				Put(40, 72635, 6); Put(41, 72732, 2); Put(42, 20959, 2);
				Mk->DebugTypeQty(TEXT("2"));
				Check(Arm(6602), TEXT("перехват 6602 (прерывание)"));
				Mk->DebugPressButton(77533);
			});
			At(17.2f, [Reply]() { Reply(Build6603(1, 0, 0, 0, 0, 0, { { 72635, 40, 3 }, { 72732, 41, 1 }, { 20959, 42, 1 } })); });
			At(17.6f, [Mk, Check]()
			{
				Mk->DebugPressButton(77534);
				Check(!Mk->DebugShown(77534) && Mk->IsOpen(), TEXT("77534 во время анимации: «отмена после текущей», кнопка спрятана, окно открыто"));
			});
			At(20.8f, [Mk, Inv, WM, Craft, Check, Dump]()
			{
				Check(!Mk->IsOpen(), TEXT("отмена: после текущей штуки окно сброшено (itemQtyDlgReset)"));
				Check(Craft->DebugCaptured.Num() == 0 || !Craft->IsRequestInFlight(), TEXT("второй 6602 серии не ушёл"));
				Check(Inv->IsWindowOpen() && !WM->IsWindowSlotCaptured(0), TEXT("uiCloseAllToggleWindows: сумка возвращена снимком окна состава, флаги сняты"));
				Check(!WM->ArePlayerWindowsHidden(), TEXT("uiExpandPartyPanel: HUD развёрнут, хотбар разблокирован"));
				Dump(TEXT("прерывание"));
			});
			// ── Шаг 9: «+1 шт.» и СЕРИЯ: 2 штуки рецепта 1 — второй 6602 уходит только после ответа и анимации ──
			At(21.2f, [Mk, Put, Arm, Check, Inv]()
			{
				Inv->CloseWindow();
				Put(44, 20931, 6);
				Inv->DebugPutItem(2, 53, 0, 0);
				Check(Mk->OpenForRecipe(1, TEXT("самотест: серия")), TEXT("окно 77500 рецептом 1 (1 ступень, rate +2 = 0)"));
				Check(Mk->GetTierCount() == 1 && Mk->DebugShown(77513) && !Mk->DebugShown(77516), TEXT("1 ступень — слот 2 (база 2), слот бонуса 5 скрыт"));
				Mk->DebugPressButton(77538);
				Check(Arm(6602), TEXT("перехват 6602 №1"));
				Mk->DebugPressButton(77533);
				Check(Mk->GetRemaining() == 2 && Mk->IsAwaitingReply(), TEXT("серия 2 шт.: ушёл 6602 №1, ждём ответа"));
			});
			At(21.5f, [Mk, Craft, Check]()
			{
				Check(!Craft->DebugIsCaptureArmed() && Craft->IsRequestInFlight(), TEXT("до ответа второй 6602 не отправляется (замок и порядок серии)"));
			});
			At(21.7f, [Reply]() { Reply(Build6603(1, 1, 1, 20962, 53, 2, { { 20931, 44, 3 } }, 1)); });
			At(24.0f, [Mk, Fmt, Check, IsBlue, Arm]()
			{
				Check(Mk->DebugShown(77516) && Mk->DebugIconCode(5) == 20962 && Mk->DebugShown(77526), TEXT("«+1 шт.»: иконка результата в 77516 и её анимация"));
				Check(Mk->DebugText(77532) == Fmt(5579, 20962) && IsBlue(Mk->DebugStatusColor()), FString::Printf(TEXT("статус «%s»"), *Mk->DebugText(77532)));
				Check(Arm(6602), TEXT("перехват 6602 №2 (до конца анимации)"));
			});
			// ★ R5-fix-b 13.11.2026 (третий заход): проверки штуки 1 — по событию «6602 №2 пойман» (с 25,0 с, потолок 5 с), ответ на него — через 0,2 с
			//   после них. Под нагрузкой один длинный кадр сдвигал конец анимации штуки 1 за 25,0 с: четыре проверки читали состояние
			//   до итога штуки и до отправки 6602 №2.
			//   ★ R5-fix-b 13.11.2026 (заключительный заход): ворота общей очереди (`When`) вместо своего опроса — ответ (25,2 с) и всё дальше отсчитываются от
			//   момента события, а итог серии (28,5 с) ждёт конца анимации штуки 2 (потолок 5 с).
			When(25.0f, [Captured]() { return Captured(6602).Num() > 0; }, 5.f, [Mk, Fmt, Captured, JHas, Check, Inv]()
			{
				Check(JHas(*FString::Printf(TEXT("chat+plank «%s»"), *Fmt(5580, 20962))), TEXT("итог штуки 1: « Ты получил 2 доп. '…'» в ленту и плашку"));
				Check(Captured(6602).Num() == 26 && Mk->GetRemaining() == 1 && Mk->IsAwaitingReply(), TEXT("после ответа и анимации ушёл 6602 №2 (N штук = N пакетов)"));
				Check(!Mk->DebugShown(77516), TEXT("иконка бонуса 77516 спрятана перед следующей штукой"));
				const FAsdaItemInstance* P = Inv->GetCell(2, 53);
				Check(P && P->ItemCode == 20962 && P->Amount == 2, TEXT("продукт «+1 шт.» (2 шт.) положен"));
			});
			At(25.2f, [Reply]() { Reply(Build6603(1, 1, 0, 20962, 53, 3, { { 20931, 44, 0 } }, 1)); });
			When(28.5f, [Mk]() { return !Mk->IsAnimating() && Mk->GetRemaining() == 0; }, 5.f, [Mk, Fmt, Check, Dump]()
			{
				Check(!Mk->IsAnimating() && Mk->GetRemaining() == 0 && Mk->DebugText(77532) == Fmt(5577, 20962) && Mk->DebugControlsEnabled(),
					TEXT("штука 2: итог 5577, серия окончена, кнопки включены"));
				Dump(TEXT("серия + бонус"));
			});
			At(28.7f, [Shot]() { Shot(TEXT("make_series")); });
			// ── Шаг 10: К-1 — «Закрыть» ДО ответа: окно сброшено; поздний ответ кладёт продукт сразу (1) / теряется (0) ──
			At(29.0f, [Mk, Put, Arm, Check, Inv]()
			{
				Put(44, 20931, 3); Inv->DebugPutItem(2, 54, 0, 0);
				Check(Arm(6602), TEXT("перехват 6602 (К-1)"));
				Mk->DebugPressButton(77533);
				Mk->DebugPressButton(77534);
				Check(!Mk->IsOpen(), TEXT("К-1: «Закрыть» до ответа — окно сброшено"));
			});
			At(29.2f, [Reply]() { Reply(Build6603(1, 1, 0, 20962, 54, 1, { { 20931, 44, 0 } }, 1)); });
			At(29.5f, [Mk, Inv, JHas, Check]()
			{
				const FAsdaItemInstance* P = Inv->GetCell(2, 54);
				Check(P && P->ItemCode == 20962 && !Mk->IsAnimating() && JHas(TEXT("не забран окном")), TEXT("К-1 (1): поздний 6603 — продукт в сумке сразу, анимации нет, вылета нет"));
			});
			At(29.7f, [Mk, Put, Arm, SetCVar, Check, Inv]()
			{
				SetCVar(TEXT("asda.Craft.FixLateResult"), 0);
				Inv->CloseWindow();
				Put(44, 20931, 3); Inv->DebugPutItem(2, 55, 0, 0);
				Mk->OpenForRecipe(1, TEXT("самотест: К-1 (0)"));
				Arm(6602);
				Mk->DebugPressButton(77533);
				Mk->DebugPressButton(77534);
			});
			At(29.9f, [Reply]() { Reply(Build6603(1, 1, 0, 20962, 55, 1, { { 20931, 44, 0 } }, 1)); });
			At(30.2f, [Mk, Inv, SetCVar, Check]()
			{
				const FAsdaItemInstance* P = Inv->GetCell(2, 55);
				Check(P && P->IsEmpty() && !Mk->IsAnimating(), TEXT("К-1 (0): как оригинал — продукт потерян (в оригинале ещё и вылет, порт не падает)"));
				SetCVar(TEXT("asda.Craft.FixLateResult"), 1);
			});
			// ── Шаг 11: коды отказа 6603 — ONEBUT + сброс окна (4 → 5567; 10 → 5566 по К-5) ──
			At(30.4f, [Mk, Put, Arm, Craft, Check, Inv, WM]()
			{
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				Inv->CloseWindow();
				Put(44, 20931, 3);
				Mk->OpenForRecipe(1, TEXT("самотест: отказы"));
				Arm(6602); Mk->DebugPressButton(77533);
				Craft->ClearNoticeJournal();
				TArray<uint8> B; B.SetNumZeroed(525); B[0] = 4; Craft->DebugInject(6603, B);
			});
			At(30.6f, [Mk, NHas, Put, Arm, Craft, Check, Inv, WM]()
			{
				Check(NHas(TEXT("msgbox 5567")) && !Mk->IsOpen(), TEXT("6603 код 4 → ONEBUT 5567 и сброс окна"));
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				Inv->CloseWindow();
				Mk->OpenForRecipe(1, TEXT("самотест: код 10"));
				Arm(6602); Mk->DebugPressButton(77533);
				Craft->ClearNoticeJournal();
				TArray<uint8> B; B.SetNumZeroed(525); B[0] = 10; Craft->DebugInject(6603, B);
			});
			At(30.8f, [Mk, NHas, Check, WM]()
			{
				Check(NHas(TEXT("msgbox 5566")) && !Mk->IsOpen(), TEXT("6603 код 10 → ONEBUT 5566 и сброс окна (К-5, окно не висит)"));
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
			});
			// ── Шаг 12: таймаут ответа — замок снят, окно возвращает кнопки ──
			At(31.0f, [Mk, Arm, SetCVar, Check, Inv]()
			{
				Inv->CloseWindow();
				SetCVar(TEXT("asda.Craft.ReplyTimeoutSec"), 1.f);
				Mk->OpenForRecipe(1, TEXT("самотест: таймаут"));
				Arm(6602); Mk->DebugPressButton(77533);
				Check(!Mk->DebugControlsEnabled(), TEXT("таймаут: ждём ответа, кнопки выключены"));
			});
			At(32.4f, [Mk, Craft, SetCVar, Check]()
			{
				Check(!Craft->IsRequestInFlight() && Mk->DebugControlsEnabled() && !Mk->IsAwaitingReply() && Mk->IsOpen(),
					TEXT("таймаут 1 с: замок снят, кнопки возвращены, окно открыто (молчание сервера — не тупик)"));
				SetCVar(TEXT("asda.Craft.ReplyTimeoutSec"), 20.f);
				Mk->ResetDialog(TEXT("самотест: после таймаута"));
			});
			// ── Шаг 13: К-6 — материал в двух стеках (2 + 2 при нужных 3) ──
			At(32.6f, [Mk, Put, Arm, Craft, Captured, Check, Inv, IsRed]()
			{
				Inv->CloseWindow();
				Put(40, 72635, 2); Put(43, 72635, 2); Put(41, 72732, 1); Put(42, 20959, 1);
				Mk->OpenForRecipe(122, TEXT("самотест: К-6"));
				Mk->DebugTypeQty(TEXT("1"));
				Check(IsRed(Mk->DebugEditColor()), TEXT("К-6 (1): ни один стек не даёт 3 — поле красное"));
				Craft->ClearNoticeJournal();
				Arm(6602); Mk->DebugPressButton(77533);
				Check(Captured(6602).Num() == 0 && !Mk->IsOpen(), TEXT("К-6 (1): 6602 не отправлен, окно сброшено"));
				Craft->DebugDisarmCapture(TEXT("К-6 (1)"));
			});
			At(32.8f, [Mk, NHas, Arm, Captured, SetCVar, Check, Inv, WM]()
			{
				Check(NHas(TEXT("msgbox 5566")), TEXT("К-6 (1): ONEBUT 5566 «Недостаточно материалов»"));
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				SetCVar(TEXT("asda.Craft.FixStackPick"), 0);
				Inv->CloseWindow();
				Mk->OpenForRecipe(122, TEXT("самотест: К-6 (0)"));
				Arm(6602); Mk->DebugPressButton(77533);
				const TArray<uint8> B = Captured(6602);
				Check(B.Num() == 26 && AsdaBytes::RdI16(&B[2]) == 43, TEXT("К-6 (0): как оригинал — сумма стеков хватает, в 6602 слот ПОСЛЕДНЕГО стека (43)"));
				SetCVar(TEXT("asda.Craft.FixStackPick"), 1);
				Mk->ResetDialog(TEXT("самотест: после К-6"));
			});
			// ── Шаг 14: Р-4 — 7 ступеней (индекс «база − 1» = −1) — порт не падает ни при 1, ни при 0 ──
			At(33.2f, [Mk, Craft, PutMats122, Arm, Check, Inv]()
			{
				Craft->DebugReleaseInFlight();
				Inv->CloseWindow();
				PutMats122();
				Mk->DebugOverrideRateOut({ 72174, 72197, 72220, 72243, 72266, 72174, 72197 });
				Mk->OpenForRecipe(122, TEXT("самотест: 7 ступеней"));
				Check(Mk->GetTierCount() == 7 && Mk->DebugShown(77511) && Mk->DebugShown(77517), TEXT("7 ступеней (подмена rate): база 0, слоты 0..6"));
				Arm(6602); Mk->DebugPressButton(77533);
			});
			At(33.4f, [Reply, Mats122Used]() { Reply(Build6603(1, 0, 0, 0, 0, 0, Mats122Used())); });
			At(36.6f, [Mk, Check, SetCVar, PutMats122, Arm]()
			{
				Check(!Mk->IsAnimating() && !Mk->DebugTierFlag(3), TEXT("Р-4 (1): провал на первой из 7 — подсветка на индексе −1 не ставится"));
				SetCVar(TEXT("asda.Craft.FixZeroTierResult"), 0);
				PutMats122();
				Arm(6602); Mk->DebugPressButton(77533);
			});
			At(36.8f, [Reply, Mats122Used]() { Reply(Build6603(1, 0, 0, 0, 0, 0, Mats122Used())); });
			At(40.0f, [Mk, Check, SetCVar, Dump]()
			{
				Check(!Mk->IsAnimating() && Mk->DebugTierFlag(3) && !Mk->DebugShown(77527), TEXT("Р-4 (0): как оригинал — флаг на FMark_2 (dword 3), ани 7-й ступени скрыта; вылета нет"));
				SetCVar(TEXT("asda.Craft.FixZeroTierResult"), 1);
				Mk->DebugOverrideRateOut({});
				Mk->ResetDialog(TEXT("самотест: после Р-4"));
				Dump(TEXT("7 ступеней"));
			});
			// ── Шаг 15: свитки в сумке — 101 (рецепт) → TWOBUT 5571 → 6598; 102 (ур. +1) → TWOBUT 5582 → 6604; не тот уровень → 5584 ──
			At(40.4f, [Craft, Inv, Put, Arm, WM, Check, NHas]()
			{
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 30, 6, 100, 10));
				Put(50, 71669, 1);   // «инструкция»: linkCode 101, рецепт 1
				Put(51, 72727, 1);   // свиток уровня: linkCode 102, группа 7
			});
			At(40.6f, [Craft, Inv, Arm, Captured, WM, Check, NHas]()
			{
				Craft->ClearNoticeJournal();
				Check(Inv->UseFromBagWindow(2, 50), TEXT("двойной клик по свитку 71669 в сумке — ветка свитка ремесла"));
				Check(NHas(TEXT("confirm 5571")) && WM->IsOpen(FName(TEXT("TWOBUT_MSGBOX"))), TEXT("TWOBUT 5571 «Прочитать инструкцию?» (команда 84)"));
				Check(Arm(6598), TEXT("перехват 6598"));
				UUserWidget* Dlg = WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX")));
				UButton* Yes = Dlg ? Cast<UButton>(Dlg->GetWidgetFromName(TEXT("TWOBUT_BUT00"))) : nullptr;
				if (Yes) { Yes->OnClicked.Broadcast(); }
				const TArray<uint8> B = Captured(6598);
				Check(B.Num() == 3 && AsdaBytes::RdI16(&B[0]) == 50 && B[2] == 2, TEXT("«Да» → 6598 {слот 50, контейнер 2}"));
				TArray<uint8> R; R.SetNumZeroed(64); R[0] = 1;
				AsdaBytes::WrI32(&R[5], 71669); R[5 + 4] = 2; AsdaBytes::WrI16(&R[5 + 5], 50); AsdaBytes::WrI32(&R[5 + 9], 0);
				AsdaBytes::WrI16(&R[62], 2);
				Craft->DebugInject(6599, R);
			});
			At(40.9f, [Craft, Inv, Arm, Captured, WM, Check, NHas]()
			{
				Check(NHas(TEXT("chat+plank 5572")) && Craft->IsLearned(2), TEXT("6599 код 1: рецепт 2 изучен, чат + плашка 5572"));
				const FAsdaItemInstance* S = Inv->GetCell(2, 50);
				Check(S && S->IsEmpty(), TEXT("6599: свиток израсходован (count 0 ⇒ ячейка удалена)"));
				Craft->ClearNoticeJournal();
				Inv->UseFromBagWindow(2, 51);
				Check(NHas(TEXT("confirm 5582")), TEXT("свиток 72727 (группа 7 = ур. 6 + 1) → TWOBUT 5582 (команда 85)"));
				Arm(6604);
				UUserWidget* Dlg = WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX")));
				UButton* Yes = Dlg ? Cast<UButton>(Dlg->GetWidgetFromName(TEXT("TWOBUT_BUT00"))) : nullptr;
				if (Yes) { Yes->OnClicked.Broadcast(); }
				const TArray<uint8> B = Captured(6604);
				Check(B.Num() == 3 && AsdaBytes::RdI16(&B[0]) == 51 && B[2] == 2, TEXT("«Да» → 6604 {слот 51, контейнер 2}"));
				TArray<uint8> R; R.SetNumZeroed(61); R[0] = 1; R[1] = 7;
				AsdaBytes::WrI32(&R[4], 72727); R[4 + 4] = 2; AsdaBytes::WrI16(&R[4 + 5], 51); AsdaBytes::WrI32(&R[4 + 9], 0);
				Craft->DebugInject(6605, R);
			});
			At(41.2f, [Craft, Inv, Put, WM, Check, NHas, Dump]()
			{
				Check(NHas(TEXT("chat+plank 5583")) && Craft->GetCraftLevel() == 7, TEXT("6605 код 1: уровень ремесла 7, чат + плашка 5583"));
				Put(51, 72727, 1);
				Craft->ClearNoticeJournal();
				Inv->UseFromBagWindow(2, 51);
				Check(NHas(TEXT("msgbox 5584")) && !NHas(TEXT("confirm")), TEXT("свиток 72727 при ур. 7 (нужен 6) → ONEBUT 5584, подтверждения нет"));
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				// Хотбар (`itemUseDoubleClick`) веток 101/102 не имеет — свиток с хотбара рецепт не учит (1:1).
				Put(50, 71669, 1);
				Craft->ClearNoticeJournal();
				Inv->UseByDoubleClick(2, 50);
				Check(!NHas(TEXT("confirm")) && !WM->IsOpen(FName(TEXT("TWOBUT_MSGBOX"))), TEXT("путь хотбара (itemUseDoubleClick): свиток НЕ предлагает изучение — как оригинал"));
				Dump(TEXT("свитки"));
			});
			At(41.6f, [Stat, bQuit, W]()
			{
				// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Craft.FixSummaryRowDarken/FixZeroTierResult/FixLateResult/FixStackPick, Craft.ReplyTimeoutSec) — к исходному значению И приоритету SetBy
				//   (прежде — константы 1 / 20 с SetByCode).
				AsdaCVarSave::RestoreAll(TEXT("asda.Craft.SelfTestMake"));
				AsdaSelfTestKit::Summary(LogAsdaCraftMake, TEXT("САМОТЕСТ-77500"), Stat->Ok, Stat->Fail, TEXT(""), true);   // ★ 11.10.2026 (В3 S1): единый итог
				AsdaSelfTestKit::Quit(W, bQuit);
			});
		}));
}
#endif // ASDA_DEV_CONSOLE
