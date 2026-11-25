// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemUpgradeSubsystem_SelfTest.cpp — ★ R3-c 12.11.2026 (разрез AsdaItemUpgradeSubsystem.cpp, дословный перенос): самотест окна улучшения.
//   Общее для частей — в AsdaItemUpgradeSubsystem_Private.h; шапка и реверс — в AsdaItemUpgradeSubsystem.cpp.
#include "UI/AsdaItemUpgradeSubsystem.h"


#include "AsdaItemUpgradeSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaItemUpgradeSubsystem*.cpp

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdUpgradeSelfTest(
		TEXT("asda.Upgrade.SelfTest"),
		TEXT("Окно 20000: положить тестовые предметы, открыть, выбрать цель/свиток/помощника, старт, успех +6 (попап), "
		     "провал; дампы контролов/состояния/геометрии. `shot` — кадры, `quit` — выход."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaItemUpgradeSubsystem* S = UpgradeFromWorld(W);
			UAsdaInventorySubsystem* Inv = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			if (!S || !Inv || !W) { return; }
			bool bQuit = false, bShot = false;
			// ★ С-Р1 (26.09.2026): ТОЛЬКО ОФЛАЙН. Шаги жмут боевую иконку старта → `SendAttempt` → 6504 — при живом
			//   соединении это настоящая заточка предмета из ячейки 25 (может сгореть). Окно защиты 6504 — весь проход (≈24 с) + запас.
			static const uint16 kGuardOps[] = { 6504 };
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Upgrade.SelfTest"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
				if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
			}
			// ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте —
			//   общий флаг шагов `At` (проверку делает первый сработавший шаг, при отказе пропускаются все).
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Upgrade.SelfTest"), bQuit);
			auto At = [W, S, Gate](float T, TFunction<void()> Fn)
			{
				FTimerHandle H;
				W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(S,
					[Gate, S, Fn = MoveTemp(Fn)]() { if (!Gate->Pass(S)) { return; } Fn(); }), T, false);
			};
			auto Shot = [bShot](const TCHAR* Name)
			{
				if (!bShot) { return; }
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: КАДР → %s.png"), Name);
				FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			};
			auto CloseMsg = [W]()
			{
				if (UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX"))); }
			};
			auto Reselect = [S]()
			{
				S->OnGridCellClicked(0);
				S->OnScrollCellClicked(0);
				S->OnHelper97CellClicked(0);
				S->OnHelper98CellClicked(0);
			};
			auto Sim = [S](int32 Code, int32 Lvl)
			{
				TArray<uint8> B;
				if (S->DebugBuildResult(Code, Lvl, B)) { S->DebugInjectResult(B); }
				else { UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: нет цели/свитка — ответ %d не синтезирован"), Code); }
			};
			// ★ Волна 5: сверка журнала «анимация/звук → момент». Ожидаемые события ищутся ПО ПОРЯДКУ
			//   (подстрока записи журнала); пары {от, до, мин мс, макс мс} — моменты между событиями.
			auto VerifyJournal = [S](const TCHAR* Step, const TArray<FString>& Expect, const TArray<TTuple<int32, int32, double, double>>& Gaps)
			{
				const TArray<FString>& J = S->GetFxJournal();
				for (const FString& L : J) { UE_LOG(LogAsdaUpgrade, Warning, TEXT("  журнал [%s]: %s"), Step, *L); }
				TArray<double> At; At.Init(-1.0, Expect.Num());
				int32 From = 0;
				bool bOrder = true;
				for (int32 E = 0; E < Expect.Num(); ++E)
				{
					int32 Found = -1;
					for (int32 I = From; I < J.Num(); ++I) { if (J[I].Contains(Expect[E])) { Found = I; break; } }
					if (Found < 0) { bOrder = false; UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ-В5 ✘ [%s] нет «%s» после позиции %d"), Step, *Expect[E], From); continue; }
					At[E] = FCString::Atod(*J[Found]);
					From = Found + 1;
				}
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ-В5 %s [%s] порядок событий (%d шт.)"), bOrder ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), Step, Expect.Num());
				for (const TTuple<int32, int32, double, double>& G : Gaps)
				{
					const int32 A = G.Get<0>(), B = G.Get<1>();
					const bool bHave = At.IsValidIndex(A) && At.IsValidIndex(B) && At[A] >= 0.0 && At[B] >= 0.0;
					const double D = bHave ? At[B] - At[A] : -1.0;
					const bool bOk = bHave && D >= G.Get<2>() && D <= G.Get<3>();
					UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ-В5 %s [%s] «%s» → «%s»: %.0f мс (допуск %.0f…%.0f)"),
						bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), Step, Expect.IsValidIndex(A) ? *Expect[A] : TEXT("?"),
						Expect.IsValidIndex(B) ? *Expect[B] : TEXT("?"), D, G.Get<2>(), G.Get<3>());
				}
				S->ClearFxJournal();
			};
			// Шаг 1: тестовые предметы (сумка 25…28, расходники 55) и открытие.
			At(2.0f, [S, Inv]()
			{
				auto Put = [Inv](int32 Cont, int32 Cell, int32 Code, int32 Amount, int32 Lvl, int32 Used, int32 Max, int32 Pct)
				{
					Inv->DebugPutItem(Cont, Cell, Code, Amount);
					if (FAsdaItemInstance* It = Inv->GetCellMutable(Cont, Cell))
					{
						if (It->Raw.Num() >= 57)
						{
							It->Raw[24] = (uint8)Lvl; It->Raw[25] = (uint8)Used; It->Raw[26] = (uint8)Max;
							AsdaBytes::WrI16(It->Raw.GetData() + 27, Pct);
							It->SyncFromRaw();
						}
					}
				};
				Put(1, 25, 22818, 1, 5, 4, 5, 40);    // цель: ступень 5, попыток 4/5, 40 %
				Put(1, 26, 22819, 1, 0, 0, 0, 0);     // вещь с пределом 0 — затемнитель Д-13 и гейт 50011 (N-14)
				Put(1, 27, 252, 2, 0, 0, 0, 0);       // помощник subType 97 (+5 % к эффекту)
				Put(1, 28, 255, 1, 0, 0, 0, 0);       // помощник subType 98
				Put(2, 55, 20936, 5, 0, 0, 0, 0);     // свиток вида 74 (оружие, любой грейд)
				Put(1, 29, 75538, 1, 0, 0, 7, 0);     // ★ Д-18: «Подарочная корзина колядок» (тип 11/14), предел 7 — в сетку не должна попасть
				Put(1, 30, 73347, 1, 0, 0, 0, 0);     // ★ Д-18: тыквенная голова — у клиента шлем, у сервера не снаряжение (ini) — тоже не должна
				S->ToggleWindow();
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: открытие → %s\n%s"), S->IsOpen() ? TEXT("ОК") : TEXT("ОТКАЗ"), *S->DumpControls());
			});
			// ★ Д-18: не-снаряжение (корзина 75538) в сетке только при ключе 0 (как оригинал).
			At(2.6f, [S]()
			{
				const bool bKey = CVarGearOnlyGrid.GetValueOnGameThread() != 0;
				const bool bListed = S->DumpState().Contains(TEXT("код=75538 "));
				if (bListed != bKey) { UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ-Д18 ✔ корзина 75538 в сетке: %s (ключ %d)"), bListed ? TEXT("да") : TEXT("нет"), bKey ? 1 : 0); }
				else                 { UE_LOG(LogAsdaUpgrade, Error, TEXT("САМОТЕСТ-Д18 FAIL корзина 75538 в сетке: %s (ключ %d)"), bListed ? TEXT("да") : TEXT("нет"), bKey ? 1 : 0); }
				// Тыквенная голова 73347: клиентский тип — шлем, отсекает список ini «сервер — не снаряжение».
				const bool bPumpkin = S->DumpState().Contains(TEXT("код=73347 "));
				if (bPumpkin != bKey) { UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ-Д18 ✔ тыквенная голова 73347 в сетке: %s (ключ %d)"), bPumpkin ? TEXT("да") : TEXT("нет"), bKey ? 1 : 0); }
				else                  { UE_LOG(LogAsdaUpgrade, Error, TEXT("САМОТЕСТ-Д18 FAIL тыквенная голова 73347 в сетке: %s (ключ %d)"), bPumpkin ? TEXT("да") : TEXT("нет"), bKey ? 1 : 0); }
			});
			At(3.0f, [W]() { GEngine->Exec(W, TEXT("asda.UI.TreeGeom upg_open WBP_Item_Upgrade_Win")); });
			At(3.2f, [Shot]() { Shot(TEXT("asda_upgrade_open")); });
			// Шаг 2: гейт N-14 — вещь с пределом 0 (вторая позиция сетки).
			At(4.0f, [S]()
			{
				S->OnGridCellClicked(1);
				S->OnScrollCellClicked(0);
				S->OnUpgradeClicked();     // ждём ONEBUT 50011
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: гейт N-14\n%s"), *S->DumpState());
			});
			At(4.6f, CloseMsg);
			// ★ Д-17: подсказка 20013 у вещи с пределом 0 — 50011 (обновляется после закрытия модалки 50011), а не «достиг предела» (110117/110118).
			At(4.9f, [S, W]()
			{
				const bool bKey = CVarHonestNoUpgradeGuide.GetValueOnGameThread() != 0;
				const UGameInstance* GI = W->GetGameInstance();
				const FString State = S->DumpState();   // первая строка — «20013 гайд: «сырой текст»»
				auto Shows = [&State, GI](int32 Id) { return State.Contains(TEXT("20013 гайд: «") + AsdaNotice::LocRaw(GI, Id) + TEXT("»")); };
				const bool bOk = bKey ? Shows(AsdaItemUpgradeLoc::kNotUpgradable)
				                      : (Shows(AsdaItemUpgradeLoc::kGuideMaxArmor) || Shows(AsdaItemUpgradeLoc::kGuideMaxWeapon));
				if (bOk) { UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ-Д17 ✔ подсказка при пределе 0 (ключ %d)"), bKey ? 1 : 0); }
				else     { UE_LOG(LogAsdaUpgrade, Error, TEXT("САМОТЕСТ-Д17 FAIL подсказка при пределе 0 (ключ %d)\n%s"), bKey ? 1 : 0, *State.Left(300)); }
			});
			// Шаг 3: цель 5/5 (на пределе), свиток, оба помощника.
			At(5.0f, [S, Reselect]() { Reselect(); UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: выбор\n%s"), *S->DumpState()); });
			At(5.6f, [W]() { GEngine->Exec(W, TEXT("asda.UI.TreeGeom upg_select WBP_Item_Upgrade_Win")); });
			At(5.8f, [Shot]() { Shot(TEXT("asda_upgrade_select")); });
			At(6.4f, [S]() { S->ClearFxJournal(); });
			// ★ В5: кнопка 20190 — путём игрока (делегат кнопки → реле): звук id 5 ПЕРЕД валидатором.
			At(6.5f, [S]() { S->DebugPressButton(20190); UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: 20190 на пределе → диалог 72")); });
			At(7.0f, [Shot]() { Shot(TEXT("asda_upgrade_confirm72")); });
			// Шаг 4: «Да» диалога 72 (боевой путь реле: CloseConfirm(true) → колбэк окна).
			At(8.0f, [S, W]()
			{
				if (UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseConfirm(true); }
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: «Да» → старт\n%s"), *S->DumpState());
			});
			At(8.4f, [Shot]() { Shot(TEXT("asda_upgrade_effect1")); });
			// Шаг 5: 20070 доиграла (1.2 с) → клик по иконке → 6504 (без сервера не уйдёт — замок НЕ ставится).
			At(10.0f, [S, VerifyJournal]()
			{
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: после 20070\n%s"), *S->DumpState());
				S->DebugHoverIcon();   // msg 8 по взведённой иконке → «S_e_button09»
				S->OnIconClicked();
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: клик по 20003\n%s"), *S->DumpState());
				// ★ В5: порядок и моменты старта попытки (§6.1–6.3): клик 20190 → звук id 5; «Да» → 20070 + звук
				//   «Itemupgrade1»; 20070 доигрывает за 1200 мс → 20071 + 20072; наведение → «button09»; клик → «button10».
				VerifyJournal(TEXT("старт попытки"),
					{ TEXT("snd id5 (кнопка 20190)"), TEXT("fx+ 20070"), TEXT("snd S_e_Itemupgrade1"), TEXT("fx0x18 20070"),
					  TEXT("fx+ 20071"), TEXT("fx+ 20072"), TEXT("snd S_e_button09"), TEXT("snd S_e_button10") },
					{ MakeTuple(1, 2, 0.0, 50.0), MakeTuple(1, 3, 1150.0, 1600.0), MakeTuple(3, 4, 0.0, 50.0) });
			});
			// Шаг 6: замок N-2 — «6504 ушёл»; новая попытка отклоняется; переоткрытие не снимает; таймаут снимает.
			At(10.5f, [S]()
			{
				S->DebugMarkSent();
				S->OnUpgradeClicked();
				S->ToggleWindow(); S->ToggleWindow();
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: N-2 после переоткрытия\n%s"), *S->DumpState());
				AsdaCVarSave::SetForTest(TEXT("asda.Upgrade.ReplyTimeoutSec"), 1.f);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходное (20) запоминается
			});
			At(12.5f, [S]()
			{
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: N-2 после таймаута\n%s"), *S->DumpState());
				// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): 10 с — значение НА ВРЕМЯ остатка прогона (шаги 7–9 как раньше); прежде это был «возврат»
				//   константой 10 при умолчании 20 с приоритетом «из консоли» — ключ оставался испорченным до перезапуска.
				//   Исходное значение и SetBy возвращает `RestoreAll` финального шага (23.8 с).
				AsdaCVarSave::SetForTest(TEXT("asda.Upgrade.ReplyTimeoutSec"), 10.f);
			});
			// Шаг 7: ответ «успех, ступень 6» → 20004 → текст + попап 19100.
			At(13.0f, [S, Reselect, Sim]() { Reselect(); S->ClearFxJournal(); Sim(1, 6); });
			At(13.4f, [Shot]() { Shot(TEXT("asda_upgrade_success_fx")); });
			At(15.0f, [S, W, CloseMsg, VerifyJournal]()
			{
				// ★ В5: успех — 20004 + звук «Itemupgrade2» в одном кадре приёма, 20004 доигрывает за 900 мс.
				VerifyJournal(TEXT("успех"), { TEXT("fx+ 20004"), TEXT("snd S_e_Itemupgrade2"), TEXT("fx0x18 20004") },
					{ MakeTuple(0, 1, 0.0, 50.0), MakeTuple(0, 2, 850.0, 1300.0) });
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: после успеха +6\n%s"), *S->DumpState());
				CloseMsg();
				GEngine->Exec(W, TEXT("asda.UI.TreeGeom upg_popup WBP_Get_UpgradeOtion"));
			});
			At(15.4f, [Shot]() { Shot(TEXT("asda_upgrade_popup")); });
			// Шаг 8: Д-12 — успех на ступени 12: текст и попап (в оригинале — тишина).
			At(16.0f, [S, Sim]() { S->OnPopupOkClicked(); S->OnScrollCellClicked(0); Sim(1, 12); });
			At(18.0f, [S, Shot]() { UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: после успеха +12 (Д-12)\n%s"), *S->DumpState()); Shot(TEXT("asda_upgrade_success12")); });
			// Шаг 9: провал с уничтожением (код 8, запись цели пустая) → 20005, 20073, 50005 и 110119.
			At(19.0f, [S, Sim, CloseMsg]() { CloseMsg(); S->OnPopupOkClicked(); S->OnScrollCellClicked(0); S->ClearFxJournal(); Sim(8, -1); });
			// ★ В5: провал БЕЗ уничтожения — анимация 20005 доигрывает сама (600 мс прогона; конец прогона
			//   `UAsdaAnimatedPlane` — кадр > последнего, то есть ≈ 600 × 8/7 мс + кадр отрисовки).
			At(22.0f, [S, Inv, CloseMsg]()
			{
				CloseMsg();
				Inv->DebugPutItem(1, 25, 22818, 1);
				if (FAsdaItemInstance* It = Inv->GetCellMutable(1, 25))
				{
					if (It->Raw.Num() >= 57) { It->Raw[24] = 3; It->Raw[25] = 1; It->Raw[26] = 5; It->SyncFromRaw(); }
				}
				Inv->DebugPutItem(2, 55, 20936, 5);
				S->OnGridCellClicked(0);   // сетка пересобирается в Tick — выбор ниже, после пересборки
			});
			At(22.3f, [S, Reselect, Sim]() { Reselect(); S->ClearFxJournal(); Sim(8, 3); });
			At(23.6f, [VerifyJournal, CloseMsg]()
			{
				VerifyJournal(TEXT("провал"), { TEXT("fx+ 20005"), TEXT("snd S_e_Itemupgrade3"), TEXT("fx0x18 20005") },
					{ MakeTuple(0, 1, 0.0, 50.0), MakeTuple(0, 2, 550.0, 1000.0) });
				CloseMsg();
			});
			At(21.0f, [S, Shot, VerifyJournal]()
			{
				// ★ В5: провал с уничтожением — 20005 + звук «Itemupgrade3» в одном кадре; цель пропала из сетки, тик
				//   проверки выбора срезает анимацию раньше конца — буфер выводится сразу (ResultNeverLost).
				VerifyJournal(TEXT("провал с уничтожением"), { TEXT("fx+ 20005"), TEXT("snd S_e_Itemupgrade3"), TEXT("fx0x18 20005") },
					{ MakeTuple(0, 1, 0.0, 50.0) });
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: после провала\n%s"), *S->DumpState());
				Shot(TEXT("asda_upgrade_fail"));
			});
			// ★ В5 §6.4: связь «ступень заточки → свечение оружия» — общая сага свечения. Имя .jeb по ступени
			//   (эталон: < 5 — нет, 5 → _01, 6 → _02, ≥ 7 → _03) и боевой путь смены экипировки: ступень
			//   надетого оружия доходит до источника свечения своего персонажа.
			At(21.6f, [W]()
			{
				UGameInstance* GI = W->GetGameInstance();
				UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
				UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
				const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
				if (!WS || !Net || !IT) { UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ-В5 ✘ свечение: нет подсистем")); return; }
				// Первое оружие таблицы, у которого по данным есть свечение на +5 (не у всех подтипов/грейдов оно есть).
				int32 Code = 0;
				for (const FAsdaItemRecord& R : IT->GetAllItems())
				{
					if (R.ItemType == 9 && !WS->BuildGlowJebName(R.ItemCode, 5, false).IsEmpty()) { Code = R.ItemCode; break; }
				}
				const FString J4 = WS->BuildGlowJebName(Code, 4, false), J5 = WS->BuildGlowJebName(Code, 5, false);
				const FString J6 = WS->BuildGlowJebName(Code, 6, false), J7 = WS->BuildGlowJebName(Code, 7, false);
				const bool bNames = J4.IsEmpty() && J5.EndsWith(TEXT("_01")) && J6.EndsWith(TEXT("_02")) && J7.EndsWith(TEXT("_03"));
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ-В5 %s свечение предмета %d по ступени: +4=«%s» +5=«%s» +6=«%s» +7=«%s»"),
					bNames ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), Code, *J4, *J5, *J6, *J7);
				const int32 PrevId = Net->SelfWeaponItemId, PrevGr = Net->SelfWeaponGrade;
				WS->ApplySelfEquipPart(9, Code, 6);
				const bool bPath = (Net->SelfWeaponItemId == Code && Net->SelfWeaponGrade == 6);
				UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ-В5 %s смена оружия (слот 9, ступень 6) → источник свечения: предмет %d ступень %d"),
					bPath ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), Net->SelfWeaponItemId, Net->SelfWeaponGrade);
				WS->ApplySelfEquipPart(9, PrevId, PrevGr);   // вернуть как было
			});
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): финальный шаг — ключ asda.Upgrade.ReplyTimeoutSec к исходному значению (умолчание 20)
			//   И приоритету SetBy; строка `CVAR_RESTORE [asda.Upgrade.SelfTest] …` в лог. Раньше выхода (24.0 с).
			At(23.8f, []() { AsdaCVarSave::RestoreAll(TEXT("asda.Upgrade.SelfTest")); });
			if (bQuit)
			{
				At(24.0f, []()
				{
					UE_LOG(LogAsdaUpgrade, Warning, TEXT("САМОТЕСТ: завершён"));
					if (GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
				});
			}
		}));
}
#endif // ASDA_DEV_CONSOLE
