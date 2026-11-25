// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «САМОЦВЕТ ВЫШЕ УРОВНЯ ПЕРСОНАЖА» (05.11.2026, баг-репорт игроков «клиент зависает, когда
//   вставляешь/меняешь Sowel, уровень которого выше уровня персонажа»). [ПОРТ]
//
//   ЧТО ПРОВЕРЯЕТ — ПУТЁМ ИГРОКА (законы ui-check-game-path-geometry, diagnostic-must-use-real-path): клики, двойные
//   щелчки, перетаскивания и ПКМ — НАСТОЯЩИЕ события Slate (`UAsdaWindowSubsystem::SimulateClickAt/SimulateDrag`) по
//   живой геометрии; 5024 уходит БОЕВЫМ отправителем под перехватом исходящих (`AsdaSelfTestKit::ArmCapture`, в сеть не
//   уходит ничего); отказ сервера 5025 подаётся ШТАТНЫМ разбором приёмника (`UAsdaNetSubsystem::DebugFeedPacket`) — тело
//   ровно как у `CharCellobItemMove@0x4DC390` (GameServer.c:87064): 117 байт, @0 = код, остальное нули
//   (`sub_4DE250@0x4DE250` → `sub_4DE530@0x4DE530`: уровень камня (строка камня +32) выше уровня персонажа ⇒ код 10).
//     A. Пустое гнездо: перенос «особого» камня высокого уровня на гнездо → 5024 (гнездо 2, старый 0) → отказ 10.
//     B. Замена главного (гнездо 0 занято): двойной щелчок по обычному камню высокого уровня → 5024 (гнездо 0) → отказ 10.
//     C. Обмен через окно выбора гнезда 1530 (все дополнительные гнёзда заняты): перенос особого камня → 1530 → щелчок по
//        камню окна → «OK» → 5024 (выбранное гнездо, старый камень) → отказ 10.
//     D. Молчание сервера: 5024 ушёл, ответа нет — повтор блокируется до таймаута ожидания, после — снова уходит.
//     E. ПКМ по камню списка и по гнезду — пакета нет.
//     F. Все коды отказа 5025 (0, 2…11, 15) штатным приёмником.
//   После КАЖДОГО отказа: (1) игровой поток жив — счётчик кадров растёт (сторож в отдельном потоке, при застое > 4 с
//   печатает стек игрового потока); (2) модального окна НЕТ (оригинал `netRecvItemActionResult@0xD0FB10` network.c:17506
//   выводит коды 2…11/15 в ЧАТ цветом −11514, окно-сообщение — только у кода 0); (3) строка отказа в чате та, что у
//   оригинала; (4) запись предмета и камень в расходниках не изменились; (5) ввод жив — щелчок по камню списка выбирает его.
//   Состояние (сумка 0…59, расходники 0…59, окно, перехват) восстанавливается в конце.
//
// Консоль: `asda.SelfTest.SowelLevel [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`), на карте мира.
//   Строки проверок — «САМОЦВЕТ-УРОВЕНЬ ok/FAIL», итог — «САМОЦВЕТ-УРОВЕНЬ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход, третья запись): шаги по событию «ввод доставлен», а не по часам

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaSowelSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaNotice.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "UI/AsdaTipPanel.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSowelLvlTest, Log, All);

