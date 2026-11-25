// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ОТКАЗ ОТ ЗАДАНИЯ» ПУТЁМ ИГРОКА (31.10.2026, баг игроков: при отказе от задания в журнале клиент пишет
//   «Нельзя удалить предмет» / "Cannot delete item", задание не уходит из списка до перезапуска). [ИСПРАВЛЕНИЕ]
//
//   ЧТО БЫЛО. Отказ уходит ОБЩИМ с уничтожением предмета опкодом 5006 (под-тип 4), сервер отвечает ТЕМ ЖЕ 5007
//   (`sendCharQuestDump@0x4DB510`: {1, ячейка, тип 4} — успех, ячейка на сервере уже пуста; {0, ячейка, тип 4} — провал БД;
//   {0} без типа — отказ проверки в `CharItemDump`). Порт отдавал 5007 только инвентарю, а тот тип 4 выбрасывал — задание
//   оставалось в журнале/трекере/метках; игрок отказывался снова → сервер: ячейка пуста → {0} → нотис 6956. Исправление —
//   `UAsdaQuestLogSubsystem::ConsumeItemDump5007` + защёлка `acceptPending` в `AbandonSelected` (ключ `asda.QuestLog.AbandonAck`).
//
//   ПУТЬ ИГРОКА: журнал — пакетом 5047 через `DebugFeedPacket` (штатный приём); выбор ячейки, корзина 713 и кнопки окна-вопроса —
//   НАСТОЯЩИЕ щелчки Slate (`SimulateClickAt`) по живой геометрии; 5006 уходит боевым отправителем под перехватом (в сеть ничего);
//   ответ 5007 — штатным приёмом (`DebugFeedPacket` → `OnItemDumpResult` → очередь инвентаря → `ApplyDumpResult`).
//   Оракул — правило эталона/сервера, записанное здесь независимо от боевого кода:
//     корзина при выбранной ячейке → вопрос TWOBUT (7, 6174) с текстом строки 6174 и названием, «без Enter»;
//     «Нет» → 5006 нет; «Да» → ровно одна 5006 = 11 б [ячейка сервера i16][0][4][0…];
//     пока ответа нет — повторный отказ не шлётся (эталон `acceptPending != 1`);
//     5007 {1, ячейка, 4} → ячейка журнала пуста, соседняя цела, отслеживание снято, строки 6956 нет;
//     5007 {0, ячейка, 4} и {0} без типа при ожидании отказа → строка журнала 6192 «Ошибка данных задания», НЕ 6956, задание
//       на месте, отслеживание возвращено, защёлка снята; 5007 {0} без ожидания отказа — это уничтожение предмета → 6956 (регресс);
//     молчание сервера дольше 10 с — защёлка снимается (провал без типа идёт инвентарю), корзина снова поднимает вопрос.
//   Разделы: A контроль ключа 0 (дефект воспроизведён = шаг 0); B «Нет»; C «Да» → 5006 + защёлка; D 5007 успех; E провал {0,4};
//   F провал {0} без типа; G регресс: 5007 {0} предмета → 6956; H молчание сервера → защёлка снята.
//
// Консоль: `asda.SelfTest.QuestAbandon [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`), на карте мира.
//   Строки проверок — «ЖУРНАЛ-ОТК-ТЕСТ ok/FAIL», итог — «ЖУРНАЛ-ОТК-ТЕСТ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuestAbandonTest, Log, All);

namespace AsdaQuestAbandonTest
{
	static const TCHAR* kCmd    = TEXT("asda.SelfTest.QuestAbandon");   // имя команды
	static const TCHAR* kKey    = TEXT("asda.QuestLog.AbandonAck");     // ключ исправления (1 — исправлено)
	static const TCHAR* kTwoBut = TEXT("TWOBUT_MSGBOX");                // окно-вопрос 1520
	static const TCHAR* kQuest  = TEXT("QUEST_WIN");                    // журнал заданий 700

	// Две записи журнала: эпизод, номер задания, ячейка (она же поле +5 — номер ячейки сервера).
	static constexpr int32 kEpA = 2020, kQnA = 19, kCellA = 3;
	static constexpr int32 kEpB = 2022, kQnB = 21, kCellB = 5;

