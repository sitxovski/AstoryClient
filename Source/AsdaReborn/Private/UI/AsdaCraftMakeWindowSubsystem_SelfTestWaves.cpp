// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCraftMakeWindowSubsystem_SelfTestWaves.cpp — ★ R3-c 12.11.2026 (разрез AsdaCraftMakeWindowSubsystem.cpp, дословный перенос): самотесты снимка, W5 и W8.
//   Общее для частей — в AsdaCraftMakeWindowSubsystem_Private.h; шапка и реверс — в AsdaCraftMakeWindowSubsystem.cpp.
#include "UI/AsdaCraftMakeWindowSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ R5-fix2-c (13.11.2026): `AsdaSelfTestKit::DelayArgOr`
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой

#include "AsdaCraftMakeWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCraftMakeWindowSubsystem*.cpp

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
// ★ САМОТЕСТ СНИМКА ОКОН (волна В4, ревизия В3 К-1/С-1/С-2/С-3) — боевыми путями диалога NPC и магазинов.
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftSnapshotSelfTest(
		TEXT("asda.Craft.SelfTestSnapshot"),
		TEXT("Снимок открытых окон: слоты 1/4/13 через диалог NPC (К-1), 30035 и 30020 — разговор прячется, окна возвращает закрытие "
		     "(С-3/С-1), элитный магазин 6000, закрыватели 43000/43200/трекера/6500 (С-2). `delay=N` `quit`."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
			UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			UAsdaCraftMakeWindowSubsystem* Mk = GI ? GI->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr;
			if (!WM || !Dlg || !Inv || !Mk) { return; }
			bool bQuit = false; float Delay = 0.f;
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			}
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
			// ★ Волна В8 (ревизия В7 С-3): только офлайн — при живом соединении отказ, иначе окно защиты отправителей.
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Craft.SelfTestSnapshot"), KitArgs)) { return; }
			struct FStat { int32 Ok = 0; int32 Fail = 0; };
			TSharedRef<FStat> Stat = MakeShared<FStat>();
			TWeakObjectPtr<UWorld> WGate(W);   // мир прогона (для отказа на отложенном старте)
			// ★ Р-2 (10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим таймером;
			//   при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
			// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Craft.SelfTestSnapshot"), bQuit);   // общий флаг отказа всех шагов прогона
			auto GateRefused = [Gate, WGate]() { return !Gate->Pass(WGate.Get()); };   // true — прогон отменён; проверка один раз, первым сработавшим шагом
			auto At = [W, Mk, Delay, GateRefused](float T, TFunction<void()> Fn)
			{
				FTimerHandle H;
				W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Mk, [GateRefused, Fn = MoveTemp(Fn)]()
				{
					if (GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
					Fn();
				}), T + Delay, false);
			};
			auto Check = [Stat](bool bOk, const FString& What)
			{
				if (bOk) { ++Stat->Ok; } else { ++Stat->Fail; }
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("САМОТЕСТ-СНИМОК %s %s"), bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
			};
			// ── К-1: слоты 1 (персонаж), 4 (квест-гид), 13 (книга навыков) — через настоящий вход/выход разговора ──
			static bool bSkillOk = false;
			At(0.5f, [GI, WM, Dlg, Check]()
			{
				UAsdaCharaInfoSubsystem* Ci = GI->GetSubsystem<UAsdaCharaInfoSubsystem>();
				UAsdaQuestLogSubsystem* Ql = GI->GetSubsystem<UAsdaQuestLogSubsystem>();
				UAsdaSkillWindowSubsystem* Sk = GI->GetSubsystem<UAsdaSkillWindowSubsystem>();
				if (Ci && !Ci->IsWindowOpen()) { Ci->OpenWindow(); }
				if (Ql && !Ql->IsOpen()) { Ql->Open(); }
				if (Sk && !Sk->IsWindowOpen()) { Sk->OpenWindow(); }
				// Книга навыков офлайн без профессии не открывается (ONEBUT «ты не меняешь профессию», job = −1) — тогда слот 13
				//   здесь не проверяется (записано в отчёте), а модалку закрываем, чтобы она не держала сумку.
				bSkillOk = Sk && Sk->IsWindowOpen();
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				Check(Ci && Ci->IsWindowOpen() && Ql && Ql->IsOpen(), TEXT("открыты окно персонажа и квест-гид"));
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("САМОТЕСТ-СНИМОК: книга навыков %s"), bSkillOk ? TEXT("открыта — слот 13 проверяется") : TEXT("офлайн не открылась (нет профессии) — слот 13 не проверяется"));
				const bool bOpen = Dlg->OpenForNpcId(1);
				Check(bOpen && Dlg->IsDialogOpen(), TEXT("разговор с NPC 1 открыт"));
				Check(WM->IsWindowSlotCaptured(1) && WM->IsWindowSlotCaptured(4) && (!bSkillOk || WM->IsWindowSlotCaptured(13)), TEXT("снимок: слоты 1, 4 (и 13) подняты"));
				Check(!Ci->IsWindowOpen() && !Ql->IsOpen() && !(Sk && Sk->IsWindowOpen()), TEXT("uiCloseAllWindows закрыл их"));
			});
			At(0.9f, [GI, WM, Dlg, Check]() mutable
			{
				Dlg->Close();
				UAsdaCharaInfoSubsystem* Ci = GI->GetSubsystem<UAsdaCharaInfoSubsystem>();
				UAsdaQuestLogSubsystem* Ql = GI->GetSubsystem<UAsdaQuestLogSubsystem>();
				UAsdaSkillWindowSubsystem* Sk = GI->GetSubsystem<UAsdaSkillWindowSubsystem>();
				Check(Ci && Ci->IsWindowOpen(), TEXT("К-1: уход от NPC вернул окно персонажа (слот 1)"));
				Check(Ql && Ql->IsOpen(), TEXT("К-1: уход от NPC вернул квест-гид (слот 4)"));
				if (bSkillOk) { Check(Sk && Sk->IsWindowOpen(), TEXT("уход от NPC вернул книгу навыков (слот 13)")); }
				Check(!WM->ArePlayerWindowsHidden(), TEXT("признак «окна убраны сценой» снят после восстановления"));
				if (Ci) { Ci->CloseWindow(); }
				if (Ql) { Ql->Close(); }
				if (Sk) { Sk->CloseWindow(); }
			});
			// ── С-3: 30035 — разговор ПРЯЧЕТСЯ под окном 16000, окна снимка возвращает ЗАКРЫТИЕ 16000 ──
			At(1.3f, [GI, WM, Dlg, Inv, Check]()
			{
				Inv->OpenWindow();
				Dlg->OpenForNpcId(1);
				Check(!Inv->IsWindowOpen() && WM->IsWindowSlotCaptured(0), TEXT("разговор: сумка снята снимком и закрыта"));
				Dlg->RunCommandForCheck(30035, 0);
				UAsdaExtractShopSubsystem* Ex = GI->GetSubsystem<UAsdaExtractShopSubsystem>();
				Check(Ex && Ex->IsOpen(), TEXT("30035: окно передачи параметров 16000 открыто"));
				Check(Dlg->IsHiddenForChildWindow() && !WM->IsWindowVisible(FName(TEXT("NPC_LINE_NPC"))), TEXT("30035: разговор спрятан (dialogOnConfirmClose), не закончен"));
				Check(!Inv->IsWindowOpen() && WM->IsWindowSlotCaptured(0), TEXT("С-3: при ОТКРЫТИИ 16000 окна снимка НЕ переоткрыты"));
				if (Ex) { Ex->CloseWindow(); }
				Check(!Dlg->IsDialogOpen() && Inv->IsWindowOpen() && !WM->IsWindowSlotCaptured(0), TEXT("С-3: закрытие 16000 → 5045: разговор окончен, сумка возвращена"));
				Inv->CloseWindow();
			});
			// ── С-3: 30046 — окно 20200 разговор не закрывает; окна снимка — по уходу от NPC ──
			At(1.7f, [GI, WM, Dlg, Inv, Check]()
			{
				Inv->OpenWindow();
				Dlg->OpenForNpcId(1);
				Dlg->RunCommandForCheck(30046, 0);
				Check(Dlg->IsDialogOpen() && !Dlg->IsHiddenForChildWindow() && !Inv->IsWindowOpen(), TEXT("30046: разговор продолжается, окна снимка не переоткрыты"));
				Dlg->Close();
				Check(Inv->IsWindowOpen(), TEXT("30046: уход от NPC вернул сумку"));
				Inv->CloseWindow();
			});
			// ── С-1: 30020 — магазин снаряжения 6800: разговор спрятан, выход из разговора и возврат окон — закрытие магазина ──
			At(2.1f, [GI, WM, Dlg, Inv, Check]()
			{
				Inv->OpenWindow();
				Dlg->OpenForNpcId(1);
				Dlg->RunCommandForCheck(30020, 0);
				UAsdaEquipShopSubsystem* Eq = GI->GetSubsystem<UAsdaEquipShopSubsystem>();
				Check(Eq && Eq->IsOpen() && Dlg->IsHiddenForChildWindow(), TEXT("30020: магазин 6800 открыт, разговор спрятан (shopWin_Open → dialogOnConfirmClose)"));
				Check(!Inv->IsWindowOpen(), TEXT("30020: окна снимка при открытии магазина не переоткрыты"));
				if (Eq) { Eq->CloseWindow(); }
				Check(!Dlg->IsDialogOpen() && Inv->IsWindowOpen() && !WM->ArePlayerWindowsHidden(), TEXT("С-1: закрытие 6800 → 5045: разговор окончен, сумка возвращена, HUD развёрнут"));
			});
			// ── С-1: мини-снимок 6800 (сумка открыта в момент открытия магазина) ──
			At(2.5f, [GI, WM, Inv, Check]()
			{
				UAsdaEquipShopSubsystem* Eq = GI->GetSubsystem<UAsdaEquipShopSubsystem>();
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
				if (Eq) { Eq->OpenWindow(); }
				Check(Eq && Eq->IsOpen() && !Inv->IsWindowOpen(), TEXT("6800 без разговора: сумка закрыта мини-снимком (uiMarkPendingWindowOpen)"));
				if (Eq) { Eq->CloseWindow(); }
				Check(Inv->IsWindowOpen(), TEXT("закрытие 6800: сумка возвращена (uiShellStore_RefreshOpenWindows)"));
			});
			// ── С-1: элитный магазин 6000 — снимок + «закрыть всё» при открытии, возврат при закрытии ──
			At(2.9f, [GI, WM, Inv, Check]()
			{
				UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
				if (Ps) { Ps->OpenWindow(false); }
				Check(Ps && Ps->IsOpen() && !Inv->IsWindowOpen() && WM->IsWindowSlotCaptured(0)
					&& (WM->GetWindowSlotOwners(0) & (uint8)UAsdaWindowSubsystem::EWindowSnapshotOwner::Shop) != 0,
					TEXT("6000: снимок (владелец «магазин») и «закрыть всё» — сумка закрыта (charBeginAutoMove)"));
				if (Ps) { Ps->CloseWindow(); }
				Check(Inv->IsWindowOpen() && !WM->IsWindowSlotCaptured(0) && !WM->ArePlayerWindowsHidden(), TEXT("закрытие 6000 → 5045: сумка возвращена, HUD развёрнут"));
				Inv->CloseWindow();
			});
			// ── С-2: закрыватели «закрыть всё» и слот 9 (окно питомца 43000) ──
			At(3.3f, [GI, WM, Check]()
			{
				UAsdaPetCageSubsystem* Pc = GI->GetSubsystem<UAsdaPetCageSubsystem>();
				const bool bCage = Pc && (Pc->IsCageOpen() || Pc->OpenCage());
				Check(bCage, TEXT("окно питомца 43000 открыто"));
				WM->CaptureOpenWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::NpcDialog, TEXT("самотест С-2"));
				Check(WM->IsWindowSlotCaptured(9), TEXT("снимок: слот 9 (43000) поднят"));
				WM->CloseAllPlayerWindows();
				Check(Pc && !Pc->IsCageOpen(), TEXT("С-2: «закрыть всё» закрыло 43000 (uiCloseWindowReset)"));
				WM->ReopenCapturedWindows(TEXT("самотест С-2"));
				WM->RestoreAfterSceneExit();
				Check(Pc && Pc->IsCageOpen(), TEXT("С-2: восстановление снимка открыло 43000 (CRefineWnd_Open)"));
				if (Pc) { Pc->CloseCage(); }
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("%s"), *WM->DumpWindowSnapshot());
			});
			At(3.6f, [Stat, bQuit, W]()
			{
				AsdaSelfTestKit::Summary(LogAsdaCraftMake, TEXT("САМОТЕСТ-СНИМОК"), Stat->Ok, Stat->Fail, TEXT(""), true);   // ★ 11.10.2026 (В3 S1): единый итог
				AsdaSelfTestKit::Quit(W, bQuit);
			});
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
// ★ САМОТЕСТ ВОЛНЫ В5 (ревизия В4: W4-1, W4-3, С-2, С-3, МЕЛК-14) — боевыми путями, путь игрока (-game). Порядок:
//   звуки окон-сообщений (ровно по одному; кнопки — делегатом `OnClicked`, ESC — `CloseTopmostOnEsc`), команда 30030,
//   значок званий в снимке (слот 8), плашка 221 после «смены карты» (сброс сессии MapChanged — тот же, что у «портала»
//   самотеста 76000), смерть с открытым элитным магазином 6000 (тот же путь, что пакет 4053: `EnterSelfDeadState`).
//   Смерть — последней (после неё идёт окно смерти). `delay=N` `quit`.
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftW5SelfTest(
		TEXT("asda.Craft.SelfTestW5"),
		TEXT("Волна В5: звуки 25/4/5 окон-сообщений в общей точке (по одному), 30030 → элитный магазин 6000, слот 8 снимка (значок "
		     "званий), плашка 221 после смены карты, смерть закрывает 6000. `delay=N` `quit`."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
			UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			UAsdaCraftMakeWindowSubsystem* Mk = GI ? GI->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr;
			UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
			if (!WM || !Dlg || !Inv || !Mk || !Craft) { return; }
			bool bQuit = false; float Delay = 0.f;
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			}
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
			// ★ Волна В8 (ревизия В7 С-3): только офлайн — при живом соединении отказ, иначе окно защиты отправителей.
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Craft.SelfTestW5"), KitArgs)) { return; }
			struct FStat { int32 Ok = 0; int32 Fail = 0; };
			TSharedRef<FStat> Stat = MakeShared<FStat>();
			// ★ 11.10.2026 (В3 S1, N1(г)): проход С-3 делает своего персонажа «известным» и МЁРТВЫМ (`EnterSelfDeadState`) и прежде
			//   так и оставлял — в пакетном прогоне все следующие самотесты с живым персонажем падали. Снимок «своего» берётся здесь,
			//   возврат (с воскрешением делегатом 4055/4056) — в последнем шаге, после итога.
			TSharedRef<FAsdaNetSelfScope> SelfScope = MakeShared<FAsdaNetSelfScope>(GI->GetSubsystem<UAsdaNetSubsystem>(), TEXT("asda.Craft.SelfTestW5"));
			TWeakObjectPtr<UWorld> WGate(W);   // мир прогона (для отказа на отложенном старте)
			// ★ Р-2 (10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим таймером;
			//   при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
			// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Craft.SelfTestW5"), bQuit);   // общий флаг отказа всех шагов прогона
			auto GateRefused = [Gate, WGate]() { return !Gate->Pass(WGate.Get()); };   // true — прогон отменён; проверка один раз, первым сработавшим шагом
			auto At = [W, Mk, Delay, GateRefused](float T, TFunction<void()> Fn)
			{
				FTimerHandle H;
				W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Mk, [GateRefused, Fn = MoveTemp(Fn)]()
				{
					if (GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
					Fn();
				}), T + Delay, false);
			};
			auto Check = [Stat](bool bOk, const FString& What)
			{
				if (bOk) { ++Stat->Ok; } else { ++Stat->Fail; }
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("САМОТЕСТ-В5 %s %s"), bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
			};
			// Счётчики звуков окон-сообщений (25 показ, 4 ответ кнопкой, 5 закрытие) — дельты от начала шага.
			struct FSfx { int32 S25 = 0, S4 = 0, S5 = 0; };
			auto Sfx = [WM]() { FSfx S; S.S25 = WM->GetMsgBoxSfxCount(25); S.S4 = WM->GetMsgBoxSfxCount(4); S.S5 = WM->GetMsgBoxSfxCount(5); return S; };
			auto SfxIs = [Sfx](const FSfx& From, int32 D25, int32 D4, int32 D5)
			{
				const FSfx N = Sfx();
				return N.S25 - From.S25 == D25 && N.S4 - From.S4 == D4 && N.S5 - From.S5 == D5;
			};
			auto SfxStr = [Sfx](const FSfx& From)
			{
				const FSfx N = Sfx();
				return FString::Printf(TEXT("звуков: 25×%d, 4×%d, 5×%d"), N.S25 - From.S25, N.S4 - From.S4, N.S5 - From.S5);
			};
			auto Btn = [WM](const TCHAR* Win, const TCHAR* Name) -> UButton*
			{
				UUserWidget* Wd = WM->FindWindow(FName(Win));
				return Wd ? Cast<UButton>(Wd->GetWidgetFromName(FName(Name))) : nullptr;
			};
			auto NoOwnClickSound = [](const UButton* B) { return B && B->GetStyle().PressedSlateSound.GetResourceObject() == nullptr; };

			// ── W4-1: звуки окон-сообщений — общая точка, ровно по одному ──
			At(0.3f, [WM, Craft, Check, Sfx, SfxIs, SfxStr, Btn, NoOwnClickSound]()
			{
				// Чистый старт: очередь окон-сообщений (26.09) снимается целиком молча — нотисы прошлых проходов не мешают.
				WM->ClearMsgBoxQueue(TEXT("самотест В5: чистый старт"));
				// 1) ONEBUT ремесла (`NoticeMsgBox`): 25 при показе — один раз (своих звуков у ремесла больше нет).
				FSfx S0 = Sfx();
				Craft->NoticeMsgBox(AsdaCraftLoc::kUnknownData);
				Check(WM->IsOpen(FName(TEXT("ONEBUT_MSGBOX"))) && SfxIs(S0, 1, 0, 0), FString::Printf(TEXT("ONEBUT ремесла 5563 показан: %s (ожидалось 25×1)"), *SfxStr(S0)));
				UButton* Ok = Btn(TEXT("ONEBUT_MSGBOX"), TEXT("ONEBUT_BUT00"));
				Check(NoOwnClickSound(Ok), TEXT("у кнопки ONEBUT_BUT00 нет своего звука нажатия (PressedSlateSound пуст) — звук 4 не удвоится"));
				// 2) «ОК» кнопкой (делегат `OnClicked` — путь игрока): 4 + 5.
				S0 = Sfx();
				if (Ok) { Ok->OnClicked.Broadcast(); }
				Check(!WM->IsOpen(FName(TEXT("ONEBUT_MSGBOX"))) && SfxIs(S0, 0, 1, 1), FString::Printf(TEXT("ONEBUT «ОК»: окно закрыто, %s (ожидалось 4×1, 5×1)"), *SfxStr(S0)));
				// 3) TWOBUT ремесла (`NoticeConfirm` 5571): 25 один раз; ESC (= «Нет» 1524) — 4 + 5; «Да» не вызван.
				static bool bYesCalled = false;
				bYesCalled = false;
				S0 = Sfx();
				Craft->NoticeConfirm(AsdaCraftLoc::kAskLearn, []() { bYesCalled = true; }, 84);
				Check(WM->IsOpen(FName(TEXT("TWOBUT_MSGBOX"))) && SfxIs(S0, 1, 0, 0), FString::Printf(TEXT("TWOBUT ремесла 5571 показан: %s (ожидалось 25×1)"), *SfxStr(S0)));
				Check(NoOwnClickSound(Btn(TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00"))) && NoOwnClickSound(Btn(TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT01"))),
					TEXT("у кнопок TWOBUT_BUT00/01 нет своего звука нажатия"));
				S0 = Sfx();
				const bool bEsc = WM->CloseTopmostOnEsc();
				Check(bEsc && !WM->IsOpen(FName(TEXT("TWOBUT_MSGBOX"))) && !bYesCalled && SfxIs(S0, 0, 1, 1),
					FString::Printf(TEXT("ESC на TWOBUT = «Нет»: окно закрыто, «Да» не вызван, %s (ожидалось 4×1, 5×1)"), *SfxStr(S0)));
				// 4) «Да» кнопкой TWOBUT_BUT00 (общая точка `ShowGameConfirm`): 25, затем 4 + 5 и колбэк «да».
				static int32 Answer = -1;
				Answer = -1;
				S0 = Sfx();
				AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(TEXT("Самотест В5: подтверждение"), [](bool bYes) { Answer = bYes ? 1 : 0; });
				if (UButton* Yes = Btn(TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00"))) { Yes->OnClicked.Broadcast(); }
				Check(Answer == 1 && SfxIs(S0, 1, 1, 1), FString::Printf(TEXT("TWOBUT «Да»: колбэк «да», %s (ожидалось 25×1, 4×1, 5×1)"), *SfxStr(S0)));
				// 5) «Нет» кнопкой TWOBUT_BUT01.
				Answer = -1;
				S0 = Sfx();
				AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(TEXT("Самотест В5: подтверждение 2"), [](bool bYes) { Answer = bYes ? 1 : 0; });
				if (UButton* No = Btn(TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT01"))) { No->OnClicked.Broadcast(); }
				Check(Answer == 0 && SfxIs(S0, 1, 1, 1), FString::Printf(TEXT("TWOBUT «Нет»: колбэк «нет», %s (ожидалось 25×1, 4×1, 5×1)"), *SfxStr(S0)));
				// 6) Программное закрытие ONEBUT другой системой (`CGameUI_CloseWindow` без кнопки): только 5.
				S0 = Sfx();
				WM->ShowGameMessage(TEXT("Самотест В5: сообщение"));
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				Check(SfxIs(S0, 1, 0, 1), FString::Printf(TEXT("ONEBUT закрыт программно: %s (ожидалось 25×1, 5×1, без 4)"), *SfxStr(S0)));
				// 7) Второй TWOBUT поверх первого — ОЧЕРЕДЬ (26.09): первый ждёт под вторым, без 4/5 и без ответа;
				//   «Нет» второму возвращает первый (4, 25, 5); ответ первому — 4 + 5, очередь пуста.
				Answer = -1;
				S0 = Sfx();
				AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(TEXT("Самотест В5: первое"), [](bool bYes) { Answer = bYes ? 1 : 0; });
				AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(TEXT("Самотест В5: второе"), [](bool) {});
				Check(Answer == -1 && WM->GetMsgBoxQueueNum() == 2 && SfxIs(S0, 2, 0, 0),
					FString::Printf(TEXT("второй TWOBUT поверх первого: первый в очереди без ответа, записей %d, %s (ожидалось 25×2)"),
						WM->GetMsgBoxQueueNum(), *SfxStr(S0)));
				S0 = Sfx();
				WM->CloseConfirm(false);
				Check(Answer == -1 && WM->IsOpen(FName(TEXT("TWOBUT_MSGBOX"))) && WM->GetMsgBoxQueueTopText() == TEXT("Самотест В5: первое")
					&& SfxIs(S0, 1, 1, 1), FString::Printf(TEXT("«Нет» второму вернул первый: %s (ожидалось 25×1, 4×1, 5×1)"), *SfxStr(S0)));
				S0 = Sfx();
				WM->CloseConfirm(false);
				Check(Answer == 0 && WM->GetMsgBoxQueueNum() == 0 && !WM->IsOpen(FName(TEXT("TWOBUT_MSGBOX"))) && SfxIs(S0, 0, 1, 1),
					FString::Printf(TEXT("ответ первому: «нет», очередь пуста, %s (ожидалось 4×1, 5×1)"), *SfxStr(S0)));
			});

			// ── W4-3: команда 30030 — элитный магазин 6000 (разговор прячется, окна снимка возвращает закрытие магазина) ──
			At(1.2f, [GI, WM, Dlg, Inv, Check]()
			{
				UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();
				if (Ps && Ps->IsOpen()) { Ps->CloseWindow(); }
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
				Dlg->OpenForNpcId(1);
				Check(Dlg->IsDialogOpen() && !Inv->IsWindowOpen() && WM->IsWindowSlotCaptured(0), TEXT("30030: разговор открыт, сумка снята снимком"));
				Dlg->RunCommandForCheck(30030, 0);
				Check(Ps && Ps->IsOpen(), TEXT("30030: открыт элитный магазин 6000 (uiInventoryOpenWindow), а не сумка"));
				Check(!Inv->IsWindowOpen(), TEXT("30030: сумка НЕ открыта (прежний порт открывал её)"));
				Check(Dlg->IsHiddenForChildWindow(), TEXT("30030: разговор спрятан (dialogOnConfirmClose) и пережил «закрыть всё» магазина"));
				Check(WM->ArePlayerWindowsHidden(), TEXT("30030: HUD свёрнут под магазином"));
				if (Ps) { Ps->CloseWindow(); }
				Check(!Dlg->IsDialogOpen() && Inv->IsWindowOpen() && !WM->IsWindowSlotCaptured(0) && !WM->ArePlayerWindowsHidden(),
					TEXT("закрытие 6000 → 5045: разговор окончен, сумка возвращена, HUD развёрнут"));
			});
			At(1.6f, [GI, WM, Dlg, Inv, Check]()
			{
				UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
				Dlg->OpenForNpcId(1);
				Dlg->RunCommandForCheck(30030, 0);
				Check(Ps && Ps->IsOpen() && Dlg->IsHiddenForChildWindow(), TEXT("30030 (2): магазин открыт, разговор спрятан"));
				Dlg->RunCommandForCheck(30030, 0);
				Check(Ps && Ps->IsOpen(), TEXT("30030 повторно при открытом 6000: гейт `!helper_getField2075` — окно не переоткрывается"));
				Dlg->Close();   // уход от NPC
				Check(Ps && !Ps->IsOpen() && Inv->IsWindowOpen() && !WM->ArePlayerWindowsHidden(),
					TEXT("уход от NPC при открытом 6000: магазин закрыт вместе с разговором (правило владельца 26.08), сумка возвращена"));
				Inv->CloseWindow();
			});

			// ── С-2: значок званий 50000 — закрыватель «закрыть всё» и слот 8 снимка ──
			At(2.0f, [GI, WM, Check]()
			{
				UAsdaTitleSubsystem* Ti = GI->GetSubsystem<UAsdaTitleSubsystem>();
				if (!Ti) { Check(false, TEXT("С-2: подсистемы званий нет")); return; }
				Ti->ShowNoticeIconFromSnapshot();   // зажечь значок тем же путём, что восстановление (`[880] = 1`)
				Check(Ti->IsNoticeIconShown(), TEXT("С-2: значок 50000 виден"));
				WM->CaptureOpenWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::NpcDialog, TEXT("самотест В5 С-2"));
				Check(WM->IsWindowSlotCaptured(8), TEXT("С-2: снимок — слот 8 (sub_923F91 «значок виден») поднят"));
				WM->CloseAllPlayerWindows();
				Check(!Ti->IsNoticeIconShown(), TEXT("С-2: «закрыть всё» погасило значок (sub_924419 = set_o370)"));
				WM->ReopenCapturedWindows(TEXT("самотест В5 С-2"));
				WM->RestoreAfterSceneExit();
				Check(Ti->IsNoticeIconShown() && !WM->IsWindowSlotCaptured(8), TEXT("С-2: восстановление вернуло значок (sub_934373 = set_o370_7470)"));
				Ti->HideNoticeIconForCloseAll();
			});

			// ── МЕЛК-14: плашка 221 после «смены карты» — один список строк, строки живут 10 с ──
			At(2.4f, [GI, Check]()
			{
				UAsdaChatSubsystem* Ch = GI->GetSubsystem<UAsdaChatSubsystem>();
				UAsdaSessionResetSubsystem* SR = GI->GetSubsystem<UAsdaSessionResetSubsystem>();
				if (!Ch || !SR) { Check(false, TEXT("плашка: нет подсистем чата/сброса")); return; }
				AsdaNotice::Plank(GI, EAsdaMsgCategory::System, TEXT("Самотест В5: строка до смены карты 1"), AsdaNoticeColor::Gold, 10000.f, 0);
				AsdaNotice::Plank(GI, EAsdaMsgCategory::System, TEXT("Самотест В5: строка до смены карты 2"), AsdaNoticeColor::Gold, 10000.f, 0);
				SR->Broadcast(EAsdaSessionResetReason::MapChanged);   // тот же сброс, что у «портала» самотеста 76000
				AsdaNotice::Plank(GI, EAsdaMsgCategory::System, TEXT("Самотест В5: строка после смены карты"), AsdaNoticeColor::Gold, 10000.f, 0);
				int32 Boxes = 0, Rows = 0;
				Ch->DebugWarWidgetCounts(Boxes, Rows);
				Check(Boxes == 1 && Rows == Ch->DebugWarLineCount() && Rows == 1,
					FString::Printf(TEXT("плашка после смены карты: списков строк %d (ждём 1), строк-виджетов %d = строк в данных %d (ждём 1) — наложения нет"),
						Boxes, Rows, Ch->DebugWarLineCount()));
			});
			// ⚠ Офлайн (-game без сервера) актора игрока в мире нет, и тик чата стоит на гейте «в мире» — поэтому гашение по TTL
			//   зовётся здесь ТОЙ ЖЕ функцией, что и тик (`TickWarExpiry`), а не своей копией.
			At(7.4f, [GI, Check]()
			{
				UAsdaChatSubsystem* Ch = GI->GetSubsystem<UAsdaChatSubsystem>();
				if (Ch) { Ch->DebugTickWarExpiry(); }
				Check(Ch && Ch->DebugWarLineCount() == 1, TEXT("плашка: через 5 с строка ещё жива (TTL 10 с)"));
			});
			At(12.9f, [GI, Check]()
			{
				UAsdaChatSubsystem* Ch = GI->GetSubsystem<UAsdaChatSubsystem>();
				int32 Boxes = 0, Rows = 0;
				if (Ch) { Ch->DebugTickWarExpiry(); Ch->DebugWarWidgetCounts(Boxes, Rows); }
				Check(Ch && Ch->DebugWarLineCount() == 0 && Rows == 0,
					FString::Printf(TEXT("плашка: через 10,5 с строк в данных %d, строк-виджетов %d (ждём 0/0) — TTL по монотонным часам"),
						Ch ? Ch->DebugWarLineCount() : -1, Rows));
			});

			// ── С-3: смерть при открытом элитном магазине 6000 — хвост эталона после 5045 ──
			At(13.3f, [GI, WM, Inv, Check]()
			{
				UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
				if (Ps) { Ps->OpenWindow(false); }
				Check(Ps && Ps->IsOpen() && !Inv->IsWindowOpen() && WM->ArePlayerWindowsHidden(), TEXT("смерть: перед ней открыт 6000 (снимок, «закрыть всё»)"));
				UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
				if (Net)
				{
					// Офлайн номер своего персонажа не пришёл (4003 нет) — делаем его «известным», иначе подписчики смерти
					//   (`IsSelfUserIdx`) примут её за чужую. Проход последний (после него — выход).
					Net->bSelfUserNumKnown = true;
					Net->bSelfDead = false;                                  // как `asda.Death.Test`
					Net->EnterSelfDeadState(TEXT("самотест В5 (С-3)"), false);   // тот же путь, что пакет 4053
				}
			});
			At(13.8f, [GI, WM, Inv, Check]()
			{
				UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();
				Check(Ps && !Ps->IsOpen(), TEXT("С-3: смерть закрыла элитный магазин 6000 (uiInventoryResetAndLeave, entity_char.c:16903)"));
				Check(Inv->IsWindowOpen(), TEXT("С-3: снимок вернул сумку (5045)"));
				Check(!WM->IsWindowSlotCaptured(0), TEXT("С-3: снимок исчерпан"));
			});
			At(14.2f, [Stat, bQuit, W, SelfScope]()
			{
				AsdaSelfTestKit::Summary(LogAsdaCraftMake, TEXT("САМОТЕСТ-В5"), Stat->Ok, Stat->Fail, TEXT(""), true);   // ★ 11.10.2026 (В3 S1): единый итог
				SelfScope->Restore();   // ★ В3 S1: персонаж снова жив, номер — как до прогона
				AsdaSelfTestKit::Quit(W, bQuit);
			});
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
// ★ САМОТЕСТ ВОЛНЫ В8 (финальная ревизия В7: С-1, С-2, С-3, МЕЛК-4) — путь игрока (-game), только офлайн.
//   Основной проход: С-2 «сумка → 76000 → рецепт → 77500 → Z → закрыть 76000 → (поздний 6601) → изготовление → сумка
//   вернулась»; С-1 сброс сессии (портал и разрыв) при открытом 77500 — HUD возвращён, окна снимка НЕ переоткрыты; С-3
//   защита отправителей взведена. Проход `death` (после смерти самотеста В5), оба значения ключа К-9
//   `asda.Craft.FixDeathRestoreOrder`: МЕЛК-4 — смерть при открытом 77500. Ключ 0 (эталон entity_char.c:16900 → 16910):
//   5045 первым, сумку он НЕ возвращает (открытие отвергнуто гейтом ui_core.c:46608 при видимом 77500, снимок обнулён),
//   сброс 77500 находит снимок пустым; ключ 1 (К-9): окна ремесла закрываются до 5045, сумку возвращает сброс 77500.
//   W8-2 — смерть при открытом 77000 без 77500: ключ 0 — окна снимка теряются так же; ключ 1 — возвращает 5045.
//   `delay=N` `shot` `prefix=` `death` `quit`.
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftW8SelfTest(
		TEXT("asda.Craft.SelfTestW8"),
		TEXT("Волна В8: С-2 снимок 77500 переживает Z/закрытие 76000/поздний 6601 — сумка возвращается после изготовления; С-1 сброс "
		     "сессии (портал/разрыв) при открытом 77500 возвращает HUD без окон снимка; С-3 офлайн-гейт. `death` — смерть при "
		     "открытом 77500 (МЕЛК-4) и при открытом 77000 без 77500 (W8-2), оба значения ключа asda.Craft.FixDeathRestoreOrder: "
		     "0 — эталон (5045 первым, окна снимка теряются), 1 — К-9 (окна ремесла до 5045, окна снимка возвращаются). "
		     "`delay=N` `shot` `prefix=` `quit`."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaCraftMakeWindowSubsystem* Mk = GI ? GI->GetSubsystem<UAsdaCraftMakeWindowSubsystem>() : nullptr;
			UAsdaCraftRecipeWindowSubsystem* Rw = GI ? GI->GetSubsystem<UAsdaCraftRecipeWindowSubsystem>() : nullptr;
			UAsdaCraftWindowSubsystem* Win = GI ? GI->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr;
			UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
			UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
			UAsdaSystemMenuSubsystem* Menu = GI ? GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
			UAsdaSessionResetSubsystem* SR = GI ? GI->GetSubsystem<UAsdaSessionResetSubsystem>() : nullptr;
			if (!Mk || !Rw || !Win || !Craft || !Inv || !WM || !Hot || !Menu || !SR) { return; }
			bool bQuit = false, bShot = false, bDeath = false;
			float Delay = 0.f;
			FString Prefix(TEXT("w8_"));
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
				if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
				if (A.Equals(TEXT("death"), ESearchCase::IgnoreCase)) { bDeath = true; }
				if (A.StartsWith(TEXT("prefix="))) { Prefix = A.Mid(7); }
			}
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
			// С-3: только офлайн — при живом соединении отказ, иначе окно защиты отправителей.
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Craft.SelfTestW8"), KitArgs)) { return; }
			struct FStat { int32 Ok = 0; int32 Fail = 0; };
			TSharedRef<FStat> Stat = MakeShared<FStat>();
			TWeakObjectPtr<UWorld> WGate(W);   // мир прогона (для отказа на отложенном старте)
			// ★ Р-2 (10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим таймером;
			//   при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
			// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Craft.SelfTestW8"), bQuit);   // общий флаг отказа всех шагов прогона
			auto GateRefused = [Gate, WGate]() { return !Gate->Pass(WGate.Get()); };   // true — прогон отменён; проверка один раз, первым сработавшим шагом
			auto At = [W, Mk, Delay, GateRefused](float T, TFunction<void()> Fn)
			{
				FTimerHandle H;
				W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Mk, [GateRefused, Fn = MoveTemp(Fn)]()
				{
					if (GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
					Fn();
				}), T + Delay, false);
			};
			auto Shot = [bShot, Prefix](const TCHAR* Name)
			{
				if (!bShot) { return; }
				const FString N = Prefix + Name;
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("САМОТЕСТ-В8: КАДР → %s.png"), *N);
				FScreenshotRequest::RequestScreenshot(N, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			};
			auto Check = [Stat](bool bOk, const FString& What)
			{
				if (bOk) { ++Stat->Ok; } else { ++Stat->Fail; }
				UE_LOG(LogAsdaCraftMake, Warning, TEXT("САМОТЕСТ-В8 %s %s"), bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
			};
			auto JHas = [Mk](const TCHAR* What) { for (const FString& L : Mk->GetJournal()) { if (L.Contains(What)) { return true; } } return false; };
			auto Put = [Inv](int32 Cell, int32 Code, int32 Qty) { Inv->DebugPutItem(2, Cell, Code, Qty); };
			auto ClearBag = [Inv]() { for (int32 C = 0; C < 60; ++C) { Inv->DebugPutItem(2, C, 0, 0); } };
			// Данные — как у самотеста 77500: рецепт 122 (ур. 6, 5 ступеней), материалы 72635×3 + 72732×1 + 20959×1.
			static const TArray<int32> kLearned = { 1, 76, 122 };
			auto PutMats122 = [Put]() { Put(40, 72635, 3); Put(41, 72732, 1); Put(42, 20959, 1); };
			auto Mats122Used = []() { return TArray<FMat>{ { 72635, 40, 0 }, { 72732, 41, 0 }, { 20959, 42, 0 } }; };
			// Подготовка: модель, материалы, сумка ОТКРЫТА (её снимет снимок 77000 и должен вернуть сброс 77500).
			auto Prepare = [Craft, Inv, WM, Mk, ClearBag, PutMats122]()
			{
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));
				Mk->ResetDialog(TEXT("самотест В8: старт")); Mk->DebugSetDryOpen(false);
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 30, 6, 100, 10));
				ClearBag(); PutMats122();
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
			};
			// Вход игрока: 76000 (путь клавиши Z/кнопки 452 — `tileInteractGateAndOpen`) → 77000 рецепта 122 → 77068 → 77500.
			auto EnterMake = [Win, Rw, Mk, Inv, WM, Check](const FString& Tag, bool bVia76000)
			{
				if (bVia76000 && !Win->IsOpen()) { Win->GateAndToggle(*Tag); }
				Check(Rw->OpenForRecipe(122, *Tag), FString::Printf(TEXT("%s: окно состава 77000 открыто рецептом 122"), *Tag));
				Rw->DebugPressButton(77068);
				Check(Mk->IsOpen() && Mk->GetRecipeId() == 122 && !Inv->IsWindowOpen() && WM->IsWindowSlotCaptured(0),
					FString::Printf(TEXT("%s: 77068 → 77500 открыто, сумка закрыта «закрыть всё», слот 0 снимка поднят"), *Tag));
			};

			if (bDeath)
			{
				// ── МЕЛК-4 + К-9: смерть при открытом 77500 — блок entity_char.c:16898-16911 одним проходом, в двух порядках ──
				//   Проход 0 — `asda.Craft.FixDeathRestoreOrder 0` (порядок эталона 5045 → 16910): 5045 восстанавливает снимок
				//   первым, открытие сумки отказывает (видно 77500, ui_core.c:46608), снимок обнулён — сброс 77500 находит его
				//   пустым. Проход 1 — ключ 1 (исправление К-9): окна ремесла до 5045, сумку возвращает сброс 77500.
				auto SetCVar = [](const TCHAR* Name, float V) { AsdaCVarSave::SetForTest(Name, V); };   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходное запоминается
				for (int32 Pass = 0; Pass < 2; ++Pass)
				{
					const float T0 = 0.1f + 1.4f * Pass;
					const bool bFix = (Pass == 1);
					const FString Tag = bFix ? TEXT("К-9 (1, исправлено)") : TEXT("К-9 (0, как оригинал)");
					At(T0, [Prepare, SetCVar, bFix]() { SetCVar(TEXT("asda.Craft.FixDeathRestoreOrder"), bFix ? 1.f : 0.f); Prepare(); });
					At(T0 + 0.3f, [EnterMake, Mk, Tag]() { Mk->ClearJournal(); EnterMake(Tag, /*bVia76000*/ false); });
					At(T0 + 0.5f, [GI, Check, WM, Tag]()
					{
						Check(WM->ArePlayerWindowsHidden(), FString::Printf(TEXT("%s: перед смертью 77500 открыто, HUD свёрнут"), *Tag));
						if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
						{
							Net->bSelfUserNumKnown = true;                           // офлайн 4003 нет (как самотест В5)
							Net->bSelfDead = false;
							Net->EnterSelfDeadState(TEXT("самотест В8 (МЕЛК-4)"), false);   // тот же путь, что пакет 4053
						}
					});
					At(T0 + 0.9f, [Mk, Inv, WM, Hot, Check, JHas, Tag, bFix, SetCVar]()
					{
						Check(!Mk->IsOpen() && JHas(TEXT("entity_char.c:16910")), FString::Printf(TEXT("%s: смерть сбросила 77500 из блока смерти (itemQtyDlgReset)"), *Tag));
						Check(!WM->IsWindowSlotCaptured(0) && !WM->ArePlayerWindowsHidden() && Hot->IsPanelActive(), FString::Printf(TEXT("%s: снимок исчерпан, HUD возвращён"), *Tag));
						if (bFix)
						{
							Check(JHas(TEXT("окон снимка переоткрыто 1")) && Inv->IsWindowOpen(), FString::Printf(TEXT("%s: сброс 77500 ДО 5045 вернул сумку"), *Tag));
						}
						else
						{
							// Порядок эталона детерминирован: 5045 первым (сумка отвергнута гейтом «ремесло открыто»), сброс 77500 — пустой снимок.
							Check(JHas(TEXT("окон снимка переоткрыто 0")) && !Inv->IsWindowOpen(), FString::Printf(TEXT("%s: порядок эталона 5045 → 16910 — сумка потеряна, как у оригинала"), *Tag));
						}
						for (const FString& L : Mk->GetJournal()) { UE_LOG(LogAsdaCraftMake, Warning, TEXT("  журнал 77500: %s"), *L); }
						SetCVar(TEXT("asda.Craft.FixDeathRestoreOrder"), 1.f);
					});
				}
				// ── W8-2 (реестр волны В8): смерть при открытом 77000 БЕЗ перехода в 77500 — путь клавиши Z ──
				//   Сумка и окно персонажа открыты → 76000 (`tileInteractGateAndOpen`) → 77000 рецепта 122 (снимок владельца
				//   Craft: слоты 0 и 1) → игрок закрывает сумку и окно персонажа (флаги живут, ui_core.c:60967 снимает их только
				//   восстановление) → смерть. Эталон (ключ 0): 5045 первым — сумка отвергнута гейтом ui_core.c:46608, окно
				//   персонажа — гейтом `uiWindow_OpenIfAllowed` ui_core.c:74270 (76000 видно), флаги обнулены; затем 16908
				//   закрывает 76000 и 77000 — не возвращается ни одно окно. Ключ 1: окна ремесла до 5045, снимок отдаётся 5045 (К-7 его не снимает) —
				//   возвращаются ОБА окна. До исправления W8-2 при ключе 1 К-7 снимал флаги владельца Craft при закрытии 77000
				//   из блока смерти, и 5045 находил снимок пустым — не возвращалось НИЧЕГО.
				for (int32 Pass = 0; Pass < 2; ++Pass)
				{
					const float T0 = 3.0f + 1.4f * Pass;
					const bool bFix = (Pass == 1);
					const FString Tag = bFix ? TEXT("W8-2 (1, исправлено)") : TEXT("W8-2 (0, как оригинал)");
					At(T0, [GI, Prepare, SetCVar, bFix, WM]()
					{
						SetCVar(TEXT("asda.Craft.FixDeathRestoreOrder"), bFix ? 1.f : 0.f);
						// Воскрешение (путь 4055/4056): иначе гейт «персонаж занят» (`objStateAllowsRender`) не даст открыть 76000.
						//   Тот же делегат, что шлёт приёмник 4055/4056 (`OnCharRevived`): мир снимает позу смерти, меню 25000 закрывается.
						if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>()) { Net->bSelfDead = false; Net->OnCharRevived.Broadcast(); }
						Prepare();                                                   // модель, материалы, сумка открыта
						WM->ClearWindowSnapshot(TEXT("самотест W8-2: старт"));        // снимок — только от 77000 этого прохода
						if (UAsdaCharaInfoSubsystem* Ci = GI->GetSubsystem<UAsdaCharaInfoSubsystem>()) { if (!Ci->IsWindowOpen()) { Ci->OpenWindow(); } }
					});
					At(T0 + 0.3f, [GI, Win, Rw, Mk, Inv, WM, Check, Tag]()
					{
						Mk->ClearJournal(); Win->ClearJournal();
						UAsdaCharaInfoSubsystem* Ci = GI->GetSubsystem<UAsdaCharaInfoSubsystem>();
						Check(Inv->IsWindowOpen() && Ci && Ci->IsWindowOpen(), FString::Printf(TEXT("%s: сумка и окно персонажа открыты"), *Tag));
						if (!Win->IsOpen()) { Win->GateAndToggle(*Tag); }            // путь клавиши Z
						Check(Win->IsOpen(), FString::Printf(TEXT("%s: 76000 открыто (tileInteractGateAndOpen)"), *Tag));
						Check(Rw->OpenForRecipe(122, *Tag) && Rw->IsOpen(), FString::Printf(TEXT("%s: окно состава 77000 открыто рецептом 122"), *Tag));
						const uint8 kCraftBit = (uint8)UAsdaWindowSubsystem::EWindowSnapshotOwner::Craft;
						Check((WM->GetWindowSlotOwners(0) & kCraftBit) && (WM->GetWindowSlotOwners(1) & kCraftBit),
							FString::Printf(TEXT("%s: снимок 77000 поднял слоты 0 (сумка) и 1 (персонаж), владелец Craft"), *Tag));
						Inv->CloseWindow();                                          // игрок закрыл сумку
						if (Ci) { Ci->CloseWindow(); }                               // и окно персонажа
						Check(!Inv->IsWindowOpen() && Ci && !Ci->IsWindowOpen() && WM->IsWindowSlotCaptured(0) && WM->IsWindowSlotCaptured(1),
							FString::Printf(TEXT("%s: сумка и окно персонажа закрыты игроком, флаги снимка живы"), *Tag));
					});
					At(T0 + 0.5f, [GI, Win, Rw, Mk, Check, Tag]()
					{
						Check(Win->IsOpen() && Rw->IsOpen() && !Mk->IsOpen(), FString::Printf(TEXT("%s: перед смертью открыты 76000 и 77000, 77500 нет"), *Tag));
						if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
						{
							Net->bSelfUserNumKnown = true;                           // офлайн 4003 нет (как самотест В5)
							Net->bSelfDead = false;
							Net->EnterSelfDeadState(TEXT("самотест В8 (W8-2)"), false);   // тот же путь, что пакет 4053
						}
					});
					At(T0 + 0.9f, [GI, Win, Rw, Inv, WM, Check, Tag, bFix, SetCVar]()
					{
						bool bWinDeath = false;
						for (const FString& L : Win->GetJournal()) { if (L.Contains(TEXT("смерть — окно закрыто"))) { bWinDeath = true; } }
						Check(!Win->IsOpen() && !Rw->IsOpen() && bWinDeath, FString::Printf(TEXT("%s: блок смерти закрыл 76000 и 77000 (entity_char.c:16908)"), *Tag));
						Check(!WM->IsWindowSlotCaptured(0) && !WM->IsWindowSlotCaptured(1), FString::Printf(TEXT("%s: снимок исчерпан"), *Tag));
						UAsdaCharaInfoSubsystem* Ci = GI->GetSubsystem<UAsdaCharaInfoSubsystem>();
						const bool bCi = Ci && Ci->IsWindowOpen();
						if (bFix)
						{
							Check(Inv->IsWindowOpen() && bCi, FString::Printf(TEXT("%s: 5045 после закрытия окон ремесла вернул СУМКУ и окно персонажа"), *Tag));
						}
						else
						{
							Check(!Inv->IsWindowOpen() && !bCi, FString::Printf(TEXT("%s: порядок эталона — сумка (ui_core.c:46608) и окно персонажа (:74270) отвергнуты гейтом «ремесло открыто», снимок потерян"), *Tag));
						}
						for (const FString& L : Win->GetJournal()) { UE_LOG(LogAsdaCraftMake, Warning, TEXT("  журнал 76000: %s"), *L); }
						SetCVar(TEXT("asda.Craft.FixDeathRestoreOrder"), 1.f);
						if (Ci) { Ci->CloseWindow(); }
					});
				}
				At(6.2f, [Stat, bQuit, W]()
				{
					// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ Craft.FixDeathRestoreOrder — к исходному значению И приоритету SetBy
					//   (прежде — константа 1 с SetByCode).
					AsdaCVarSave::RestoreAll(TEXT("asda.Craft.SelfTestW8"));
					AsdaSelfTestKit::Summary(LogAsdaCraftMake, TEXT("САМОТЕСТ-В8-СМЕРТЬ"), Stat->Ok, Stat->Fail, TEXT(""), true);   // ★ 11.10.2026 (В3 S1): единый итог
					AsdaSelfTestKit::Quit(W, bQuit);
				});
				return;
			}

			// ── С-2: сумка → 76000 → рецепт → 77500 → Z → закрыть 76000 → поздний 6601 → изготовление → сумка вернулась ──
			At(0.1f, [Prepare]() { Prepare(); });
			At(0.4f, [EnterMake, Mk]() { Mk->ClearJournal(); EnterMake(TEXT("С-2"), /*bVia76000*/ true); });
			At(0.7f, [Win, Mk, WM, Check]()
			{
				Win->GateAndToggle(TEXT("самотест В8: Z поверх 77500"));   // путь клавиши Z (PlayerController → GateAndToggle)
				Check(Win->IsOpen() && Mk->IsOpen(), TEXT("С-2: Z открыл 76000 поверх 77500 (гейта нет и у эталона)"));
				Win->DebugPressButton(76097);                                // «Закрыть» 76000 — путь игрока
				Check(!Win->IsOpen(), TEXT("С-2: 76000 закрыто кнопкой 76097"));
				Check(WM->IsWindowSlotCaptured(0), TEXT("С-2: снимок 77500 (слот 0 — сумка) пережил закрытие 76000 (К-7 не снимает его при открытом 77500)"));
			});
			At(0.9f, [Craft]()
			{
				// Поздний 6601 код 1 (ответ на давнее «Удалить» рецепта 76): эталон безусловно зовёт `uiWindowHideAndCleanup`.
				TArray<uint8> B; B.SetNumZeroed(3); B[0] = 1; AsdaBytes::WrI16(&B[1], 76);
				Craft->DebugInject(6601, B);
			});
			At(1.1f, [Craft, WM, Mk, Check]()
			{
				Check(!Craft->IsLearned(76), TEXT("С-2: поздний 6601 код 1 применён (рецепт 76 забыт)"));
				Check(Mk->IsOpen() && WM->IsWindowSlotCaptured(0), TEXT("С-2: поздний 6601 (закрытое 76000) снимок 77500 не снял"));
			});
			At(1.3f, [Mk, Craft, Check]()
			{
				Craft->DebugCaptured.Reset();
				Check(Craft->DebugCaptureOnce(6602), TEXT("С-2: перехват 6602 поставлен"));
				Mk->DebugPressButton(77533);
				Check(Mk->IsAwaitingReply(), TEXT("С-2: «Создать» — 6602 ушёл в перехват, ждём 6603"));
			});
			At(1.5f, [Craft, Mats122Used]() { Craft->DebugInject(6603, Build6603(1, 0, 0, 0, 0, 0, Mats122Used())); });
			At(5.4f, [Mk, Check]()
			{
				Check(!Mk->IsAnimating() && Mk->DebugControlsEnabled(), TEXT("С-2: изготовление (1 шт.) закончено, кнопки включены"));
			});
			At(5.6f, [Mk, Inv, WM, Check, Shot]()
			{
				Mk->DebugPressButton(77534);                                 // «Закрыть» — `itemQtyDlgReset`
				Check(!Mk->IsOpen(), TEXT("С-2: 77500 закрыто кнопкой 77534"));
				Check(Inv->IsWindowOpen(), TEXT("С-2: СУМКА ВЕРНУЛАСЬ после изготовления (как эталон)"));
				Check(!WM->IsWindowSlotCaptured(0) && !WM->ArePlayerWindowsHidden(), TEXT("С-2: снимок исчерпан, HUD развёрнут"));
				Shot(TEXT("c2_bag_back"));
			});
			// ── С-1: сброс сессии при открытом 77500 — портал (MapChanged), затем разрыв (Disconnected) ──
			const EAsdaSessionResetReason kReasons[2] = { EAsdaSessionResetReason::MapChanged, EAsdaSessionResetReason::Disconnected };
			for (int32 I = 0; I < 2; ++I)
			{
				const float T0 = 6.0f + 1.2f * I;
				const EAsdaSessionResetReason Reason = kReasons[I];
				const FString Tag = FString::Printf(TEXT("С-1 (%s)"), AsdaSessionResetReasonName(Reason));
				At(T0, [Prepare]() { Prepare(); });
				At(T0 + 0.3f, [EnterMake, Mk, Hot, Menu, WM, Check, Tag]()
				{
					Mk->ClearJournal();
					EnterMake(Tag, /*bVia76000*/ true);
					Check(WM->ArePlayerWindowsHidden() && !Hot->IsPanelActive() && !Menu->IsExpanded(),
						FString::Printf(TEXT("%s: до сброса HUD заблокирован (хотбар заблокирован, трей свёрнут)"), *Tag));
				});
				At(T0 + 0.6f, [SR, Mk, Inv, WM, Hot, Menu, Check, JHas, Tag, Reason]()
				{
					SR->Broadcast(Reason);
					Check(!Mk->IsOpen(), FString::Printf(TEXT("%s: 77500 закрыто сбросом"), *Tag));
					Check(Hot->IsPanelActive(), FString::Printf(TEXT("%s: хотбар РАЗБЛОКИРОВАН"), *Tag));
					Check(Menu->IsExpanded() && !WM->ArePlayerWindowsHidden(), FString::Printf(TEXT("%s: трей развёрнут, признак «окна убраны» снят"), *Tag));
					Check(!Inv->IsWindowOpen(), FString::Printf(TEXT("%s: окна снимка НЕ переоткрыты посреди сброса"), *Tag));
					Check(JHas(TEXT("HUD возвращён, окна снимка не переоткрывались")), FString::Printf(TEXT("%s: журнал 77500 — HUD возвращён"), *Tag));
				});
			}
			// ── С-3: офлайн-гейт самотеста ──
			At(8.6f, [W, Craft, Check]()
			{
				Check(Craft->IsSelfTestSendGuardActive(), TEXT("С-3: окно защиты отправителей взведено (соединение живое — запрос ремесла не уйдёт)"));
				Check(!UAsdaCraftSubsystem::RefuseDebugWhenOnline(W, TEXT("проверка С-3"), 0.f), TEXT("С-3: офлайн — самотест разрешён (при живом соединении — отказ)"));
			});
			At(8.9f, [Stat, bQuit, W]()
			{
				AsdaSelfTestKit::Summary(LogAsdaCraftMake, TEXT("САМОТЕСТ-В8"), Stat->Ok, Stat->Fail, TEXT(""), true);   // ★ 11.10.2026 (В3 S1): единый итог
				AsdaSelfTestKit::Quit(W, bQuit);
			});
		}));
}
#endif // ASDA_DEV_CONSOLE
