// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПЕРЕНОС ЗАДАНИЯ МЕЖДУ ЯЧЕЙКАМИ ЖУРНАЛА» ПУТЁМ ИГРОКА (06.11.2026, баг игроков: задания нельзя переместить и поменять
//   местами). [ИСПРАВЛЕНИЕ]
//
//   ЧТО БЫЛО. У ячеек журнала не было перетаскивания (`UAsdaQuestLogSubsystem::MoveSlot` никто не звал), а 5001 типа 4 инвентарь
//   отбрасывал. Исправление — `TickSlotDrag`/`EndSlotDrag`/`ConsumeItemMove5001`, ключ `asda.QuestLog.SlotMove`.
//
//   ПУТЬ ИГРОКА: журнал — пакетом 5047 (`DebugFeedPacket`); перетаскивания и щелчки — настоящие события Slate
//   (`SimulateDrag`/`SimulateClickAt`) по живой геометрии; 5000 уходит боевым отправителем под перехватом (в сеть ничего); ответ 5001 —
//   штатным приёмом (`DebugFeedPacket` → очередь инвентаря → `ApplyMoveResult`).
//   Оракул — правило эталона/сервера, записанное здесь независимо от боевого кода:
//     бросок на другую ячейку → ровно одна 5000: хвост 22 б [источник i16][+4 = 4][+11 цель i16][+15 = 4]; до ответа журнал не меняется;
//     5001 {1, подвид 2} → запись в цели, источник пуст, поле +5 = новая ячейка; {1, подвид 1} → обмен; выбор едет с заданием;
//     отслеживание остаётся за заданием; бросок на ту же ячейку и мимо окна — 5000 нет; бросок на корзину 713 — вопрос (7, 6174);
//     отказ 5001 {0, эхо запроса} → нотис 2014, журнал цел; щелчок без сдвига — только выбор; до ответа новый перенос не шлётся,
//     через 10 с молчания — шлётся; 5047 (перезаход) показывает порядок сервера.
//   Разделы: A контроль ключа 0 (дефект воспроизведён = шаг 0); B в пустую ячейку; C обмен; D на ту же ячейку; E мимо окна; F корзина;
//   G отказ сервера; H щелчок; I молчание сервера; J 5047 с новым порядком.
//
// Консоль: `asda.SelfTest.QuestSlotMove [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`), на карте мира.
//   Строки проверок — «ЖУРНАЛ-ПЕРЕНОС ok/FAIL», итог — «ЖУРНАЛ-ПЕРЕНОС ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): общая очередь шагов «по событию»
#include "Quest/AsdaQuestLogSubsystem.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuestSlotMoveTest, Log, All);

namespace AsdaQuestSlotMoveTest
{
	static const TCHAR* kCmd    = TEXT("asda.SelfTest.QuestSlotMove");  // имя команды
	static const TCHAR* kKey    = TEXT("asda.QuestLog.SlotMove");       // ключ исправления
	static const TCHAR* kTwoBut = TEXT("TWOBUT_MSGBOX");                // окно-вопрос 1520
	static const TCHAR* kQuest  = TEXT("QUEST_WIN");                    // журнал заданий 700

	// Два задания прогона и пустая ячейка.
	static constexpr int32 kEpA = 2020, kQnA = 19, kCellA = 3;
	static constexpr int32 kEpB = 2022, kQnB = 21, kCellB = 5;
	static constexpr int32 kCellFree = 7;