	// Перехваченная отправка: опкод и тело.
	struct FSent { int32 Op = 0; TArray<uint8> Body; };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;           // пройдено / провалено
		TWeakObjectPtr<UWorld> W;         // мир прогона
		bool bQuit = false;               // выйти после итога
		uint32 CapGen = 0;                // поколение перехвата отправок
		TArray<FSent> Sent;               // перехваченные отправки
		int32 Mark = 0;                   // число отправок на начало шага
		FString TxtItem, TxtQuest;        // строки 6956 («Нельзя удалить предмет») и 6192 на языке сессии
		int32 NItem0 = 0, NQuest0 = 0;    // число таких строк в журнале чата на начало шага

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaQuestAbandonTest, Log,   TEXT("ЖУРНАЛ-ОТК-ТЕСТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaQuestAbandonTest, Error, TEXT("ЖУРНАЛ-ОТК-ТЕСТ FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Центр живой геометрии узла (false — не отрисован / скрыт / нулевой размер).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).
	// Узел окна по имени.
	static UWidget* Node(const FCtxRef& C, const TCHAR* Win, const TCHAR* Name)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Wd = WM ? WM->FindWindow(FName(Win)) : nullptr;
		return Wd ? AsdaUi::FindNodeByName(Wd, Name, nullptr) : nullptr;
	}
	// Настоящий щелчок по узлу окна.
	static void Click(const FCtxRef& C, const TCHAR* Win, const TCHAR* Name, const TCHAR* Tag)
	{
		FVector2D P = FVector2D::ZeroVector;
		const bool bGeo = Center(Node(C, Win, Name), P);
		C->Expect(bGeo, FString::Printf(TEXT("%s узел %s окна %s на экране"), Tag, Name, Win));
		if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, /*HoldFrames*/ 2); }
	}
	// Выбрать ячейку журнала щелчком и нажать корзину 713.
	static void ClickTrashOn(const FCtxRef& C, int32 Cell, const TCHAR* Tag)
	{
		C->Mark = C->Sent.Num();
		Click(C, kQuest, *FString::Printf(TEXT("QUEST_INVEN%d_HIT"), Cell), Tag);
		At(C, 0.25f, [C, Tag]() { Click(C, kQuest, TEXT("QUEST_TRASHBUT"), Tag); });
	}

	// Пакет 5047 (688 б): записи журнала + пустая карта завершения.
	static TArray<uint8> Make5047(bool bA, bool bB)
	{
		TArray<uint8> B; B.SetNumZeroed(688);
		auto Put = [&B](int32 Cell, int32 Ep, int32 Qn)
		{
			uint8* R = B.GetData() + 44 * Cell;
			AsdaBytes::WrI32(R + 0, Ep);   // +0 эпизод
			R[4] = 4;                      // +4 тип записи = ячейка журнала квестов
			AsdaBytes::WrI16(R + 5, Cell); // +5 номер ячейки сервера
			AsdaBytes::WrI16(R + 8, Qn);   // +8 номер задания
			AsdaBytes::WrI16(R + 10, 0);   // +10 стадия
		};
		if (bA) { Put(kCellA, kEpA, kQnA); }
		if (bB) { Put(kCellB, kEpB, kQnB); }
		return B;
	}
	// Ответ 5007 штатным приёмом. `Kind` < 0 — тело без типа (отказ проверки `CharItemDump`, LABEL_50).
	static void Reply5007(const FCtxRef& C, int32 Result, int32 Slot, int32 Kind)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (!Net || !Inv) { return; }
		TArray<uint8> B; B.SetNumZeroed(18);
		B[0] = (uint8)Result;
		if (Kind >= 0) { AsdaBytes::WrI16(B.GetData() + 1, Slot); B[5] = (uint8)Kind; }
		Net->DebugFeedPacket(5007, B);
		Inv->DebugPumpPendingPackets();
	}
	// Отправки 5006 с шага (последняя — в OutLast).
	static int32 Sent5006(const FCtxRef& C, TArray<uint8>* OutLast = nullptr)
	{
		int32 N = 0;
		for (int32 i = C->Mark; i < C->Sent.Num(); ++i) { if (C->Sent[i].Op == 5006) { ++N; if (OutLast) { *OutLast = C->Sent[i].Body; } } }
		return N;
	}
	// Ровно одна 5006 отказа: хвост 11 б = [ячейка i16][0 i16][4 u8][0…].
	static void Expect5006(const FCtxRef& C, const TCHAR* Tag, int32 Slot)
	{
		TArray<uint8> B;
		const int32 N = Sent5006(C, &B);
		if (N != 1 || B.Num() < 11) { C->Expect(false, FString::Printf(TEXT("%s ушла ровно одна 5006 (ушло %d, тело %d б)"), Tag, N, B.Num())); return; }
		const uint8* P = B.GetData() + B.Num() - 11;
		const int32 GSlot = AsdaBytes::RdI16(P + 0), GZero = AsdaBytes::RdI16(P + 2), GKind = P[4];
		C->Expect(GSlot == Slot && GZero == 0 && GKind == 4,
			FString::Printf(TEXT("%s 5006: ячейка %d, второе поле %d, под-тип %d (ждали %d/0/4)"), Tag, GSlot, GZero, GKind, Slot));
	}
	// Занята ли ячейка журнала и каким заданием (0 — пусто).
	static int32 QuestAt(const FCtxRef& C, int32 Cell)
	{
		UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
		if (!QM || !QM->GetSlots().IsValidIndex(Cell)) { return 0; }
		const FAsdaQuestSlot& S = QM->GetSlots()[Cell];
		return S.QuestId >= 2001 ? S.QuestNo : 0;
	}
	// Вопрос (7, 6174) показан: текст начинается со строки 6174, пометка «без Enter».
	static void ExpectAsk(const FCtxRef& C, const TCHAR* Tag)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UWidget* T = Node(C, kTwoBut, TEXT("TWOBUT_TEXT"));
		const FString Shown = UAsdaWindowSubsystem::ReadMsgBoxShownText(T);
		const FString Want = AsdaNotice::Loc(C->GI(), 6174);
		C->Expect(WM && WM->IsMsgBoxQueued(7, 6174), FString::Printf(TEXT("%s показан вопрос (7, 6174)"), Tag));
		C->Expect(!Want.IsEmpty() && Shown.StartsWith(Want),
			FString::Printf(TEXT("%s текст «%s» (строка 6174 «%s»)"), Tag, *Shown.Replace(TEXT("\n"), TEXT(" / ")), *Want));
		C->Expect(WM && WM->IsDisplayedMsgBoxNoEnter(), FString::Printf(TEXT("%s вопрос помечен «без Enter»"), Tag));
	}
	// Счётчики строк 6956 / 6192 в журнале чата на начало шага.
	static void MarkChat(const FCtxRef& C)
	{
		UAsdaChatSubsystem* Chat = C->Sub<UAsdaChatSubsystem>();
		C->NItem0  = Chat ? Chat->DebugCountSystemLines(C->TxtItem) : 0;
		C->NQuest0 = Chat ? Chat->DebugCountSystemLines(C->TxtQuest) : 0;
	}
	// Прирост строк 6956 / 6192 с начала шага.
	static void ChatDelta(const FCtxRef& C, int32& OutItem, int32& OutQuest)
	{
		UAsdaChatSubsystem* Chat = C->Sub<UAsdaChatSubsystem>();
		OutItem  = Chat ? Chat->DebugCountSystemLines(C->TxtItem) - C->NItem0 : -1;
		OutQuest = Chat ? Chat->DebugCountSystemLines(C->TxtQuest) - C->NQuest0 : -1;
	}
	// Журнал из 5047 и оба задания на месте.
	static void Feed(const FCtxRef& C, const TCHAR* Tag)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5047, Make5047(true, true)); }
		C->Expect(QuestAt(C, kCellA) == kQnA && QuestAt(C, kCellB) == kQnB,
			FString::Printf(TEXT("%s 5047: ячейка %d = задание %d, ячейка %d = задание %d"), Tag, kCellA, QuestAt(C, kCellA), kCellB, QuestAt(C, kCellB)));
	}

	static void Finish(const FCtxRef& C)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
		if (WM && WM->IsMsgBoxQueued(7, 6174)) { WM->CloseConfirm(false); }
		// Отслеживание заданий прогона снять до пустого журнала: трекер ключуется номером задания и пережил бы 5047.
		if (QL) { for (int32 Cell : { kCellA, kCellB }) { if (QL->IsSlotTracked(Cell)) { QL->SetSlotTracked(Cell, false); } } }
		if (QL && QL->IsOpen()) { QL->Close(); }
		if (Net) { Net->DebugFeedPacket(5047, Make5047(false, false)); }   // журнал прогона — пустой (офлайн своего журнала нет)
		if (Net && C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaQuestAbandonTest, TEXT("ЖУРНАЛ-ОТК-ТЕСТ"), C->Ok, C->Fail);
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
		C->TxtItem  = AsdaNotice::Loc(C->GI(), 6956);
		C->TxtQuest = AsdaNotice::Loc(C->GI(), 6192);
		C->Expect(!C->TxtItem.IsEmpty() && !C->TxtQuest.IsEmpty(),
			FString::Printf(TEXT("строки 6956 «%s» и 6192 «%s» есть"), *C->TxtItem, *C->TxtQuest));
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { FSent S; S.Op = (int32)Op; if (P && Len > 0) { S.Body.Append(P, Len); } X->Sent.Add(MoveTemp(S)); }
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват отправок свободен")); Finish(C); return; }

		AsdaCVarSave::SetForTest(kKey, 0);
		Feed(C, TEXT("A"));
		if (!QL->IsOpen()) { QL->Open(); }
		WM->BringWindowToFront(FName(kQuest));

		// ═══ A. КОНТРОЛЬ (шаг 0): ключ 0 — 5007 успеха игнорируется, повторный отказ → 5007 {0} → «Нельзя удалить предмет» ═══
		At(C, 1.0f, [C]() { ClickTrashOn(C, kCellA, TEXT("A")); });
		At(C, 2.0f, [C]() { ExpectAsk(C, TEXT("A")); Click(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("A «Да»:")); });
		At(C, 2.8f, [C]()
		{
			Expect5006(C, TEXT("A"), kCellA);
			Reply5007(C, 1, kCellA, 4);
			C->Expect(QuestAt(C, kCellA) == kQnA, TEXT("A КОНТРОЛЬ ключ 0: после 5007 {1,4} задание осталось в журнале (дефект воспроизведён)"));
		});
		At(C, 3.2f, [C]() { ClickTrashOn(C, kCellA, TEXT("A повтор")); });
		At(C, 4.2f, [C]() { Click(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("A повтор «Да»:")); });
		At(C, 5.0f, [C]()
		{
			C->Expect(Sent5006(C) == 1, FString::Printf(TEXT("A ключ 0: повторная 5006 ушла (защёлки нет), ушло %d"), Sent5006(C)));
			MarkChat(C);
			Reply5007(C, 0, 0, -1);   // сервер: ячейка уже пуста → `sub_532B30` ложь → LABEL_50 {0}
			int32 DI = 0, DQ = 0; ChatDelta(C, DI, DQ);
			C->Expect(DI == 1, FString::Printf(TEXT("A КОНТРОЛЬ ключ 0: в чате «%s» (+%d) — ошибочный нотис воспроизведён"), *C->TxtItem, DI));
			AsdaCVarSave::SetForTest(kKey, 1);
			Feed(C, TEXT("B"));
		});

		// ═══ B. Ключ 1: корзина → вопрос 6174 «без Enter» → «Нет» → 5006 нет, защёлки нет ═══
		At(C, 5.4f, [C]() { ClickTrashOn(C, kCellA, TEXT("B")); });
		At(C, 6.4f, [C]() { ExpectAsk(C, TEXT("B")); Click(C, kTwoBut, TEXT("TWOBUT_BUT01"), TEXT("B «Нет»:")); });
		At(C, 7.2f, [C]()
		{
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			C->Expect(WM && !WM->IsMsgBoxQueued(7, 6174), TEXT("B вопрос закрыт"));
			C->Expect(Sent5006(C) == 0 && !C->Sub<UAsdaQuestLogSubsystem>()->IsAbandonPending(), TEXT("B «Нет»: 5006 не ушла, ожидания нет"));
			C->Expect(QuestAt(C, kCellA) == kQnA, TEXT("B «Нет»: задание на месте"));
			// Отслеживание задания A — чтобы проверить снятие с трекера.
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			QL->SetSlotTracked(kCellA, true);
			C->Expect(QL->IsSlotTracked(kCellA), TEXT("B задание A отслеживается (трекер 1800)"));
		});

		// ═══ C. «Да» → ровно одна 5006 (ячейка A, тип 4), ожидание; повторная корзина до ответа — без вопроса и без 5006 ═══
		At(C, 7.6f, [C]() { ClickTrashOn(C, kCellA, TEXT("C")); });
		At(C, 8.6f, [C]() { ExpectAsk(C, TEXT("C")); Click(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("C «Да»:")); });
		At(C, 9.4f, [C]()
		{
			Expect5006(C, TEXT("C"), kCellA);
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			C->Expect(QL->IsAbandonPending(), TEXT("C ждём ответа 5007 (эталон `acceptPending = 1`)"));
			C->Expect(!QL->IsSlotTracked(kCellA), TEXT("C отслеживание снято при отправке (эталон `questTracker_RemoveEntry`)"));
		});
		At(C, 9.8f, [C]() { ClickTrashOn(C, kCellA, TEXT("C повтор")); });
		At(C, 10.8f, [C]()
		{
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			C->Expect(WM && !WM->IsMsgBoxQueued(7, 6174), TEXT("C повтор до ответа: вопроса нет"));
			C->Expect(Sent5006(C) == 0, TEXT("C повтор до ответа: 5006 не ушла"));
		});

		// ═══ D. 5007 {1, A, 4} → ячейка A пуста, B цела, ожидания нет, «Нельзя удалить предмет» нет ═══
		At(C, 11.2f, [C]()
		{
			MarkChat(C);
			Reply5007(C, 1, kCellA, 4);
			int32 DI = 0, DQ = 0; ChatDelta(C, DI, DQ);
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			C->Expect(QuestAt(C, kCellA) == 0, FString::Printf(TEXT("D 5007 {1,4}: ячейка %d журнала пуста (там %d)"), kCellA, QuestAt(C, kCellA)));
			C->Expect(QuestAt(C, kCellB) == kQnB, TEXT("D соседняя ячейка цела"));
			C->Expect(!QL->IsAbandonPending(), TEXT("D ожидание снято (эталон `acceptPending = 0`)"));
			C->Expect(QL->GetSelectedSlot() != kCellA, FString::Printf(TEXT("D выбор снят с пустой ячейки (выбрана %d)"), QL->GetSelectedSlot()));
			C->Expect(!QL->IsSlotTracked(kCellA), TEXT("D трекер задания A пуст"));
			C->Expect(DI == 0 && DQ == 0, FString::Printf(TEXT("D нотисов нет («%s» +%d, «%s» +%d)"), *C->TxtItem, DI, *C->TxtQuest, DQ));
		});

		// ═══ E. Провал БД: 5007 {0, B, 4} → строка 6192, не 6956; задание B на месте, отслеживание возвращено ═══
		At(C, 11.6f, [C]()
		{
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			QL->SetSlotTracked(kCellB, true);
			ClickTrashOn(C, kCellB, TEXT("E"));
		});
		At(C, 12.6f, [C]() { ExpectAsk(C, TEXT("E")); Click(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("E «Да»:")); });
		At(C, 13.4f, [C]()
		{
			Expect5006(C, TEXT("E"), kCellB);
			MarkChat(C);
			Reply5007(C, 0, kCellB, 4);
			int32 DI = 0, DQ = 0; ChatDelta(C, DI, DQ);
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			C->Expect(DQ == 1 && DI == 0, FString::Printf(TEXT("E 5007 {0,4}: в чате «%s» (+%d), «%s» нет (+%d)"), *C->TxtQuest, DQ, *C->TxtItem, DI));
			C->Expect(QuestAt(C, kCellB) == kQnB, TEXT("E задание B на месте"));
			C->Expect(!QL->IsAbandonPending(), TEXT("E ожидание снято (у эталона висело до конца сессии)"));
			C->Expect(QL->IsSlotTracked(kCellB), TEXT("E отслеживание задания B возвращено"));
		});

		// ═══ F. Отказ проверки сервера: 5007 {0} без типа при ожидании отказа → 6192, не 6956 ═══
		At(C, 13.8f, [C]() { ClickTrashOn(C, kCellB, TEXT("F")); });
		At(C, 14.8f, [C]() { Click(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("F «Да»:")); });
		At(C, 15.6f, [C]()
		{
			Expect5006(C, TEXT("F"), kCellB);
			MarkChat(C);
			Reply5007(C, 0, 0, -1);
			int32 DI = 0, DQ = 0; ChatDelta(C, DI, DQ);
			C->Expect(DQ == 1 && DI == 0, FString::Printf(TEXT("F 5007 {0} без типа: «%s» (+%d), «%s» нет (+%d)"), *C->TxtQuest, DQ, *C->TxtItem, DI));
			C->Expect(QuestAt(C, kCellB) == kQnB && !C->Sub<UAsdaQuestLogSubsystem>()->IsAbandonPending(), TEXT("F задание на месте, ожидания нет"));
		});

		// ═══ G. Регресс уничтожения предмета: 5007 {0} без ожидания отказа → инвентарю → «Нельзя удалить предмет» ═══
		At(C, 16.0f, [C]()
		{
			MarkChat(C);
			Reply5007(C, 0, 0, -1);
			int32 DI = 0, DQ = 0; ChatDelta(C, DI, DQ);
			C->Expect(DI == 1 && DQ == 0, FString::Printf(TEXT("G 5007 {0} предмета: «%s» (+%d), «%s» нет (+%d)"), *C->TxtItem, DI, *C->TxtQuest, DQ));
		});

		// ═══ H. Молчание сервера: 5006 без ответа → через 10 с корзина снова поднимает вопрос ═══
		At(C, 16.4f, [C]() { ClickTrashOn(C, kCellB, TEXT("H")); });
		At(C, 17.4f, [C]() { Click(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("H «Да»:")); });
		At(C, 18.2f, [C]()
		{
			Expect5006(C, TEXT("H"), kCellB);
			C->Expect(C->Sub<UAsdaQuestLogSubsystem>()->IsAbandonPending(), TEXT("H ждём ответа"));
		});
		// Ожидание старше 10 с уже не живо: провал без типа — это уничтожение предмета, инвентарю (ревизия 31.10, А1).
		At(C, 28.6f, [C]()
		{
			MarkChat(C);
			Reply5007(C, 0, 0, -1);
			int32 DI = 0, DQ = 0; ChatDelta(C, DI, DQ);
			C->Expect(DI == 1 && DQ == 0, FString::Printf(TEXT("H 5007 {0} через 10 с ожидания отказа — инвентарю: «%s» (+%d), «%s» нет (+%d)"),
				*C->TxtItem, DI, *C->TxtQuest, DQ));
		});
		At(C, 29.0f, [C]() { ClickTrashOn(C, kCellB, TEXT("H после 10 с")); });
		At(C, 30.0f, [C]()
		{
			ExpectAsk(C, TEXT("H после 10 с молчания:"));
			Click(C, kTwoBut, TEXT("TWOBUT_BUT01"), TEXT("H «Нет»:"));
		});
		At(C, 30.8f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestQuestAbandon(
	TEXT("asda.SelfTest.QuestAbandon"),
	TEXT("Самотест отказа от задания путём игрока: контроль ключа asda.QuestLog.AbandonAck 0, корзина 713 → вопрос 6174 «Нет»/«Да» → "
	     "5006 под-тип 4 → 5007 успех (задание снято) / провал (6192, не 6956) / без ответа (защёлка 10 с); регресс 5007 предмета. "
	     "5006 под перехватом. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaQuestAbandonTest;
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