namespace AsdaSowelLevelTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.SowelLevel");   // имя команды (и ключ реестра ключей консоли)
	static const TCHAR* kTag = TEXT("САМОЦВЕТ-УРОВЕНЬ");            // префикс строк журнала

	static constexpr int32 kBagCell     = 0;    // ячейка сумки с предметом-носителем
	static constexpr int32 kHighNormal  = 50;   // расходники 50 — обычный камень ВЫШЕ уровня (замена гнезда 0)
	static constexpr int32 kHighSpecial = 51;   // расходники 51 — «особый» камень ВЫШЕ уровня (свободное гнездо / окно 1530)
	static constexpr int32 kLowNormal   = 52;   // расходники 52 — обычный камень по уровню (проверка «ввод жив»)

	// ── Сторож игрового потока: отдельный поток следит за счётчиком кадров; застой > StallS — стек игрового потока в журнал ──
	class FFrameWatchdog : public FRunnable
	{
	public:
		FThreadSafeBool bStop = false;            // просьба остановиться
		FThreadSafeCounter Stalls;                // сколько раз ловили застой
		double StallS = 4.0;                      // порог застоя, с
		uint32 Run() override
		{
			uint64 LastFrame = GFrameCounter;
			double LastMoveS = FPlatformTime::Seconds();
			bool bReported = false;
			while (!bStop)
			{
				FPlatformProcess::Sleep(0.25f);
				const uint64 Now = GFrameCounter;
				if (Now != LastFrame) { LastFrame = Now; LastMoveS = FPlatformTime::Seconds(); bReported = false; continue; }
				if (!bReported && FPlatformTime::Seconds() - LastMoveS > StallS)
				{
					bReported = true;
					Stalls.Increment();
					// Стек игрового потока снимаем ИЗ ЭТОГО потока (игровой стоит) и печатаем символами.
					uint64 Frames[64] = {};
					const int32 N = FPlatformStackWalk::CaptureThreadStackBackTrace(GGameThreadId, Frames, 64);
					UE_LOG(LogAsdaSowelLvlTest, Error, TEXT("%s СТОРОЖ: игровой поток стоит %.1f с (кадр %llu), стек (%d):"),
						kTag, FPlatformTime::Seconds() - LastMoveS, Now, N);
					for (int32 i = 0; i < N; ++i)
					{
						ANSICHAR Buf[1024] = {};
						FPlatformStackWalk::ProgramCounterToHumanReadableString(i, Frames[i], Buf, sizeof(Buf));
						UE_LOG(LogAsdaSowelLvlTest, Error, TEXT("%s СТОРОЖ   %s"), kTag, ANSI_TO_TCHAR(Buf));
					}
					GLog->Flush();
				}
			}
			return 0;
		}
	};

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		TSharedPtr<AsdaSelfTestSeq::FSeq> Seq;     // ★ R5-fix-b 13.11.2026 (заключительный заход, третья запись): очередь шагов расписания
		bool bQuit = false;                        // выйти после итога
		TArray<TArray<uint8>> SavedBag;            // сумка до прогона (пусто — ячейка была пуста)
		TArray<TArray<uint8>> SavedSpend;          // расходники 0…59 до прогона
		uint32 CapGen = 0;                         // поколение перехвата отправок
		TArray<TPair<int32, TArray<uint8>>> Sent;  // перехваченные отправки (опкод, тело)
		int32 Level = 0;                           // уровень своего персонажа в прогоне
		int32 ItemCode = 0, ItemType = -1;         // предмет-носитель (броня, 4 гнезда)
		int32 LowNormal = 0;                       // обычный камень по уровню (стоит в гнезде 0 и лежит в 2:52)
		int32 HighNormal = 0;                      // обычный камень выше уровня
		int32 HighSpecial = 0;                     // особый камень выше уровня
		int32 LowSpecial = 0;                      // особый камень по уровню (заполняет гнёзда 1…3 для окна 1530)
		int32 HighLevelReq = 0;                    // уровень камня HighNormal (журнал)
		uint64 FrameMark = 0;                      // кадр в момент подачи отказа
		int32 N5024Mark = 0;                       // число 5024 к моменту шага
		TArray<uint8> ItemBefore;                  // запись предмета до отказа
		FFrameWatchdog* Dog = nullptr;             // сторож
		FRunnableThread* DogThread = nullptr;      // его поток

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaSowelLvlTest, Log,   TEXT("%s ok   %s"), kTag, *What); }
			else   { ++Fail; UE_LOG(LogAsdaSowelLvlTest, Error, TEXT("%s FAIL %s"), kTag, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд.
	// ★ R5-fix-b 13.11.2026 (заключительный заход, третья запись): расписание — общая очередь `AsdaSelfTestSeq::At` (запись та же): шаг ждёт доставки заказанного
	//   щелчка/перетаскивания и тот же промежуток после предыдущего шага. В `baseline3` [ru] под нагрузкой мировой таймер
	//   проверял раньше события (110/5: F2/F7 «кадров 9», I1 «окно 1530 не открыто» и каскад); одиночно — 115/0.
	using AsdaSelfTestSeq::At;

	// Центр живой геометрии узла (false — не отрисован / скрыт / нулевой размер).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).

	// Двойной щелчок НАСТОЯЩИМИ событиями Slate: второй — как только первый доставлен.
	static void DoubleClick(UAsdaWindowSubsystem* WM, const FVector2D& P)
	{
		if (!WM) { return; }
		WM->SimulateClickAt(P, 1);
		TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, P](float) -> bool
		{
			UAsdaWindowSubsystem* W2 = WW.Get();
			if (!W2) { return false; }
			if (W2->IsSimClickPending()) { return true; }
			W2->SimulateClickAt(P, 1);
			return false;
		}), 0.f);
	}

	static int32 CountSent(const FCtxRef& C, int32 Op, TArray<uint8>* OutLast)
	{
		int32 N = 0;
		for (const TPair<int32, TArray<uint8>>& P : C->Sent) { if (P.Key == Op) { ++N; if (OutLast) { *OutLast = P.Value; } } }
		return N;
	}

	static void PutRec(const FCtxRef& C, const TArray<uint8>& R)
	{
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>()) { Inv->ApplyExternalRecord(R, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
	}

	// Носитель: гнездо 0 — обычный камень по уровню; гнёзда 1…3 — особые по уровню, если bFull, иначе 1 занят, 2/3 пусты.
	static void PutTarget(const FCtxRef& C, bool bFull)
	{
		FAsdaItemRecordWriter Rw;
		Rw.Code(C->ItemCode).Container(1).Slot(kBagCell).Second(-1).Amount(1).Durability(33).StackWeight(10)
		  .Sowel(0, C->LowNormal).Sowel(1, C->LowSpecial).Byte26(5).Byte25(2);
		if (bFull) { Rw.Sowel(2, C->LowSpecial).Sowel(3, C->LowSpecial); }
		PutRec(C, Rw.ToArray());
	}

	static UWidget* GridCell(const FCtxRef& C, int32 Col)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UUserWidget* Win = Inv ? Inv->GetWindowWidget() : nullptr;
		return Win ? AsdaUi::FindNodeByName(Win, TEXT(""), *FString::Printf(TEXT("INVEN_SET%d_"), Col)) : nullptr;
	}

	// Колонка списка, в которой лежит камень из ячейки расходников Cell (−1 — нет на текущей странице).
	static int32 TrayColOf(const FCtxRef& C, int32 SpendCell)
	{
		UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
		if (!S) { return -1; }
		const TArray<FAsdaSowelListEntry>& L = S->GetList();
		const int32 Page = S->GetListPage();
		for (int32 i = 0; i < L.Num(); ++i)
		{
			if (L[i].SpendCell == SpendCell) { return (i / 6 == Page) ? (i % 6) : -1; }
		}
		return -1;
	}

	// Подобрать предмет и камни по ДАННЫМ: байт-цель камня = тип предмета, остальные цели — 15 «нет цели».
	static bool PickData(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!IT) { return false; }
		for (int32 Code = 20551; Code <= 79428 && C->ItemCode == 0; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (!R || R->ItemType < 0 || R->ItemType > 4 || R->EnchantOrSocket != 3 || R->DurationOrEnchant != 0 || R->CategoryFlag == 3) { continue; }
			C->ItemCode = Code; C->ItemType = R->ItemType;
		}
		if (C->ItemCode == 0) { return false; }
		for (int32 Code = 7001; Code <= 10925; ++Code)
		{
			FAsdaSowellRow Row;
			if (!IT->GetSowell(Code, Row)) { continue; }
			if (Row.SocketTargetType != C->ItemType || !C->Sub<UAsdaSowelSubsystem>()->DebugIsCompatible(Code, C->ItemCode, C->ItemType)) { continue; }
			// Клиент уровень камня не проверяет (проверяет сервер), поэтому «по уровню» — просто ПЕРВЫЙ подходящий камень
			//   (офлайн уровень персонажа 0, а камней ниже 1-го уровня нет); «выше уровня» — следующий, чей уровень > уровня персонажа.
			const bool bHigh = Row.RequiredLevel > C->Level;
			if (Row.SpecialFlag == 0)
			{
				if (!C->LowNormal) { C->LowNormal = Code; }
				else if (bHigh && !C->HighNormal) { C->HighNormal = Code; C->HighLevelReq = Row.RequiredLevel; }
			}
			else
			{
				if (!C->LowSpecial) { C->LowSpecial = Code; }
				else if (bHigh && !C->HighSpecial) { C->HighSpecial = Code; }
			}
		}
		return C->HighNormal && C->HighSpecial && C->LowNormal && C->LowSpecial;
	}

	// Подать отказ 5025 ШТАТНЫМ приёмником (тело 117 б, @0 = код — как у сервера).
	static void FeedRefusal(const FCtxRef& C, int32 Code)
	{
		TArray<uint8> B; B.SetNumZeroed(117);
		B[0] = (uint8)Code;
		C->FrameMark = GFrameCounter;
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5025, B, 0); }
	}

	// Строка нотиса оригинала для кода 5025 (network.c:17526-17578).
	static int32 RefusalLoc(int32 Code)
	{
		switch (Code)
		{
		case 0: return 2311; case 2: return 1215; case 4: return 1216; case 5: return 2045; case 6: return 1217;
		case 7: return 1218; case 8: return 1219; case 9: return 8013; case 10: return 1220; case 11: return 1221;
		case 15: return 111303; default: return 0;
		}
	}

	// ★ R5-fix-b 13.11.2026 (заключительный заход, третья запись): шаг проверки после отказа — через Sec с и когда с подачи отказа прошло ≥10 кадров (порог её
	//   проверки «поток жив»). При ~15 к/с (три игры) 0,6 с давали 9 кадров — ложный провал F2/F7. Потолок 4 с — порог застоя
	//   сторожа: не дождались — проверка та же и падает (застой виден в её строке).
	static void AfterRefusal(const FCtxRef& C, float Sec, TFunction<void()> Fn)
	{
		AsdaSelfTestSeq::When(C, Sec, [C]() { return GFrameCounter - C->FrameMark >= 10; }, 4.f, MoveTemp(Fn));
	}

	// Проверки после отказа: поток жив, модалки нет (кроме кода 0), строка в чате, записи не тронуты, ввод жив.
	static void CheckAfterRefusal(const FCtxRef& C, const TCHAR* Step, int32 Code, int32 StoneCell)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		const uint64 Frames = GFrameCounter - C->FrameMark;
		C->Expect(Frames >= 10, FString::Printf(TEXT("%s игровой поток жив после отказа %d: кадров прошло %llu, застоев сторожа %d"),
			Step, Code, Frames, C->Dog ? C->Dog->Stalls.GetValue() : -1));
		const bool bModal = WM && WM->IsAnyModalOpen();
		const UUserWidget* Top = WM ? WM->GetTopModalWindow() : nullptr;
		if (Code == 0)
		{
			C->Expect(bModal, FString::Printf(TEXT("%s код 0 — окно-сообщение 2311, как оригинал (модалка %s)"), Step, Top ? *Top->GetName() : TEXT("нет")));
			// Закрываем окно-сообщение НАСТОЯЩИМ щелчком по его «OK» (у 1500 — `ONEBUT_BUT00`).
			FVector2D Ok;
			UWidget* Btn = Top ? AsdaUi::FindNodeByName(Top, TEXT("ONEBUT_BUT00"), TEXT("ONEBUT_BUT00")) : nullptr;
			if (WM && Center(Btn, Ok)) { WM->SimulateClickAt(Ok, 2); }
		}
		else
		{
			C->Expect(!bModal, FString::Printf(TEXT("%s код %d — модального окна НЕТ, ввод не заперт (оригинал — строка в чате; модалка: %s)"),
				Step, Code, Top ? *Top->GetName() : TEXT("нет")));
			const FString Want = AsdaNotice::LocRaw(C->GI(), RefusalLoc(Code), false);
			const FString Got = S ? S->DebugLastNotice() : FString();
			const FColor Gold(0xFF, 0xD3, 0x06, 0xFF);
			// Строки нет в таблицах игры (111303) — у оригинала в чат не уходит ничего.
			C->Expect(Want.IsEmpty() ? Got.IsEmpty() : (Got == Want && S->DebugLastNoticeColor() == Gold),
				FString::Printf(TEXT("%s код %d — в чат строка %d «%s» цветом −11514 (получено «%s»)"), Step, Code, RefusalLoc(Code), *Want, *Got));
		}
		const FAsdaItemInstance* It = Inv ? Inv->GetCell(1, kBagCell) : nullptr;
		C->Expect(It && It->Raw == C->ItemBefore, FString::Printf(TEXT("%s запись предмета-носителя не изменилась (гнёзда %d/%d/%d/%d)"),
			Step, It ? It->Sowel0 : -9, It ? It->Sowel1 : -9, It ? It->Sowel2 : -9, It ? It->Sowel3 : -9));
		if (StoneCell >= 0)
		{
			const FAsdaItemInstance* St = Inv ? Inv->GetCell(2, StoneCell) : nullptr;
			C->Expect(St && !St->IsEmpty(), FString::Printf(TEXT("%s камень остался в расходниках 2:%d (код %d)"), Step, StoneCell, St ? St->ItemCode : 0));
		}
	}

	// «Ввод жив»: настоящий щелчок по камню по уровню выбирает его.
	static void ClickLowStone(const FCtxRef& C)
	{
		// Сначала снимаем выбор (тот же вход, что клик по предмету), чтобы «выбран» означало именно этот щелчок.
		C->Sub<UAsdaSowelSubsystem>()->OnItemSelected(1, kBagCell);
		FVector2D P;
		const int32 Col = TrayColOf(C, kLowNormal);
		if (Col >= 0 && Center(C->Sub<UAsdaSowelSubsystem>()->DebugTrayIcon(Col), P)) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2); }
	}
	static void CheckLowStoneSelected(const FCtxRef& C, const TCHAR* Step)
	{
		UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
		const int32 Col = TrayColOf(C, kLowNormal);
		C->Expect(S && Col >= 0 && S->DebugPick() == EAsdaSowelPick::Stone,
			FString::Printf(TEXT("%s ввод жив: щелчок по камню по уровню (колонка %d) выбрал камень"), Step, Col));
	}

	static void Finish(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>()) { S->DebugClearAwaiting(); }
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideSowelStonesWindow(); }
		if (Net && C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; }
		if (Inv && C->SavedBag.Num() == UAsdaInventorySubsystem::kShellSlots && C->SavedSpend.Num() == 60)
		{
			for (int32 I = 0; I < UAsdaInventorySubsystem::kShellSlots; ++I) { if (C->SavedBag[I].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedBag[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(1, I); } }
			for (int32 I = 0; I < 60; ++I) { if (C->SavedSpend[I].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedSpend[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); } else { Inv->ClearCell(2, I); } }
		}
		if (Inv && Inv->IsWindowOpen()) { Inv->CloseWindow(); }
		if (C->Dog) { C->Expect(C->Dog->Stalls.GetValue() == 0, FString::Printf(TEXT("Z сторож: застоев игрового потока за прогон %d"), C->Dog->Stalls.GetValue())); }
		if (C->DogThread) { C->Dog->bStop = true; C->DogThread->WaitForCompletion(); delete C->DogThread; C->DogThread = nullptr; }
		delete C->Dog; C->Dog = nullptr;
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaSowelLvlTest, kTag, C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Inv || !S || !WM || !Net || !C->Sub<UAsdaItemTooltipSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы инвентаря/самоцветов/окон/сети/подсказок есть"));
			Finish(C);
			return;
		}
		C->Dog = new FFrameWatchdog();
		C->DogThread = FRunnableThread::Create(C->Dog, TEXT("AsdaSowelLevelWatchdog"));
		C->Level = AsdaSelf::GetLevel(C->GI());
		if (!PickData(C))
		{
			C->Expect(false, FString::Printf(TEXT("данные: броня с 4 гнёздами и камни выше/по уровню %d (нашли: обычн. выше %d, особ. выше %d, обычн. %d, особ. %d)"),
				C->Level, C->HighNormal, C->HighSpecial, C->LowNormal, C->LowSpecial));
			Finish(C);
			return;
		}
		C->Expect(true, FString::Printf(TEXT("данные: уровень персонажа %d; предмет %d (тип %d); камни выше уровня: обычный %d (ур. %d), особый %d; по уровню: %d, %d; язык %s"),
			C->Level, C->ItemCode, C->ItemType, C->HighNormal, C->HighLevelReq, C->HighSpecial, C->LowNormal, C->LowSpecial, FAsdaLang::Code(FAsdaLang::Current())));
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { TArray<uint8> B; if (P && Len > 0) { B.Append(P, Len); } X->Sent.Emplace((int32)Op, MoveTemp(B)); }
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват отправок свободен")); Finish(C); return; }

		for (int32 I = 0; I < UAsdaInventorySubsystem::kShellSlots; ++I) { const FAsdaItemInstance* X = Inv->GetCell(1, I); C->SavedBag.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		for (int32 I = 0; I < 60; ++I) { const FAsdaItemInstance* X = Inv->GetCell(2, I); C->SavedSpend.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		for (int32 I = 0; I < 60; ++I) { Inv->ClearCell(2, I); }
		PutTarget(C, /*bFull*/ false);
		PutRec(C, FAsdaItemRecordWriter::Cell(C->HighNormal, 2, kHighNormal, 1));
		PutRec(C, FAsdaItemRecordWriter::Cell(C->HighSpecial, 2, kHighSpecial, 1));
		PutRec(C, FAsdaItemRecordWriter::Cell(C->LowNormal, 2, kLowNormal, 1));
		if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
		Inv->SetTab(1);

		// Выбор предмета настоящим кликом по ячейке сумки.
		At(C, 0.8f, [C]()
		{
			FVector2D P;
			const bool bGeo = Center(GridCell(C, kBagCell), P);
			C->Expect(bGeo, TEXT("0 ячейка сумки 0 отрисована"));
			if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2); }
		});

		// ═══ A. Пустое гнездо: перенос особого камня выше уровня на гнездо 2 ═══
		At(C, 2.0f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			const int32 Col = TrayColOf(C, kHighSpecial);
			FVector2D From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
			const bool bGeo = Col >= 0 && Center(S->DebugTrayIcon(Col), From) && Center(S->DebugSocketIcon(2), To);
			C->Expect(bGeo, FString::Printf(TEXT("A0 камень выше уровня в списке (колонка %d) и гнездо 2 отрисованы; в списке %d"), Col, S->GetList().Num()));
			C->N5024Mark = CountSent(C, 5024, nullptr);
			if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateDrag(From, To, 24); }
		});
		At(C, 3.4f, [C]()
		{
			TArray<uint8> B;
			const int32 N = CountSent(C, 5024, &B);
			// Тело 5024 до шифрования: @24 = 11 байт запроса (контейнер, слот i16, гнездо, старый i16, новый i16, конт., слот i16).
			C->Expect(N == C->N5024Mark + 1, FString::Printf(TEXT("A1 перенос — 5024 ушёл, как у оригинала (проверки уровня у клиента нет): %d → %d"), C->N5024Mark, N));
			const FAsdaItemInstance* It = C->Sub<UAsdaInventorySubsystem>()->GetCell(1, kBagCell);
			C->ItemBefore = It ? It->Raw : TArray<uint8>();
			FeedRefusal(C, 10);
		});
		AfterRefusal(C, 4.4f, [C]() { CheckAfterRefusal(C, TEXT("A2"), 10, kHighSpecial); ClickLowStone(C); });
		At(C, 5.2f, [C]() { CheckLowStoneSelected(C, TEXT("A3")); });

		// ═══ B. Замена главного камня: двойной щелчок по обычному камню выше уровня ═══
		At(C, 5.6f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			const int32 Col = TrayColOf(C, kHighNormal);
			FVector2D P;
			const bool bGeo = Col >= 0 && Center(S->DebugTrayIcon(Col), P);
			C->Expect(bGeo, FString::Printf(TEXT("B0 обычный камень выше уровня в списке (колонка %d)"), Col));
			C->N5024Mark = CountSent(C, 5024, nullptr);
			if (bGeo) { DoubleClick(C->Sub<UAsdaWindowSubsystem>(), P); }
		});
		At(C, 6.8f, [C]()
		{
			const int32 N = CountSent(C, 5024, nullptr);
			C->Expect(N == C->N5024Mark + 1, FString::Printf(TEXT("B1 двойной щелчок — 5024 (замена гнезда 0): %d → %d"), C->N5024Mark, N));
			const FAsdaItemInstance* It = C->Sub<UAsdaInventorySubsystem>()->GetCell(1, kBagCell);
			C->ItemBefore = It ? It->Raw : TArray<uint8>();
			FeedRefusal(C, 10);
		});
		AfterRefusal(C, 7.8f, [C]() { CheckAfterRefusal(C, TEXT("B2"), 10, kHighNormal); ClickLowStone(C); });
		At(C, 8.6f, [C]() { CheckLowStoneSelected(C, TEXT("B3")); });

		// ═══ C. Обмен через окно 1530: все дополнительные гнёзда заняты ═══
		At(C, 9.0f, [C]()
		{
			PutTarget(C, /*bFull*/ true);
			C->Sub<UAsdaSowelSubsystem>()->OnItemSelected(1, kBagCell);
		});
		At(C, 9.6f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			const int32 Col = TrayColOf(C, kHighSpecial);
			FVector2D From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
			const bool bGeo = Col >= 0 && Center(S->DebugTrayIcon(Col), From) && Center(S->DebugSocketIcon(1), To);
			C->Expect(bGeo, FString::Printf(TEXT("C0 особый камень выше уровня (колонка %d) и гнездо 1 отрисованы"), Col));
			C->N5024Mark = CountSent(C, 5024, nullptr);
			if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateDrag(From, To, 24); }
		});
		At(C, 11.0f, [C]()
		{
			UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			C->Expect(T->IsSowelStonesWindowShown() && CountSent(C, 5024, nullptr) == C->N5024Mark,
				TEXT("C1 гнёзд нет — окно выбора 1530 показано, пакета нет (как оригинал)"));
			UUserWidget* Box = WM->FindWindow(FName(TEXT("SOWELL_SELBOX")));
			FVector2D P;
			UWidget* Stone = Box ? AsdaUi::FindNodeByName(Box, TEXT(""), TEXT("SOWELL_BTRAY01_")) : nullptr;
			const bool bGeo = Center(Stone, P);
			C->Expect(bGeo, TEXT("C2 камень гнезда 2 в окне 1530 отрисован"));
			if (bGeo) { WM->SimulateClickAt(P, 2); }
		});
		At(C, 11.8f, [C]()
		{
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			UUserWidget* Box = WM->FindWindow(FName(TEXT("SOWELL_SELBOX")));
			FVector2D P;
			const bool bGeo = Center(Box ? Box->GetWidgetFromName(TEXT("SOWELL_BUT00")) : nullptr, P);
			C->Expect(bGeo, TEXT("C3 кнопка «OK» окна 1530 отрисована"));
			if (bGeo) { WM->SimulateClickAt(P, 2); }
		});
		At(C, 12.8f, [C]()
		{
			TArray<uint8> B;
			const int32 N = CountSent(C, 5024, &B);
			C->Expect(N == C->N5024Mark + 1 && !C->Sub<UAsdaItemTooltipSubsystem>()->IsSowelStonesWindowShown(),
				FString::Printf(TEXT("C4 «OK» — 5024 ушёл, окно 1530 закрыто: %d → %d"), C->N5024Mark, N));
			const FAsdaItemInstance* It = C->Sub<UAsdaInventorySubsystem>()->GetCell(1, kBagCell);
			C->ItemBefore = It ? It->Raw : TArray<uint8>();
			FeedRefusal(C, 10);
		});
		AfterRefusal(C, 13.8f, [C]() { CheckAfterRefusal(C, TEXT("C5"), 10, kHighSpecial); ClickLowStone(C); });
		At(C, 14.6f, [C]() { CheckLowStoneSelected(C, TEXT("C6")); });

		// ═══ D. Молчание сервера: ответа нет ═══
		At(C, 15.0f, [C]()
		{
			PutTarget(C, /*bFull*/ false);
			C->Sub<UAsdaSowelSubsystem>()->OnItemSelected(1, kBagCell);
		});
		At(C, 15.6f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			const int32 Col = TrayColOf(C, kHighNormal);
			FVector2D P;
			C->N5024Mark = CountSent(C, 5024, nullptr);
			C->FrameMark = GFrameCounter;
			if (Col >= 0 && Center(S->DebugTrayIcon(Col), P)) { DoubleClick(C->Sub<UAsdaWindowSubsystem>(), P); }
		});
		At(C, 16.8f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			const int32 Col = TrayColOf(C, kHighNormal);
			FVector2D P;
			C->Expect(CountSent(C, 5024, nullptr) == C->N5024Mark + 1, TEXT("D1 5024 ушёл, сервер молчит"));
			if (Col >= 0 && Center(S->DebugTrayIcon(Col), P)) { DoubleClick(C->Sub<UAsdaWindowSubsystem>(), P); }
		});
		At(C, 18.0f, [C]()
		{
			C->Expect(CountSent(C, 5024, nullptr) == C->N5024Mark + 1 && (GFrameCounter - C->FrameMark) >= 10,
				FString::Printf(TEXT("D2 пока ждём ответ, повтор не уходит, а игра идёт (кадров %llu)"), GFrameCounter - C->FrameMark));
			ClickLowStone(C);
		});
		At(C, 18.8f, [C]() { CheckLowStoneSelected(C, TEXT("D3 (молчание сервера)")); });
		At(C, 21.8f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			const int32 Col = TrayColOf(C, kHighNormal);
			FVector2D P;
			if (Col >= 0 && Center(S->DebugTrayIcon(Col), P)) { DoubleClick(C->Sub<UAsdaWindowSubsystem>(), P); }
		});
		At(C, 23.0f, [C]()
		{
			C->Expect(CountSent(C, 5024, nullptr) == C->N5024Mark + 2, TEXT("D4 после таймаута ожидания (5 с) повтор снова уходит"));
			const FAsdaItemInstance* It = C->Sub<UAsdaInventorySubsystem>()->GetCell(1, kBagCell);
			C->ItemBefore = It ? It->Raw : TArray<uint8>();
			FeedRefusal(C, 10);
		});
		AfterRefusal(C, 24.0f, [C]() { CheckAfterRefusal(C, TEXT("D5 (поздний отказ)"), 10, kHighNormal); });

		// ═══ E. ПКМ по камню и по гнезду — пакета нет ═══
		At(C, 24.4f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			const int32 Col = TrayColOf(C, kHighNormal);
			FVector2D P;
			C->N5024Mark = CountSent(C, 5024, nullptr) + CountSent(C, 5022, nullptr);
			C->FrameMark = GFrameCounter;
			if (Col >= 0 && Center(S->DebugTrayIcon(Col), P)) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2, /*bRightButton*/ true); }
		});
		At(C, 25.0f, [C]()
		{
			FVector2D P;
			if (Center(C->Sub<UAsdaSowelSubsystem>()->DebugSocketIcon(0), P)) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2, /*bRightButton*/ true); }
		});
		At(C, 25.8f, [C]()
		{
			C->Expect(CountSent(C, 5024, nullptr) + CountSent(C, 5022, nullptr) == C->N5024Mark && (GFrameCounter - C->FrameMark) >= 10,
				TEXT("E1 ПКМ по камню списка и по гнезду — пакетов нет, игра идёт"));
		});

		// ═══ F. Все коды отказа 5025 штатным приёмником ═══
		static const int32 kCodes[] = { 2, 4, 5, 6, 7, 8, 9, 10, 11, 15, 0 };
		float T = 26.2f;
		for (int32 Code : kCodes)
		{
			At(C, T, [C, Code]()
			{
				C->Sub<UAsdaSowelSubsystem>()->DebugClearLastNotice();
				const FAsdaItemInstance* It = C->Sub<UAsdaInventorySubsystem>()->GetCell(1, kBagCell);
				C->ItemBefore = It ? It->Raw : TArray<uint8>();
				FeedRefusal(C, Code);
			});
			AfterRefusal(C, T + 0.6f, [C, Code]() { CheckAfterRefusal(C, *FString::Printf(TEXT("F%d"), Code), Code, -1); });
			T += 1.0f;
		}
		// ═══ K. КОНТРОЛЬ прежнего поведения (`asda.Sowel.RefusalToChat 0`): окно 1500 на отказ 10 — где оно и можно ли его закрыть ═══
		At(C, T + 0.3f, [C]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Sowel.RefusalToChat"), 0);
			FeedRefusal(C, 10);
		});
		At(C, T + 1.3f, [C]()
		{
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			UUserWidget* Top = WM->GetTopModalWindow();
			UWidget* Btn = Top ? AsdaUi::FindNodeByName(Top, TEXT("ONEBUT_BUT00"), TEXT("ONEBUT_BUT00")) : nullptr;
			FVector2D Ok = FVector2D::ZeroVector, Mid = FVector2D::ZeroVector;
			const bool bOkGeo = Center(Btn, Ok);
			const bool bTopGeo = Center(Top ? Top->GetRootWidget() : nullptr, Mid);
			const FName Catch = bOkGeo ? WM->FindWindowCatchingAt(Ok) : NAME_None;
			C->Expect(true, FString::Printf(TEXT("K1 КОНТРОЛЬ (ключ 0): модалка %s, корень %s (%.0f,%.0f), «OK» %s (%.0f,%.0f), хит-тест окон над «OK» → %s; ввод мира заперт: %s"),
				Top ? *Top->GetName() : TEXT("нет"), bTopGeo ? TEXT("на экране") : TEXT("НЕ на экране"), Mid.X, Mid.Y,
				bOkGeo ? TEXT("на экране") : TEXT("НЕ на экране"), Ok.X, Ok.Y, *Catch.ToString(),
				WM->IsWorldInputCapturedByModal() ? TEXT("да") : TEXT("нет")));
			C->Expect(Top != nullptr, TEXT("K2 КОНТРОЛЬ: при ключе 0 дефект воспроизводится — отказ 10 открыл МОДАЛЬНОЕ окно (оригинал пишет в чат)"));
			if (bOkGeo) { WM->SimulateClickAt(Ok, 2); }
		});
		At(C, T + 2.3f, [C]()
		{
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			C->Expect(true, FString::Printf(TEXT("K3 КОНТРОЛЬ: после щелчка по «OK» модалка %s"), WM->IsAnyModalOpen() ? TEXT("ОСТАЛАСЬ") : TEXT("закрыта")));
			if (UUserWidget* Top = WM->GetTopModalWindow()) { WM->CloseWindow(Top->GetFName()); }
			AsdaCVarSave::SetForTest(TEXT("asda.Sowel.RefusalToChat"), 1);
		});
		// ═══ H. [07.11.2026] НАВЕДЕНИЕ: подсказка камня над списком 430…435 и над лотком окна 1530 (пачка 13, баг владельца) ═══
		T += 3.0f;
		auto TipOn = [](const FCtxRef& X, EAsdaTipOwner Own) -> bool
		{
			UAsdaItemTooltipSubsystem* Tp = X->Sub<UAsdaItemTooltipSubsystem>();
			UAsdaTipPanel* Pn = Tp ? Tp->GetMainPanel() : nullptr;
			return Pn && Pn->IsShown() && Pn->GetOwner() == Own;
		};
		const FVector2D kAway(4.0, 4.0);   // угол экрана: окон и камней там нет
		At(C, T, [C]()
		{
			PutTarget(C, /*bFull*/ false);
			C->Sub<UAsdaSowelSubsystem>()->OnItemSelected(1, kBagCell);
		});
		At(C, T + 0.5f, [C]()
		{
			FVector2D P;
			const int32 Col = TrayColOf(C, kHighSpecial);
			const bool bGeo = Col >= 0 && Center(C->Sub<UAsdaSowelSubsystem>()->DebugTrayIcon(Col), P);
			C->Expect(bGeo, FString::Printf(TEXT("H0 камень списка отрисован (колонка %d)"), Col));
			if (bGeo) { FSlateApplication::Get().SetCursorPos(P); }
		});
		At(C, T + 1.0f, [C, TipOn]()
		{
			C->Expect(TipOn(C, EAsdaTipOwner::InfoSocket), TEXT("H1 наведение на камень списка 430…435 — подсказка камня на экране (`invSlotBuildTooltip`, ветка 0x1AE…0x1B3)"));
			const FString File = FString::Printf(TEXT("@@ASTORY_DEV_ROOT_FWD@@/Reports/batch13/sowel_tip/%s_tray.png"), FAsdaLang::Code(FAsdaLang::Current()));
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		});
		At(C, T + 1.5f, [kAway]() { FSlateApplication::Get().SetCursorPos(kAway); });
		At(C, T + 2.0f, [C, TipOn]()
		{
			C->Expect(!TipOn(C, EAsdaTipOwner::InfoSocket), TEXT("H2 курсор ушёл — подсказка погасла"));
			AsdaCVarSave::SetForTest(TEXT("asda.Sowel.HoverTip"), 0);
			FVector2D P;
			const int32 Col = TrayColOf(C, kHighSpecial);
			if (Col >= 0 && Center(C->Sub<UAsdaSowelSubsystem>()->DebugTrayIcon(Col), P)) { FSlateApplication::Get().SetCursorPos(P); }
		});
		At(C, T + 2.5f, [C, TipOn, kAway]()
		{
			C->Expect(!TipOn(C, EAsdaTipOwner::InfoSocket), TEXT("H3 КОНТРОЛЬ: ключ asda.Sowel.HoverTip 0 — дефект воспроизведён, подсказки над списком нет"));
			AsdaCVarSave::SetForTest(TEXT("asda.Sowel.HoverTip"), 1);
			FSlateApplication::Get().SetCursorPos(kAway);
			PutTarget(C, /*bFull*/ true);
			C->Sub<UAsdaSowelSubsystem>()->OnItemSelected(1, kBagCell);
		});
		At(C, T + 3.0f, [C]()
		{
			UAsdaSowelSubsystem* S = C->Sub<UAsdaSowelSubsystem>();
			S->DebugClearAwaiting();
			const int32 Col = TrayColOf(C, kHighSpecial);
			FVector2D From, To;
			if (Col >= 0 && Center(S->DebugTrayIcon(Col), From) && Center(S->DebugSocketIcon(1), To)) { C->Sub<UAsdaWindowSubsystem>()->SimulateDrag(From, To, 24); }
		});
		At(C, T + 4.4f, [C]()
		{
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			C->Expect(C->Sub<UAsdaItemTooltipSubsystem>()->IsSowelStonesWindowShown(), TEXT("H4 окно 1530 показано"));
			UUserWidget* Box = WM->FindWindow(FName(TEXT("SOWELL_SELBOX")));
			UWidget* Stone = Box ? AsdaUi::FindNodeByName(Box, TEXT(""), TEXT("SOWELL_BTRAY01_")) : nullptr;
			FVector2D P;
			if (Center(Stone, P)) { FSlateApplication::Get().SetCursorPos(P); }
		});
		At(C, T + 4.9f, [C, TipOn]()
		{
			C->Expect(TipOn(C, EAsdaTipOwner::Item), TEXT("H5 наведение на камень окна 1530 — подсказка без щелчка (`uiControlSetStateByMsg@0xD78710`)"));
			UAsdaWindowSubsystem* WM5 = C->Sub<UAsdaWindowSubsystem>();
			const int32 ZBox = WM5->GetWindowZOrder(FName(TEXT("SOWELL_SELBOX")));
			const int32 ZTip = WM5->GetWindowZOrder(FName(TEXT("ITEM_TIPWIN")));
			C->Expect(ZTip > ZBox, FString::Printf(TEXT("H5б плашка над окном 1530: z %d > %d"), ZTip, ZBox));
			const FString File = FString::Printf(TEXT("@@ASTORY_DEV_ROOT_FWD@@/Reports/batch13/sowel_tip/%s_selbox.png"), FAsdaLang::Code(FAsdaLang::Current()));
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		});
		At(C, T + 5.4f, [kAway]() { FSlateApplication::Get().SetCursorPos(kAway); });
		At(C, T + 5.9f, [C, TipOn]()
		{
			C->Expect(!TipOn(C, EAsdaTipOwner::Item), TEXT("H6 курсор ушёл с камня окна 1530 — подсказка погасла"));
			C->Sub<UAsdaItemTooltipSubsystem>()->HideSowelStonesWindow();
		});
		// ═══ I. [09.11.2026] ВВОД МИРА ПОСЛЕ ОКНА 1530 (баг alpha-7.4.0: после окна выбора не работают щелчки и WASD) ═══
		//   Каждая ветка: открыть 1530 НАСТОЯЩИМ переносом камня на занятое гнездо → пока окно открыто, ввод мира отдан модалке →
		//   закрыть веткой → вьюпорт снова принимает ввод (`IgnoreInput() == false`), модалок нет.
		T += 6.6f;
		auto WorldInputFree = []() -> bool { return GEngine && GEngine->GameViewport && !GEngine->GameViewport->IgnoreInput(); };
		auto Open1530 = [](const FCtxRef& X)
		{
			PutTarget(X, /*bFull*/ true);
			X->Sub<UAsdaSowelSubsystem>()->OnItemSelected(1, kBagCell);
			X->Sub<UAsdaSowelSubsystem>()->DebugClearAwaiting();
			const int32 Col = TrayColOf(X, kHighSpecial);
			FVector2D From, To;
			if (Col >= 0 && Center(X->Sub<UAsdaSowelSubsystem>()->DebugTrayIcon(Col), From) && Center(X->Sub<UAsdaSowelSubsystem>()->DebugSocketIcon(1), To))
			{
				X->Sub<UAsdaWindowSubsystem>()->SimulateDrag(From, To, 24);
			}
		};
		auto ClickBox = [](const FCtxRef& X, const TCHAR* Node)
		{
			UAsdaWindowSubsystem* WM = X->Sub<UAsdaWindowSubsystem>();
			UUserWidget* Box = WM->FindWindow(FName(TEXT("SOWELL_SELBOX")));
			FVector2D P;
			if (Center(Box ? AsdaUi::FindNodeByName(Box, Node, Node) : nullptr, P)) { WM->SimulateClickAt(P, 2); }
		};
		struct FBranch { const TCHAR* Name; int32 Kind; };
		static const FBranch kBranches[] = {
			{ TEXT("I1 «Отмена» (первое открытие окна)"), 0 }, { TEXT("I2 Esc"), 1 }, { TEXT("I3 «OK» без выбора"), 2 },
			{ TEXT("I4 «OK» с выбором + отказ сервера 10"), 3 }, { TEXT("I5 снятие окон-сообщений перед нотисом (DropTopMsgBoxBeforeNotice, очередь пуста)"), 4 },
			{ TEXT("I7 запись окна снята менеджером (как CloseAll при смене карты), затем скрытие"), 6 },
			{ TEXT("I6 КОНТРОЛЬ ключ asda.Sowel.SelBoxInputRefresh 0 + первое открытие + «Отмена»"), 5 },
		};
		for (const FBranch& Br : kBranches)
		{
			const FString Name = Br.Name;
			const int32 Kind = Br.Kind;
			At(C, T, [C, Open1530, Kind]()
			{
				if (Kind == 5) { AsdaCVarSave::SetForTest(TEXT("asda.Sowel.SelBoxInputRefresh"), 0); }
				// «Отмена» и контроль — с ПЕРВОГО открытия окна за сессию (как у игрока): окно снимается, показ привяжет его заново.
				if (Kind == 0 || Kind == 5) { C->Sub<UAsdaWindowSubsystem>()->CloseWindow(FName(TEXT("SOWELL_SELBOX"))); }
				C->Expect(GEngine && GEngine->GameViewport && !GEngine->GameViewport->IgnoreInput(), TEXT("предусловие: до открытия окна 1530 вьюпорт принимает ввод"));
				Open1530(C);
			});
			// ★ R5-fix-b 13.11.2026 (заключительный заход, третья запись): проверка ветки — когда окно 1530 открыто и ввод мира отдан модалке (первое открытие грузит ассет
			//   дольше 1,4 с под нагрузкой — I1 «нет/нет» и каскад на предусловие следующей ветки); потолок 5 с — иначе та же проверка падает.
			AsdaSelfTestSeq::When(C, T + 1.4f, [C]()
			{
				return C->Sub<UAsdaItemTooltipSubsystem>()->IsSowelStonesWindowShown() && GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput();
			}, 5.f, [C, Name, ClickBox, Kind]()
			{
				const bool bShown = C->Sub<UAsdaItemTooltipSubsystem>()->IsSowelStonesWindowShown();
				const bool bCaptured = GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput();
				C->Expect(bShown && bCaptured, FString::Printf(TEXT("%s: окно 1530 открыто, ввод мира отдан модалке (%s/%s)"), *Name,
					bShown ? TEXT("да") : TEXT("нет"), bCaptured ? TEXT("да") : TEXT("нет")));
				switch (Kind)
				{
				case 0: case 5: ClickBox(C, TEXT("SOWELL_BUT01")); break;
				case 1:
					FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27));
					FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27));
					break;
				case 2: ClickBox(C, TEXT("SOWELL_BUT00")); break;
				case 3:
				{
					UUserWidget* Box = C->Sub<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("SOWELL_SELBOX")));
					FVector2D P;
					if (Center(Box ? AsdaUi::FindNodeByName(Box, TEXT(""), TEXT("SOWELL_BTRAY01_")) : nullptr, P)) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2); }
					break;
				}
				case 4: C->Sub<UAsdaWindowSubsystem>()->DropTopMsgBoxBeforeNotice(TEXT("самотест I5")); break;
				case 6:
					C->Sub<UAsdaWindowSubsystem>()->CloseWindow(FName(TEXT("SOWELL_SELBOX")));   // запись снята, указатель у подсистемы остался
					C->Sub<UAsdaItemTooltipSubsystem>()->HideSowelStonesWindow();
					break;
				default: break;
				}
			});
			if (Kind == 3)
			{
				At(C, T + 2.0f, [C, ClickBox]() { ClickBox(C, TEXT("SOWELL_BUT00")); });
				At(C, T + 2.6f, [C]() { FeedRefusal(C, 10); });
			}
			At(C, T + 3.2f, [C, Name, Kind, WorldInputFree]()
			{
				UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
				const bool bShown = C->Sub<UAsdaItemTooltipSubsystem>()->IsSowelStonesWindowShown();
				const bool bFree = WorldInputFree();
				if (Kind == 5)
				{
					C->Expect(!bShown && !bFree, FString::Printf(TEXT("%s: дефект воспроизведён — окно закрыто, а вьюпорт по-прежнему игнорирует ввод (WASD и щелчки мертвы)"), *Name));
					AsdaCVarSave::SetForTest(TEXT("asda.Sowel.SelBoxInputRefresh"), 1);
					WM->SetWindowVisible(FName(TEXT("SOWELL_SELBOX")), false);   // вернуть режим ввода для следующих тестов
				}
				else
				{
					C->Expect(!bShown && bFree && !WM->IsAnyModalOpen(), FString::Printf(TEXT("%s: окно закрыто, мир снова принимает ввод (вьюпорт %s, модалок %s)"), *Name,
						bFree ? TEXT("принимает") : TEXT("ИГНОРИРУЕТ"), WM->IsAnyModalOpen() ? TEXT("есть") : TEXT("нет")));
				}
				C->Sub<UAsdaSowelSubsystem>()->DebugClearAwaiting();
			});
			T += 3.6f;
		}
		At(C, T, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSowelLevel(
	TEXT("asda.SelfTest.SowelLevel"),
	TEXT("Самотест «самоцвет выше уровня персонажа»: перенос/двойной щелчок/окно 1530/ПКМ путём игрока, отказ 5025 штатным приёмником "
	     "и молчание сервера; сторож игрового потока. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSowelLevelTest;
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
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
