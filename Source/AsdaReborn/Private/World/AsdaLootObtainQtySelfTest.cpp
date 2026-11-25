// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПОЛУЧЕНО N ПРЕДМЕТОВ» (05.11.2026): `asda.SelfTest.LootObtainQty [delay=N] [quit]`. Офлайн (-AsdaNoNet).
//   Жалоба игроков: в системном чате «Ты получаешь предмет: Реагент C.» БЕЗ количества — не понять, одна вещь пришла или стопка.
//   Причина: порт для стопок в 5013 печатал loc 20066 (строка без числа) вместо эталонной 20131.
//   Эталон `uiInventoryOnRecvItemAcquired@0xD10E80` (ui_core.c:47043-47105) и `Net_HandleItemPickupResult@0xF45420`
//   (network.c:42887-42925). Тела 5013 — НАСТОЯЩАЯ раскладка сервера `CharItemPicking` (62 б: @0 u8 результат, @1 i16 подобравший,
//   @3 запись 57 б, @60 i16 вес), подаются штатным `DebugFeedPacket` → `HandlePacket` → мир/сумка. Проверка — по журналу
//   системного чата 2572 (то, что видит игрок), строки ожидаются ЯВНО на языке сессии (RU или EN, `-AsdaLang=en`):
//   Q1  штучный предмет (Src+9 = 0)                         → 20066 «Ты получаешь предмет: X.» / «You have obtained X.»
//   Q2  стопка 1 в пустую ячейку                              → 20131, [sd1] = 1
//   Q3  стопка 5 в пустую ячейку                              → 20131, [sd1] = 5
//   Q4  слияние: в ячейке уже 5 (модель сумки), итог 12       → 20131, [sd1] = 7 (прибавка, а не итог)
//   Q5  два 5013 в ту же ячейку за ОДИН кадр (итоги 15, 20)   → 3 и 5 (модель ещё не применила первый)
//   Q6  колчан (FlagWord176 ≠ 0), итог 250                    → 20130, [sd1] = 250 (итог, как эталон)
//   Q7  чужая ячейка (в ней другой предмет), итог 4           → 20131, [sd1] = 4 ([ИСПРАВЛЕНИЕ]: чужое число не вычитается)
//   Q8  подбор членом отряда (picker = его userIdx, в поле зрения) → 20250 «[имя] …[предмет]», без числа (эталон)
//   Q9  подбор членом отряда вне поля зрения                  → строки нет (эталон)
//   G1–G5 цвет имени (05.11.2026): « <FC=%08x>[имя]</F> » как `UI_FormatLocalizedNotice` с highlightMode 1 — стопка/снаряжение
//       грейда ≥ 1, свой самоцвет (dropColorIndex), самоцвет и стопка у члена отряда (partGrade / itemGrade, имя игрока без цвета)
//   K0  ключ `asda.Loot.ObtainNameGradeColor 0` → имя без цвета и скобок
//   Q1–Q9 ждут имя в том виде, какой задаёт ключ (по умолчанию 1 — с цветом грейда 0 и скобками).
//   R1  регрессия: отказы 2/4/0 → 1011/1030/1012 по одной строке; золото 20551 → 20262 с прибавкой кошелька
//   Ячейки сумки, которые трогал тест, и «свой персонаж» сети возвращаются. Итог: «ПОЛУЧЕНО-N ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaNotice.h"
#include "World/AsdaWorldSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "HighResScreenshot.h"   // кадр `shot` (FScreenshotRequest)
#include "Misc/Paths.h"          // путь кадра в Saved/Screenshots

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLootQtyTest, Log, All);   // канал строк самотеста

