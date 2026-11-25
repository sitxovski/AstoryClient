// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПАРА СЛОТОВ КОЛЕЦ 5/6» ПУТЁМ ИГРОКА (26.10.2026, баг игроков «Second ring slot is non-functional and prevents
//   equipping a second ring»). [ОРИГИНАЛ]
//
//   ЧТО БЫЛО. Порт слал в 5000 всегда `dstSlot = itemType` (у колец = 5) и не учитывал слот-цель броска ⇒ второе кольцо
//   двойным кликом и броском на слот 6 только МЕНЯЛОСЬ местами с первым (журнал игрока Player52 25.10.2026 20:55: трижды
//   «обмен местами (2:9 → 3:5)» при пустом слоте 6). Исправление и разбор эталона — `EquipItemToSlot`
//   (`AsdaInventorySubsystem_Use.cpp`, ключ `asda.Inven.RingSlotPick`).
//
//   ПУТЬ ИГРОКА (законы diagnostic-must-use-real-path, ui-check-game-path-geometry): двойные щелчки и перетаскивания —
//   НАСТОЯЩИЕ события Slate (`UAsdaWindowSubsystem::SimulateClickAt/SimulateDrag`) по ЖИВОЙ геометрии ячеек сумки
//   (вкладка расходников = контейнер 2) и слотов манекена `EQUIP_SET<n>`; 5000 уходит БОЕВЫМ отправителем под перехватом
//   (`AsdaSelfTestKit::ArmCapture`, в сеть ничего), ответ 5001 подаётся штатным приёмом (`DebugFeedPacket`) — так, как
//   его собирает сервер (`[результат][вид 1 — обмен / 2 — переезд][22 б запроса]`, `CharItemMove@0x4CE3E0`).
//   Оракул — правило эталона, записанное здесь НЕЗАВИСИМО от боевого кода:
//     двойной клик:  5 пуст → 5; 5 занят и 6 пуст → 6; оба заняты → 5 (замена)   (`CInventory_OnSlotUse@0xD07460`);
//     бросок:        на слот 5 → 5, на 6 → 6, на любой другой слот манекена — как двойной клик (`netItem_SendUseRequest@0xD19A70`);
//     не кольцо:     `itemType`, куда бы ни бросили (ожерелье на слот 6 → 7).
//   Разделы: A/B двойной клик ×2 → слоты 5 и 6; C третье кольцо → замена в 5; D снятие слота 6 двойным кликом (dstType 2);
//   E бросок в пустой 6; F бросок в занятый 6 (замена); G бросок в занятый 5; H бросок на шлем → по правилу (5);
//   I ожерелье на слот 6 → 7; J ключ 0 — прежнее поведение (контроль: двойной клик и бросок на 6 → 5); K ключ 1 — бросок на 6.
//   Состояние (расходники 0…59, надетое 0…10, окно, ключ, перехват) восстанавливается в конце.
//
// Консоль: `asda.SelfTest.Rings [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`), на карте мира.
//   Строки проверок — «КОЛЬЦА-ТЕСТ ok/FAIL», итог — «КОЛЬЦА-ТЕСТ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): шаги по событию «ввод доставлен», а не по часам
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRingTest, Log, All);

