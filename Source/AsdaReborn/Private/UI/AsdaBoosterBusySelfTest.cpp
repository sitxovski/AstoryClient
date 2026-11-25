// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НЕСКОЛЬКО БУСТЕРОВ ПОДРЯД» (05.11.2026, баг игроков, волна booster-loot): `asda.SelfTest.BoosterBusy [quit]`.
//   Баг: второй бустер, открытый, пока у первого идёт анимация/транзакция, ломал сумку — выпавший предмет не виден, ячейки пустые
//   до перезапуска. Причина: двойной щелчок по второму бустеру снова звал `UAsdaBoosterPanelSubsystem::OpenWindow`, тот обнулял
//   буфер 5469 первого (`Packet68`/`bHavePacket`) и выбранную ячейку — приз не ложился в модель, потраченный бустер оставался.
//   Эталон: открытие барабана закрывает сумку (`sowelWindowClose`, item_inventory.c:43103), двойной щелчок по сумке при поле 254
//   отклоняется нотисом 13106 (ui_core.c:47996). Ключи: `asda.Booster.BagUseGate`, `asda.Booster.OpenClosesBag` [ОРИГИНАЛ],
//   `asda.Booster.BusyReopenGuard` [ИСПРАВЛЕНИЕ].
//   Путь игрока (закон `ui-check-game-path-geometry`): предметы — в ячейки сумки боевой точкой `ApplyExternalRecord`; двойной щелчок
//   и кнопки окна 10050 — настоящими событиями мыши Slate по живой геометрии; 5468 — боевым отправителем под перехватом (в сеть не
//   уходит); 5469 — штатным приёмом `DebugFeedPacket`. Офлайн (-AsdaNoNet). Разделы:
//     A. бустер A (75834, ячейка 0) → окно 10050, сумка закрыта; «Начать» → ровно один 5468 (код 75834, 1:0);
//     B. «клик во время анимации»: сумка снова открыта, двойной щелчок по бустеру B (75825, ячейка 1) — окно не перезапущено,
//        второго 5468 нет, B в сумке;
//     C. «ответ с задержкой»: 5469 успеха для A приходит после щелчка по B; ещё один щелчок по B до OK — снова отклонён; кнопка OK →
//        в модели ячейка 0 пуста, приз 75676 в ячейке 10, иконка ячейки 10 нарисована, ячейка 0 без иконки;
//     R. те же шаги при трёх ключах 0 (прежний порт): щелчок по B после 5469 перезапускает окно, крестик → приз 75716 в ячейку 11
//        НЕ лёг, бустер A остался в ячейке 0 — баг воспроизведён;
//     F. отказ: бустер B → «Начать» → 5469 с кодом 3 → окно закрыто, B на месте.
//   Кадры — `Reports/booster_bundles/2026-11-05/frames/<язык>_busy_<раздел>.png`. Сумка, ключи и окна возвращаются в конце.
//   Итог: «БУСТЕР-ЗАНЯТ ИТОГ: ok=N fail=M». Карта мира (`--map world`), язык — `--lang ru|en`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaShopDblClick.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "AsdaBytes.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoosterBusyTest, Log, All);