	// Перехваченная отправка: опкод и тело.
	struct FSent { int32 Op = 0; TArray<uint8> Body; };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;            // пройдено / провалено
		TWeakObjectPtr<UWorld> W;          // мир прогона
		TSharedPtr<AsdaSelfTestSeq::FSeq> Seq;   // ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): очередь шагов (`AsdaSelfTestSeq`)
		bool bQuit = false;                // выйти после итога
		uint32 CapGen = 0;                 // поколение перехвата отправок
		TArray<FSent> Sent;                // перехваченные отправки
		int32 Mark = 0;                    // число отправок на начало шага
		FVector2D OutPt = FVector2D::ZeroVector;   // точка вне окон
		FString TxtRefuse;                 // строка 2014 на языке сессии
		int32 NRefuse0 = 0;                // её счётчик в чате на начало шага

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaQuestSlotMoveTest, Log,   TEXT("ЖУРНАЛ-ПЕРЕНОС ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaQuestSlotMoveTest, Error, TEXT("ЖУРНАЛ-ПЕРЕНОС FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	//   ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8): шаги — очередью `AsdaSelfTestSeq` (локальный `At` в прогоне), общий `AsdaSelfTestCtx::At` не нужен.

	// Центр живой геометрии узла (false — не отрисован / скрыт / нулевой размер).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).
	// Узел окна по имени.
	static UWidget* Node(const FCtxRef& C, const TCHAR* Win, const TCHAR* Name)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Wd = WM ? WM->FindWindow(FName(Win)) : nullptr;
		return Wd ? AsdaUi::FindNodeByName(Wd, Name, nullptr) : nullptr;
	}
	// Кнопка ячейки журнала.
	static UWidget* Cell(const FCtxRef& C, int32 I) { return Node(C, kQuest, *FString::Printf(TEXT("QUEST_INVEN%d_HIT"), I)); }
	// План-иконка ячейки (`QUEST_INVEN<i>_<28+i>_0`).
	static UImage* CellIcon(const FCtxRef& C, int32 I) { return Cast<UImage>(Node(C, kQuest, *FString::Printf(TEXT("QUEST_INVEN%d_%d_0"), I, 28 + I))); }
	// На иконке ячейки есть картинка.
	static bool HasPicture(const FCtxRef& C, int32 I)
	{
		UImage* Img = CellIcon(C, I);
		return Img && Img->GetBrush().GetResourceObject() != nullptr;
	}
	// Настоящее перетаскивание из ячейки в точку экрана.
	static void Drag(const FCtxRef& C, int32 From, const FVector2D& To, const TCHAR* Tag)
	{
		FVector2D P = FVector2D::ZeroVector;
		const bool bGeo = Center(Cell(C, From), P);
		C->Expect(bGeo, FString::Printf(TEXT("%s ячейка %d на экране, бросок в (%.0f,%.0f)"), Tag, From, To.X, To.Y));
		C->Mark = C->Sent.Num();
		if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateDrag(P, To, /*HoldFrames*/ 30); }
	}
	// Перетаскивание с ячейки на ячейку.
	static void DragToCell(const FCtxRef& C, int32 From, int32 To, const TCHAR* Tag)
	{
		FVector2D P = FVector2D::ZeroVector;
		C->Expect(Center(Cell(C, To), P), FString::Printf(TEXT("%s ячейка-цель %d на экране"), Tag, To));
		Drag(C, From, P, Tag);
	}
	// Настоящий щелчок по узлу окна.
	static void Click(const FCtxRef& C, const TCHAR* Win, const TCHAR* Name, const TCHAR* Tag)
	{
		FVector2D P = FVector2D::ZeroVector;
		const bool bGeo = Center(Node(C, Win, Name), P);
		C->Expect(bGeo, FString::Printf(TEXT("%s узел %s окна %s на экране"), Tag, Name, Win));
		if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, /*HoldFrames*/ 2); }
	}
	// Отправки 5000 с шага (последняя — в OutLast).
	static int32 Sent5000(const FCtxRef& C, TArray<uint8>* OutLast = nullptr)
	{
		int32 N = 0;
		for (int32 i = C->Mark; i < C->Sent.Num(); ++i) { if (C->Sent[i].Op == 5000) { ++N; if (OutLast) { *OutLast = C->Sent[i].Body; } } }
		return N;
	}
	// Ровно одна 5000 переноса.
	static void Expect5000(const FCtxRef& C, const TCHAR* Tag, int32 Src, int32 Dst)
	{
		TArray<uint8> B;
		const int32 N = Sent5000(C, &B);
		if (N != 1 || B.Num() < 22) { C->Expect(false, FString::Printf(TEXT("%s ушла ровно одна 5000 (ушло %d, тело %d б)"), Tag, N, B.Num())); return; }
		const uint8* P = B.GetData() + B.Num() - 22;
		const int32 GS = AsdaBytes::RdI16(P + 0), GT = P[4], GD = AsdaBytes::RdI16(P + 11), GDT = P[15];
		C->Expect(GS == Src && GT == 4 && GD == Dst && GDT == 4,
			FString::Printf(TEXT("%s 5000: источник %d тип %d, цель %d тип %d (ждали %d/4, %d/4)"), Tag, GS, GT, GD, GDT, Src, Dst));
	}
	// Ответ 5001 штатным приёмом: раскладка `sub_4D12F0` (+1 подвид, +2 источник, +6 тип, +13 цель, +17 тип).
	static void Reply5001(const FCtxRef& C, int32 Result, int32 SubKind, int32 Src, int32 Dst)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (!Net || !Inv) { return; }
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = (uint8)Result; B[1] = (uint8)SubKind;
		AsdaBytes::WrI16(B.GetData() + 2, Src);  B[6] = 4;
		AsdaBytes::WrI16(B.GetData() + 13, Dst); B[17] = 4;
		Net->DebugFeedPacket(5001, B);
		Inv->DebugPumpPendingPackets();
	}
	// Задание в ячейке (0 — пусто) и поле +5 записи.
	static int32 QuestAt(const FCtxRef& C, int32 I)
	{
		UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
		if (!QM || !QM->GetSlots().IsValidIndex(I)) { return 0; }
		const FAsdaQuestSlot& S = QM->GetSlots()[I];
		return S.QuestId >= 2001 ? S.QuestNo : 0;
	}
	static int32 FieldAt(const FCtxRef& C, int32 I)
	{
		UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
		return (QM && QM->GetSlots().IsValidIndex(I)) ? QM->GetSlots()[I].SlotIndex : -1;
	}
	// Пакет 5047 (688 б): два задания по указанным ячейкам (−1 — без задания).
	static TArray<uint8> Make5047(int32 CellA, int32 CellB)
	{
		TArray<uint8> B; B.SetNumZeroed(688);
		auto Put = [&B](int32 I, int32 Ep, int32 Qn)
		{
			uint8* R = B.GetData() + 44 * I;
			AsdaBytes::WrI32(R + 0, Ep);
			R[4] = 4;
			AsdaBytes::WrI16(R + 5, I);
			AsdaBytes::WrI16(R + 8, Qn);
			AsdaBytes::WrI16(R + 10, 0);
		};
		if (CellA >= 0) { Put(CellA, kEpA, kQnA); }
		if (CellB >= 0) { Put(CellB, kEpB, kQnB); }
		return B;
	}
	// Журнал из 5047.
	static void Feed(const FCtxRef& C, int32 CellA, int32 CellB)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5047, Make5047(CellA, CellB)); }
	}
	// Точка экрана вне всех окон.
	static bool PickOutside(const FCtxRef& C)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		if (!WM || !GEngine || !GEngine->GameViewport) { return false; }
		const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
		if (!Vp.IsValid()) { return false; }
		const FGeometry& G = Vp->GetCachedGeometry();
		const FVector2D Mn = G.GetAbsolutePosition(), Sz = G.GetAbsoluteSize();
		const FVector2D Mid = Mn + Sz * 0.5f;
		float Best = TNumericLimits<float>::Max();
		bool bFound = false;
		for (int32 iy = 1; iy < 40; ++iy)
		{
			for (int32 ix = 1; ix < 40; ++ix)
			{
				const FVector2D P = Mn + FVector2D(Sz.X * ix / 40.f, Sz.Y * iy / 40.f);
				FName Z = NAME_None;
				if (WM->IsScreenPointOverAnyUiWindow(P, Z)) { continue; }
				const float D = FVector2D::DistSquared(P, Mid);
				if (D < Best) { Best = D; C->OutPt = P; bFound = true; }
			}
		}
		return bFound;
	}

	static void Finish(const FCtxRef& C)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
		if (WM && WM->IsMsgBoxQueued(7, 6174)) { WM->CloseConfirm(false); }
		// Трекер ключуется номером задания и пережил бы 5047 — снять до пустого журнала.
		if (QL) { for (int32 I = 0; I < 12; ++I) { if (QL->IsSlotTracked(I)) { QL->SetSlotTracked(I, false); } } }
		if (QL && QL->IsOpen()) { QL->Close(); }
		if (Net) { Net->DebugFeedPacket(5047, Make5047(-1, -1)); }
		if (Net && C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaQuestSlotMoveTest, TEXT("ЖУРНАЛ-ПЕРЕНОС"), C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
		if (!WM || !Net || !QL || !C->Sub<UAsdaQuestMarkerSubsystem>() || !C->Sub<UAsdaChatSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы окон/сети/журнала/меток/чата есть"));
			Finish(C);
			return;
		}
		C->TxtRefuse = AsdaNotice::Loc(C->GI(), 2014);
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { FSent S; S.Op = (int32)Op; if (P && Len > 0) { S.Body.Append(P, Len); } X->Sent.Add(MoveTemp(S)); }
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват отправок свободен")); Finish(C); return; }

		AsdaCVarSave::SetForTest(kKey, 0);
		Feed(C, kCellA, kCellB);
		// ★ R4-хвосты-c (15.11.2026): признак «открыт» мог остаться от прошлого самотеста пакета без окна на экране (окна выброшены точкой входа 4003,
		//   журнал об этом не знает) — тогда `Open` молча выходил, и все проверки ячеек падали. Сначала закрываем такой журнал.
		if (QL->IsOpen() && !WM->IsWindowVisible(FName(kQuest))) { QL->Close(); }
		if (!QL->IsOpen()) { QL->Open(); }
		WM->BringWindowToFront(FName(kQuest));

		// ★ R5-fix6-c (13.11.2026), R5-fix-b 13.11.2026 (четвёртая запись), ★ R4-хвосты-c (15.11.2026) — комментарий приведён к коду: шаги идут общей
		//   очередью «по событию» `AsdaSelfTestSeq` (`Public/Test/AsdaSelfTestSeq.h`); своей очереди `FStepSeq` в этом файле больше нет.
		//   Расписание `At(C, t, шаг)` прежнее, шаги ставятся сразу, отдельного запуска нет. Шаг начинается, когда выполнено всё:
		//     1) заказанное предыдущим шагом перетаскивание/щелчок доставлено (`IsSimClickPending() == false`, потолок ожидания 20 с);
		//     2) после отпускания прошло 3 кадра и пауза `AfterInputSec` (здесь 0,5 с — строкой ниже; не больше промежутка шага);
		//     3) истёк промежуток расписания от предыдущего шага (`t_i − t_{i−1}`), часы реальные (`FPlatformTime`), не мировой таймер.
		//   Прежде проверка стояла по мировому таймеру через 1,6 с после заказа перетаскивания на 30 кадров; при кадре длиннее 53 мс
		//   (пачка под нагрузкой) она срабатывала до отпускания кнопки — 7 красных проверок (E, F, G, H, I), одиночно зелёный.
		auto At = [](const FCtxRef& Cx, float T, TFunction<void()> Fn) { AsdaSelfTestSeq::At(Cx, T, MoveTemp(Fn)); };
		// ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8) (МЕЛК-3): пауза после доставки ввода 0,5 с — как `SettleSec` прежней очереди `FStepSeq`.
		if (AsdaSelfTestSeq::FSeq* Q = AsdaSelfTestSeq::Queue(C->Seq, C->W.Get())) { Q->AfterInputSec = 0.5; }

		At(C, 0.8f, [C]() { C->Expect(PickOutside(C), FString::Printf(TEXT("точка вне окон (%.0f,%.0f)"), C->OutPt.X, C->OutPt.Y)); });

		// ═══ A. КОНТРОЛЬ (шаг 0): ключ 0 — перетаскивания нет, 5000 не уходит ═══
		At(C, 1.2f, [C]() { DragToCell(C, kCellA, kCellFree, TEXT("A")); });
		At(C, 2.8f, [C]()
		{
			C->Expect(Sent5000(C) == 0 && QuestAt(C, kCellA) == kQnA, TEXT("A КОНТРОЛЬ ключ 0: 5000 не ушла, задание на месте (дефект воспроизведён)"));
			AsdaCVarSave::SetForTest(kKey, 1);
			C->Sub<UAsdaQuestLogSubsystem>()->SetSlotTracked(kCellA, true);
		});

		// ═══ B. Ключ 1: бросок в пустую ячейку → 5000 → до ответа журнал цел, новый перенос не шлётся → 5001 подвид 2 ═══
		At(C, 3.2f, [C]() { DragToCell(C, kCellA, kCellFree, TEXT("B")); });
		At(C, 4.8f, [C]()
		{
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			Expect5000(C, TEXT("B"), kCellA, kCellFree);
			C->Expect(QL->IsMovePending() && !QL->IsSlotDragActive(), TEXT("B ждём 5001, призрак снят"));
			C->Expect(QuestAt(C, kCellA) == kQnA && QuestAt(C, kCellFree) == 0, TEXT("B до ответа журнал не менялся"));
			UImage* Img = CellIcon(C, kCellA);
			C->Expect(Img && Img->GetColorAndOpacity().A > 0.99f, TEXT("B иконка источника вернула яркость"));
		});
		At(C, 5.0f, [C]() { DragToCell(C, kCellB, kCellFree, TEXT("B повтор")); });
		At(C, 6.6f, [C]()
		{
			C->Expect(Sent5000(C) == 0, TEXT("B до ответа второй перенос не ушёл"));
			Reply5001(C, 1, 2, kCellA, kCellFree);
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			C->Expect(QuestAt(C, kCellFree) == kQnA && QuestAt(C, kCellA) == 0, FString::Printf(TEXT("B 5001 подвид 2: задание в ячейке %d, ячейка %d пуста"), kCellFree, kCellA));
			C->Expect(FieldAt(C, kCellFree) == kCellFree, FString::Printf(TEXT("B поле +5 = %d"), FieldAt(C, kCellFree)));
			C->Expect(!QL->IsMovePending(), TEXT("B ожидание снято"));
			C->Expect(QL->IsSlotTracked(kCellFree), TEXT("B отслеживание едет с заданием"));
		});
		At(C, 7.0f, [C]()
		{
			C->Expect(C->Sub<UAsdaQuestLogSubsystem>()->GetSelectedSlot() == kCellFree, TEXT("B выбор на новой ячейке"));
			C->Expect(HasPicture(C, kCellFree) && !HasPicture(C, kCellA), TEXT("B иконка переехала на экране"));
		});

		// ═══ C. Обмен: бросок на занятую ячейку → 5001 подвид 1 ═══
		At(C, 7.4f, [C]() { DragToCell(C, kCellB, kCellFree, TEXT("C")); });
		At(C, 9.0f, [C]()
		{
			Expect5000(C, TEXT("C"), kCellB, kCellFree);
			Reply5001(C, 1, 1, kCellB, kCellFree);
			C->Expect(QuestAt(C, kCellFree) == kQnB && QuestAt(C, kCellB) == kQnA, TEXT("C 5001 подвид 1: задания поменялись местами"));
			C->Expect(FieldAt(C, kCellFree) == kCellFree && FieldAt(C, kCellB) == kCellB, TEXT("C поля +5 = новые ячейки"));
			C->Expect(C->Sub<UAsdaQuestLogSubsystem>()->GetSelectedSlot() == kCellFree, TEXT("C выбор едет с перетащенным заданием"));
		});

		// ═══ D. На ту же ячейку и E. мимо окна — 5000 нет ═══
		At(C, 9.4f, [C]()
		{
			FVector2D P = FVector2D::ZeroVector;
			if (Center(Cell(C, kCellB), P)) { Drag(C, kCellB, P + FVector2D(6.f, 6.f), TEXT("D")); }
		});
		At(C, 11.0f, [C]() { C->Expect(Sent5000(C) == 0 && QuestAt(C, kCellB) == kQnA, TEXT("D бросок на ту же ячейку: 5000 нет")); });
		At(C, 11.2f, [C]() { Drag(C, kCellB, C->OutPt, TEXT("E")); });
		At(C, 12.8f, [C]()
		{
			C->Expect(Sent5000(C) == 0 && QuestAt(C, kCellB) == kQnA && !C->Sub<UAsdaQuestLogSubsystem>()->IsSlotDragActive(), TEXT("E бросок мимо окна: 5000 нет, захват снят"));
		});

		// ═══ F. Бросок на корзину 713 → вопрос (7, 6174), «Нет» ═══
		At(C, 13.0f, [C]()
		{
			FVector2D P = FVector2D::ZeroVector;
			C->Expect(Center(Node(C, kQuest, TEXT("QUEST_TRASHBUT")), P), TEXT("F корзина на экране"));
			Drag(C, kCellB, P, TEXT("F"));
		});
		At(C, 14.6f, [C]()
		{
			C->Expect(C->Sub<UAsdaWindowSubsystem>()->IsMsgBoxQueued(7, 6174) && Sent5000(C) == 0, TEXT("F показан вопрос (7, 6174), 5000 нет"));
			Click(C, kTwoBut, TEXT("TWOBUT_BUT01"), TEXT("F «Нет»:"));
		});

		// ═══ G. Отказ сервера: журнал цел, выбор на месте, нотис 2014 ═══
		At(C, 15.4f, [C]() { DragToCell(C, kCellB, kCellA, TEXT("G")); });
		At(C, 17.0f, [C]()
		{
			Expect5000(C, TEXT("G"), kCellB, kCellA);
			UAsdaChatSubsystem* Chat = C->Sub<UAsdaChatSubsystem>();
			C->NRefuse0 = Chat->DebugCountSystemLines(C->TxtRefuse);
			Reply5001(C, 0, 0, kCellB, kCellA);
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			C->Expect(Chat->DebugCountSystemLines(C->TxtRefuse) == C->NRefuse0 + 1, FString::Printf(TEXT("G нотис «%s»"), *C->TxtRefuse));
			C->Expect(QuestAt(C, kCellB) == kQnA && QuestAt(C, kCellA) == 0 && !QL->IsMovePending(), TEXT("G журнал цел, ожидание снято"));
			C->Expect(QL->GetSelectedSlot() == kCellB, TEXT("G выбор остался на задании"));
		});

		// ═══ H. Щелчок без сдвига — только выбор ═══
		At(C, 17.4f, [C]() { C->Mark = C->Sent.Num(); Click(C, kQuest, *FString::Printf(TEXT("QUEST_INVEN%d_HIT"), kCellFree), TEXT("H")); });
		At(C, 18.0f, [C]()
		{
			C->Expect(Sent5000(C) == 0 && C->Sub<UAsdaQuestLogSubsystem>()->GetSelectedSlot() == kCellFree, TEXT("H щелчок выбрал ячейку, 5000 нет"));
		});

		// ═══ I. Молчание сервера: через 10 с новый перенос снова уходит ═══
		At(C, 18.2f, [C]() { DragToCell(C, kCellFree, kCellA, TEXT("I")); });
		At(C, 19.8f, [C]() { Expect5000(C, TEXT("I"), kCellFree, kCellA); });
		At(C, 30.2f, [C]() { DragToCell(C, kCellFree, kCellA, TEXT("I после 10 с")); });
		At(C, 31.8f, [C]()
		{
			Expect5000(C, TEXT("I после 10 с"), kCellFree, kCellA);
			Reply5001(C, 1, 2, kCellFree, kCellA);
			C->Expect(QuestAt(C, kCellA) == kQnB, TEXT("I ответ применён"));
		});

		// ═══ J. Перезаход: 5047 несёт порядок сервера ═══
		At(C, 32.2f, [C]()
		{
			Feed(C, /*A*/ 9, /*B*/ 1);
			C->Expect(QuestAt(C, 9) == kQnA && QuestAt(C, 1) == kQnB && QuestAt(C, kCellA) == 0 && QuestAt(C, kCellB) == 0, TEXT("J 5047: журнал лёг по ячейкам сервера"));
		});
		At(C, 32.8f, [C]() { C->Expect(HasPicture(C, 9) && HasPicture(C, 1), TEXT("J иконки на ячейках сервера")); Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestQuestSlotMove(
	TEXT("asda.SelfTest.QuestSlotMove"),
	TEXT("Самотест переноса заданий в журнале настоящей мышью: контроль ключа asda.QuestLog.SlotMove 0, бросок в пустую ячейку, обмен, на ту же "
	     "ячейку, мимо окна, на корзину, отказ сервера, щелчок, молчание сервера, 5047. 5000 под перехватом. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaQuestSlotMoveTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен гейтам мира — офлайн его нет: штатный фейковый спавн и повторный запуск через 2 с.
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