namespace AsdaLootQtyTest
{
	static const TCHAR* kCmd      = TEXT("asda.SelfTest.LootObtainQty");   // имя команды
	static const TCHAR* kSelfName = TEXT("ПодборщикQ");                    // имя своего персонажа офлайн-спавна
	static const TCHAR* kMate     = TEXT("Соотрядник");                    // имя члена отряда (фейковый сосед)
	static constexpr int32 kSelfIdx = 7;                                   // свой userIdx
	static constexpr int32 kMateIdx = 3;                                   // userIdx члена отряда в поле зрения
	static constexpr int32 kFarIdx  = 4;                                   // userIdx члена отряда вне поля зрения
	static constexpr int32 kSpend   = 2;                                   // контейнер расходников (стопки)
	static constexpr int32 kShell   = 1;                                   // сумка (снаряжение)

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                                // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                              // мир прогона
		bool bQuit = false;                                    // выйти после итога
		bool bShot = false;                                    // снять кадр чата после строк G1–G5/K0
		TUniquePtr<FAsdaNetSelfScope> Self;                    // «свой персонаж» сети — вернуть в конце
		TArray<TTuple<int32, int32, TArray<uint8>>> Saved;     // (контейнер, ячейка, прежняя запись; пусто — ячейка была пустой)

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaLootQtyTest, Log,   TEXT("ПОЛУЧЕНО-N ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaLootQtyTest, Error, TEXT("ПОЛУЧЕНО-N FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Тело 5013 (62 б) по раскладке сервера: результат, подобравший, запись 57 б, вес.
	static TArray<uint8> Body5013(uint8 Result, int32 Picker, const TArray<uint8>& Rec57, int32 Weight)
	{
		TArray<uint8> B; B.SetNumZeroed(62);
		B[0] = Result;
		AsdaBytes::WrI16(&B[1], (int16)Picker);
		if (Rec57.Num() >= FAsdaItemInstance::kRecordSize) { FMemory::Memcpy(&B[3], Rec57.GetData(), FAsdaItemInstance::kRecordSize); }
		AsdaBytes::WrI16(&B[60], (int16)Weight);
		return B;
	}

	// Ожидаемая строка на языке сессии — ЯВНО, не через ту же функцию подстановки, что у кода.
	static bool IsEn() { return FAsdaLang::Current() == EAsdaLang::En; }

	// ★ 05.11.2026 (цвет имени): drop-палитра ЯВНЫМИ числами из live.dmp @0x2907A28 — не через код порта.
	static uint32 Pal(int32 I)
	{
		static const uint32 k[10] = { 0xFFFFFFFF, 0xFFFFFF46, 0xFFFF96DC, 0xFF96FF32, 0xFFFF7800,
		                              0xFF4141FF, 0xFFBE50FF, 0xFFFA1E46, 0xFF9BEBEB, 0xFFE6E6E6 };
		return (I >= 0 && I < 10) ? k[I] : k[0];
	}
	// Ключ `asda.Loot.ObtainNameGradeColor` прямо сейчас.
	static bool ColourOn()
	{
		const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Loot.ObtainNameGradeColor"));
		return !V || V->GetInt() != 0;
	}
	static void SetColour(int32 V)
	{
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Loot.ObtainNameGradeColor"))) { C->Set(V, ECVF_SetByConsole); }
	}
	// Имя предмета так, как его вставляет `UI_FormatLocalizedNotice` при highlightMode 1 (ui_core.c:2104): « <FC=%08x>[имя]</F> ».
	static FString Frag(const FString& N, uint32 Argb)
	{
		return ColourOn() ? FString::Printf(TEXT(" <FC=%08x>[%s]</F> "), Argb, *N) : N;
	}
	static FString Want20066(const FString& N)            { return IsEn() ? FString::Printf(TEXT("You have obtained %s."), *N) : FString::Printf(TEXT("Ты получаешь предмет: %s."), *N); }
	static FString Want20131(const FString& N, int32 Q)   { return IsEn() ? FString::Printf(TEXT("You have obtained %d %s."), Q, *N) : FString::Printf(TEXT("%s: получено %d шт."), *N, Q); }
	static FString Want20130(const FString& N, int32 Q)   { return IsEn() ? FString::Printf(TEXT("You have obtained %s containing %d arrows."), *N, Q) : FString::Printf(TEXT("Получен предмет: %s со стрелами в количестве %d."), *N, Q); }

	// Запомнить ячейку до правки (один раз).
	static void Save(const FCtxRef& C, UAsdaInventorySubsystem* Inv, int32 Cont, int32 Cell)
	{
		for (const auto& T : C->Saved) { if (T.Get<0>() == Cont && T.Get<1>() == Cell) { return; } }
		const FAsdaItemInstance* I = Inv->GetCell(Cont, Cell);
		C->Saved.Emplace(Cont, Cell, (I && !I->IsEmpty() && I->Raw.Num() >= FAsdaItemInstance::kRecordSize) ? I->Raw : TArray<uint8>());
	}

	// Подать 5013 и сразу применить очередь сумки (то, что сделает её Tick).
	static void Feed(const FCtxRef& C, const TArray<uint8>& Body, bool bPump = true)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5013, Body); }
		if (bPump) { if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>()) { Inv->DebugPumpPendingPackets(); } }
	}

	// Сколько раз строка встречается в журнале 2572.
	static int32 Lines(const FCtxRef& C, const FString& Needle)
	{
		const UAsdaChatSubsystem* Sys = C->Sub<UAsdaChatSubsystem>();
		return Sys ? Sys->DebugCountSystemLines(Needle) : -1;
	}

	// Подать 5013 и проверить, что строка Want появилась ровно один раз.
	static void FeedExpect(const FCtxRef& C, const TCHAR* Tag, const TArray<uint8>& Body, const FString& Want)
	{
		const int32 Before = Lines(C, Want);
		Feed(C, Body);
		const int32 After = Lines(C, Want);
		C->Expect(After == Before + 1, FString::Printf(TEXT("%s «%s»: строк %d→%d (ждём +1)"), Tag, *Want, Before, After));
	}

	static void Finish(const FCtxRef& C)
	{
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>())
		{
			for (const auto& T : C->Saved)
			{
				if (T.Get<2>().Num() >= FAsdaItemInstance::kRecordSize) { Inv->ApplyExternalRecord(T.Get<2>(), UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(T.Get<0>(), T.Get<1>()); }
			}
		}
		if (C->Self) { C->Self->Restore(); C->Self.Reset(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaLootQtyTest, TEXT("ПОЛУЧЕНО-N"), C->Ok, C->Fail,
			IsEn() ? TEXT(" (язык EN)") : TEXT(" (язык RU)"));
	}

	// Свободная ячейка контейнера, кроме уже занятых тестом (−1 — нет).
	static int32 FreeCell(UAsdaInventorySubsystem* Inv, int32 Cont, const TArray<int32>& Skip)
	{
		const int32 N = Inv->GetContainerSize(Cont);
		for (int32 i = N - 1; i >= 0; --i)
		{
			if (Skip.Contains(i)) { continue; }
			const FAsdaItemInstance* I = Inv->GetCell(Cont, i);
			if (!I || I->IsEmpty()) { return i; }
		}
		return -1;
	}

	static void RunTail(const FCtxRef& C);   // хвост прогона (R1 + итог), ниже

	static void Run(const FCtxRef& C)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaWorldSubsystem* WS = C->Sub<UAsdaWorldSubsystem>();
		if (!Net || !Inv || !IT || !WS || !C->Sub<UAsdaChatSubsystem>()) { C->Expect(false, TEXT("подсистемы сети/сумки/таблиц/мира/чата есть")); Finish(C); return; }
		C->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SetSelfUserNumForDebug(kSelfIdx);

		// Предметы прогона из таблицы: расходник со стопкой (тип 11, грейд 0, без колчана), второй такой же, колчан, снаряжение.
		int32 Stack = 0, Stack2 = 0, Quiver = 0, Equip = 0;
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			if (R.ItemCode <= 0 || R.ItemCode == 20551 || R.ItemGrade != 0 || !IT->HasRuName(R.ItemCode)) { continue; }
			if (R.ItemType == 11 && R.FlagWord176 == 0 && R.TradableFlag != 0) { if (!Stack) { Stack = R.ItemCode; } else if (!Stack2) { Stack2 = R.ItemCode; } }
			else if (R.ItemType == 10 && R.FlagWord176 > 0 && !Quiver) { Quiver = R.ItemCode; }
			else if (R.ItemType == 1 && !Equip) { Equip = R.ItemCode; }
		}
		C->Expect(Stack && Stack2 && Quiver && Equip, FString::Printf(TEXT("предметы прогона: стопка %d, вторая %d, колчан %d, снаряжение %d"), Stack, Stack2, Quiver, Equip));
		if (!(Stack && Stack2 && Quiver && Equip)) { Finish(C); return; }
		const FString NStack = IT->GetItemName(Stack), NStack2 = IT->GetItemName(Stack2), NQuiver = IT->GetItemName(Quiver), NEquip = IT->GetItemName(Equip);
		// Имена в строке — как их вставляет оригинал (все четыре предмета грейда 0 → запись палитры [0]).
		const FString FStack = Frag(NStack, Pal(0)), FQuiver = Frag(NQuiver, Pal(0)), FEquip = Frag(NEquip, Pal(0));
		UE_LOG(LogAsdaLootQtyTest, Log, TEXT("ПОЛУЧЕНО-N ключ asda.Loot.ObtainNameGradeColor = %d"), ColourOn() ? 1 : 0);

		TArray<int32> UsedSpend;
		const int32 S1 = FreeCell(Inv, kSpend, UsedSpend); UsedSpend.Add(S1);
		const int32 S2 = FreeCell(Inv, kSpend, UsedSpend); UsedSpend.Add(S2);
		const int32 S3 = FreeCell(Inv, kSpend, UsedSpend); UsedSpend.Add(S3);
		const int32 S4 = FreeCell(Inv, kSpend, UsedSpend); UsedSpend.Add(S4);
		const int32 E1 = FreeCell(Inv, kShell, {});
		C->Expect(S1 >= 0 && S2 >= 0 && S3 >= 0 && S4 >= 0 && E1 >= 0, FString::Printf(TEXT("свободные ячейки: расходники %d/%d/%d/%d, сумка %d"), S1, S2, S3, S4, E1));
		if (S1 < 0 || S2 < 0 || S3 < 0 || S4 < 0 || E1 < 0) { Finish(C); return; }
		Save(C, Inv, kSpend, S1); Save(C, Inv, kSpend, S2); Save(C, Inv, kSpend, S3); Save(C, Inv, kSpend, S4); Save(C, Inv, kShell, E1);

		// Q1 штучный: запись снаряжения с количеством 0, picker −1 (сервер так шлёт свой подбор).
		FeedExpect(C, TEXT("Q1 штучный (кол-во 0; без отряда / «каждый себе»: picker −1)"), Body5013(1, -1, FAsdaItemRecordWriter::Cell(Equip, kShell, E1, 0), 100), Want20066(FEquip));
		// Q2 стопка 1 в пустую ячейку: picker = свой userIdx.
		FeedExpect(C, TEXT("Q2 стопка 1 в пустую (отряд, делёж предметов: picker = свой userIdx)"), Body5013(1, kSelfIdx, FAsdaItemRecordWriter::Cell(Stack, kSpend, S1, 1), 100), Want20131(FStack, 1));
		C->Expect(Inv->GetCellAmount(kSpend, S1) == 1, FString::Printf(TEXT("Q2 модель: в ячейке %d:%d = %d (ждём 1)"), kSpend, S1, Inv->GetCellAmount(kSpend, S1)));
		// Q3 стопка 5 в пустую.
		FeedExpect(C, TEXT("Q3 стопка 5 в пустую"), Body5013(1, -1, FAsdaItemRecordWriter::Cell(Stack, kSpend, S2, 5), 100), Want20131(FStack, 5));
		// Q4 слияние: в S2 уже 5, итог 12 → прибавка 7; строка с итогом 12 появиться НЕ должна.
		{
			const int32 Wrong = Lines(C, Want20131(FStack, 12));
			FeedExpect(C, TEXT("Q4 слияние 5→12"), Body5013(1, -1, FAsdaItemRecordWriter::Cell(Stack, kSpend, S2, 12), 100), Want20131(FStack, 7));
			C->Expect(Lines(C, Want20131(FStack, 12)) == Wrong, TEXT("Q4 итог 12 как «получено» не печатается"));
			C->Expect(Inv->GetCellAmount(kSpend, S2) == 12, FString::Printf(TEXT("Q4 модель: в ячейке = %d (ждём 12)"), Inv->GetCellAmount(kSpend, S2)));
		}
		// Q5 два пакета за один кадр в S2 (12 → 15 → 20), очередь сумки между ними НЕ разбирается.
		{
			const int32 B3 = Lines(C, Want20131(FStack, 3)), B5 = Lines(C, Want20131(FStack, 5)), B8 = Lines(C, Want20131(FStack, 8));
			Feed(C, Body5013(1, -1, FAsdaItemRecordWriter::Cell(Stack, kSpend, S2, 15), 100), /*bPump*/false);
			Feed(C, Body5013(1, -1, FAsdaItemRecordWriter::Cell(Stack, kSpend, S2, 20), 100), /*bPump*/false);
			Inv->DebugPumpPendingPackets();
			C->Expect(Lines(C, Want20131(FStack, 3)) == B3 + 1 && Lines(C, Want20131(FStack, 5)) == B5 + 1 && Lines(C, Want20131(FStack, 8)) == B8,
				TEXT("Q5 два 5013 за кадр 12→15→20: «получено 3» и «получено 5», без «8»"));
			C->Expect(Inv->GetCellAmount(kSpend, S2) == 20, FString::Printf(TEXT("Q5 модель: в ячейке = %d (ждём 20)"), Inv->GetCellAmount(kSpend, S2)));
		}
		// Q6 колчан: итог 250 → 20130 с итогом.
		FeedExpect(C, TEXT("Q6 колчан итог 250"), Body5013(1, -1, FAsdaItemRecordWriter::Cell(Quiver, kSpend, S3, 250), 100), Want20130(FQuiver, 250));
		// Q7 в ячейке S4 лежит ДРУГОЙ предмет (×9), приходит стопка 4 → «получено 4».
		Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Stack2, kSpend, S4, 9), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		FeedExpect(C, TEXT("Q7 чужая ячейка (другой ×9) → 4"), Body5013(1, -1, FAsdaItemRecordWriter::Cell(Stack, kSpend, S4, 4), 100), Want20131(FStack, 4));

		// Q8/Q9 член отряда: фейковый сосед в поле зрения и подбор им; второй — не в поле зрения.
		WS->DebugSpawnFakePeer(kMateIdx, kMate, FVector2D(257.f, 257.f));
		Save(C, Inv, kSpend, 0);
		{
			const FAsdaItemInstance* Was = Inv->GetCell(kSpend, 0);
			const int32 WasCode = Was ? Was->ItemCode : 0, WasAmt = Was ? Was->Amount : 0;
			const FString Want = IsEn() ? FString::Printf(TEXT("%s has obtained %s."), kMate, *FStack) : FString::Printf(TEXT("%s получает предмет: %s."), kMate, *FStack);
			const int32 BeforeName = Lines(C, kMate);
			const int32 BeforeItem = Lines(C, NStack);
			Feed(C, Body5013(1, kMateIdx, FAsdaItemRecordWriter::Cell(Stack, kSpend, 0, 6), 100));
			const bool bNameLine = Lines(C, kMate) == BeforeName + 1 && Lines(C, NStack) == BeforeItem + 1;
			C->Expect(bNameLine && Lines(C, Want) >= 1,
				FString::Printf(TEXT("Q8 подбор членом отряда %d «%s»: строка 20250 «%s»"), kMateIdx, kMate, *Want));
			C->Expect(Lines(C, Want20131(FStack, 6)) == 0, TEXT("Q8 количество у чужого подбора не печатается (у 20250 числа нет)"));
			const FAsdaItemInstance* Now = Inv->GetCell(kSpend, 0);
			C->Expect((Now ? Now->ItemCode : 0) == WasCode && (Now ? Now->Amount : 0) == WasAmt,
				FString::Printf(TEXT("Q8 запись члена отряда НЕ легла в свою сумку: ячейка %d:0 код %d ×%d (было %d ×%d)"),
					kSpend, Now ? Now->ItemCode : 0, Now ? Now->Amount : 0, WasCode, WasAmt));
			const int32 B9 = Lines(C, NStack);
			Feed(C, Body5013(1, kFarIdx, FAsdaItemRecordWriter::Cell(Stack, kSpend, 0, 6), 100));
			C->Expect(Lines(C, NStack) == B9, TEXT("Q9 подбор членом отряда вне поля зрения → строки нет (эталон)"));
		}

		// G1–G5 ЦВЕТ ИМЕНИ по таблице предмета (05.11.2026): грейд ≥ 1, самоцвет (своё — dropColorIndex +0xA9, чужое — partGrade +0x25),
		//   K0 — ключ 0 даёт строку без цвета и скобок. Палитра в ожидании — явные числа (Pal), не код порта.
		{
			int32 GStack = 0, GEquip = 0;
			for (const FAsdaItemRecord& R : IT->GetAllItems())
			{
				if (R.ItemCode <= 0 || R.ItemCode == 20551 || R.ItemGrade < 1 || R.ItemGrade > 9 || !IT->HasRuName(R.ItemCode)) { continue; }
				if (R.ItemType == 11 && R.FlagWord176 == 0 && !GStack) { GStack = R.ItemCode; }
				else if (R.ItemType == 1 && !GEquip) { GEquip = R.ItemCode; }
			}
			// Самоцвет: код sowell_list, у которого partGrade и dropColorIndex дают РАЗНЫЕ цвета палитры (видно, что поле взято верное).
			int32 Sowel = 0, SowelAny = 0;
			FAsdaSowellRow SwRow;
			for (int32 Code = 1; Code < 400000 && !Sowel; ++Code)
			{
				if (!IT->IsSowelCode(Code)) { continue; }
				FAsdaSowellRow Sw;
				if (!IT->GetSowell(Code, Sw) || IT->GetItemName(Code).IsEmpty()) { continue; }
				if (!SowelAny) { SowelAny = Code; SwRow = Sw; }
				if (Pal(Sw.PartGrade) != Pal(Sw.GradeColorIndex)) { Sowel = Code; SwRow = Sw; }
			}
			if (!Sowel) { Sowel = SowelAny; }
			C->Expect(GStack && GEquip && Sowel, FString::Printf(TEXT("G предметы: стопка грейда %d, снаряжение грейда %d, самоцвет %d (partGrade %d, dropColorIndex %d)"),
				GStack, GEquip, Sowel, SwRow.PartGrade, SwRow.GradeColorIndex));
			TArray<int32> Used2 = UsedSpend;
			const int32 S5 = FreeCell(Inv, kSpend, Used2); Used2.Add(S5);
			const int32 S6 = FreeCell(Inv, kSpend, Used2);
			const int32 E2 = FreeCell(Inv, kShell, { E1 });
			const int32 E3 = FreeCell(Inv, kShell, { E1, E2 });
			if (GStack && GEquip && Sowel && S5 >= 0 && S6 >= 0 && E2 >= 0 && E3 >= 0)
			{
				Save(C, Inv, kSpend, S5); Save(C, Inv, kSpend, S6); Save(C, Inv, kShell, E2); Save(C, Inv, kShell, E3);
				const FAsdaItemRecord* RS = IT->GetItem(GStack);
				const FAsdaItemRecord* RE = IT->GetItem(GEquip);
				const FString NG = IT->GetItemName(GStack), NE = IT->GetItemName(GEquip), NS = IT->GetItemName(Sowel);
				// G1 стопка грейда g → 20131, имя цветом Pal(g).
				FeedExpect(C, *FString::Printf(TEXT("G1 стопка грейда %d (цвет %08x)"), RS->ItemGrade, Pal(RS->ItemGrade)),
					Body5013(1, -1, FAsdaItemRecordWriter::Cell(GStack, kSpend, S5, 3), 100), Want20131(Frag(NG, Pal(RS->ItemGrade)), 3));
				// G2 штучное снаряжение грейда g → 20066, Pal(g).
				FeedExpect(C, *FString::Printf(TEXT("G2 снаряжение грейда %d (цвет %08x)"), RE->ItemGrade, Pal(RE->ItemGrade)),
					Body5013(1, -1, FAsdaItemRecordWriter::Cell(GEquip, kShell, E2, 0), 100), Want20066(Frag(NE, Pal(RE->ItemGrade))));
				// G3 свой самоцвет → 20066, цвет по dropColorIndex (+0xA9), ui_core.c:47054-47057.
				FeedExpect(C, *FString::Printf(TEXT("G3 свой самоцвет %d → dropColorIndex %d (цвет %08x)"), Sowel, SwRow.GradeColorIndex, Pal(SwRow.GradeColorIndex)),
					Body5013(1, -1, FAsdaItemRecordWriter::Cell(Sowel, kShell, E3, 0), 100), Want20066(Frag(NS, Pal(SwRow.GradeColorIndex))));
				// G4 самоцвет у члена отряда → 20250, цвет по partGrade (+0x25), network.c:42914-42918; имя игрока без цвета.
				{
					const FString Want = IsEn() ? FString::Printf(TEXT("%s has obtained %s."), kMate, *Frag(NS, Pal(SwRow.PartGrade)))
					                            : FString::Printf(TEXT("%s получает предмет: %s."), kMate, *Frag(NS, Pal(SwRow.PartGrade)));
					FeedExpect(C, *FString::Printf(TEXT("G4 самоцвет у члена отряда → partGrade %d (цвет %08x)"), SwRow.PartGrade, Pal(SwRow.PartGrade)),
						Body5013(1, kMateIdx, FAsdaItemRecordWriter::Cell(Sowel, kShell, E3, 0), 100), Want);
				}
				// G5 предмет грейда g у члена отряда → 20250, Pal(g) на [ss2].
				{
					const FString Want = IsEn() ? FString::Printf(TEXT("%s has obtained %s."), kMate, *Frag(NG, Pal(RS->ItemGrade)))
					                            : FString::Printf(TEXT("%s получает предмет: %s."), kMate, *Frag(NG, Pal(RS->ItemGrade)));
					FeedExpect(C, TEXT("G5 стопка грейда у члена отряда → itemGrade"), Body5013(1, kMateIdx, FAsdaItemRecordWriter::Cell(GStack, kSpend, 0, 2), 100), Want);
				}
				// K0 ключ 0 → прежний вид: имя без тега и скобок.
				if (ColourOn())
				{
					SetColour(0);
					FeedExpect(C, TEXT("K0 ключ asda.Loot.ObtainNameGradeColor 0 → имя без цвета"),
						Body5013(1, -1, FAsdaItemRecordWriter::Cell(GStack, kSpend, S6, 4), 100), Want20131(NG, 4));
					SetColour(1);
				}
			}
			else
			{
				C->Expect(false, FString::Printf(TEXT("G ячейки: расходники %d/%d, сумка %d/%d"), S5, S6, E2, E3));
			}
		}

		// Кадр (`shot`): строки G1–G5/K0 — последние в журнале 2572 и в буфере B; снимок с UI через 1 с, хвост теста — после кадра.
		if (C->bShot && C->W.IsValid())
		{
			const FString Png = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"), TEXT("Screenshots"),   /* без суффикса -saveddirsuffix: папку пакета удаляют */
				FString::Printf(TEXT("asda_lootcol_%s.png"), IsEn() ? TEXT("en") : TEXT("ru"))));
			FTimerHandle H1, H2;
			C->W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([Png]()
			{
				UE_LOG(LogAsdaLootQtyTest, Warning, TEXT("ПОЛУЧЕНО-N КАДР → %s"), *Png);
				FScreenshotRequest::RequestScreenshot(Png, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			}), 1.0f, false);
			C->W->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda([C]() { RunTail(C); }), 2.5f, false);
			return;
		}
		RunTail(C);
	}

	// Хвост прогона: регрессии R1 и итог.
	static void RunTail(const FCtxRef& C)
	{
		// R1 регрессии: отказы и золото.
		{
			struct FRef { uint8 Code; int32 Loc; };
			const FRef Refs[] = { { 2, 1011 }, { 4, 1030 }, { 0, 1012 } };
			for (const FRef& R : Refs)
			{
				const FString T = AsdaNotice::Loc(C->GI(), R.Loc);
				const int32 Before = Lines(C, T);
				Feed(C, Body5013(R.Code, -1, TArray<uint8>(), 0));
				C->Expect(!T.IsEmpty() && Lines(C, T) == Before + 1, FString::Printf(TEXT("R1 отказ %d → loc %d «%s» +1"), R.Code, R.Loc, *T));
			}
			// Золото: первый пакет сеет базу (строки нет), второй даёт прибавку.
			Feed(C, Body5013(1, -1, FAsdaItemRecordWriter::Cell(20551, kShell, 0, 1000), 100));
			const FString Gold = IsEn() ? FString(TEXT("You have obtained 234 gold.")) : FString(TEXT("Получено 234 золота."));
			const UAsdaTextSubsystem* Txt = C->Sub<UAsdaTextSubsystem>();
			const FString GoldLoc = Txt ? Txt->FormatById(20262, {}, { 234 }) : FString();
			const int32 BG = Lines(C, GoldLoc);
			Feed(C, Body5013(1, -1, FAsdaItemRecordWriter::Cell(20551, kShell, 0, 1234), 100));
			C->Expect(!GoldLoc.IsEmpty() && Lines(C, GoldLoc) == BG + 1 && GoldLoc == Gold,
				FString::Printf(TEXT("R1 золото 1000→1234 (дележ золота в отряде: каждому своя запись, picker −1, GameServer.c:84441-84476) → 20262 «%s» +1"), *GoldLoc));
		}
		Finish(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestLootObtainQty(
	TEXT("asda.SelfTest.LootObtainQty"),
	TEXT("05.11.2026: строка «получено N» подбора 5013 — штучный 20066, стопка 20131 (прибавка к ячейке, слияние, два пакета за кадр), "
	     "колчан 20130, член отряда 20250, отказы 1011/1030/1012, золото 20262. Офлайн. `delay=N` `quit` `shot` (кадр чата с цветными строками в Saved/Screenshots). Язык — `-AsdaLang=`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaLootQtyTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Сумка и чат живут при своём персонаже в мире — офлайн его нет: штатный фейковый спавн и повтор через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W, kSelfName))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""), KA.Has(TEXT("shot")) ? TEXT(" shot") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->bShot = KA.Has(TEXT("shot"));
		if (KA.Delay > 0.f)
		{
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { Run(C); }), KA.Delay, false);
		}
		else
		{
			Run(C);
		}
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
