// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ЗАРЯДЫ P100 В КОРОБКАХ» (01.11.2026, клиент 5.1.4): `asda.SelfTest.ChargeStackReserve [quit]`. [ИСПРАВЛЕНИЕ]
//   Что проверяет — подбор ячеек-приёмников коробки ДО отправки 6096 (`UAsdaInventorySubsystem::ReservePackageSlots` →
//   `AsdaPackageReserve::Compute`, порт `itemFindSowelSlots@0xDAACF0`) по правилу сервера P100_ChargeStack (шапка
//   `Items/AsdaChargeStack.h`): складываемый спецпредмет (ключ > 0 по `DT_ChargeGroup`) со стопкой того же ключа в сумке
//   новую ячейку не требует; потолок 30 000; первая копия ключа без стопки занимает свободную ячейку, следующие копии
//   этой же выдачи сливаются в неё; ключ 0 — как сток. Офлайн (-AsdaNoNet), подставные записи сумки.
//   A. данные: таблица загружена (173 кода), коды подобраны (ключ A, ключ B той же группы с другой привязкой, код с зарядами
//      100, нескладываемый спецпредмет);
//   B. модель (`Compute` с подставной сумкой 60): полная сумка + стопка → ячейка не нужна; без стопки → нехватка; стопка
//      другой привязки → нехватка; стопка 29 950 + 100 → нужна ячейка (и ровно 30 000 — сливается); две позиции одного
//      ключа без стопки при одной свободной → хватает; несколько ключей; ключ 0 — как сток; переполнение первой стопки →
//      вторая стопка того же ключа; ключ поведения 0 → эталон;
//   C. путь игрока на НАСТОЯЩЕЙ коробке из `packagetable` (весь состав — складываемые спецпредметы): полная сумка 60 со
//      стопками → `ReservePackageSlots` без нехватки → `SendOpenPackage6096` уходит (перехват), слоты тела = стопки;
//      ключ 0 → нехватка, 6096 не уходит; подставной 6097 «как сервер P100» (запись в ячейку стопки с суммой) → ячейка
//      стопки получила сумму, лишняя ячейка не занята, коробка снята.
//   Состояние (сумка, расходники, сеть «свой персонаж», ключи, перехват) возвращается в конце. Итог: «ЗАРЯДЫ-P100 ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaChargeStack.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaChargeStackTest, Log, All);