namespace AsdaRingTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.Rings");   // имя команды (и ключ реестра ключей консоли)
	static const TCHAR* kKey = TEXT("asda.Inven.RingSlotPick");   // ключ исправления (1 — как оригинал)

	// Перехваченная отправка 5000: опкод и 22 б запроса.
	struct FSent { int32 Op = 0; TArray<uint8> Body; };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                   // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		TSharedPtr<AsdaSelfTestSeq::FSeq> Seq;      // очередь шагов `At` (★ R5-fix-b 13.11.2026 (заключительный заход))
		bool bQuit = false;                       // выйти после итога
		TArray<TArray<uint8>> SavedSpend;         // расходники 0…59 до прогона (пусто — ячейка была пуста)
		TArray<TPair<int32, int32>> SavedEquip;   // надетое 0…10 до прогона (код, количество)
		uint32 CapGen = 0;                        // поколение перехвата отправок
		TArray<FSent> Sent;                       // перехваченные отправки
		int32 Mark = 0;                           // число отправок на начало шага
		int32 R1 = 0, R2 = 0, R3 = 0, Neck = 0;   // три кольца (itemType 5) и ожерелье (itemType 7)

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaRingTest, Log,   TEXT("КОЛЬЦА-ТЕСТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaRingTest, Error, TEXT("КОЛЬЦА-ТЕСТ FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Ячейки расходников (контейнер 2) набора.
	static constexpr int32 kCellR1 = 0, kCellR2 = 1, kCellR3 = 2, kCellNeck = 3;

	// Шаг через Sec секунд от старта.
	// ★ R5-fix-b 13.11.2026 (заключительный заход): расписание — общая очередь `AsdaSelfTestSeq::At` (запись та же): шаг ждёт доставки заказанного
	//   клика/перетаскивания и тот же промежуток после предыдущего шага. Под нагрузкой (три игры, `rx_r5fix3`) мировой таймер
	//   проверял до отпускания перетаскивания (30 кадров удержания), следующий заказ затирал недоставленный — каскад провалов.
	using AsdaSelfTestSeq::At;

	// Центр живой геометрии узла (false — не отрисован / скрыт / нулевой размер).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).

	// Двойной щелчок НАСТОЯЩИМИ событиями Slate: второй — как только первый доставлен (`IsSimClickPending`).
	static void DoubleClick(UAsdaWindowSubsystem* WM, const FVector2D& P)
	{
		if (!WM) { return; }
		WM->SimulateClickAt(P, /*HoldFrames*/ 1);
		TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, P](float) -> bool
		{
			UAsdaWindowSubsystem* W2 = WW.Get();
			if (!W2) { return false; }
			if (W2->IsSimClickPending()) { return true; }
			W2->SimulateClickAt(P, /*HoldFrames*/ 1);
			return false;
		}), 0.f);
	}

	// Код в ячейке (0 — пусто).
	static int32 CodeAt(const FCtxRef& C, int32 Cont, int32 Cell)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		const FAsdaItemInstance* I = Inv ? Inv->GetCell(Cont, Cell) : nullptr;
		return (I && !I->IsEmpty()) ? I->ItemCode : 0;
	}

	// Двойной щелчок по ячейке сетки расходников (видимая страница 0 вкладки 0 = контейнер 2).
	static void DblGrid(const FCtxRef& C, int32 Cell, const TCHAR* Tag)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		FVector2D P;
		const bool bGeo = Inv && Center(Inv->DebugGetGridIcon(Cell), P);
		C->Expect(bGeo, FString::Printf(TEXT("%s ячейка расходников %d отрисована"), Tag, Cell));
		C->Mark = C->Sent.Num();
		if (bGeo) { DoubleClick(C->Sub<UAsdaWindowSubsystem>(), P); }
	}

	// Двойной щелчок по слоту манекена.
	static void DblEquip(const FCtxRef& C, int32 Slot, const TCHAR* Tag)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		FVector2D P;
		const bool bGeo = Inv && Center(Inv->DebugGetEquipSlotIcon(Slot), P);
		C->Expect(bGeo, FString::Printf(TEXT("%s слот манекена %d отрисован"), Tag, Slot));
		C->Mark = C->Sent.Num();
		if (bGeo) { DoubleClick(C->Sub<UAsdaWindowSubsystem>(), P); }
	}

	// Перетаскивание из ячейки расходников на слот манекена.
	static void DragToEquip(const FCtxRef& C, int32 Cell, int32 Slot, const TCHAR* Tag)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		FVector2D From = FVector2D::ZeroVector, To = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
		const bool bGeo = Inv && Center(Inv->DebugGetGridIcon(Cell), From) && Center(Inv->DebugGetEquipSlotIcon(Slot), To);
		C->Expect(bGeo, FString::Printf(TEXT("%s ячейка %d и слот манекена %d отрисованы"), Tag, Cell, Slot));
		C->Mark = C->Sent.Num();
		if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateDrag(From, To, /*HoldFrames*/ 30); }
	}

	// Ровно одна отправка 5000 с шага: src(тип, слот) → dst(тип, слот). Возвращает её тело (пусто — нет/не та).
	static TArray<uint8> ExpectMove(const FCtxRef& C, const TCHAR* Tag, int32 SrcT, int32 SrcS, int32 DstT, int32 DstS)
	{
		TArray<const FSent*> New;
		for (int32 i = C->Mark; i < C->Sent.Num(); ++i) { if (C->Sent[i].Op == 5000) { New.Add(&C->Sent[i]); } }
		if (New.Num() != 1 || New[0]->Body.Num() < 22)
		{
			C->Expect(false, FString::Printf(TEXT("%s ушла ровно одна 5000 (ушло %d)"), Tag, New.Num()));
			return {};
		}
		const uint8* P = New[0]->Body.GetData();
		const int32 GotSrcS = AsdaBytes::RdI16(P + 0), GotSrcT = P[4];
		const int32 GotDstS = AsdaBytes::RdI16(P + 11), GotDstT = P[15];
		const bool b = GotSrcT == SrcT && GotSrcS == SrcS && GotDstT == DstT && GotDstS == DstS;
		C->Expect(b, FString::Printf(TEXT("%s 5000: src(%d:%d) → dst(тип %d, слот %d) — ждали src(%d:%d) → dst(%d, %d)"),
			Tag, GotSrcT, GotSrcS, GotDstT, GotDstS, SrcT, SrcS, DstT, DstS));
		return b ? New[0]->Body : TArray<uint8>();
	}

	// Ответ 5001 штатным приёмом: [результат][вид][22 б запроса]; `FinalDst` ≥ 0 — конечный слот (снятие: сервер выбрал сам).
	static void Reply(const FCtxRef& C, const TArray<uint8>& Req, int32 Result, int32 SubKind, int32 FinalDst = -1)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Net || Req.Num() < 22) { return; }
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = (uint8)Result;
		B[1] = (uint8)SubKind;
		FMemory::Memcpy(B.GetData() + 2, Req.GetData(), 22);
		if (FinalDst >= 0) { AsdaBytes::WrI16(B.GetData() + 2 + 11, FinalDst); }
		Net->DebugFeedPacket(5001, B);
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>()) { Inv->DebugPumpPendingPackets(); }
	}

	// Тихий отказ (код 18 — эталон молчит): снять ожидание ответа после контрольной отправки.
	static void ReplySilentRefuse(const FCtxRef& C, const TArray<uint8>& Req) { Reply(C, Req, 18, 0); }

	// Ожидаемое содержимое ячейки расходников.
	struct FCellWant { int32 Cell; int32 Code; };

	// Слоты 5/6/7 и ячейки — как ждём.
	static void ExpectState(const FCtxRef& C, const TCHAR* Tag, int32 S5, int32 S6, int32 S7, std::initializer_list<FCellWant> Cells)
	{
		const int32 G5 = CodeAt(C, 3, 5), G6 = CodeAt(C, 3, 6), G7 = CodeAt(C, 3, 7);
		C->Expect(G5 == S5 && G6 == S6 && G7 == S7, FString::Printf(TEXT("%s надето: слот5=%d слот6=%d слот7=%d (ждали %d/%d/%d)"),
			Tag, G5, G6, G7, S5, S6, S7));
		for (const FCellWant& Pr : Cells)
		{
			const int32 G = CodeAt(C, 2, Pr.Cell);
			C->Expect(G == Pr.Code, FString::Printf(TEXT("%s расходники[%d]=%d (ждали %d)"), Tag, Pr.Cell, G, Pr.Code));
		}
	}

	// Подобрать предметы по таблице (оракул типа — сама запись item_list, не боевой код).
	static bool PickData(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!IT) { return false; }
		auto TypeOf = [IT](int32 Code) { const FAsdaItemRecord* R = IT->GetItem(Code); return (R && R->TooltipFlag200 != 1) ? R->ItemType : -1; };
		C->R1 = 20649; C->R2 = 20655; C->R3 = 20661; C->Neck = 20635;   // Кольцо Понтиоса / волшебного зеркала / обороны Востока; Ожерелье слабости
		return TypeOf(C->R1) == 5 && TypeOf(C->R2) == 5 && TypeOf(C->R3) == 5 && TypeOf(C->Neck) == 7;
	}

	static void Finish(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (Net && C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; }
		if (Inv && C->SavedSpend.Num() == 60 && C->SavedEquip.Num() == 11)
		{
			for (int32 I = 0; I < 60; ++I)
			{
				if (C->SavedSpend[I].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedSpend[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(2, I); }
			}
			for (int32 S = 0; S < 11; ++S) { Inv->DebugPutItem(3, S, C->SavedEquip[S].Key, C->SavedEquip[S].Value); }
		}
		if (Inv && Inv->IsWindowOpen()) { Inv->CloseWindow(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaRingTest, TEXT("КОЛЬЦА-ТЕСТ"), C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Inv || !WM || !Net) { C->Expect(false, TEXT("подсистемы инвентаря/окон/сети есть")); Finish(C); return; }
		if (!PickData(C))
		{
			C->Expect(false, TEXT("данные: кольца 20649/20655/20661 (itemType 5) и ожерелье 20635 (itemType 7) без привязки при надевании"));
			Finish(C);
			return;
		}
		C->Expect(true, FString::Printf(TEXT("данные: кольца %d/%d/%d, ожерелье %d"), C->R1, C->R2, C->R3, C->Neck));
		// Перехват ВСЕХ отправок — боевой отправитель 5000 доходит до него, в сеть не уходит ничего.
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { FSent S; S.Op = (int32)Op; if (P && Len > 0) { S.Body.Append(P, Len); } X->Sent.Add(MoveTemp(S)); }
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват отправок свободен")); Finish(C); return; }

		// Снимок расходников и надетого, затем свой набор: кольца в 0/1/2, ожерелье в 3, слоты 5/6/7 пусты.
		for (int32 I = 0; I < 60; ++I) { const FAsdaItemInstance* X = Inv->GetCell(2, I); C->SavedSpend.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		for (int32 S = 0; S < 11; ++S) { const FAsdaItemInstance* X = Inv->GetCell(3, S); C->SavedEquip.Emplace(X && !X->IsEmpty() ? X->ItemCode : 0, X && !X->IsEmpty() ? X->Amount : 0); }
		for (int32 I = 0; I < 60; ++I) { Inv->ClearCell(2, I); }
		for (int32 S = 5; S <= 7; ++S) { Inv->DebugPutItem(3, S, 0, 0); }
		auto Put = [Inv](int32 Code, int32 Cell) { Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Code, 2, Cell, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs); };
		Put(C->R1, kCellR1); Put(C->R2, kCellR2); Put(C->R3, kCellR3); Put(C->Neck, kCellNeck);
		AsdaCVarSave::SetForTest(kKey, 1);
		if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
		WM->BringWindowToFront(FName(TEXT("INVEN_WIN")));
		Inv->SetTab(0);   // вкладка расходников = контейнер 2 (`GetGridContainer`)

		// ═══ A. Двойной клик по кольцу №1 — оба слота пусты → слот 5 ═══
		At(C, 1.0f, [C]() { DblGrid(C, kCellR1, TEXT("A")); });
		At(C, 1.8f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("A двойной клик, 5 и 6 пусты:"), 2, kCellR1, 3, 5);
			Reply(C, R, 1, /*переезд*/ 2);
			ExpectState(C, TEXT("A после 5001:"), C->R1, 0, 0, { { kCellR1, 0 } });
		});
		// ═══ B. Двойной клик по кольцу №2 — слот 5 занят, 6 пуст → слот 6 (суть бага) ═══
		At(C, 2.6f, [C]() { DblGrid(C, kCellR2, TEXT("B")); });
		At(C, 3.4f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("B двойной клик, 5 занят и 6 пуст:"), 2, kCellR2, 3, 6);
			Reply(C, R, 1, 2);
			ExpectState(C, TEXT("B после 5001:"), C->R1, C->R2, 0, { { kCellR2, 0 } });
		});
		// ═══ C. Третье кольцо — оба заняты → замена в слоте 5 (кольцо №1 уезжает в ячейку третьего) ═══
		At(C, 4.2f, [C]() { DblGrid(C, kCellR3, TEXT("C")); });
		At(C, 5.0f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("C двойной клик, оба заняты (замена):"), 2, kCellR3, 3, 5);
			Reply(C, R, 1, /*обмен*/ 1);
			ExpectState(C, TEXT("C после 5001:"), C->R3, C->R2, 0, { { kCellR3, C->R1 } });
		});
		// ═══ D. Снятие кольца из слота 6 двойным кликом по манекену → dstType 2, dstSlot 0 (место выбирает сервер) ═══
		At(C, 5.8f, [C]() { DblEquip(C, 6, TEXT("D")); });
		At(C, 6.6f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("D снятие слота 6:"), 3, 6, 2, 0);
			Reply(C, R, 1, 2, /*сервер положил в*/ kCellR2);
			ExpectState(C, TEXT("D после 5001:"), C->R3, 0, 0, { { kCellR2, C->R2 } });
		});
		// ═══ E. Бросок кольца №2 на ПУСТОЙ слот 6 → слот 6 ═══
		At(C, 7.4f, [C]() { DragToEquip(C, kCellR2, 6, TEXT("E")); });
		At(C, 9.0f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("E бросок на пустой слот 6:"), 2, kCellR2, 3, 6);
			Reply(C, R, 1, 2);
			ExpectState(C, TEXT("E после 5001:"), C->R3, C->R2, 0, { { kCellR2, 0 } });
		});
		// ═══ F. Бросок кольца №1 на ЗАНЯТЫЙ слот 6 → слот 6 (замена: кольцо №2 уезжает в ячейку №1) ═══
		At(C, 9.6f, [C]() { DragToEquip(C, kCellR3, 6, TEXT("F")); });
		At(C, 11.2f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("F бросок на занятый слот 6 (замена):"), 2, kCellR3, 3, 6);
			Reply(C, R, 1, 1);
			ExpectState(C, TEXT("F после 5001:"), C->R3, C->R1, 0, { { kCellR3, C->R2 } });
		});
		// ═══ G. Бросок кольца №2 на ЗАНЯТЫЙ слот 5 (6 тоже занят) → слот 5 ═══
		At(C, 11.8f, [C]() { DragToEquip(C, kCellR3, 5, TEXT("G")); });
		At(C, 13.4f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("G бросок на занятый слот 5:"), 2, kCellR3, 3, 5);
			Reply(C, R, 1, 1);
			ExpectState(C, TEXT("G после 5001:"), C->R2, C->R1, 0, { { kCellR3, C->R3 } });
		});
		// ═══ H. Бросок кольца на слот ШЛЕМА (0) — эталон считает «первый свободный»: оба заняты → 5 ═══
		At(C, 14.0f, [C]() { DragToEquip(C, kCellR3, 0, TEXT("H")); });
		At(C, 15.6f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("H бросок на слот шлема:"), 2, kCellR3, 3, 5);
			Reply(C, R, 1, 1);
			ExpectState(C, TEXT("H после 5001:"), C->R3, C->R1, 0, { { kCellR3, C->R2 } });
		});
		// ═══ I. Ожерелье на слот 6 → его слот 7 (не кольцо — правило пары не действует) ═══
		At(C, 16.2f, [C]() { DragToEquip(C, kCellNeck, 6, TEXT("I")); });
		At(C, 17.8f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("I ожерелье брошено на слот 6:"), 2, kCellNeck, 3, 7);
			Reply(C, R, 1, 2);
			ExpectState(C, TEXT("I после 5001:"), C->R3, C->R1, C->Neck, { { kCellNeck, 0 } });
		});
		// ═══ J. КОНТРОЛЬ: ключ 0 — прежнее поведение порта (слот 6 освобождён; двойной клик и бросок на 6 → 5) ═══
		At(C, 18.4f, [C]()
		{
			AsdaCVarSave::SetForTest(kKey, 0);
			if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>()) { Inv->DebugPutItem(3, 6, 0, 0); }
			DblGrid(C, kCellR3, TEXT("J1"));
		});
		At(C, 19.2f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("J1 КОНТРОЛЬ ключ 0, двойной клик при пустом 6 — дефект воспроизведён:"), 2, kCellR3, 3, 5);
			ReplySilentRefuse(C, R);
		});
		At(C, 19.8f, [C]() { DragToEquip(C, kCellR3, 6, TEXT("J2")); });
		At(C, 21.4f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("J2 КОНТРОЛЬ ключ 0, бросок на слот 6 — дефект воспроизведён:"), 2, kCellR3, 3, 5);
			ReplySilentRefuse(C, R);
			AsdaCVarSave::SetForTest(kKey, 1);
		});
		// ═══ K. Ключ 1 снова: тот же бросок на пустой слот 6 → слот 6 ═══
		At(C, 22.0f, [C]() { DragToEquip(C, kCellR3, 6, TEXT("K")); });
		At(C, 23.6f, [C]()
		{
			const TArray<uint8> R = ExpectMove(C, TEXT("K ключ 1, бросок на пустой слот 6:"), 2, kCellR3, 3, 6);
			Reply(C, R, 1, 2);
			ExpectState(C, TEXT("K после 5001:"), C->R3, C->R2, C->Neck, { { kCellR3, 0 } });
			// Ушло только 5000 — других отправок путь надевания колец не порождает.
			int32 Other = 0;
			for (const FSent& S : C->Sent) { if (S.Op != 5000) { ++Other; } }
			C->Expect(Other == 0, FString::Printf(TEXT("за прогон ушли только 5000 (прочих отправок %d)"), Other));
		});
		At(C, 24.2f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestRings(
	TEXT("asda.SelfTest.Rings"),
	TEXT("Самотест пары слотов колец 5/6 путём игрока: двойной клик ×2 → слоты 5 и 6, третье кольцо — замена в 5, снятие, бросок "
	     "в любой из двух слотов (пустой/занятый), ожерелье на слот 6 → 7, контроль ключа. 5000 под перехватом, 5001 штатным приёмом. "
	     "Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaRingTest;
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
