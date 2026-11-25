// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHighRefineSubsystem_SelfTest.cpp — ★ R3-c 12.11.2026 (разрез AsdaHighRefineSubsystem.cpp, дословный перенос): самотест окна высокой переплавки.
//   Общее для частей — в AsdaHighRefineSubsystem_Private.h; шапка и реверс — в AsdaHighRefineSubsystem.cpp.
#include "UI/AsdaHighRefineSubsystem.h"


#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "AsdaHighRefineSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaHighRefineSubsystem*.cpp

// ★ U21 (В1 дедупа №2): имя константы — по контейнеру; значение обязано совпадать с размером контейнера инвентаря.
static_assert(UAsdaHighRefineSubsystem::kShellCells == UAsdaInventorySubsystem::kShellSlots, "размер контейнера разошёлся с инвентарём");

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
// ★ САМОТЕСТ ПУТЁМ ИГРОКА (-game). Сервера нет: предметы кладутся в МОДЕЛЬ инвентаря отладочным путём,
//   окно открывается КНОПКОЙ 20197 окна заточки, 6501 синтезируется и идёт боевым приёмником.
//   `delay=N` — сдвиг всех шагов (запуск после самотеста окна 20000), `shot` — кадры, `quit` — выход.
	FAutoConsoleCommandWithWorldAndArgs GCmdHighRefineSelfTest(
		TEXT("asda.HighRefine.SelfTest"),
		TEXT("Окно 78200: открыть кнопкой 20197, выбрать предмет, рецепт, нехватка материалов, «ОК» и полоса, "
		     "ответы 6501 (успех/провал/отказы), закрытие. `delay=N` `shot` `quit`."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaHighRefineSubsystem* S = HighRefineFromWorld(W);
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			UAsdaItemUpgradeSubsystem* Up = GI ? GI->GetSubsystem<UAsdaItemUpgradeSubsystem>() : nullptr;
			UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			if (!S || !Inv || !Up || !IT || !W) { return; }
			bool bQuit = false, bShot = false;
			float Delay = 0.f;
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
				if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
			}
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
			// ★ С-Р1 (26.09.2026): ТОЛЬКО ОФЛАЙН. «ОК» окна 78200 → `SendCraftRequest` → 6500 — при живом соединении это
			//   настоящая переплавка с расходом материалов (перехват `DebugCapture6500` при соединении не ставится).
			//   Окно защиты 6500 — задержка + весь проход (≈56 с) + запас.
			static const uint16 kGuardOps[] = { 6500 };
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.HighRefine.SelfTest"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			// ★ Р-2 (ревизия shopdbl2, 10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим
			//   шагом; при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
			// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.HighRefine.SelfTest"), bQuit);   // общий флаг отложенного старта
			auto At = [W, S, Delay, Gate, bQuit](float T, TFunction<void()> Fn)
			{
				FTimerHandle H;
				TWeakObjectPtr<UWorld> WW(W);   // мир прогона (для отказа)
				W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(S, [Gate, WW, bQuit, Fn = MoveTemp(Fn)]()
				{
					if (!Gate->Pass(WW.Get())) { return; }   // Р-2: офлайн повторно — на отложенном старте
					Fn();
				}), T + Delay, false);
			};
			auto Shot = [bShot](const TCHAR* Name)
			{
				if (!bShot) { return; }
				UE_LOG(LogAsdaHighRefine, Warning, TEXT("САМОТЕСТ-78200: КАДР → %s.png"), Name);
				FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			};
			auto Dump = [S](const TCHAR* Step)
			{
				UE_LOG(LogAsdaHighRefine, Warning, TEXT("САМОТЕСТ-78200: %s\n%s"), Step, *S->DumpState());
				for (const FString& L : S->GetJournal()) { UE_LOG(LogAsdaHighRefine, Warning, TEXT("  журнал: %s"), *L); }
				S->ClearJournal();
			};
			auto Check = [](bool bOk, const FString& What)
			{
				UE_LOG(LogAsdaHighRefine, Warning, TEXT("САМОТЕСТ-78200 %s %s"), bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
			};
			// Общие данные шагов: выбранный предмет, ячейки материалов, рецепт.
			struct FCtx { int32 Code = 0; int32 Recipe = -1; int32 MatCode[3] = { 0, 0, 0 }; int32 MatCont[3] = { 0, 0, 0 }; int32 MatCell[3] = { -1, -1, -1 }; int32 MatNeed[3] = { 0, 0, 0 }; int32 ListIdx = -1;
				int32 GoldWas = 0; int32 GoldCellWas = -1; };   // ★ ревизия О4 МЕЛК-4: деньги и ячейка 20551 до теста (−1 — ячейки нет)
			TSharedRef<FCtx> Ctx = MakeShared<FCtx>();
			// Шаг 1: подобрать предмет снаряжения с рецептом, положить его и материалы, золото; открыть 20000 и нажать 20197.
			At(1.0f, [S, Inv, Up, IT, Ctx, Check, W]()
			{
				// Нотисы прошлого самотеста (окно 20000) не должны закрывать кадры этого.
				if (UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX"))); }
				const UDataTable* DT = IT->GetDataTable(FName(TEXT("refinehigh")));
				TArray<FAsdaRefinehighRow*> Rows;
				if (DT) { DT->GetAllRows<FAsdaRefinehighRow>(TEXT("HighRefineTest"), Rows); }
				Rows.Sort([](const FAsdaRefinehighRow& A, const FAsdaRefinehighRow& B) { return A.Idx < B.Idx; });
				const FAsdaRefinehighRow* Pick = nullptr;
				for (const FAsdaItemRecord& R : IT->GetAllItems())
				{
					if (R.ItemType != 9) { continue; }   // оружие
					const FAsdaImgRecord* Img = IT->GetImgRecord(R.ImgIndex);
					if (!Img || Img->IconAtlasMale.IsEmpty()) { continue; }
					for (const FAsdaRefinehighRow* Row : Rows)
					{
						if (Row && Row->ResultRequiredLevel == R.RequiredLevel && Row->ResultItemType == R.ItemType && Row->ResultItemGrade == R.ItemGrade)
						{
							// Первая запись по порядку — та, что найдёт окно; материалы должны быть в item_list.
							if (IT->GetItem(Row->Mat1Code) && IT->GetItem(Row->Mat2Code) && IT->GetItem(Row->Mat3Code)) { Pick = Row; Ctx->Code = R.ItemCode; }
							break;
						}
					}
					if (Pick) { break; }
				}
				Check(Pick != nullptr, FString::Printf(TEXT("подобран предмет %d и рецепт %d"), Ctx->Code, Pick ? Pick->Idx : -1));
				if (!Pick) { return; }
				Ctx->Recipe = Pick->Idx;
				// Сумка 0…29 — очистить, цель в 20; материалы — снаряжение в сумку 25…27, прочее в расходники 50…52.
				for (int32 C = 0; C < UAsdaInventorySubsystem::kShellSlots; ++C) { Inv->DebugPutItem(1, C, 0, 0); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
				Inv->DebugPutItem(1, 20, Ctx->Code, 1);
				const int32 MC[3] = { Pick->Mat1Code, Pick->Mat2Code, Pick->Mat3Code };
				const int32 MN[3] = { Pick->Mat1Qty, Pick->Mat2Qty, Pick->Mat3Qty };
				for (int32 M = 0; M < 3; ++M)
				{
					const FAsdaItemRecord* MR = IT->GetItem(MC[M]);
					// ★ Волна 11 (МЕЛК-4): общая точка `itemIsWeaponOrEquipClass`, а не копия условия.
					const bool bEquip = IT->IsWeaponOrEquipClass(MC[M]);
					Ctx->MatCode[M] = MC[M];   // ★ волна 13: для докладки материалов перед отправкой со 2-й страницы
					Ctx->MatCont[M] = bEquip ? 1 : 2;
					Ctx->MatCell[M] = bEquip ? 25 + M : 50 + M;
					Ctx->MatNeed[M] = MN[M];
					// Материал 1 кладём на ОДИН меньше нужного — проверка «не хватает» (красный, 15303).
					Inv->DebugPutItem(Ctx->MatCont[M], Ctx->MatCell[M], MC[M], M == 1 ? FMath::Max(0, MN[M] - 1) : MN[M]);
					UE_LOG(LogAsdaHighRefine, Warning, TEXT("САМОТЕСТ-78200: материал %d код %d «%s» itemType=%d → к=%d яч=%d, нужно %d"),
						M, MC[M], *IT->GetItemName(MC[M]), MR ? MR->ItemType : -1, Ctx->MatCont[M], Ctx->MatCell[M], MN[M]);
				}
				// ★ Ревизия О4 (МЕЛК-4): `SetGoldDisplay` с О4 пишет и ячейку 20551 — запомнить обе, вернуть на шаге 55 с.
				Ctx->GoldWas = Inv->GetGold();
				for (int32 i = 0; i < 60 && Ctx->GoldCellWas < 0; ++i) { if (const FAsdaItemInstance* G = Inv->GetCell(2, i)) { if (G->ItemCode == 20551) { Ctx->GoldCellWas = G->Amount; } } }
				Inv->SetGoldDisplay(FMath::Max(Pick->GoldCost * 3, 1000000));
				if (!Up->IsOpen()) { Up->ToggleWindow(); }
				Up->OnSpecialClicked();   // кнопка 20197 — боевой путь окна заточки
				Check(S->IsOpen(), TEXT("20197 «Особый» открыл окно 78200"));
				UE_LOG(LogAsdaHighRefine, Warning, TEXT("%s"), *S->DumpControls());
			});
			At(2.0f, [W]() { GEngine->Exec(W, TEXT("asda.UI.TreeGeom hr_open WBP_Smelting3_Win")); });
			At(2.2f, [Shot]() { Shot(TEXT("asda_highrefine_open")); });
			// Шаг 2: найти позицию цели в сетке, выбрать; материал 1 не хватает — красный, «ОК» → 15303.
			At(3.0f, [S, Ctx, Dump, Check]()
			{
				Ctx->ListIdx = 0;   // сумка очищена — единственный предмет снаряжения стоит первым
				S->OnGridCellClicked(Ctx->ListIdx);
				Check(S->GetRecipeKey() == Ctx->Recipe, FString::Printf(TEXT("рецепт найден: %d (ждали %d)"), S->GetRecipeKey(), Ctx->Recipe));
				Dump(TEXT("выбор предмета (материал 1 не хватает)"));
				S->OnOkClicked();
				Check(S->GetGuideText().Contains(TEXT("Недостаточно материалов")), FString::Printf(TEXT("«ОК» без материалов → гайд 15303 «%s»"), *S->GetGuideText()));
			});
			At(3.3f, [Shot]() { Shot(TEXT("asda_highrefine_short")); });
			// Шаг 3: докладываем материал, повторный выбор — всё белое; тело 6500; «ОК» → полоса → 0x18 → отправка.
			At(4.0f, [S, Inv, Ctx, Dump, Check]()
			{
				Inv->DebugPutItem(Ctx->MatCont[1], Ctx->MatCell[1], 0, 0);
				// повторно положить с нужным количеством (код тот же)
				Dump(TEXT("перед докладкой"));
			});
			At(4.2f, [S, Inv, Ctx, IT, Check]()
			{
				const FAsdaRefinehighRow* Dummy = nullptr; (void)Dummy;
				const UDataTable* DT = IT->GetDataTable(FName(TEXT("refinehigh")));
				const FAsdaRefinehighRow* Row = nullptr;
				if (DT) { TArray<FAsdaRefinehighRow*> Rows; DT->GetAllRows<FAsdaRefinehighRow>(TEXT("HighRefineTest"), Rows); for (const FAsdaRefinehighRow* R : Rows) { if (R && R->Idx == Ctx->Recipe) { Row = R; } } }
				if (!Row) { return; }
				Inv->DebugPutItem(Ctx->MatCont[1], Ctx->MatCell[1], Row->Mat2Code, Row->Mat2Qty);
				S->OnGridCellClicked(Ctx->ListIdx);
				TArray<uint8> Body;
				const bool bB = S->DebugBuildRequest(Body);
				FString Hex; for (uint8 B : Body) { Hex += FString::Printf(TEXT("%02X "), B); }
				Check(bB && Body.Num() == 40, FString::Printf(TEXT("тело 6500 (40 б): %s"), *Hex));
				if (bB)
				{
					Check(AsdaBytes::RdI32(Body.GetData()) == Ctx->Recipe, TEXT("6500 @0 = ключ рецепта"));
					Check(AsdaBytes::RdI16(Body.GetData() + 4) == 20 && Body[6] == 1, TEXT("6500 @4/@6 = слот 20 / контейнер 1"));
					for (int32 M = 0; M < 3; ++M)
					{
						const int32 At7 = 7 + 11 * M;
						Check(AsdaBytes::RdI16(Body.GetData() + At7) == Ctx->MatCell[M] && Body[At7 + 4] == Ctx->MatCont[M]
							&& AsdaBytes::RdI32(Body.GetData() + At7 + 5) == Ctx->MatNeed[M],
							FString::Printf(TEXT("6500 ссылка %d = слот %d / контейнер %d / количество %d"), M, Ctx->MatCell[M], Ctx->MatCont[M], Ctx->MatNeed[M]));
					}
				}
				S->OnOkClicked();   // полоса пошла; без сервера 6500 «не ушёл» — окно освобождается
			});
			At(4.35f, [Shot]() { Shot(TEXT("asda_highrefine_bar")); });
			At(5.5f, [S, Dump, Check]()
			{
				const TArray<FString>& J = S->GetJournal();
				int32 IStart = -1, ISnd = -1, IEnd = -1, ISend = -1;
				double TStart = 0, TEnd = 0;
				for (int32 I = 0; I < J.Num(); ++I)
				{
					const double T = FCString::Atod(*J[I]);
					if (J[I].Contains(TEXT("bar start"))) { IStart = I; TStart = T; }
					if (J[I].Contains(TEXT("snd S_e_refine_process"))) { ISnd = I; }
					if (J[I].Contains(TEXT("bar 0x18"))) { IEnd = I; TEnd = T; }
					if (J[I].Contains(TEXT("send 6500"))) { ISend = I; }
				}
				Check(IStart >= 0 && ISnd > IStart && IEnd > ISnd && ISend > IEnd, TEXT("порядок: полоса → звук S_e_refine_process → 0x18 полосы → отправка 6500"));
				Check(IEnd >= 0 && (TEnd - TStart) > 300.0 && (TEnd - TStart) < 700.0,
					FString::Printf(TEXT("полоса доиграла за %.0f мс (эталон ≈388: 1000 × 100 / ширина 258)"), TEnd - TStart));
				Dump(TEXT("после «ОК» и полосы"));
			});
			// Шаг 4: ответы 6501 боевым приёмником.
			At(6.0f, [S, Check]()
			{
				S->OnGridCellClicked(0);
				TArray<uint8> B; const bool bB = S->DebugBuildResult(1, B);
				Check(bB, TEXT("тело 6501 код 1 собрано"));
				if (bB) { S->DebugInjectResult(B); }
			});
			At(6.3f, [Shot]() { Shot(TEXT("asda_highrefine_success")); });
			At(6.4f, [S, Check]()
			{
				S->OnCloseClicked();   // недавняя активность (< 1000 мс от ответа) — закрытие отклонено
				Check(S->IsOpen(), TEXT("закрытие в первую секунду после ответа отклонено (uiCheckRecentActivity)"));
			});
			At(8.5f, [S, Dump, Check]()
			{
				const TArray<FString>& J = S->GetJournal();
				bool bRecv = false, bFx = false, bEnd = false;
				for (const FString& L : J)
				{
					bRecv |= L.Contains(TEXT("recv 6501 код 1"));
					bFx |= L.Contains(TEXT("fx+ 78272"));
					bEnd |= L.Contains(TEXT("fx0x18 78272"));
				}
				Check(bRecv && bFx && bEnd, TEXT("успех: 6501 код 1 → эффект 78272 → событие 0x18 (конец эффекта)"));
				Dump(TEXT("после успеха"));
			});
			At(9.0f, [S, Inv, Ctx, IT]()
			{
				// материалы списаны — вернуть их для следующего ответа
				const UDataTable* DT = IT->GetDataTable(FName(TEXT("refinehigh")));
				TArray<FAsdaRefinehighRow*> Rows; if (DT) { DT->GetAllRows<FAsdaRefinehighRow>(TEXT("HighRefineTest"), Rows); }
				for (const FAsdaRefinehighRow* R : Rows)
				{
					if (!R || R->Idx != Ctx->Recipe) { continue; }
					Inv->DebugPutItem(Ctx->MatCont[0], Ctx->MatCell[0], R->Mat1Code, R->Mat1Qty * 2);
					Inv->DebugPutItem(Ctx->MatCont[1], Ctx->MatCell[1], R->Mat2Code, R->Mat2Qty * 2);
					Inv->DebugPutItem(Ctx->MatCont[2], Ctx->MatCell[2], R->Mat3Code, R->Mat3Qty * 2);
				}
				S->OnGridCellClicked(0);
				TArray<uint8> B; if (S->DebugBuildResult(8, B)) { S->DebugInjectResult(B); }
			});
			At(9.3f, [Shot]() { Shot(TEXT("asda_highrefine_fail")); });
			At(11.0f, [S, Dump, Check]()
			{
				bool bFx = false; for (const FString& L : S->GetJournal()) { bFx |= L.Contains(TEXT("fx+ 78273")); }
				Check(bFx, TEXT("провал: 6501 код 8 → эффект 78273"));
				Dump(TEXT("после провала"));
				TArray<uint8> B; B.SetNumZeroed(235); B[0] = 2; S->DebugInjectResult(B);   // «Не хватает золота.»
			});
			At(11.5f, [S, Dump, Check]()
			{
				bool bChat = false, bFx = false;
				for (const FString& L : S->GetJournal()) { bChat |= L.Contains(TEXT("chat 15132")); bFx |= L.Contains(TEXT("fx+ 78273")); }
				Check(bChat && !bFx, TEXT("отказ 2 → чат 15132, эффекта «разрушения» нет (Д7-1)"));
				Dump(TEXT("после отказа 2"));
				TArray<uint8> B; B.SetNumZeroed(235); B[0] = 9; S->DebugInjectResult(B);
			});
			At(12.0f, [S, Dump, Check]()
			{
				bool bChat = false; for (const FString& L : S->GetJournal()) { bChat |= L.Contains(TEXT("chat 15130")); }
				Check(bChat, TEXT("код 9 → чат 15130 (Д7-2)"));
				Dump(TEXT("после кода 9"));
			});
			At(13.5f, [S, Check]()
			{
				S->OnCloseClicked();
				Check(!S->IsOpen(), TEXT("X после паузы > 1 с закрывает окно"));
			});
			// ═══════════ ВОЛНА 9: С-1 / С-2 / Д7-7 / таймаут Д7-6 / звук кнопок процедуры инвентаря ═══════════
			// Общие помощники шагов волны 9.
			auto Restock = [Inv, Ctx, IT]()
			{
				// Материалы рецепта — вдвое больше нужного (ответ «успех» списывает по нужному).
				const UDataTable* DT = IT->GetDataTable(FName(TEXT("refinehigh")));
				TArray<FAsdaRefinehighRow*> Rows; if (DT) { DT->GetAllRows<FAsdaRefinehighRow>(TEXT("HighRefineTest"), Rows); }
				for (const FAsdaRefinehighRow* R : Rows)
				{
					if (!R || R->Idx != Ctx->Recipe) { continue; }
					Inv->DebugPutItem(Ctx->MatCont[0], Ctx->MatCell[0], R->Mat1Code, R->Mat1Qty * 2);
					Inv->DebugPutItem(Ctx->MatCont[1], Ctx->MatCell[1], R->Mat2Code, R->Mat2Qty * 2);
					Inv->DebugPutItem(Ctx->MatCont[2], Ctx->MatCell[2], R->Mat3Code, R->Mat3Qty * 2);
				}
				Inv->DebugPutItem(1, 20, Ctx->Code, 1);
			};
			auto JournalIdx = [S](const TCHAR* What, int32 From) -> int32
			{
				const TArray<FString>& J = S->GetJournal();
				for (int32 I = FMath::Max(0, From); I < J.Num(); ++I) { if (J[I].Contains(What)) { return I; } }
				return -1;
			};
			auto UpJournalHas = [Up](const TCHAR* What) -> bool
			{
				for (const FString& L : Up->GetFxJournal()) { if (L.Contains(What)) { return true; } }
				return false;
			};
			auto ClickUpCell0 = [W]() -> bool
			{
				// НАСТОЯЩИЙ клик Slate (общий симклик) по ячейке 0 сетки окна 20000 — путь игрока.
				UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
				UUserWidget* Win = WM ? WM->FindWindow(FName(TEXT("Item_Upgrade_Win"))) : nullptr;
				UWidget* Cell = Win ? Win->GetWidgetFromName(FName(TEXT("Item_Upgrade_Item00_104_0"))) : nullptr;
				FVector2D Mn, Mx;
				if (!Cell || !AsdaUi::WidgetAbsRect(Cell, Mn, Mx)) { return false; }
				WM->SimulateClickAt((Mn + Mx) * 0.5f, /*HoldFrames*/ 3);
				return true;
			};
			auto ZOf = [W](const TCHAR* Id) -> int32
			{
				UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
				return WM ? WM->GetWindowZOrder(FName(Id)) : -1;
			};
			// С-2 + С-1: окно 78200 снова открыто кнопкой 20197 (поверх 20000), ответ 6501 → секунда блокировки.
			At(14.0f, [S, Up, Inv, Restock, Check, ZOf]()
			{
				// Окно инвентаря может лежать поверх ячеек 20000 — убираем, чтобы клик шёл именно в 20000.
				if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
				Restock();
				Up->OnSpecialClicked();
				S->OnGridCellClicked(0);
				Check(S->IsOpen() && ZOf(TEXT("Smelting3_Win")) > ZOf(TEXT("Item_Upgrade_Win")),
					FString::Printf(TEXT("С-2: 78200 открыт поверх 20000 (z %d > %d), владельца нет"), ZOf(TEXT("Smelting3_Win")), ZOf(TEXT("Item_Upgrade_Win"))));
			});
			At(14.2f, [S]() { TArray<uint8> B; B.SetNumZeroed(235); B[0] = 2; S->DebugInjectResult(B); });
			At(14.4f, [GI, Up, Check, ClickUpCell0]()
			{
				Check(AsdaUi::CheckRecentActivity(GI), TEXT("С-1: общая блокировка uiCheckRecentActivity истинна < 1000 мс после ответа 6501"));
				Up->ClearFxJournal();
				Check(ClickUpCell0(), TEXT("С-1: симклик по ячейке 0 окна 20000 заказан"));
			});
			At(15.0f, [Check, UpJournalHas, ZOf]()
			{
				Check(UpJournalHas(TEXT("клик отклонён: uiCheckRecentActivity")) && !UpJournalHas(TEXT("клик 0xB")),
					TEXT("С-1: клик по ячейке 20000 во время блокировки отклонён (путь игрока)"));
				Check(ZOf(TEXT("Item_Upgrade_Win")) > ZOf(TEXT("Smelting3_Win")),
					FString::Printf(TEXT("С-2: клик по 20000 поднял его над 78200 (z %d > %d)"), ZOf(TEXT("Item_Upgrade_Win")), ZOf(TEXT("Smelting3_Win"))));
			});
			At(15.6f, [GI, Up, Check, ClickUpCell0]()
			{
				Check(!AsdaUi::CheckRecentActivity(GI), TEXT("С-1: через 1 с после ответа блокировка снята"));
				Up->ClearFxJournal();
				ClickUpCell0();
			});
			At(16.2f, [Check, UpJournalHas]()
			{
				Check(UpJournalHas(TEXT("клик 0xB вид 0 ячейка 0")) && !UpJournalHas(TEXT("клик отклонён")),
					TEXT("С-1: после блокировки тот же клик по 20000 принят"));
			});
			// С-1 (защита): во время полосы ячейка сменила предмет — 6500 не уходит, окно освобождается.
			At(16.5f, [S, GI, Inv, IT, Ctx, Restock, Check]()
			{
				Restock();
				S->OnGridCellClicked(0);
				S->ClearJournal();
				S->OnOkClicked();
				Check(AsdaUi::CheckRecentActivity(GI), TEXT("С-1: блокировка истинна во время полосы (+4 «идёт»)"));
				const UDataTable* DT = IT->GetDataTable(FName(TEXT("refinehigh")));
				TArray<FAsdaRefinehighRow*> Rows; if (DT) { DT->GetAllRows<FAsdaRefinehighRow>(TEXT("HighRefineTest"), Rows); }
				for (const FAsdaRefinehighRow* R : Rows) { if (R && R->Idx == Ctx->Recipe) { Inv->DebugPutItem(1, 20, R->Mat1Code, 1); } }
			});
			At(17.3f, [S, Check, JournalIdx, Dump]()
			{
				Check(JournalIdx(TEXT("send 6500 отказ: код ячейки сменился"), 0) >= 0 && JournalIdx(TEXT("send 6500 OK"), 0) < 0
					&& JournalIdx(TEXT("send 6500 FAIL"), 0) < 0 && !S->DebugIsInProgress() && !S->DebugIsBusy(),
					TEXT("С-1: код ячейки сменился во время полосы → 6500 не отправлен, окно свободно"));
				Dump(TEXT("после отказа по смене предмета"));
			});
			// Д7-7: «ОК» в последние ≈190 мс эффекта 78272 (1600 мс) — новая попытка доживает до отправки.
			At(17.5f, [S, Restock, Check]()
			{
				Restock();
				S->OnGridCellClicked(0);
				S->ClearJournal();
				TArray<uint8> B; const bool bB = S->DebugBuildResult(1, B);
				Check(bB, TEXT("Д7-7: тело 6501 код 1 собрано"));
				if (bB) { S->DebugInjectResult(B); }
			});
			At(17.5f + 1.45f, [S]() { S->OnOkClicked(); });
			At(19.9f, [S, Check, JournalIdx, Dump]()
			{
				const int32 IFx = JournalIdx(TEXT("fx+ 78272"), 0);
				const int32 IBar = JournalIdx(TEXT("bar start"), IFx);
				const int32 IEnd = JournalIdx(TEXT("fx0x18 78272"), IBar);
				const int32 IKeep = JournalIdx(TEXT("новая попытка сохранена"), IEnd);
				const int32 IBarEnd = JournalIdx(TEXT("bar 0x18"), IKeep);
				const int32 ISend = JournalIdx(TEXT("send 6500"), IBarEnd);
				Check(IFx >= 0 && IBar > IFx && IEnd > IBar && IKeep > IEnd && IBarEnd > IKeep && ISend > IBarEnd,
					FString::Printf(TEXT("Д7-7: эффект → «ОК» → конец эффекта (попытка сохранена) → конец полосы → отправка (%d %d %d %d %d %d)"),
						IFx, IBar, IEnd, IKeep, IBarEnd, ISend));
				Dump(TEXT("Д7-7 исправлено"));
			});
			// Д7-7, откат = оригинал дословно: конец эффекта гасит новую полосу, 6500 не уходит.
			At(20.0f, [S, Restock]()
			{
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.EffectEndKeepsNewAttempt"), 0);
				Restock();
				S->OnGridCellClicked(0);
				S->ClearJournal();
				TArray<uint8> B; if (S->DebugBuildResult(1, B)) { S->DebugInjectResult(B); }
			});
			At(20.0f + 1.45f, [S]() { S->OnOkClicked(); });
			At(22.4f, [S, Check, JournalIdx, Dump]()
			{
				const int32 IBar = JournalIdx(TEXT("bar start"), 0);
				const int32 IEnd = JournalIdx(TEXT("fx0x18 78272"), IBar);
				// ★ Эталон прячет полосу `SetVisible(80, 0)` и ставит +4 = 0; дойдёт ли у скрытой полосы событие 0x18 —
				//   неважно: `case 78280` шлёт только при +4 ≠ 0. Проверяем исход: 6500 не ушёл, «идёт» = 0.
				Check(IBar >= 0 && IEnd > IBar && JournalIdx(TEXT("send 6500"), IBar) < 0 && !S->DebugIsInProgress(),
					TEXT("Д7-7 откат (EffectEndKeepsNewAttempt 0): как оригинал — конец эффекта сорвал попытку, 6500 не ушёл"));
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.EffectEndKeepsNewAttempt"), 1);
				Dump(TEXT("Д7-7 откат"));
			});
			// Таймаут Д7-6: после него окно не «занято» (закон «молчание сервера не даёт тупик»).
			At(22.6f, [S, Check]()
			{
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 1.f);
				S->DebugMarkInFlight();
				Check(S->DebugIsInFlight() && S->DebugIsBusy() && S->DebugIsInProgress(), TEXT("таймаут: «6500 ушёл» — окно занято, замок стоит"));
			});
			At(24.0f, [S, Check, JournalIdx]()
			{
				Check(!S->DebugIsInFlight() && !S->DebugIsBusy() && !S->DebugIsInProgress() && JournalIdx(TEXT("timeout 6501"), 0) >= 0,
					TEXT("таймаут Д7-6: замок снят и окно освобождено («занято»/«идёт» = 0) без «Отмены»"));
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 20.f);
			});
			// Звук id 5 — общая точка кнопок процедуры инвентаря: жмём НАСТОЯЩИЕ кнопки (OnClicked → реле).
			At(24.3f, [W, Inv, Check]()
			{
				UGameInstance* G = W->GetGameInstance();
				UAsdaWindowSubsystem* WM = G->GetSubsystem<UAsdaWindowSubsystem>();
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
				const int32 C0 = AsdaUi::GetInvenButtonSfxCount();
				int32 Pressed = 0;
				FString Missing;
				UUserWidget* InvW = WM ? WM->FindWindow(FName(TEXT("INVEN_WIN"))) : nullptr;
				const TCHAR* Names[] = { TEXT("INVEN_OPENBUT"), TEXT("INVEN_CLOSEBUT"), TEXT("INVEN_CHOICEBUT_OPT1"), TEXT("INVEN_CHOICEBUT_OPT0"),
					TEXT("Inven_Smelting_Btn"), TEXT("Inven_Disassembly_Btn"), TEXT("Inven_primium_item_btn") };
				for (const TCHAR* N : Names)
				{
					UButton* B = InvW ? Cast<UButton>(InvW->GetWidgetFromName(FName(N))) : nullptr;
					if (B) { B->OnClicked.Broadcast(); ++Pressed; } else { Missing += FString(N) + TEXT(" "); }
				}
				if (UAsdaItemResetSubsystem* Rs = G->GetSubsystem<UAsdaItemResetSubsystem>())
				{
					Rs->OpenWindow(UAsdaItemResetSubsystem::kModeFullReset);
					UUserWidget* RsW = WM ? WM->FindWindow(FName(TEXT("Item_Initializ_Win"))) : nullptr;
					UButton* B = RsW ? Cast<UButton>(RsW->GetWidgetFromName(FName(TEXT("Item_Initializ_Cancel")))) : nullptr;
					if (B) { B->OnClicked.Broadcast(); ++Pressed; } else { Missing += TEXT("Item_Initializ_Cancel "); }
				}
				const int32 D = AsdaUi::GetInvenButtonSfxCount() - C0;
				Check(Pressed == 8 && D == Pressed,
					FString::Printf(TEXT("звук id 5 (общая точка): нажато %d кнопок (402×2, 408, 412, 452, 453, 10082, 20370), звуков %d%s%s"),
						Pressed, D, Missing.IsEmpty() ? TEXT("") : TEXT(", не найдены: "), *Missing));
				// ★ Волна В6 ремесла (ревизия В5 СРЕДН-1): кнопка 452 открывает окно ремесла 76000, а при нём гейт эталона
				//   `uiInventoryOpenWindow` (ui_core.c:46608) молча не даёт открыть инвентарь — игра права, самотест нет.
				//   Закрываем 76000 его игровой точкой (`uiWindowHideAndCleanup`), чтобы проверки ниже шли с инвентарём.
				if (UAsdaCraftWindowSubsystem* Cw = G->GetSubsystem<UAsdaCraftWindowSubsystem>())
				{
					if (Cw->IsOpen()) { Cw->HideAndCleanup(); }
					Check(!Cw->IsOpen(), TEXT("В6: окно ремесла 76000, открытое кнопкой 452, закрыто игровой точкой"));
				}
				// ★ 11.10.2026 (В3 S1, загрязнение пакета): кнопки 453 и 10082 открывают окна разборки 32000 и элитного ящика — прежде
				//   они оставались открытыми до конца процесса, и в пакетном прогоне следующие самотесты получали отказы гейтов
				//   (заточка 20000 «открытие → ОТКАЗ», элитный магазин 6000 «открыто окно разборки»). Закрываются игровыми точками.
				if (UAsdaDisassemblySubsystem* Ds = G->GetSubsystem<UAsdaDisassemblySubsystem>()) { if (Ds->IsOpen()) { Ds->CloseWindow(); } }
				if (UAsdaPremiumPostSubsystem* Pp = G->GetSubsystem<UAsdaPremiumPostSubsystem>()) { Pp->CloseWindow(); }
			});
			// ★ Волна 10: 0xE (ПКМ) по взведённой иконке 20003 окна 20000 шлёт 6504 и во время блокировки
			//   `uiCheckRecentActivity` — в ветке 0xE эталона её нет (ui_core.c:48005-48034). Путь игрока:
			//   НАСТОЯЩИЙ правый симклик Slate по иконке; взвод — боевой: выбор → 20190 → конец 20070.
			At(24.8f, [W, Inv, Up]()
			{
				UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
				if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
				if (!Up->IsOpen()) { Up->ToggleWindow(); }
				if (WM) { WM->BringWindowToFront(FName(TEXT("Item_Upgrade_Win"))); }
				// Сумку очищаем, цель — в ячейку 0 (позиция 0 сетки 20000): ступень 3, попыток 1/5 (без диалога 72).
				for (int32 C = 0; C < 60; ++C) { Inv->DebugPutItem(1, C, 0, 0); }
				Inv->DebugPutItem(1, 0, 22818, 1);
				if (FAsdaItemInstance* It = Inv->GetCellMutable(1, 0))
				{
					if (It->Raw.Num() >= 57) { It->Raw[24] = 3; It->Raw[25] = 1; It->Raw[26] = 5; It->SyncFromRaw(); }
				}
				Inv->DebugPutItem(2, 55, 20936, 5);
			});
			At(25.1f, [W, GI, S, Up, Check, UpJournalHas]()
			{
				Up->OnGridCellClicked(0);
				Up->OnScrollCellClicked(0);
				Up->ClearFxJournal();
				Up->OnUpgradeClicked();                          // 20190 → 20070
				Up->DebugFinishEffect(EAsdaUpgradeFx::Effect1);  // 0x18 по 20070 → 20071/20072, иконка взведена
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 2.f);
				S->DebugMarkInFlight();                          // 78200 «идёт» ⇒ блокировка
				Check(UpJournalHas(TEXT("fx+ 20071")) && AsdaUi::CheckRecentActivity(GI),
					TEXT("0xE: иконка 20003 взведена, общая блокировка истинна"));
				UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
				UUserWidget* Win = WM ? WM->FindWindow(FName(TEXT("Item_Upgrade_Win"))) : nullptr;
				UWidget* Icon = Win ? Win->GetWidgetFromName(FName(TEXT("Item_Upgrade_Icon_20_0"))) : nullptr;
				FVector2D Mn, Mx;
				const bool bRect = Icon && AsdaUi::WidgetAbsRect(Icon, Mn, Mx);
				if (bRect) { WM->SimulateClickAt((Mn + Mx) * 0.5f, /*HoldFrames*/ 3, /*bRightButton*/ true); }
				Check(bRect, TEXT("0xE: правый симклик по иконке 20003 заказан"));
			});
			At(25.8f, [Up, Check, UpJournalHas]()
			{
				bool bOrder = false;
				int32 IClick = -1;
				const TArray<FString>& J = Up->GetFxJournal();
				for (int32 I = 0; I < J.Num(); ++I)
				{
					UE_LOG(LogAsdaHighRefine, Warning, TEXT("  журнал 20000 [0xE]: %s"), *J[I]);
					if (IClick < 0 && J[I].Contains(TEXT("клик 0xE (ПКМ) по 20003"))) { IClick = I; }
					else if (IClick >= 0 && J[I].Contains(TEXT("snd S_e_button10"))) { bOrder = true; }
				}
				Check(bOrder && !UpJournalHas(TEXT("клик отклонён")),
					TEXT("0xE: ПКМ по взведённой 20003 во время блокировки → тело клика («button10» → отправка 6504), без блокировки"));
			});
			At(27.6f, [S, Check]()
			{
				Check(!S->DebugIsInFlight() && !S->DebugIsBusy(), TEXT("0xE: 78200 освобождён таймаутом после проверки"));
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 20.f);
			});
			// ═══════════ ВОЛНА 11: МЕЛК-2 / МЕЛК-1 / СРЕДН-1 ═══════════
			// МЕЛК-2: все коды материалов ЖИВОЙ `DT_refinehigh` есть в item_list порта. У оригинала подсчёт
			//   `CInventory_CountItemById` по коду без записи разыменовал бы NULL (`->tradableFlag`) — в данных такого быть
			//   не должно; общая точка порта для такого кода считала бы ячейки расходников по одной.
			At(28.0f, [IT, Check]()
			{
				const UDataTable* DT = IT->GetDataTable(FName(TEXT("refinehigh")));
				TArray<FAsdaRefinehighRow*> Rows; if (DT) { DT->GetAllRows<FAsdaRefinehighRow>(TEXT("HighRefineTest"), Rows); }
				TSet<int32> Codes;     // уникальные коды материалов (> 0)
				TSet<int32> Missing;   // из них без записи item_list
				int32 Empty = 0;       // пустых (≤ 0) полей материалов
				for (const FAsdaRefinehighRow* R : Rows)
				{
					if (!R) { continue; }
					for (const int32 C : { R->Mat1Code, R->Mat2Code, R->Mat3Code })
					{
						if (C <= 0) { ++Empty; continue; }
						Codes.Add(C);
						if (!IT->GetItem(C)) { Missing.Add(C); }
					}
				}
				FString MissList;
				for (const int32 C : Missing) { MissList += FString::Printf(TEXT(" %d"), C); }
				Check(Rows.Num() > 0 && Missing.Num() == 0,
					FString::Printf(TEXT("МЕЛК-2: рецептов %d, кодов материалов %d (пустых полей %d), нет в item_list: %d%s"),
						Rows.Num(), Codes.Num(), Empty, Missing.Num(), *MissList));
			});
			// МЕЛК-1: пересоздание виджета 78200 во время полосы (6500 ещё не ушёл) — «идёт» снято, блокировки нет,
			//   при следующем открытии 6500 не уходит.
			At(28.2f, [S, Up, Inv, Restock]()
			{
				Inv->DebugPutItem(1, 0, 0, 0);   // цель проверки 0xE убрать — позиция 0 сетки 78200 снова наш предмет
				Restock();
				if (!S->IsOpen()) { Up->OnSpecialClicked(); }
			});
			At(28.5f, [S, GI, Ctx, Check, Dump]()
			{
				S->OnGridCellClicked(0);
				S->ClearJournal();
				S->OnOkClicked();
				const bool bBar = S->DebugIsInProgress() && !S->DebugIsInFlight();
				S->DebugResetWidget();
				Check(bBar && S->GetRecipeKey() == Ctx->Recipe && !S->DebugIsInProgress() && !S->IsOpen() && !AsdaUi::CheckRecentActivity(GI),
					FString::Printf(TEXT("МЕЛК-1: полоса шла (%d), виджет пересоздан → «идёт» снято, окно закрыто, блокировки нет"), bBar ? 1 : 0));
				if (!bBar) { Dump(TEXT("МЕЛК-1: полоса не началась")); }
			});
			At(28.8f, [S, Up, Check]()
			{
				Up->OnSpecialClicked();   // 20197 — повторное открытие (виджет создаётся заново)
				Check(S->IsOpen(), TEXT("МЕЛК-1: 78200 открыт снова после пересоздания виджета"));
			});
			At(29.6f, [S, Check, JournalIdx, Dump]()
			{
				Check(JournalIdx(TEXT("reset widget"), 0) >= 0 && JournalIdx(TEXT("send 6500"), 0) < 0 && !S->DebugIsInProgress(),
					TEXT("МЕЛК-1: после повторного открытия 6500 по сорванной полосе не ушёл"));
				Dump(TEXT("МЕЛК-1"));
			});
			// СРЕДН-1: двойной щелчок «снять вещь» по НАДЕТОМУ слоту во время блокировки проходит — у эталона
			//   слоты 600…610 обслуживает `uiControlMessageProc` (ui_core.c:34823) без `uiCheckRecentActivity`.
			//   Путь игрока: два НАСТОЯЩИХ ЛКМ-симклика Slate по иконке слота, второй — сразу после отпускания первого.
			//   Контрольная пара: тот же двойной щелчок БЕЗ блокировки (доказывает, что путь клика рабочий), затем С ней.
			struct FEqCtx
			{
				int32 Slot = -1;   // надетый слот (= itemType предмета)
				int32 U0 = 0;      // счётчик «снять» перед очередной парой щелчков
				bool bPump = false;   // крутить ли покадровый разбор мыши инвентаря (офлайн: павна нет — `Tick` его не зовёт)
			};
			TSharedRef<FEqCtx> Eq = MakeShared<FEqCtx>();
			// Двойной щелчок по иконке надетого слота: первый клик сейчас, второй — как только первый доставлен
			//   (заказ поверх незавершённого затёр бы первый). Возвращает «иконка найдена, клик заказан».
			auto DoubleClickEquip = [GI, Inv, Eq]() -> bool
			{
				UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
				UWidget* Icon = Inv->DebugGetEquipSlotIcon(Eq->Slot);
				FVector2D Mn, Mx;
				if (!WM || !Icon || !AsdaUi::WidgetAbsRect(Icon, Mn, Mx)) { return false; }
				const FVector2D P = (Mn + Mx) * 0.5f;
				Eq->U0 = Inv->DebugGetUnequipRequests();
				WM->SimulateClickAt(P, /*HoldFrames*/ 2);
				TWeakObjectPtr<UAsdaWindowSubsystem> WeakWM(WM);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWM, P](float) -> bool
				{
					UAsdaWindowSubsystem* M = WeakWM.Get();
					if (!M) { return false; }
					if (M->IsSimClickPending()) { return true; }   // первый ещё не отпущен
					M->SimulateClickAt(P, /*HoldFrames*/ 2);
					return false;                                   // второй заказан — тикер снят
				}));
				return true;
			};
			At(30.0f, [GI, Inv, IT, Eq, Check]()
			{
				UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
				const FAsdaItemRecord* R = IT->GetItem(22818);
				Eq->Slot = R ? R->ItemType : -1;
				if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
				if (WM) { WM->BringWindowToFront(FName(TEXT("INVEN_WIN"))); }
				if (Inv->IsEquipSlotUsable(Eq->Slot)) { Inv->DebugPutItem(3, Eq->Slot, 22818, 1); }
				// Офлайн-прогон без павна: `UAsdaInventorySubsystem::Tick` выходит до разбора мыши («живёт только в мире»),
				//   поэтому БОЕВОЙ `TickPointer` крутим покадрово сами на время проверки.
				Eq->bPump = true;
				TWeakObjectPtr<UAsdaInventorySubsystem> WeakInv(Inv);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakInv, Eq](float) -> bool
				{
					UAsdaInventorySubsystem* I = WeakInv.Get();
					if (!I || !Eq->bPump) { return false; }
					I->DebugTickPointer();
					return true;
				}));
				UWidget* Icon = Inv->DebugGetEquipSlotIcon(Eq->Slot);
				Check(Inv->IsEquipSlotUsable(Eq->Slot) && Icon && !AsdaUi::CheckRecentActivity(GI),
					FString::Printf(TEXT("СРЕДН-1: в надетый слот %d положен 22818 (иконка %s), блокировки нет"),
						Eq->Slot, Icon ? *Icon->GetName() : TEXT("нет")));
			});
			At(30.3f, [DoubleClickEquip, Check]() { Check(DoubleClickEquip(), TEXT("СРЕДН-1 контроль: двойной щелчок по надетому заказан")); });
			At(31.0f, [GI, S, Inv, Eq, Check]()
			{
				const int32 D = Inv->DebugGetUnequipRequests() - Eq->U0;
				Check(D == 1, FString::Printf(TEXT("СРЕДН-1 контроль: без блокировки двойной щелчок по слоту %d дошёл до «снять» (запросов %d)"), Eq->Slot, D));
				UE_LOG(LogAsdaHighRefine, Warning, TEXT("%s"), *Inv->DumpSelection());
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 3.f);
				S->DebugMarkInFlight();   // 78200 «идёт» ⇒ общая блокировка
				Check(AsdaUi::CheckRecentActivity(GI), TEXT("СРЕДН-1: общая блокировка истинна (78200 «идёт»)"));
			});
			At(31.3f, [DoubleClickEquip, Check]() { Check(DoubleClickEquip(), TEXT("СРЕДН-1: двойной щелчок по надетому во время блокировки заказан")); });
			At(32.0f, [GI, Inv, Eq, Check]()
			{
				const int32 D = Inv->DebugGetUnequipRequests() - Eq->U0;
				const bool bLock = AsdaUi::CheckRecentActivity(GI);
				Check(D == 1 && bLock,
					FString::Printf(TEXT("СРЕДН-1: двойной щелчок по надетому слоту %d во время блокировки дошёл до «снять» (запросов %d, блокировка %d)"),
						Eq->Slot, D, bLock ? 1 : 0));
			});
			At(34.6f, [S, Inv, Eq, Check]()
			{
				Eq->bPump = false;   // покадровый разбор мыши инвентаря больше не нужен — тикер снимется сам
				Check(!S->DebugIsInFlight() && !S->DebugIsBusy(), TEXT("СРЕДН-1: 78200 освобождён таймаутом после проверки"));
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 20.f);
				if (Inv->IsEquipSlotUsable(Eq->Slot)) { Inv->DebugPutItem(3, Eq->Slot, 0, 0); }
			});
			// ═══════════ ★ ВОЛНА 12: ПОЛОСА ПРОКРУТКИ 78226 — ПУТЁМ ИГРОКА (настоящие клики/перетаскивание Slate) ═══════════
			//   Геометрия — живая (`GetCachedGeometry`), клики — общий симклик окна (курсор ведётся событиями Slate,
			//   опрос мыши окна видит и фронт ЛКМ, и `GetCursorPos`).
			auto ScrCenter = [S](int32 Plane, float FracY) -> FVector2D
			{
				FVector2D Mn(-1, -1), Mx(-1, -1);
				if (UWidget* P = S->DebugGetScrollPlane(Plane)) { AsdaUi::WidgetAbsRect(P, Mn, Mx); }
				return FVector2D((Mn.X + Mx.X) * 0.5f, Mn.Y + (Mx.Y - Mn.Y) * FracY);
			};
			auto ScrClick = [W, ScrCenter](int32 Plane, float FracY)
			{
				if (UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
				{
					WM->SimulateClickAt(ScrCenter(Plane, FracY), /*HoldFrames*/ 3);
				}
			};
			auto PageEvents = [S](const TCHAR* Src) -> int32
			{
				int32 N = 0;
				for (const FString& L : S->GetJournal()) { if (L.Contains(TEXT("page ")) && L.Contains(Src)) { ++N; } }
				return N;
			};
			// Шаг С1: окно закрыть (инвентарь и 20000 не должны лежать поверх), в сумку — 20 предметов снаряжения (2 страницы).
			At(35.0f, [S, Inv]()
			{
				if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
				S->OnCloseClicked();
			});
			At(36.2f, [S, Inv, Up, Ctx, Check, Dump]()
			{
				for (int32 C = 0; C < UAsdaInventorySubsystem::kShellSlots; ++C) { Inv->DebugPutItem(1, C, C < 20 ? Ctx->Code : 0, C < 20 ? 1 : 0); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
				Up->OnSpecialClicked();   // кнопка 20197 — окно 78200 снова наверху
				Check(S->IsOpen(), TEXT("полоса: окно 78200 открыто кнопкой 20197 с 20 предметами снаряжения"));
				Check(S->DebugGetPage() == 0 && S->GetMaxPage() == 1,
					FString::Printf(TEXT("полоса: страница %d, последняя %d (20 предметов = 2 страницы)"), S->DebugGetPage(), S->GetMaxPage()));
				S->ClearJournal();
			});
			At(36.8f, [W, S, Check]()
			{
				GEngine->Exec(W, TEXT("asda.UI.TreeGeom hr_scroll WBP_Smelting3_Win"));
				UE_LOG(LogAsdaHighRefine, Warning, TEXT("САМОТЕСТ-78200: %s"), *S->DumpListScroll());
				FVector2D UMn = FVector2D::ZeroVector, UMx = FVector2D::ZeroVector, DMn = FVector2D::ZeroVector, DMx = FVector2D::ZeroVector;   // ★ R3-c чистка 12.11.2026: C4701
				FVector2D TMn = FVector2D::ZeroVector, TMx = FVector2D::ZeroVector, RMn = FVector2D::ZeroVector, RMx = FVector2D::ZeroVector;   //   (читаются только при bGeo — все заполнены)
				UWidget* Tr = S->DebugGetScrollPlane(0);
				UWidget* Upl = S->DebugGetScrollPlane(1);
				UWidget* Dn = S->DebugGetScrollPlane(2);
				UWidget* Th = S->DebugGetScrollPlane(3);
				const bool bGeo = Tr && Upl && Dn && Th && AsdaUi::WidgetAbsRect(Tr, RMn, RMx) && AsdaUi::WidgetAbsRect(Upl, UMn, UMx)
					&& AsdaUi::WidgetAbsRect(Dn, DMn, DMx) && AsdaUi::WidgetAbsRect(Th, TMn, TMx);
				Check(bGeo && S->DebugIsScrollShown(), TEXT("полоса: 4 плана из ассета на экране (живая геометрия)"));
				if (!bGeo) { return; }
				// Масштаб экрана = ширина стрелки / 11 (ширина планов скина `Nomal_Scr`).
				const float K = (float)(UMx.X - UMn.X) / 11.f;
				Check(K > 0.f && FMath::IsNearlyEqual((float)(UMx.Y - UMn.Y), 11.f * K, 1.f) && FMath::IsNearlyEqual((float)(DMx.Y - DMn.Y), 11.f * K, 1.f),
					FString::Printf(TEXT("полоса: стрелки 11×11 (масштаб %.3f)"), K));
				Check(FMath::IsNearlyEqual((float)(DMx.Y - UMn.Y), 66.f * K, 1.5f),
					FString::Printf(TEXT("полоса: высота = высоте зоны 78226 (66) — %.1f px экрана"), (DMx.Y - UMn.Y) / K));
				Check(FMath::IsNearlyEqual((float)(TMx.Y - TMn.Y), 22.f * K, 1.5f) && FMath::IsNearlyEqual((float)TMn.Y, (float)UMx.Y, 1.f),
					FString::Printf(TEXT("полоса: бегунок = половина дорожки (44 × 1/2 = 22) и стоит вверху — длина %.1f"), (TMx.Y - TMn.Y) / K));
			});
			At(37.0f, [Shot]() { Shot(TEXT("asda_highrefine_scroll_p0")); });
			// Шаг С2: выбор предмета, клик по стрелке ↓ → страница 2, выбор сброшен (`uiResetTradeWindow`), бегунок внизу.
			At(37.4f, [S, ScrClick]()
			{
				S->OnGridCellClicked(0);
				ScrClick(2, 0.5f);
			});
			At(38.1f, [S, Check, PageEvents, ScrCenter]()
			{
				const FVector2D T = ScrCenter(3, 0.f), D = ScrCenter(2, 0.f);
				Check(S->DebugGetPage() == 1 && PageEvents(TEXT("(полоса)")) == 1,
					FString::Printf(TEXT("полоса: клик по ↓ → страница %d (ждали 1), событий 0x17: %d"), S->DebugGetPage(), PageEvents(TEXT("(полоса)"))));
				Check(S->GetRecipeKey() == -1, TEXT("полоса: смена страницы сбросила выбор (ветка 0x17 → uiResetTradeWindow)"));
				Check(S->DebugGetScrollPlane(3) && FMath::Abs((float)(ScrCenter(3, 1.f).Y - D.Y)) <= 1.5f,
					FString::Printf(TEXT("полоса: бегунок ушёл вниз (низ бегунка %.0f = верх ↓ %.0f)"), ScrCenter(3, 1.f).Y, D.Y));
				(void)T;
				S->ClearJournal();
			});
			At(38.2f, [Shot]() { Shot(TEXT("asda_highrefine_scroll_p1")); });
			// Шаг С3: ↓ на последней странице — события нет; ↑ → страница 1.
			At(38.5f, [ScrClick]() { ScrClick(2, 0.5f); });
			At(39.0f, [S, Check, PageEvents, ScrClick]()
			{
				Check(S->DebugGetPage() == 1 && PageEvents(TEXT("(полоса)")) == 0, TEXT("полоса: ↓ на последней странице — страница та же, события нет"));
				ScrClick(1, 0.5f);
			});
			At(39.5f, [S, Check, PageEvents, ScrClick]()
			{
				Check(S->DebugGetPage() == 0 && PageEvents(TEXT("(полоса)")) == 1, FString::Printf(TEXT("полоса: клик по ↑ → страница %d (ждали 0)"), S->DebugGetPage()));
				S->ClearJournal();
				ScrClick(0, 0.92f);   // дорожка НИЖЕ бегунка — «страница вниз» (69636)
			});
			At(40.0f, [S, Check, PageEvents, ScrCenter, W]()
			{
				Check(S->DebugGetPage() == 1 && PageEvents(TEXT("(полоса)")) == 1, FString::Printf(TEXT("полоса: клик по дорожке под бегунком → страница %d (ждали 1)"), S->DebugGetPage()));
				S->ClearJournal();
				// Перетаскивание бегунка к верху дорожки (69637) — страница 0.
				if (UAsdaWindowSubsystem* WM = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
				{
					WM->SimulateDrag(ScrCenter(3, 0.5f), ScrCenter(0, 0.05f), /*HoldFrames*/ 12);
				}
			});
			At(40.8f, [S, Check, PageEvents, W]()
			{
				Check(S->DebugGetPage() == 0 && PageEvents(TEXT("(бегунок)")) >= 1,
					FString::Printf(TEXT("полоса: перетаскивание бегунка вверх → страница %d (ждали 0), событий %d"), S->DebugGetPage(), PageEvents(TEXT("(бегунок)"))));
				S->ClearJournal();
				// Колесо (Д7-3) двигает и бегунок: курсор над окном (последний симклик — на полосе).
				S->OnWheel(-1);
				Check(S->DebugGetPage() == 1 && PageEvents(TEXT("(колесо)")) == 1, TEXT("полоса: колесо вниз → страница 1"));
			});
			At(41.0f, [S, Check, ScrCenter]()
			{
				Check(FMath::Abs((float)(ScrCenter(3, 1.f).Y - ScrCenter(2, 0.f).Y)) <= 1.5f, TEXT("полоса: после колеса бегунок внизу (полоса отражает страницу)"));
				// «Занято» (+2): клик по ↑ съедается — как колесо (волна 9).
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 1.0f);
				S->DebugMarkInFlight();
				S->ClearJournal();
			});
			At(41.1f, [ScrClick]() { ScrClick(1, 0.5f); });
			At(41.6f, [S, Check, PageEvents]()
			{
				bool bEaten = false; for (const FString& L : S->GetJournal()) { bEaten |= L.Contains(TEXT("полоса съедена")); }
				Check(S->DebugGetPage() == 1 && bEaten && PageEvents(TEXT("(полоса)")) == 0,
					FString::Printf(TEXT("полоса: при «занято» клик по ↑ съеден (страница %d, журнал «съедена» %d)"), S->DebugGetPage(), bEaten ? 1 : 0));
			});
			At(42.6f, [S, Check]()
			{
				Check(!S->DebugIsBusy(), TEXT("полоса: «занято» снято таймаутом"));
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 20.f);
				// ★ Волна 13 (СРЕДН-1): выбор на 2-й странице, затем ключ Д7-3 = 0 НА ХОДУ — полосы нет, как у оригинала,
				//   и окно обязано вернуться к одной странице (сетка/рецепт/отправка — страница 1).
				S->OnGridCellClicked(1);
				Check(S->DebugGetPage() == 1 && S->GetRecipeKey() >= 0, FString::Printf(TEXT("ключ на ходу: выбор на 2-й странице (страница %d, рецепт %d)"), S->DebugGetPage(), S->GetRecipeKey()));
				S->ClearJournal();
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.WheelPages"), 0);
			});
			At(42.9f, [S, Check, PageEvents]()
			{
				Check(!S->DebugIsScrollShown(), TEXT("полоса: ключ asda.HighRefine.WheelPages 0 — полоса скрыта (как оригинал)"));
				Check(S->DebugGetPage() == 0 && S->DebugGetSelCell() == -1 && S->GetRecipeKey() == -1 && PageEvents(TEXT("(ключ Д7-3 = 0)")) == 1,
					FString::Printf(TEXT("ключ 0 на ходу со 2-й страницы → страница %d, выбор %d, рецепт %d (ждали 0/−1/−1, событие 0x17 «ключ»)"),
						S->DebugGetPage(), S->DebugGetSelCell(), S->GetRecipeKey()));
				// При ключе 0 выбор ячейки 1 = предмет №2 списка (страница 1), и тело 6500 — тот же предмет.
				S->OnGridCellClicked(1);
				TArray<uint8> Body;
				const bool bB = S->DebugBuildRequest(Body);
				Check(bB && S->DebugGetPage() == 0 && AsdaBytes::RdI16(Body.GetData() + 4) == S->DebugGetListCell(1),
					FString::Printf(TEXT("ключ 0: ячейка 1 → 6500 @4 = слот %d (ждали %d = список[1])"),
						bB ? (int32)AsdaBytes::RdI16(Body.GetData() + 4) : -1, S->DebugGetListCell(1)));
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.WheelPages"), 1);
			});
			At(43.2f, [S, Check, Inv]()
			{
				Check(S->DebugIsScrollShown(), TEXT("полоса: ключ 1 — полоса снова на экране"));
				// 30 предметов — 3 страницы (у эталона диапазон жёстко 2, [ПОРТ] по списку): бегунок 1/3 дорожки.
				for (int32 C = 0; C < UAsdaInventorySubsystem::kShellSlots; ++C) { Inv->DebugPutItem(1, C, 0, 0); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
				S->OnCloseClicked();
			});
			At(44.4f, [S, Up, Inv, Ctx, Check]()
			{
				for (int32 C = 0; C < UAsdaInventorySubsystem::kShellSlots; ++C) { Inv->DebugPutItem(1, C, C < 30 ? Ctx->Code : 0, C < 30 ? 1 : 0); }   // ★ 29.10.2026: 30 предметов, хвост 30..59 пуст
				Up->OnSpecialClicked();
				Check(S->IsOpen() && S->GetMaxPage() == 2, FString::Printf(TEXT("полоса: 30 предметов → последняя страница %d (ждали 2)"), S->GetMaxPage()));
			});
			At(44.8f, [S, Check, ScrCenter]()
			{
				const float Len = (float)(ScrCenter(3, 1.f).Y - ScrCenter(3, 0.f).Y);
				const float Tr = (float)(ScrCenter(0, 1.f).Y - ScrCenter(0, 0.f).Y);
				// ★ Волна 13 (М-1): эталон `(unsigned __int64)(44 × 1/3)` = 14 — дробная часть отбрасывается (не 15).
				const float K = Tr / 44.f;
				Check(Tr > 0.f && FMath::IsNearlyEqual(Len, 14.f * K, 0.75f), FString::Printf(TEXT("полоса: бегунок = ⌊44/3⌋ = 14 (%.1f / дорожка %.1f)"), Len / K, Tr / K));
				UE_LOG(LogAsdaHighRefine, Warning, TEXT("САМОТЕСТ-78200: %s"), *S->DumpListScroll());
			});
			At(45.0f, [Shot]() { Shot(TEXT("asda_highrefine_scroll_3pages")); });
			// ═══════════ ★ ВОЛНА 13 (М-8): ОТПРАВКА 6500 СО 2-Й СТРАНИЦЫ — БОЕВЫМ ПУТЁМ (ОК → полоса → 0x18 → отправка) ═══════════
			//   20 предметов снаряжения (2 страницы) и материалы на местах; ↓ полосы → стр. 2, ячейка 2 = список[16];
			//   тело перехватывается на `SendSecured` (в сеть не уходит), «занято» снимает таймаут.
			At(45.3f, [S, Inv, Ctx]()
			{
				S->OnCloseClicked();
				for (int32 C = 0; C < UAsdaInventorySubsystem::kShellSlots; ++C) { Inv->DebugPutItem(1, C, C < 20 ? Ctx->Code : 0, C < 20 ? 1 : 0); }   // ★ 29.10.2026 (инвентарь ×2): вся сумка 60
				for (int32 M = 0; M < 3; ++M) { Inv->DebugPutItem(Ctx->MatCont[M], Ctx->MatCell[M], Ctx->MatCode[M], Ctx->MatNeed[M]); }
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 1.0f);
			});
			At(45.9f, [S, Up, Check]()
			{
				Up->OnSpecialClicked();
				Check(S->IsOpen() && S->GetMaxPage() >= 1, FString::Printf(TEXT("6500 со 2-й страницы: окно открыто, последняя страница %d"), S->GetMaxPage()));
			});
			At(46.2f, [ScrClick]() { ScrClick(2, 0.5f); });
			At(46.7f, [S, Ctx, Check]()
			{
				S->OnGridCellClicked(2);
				const int32 Want = S->DebugGetListCell(14 + 2);
				Check(S->DebugGetPage() == 1 && S->GetRecipeKey() == Ctx->Recipe && Want == 16,
					FString::Printf(TEXT("6500 со 2-й страницы: страница %d, ячейка 2 = список[16] = слот %d, рецепт %d (ждали 1/16/%d)"),
						S->DebugGetPage(), Want, S->GetRecipeKey(), Ctx->Recipe));
				S->ClearJournal();
				// ★ Волна 14 (МЕЛК-3): перехват не ставится при живом соединении — тогда и «ОК» не жмём (6500 ушёл бы на сервер).
				if (S->DebugCapture6500(true)) { S->OnOkClicked(); }   // боевой путь: полоса ≈388 мс → 0x18 → `SendCraftRequest`
				else { Check(false, TEXT("6500 со 2-й страницы: перехват не поставлен (живое соединение/занят) — шаг не проверен")); }
			});
			At(47.6f, [S, Ctx, Check]()
			{
				const TArray<TArray<uint8>> Sent = S->DebugGetSent6500();
				// ★ Волна 14 (МЕЛК-3): перехватчик обязан сняться САМ сразу после первого 6500 — до этой строки.
				Check(!S->DebugIsCapture6500Armed() && !S->DebugNetHasSendCapture(),
					FString::Printf(TEXT("перехват 6500 снят сам после первого пакета: поставлен=%d, у сети=%d (ждали 0/0)"),
						S->DebugIsCapture6500Armed() ? 1 : 0, S->DebugNetHasSendCapture() ? 1 : 0));
				S->DebugCapture6500(false);
				const bool bOne = Sent.Num() == 1 && Sent[0].Num() == 40;
				bool bRefused = false; for (const FString& L : S->GetJournal()) { bRefused |= L.Contains(TEXT("отказ")); }
				Check(bOne && !bRefused && AsdaBytes::RdI32(Sent[0].GetData()) == Ctx->Recipe
					&& AsdaBytes::RdI16(Sent[0].GetData() + 4) == S->DebugGetListCell(16) && Sent[0][6] == 1,
					FString::Printf(TEXT("6500 со 2-й страницы УШЁЛ боевым путём: тел %d, @0 рецепт %d, @4 слот %d, @6 контейнер %d (ждали %d/16/1)"),
						Sent.Num(), bOne ? AsdaBytes::RdI32(Sent[0].GetData()) : -1, bOne ? (int32)AsdaBytes::RdI16(Sent[0].GetData() + 4) : -1,
						bOne ? (int32)Sent[0][6] : -1, Ctx->Recipe));
			});
			At(49.0f, [S, Check]()
			{
				Check(!S->DebugIsBusy() && !S->DebugIsInFlight(), TEXT("6500 со 2-й страницы: «занято» снято таймаутом"));
				AsdaCVarSave::SetForTest(TEXT("asda.HighRefine.ReplyTimeoutSec"), 20.f);
			});
			// ═══════════ ★ ВОЛНА 14 (МЕЛК-3): ПРЕРВАННЫЙ ЗАХВАТ НЕ ОСТАВЛЯЕТ ПЕРЕХВАТЧИК ═══════════
			//   Ставим перехват и 6500 НЕ шлём (как прерванный прогон): узкий перехват «связь» не подменяет,
			//   а через `kCapture6500TimeoutSec` (5 с) `Tick` снимает его сам.
			// ═══════════ ★ ВОЛНА 14 (МЕЛК-1): ДЛИНА БЕГУНКА = float-АРИФМЕТИКА `uiScrollbarSetRange` ═══════════
			//   Эталонные значения — модель x87 с точностью 24 бита (`Reports/gear_upgrade/wave14_fixes.md`, скрипт
			//   `wave14_thumb24.py` там же): три пары ревизии (гача/звания/склад) и пары, где прежняя формула порта ⌊len·(a2/a3)⌋ ошибалась.
			At(49.1f, [Check]()
			{
				struct FCase { int32 L, A2, A3, Want; };
				static const FCase Cases[] = {
					{ 312, 1, 3, 104 }, { 253, 12, 23, 132 }, { 143, 5, 11, 65 }, { 312, 5, 28, 55 },
					{ 44, 13, 22, 26 }, { 175, 29, 35, 144 }, { 197, 9, 197, 8 }, { 44, 1, 2, 22 }, { 44, 1, 3, 14 }, { 10, 1, 5, 6 },
				};
				FString Bad;
				for (const FCase& K : Cases)
				{
					const int32 Got = FAsdaScrollBarNomal::EtalonThumbLen(K.L, K.A2, K.A3);
					if (Got != K.Want) { Bad += FString::Printf(TEXT(" %d·%d/%d=%d(ждали %d)"), K.L, K.A2, K.A3, Got, K.Want); }
				}
				Check(Bad.IsEmpty(), FString::Printf(TEXT("длина бегунка по float-арифметике эталона: %d пар%s"),
					(int32)UE_ARRAY_COUNT(Cases), Bad.IsEmpty() ? TEXT(" совпали") : *Bad));
			});
			At(49.2f, [S, Check]()
			{
				const bool bArmed = S->DebugCapture6500(true);
				Check(bArmed && S->DebugNetHasSendCapture() && !S->DebugNetLinkFakedByCapture(),
					FString::Printf(TEXT("узкий перехват 6500: поставлен=%d, у сети=%d, «связь» подменена=%d (ждали 1/1/0)"),
						bArmed ? 1 : 0, S->DebugNetHasSendCapture() ? 1 : 0, S->DebugNetLinkFakedByCapture() ? 1 : 0));
			});
			At(55.0f, [S, Check, Inv, Ctx]()
			{
				// ★ Ревизия О4 (МЕЛК-4): деньги и ячейка 20551 — в исходное.
				Inv->SetGoldDisplay(Ctx->GoldWas);
				for (int32 i = 0; i < 60 && Ctx->GoldCellWas >= 0; ++i)
				{
					FAsdaItemInstance* G = Inv->GetCellMutable(2, i);
					if (G && G->ItemCode == 20551) { G->SetAmount(Ctx->GoldCellWas); break; }
				}
				Check(!S->DebugIsCapture6500Armed() && !S->DebugNetHasSendCapture(),
					FString::Printf(TEXT("прерванный захват: перехватчик снят таймаутом (поставлен=%d, у сети=%d, ждали 0/0)"),
						S->DebugIsCapture6500Armed() ? 1 : 0, S->DebugNetHasSendCapture() ? 1 : 0));
			});
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): финальный шаг — ключи прогона (HighRefine.ReplyTimeoutSec, EffectEndKeepsNewAttempt, WheelPages) к
			//   исходному значению И приоритету SetBy. Прежде шаги «возвращали» их константами 20/1/1 с приоритетом «из консоли»
			//   (промежуточные 20 с между парами таймаута оставлены — это значение НА ВРЕМЯ прогона).
			At(55.2f, []() { AsdaCVarSave::RestoreAll(TEXT("asda.HighRefine.SelfTest")); });
			// ★ 11.10.2026 (В3 S1, загрязнение пакета): окна 78200 и 20000, открытые прогоном, — закрыть (прежде оставались открытыми,
			//   и в пакетном прогоне `asda.Upgrade.SelfTest` своим тумблером ЗАКРЫВАЛ 20000 вместо открытия — «открытие → ОТКАЗ»).
			At(55.3f, [W]()
			{
				UGameInstance* G = W ? W->GetGameInstance() : nullptr;
				if (UAsdaHighRefineSubsystem* Hr = G ? G->GetSubsystem<UAsdaHighRefineSubsystem>() : nullptr) { if (Hr->IsOpen()) { Hr->CloseWindow(); } }
				if (UAsdaItemUpgradeSubsystem* Ug = G ? G->GetSubsystem<UAsdaItemUpgradeSubsystem>() : nullptr) { if (Ug->IsOpen()) { Ug->ToggleWindow(); } }
			});
			if (bQuit)
			{
				At(55.5f, []()
				{
					UE_LOG(LogAsdaHighRefine, Warning, TEXT("САМОТЕСТ-78200: завершён"));
					if (GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
				});
			}
		}));
}
#endif // ASDA_DEV_CONSOLE
