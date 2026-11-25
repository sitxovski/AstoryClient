// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCraftMakeWindowSubsystem_SelfTestStop.cpp — ★ R3-c 12.11.2026 (разрез AsdaCraftMakeWindowSubsystem.cpp, дословный перенос): самотест остановки создания при движении.
//   Общее для частей — в AsdaCraftMakeWindowSubsystem_Private.h; шапка и реверс — в AsdaCraftMakeWindowSubsystem.cpp.
#include "UI/AsdaCraftMakeWindowSubsystem.h"

#include "Misc/Paths.h"   // ★ 08.11.2026: самотест asda.SelfTest.CraftMoveStop
#include "World/AsdaPlayerCharacter.h"   // ★ 08.11.2026: самотест asda.SelfTest.CraftMoveStop
#include "World/AsdaPlayerController.h"   // ★ 08.11.2026: самотест asda.SelfTest.CraftMoveStop
#include "AsdaCraftMakeWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCraftMakeWindowSubsystem*.cpp

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	// ★★ 08.11.2026 (пачка 19, баг 6) САМОТЕСТ «КРАФТ ОСТАНАВЛИВАЕТ ХОД» — `asda.SelfTest.CraftMoveStop [delay=N] [quit]`, офлайн.
	//   Путь игрока: вход в ремесло (тумблер 76000) → 77000 рецепт 122 → 77068 → 77500 → кнопка 77533 (делегат кнопки), клавиша W —
	//   `InputKey`, клик по земле — `RequestMoveTo`. Отправки перехвачены (в сеть ничего не уходит), «в игре» включено на время прогона.
	//   O: шёл → вход в ремесло: павн встал [ОРИГИНАЛ]; O2 — контроль ключом 0 (ехал дальше).
	//   K: W зажата в 77500 → «Создать»: 4008 раньше 6602, стоит, общая точка взведена; W держит — хода и 4006 нет; 6603 код 1 —
	//      анимация, стоит; конец серии — W снова ведёт. A: автопуть → «Создать»: заявка снята. M: верхом — то же.
	//   C: контроль ключом asda.Move.UiActionStop 0 — после «Создать» персонаж идёт дальше (воспроизведение бага).
	//   Кадры — Reports/batch19/craft_stop/<ru|en>_*.png. Строки «КРАФТ-СТОП», итог «КРАФТ-СТОП ИТОГ: ok=… fail=…».
	struct FCraftStopRun
	{
		TWeakObjectPtr<UWorld> W;          // мир прогона
		bool bQuit = false;                // выйти после итога
		int32 Ok = 0, Fail = 0;            // счёт проверок
		uint32 CapGen = 0;                 // поколение перехвата отправок
		TArray<uint16> Ops;                // порядок 4006/4008/6602
		FVector2D Hold = FVector2D::ZeroVector;   // точка, где персонаж обязан стоять
		FString Lang, ShotDir;             // язык и папка кадров
		bool bConn0 = false;               // «в игре» до прогона
		float Speed0 = 0.35f;              // скорость сервера до прогона
		int32 Tries = 0;                   // ожидание своего персонажа
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCraftMake, Log, TEXT("КРАФТ-СТОП ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaCraftMake, Error, TEXT("КРАФТ-СТОП FAIL %s"), *What); }
		}
		int32 Count(uint16 Op) const { int32 N = 0; for (uint16 O : Ops) { N += (O == Op); } return N; }
	};
	using FCSR = TSharedRef<FCraftStopRun>;
	static const TCHAR* const kCraftStopCmd = TEXT("asda.SelfTest.CraftMoveStop");

	// Помощники самотеста (мир, подсистемы, клавиша W, кадры, таймеры, ответы сервера).
	struct FCS
	{
		static UGameInstance* GI(const FCSR& R) { UWorld* Wd = R->W.Get(); return Wd ? Wd->GetGameInstance() : nullptr; }
		static AAsdaPlayerController* PC(const FCSR& R) { UWorld* Wd = R->W.Get(); return Wd ? Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController()) : nullptr; }
		static AAsdaPlayerCharacter* Me(const FCSR& R) { AAsdaPlayerController* P = PC(R); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		template<class T> static T* Sub(const FCSR& R) { UGameInstance* G = GI(R); return G ? G->GetSubsystem<T>() : nullptr; }
		static void After(const FCSR& R, float Sec, TFunction<void(const FCSR&)> Fn)
		{
			UWorld* Wd = R->W.Get(); if (!Wd) { return; }
			FTimerHandle Hd;
			Wd->GetTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([R, Fn = MoveTemp(Fn)]() { Fn(R); }), Sec, false);
		}
		static void Key(const FCSR& R, bool bDown)
		{
			// Офлайн ответа на прежний 4006 нет — бит «запрос в пути» держал бы клавиатуру эталона (новый 4006 только после ответа).
			if (bDown) { if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { Net->ClearMoveRequestInFlight(TEXT("самотест CraftMoveStop: ответ на ход")); } }
			if (APlayerController* P = PC(R)) { P->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f)); }
		}
		static void Shot(const FCSR& R, const TCHAR* Name)
		{
			const FString File = FPaths::Combine(R->ShotDir, FString::Printf(TEXT("%s_%s.png"), *R->Lang, Name));
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			UE_LOG(LogAsdaCraftMake, Log, TEXT("КРАФТ-СТОП кадр: %s"), *File);
		}
		static float Moved(const FCSR& R) { AAsdaPlayerCharacter* M = Me(R); return M ? (float)FVector2D::Distance(M->GetGamePosXZ(), R->Hold) : -1.f; }
		static void Mark(const FCSR& R) { if (AAsdaPlayerCharacter* M = Me(R)) { R->Hold = M->GetGamePosXZ(); } }
		static bool Moving(const FCSR& R) { AAsdaPlayerCharacter* M = Me(R); return M && M->IsMoving(); }
		// Материалы рецепта 122 в сумку (как `asda.Craft.SelfTestMake`).
		static void Mats(const FCSR& R)
		{
			if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R))
			{
				Inv->DebugPutItem(2, 40, 72635, 3); Inv->DebugPutItem(2, 41, 72732, 1); Inv->DebugPutItem(2, 42, 20959, 1);
			}
		}
		// 76000 → 77000 (122) → 77068 → 77500.
		static bool OpenMake(const FCSR& R)
		{
			UAsdaCraftWindowSubsystem* Win = Sub<UAsdaCraftWindowSubsystem>(R);
			UAsdaCraftRecipeWindowSubsystem* Rw = Sub<UAsdaCraftRecipeWindowSubsystem>(R);
			UAsdaCraftMakeWindowSubsystem* Mk = Sub<UAsdaCraftMakeWindowSubsystem>(R);
			if (!Win || !Rw || !Mk) { return false; }
			if (Mk->IsOpen()) { return true; }
			if (!Win->IsOpen()) { Win->GateAndToggle(TEXT("самотест CraftMoveStop")); }
			Rw->OpenForRecipe(122, TEXT("самотест CraftMoveStop"));
			Rw->DebugPressButton(77068);
			return Mk->IsOpen();
		}
		// «Создать» кнопкой 77533 (делегат кнопки).
		static void Create(const FCSR& R) { if (UAsdaCraftMakeWindowSubsystem* Mk = Sub<UAsdaCraftMakeWindowSubsystem>(R)) { Mk->DebugPressButton(77533); } }
		// Дождаться конца серии (анимация и ожидание ответа сняты) — до 10 с.
		static void WaitIdle(const FCSR& R, TFunction<void(const FCSR&)> Then, int32 Left = 50)
		{
			UAsdaCraftMakeWindowSubsystem* Mk = Sub<UAsdaCraftMakeWindowSubsystem>(R);
			if (!Mk || Left <= 0 || (!Mk->IsAnimating() && !Mk->IsAwaitingReply())) { Then(R); return; }
			After(R, 0.2f, [Then, Left](const FCSR& R) { WaitIdle(R, Then, Left - 1); });
		}
		// Ответ сервера 6603 «код 1, провал на первой ступени» — анимация одной ступени.
		static void ReplyFail(const FCSR& R)
		{
			if (UAsdaCraftSubsystem* C = Sub<UAsdaCraftSubsystem>(R))
			{
				C->DebugInject(6603, Build6603(1, 0, 0, 0, 0, 0, TArray<FMat>{ { 72635, 40, 0 }, { 72732, 41, 0 }, { 20959, 42, 0 } }));
			}
		}
		static void Finish(const FCSR& R)
		{
			Key(R, false);
			if (AAsdaPlayerCharacter* M = Me(R)) { M->StopMove(); M->ClearMount(); M->SetAnimState(EAsdaPlayerState::Idle); }
			if (UAsdaMinimapSubsystem* Mini = Sub<UAsdaMinimapSubsystem>(R)) { if (Mini->IsTravelRequested()) { Mini->CancelTravelRequest(); } }
			if (UAsdaCraftMakeWindowSubsystem* Mk = Sub<UAsdaCraftMakeWindowSubsystem>(R)) { Mk->ResetDialog(TEXT("самотест CraftMoveStop: конец")); }
			if (UAsdaCraftWindowSubsystem* Win = Sub<UAsdaCraftWindowSubsystem>(R)) { if (Win->IsOpen()) { Win->HideAndCleanup(); } }
			if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R))
			{
				if (R->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, R->CapGen); R->CapGen = 0; }
				Net->bInGameConnected = R->bConn0;
				Net->bActionPending = false;
				Net->ClearMoveRequestInFlight(TEXT("самотест CraftMoveStop: конец"));
				Net->LastServerSpeed = R->Speed0;
			}
			AsdaCVarSave::RestoreAll(kCraftStopCmd);
			AsdaSelfTestKit::Finish(R->W.Get(), kCraftStopCmd, R->bQuit, LogAsdaCraftMake, TEXT("КРАФТ-СТОП"), R->Ok, R->Fail,
				*FString::Printf(TEXT(" (4006: %d, 4008: %d, 6602: %d)"), R->Count(4006), R->Count(4008), R->Count(6602)));
		}

		// ── Шаги ──
		static void Begin(const FCSR& R)
		{
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
			if (!Me(R) || !Net)
			{
				if (++R->Tries > 40) { AsdaSelfTestKit::Refused(R->W.Get(), kCraftStopCmd, R->bQuit, TEXT("нет своего персонажа за 20 с")); return; }
				After(R, 0.5f, [](const FCSR& R) { Begin(R); });
				return;
			}
			UAsdaCraftSubsystem* Craft = Sub<UAsdaCraftSubsystem>(R);
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
			if (!Craft || !Inv) { AsdaSelfTestKit::Refused(R->W.Get(), kCraftStopCmd, R->bQuit, TEXT("нет подсистем ремесла/сумки")); return; }
			R->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
			R->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch19/craft_stop")));
			AsdaCVarSave::ArmWorldGuard(R->W.Get(), kCraftStopCmd);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.UiActionStop"), 1);
			TWeakPtr<FCraftStopRun> WR = R;
			R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				TSharedPtr<FCraftStopRun> S = WR.Pin();
				if (S && (Op == 4006 || Op == 4008 || Op == 6602)) { S->Ops.Add(Op); }
				// Офлайн-«сервер»: на каждый 4006 — грант на следующем тике (тот же делегат, что пакет 4007; не из колбэка отправки).
				//   Иначе эталонная клавиатура ждала бы ответа (бит «запрос в пути»), а локальный старт вставал через 0,6 с.
				if (S && Op == 4006 && P && Len >= 37)
				{
					const FVector2D Cur(AsdaBytes::RdF32(P + 16 + 5), AsdaBytes::RdF32(P + 16 + 9));
					const FVector2D Dst(AsdaBytes::RdF32(P + 16 + 13), AsdaBytes::RdF32(P + 16 + 17));
					if (UWorld* Wd = S->W.Get())
					{
						Wd->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WR, Cur, Dst]()
						{
							TSharedPtr<FCraftStopRun> S2 = WR.Pin();
							UWorld* W2 = S2 ? S2->W.Get() : nullptr;
							UAsdaNetSubsystem* N = W2 ? W2->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
							if (N) { N->OnEntityMove.Broadcast((int32)N->SelfUserNum, 1, Cur, Dst, N->LastServerSpeed, false, 2, -1); }
						}));
					}
				}
			}, 0, kCraftStopCmd);
			if (R->CapGen == 0) { R->Expect(false, TEXT("перехват отправок занят")); Finish(R); return; }
			if (!Net->IsSelfUserNumKnown()) { Net->SetSelfUserNumForDebug(41); }
			R->bConn0 = Net->bInGameConnected; R->Speed0 = Net->LastServerSpeed;
			Net->bInGameConnected = true;
			Net->LastServerSpeed = 0.35f;
			Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(TArray<int32>{ 1, 76, 122 }, 30, 6, 100, 10));
			for (int32 C = 0; C < 60; ++C) { Inv->DebugPutItem(2, C, 0, 0); }
			Mats(R);
			if (AAsdaPlayerCharacter* M = Me(R)) { M->StopMove(); M->SetAnimState(EAsdaPlayerState::Idle); }
			After(R, 0.3f, [](const FCSR& R) { StepO(R); });
		}
		// Ход «в точку»: клик по земле; не тронулся (офлайн-сетка) — ход «в точку» напрямую, как самотест высадки.
		static void Walk(const FCSR& R, float Dx)
		{
			AAsdaPlayerCharacter* M = Me(R);
			if (!M) { return; }
			const FVector2D Dst = M->GetGamePosXZ() + FVector2D(Dx, 0.f);
			PC(R)->RequestMoveTo(Dst);
			if (!M->IsMoving()) { M->NetMoveFromActor(Dst, Sub<UAsdaNetSubsystem>(R)->LastServerSpeed); Sub<UAsdaNetSubsystem>(R)->bActionPending = true; }
		}
		// O. Шёл → вход в ремесло 76000.
		static void StepO(const FCSR& R)
		{
			Walk(R, 900.f);
			After(R, 0.3f, [](const FCSR& R)
			{
				R->Expect(Moving(R), TEXT("O0 персонаж идёт кликом по земле"));
				Sub<UAsdaCraftWindowSubsystem>(R)->GateAndToggle(TEXT("самотест CraftMoveStop (O)"));
				Mark(R);
				After(R, 0.3f, [](const FCSR& R)
				{
					R->Expect(Sub<UAsdaCraftWindowSubsystem>(R)->IsOpen() && !Moving(R) && Moved(R) < 2.f,
						FString::Printf(TEXT("O1 вход в ремесло 76000: персонаж встал (идёт %d, сдвиг %.1f) [ОРИГИНАЛ CharFx 0]"), (int32)Moving(R), Moved(R)));
					Shot(R, TEXT("o1_craft_open_stopped"));
					R->Expect(OpenMake(R), TEXT("K0 77068 → окно изготовления 77500 открыто"));
					StepK(R);
				});
			});
		}
		// K. W зажата в 77500 → «Создать» → ожидание → анимация → конец серии.
		static void StepK(const FCSR& R)
		{
			Key(R, true);
			After(R, 0.4f, [](const FCSR& R)
			{
				if (!Moving(R))
				{
					// Кадр мог подвиснуть на скриншоте раздела O — второе нажатие (отпустить и нажать), в журнал.
					UE_LOG(LogAsdaCraftMake, Warning, TEXT("КРАФТ-СТОП K1: за 0,4 с ход не начался — W нажата повторно"));
					Key(R, false); Key(R, true);
					After(R, 0.5f, [](const FCSR& R) { StepK2(R); });
					return;
				}
				StepK2(R);
			});
		}
		static void StepK2(const FCSR& R)
		{
			{
				R->Expect(Moving(R), TEXT("K1 в 77500 W ведёт персонажа (как оригинал: клавиши окном ремесла не держатся)"));
				Shot(R, TEXT("k1_walking_w"));
				R->Ops.Reset();
				Create(R);
				const int32 I8 = R->Ops.IndexOfByKey((uint16)4008), I6 = R->Ops.IndexOfByKey((uint16)6602);
				R->Expect(I6 >= 0 && I8 >= 0 && I8 < I6, FString::Printf(TEXT("K2 «Создать»: 4008 ушёл раньше 6602 (4008 №%d, 6602 №%d)"), I8, I6));
				R->Expect(!Moving(R) && PC(R)->IsStunHardStopped(),
					FString::Printf(TEXT("K3 персонаж стоит (%d), ход держится общей точкой (%d)"), (int32)!Moving(R), (int32)PC(R)->IsStunHardStopped()));
				Mark(R);
				const int32 N0 = R->Count(4006);
				After(R, 0.5f, [N0](const FCSR& R)
				{
					R->Expect(!Moving(R) && Moved(R) < 2.f && R->Count(4006) == N0,
						FString::Printf(TEXT("K4 ждём 6603, W зажата: хода нет (сдвиг %.1f, новых 4006 %d)"), Moved(R), R->Count(4006) - N0));
					Shot(R, TEXT("k4_create_stopped_w_held"));
					ReplyFail(R);
					After(R, 0.6f, [N0](const FCSR& R)
					{
						UAsdaCraftMakeWindowSubsystem* Mk = Sub<UAsdaCraftMakeWindowSubsystem>(R);
						R->Expect(Mk && Mk->IsAnimating() && !Moving(R) && Moved(R) < 2.f && R->Count(4006) == N0,
							FString::Printf(TEXT("K5 6603 код 1: анимация идёт (%d), персонаж стоит (сдвиг %.1f)"), Mk ? (int32)Mk->IsAnimating() : -1, Moved(R)));
						WaitIdle(R, [](const FCSR& R)
						{
							const int32 N1 = R->Count(4006);
							R->Expect(!PC(R)->IsStunHardStopped(), TEXT("K6 серия окончена — удержание снято"));
							// Ответ на 4006 раздела K1 офлайн не придёт — снимаем бит «запрос в пути», как сделал бы грант сервера.
							if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { Net->ClearMoveRequestInFlight(TEXT("самотест CraftMoveStop: ответ на ход")); }
							After(R, 0.5f, [N1](const FCSR& R)
							{
								R->Expect(Moving(R) && Moved(R) > 5.f,
									FString::Printf(TEXT("K7 после крафта зажатая W снова ведёт (идёт %d, сдвиг %.1f, 4006 %d)"), (int32)Moving(R), Moved(R), R->Count(4006) - N1));
								Key(R, false);
								After(R, 0.3f, [](const FCSR& R) { if (AAsdaPlayerCharacter* M = Me(R)) { M->StopMove(); } StepA(R); });
							});
						});
					});
				});
			}
		}
		// A. Автопуть → «Создать».
		static void StepA(const FCSR& R)
		{
			Mats(R);
			UAsdaMinimapSubsystem* Mini = Sub<UAsdaMinimapSubsystem>(R);
			const FVector2D P = Me(R)->GetGamePosXZ();
			const bool bBegun = Mini && Mini->BeginMapTravel(3, FMath::FloorToInt(P.X / 100.f) + 12, FMath::FloorToInt(P.Y / 100.f));
			After(R, 0.4f, [bBegun](const FCSR& R)
			{
				UAsdaMinimapSubsystem* Mini = Sub<UAsdaMinimapSubsystem>(R);
				R->Expect(bBegun && Mini && Mini->IsTravelRequested(), TEXT("A0 автопуть взведён"));
				R->Ops.Reset();
				Create(R);
				R->Expect(Mini && !Mini->IsTravelRequested() && !Moving(R) && R->Count(6602) == 1,
					FString::Printf(TEXT("A1 «Создать»: заявка автопути снята (%d), стоит (%d), 6602 ушёл (%d)"),
						Mini ? (int32)!Mini->IsTravelRequested() : -1, (int32)!Moving(R), R->Count(6602)));
				Mark(R);
				After(R, 0.5f, [](const FCSR& R)
				{
					R->Expect(!Moving(R) && Moved(R) < 2.f, FString::Printf(TEXT("A2 через 0,5 с автопуть не повёл (сдвиг %.1f)"), Moved(R)));
					ReplyFail(R);
					WaitIdle(R, [](const FCSR& R) { StepM(R); });
				});
			});
		}
		// M. Верхом, W → «Создать».
		static void StepM(const FCSR& R)
		{
			Mats(R);
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
			Net->OnMountState.Broadcast((int32)Net->SelfUserNum, 522, true);
			Net->LastServerSpeed = 0.56f;
			Key(R, true);
			After(R, 0.4f, [](const FCSR& R)
			{
				AAsdaPlayerCharacter* M = Me(R);
				R->Expect(M && M->IsMounted() && M->IsMoving(), FString::Printf(TEXT("M0 верхом (%d) W ведёт (%d)"), M ? (int32)M->IsMounted() : -1, (int32)Moving(R)));
				Shot(R, TEXT("m0_riding"));
				R->Ops.Reset();
				Create(R);
				const int32 I8 = R->Ops.IndexOfByKey((uint16)4008), I6 = R->Ops.IndexOfByKey((uint16)6602);
				Mark(R);
				R->Expect(!Moving(R) && I8 >= 0 && I6 > I8, FString::Printf(TEXT("M1 верхом «Создать»: стоит, 4008 №%d раньше 6602 №%d"), I8, I6));
				After(R, 0.5f, [](const FCSR& R)
				{
					R->Expect(!Moving(R) && Moved(R) < 2.f, FString::Printf(TEXT("M2 через 0,5 с верхом на месте (сдвиг %.1f)"), Moved(R)));
					Shot(R, TEXT("m2_riding_stopped"));
					Key(R, false);
					ReplyFail(R);
					WaitIdle(R, [](const FCSR& R)
					{
						if (AAsdaPlayerCharacter* M = Me(R)) { M->StopMove(); M->ClearMount(); }
						Sub<UAsdaNetSubsystem>(R)->LastServerSpeed = 0.35f;
						StepC(R);
					});
				});
			});
		}
		// C. Контроль ключом 0 — прежнее поведение (баг), затем O2 — вход в ремесло на ходу с ключом 0.
		static void StepC(const FCSR& R)
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Move.UiActionStop"), 0);
			Mats(R);
			Key(R, true);
			After(R, 0.4f, [](const FCSR& R)
			{
				R->Ops.Reset();
				Create(R);
				Mark(R);
				const bool bNo4008 = R->Count(4008) == 0 && R->Count(6602) == 1;
				After(R, 0.4f, [bNo4008](const FCSR& R)
				{
					R->Expect(Moving(R) && Moved(R) > 20.f && bNo4008,
						FString::Printf(TEXT("C1 контроль ключом asda.Move.UiActionStop 0: после «Создать» персонаж идёт дальше (сдвиг %.1f), 4008 нет — баг воспроизведён"), Moved(R)));
					Shot(R, TEXT("c1_control_key0_keeps_walking"));
					Key(R, false);
					ReplyFail(R);
					WaitIdle(R, [](const FCSR& R)
					{
						if (UAsdaCraftMakeWindowSubsystem* Mk = Sub<UAsdaCraftMakeWindowSubsystem>(R)) { Mk->ResetDialog(TEXT("самотест: O-контроль")); }
						if (UAsdaCraftWindowSubsystem* Win = Sub<UAsdaCraftWindowSubsystem>(R)) { if (Win->IsOpen()) { Win->HideAndCleanup(); } }
						if (AAsdaPlayerCharacter* M = Me(R)) { M->StopMove(); }
						Walk(R, -900.f);
						After(R, 0.2f, [](const FCSR& R)
						{
							Sub<UAsdaCraftWindowSubsystem>(R)->GateAndToggle(TEXT("самотест CraftMoveStop (O-контроль)"));
							Mark(R);
							After(R, 0.3f, [](const FCSR& R)
							{
								R->Expect(Moving(R) && Moved(R) > 20.f,
									FString::Printf(TEXT("O2 контроль ключом 0: вход в ремесло на ходу — павн едет дальше (сдвиг %.1f), прежний дефект порта"), Moved(R)));
								Finish(R);
							});
						});
					});
				});
			});
		}
	};

	FAutoConsoleCommandWithWorldAndArgs GCmdCraftMoveStopSelfTest(
		TEXT("asda.SelfTest.CraftMoveStop"),
		TEXT("Самотест [ИСПРАВЛЕНИЕ 08.11]: «Создать» 77533 останавливает ход (клавиши, клик, автопуть, верхом) до конца анимации; вход в ремесло "
		     "гасит ход павна [ОРИГИНАЛ]; контроль ключом asda.Move.UiActionStop 0. Офлайн. `delay=N` `quit`."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
			if (!W) { return; }
			if (AsdaSelfTestKit::RefuseStart(W, kCraftStopCmd, KA)) { return; }
			FCSR R = MakeShared<FCraftStopRun>();
			R->W = W; R->bQuit = KA.bQuit;
			if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H0;
			W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda([R, WW]()
			{
				if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), kCraftStopCmd, R->bQuit)) { return; }
				FCS::Begin(R);
			}), FMath::Max(0.5f, KA.Delay), false);
		}));
}
#endif // ASDA_DEV_CONSOLE