namespace AsdaChargeStackTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.ChargeStackReserve");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Item.ChargeStackReserve");       // ключ поведения
	static constexpr int32 kBag = 60;                                      // ёмкость сумки (инвентарь ×2)

	// Перехваченная отправка.
	struct FSent { int32 Op = 0; TArray<uint8> Body; };

	// Контекст прогона (живёт до шага 6097 по таймеру).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		TArray<TArray<uint8>> SavedBag, SavedSpend;   // контейнеры 1/2 до прогона (пусто — ячейка была пустой)
		TUniquePtr<FAsdaNetSelfScope> Self;        // «свой персонаж» сети — вернуть в конце
		uint32 CapGen = 0;                         // поколение перехвата
		TArray<FSent> Sent;                        // перехваченные отправки

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaChargeStackTest, Log,   TEXT("ЗАРЯДЫ-P100 ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaChargeStackTest, Error, TEXT("ЗАРЯДЫ-P100 FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Подобранные коды (0 — не найден).
	struct FCodes
	{
		int32 A = 0, KeyA = 0, ChA = 0;      // складываемый код и его ключ/заряды
		int32 A2 = 0;                        // ДРУГОЙ код того же ключа (0 — в группе один код с этой привязкой)
		int32 B = 0, KeyB = 0;               // код той же группы с ДРУГОЙ привязкой (ключ ≠ KeyA)
		int32 C = 0, KeyC = 0;               // код третьего ключа (другая группа)
		int32 Big = 0, KeyBig = 0, ChBig = 0;   // код с зарядами 100 (или наибольшими ≥ 51)
		int32 NoKey = 0;                     // спецпредмет вне таблицы (ключ 0 — не складывается)
		int32 Bound = 0, KeyBound = 0;       // складываемый код с привязкой 2 (ключ % 10 == 2) — сервер ставит записи Owner 2
	};

	// Owner, который сервер (P100, ревизия 02.11 п.1) ставит записи коробки этого ключа: привязка 2 → 2, иначе 0.
	static uint8 OwnerOfKey(int32 Key) { return (Key % 10 == 2) ? 2 : 0; }

	// Подставная ячейка сумки (Compute читает ItemCode/Amount и Owner записи +50).
	static FAsdaItemInstance Cell(int32 Code, int32 Amount, uint8 Owner = 0)
	{
		FAsdaItemInstance I;
		I.ItemCode = Code;
		I.Amount = Amount;
		I.Raw.SetNumZeroed(FAsdaItemInstance::kRecordSize);
		I.Raw[50] = Owner;
		return I;
	}
	// Подставная сумка kBag: Filler во всех ячейках, кроме Free (пустые), затем Stacks {ячейка, код, заряды}
	//   (Owner стопки — как поставит сервер для её ключа; Owners — явные Owner ячеек {ячейка → Owner} поверх).
	static TArray<FAsdaItemInstance> Bag(int32 Filler, const TArray<int32>& Free, const TArray<FIntVector>& Stacks,
	                                     const UAsdaItemTableSubsystem* IT = nullptr, const TMap<int32, uint8>& Owners = {})
	{
		TArray<FAsdaItemInstance> B;
		for (int32 s = 0; s < kBag; ++s) { B.Add(Free.Contains(s) ? FAsdaItemInstance() : Cell(Filler, 1)); }
		for (const FIntVector& X : Stacks)
		{
			int32 Key = 0, Ch = 0;
			const uint8 Own = (IT && AsdaChargeStack::Lookup(IT, X.Y, Key, Ch)) ? OwnerOfKey(Key) : 0;
			B[X.X] = Cell(X.Y, X.Z, Own);
		}
		for (const TPair<int32, uint8>& O : Owners) { if (B.IsValidIndex(O.Key) && B[O.Key].Raw.Num() > 50) { B[O.Key].Raw[50] = O.Value; } }
		return B;
	}
	// Позиции состава.
	static TArray<FAsdaPackageEntry> Pack(const TArray<int32>& Codes)
	{
		TArray<FAsdaPackageEntry> P;
		for (const int32 C : Codes) { FAsdaPackageEntry& E = P.AddDefaulted_GetRef(); E.ItemCode = C; E.Amount = 1; }
		return P;
	}
	// Слот позиции i в 15 байтах (−100 — места нет).
	static int32 SlotOf(const TArray<uint8>& S15, int32 i) { return S15.Num() >= 3 * i + 3 ? (int32)AsdaBytes::RdI16(S15.GetData() + 3 * i + 1) : -999; }

	// Итог одного расчёта.
	struct FRes { TArray<uint8> S15; int32 Count = 0, ShortSpend = 0, ShortBag = 0, Merged = 0; };
	static FRes Run(const UAsdaItemTableSubsystem* IT, const TArray<int32>& Codes, const TArray<FAsdaItemInstance>& B, bool bMerge)
	{
		FRes R;
		const TArray<FAsdaItemInstance> Spend;   // расходники не участвуют (состав — только спецпредметы)
		AsdaPackageReserve::Compute(IT, Pack(Codes), &B, kBag, &Spend, bMerge, R.S15, R.Count, R.ShortSpend, R.ShortBag, &R.Merged);
		return R;
	}
	static FString Str(const FRes& R)
	{
		FString S = FString::Printf(TEXT("позиций %d, нехватка сумки %d, слито %d, слоты ["), R.Count, R.ShortBag, R.Merged);
		for (int32 i = 0; i < R.Count; ++i) { S += FString::Printf(TEXT("%s%d"), i ? TEXT(" ") : TEXT(""), SlotOf(R.S15, i)); }
		return S + TEXT("]");
	}

	// Подбор кодов по загруженной таблице (`Lookup`) и диапазону спецпредметов.
	static bool PickCodes(const UAsdaItemTableSubsystem* IT, FCodes& K)
	{
		TMap<int32, TArray<int32>> ByKey;   // ключ → коды по возрастанию
		TMap<int32, int32> Charges;         // код → заряды
		for (int32 c = 1; c <= 637; ++c)
		{
			if (!IT->IsSpecialItemCode(c)) { continue; }
			int32 Key = 0, Ch = 0;
			if (AsdaChargeStack::Lookup(IT, c, Key, Ch)) { ByKey.FindOrAdd(Key).Add(c); Charges.Add(c, Ch); }
			else if (!K.NoKey) { FAsdaSpecialItemlistRow Row; if (IT->GetSpecialItem(c, Row)) { K.NoKey = c; } }
		}
		// Ключ A: в группе есть ключ с другой привязкой (ключ/10 одинаков) — тогда B той же группы; иначе первый ключ.
		TArray<int32> Keys; ByKey.GetKeys(Keys); Keys.Sort();
		for (const int32 Ka : Keys)
		{
			for (const int32 Kb : Keys)
			{
				if (Kb != Ka && Kb / 10 == Ka / 10 && !K.KeyA) { K.KeyA = Ka; K.KeyB = Kb; }
			}
		}
		if (!K.KeyA && Keys.Num() >= 2) { K.KeyA = Keys[0]; K.KeyB = Keys[1]; }
		if (!K.KeyA) { return false; }
		K.A = ByKey[K.KeyA][0]; K.ChA = Charges[K.A];
		K.A2 = ByKey[K.KeyA].Num() > 1 ? ByKey[K.KeyA][1] : 0;
		K.B = ByKey[K.KeyB][0];
		for (const int32 Kc : Keys) { if (Kc / 10 != K.KeyA / 10 && Kc != K.KeyB) { K.KeyC = Kc; K.C = ByKey[Kc][0]; break; } }
		// Код с зарядами 100 (как в брифе), иначе с наибольшими.
		for (const TPair<int32, int32>& P : Charges)
		{
			if (P.Value == 100 && (!K.Big || P.Key < K.Big)) { K.Big = P.Key; }
		}
		if (!K.Big) { for (const TPair<int32, int32>& P : Charges) { if (!K.Big || P.Value > Charges[K.Big]) { K.Big = P.Key; } } }
		int32 Ch = 0;
		AsdaChargeStack::Lookup(IT, K.Big, K.KeyBig, Ch);
		K.ChBig = Ch;
		// Код с привязкой 2 (ключ % 10 == 2) — для правила Owner (ревизия 02.11 п.1).
		for (const int32 Kx : Keys) { if (Kx % 10 == 2) { K.KeyBound = Kx; K.Bound = ByKey[Kx][0]; break; } }
		return K.A && K.B && K.C && K.Big && K.NoKey && K.ChBig > 50 && K.Bound;
	}

	// Вернуть сумку/расходники, сеть, ключи; снять перехват; итог.
	static void Finish(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (Inv)
		{
			auto Restore = [Inv](const TArray<TArray<uint8>>& Saved, int32 Cont)
			{
				for (int32 I = 0; I < Saved.Num(); ++I)
				{
					if (Saved[I].Num() >= FAsdaItemInstance::kRecordSize) { Inv->ApplyExternalRecord(Saved[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
					else { Inv->ClearCell(Cont, I); }
				}
			};
			Restore(C->SavedBag, 1);
			Restore(C->SavedSpend, 2);
		}
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; } }
		if (C->Self) { C->Self->Restore(); C->Self.Reset(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaChargeStackTest, TEXT("ЗАРЯДЫ-P100"), C->Ok, C->Fail);
	}

	// Настоящая коробка, весь состав которой — складываемые спецпредметы (предпочтительно ≥ 2 позиций одного ключа).
	static int32 PickBox(const UAsdaItemTableSubsystem* IT, TArray<FAsdaPackageEntry>& OutContents)
	{
		int32 Best = 0, BestScore = -1;
		for (const FAsdaItemRecord& Rc : IT->GetAllItems())
		{
			if (!IT->IsPackageItem(Rc.ItemCode)) { continue; }
			TArray<FAsdaPackageEntry> Ct;
			if (!IT->GetPackageContents(Rc.ItemCode, Ct) || Ct.Num() == 0) { continue; }
			bool bAll = true;
			TSet<int32> Keys;
			for (const FAsdaPackageEntry& E : Ct)
			{
				int32 Key = 0, Ch = 0;
				if (!IT->IsSpecialItemCode(E.ItemCode) || !AsdaChargeStack::Lookup(IT, E.ItemCode, Key, Ch)) { bAll = false; break; }
				Keys.Add(Key);
			}
			if (!bAll) { continue; }
			const int32 Score = (Ct.Num() - Keys.Num()) * 10 + Ct.Num();   // повторы ключа важнее, затем число позиций
			if (Score > BestScore) { BestScore = Score; Best = Rc.ItemCode; OutContents = Ct; }
		}
		return Best;
	}

	// Весь прогон.
	static void Start(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Inv || !IT || !Net) { C->Expect(false, TEXT("подсистемы инвентаря/таблиц/сети есть")); Finish(C); return; }

		// ═══ A. Данные ═══
		const int32 NCodes = AsdaChargeStack::NumCodes(IT);
		C->Expect(NCodes == 173, FString::Printf(TEXT("A таблица DT_ChargeGroup: кодов %d (ждём 173 — charge_groups_client.csv)"), NCodes));
		FCodes K;
		const bool bCodes = PickCodes(IT, K);
		C->Expect(bCodes, FString::Printf(TEXT("A коды: A=%d (ключ %d, заряды %d), A2=%d, B=%d (ключ %d), C=%d (ключ %d), Big=%d (ключ %d, заряды %d), ключ0=%d"),
			K.A, K.KeyA, K.ChA, K.A2, K.B, K.KeyB, K.C, K.KeyC, K.Big, K.KeyBig, K.ChBig, K.NoKey));
		if (!bCodes) { Finish(C); return; }
		C->Expect(K.KeyB / 10 == K.KeyA / 10, FString::Printf(TEXT("A ключ B — та же группа %d, другая привязка (%d ≠ %d)"), K.KeyA / 10, K.KeyB, K.KeyA));
		const int32 F = K.NoKey;   // заполнитель сумки — нескладываемый спецпредмет

		// ═══ B. Модель: Compute с подставной сумкой ═══
		{
			// B1 полная сумка 60 + стопка ключа A (ячейка 7) → коробка с A: ячейка не нужна, слот = стопка.
			const FRes R = Run(IT, { K.A }, Bag(F, {}, { FIntVector(7, K.A, 30) }, IT), true);
			C->Expect(R.ShortBag == 0 && R.Merged == 1 && SlotOf(R.S15, 0) == 7, TEXT("B1 полная сумка + стопка того же ключа: ") + Str(R));
		}
		if (K.A2)
		{
			// B1б стопка ДРУГОГО кода того же ключа (группа и привязка совпадают) — тоже сливается.
			const FRes R = Run(IT, { K.A }, Bag(F, {}, { FIntVector(12, K.A2, 30) }, IT), true);
			C->Expect(R.ShortBag == 0 && SlotOf(R.S15, 0) == 12, FString::Printf(TEXT("B1б стопка кода %d того же ключа: "), K.A2) + Str(R));
		}
		{
			// B2 полная сумка без стопки → нехватка как сток.
			const FRes R = Run(IT, { K.A }, Bag(F, {}, {}, IT), true);
			C->Expect(R.ShortBag == 1 && R.Merged == 0 && SlotOf(R.S15, 0) == -100, TEXT("B2 полная сумка без стопки: ") + Str(R));
		}
		{
			// B3 стопка другого ключа (та же группа, другая привязка) → нехватка.
			const FRes R = Run(IT, { K.A }, Bag(F, {}, { FIntVector(7, K.B, 30) }, IT), true);
			C->Expect(R.ShortBag == 1 && R.Merged == 0, FString::Printf(TEXT("B3 стопка ключа %d (другая привязка): "), K.KeyB) + Str(R));
		}
		{
			// B4 стопка 30000 − заряды + 50 (у кода с зарядами 100: 29 950) + добавка → потолок, нужна ячейка; полная сумка → нехватка.
			const int32 Near = AsdaChargeStack::kCap - K.ChBig + 50;
			const FRes R = Run(IT, { K.Big }, Bag(F, {}, { FIntVector(3, K.Big, Near) }, IT), true);
			C->Expect(R.ShortBag == 1 && R.Merged == 0, FString::Printf(TEXT("B4 стопка %d + %d > 30000, полная сумка: "), Near, K.ChBig) + Str(R));
			// … с одной свободной (ячейка 40) — идёт в свободную, не в стопку.
			const FRes R2 = Run(IT, { K.Big }, Bag(F, { 40 }, { FIntVector(3, K.Big, Near) }, IT), true);
			C->Expect(R2.ShortBag == 0 && R2.Merged == 0 && SlotOf(R2.S15, 0) == 40, TEXT("B4б то же, одна свободная ячейка 40: ") + Str(R2));
			// … ровно до потолка (Amount + n = 30000) — сливается.
			const int32 Exact = AsdaChargeStack::kCap - K.ChBig;
			const FRes R3 = Run(IT, { K.Big }, Bag(F, {}, { FIntVector(3, K.Big, Exact) }, IT), true);
			C->Expect(R3.ShortBag == 0 && R3.Merged == 1 && SlotOf(R3.S15, 0) == 3, FString::Printf(TEXT("B4в стопка %d + %d = 30000 ровно: "), Exact, K.ChBig) + Str(R3));
			// … переполненная первая стопка (ячейка 3) и вторая того же ключа (ячейка 9) → вторая.
			const FRes R4 = Run(IT, { K.Big }, Bag(F, {}, { FIntVector(3, K.Big, Near), FIntVector(9, K.Big, 10) }, IT), true);
			C->Expect(R4.ShortBag == 0 && SlotOf(R4.S15, 0) == 9, TEXT("B4г первая стопка у потолка, вторая того же ключа: ") + Str(R4));
		}
		{
			// B5 две позиции одного ключа без стопки, одна свободная ячейка (25) → хватает: первая занимает 25, вторая сливается в неё.
			const FRes R = Run(IT, { K.A, K.A }, Bag(F, { 25 }, {}, IT), true);
			C->Expect(R.ShortBag == 0 && R.Merged == 1 && SlotOf(R.S15, 0) == 25 && SlotOf(R.S15, 1) == 25,
				TEXT("B5 две позиции одного ключа, одна свободная: ") + Str(R));
			// Эталон (ключ поведения 0) в той же сумке: вторая позиция без ячейки.
			const FRes R0 = Run(IT, { K.A, K.A }, Bag(F, { 25 }, {}, IT), false);
			C->Expect(R0.ShortBag == 1 && SlotOf(R0.S15, 0) == 25 && SlotOf(R0.S15, 1) == -100, TEXT("B5 эталон (правило выключено): ") + Str(R0));
		}
		{
			// B6 несколько ключей: стопка A (ячейка 2), без стопки B и C, одна свободная (50): A → 2, B → 50, C → нехватка.
			const FRes R = Run(IT, { K.A, K.B, K.C, K.A }, Bag(F, { 50 }, { FIntVector(2, K.A, 5) }, IT), true);
			C->Expect(R.ShortBag == 1 && R.Merged == 2 && SlotOf(R.S15, 0) == 2 && SlotOf(R.S15, 1) == 50 && SlotOf(R.S15, 2) == -100 && SlotOf(R.S15, 3) == 2,
				TEXT("B6 ключи A(стопка)/B/C/A при одной свободной: ") + Str(R));
			// Две свободные (50, 51): B → 50, C → 51, нехватки нет; повтор B после C сливается в 50.
			const FRes R2 = Run(IT, { K.B, K.C, K.B }, Bag(F, { 50, 51 }, {}, IT), true);
			C->Expect(R2.ShortBag == 0 && SlotOf(R2.S15, 0) == 50 && SlotOf(R2.S15, 1) == 51 && SlotOf(R2.S15, 2) == 50,
				TEXT("B6б ключи B/C/B при двух свободных: ") + Str(R2));
		}
		{
			// B7 ключ 0 (нескладываемый спецпредмет) с «стопкой» того же кода и полной сумкой → как сток: нехватка.
			const FRes R = Run(IT, { K.NoKey }, Bag(F, {}, { FIntVector(4, K.NoKey, 30) }, IT), true);
			C->Expect(R.ShortBag == 1 && R.Merged == 0, FString::Printf(TEXT("B7 ключ 0 (код %d) — как сток: "), K.NoKey) + Str(R));
		}
		{
			// B8 правило выключено → полная сумка + стопка = нехватка (эталон).
			const FRes R = Run(IT, { K.A }, Bag(F, {}, { FIntVector(7, K.A, 30) }, IT), false);
			C->Expect(R.ShortBag == 1 && R.Merged == 0 && SlotOf(R.S15, 0) == -100, TEXT("B8 правило выключено, полная сумка + стопка: ") + Str(R));
		}
		{
			// B9 правило Owner (сервер P100 сливает только в ячейку с тем же Owner, что получит входящая запись коробки):
			//   код с привязкой 2 — входящая Owner 2. Стопка того же ключа с Owner 0 (полная сумка) → нехватка; с Owner 2 → сливается.
			const FRes R0 = Run(IT, { K.Bound }, Bag(F, {}, { FIntVector(7, K.Bound, 30) }, IT, { { 7, (uint8)0 } }), true);
			C->Expect(R0.ShortBag == 1 && R0.Merged == 0, FString::Printf(TEXT("B9 код %d (ключ %d, привязка 2), стопка Owner 0: "), K.Bound, K.KeyBound) + Str(R0));
			const FRes R2 = Run(IT, { K.Bound }, Bag(F, {}, { FIntVector(7, K.Bound, 30) }, IT, { { 7, (uint8)2 } }), true);
			C->Expect(R2.ShortBag == 0 && R2.Merged == 1 && SlotOf(R2.S15, 0) == 7, TEXT("B9б та же стопка с Owner 2: ") + Str(R2));
			// Код без привязки 2 (Owner входящей 0): стопка с Owner 2 → нехватка.
			const FRes R3 = Run(IT, { K.A }, Bag(F, {}, { FIntVector(7, K.A, 30) }, IT, { { 7, (uint8)(OwnerOfKey(K.KeyA) == 2 ? 0 : 2) } }), true);
			C->Expect(R3.ShortBag == 1 && R3.Merged == 0, FString::Printf(TEXT("B9в код %d, стопка с чужим Owner: "), K.A) + Str(R3));
		}

		// ═══ C. Путь игрока на настоящей коробке ═══
		TArray<FAsdaPackageEntry> Ct;
		const int32 Box = PickBox(IT, Ct);
		C->Expect(Box > 0, FString::Printf(TEXT("C коробка из packagetable со складываемым составом: %d (позиций %d)"), Box, Ct.Num()));
		if (Box <= 0) { Finish(C); return; }

		// Снимок и очистка сумки/расходников; ёмкость 60; «свой персонаж»; перехват.
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.ShellX2"), 1);
		AsdaCVarSave::SetForTest(kKey, 1);
		auto Snap = [Inv](int32 Cont, TArray<TArray<uint8>>& Out)
		{
			const int32 N = (Cont == 1) ? UAsdaInventorySubsystem::kShellSlots : FMath::Max(0, Inv->GetContainerSize(Cont));
			for (int32 I = 0; I < N; ++I) { const FAsdaItemInstance* X = Inv->GetCell(Cont, I); Out.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		};
		Snap(1, C->SavedBag); Snap(2, C->SavedSpend);
		for (int32 I = 0; I < C->SavedBag.Num(); ++I)   { Inv->ClearCell(1, I); }
		for (int32 I = 0; I < C->SavedSpend.Num(); ++I) { Inv->ClearCell(2, I); }
		C->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelfUserNum = 321; Net->bSelfUserNumKnown = true; Net->SelectedCharId = 0x00ABCDEF;
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { FSent S; S.Op = (int32)Op; if (P && Len > 0) { S.Body.Append(P, Len); } X->Sent.Add(MoveTemp(S)); }
		}, 6096, kCmd);
		C->Expect(C->CapGen != 0 && Inv->GetContainerSize(1) == kBag, FString::Printf(TEXT("C перехват 6096 взведён, ёмкость сумки %d"), Inv->GetContainerSize(1)));
		if (!C->CapGen) { Finish(C); return; }

		// Стопки: по одной на каждый ключ состава (ячейки 0..), остальное — заполнитель; коробка — в расходниках, ячейка 0.
		TMap<int32, int32> StackOfKey;   // ключ → ячейка стопки
		TMap<int32, int64> AddOfKey;     // ключ → ожидаемая добавка
		for (const FAsdaPackageEntry& E : Ct)
		{
			int32 Key = 0, Ch = 0;
			AsdaChargeStack::Lookup(IT, E.ItemCode, Key, Ch);
			if (!StackOfKey.Contains(Key))
			{
				// стопка с тем Owner, что сервер поставит записям этого ключа (иначе P100 не сольёт)
				const int32 S = StackOfKey.Num(); StackOfKey.Add(Key, S);
				TArray<uint8> Rec = FAsdaItemRecordWriter::Cell(E.ItemCode, 1, S, 10);
				if (Rec.Num() > 50) { Rec[50] = OwnerOfKey(Key); }
				Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
			AddOfKey.FindOrAdd(Key) += (int64)Ch;   // добавка позиции = заряды кода (сервер количество не учитывает)
		}
		for (int32 S = StackOfKey.Num(); S < kBag; ++S) { Inv->DebugPutItem(1, S, F, 1); }
		Inv->DebugPutItem(2, 0, Box, 1);
		int32 Filled = 0;
		for (int32 S = 0; S < kBag; ++S) { if (Inv->GetCellItemCode(1, S) > 0) { ++Filled; } }
		C->Expect(Filled == kBag, FString::Printf(TEXT("C сумка заполнена: %d/%d (стопок %d)"), Filled, kBag, StackOfKey.Num()));

		// C1 ключ 1: ReservePackageSlots без нехватки, 6096 уходит, слоты тела = стопки ключей.
		{
			TArray<uint8> S15; int32 Cnt = 0, ShS = 0, ShB = 0;
			const bool bRes = Inv->ReservePackageSlots(Box, S15, Cnt, ShS, ShB);
			bool bSlots = Cnt == Ct.Num();
			for (int32 i = 0; i < Ct.Num() && bSlots; ++i)
			{
				int32 Key = 0, Ch = 0;
				AsdaChargeStack::Lookup(IT, Ct[i].ItemCode, Key, Ch);
				bSlots = S15[3 * i] == 1 && SlotOf(S15, i) == StackOfKey[Key];
			}
			C->Expect(bRes && ShB == 0 && ShS == 0 && bSlots,
				FString::Printf(TEXT("C1 коробка %d при полной сумке со стопками: позиций %d, нехватка сумки %d / расходников %d, слоты = стопки %d"),
					Box, Cnt, ShB, ShS, bSlots ? 1 : 0));
			const int32 Mark = C->Sent.Num();
			const bool bSend = Inv->SendOpenPackage6096(Box, 2, 0);
			const int32 N = C->Sent.Num() - Mark;
			const TArray<uint8> B = N > 0 ? C->Sent.Last().Body : TArray<uint8>();
			bool bBody = B.Num() >= 10 + 3 * Ct.Num() && B[6] == (uint8)Ct.Num();   // @6 — число позиций, @10 — тройки слотов
			for (int32 i = 0; i < Ct.Num() && bBody; ++i)
			{
				bBody = B[10 + 3 * i] == S15[3 * i] && AsdaBytes::RdI16(B.GetData() + 11 + 3 * i) == SlotOf(S15, i);
			}
			C->Expect(bSend && N == 1 && bBody, FString::Printf(TEXT("C1 6096 ушёл ×%d (возврат %d), тело %d б, N=%d, слоты тела = подбор %d"),
				N, bSend ? 1 : 0, B.Num(), B.Num() > 6 ? (int32)B[6] : -1, bBody ? 1 : 0));
		}
		// C2 ключ 0: нехватка как эталон, 6096 не уходит.
		{
			AsdaCVarSave::SetForTest(kKey, 0);
			TArray<uint8> S15; int32 Cnt = 0, ShS = 0, ShB = 0;
			Inv->ReservePackageSlots(Box, S15, Cnt, ShS, ShB);
			const int32 Mark = C->Sent.Num();
			const bool bSend = Inv->SendOpenPackage6096(Box, 2, 0);
			C->Expect(ShB == Ct.Num() && !bSend && C->Sent.Num() == Mark,
				FString::Printf(TEXT("C2 ключ %s = 0: нехватка сумки %d (ждём %d), 6096 ×%d, возврат %d"), kKey, ShB, Ct.Num(), C->Sent.Num() - Mark, bSend ? 1 : 0));
			AsdaCVarSave::SetForTest(kKey, 1);
		}
		// C3 подставной 6097 «как сервер P100»: записи в ячейки стопок с суммой (две позиции одного ключа — обе с ИТОГОВОЙ суммой).
		{
			TArray<uint8> Body; Body.SetNumZeroed(300);
			AsdaBytes::WrI16(Body.GetData() + 0, 321);
			Body[6] = (uint8)Ct.Num(); Body[7] = 1; Body[8] = 2; AsdaBytes::WrI16(Body.GetData() + 9, 0);
			TMap<int32, int64> Running;   // ключ → сумма на момент записи
			for (int32 i = 0; i < Ct.Num() && i < 5; ++i)
			{
				int32 Key = 0, Ch = 0;
				AsdaChargeStack::Lookup(IT, Ct[i].ItemCode, Key, Ch);
				int64& Sum = Running.FindOrAdd(Key);
				if (Sum == 0) { Sum = 10; }
				Sum += (int64)Ch;   // добавка позиции = заряды кода
				const int32 S = StackOfKey[Key];
				// Код ячейки не меняется (P100: прибавка к лежащей записи) — сервер шлёт запись стопки.
				TArray<uint8> Rec = FAsdaItemRecordWriter::Cell(Inv->GetCellItemCode(1, S), 1, S, (int32)Sum);
				if (Rec.Num() > 50) { Rec[50] = OwnerOfKey(Key); }   // Owner стопки сохраняется
				FMemory::Memcpy(Body.GetData() + 11 + 57 * i, Rec.GetData(), FMath::Min(57, Rec.Num()));
			}
			AsdaBytes::WrI32(Body.GetData() + 296, FMath::Max(0, Net->SelfWeightCur));
			Inv->SimulatePackageResult(Body);
		}
		const TMap<int32, int32> Stacks = StackOfKey;
		const TMap<int32, int64> Adds = AddOfKey;
		FTimerHandle H;
		if (UWorld* W = C->W.Get())
		{
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C, Stacks, Adds, F]()   // контекст держится СИЛЬНО: кроме таймера, его никто не держит
			{
				const FCtxRef& CR = C;
				UAsdaInventorySubsystem* I = CR->Sub<UAsdaInventorySubsystem>();
				bool bAll = I != nullptr;
				FString Txt;
				for (const TPair<int32, int32>& P : Stacks)
				{
					const FAsdaItemInstance* Cl = I ? I->GetCell(1, P.Value) : nullptr;
					const int64 Want = 10 + Adds[P.Key];
					const int32 Have = Cl ? Cl->Amount : -1;
					bAll = bAll && Have == (int32)Want;
					Txt += FString::Printf(TEXT(" ключ %d ячейка %d: %d (ждём %lld);"), P.Key, P.Value, Have, Want);
				}
				int32 Foreign = 0;   // ячейки сумки не со стопкой и не с заполнителем — «лишняя ячейка»
				for (int32 S = 0; I && S < kBag; ++S)
				{
					const int32 Cd = I->GetCellItemCode(1, S);
					bool bStack = false;
					for (const TPair<int32, int32>& P : Stacks) { bStack |= P.Value == S; }
					if (!bStack && Cd != F) { ++Foreign; }
				}
				CR->Expect(bAll && Foreign == 0 && I && I->GetCellItemCode(2, 0) == 0,
					FString::Printf(TEXT("C3 6097 «сервер P100» разложен по +5:%s чужих ячеек %d, коробка снята %d"),
						*Txt, Foreign, (I && I->GetCellItemCode(2, 0) == 0) ? 1 : 0));
				Finish(CR);
			}), 0.6f, false);
		}
		else
		{
			Finish(C);
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestChargeStackReserve(
	TEXT("asda.SelfTest.ChargeStackReserve"),
	TEXT("[ИСПРАВЛЕНИЕ] 01.11.2026 (P100_ChargeStack): подбор ячеек коробки (6096) со стопками зарядов спецпредметов — модель "
	     "(полная сумка + стопка, без стопки, другая привязка, потолок 30000, две позиции одного ключа, несколько ключей, ключ 0, "
	     "ключ поведения 0) и путь игрока на настоящей коробке (перехват 6096, подставной 6097). Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaChargeStackTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Очередь пакетов инвентаря (6097) разбирает Tick только при своём персонаже в мире — офлайн его нет: штатный
		//   фейковый спавн и повторный запуск через 2 с (как `asda.SelfTest.InvX2`).
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));   // повторная команда
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		static const uint16 kGuardOps[] = { 6096 };   // опкод самотеста в окне отказа «онлайн»
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, MakeArrayView(kGuardOps))) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		if (KA.Delay > 0.f)
		{
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { Start(C); }), KA.Delay, false);
		}
		else
		{
			Start(C);
		}
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