namespace AsdaBoosterBusyTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.BoosterBusy");   // имя команды
	static const TCHAR* kPfx = TEXT("БУСТЕР-ЗАНЯТ");                // префикс строк и итога
	static const FName kDrumId(TEXT("BOOSTING_WIN"));                 // окно барабана 10050

	static constexpr int32 kBoostA   = 75834;   // бустер A (строка гачи 3946)
	static constexpr int32 kBoostB   = 75825;   // бустер B (строка 3937)
	static constexpr int32 kCellA    = 0;       // ячейка сумки A
	static constexpr int32 kCellB    = 1;       // ячейка сумки B
	static constexpr int32 kPrize1   = 75676;   // приз A в разделе C («Шлем ловкача», строка 3946)
	static constexpr int32 kPrizeC1  = 10;      // его ячейка
	static constexpr int32 kPrize2   = 75716;   // приз A в разделе R («Ботинки Асуры», строка 3946)
	static constexpr int32 kPrizeC2  = 11;      // его ячейка
	static const TCHAR* kKeys[] = { TEXT("asda.Booster.BagUseGate"), TEXT("asda.Booster.OpenClosesBag"), TEXT("asda.Booster.BusyReopenGuard") };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		FString FrameDir;                          // папка кадров
		TMap<int32, TArray<uint8>> Saved;          // ячейка сумки → сырая запись до прогона
		TArray<TArray<uint8>> Sent5468;            // перехваченные тела 5468
		uint32 CapGen = 0;                         // поколение перехвата
		TSharedPtr<FAsdaNetSelfScope> Self;        // свой персонаж (уровень)
		double WaitStart = 0.0;                    // начало ожидания кнопки OK

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaBoosterBusyTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaBoosterBusyTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живой геометрии узла.
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Настоящий щелчок Slate по узлу окна барабана (по началу имени). false — узла нет на экране.
	static bool ClickDrum(const FCtxRef& C, const TCHAR* Pfx)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(kDrumId) : nullptr;
		UWidget* N = Win ? AsdaUi::FindNodeByPrefix(Win, Pfx) : nullptr;
		FVector2D P;
		if (!WM || !Center(N, P)) { return false; }
		Hover(P);
		WM->SimulateClickAt(P, /*HoldFrames*/ 2);
		return true;
	}
	static bool DrumNodeShown(const FCtxRef& C, const TCHAR* Pfx)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(kDrumId) : nullptr;
		FVector2D P;
		return Win && Center(AsdaUi::FindNodeByPrefix(Win, Pfx), P);
	}
	// Сумка открыта на вкладке «Снаряжение».
	static void OpenBag(const FCtxRef& C)
	{
		if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>()) { if (!I->IsWindowOpen()) { I->OpenWindow(); } I->SetTab(1); }
	}
	// Двойной щелчок по иконке ячейки сумки.
	static bool DblBag(const FCtxRef& C, int32 Cell)
	{
		UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		FVector2D P;
		if (!I || !Center(I->DebugGetGridIcon(Cell), P)) { return false; }
		Hover(P);
		AsdaSimDoubleClickAt(C->Sub<UAsdaWindowSubsystem>(), P, false);
		return true;
	}
	// Положить предмет (0 — очистить) в ячейку сумки, запомнив прежнюю запись.
	static void Put(const FCtxRef& C, int32 Cell, int32 Code)
	{
		UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		if (!I) { return; }
		if (!C->Saved.Contains(Cell))
		{
			const FAsdaItemInstance* It = I->GetCell(1, Cell);
			C->Saved.Add(Cell, (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>());
		}
		I->ApplyExternalRecord(Code > 0 ? FAsdaItemRecordWriter::Cell(Code, 1, Cell, 1) : FAsdaItemRecordWriter::ClearCell(1, Cell),
			UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	static int32 CodeAt(const FCtxRef& C, int32 Cell)
	{
		const UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		const FAsdaItemInstance* It = I ? I->GetCell(1, Cell) : nullptr;
		return (It && !It->IsEmpty()) ? It->ItemCode : 0;
	}
	// Иконка ячейки нарисована (виден узел и у картинки есть ресурс кисти).
	static bool IconDrawn(const FCtxRef& C, int32 Cell)
	{
		const UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		const UImage* Img = I ? Cast<UImage>(I->DebugGetGridIcon(Cell)) : nullptr;
		return Img && AsdaUi::IsShown(Img) && Img->GetBrush().GetResourceObject() != nullptr;
	}
	// Тело 5469 (68 б): код результата, потраченный бустер (код/контейнер/ячейка), запись приза 57 б с +9, вес с +66.
	static TArray<uint8> Body5469(int32 Result, int32 Spent, int32 SpentCell, int32 Prize, int32 PrizeCell)
	{
		TArray<uint8> B; B.SetNumZeroed(68);
		B[0] = (uint8)Result;
		AsdaBytes::WrI32(&B[2], Spent); B[6] = 1; AsdaBytes::WrI16(&B[7], SpentCell);
		if (Prize > 0)
		{
			const TArray<uint8> R = FAsdaItemRecordWriter::Cell(Prize, 1, PrizeCell, 1);
			FMemory::Memcpy(&B[9], R.GetData(), FMath::Min(R.Num(), 57));
		}
		AsdaBytes::WrI16(&B[66], 100);
		return B;
	}
	static void Feed5469(const FCtxRef& C, const TArray<uint8>& B)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5469, B); }
	}
	static void Shot(const FCtxRef& C, const TCHAR* Tag)
	{
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_busy_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), Tag);
		UE_LOG(LogAsdaBoosterBusyTest, Log, TEXT("%s: КАДР → %s"), kPfx, *File);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}
	static void SetKeys(const TCHAR* V) { for (const TCHAR* K : kKeys) { AsdaCVarSave::SetForTest(K, V); } }
	static void CloseDrum(const FCtxRef& C)
	{
		if (UAsdaBoosterPanelSubsystem* B = C->Sub<UAsdaBoosterPanelSubsystem>()) { if (B->IsWindowOpen()) { B->OnCloseButton(); } }
	}

	static void Finish(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		CloseDrum(C);
		if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>())
		{
			for (const TPair<int32, TArray<uint8>>& S : C->Saved)
			{
				I->ApplyExternalRecord(S.Value.Num() == FAsdaItemRecordWriter::kSize ? S.Value : FAsdaItemRecordWriter::ClearCell(1, S.Key),
					UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
			if (I->IsWindowOpen()) { I->CloseWindow(); }
		}
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); } }
		if (C->Self) { C->Self->Restore(); }
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaBoosterBusyTest, kPfx, C->Ok, C->Fail,
			*FString::Printf(TEXT(" (перехвачено 5468: %d)"), C->Sent5468.Num()));
	}

	// Ждать кнопку OK барабана (появляется после остановки и «взрыва»), затем Then; потолок 20 с.
	static void WaitOk(const FCtxRef& C, TFunction<void(bool)> Then)
	{
		if (C->WaitStart <= 0.0) { C->WaitStart = FPlatformTime::Seconds(); }
		if (DrumNodeShown(C, TEXT("BOOSTER_OK"))) { C->WaitStart = 0.0; Then(true); return; }
		if (FPlatformTime::Seconds() - C->WaitStart > 20.0) { C->WaitStart = 0.0; Then(false); return; }
		At(C, 0.25f, [C, Then]() { WaitOk(C, Then); });
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Net || !C->Sub<UAsdaInventorySubsystem>() || !C->Sub<UAsdaBoosterPanelSubsystem>() || !C->Sub<UAsdaWindowSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы сети/инвентаря/барабана/окон есть"));
			Finish(C);
			return;
		}
		C->Self = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelfLevel = 70;
		TWeakPtr<FCtx> WC = C;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { if (Op == 5468 && P && Len >= 7) { X->Sent5468.Add(TArray<uint8>(P, 7)); } }
		}, 5468, kCmd);
		if (C->CapGen == 0) { C->Expect(false, TEXT("перехват отправок свободен")); Finish(C); return; }
		SetKeys(TEXT("1"));

		// ═══ A ═══
		Put(C, kCellA, kBoostA); Put(C, kCellB, kBoostB); Put(C, kPrizeC1, 0); Put(C, kPrizeC2, 0);
		OpenBag(C);
		At(C, 0.8f, [C]() { C->Expect(DblBag(C, kCellA), TEXT("A0 двойной щелчок по бустеру A 75834 (ячейка 0)")); });
		At(C, 1.7f, [C]()
		{
			const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			const UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			C->Expect(Bp->IsWindowOpen(), TEXT("A1 окно барабана 10050 открыто"));
			C->Expect(!I->IsWindowOpen(), TEXT("A2 сумка закрыта открытием барабана (`sowelWindowClose`, asda.Booster.OpenClosesBag)"));
			C->Expect(ClickDrum(C, TEXT("BOOSTER_START")), TEXT("A3 щелчок «Начать» (10069)"));
		});
		At(C, 2.5f, [C]()
		{
			const bool b1 = C->Sent5468.Num() == 1;
			const int32 Code = b1 ? AsdaBytes::RdI32(C->Sent5468[0].GetData()) : -1;
			const int32 Slot = b1 ? AsdaBytes::RdI16(C->Sent5468[0].GetData() + 5) : -1;
			C->Expect(b1 && Code == kBoostA && C->Sent5468[0][4] == 1 && Slot == kCellA,
				FString::Printf(TEXT("A4 ровно один 5468: код %d, контейнер %d, ячейка %d (ждём 75834, 1, 0)"), Code, b1 ? C->Sent5468[0][4] : -1, Slot));
			OpenBag(C);
		});
		// ═══ B: щелчок по второму бустеру во время анимации ═══
		At(C, 3.3f, [C]() { C->Expect(DblBag(C, kCellB), TEXT("B0 сумка снова открыта — двойной щелчок по бустеру B 75825 во время анимации")); });
		At(C, 4.1f, [C]()
		{
			const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			C->Expect(Bp->IsWindowOpen() && !DrumNodeShown(C, TEXT("BOOSTER_START")),
				TEXT("B1 окно барабана не перезапущено (кнопки «Начать» нет — идёт процесс бустера A)"));
			C->Expect(C->Sent5468.Num() == 1, FString::Printf(TEXT("B2 второго 5468 нет (всего %d)"), C->Sent5468.Num()));
			C->Expect(CodeAt(C, kCellB) == kBoostB && CodeAt(C, kCellA) == kBoostA, TEXT("B3 оба бустера на месте до ответа сервера"));
			Shot(C, TEXT("B_second_refused"));
			// ═══ C: ответ с задержкой ═══
			Feed5469(C, Body5469(0, kBoostA, kCellA, kPrize1, kPrizeC1));
		});
		At(C, 4.9f, [C]()
		{
			OpenBag(C);
		});
		At(C, 5.5f, [C]() { C->Expect(DblBag(C, kCellB), TEXT("C0 после 5469 (до OK) — ещё щелчок по бустеру B")); });
		At(C, 6.3f, [C]()
		{
			C->Expect(C->Sub<UAsdaBoosterPanelSubsystem>()->IsWindowOpen() && C->Sent5468.Num() == 1,
				TEXT("C1 окно не перезапущено и 5468 не ушёл — результат A цел"));
			WaitOk(C, [C](bool bOkShown)
			{
				C->Expect(bOkShown && ClickDrum(C, TEXT("BOOSTER_OK")), TEXT("C2 кнопка OK (10070) появилась и нажата"));
				At(C, 0.8f, [C]()
				{
					OpenBag(C);
					At(C, 0.6f, [C]()
					{
						C->Expect(!C->Sub<UAsdaBoosterPanelSubsystem>()->IsWindowOpen(), TEXT("C3 окно барабана закрыто"));
						C->Expect(CodeAt(C, kCellA) == 0 && CodeAt(C, kPrizeC1) == kPrize1 && CodeAt(C, kCellB) == kBoostB,
							FString::Printf(TEXT("C4 модель: ячейка 0 пуста (%d), приз 75676 в ячейке 10 (%d), бустер B на месте (%d)"),
								CodeAt(C, kCellA), CodeAt(C, kPrizeC1), CodeAt(C, kCellB)));
						C->Expect(IconDrawn(C, kPrizeC1) && !IconDrawn(C, kCellA) && IconDrawn(C, kCellB),
							TEXT("C5 картинка: иконка приза в ячейке 10 нарисована, ячейка 0 пуста, бустер B виден"));
						Shot(C, TEXT("C_prize_in_bag"));
						// ═══ R: прежний порт (ключи 0) — баг ═══
						// Раздел R начинается после кадра C (снимок берётся в конце кадра — иначе на нём уже бустер A).
						At(C, 0.3f, [C]() { SetKeys(TEXT("0")); Put(C, kCellA, kBoostA); });
						At(C, 0.5f, [C]() { DblBag(C, kCellA); });
						At(C, 1.3f, [C]() { C->Expect(ClickDrum(C, TEXT("BOOSTER_START")), TEXT("R0 ключи 0: бустер A → «Начать»")); });
						At(C, 2.0f, [C]() { Feed5469(C, Body5469(0, kBoostA, kCellA, kPrize2, kPrizeC2)); });
						At(C, 2.6f, [C]() { DblBag(C, kCellB); });
						At(C, 3.4f, [C]()
						{
							CloseDrum(C);
							C->Expect(CodeAt(C, kPrizeC2) == 0 && CodeAt(C, kCellA) == kBoostA,
								FString::Printf(TEXT("R1 ключи 0: щелчок по B после 5469 перезапустил окно — приз 75716 НЕ в ячейке 11 (%d), "
								     "бустер A остался «призраком» в ячейке 0 (%d) — баг воспроизведён"), CodeAt(C, kPrizeC2), CodeAt(C, kCellA)));
							Shot(C, TEXT("R_old_port_lost_prize"));
							SetKeys(TEXT("1"));
							Put(C, kCellA, 0);
							OpenBag(C);
						});
						// ═══ F: отказ сервера ═══
						At(C, 4.2f, [C]() { DblBag(C, kCellB); });
						At(C, 5.0f, [C]() { C->Expect(ClickDrum(C, TEXT("BOOSTER_START")), TEXT("F0 бустер B → «Начать»")); });
						At(C, 5.7f, [C]() { Feed5469(C, Body5469(3, kBoostB, kCellB, 0, 0)); });
						At(C, 6.4f, [C]()
						{
							C->Expect(!C->Sub<UAsdaBoosterPanelSubsystem>()->IsWindowOpen() && CodeAt(C, kCellB) == kBoostB,
								TEXT("F1 отказ 5469 (код 3): окно закрыто, бустер B на месте"));
							Finish(C);
						});
					});
				});
			});
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBoosterBusy(
	TEXT("asda.SelfTest.BoosterBusy"),
	TEXT("Самотест «несколько бустеров подряд» (05.11.2026): второй бустер во время анимации/ответа первого, ответ с задержкой, отказ; "
	     "пара с прежним портом (ключи 0). Путь игрока, офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBoosterBusyTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/booster_bundles/2026-11-05/frames"));
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
