// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «НОТИСЫ УДОЧКИ» (29.10.2026): `asda.SelfTest.RodNotices [quit]`.
//   Два бага владельца:
//     1) при надетой удочке в чате «Без живых камней оружие и доспехи ни на что не годятся.» (2033) — его давала НАЖИВКА
//        (тип 10, слот 10): эталон `inventoryApplySlotItem@0xD60BA0` case 3 зовёт проверку гнёзд `itemUI_ApplyEquipWithColor`
//        только при `flagWord176 == 0` (item_inventory.c:50070), у колчанов/наживки/зарядов бура поле ≠ 0; порт условия не
//        проверял. Исправление — `UAsdaInventorySubsystem::OnSelfEquipSlotChanged`, ключ `asda.Inven.EquipSowelGate`;
//     2) атака удочкой → «#112117»: строк 112117/112118 (отказ 4027/4202 коды 11/12 — оружие подтип 10 вариант 1/2) не было
//        в DT_GameText; добавлены ведомостями `gametext_overrides.json` и `loc/en/GameText.json`.
//   Реальный путь (закон diagnostic-must-use-real-path), без сети: тела 5001/4027 подаются в ШТАТНЫЙ `HandlePacket`
//   (`UAsdaNetSubsystem::DebugFeedPacket`), очередь инвентаря прокручивается `DebugPumpPendingPackets` (офлайн-стенд не «в мире»).
//   Разделы:
//     A. наживка 69667 → слот 10: 2033 нет;  B. удочка 79738 (перенос WoD) → слот 9: 7002/2033 нет; родная 69657 — так же;
//     C. меч 79722 без камней → слот 9: 7002 красным; шлем 20556 → слот 0: 2033 цветом #FFAD00;
//     D. ключ `asda.Inven.EquipSowelGate 0` — наживка снова даёт 2033 (прежнее поведение);
//     E. отказ 4027 код 11 своему → строка 112117 «Удочкой драться нельзя.» красным, без «#112117»; код 12 → 112118.
//   Запускать на карте мира (`--map world` пакета), только офлайн (-AsdaNoNet). Итог: «УДОЧКА-НОТИС ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNotice.h"
#include "World/AsdaWorldKind.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRodNoticesTest, Log, All);

namespace AsdaRodNoticesSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.RodNotices");   // имя команды (итог, отказ)
	static const TCHAR* kTag = TEXT("УДОЧКА-НОТИС");                // префикс строк журнала и итога

	constexpr int32 kBait    = 69667;   // «Альпийский червяк» — тип 10 (слот 10), flagWord176 = 50
	constexpr int32 kRodWod  = 79738;   // удочка из переноса WorldOfDreams (тип 9, подтип 10, вариант 1)
	constexpr int32 kRodOwn  = 69657;   // родная «Старая удочка» (9/10/1)
	constexpr int32 kSword   = 79722;   // «Chess's Sword» — обычное оружие (вариант 0)
	constexpr int32 kHelm    = 20556;   // «Простой головной убор» — тип 0, flagWord176 = 0
	constexpr int32 kBagCell = 29;      // ячейка сумки-источник
	constexpr uint16 kMe     = 7;       // свой номер сессии для раздела E
	const FColor kOrange(0xFF, 0xAD, 0x00, 0xFF);   // −21248 — 2033
	const FColor kRed(0xFF, 0x00, 0x00, 0xFF);      // −65536 — 7002

	// Сохранённая ячейка (контейнер, номер, сырые 57 байт; пусто ⇒ ячейка была пуста).
	struct FSaved { int32 Cont = 0; int32 Cell = 0; TArray<uint8> Raw; };

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры
		TWeakObjectPtr<UWorld> World;       // мир прогона
		bool bQuit = false;                 // выйти после итога
		int32 Ok = 0, Fail = 0;             // счёт проверок
		int32 Step = 0;                     // шаг машины состояний
		double StepAt = 0.0;                // время входа в шаг
		TArray<FSaved> Saved;               // занятые ячейки — вернуть в конце
		int32 PrevKey = 1;                  // значение ключа до прогона
		FAsdaNetSelfScope Self;             // «свой» сети до подмены userNum — возврат в `Finish` (★ R5-fix6-c (13.11.2026))
		FString N2033, N7002, N112117, N112118;   // тексты нотисов (без краевых пробелов)
		int32 C2033 = 0, C7002 = 0, C117 = 0, C118 = 0, CRaw = 0;   // счёт строк журнала до шага
	};
	static TSharedPtr<FRun> GRun;

	// Записать результат проверки.
	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaRodNoticesTest, Log, TEXT("%s ok   %s"), kTag, *What); }
		else { ++R.Fail; UE_LOG(LogAsdaRodNoticesTest, Error, TEXT("%s FAIL %s"), kTag, *What); }
	}

	static UAsdaInventorySubsystem* Inv(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr; }
	static UAsdaNetSubsystem* Net(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
	static UAsdaChatSubsystem* Chat(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr; }

	// Ключ исправления (только дев-сборка — этот файл в Shipping не собирается).
	static IConsoleVariable* Key() { return IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Inven.EquipSowelGate")); }

	// Запомнить ячейку до правки (один раз на ячейку).
	static void Save(FRun& R, int32 Cont, int32 Cell)
	{
		for (const FSaved& S : R.Saved) { if (S.Cont == Cont && S.Cell == Cell) { return; } }
		FSaved S; S.Cont = Cont; S.Cell = Cell;
		if (const FAsdaItemInstance* It = Inv(R) ? Inv(R)->GetCell(Cont, Cell) : nullptr)
		{
			if (!It->IsEmpty()) { S.Raw = It->Raw; }
		}
		R.Saved.Add(S);
	}

	// Счёт строк журнала 2572 с подстрокой.
	static int32 Count(const FRun& R, const FString& Needle) { const UAsdaChatSubsystem* C = Chat(R); return C ? C->DebugCountSystemLines(Needle) : -1; }
	static bool ColorIs(const FRun& R, const FString& Needle, const FColor& Want)
	{
		FColor Got;
		const UAsdaChatSubsystem* C = Chat(R);
		return C && C->DebugFindLastSystemLineColor(Needle, Got) && Got == Want;
	}
	static void Snap(FRun& R)
	{
		R.C2033 = Count(R, R.N2033); R.C7002 = Count(R, R.N7002);
		R.C117 = Count(R, R.N112117); R.C118 = Count(R, R.N112118); R.CRaw = Count(R, TEXT("#11211"));
	}

	// Надеть: предмет в ячейку сумки, ячейку слота экипировки освободить, подать 5001 «переезд» 1:kBagCell → 3:Slot.
	static bool Equip(FRun& R, int32 Code, int32 Slot)
	{
		UAsdaInventorySubsystem* I = Inv(R);
		UAsdaNetSubsystem* N = Net(R);
		if (!I || !N) { return false; }
		Save(R, 1, kBagCell);
		Save(R, 3, Slot);
		I->ClearCell(3, Slot);
		if (!I->DebugPutItem(1, kBagCell, Code, 1)) { return false; }
		Snap(R);
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = 1; B[1] = 2;                       // успех, переезд в пустой слот
		AsdaBytes::WrI16(B.GetData() + 2, kBagCell); B[6] = 1;
		AsdaBytes::WrI16(B.GetData() + 13, Slot); B[17] = 3;
		N->DebugFeedPacket(5001, B);
		I->DebugPumpPendingPackets();             // офлайн-стенд не «в мире» — Tick очередь не разбирает
		const FAsdaItemInstance* It = I->GetCell(3, Slot);
		return It && It->ItemCode == Code;
	}

	// Отказ 4027 своему: @0 код, @1 атакующий (я), @3 цель.
	static void Refuse4027(FRun& R, uint8 Code)
	{
		Snap(R);
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = Code;
		AsdaBytes::WrI16(B.GetData() + 1, kMe);
		AsdaBytes::WrI16(B.GetData() + 3, 0);
		if (UAsdaNetSubsystem* N = Net(R)) { N->DebugFeedPacket(4027, B); }
	}

	// Вернуть ячейки и ключ.
	static void Restore(FRun& R)
	{
		UAsdaInventorySubsystem* I = Inv(R);
		for (int32 K = R.Saved.Num() - 1; K >= 0; --K)
		{
			const FSaved& S = R.Saved[K];
			if (!I) { break; }
			if (S.Raw.Num() >= FAsdaItemInstance::kRecordSize) { I->ApplyExternalRecord(S.Raw, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
			else { I->ClearCell(S.Cont, S.Cell); }
		}
		if (IConsoleVariable* K = Key()) { K->Set(R.PrevKey, ECVF_SetByConsole); }
	}

	static bool Finish(FRun& R)
	{
		Restore(R);
		R.Self.Restore();
		AsdaSelfTestKit::Finish(R.World.Get(), kCmd, R.bQuit, LogAsdaRodNoticesTest, kTag, R.Ok, R.Fail);
		GRun.Reset();
		return false;
	}

	// Шаги: подать → через 0,4 с проверить.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		const double Now = FPlatformTime::Seconds();
		if (Now - R.StepAt < 0.4) { return true; }
		if (!Net(R) || !Inv(R) || !Chat(R) || !R.World.IsValid()) { Expect(R, false, TEXT("подсистемы/мир пропали посреди прогона")); return Finish(R); }
		R.StepAt = Now;
		auto D2033 = [&]() { return Count(R, R.N2033) - R.C2033; };
		auto D7002 = [&]() { return Count(R, R.N7002) - R.C7002; };
		switch (R.Step++)
		{
		case 0:   // A: наживка → слот 10
		{
			const bool bOn = Equip(R, kBait, 10);
			Expect(R, bOn, TEXT("A0: наживка 69667 надета в слот 10"));
			Expect(R, D2033() == 0 && D7002() == 0, FString::Printf(TEXT("A1: наживка (flagWord176 ≠ 0) — предупреждения нет (2033 +%d, 7002 +%d)"), D2033(), D7002()));
			return true;
		}
		case 1:   // B: удочки → слот 9
		{
			const bool bWod = Equip(R, kRodWod, 9);
			Expect(R, bWod && D2033() == 0 && D7002() == 0, FString::Printf(TEXT("B1: удочка WoD 79738 в слот 9 — молча (2033 +%d, 7002 +%d)"), D2033(), D7002()));
			const bool bOwn = Equip(R, kRodOwn, 9);
			Expect(R, bOwn && D2033() == 0 && D7002() == 0, FString::Printf(TEXT("B2: родная удочка 69657 в слот 9 — молча (2033 +%d, 7002 +%d)"), D2033(), D7002()));
			return true;
		}
		case 2:   // C: обычные вещи без камней — предупреждения эталона
		{
			const bool bSw = Equip(R, kSword, 9);
			Expect(R, bSw && D7002() == 1 && ColorIs(R, R.N7002, kRed), FString::Printf(TEXT("C1: меч 79722 без камней — 7002 красным (+%d)"), D7002()));
			const bool bHm = Equip(R, kHelm, 0);
			Expect(R, bHm && D2033() == 1 && ColorIs(R, R.N2033, kOrange), FString::Printf(TEXT("C2: шлем 20556 без камней — 2033 цветом #FFAD00 (+%d)"), D2033()));
			return true;
		}
		case 3:   // D: ключ 0 — прежнее поведение
		{
			if (IConsoleVariable* K = Key()) { K->Set(0, ECVF_SetByConsole); }
			const bool bOn = Equip(R, kBait, 10);
			Expect(R, bOn && D2033() == 1, FString::Printf(TEXT("D1: ключ 0 — наживка снова даёт 2033 (+%d)"), D2033()));
			if (IConsoleVariable* K = Key()) { K->Set(1, ECVF_SetByConsole); }
			// ★ R5-fix6-c (13.11.2026): прежде подменённый userNum 7 оставался соседям пачки (в сводке — «свой изменён: userNum 0→7»).
			if (UAsdaNetSubsystem* N = Net(R)) { R.Self.Capture(N, kCmd); N->SetSelfUserNumForDebug(kMe); }
			Refuse4027(R, 11);
			return true;
		}
		case 4:   // E: 4027 код 11
		{
			const int32 d = Count(R, R.N112117) - R.C117, dRaw = Count(R, TEXT("#11211")) - R.CRaw;
			Expect(R, d == 1 && dRaw == 0, FString::Printf(TEXT("E1: отказ 4027 код 11 — «%s» (+%d), «#112117» нет (+%d)"), *R.N112117, d, dRaw));
			Expect(R, ColorIs(R, R.N112117, kRed), TEXT("E1: цвет строки 112117 красный (−65536, как у эталона)"));
			Refuse4027(R, 12);
			return true;
		}
		case 5:
		{
			const int32 d = Count(R, R.N112118) - R.C118, dRaw = Count(R, TEXT("#11211")) - R.CRaw;
			Expect(R, d == 1 && dRaw == 0, FString::Printf(TEXT("E2: отказ 4027 код 12 — «%s» (+%d), «#112118» нет (+%d)"), *R.N112118, d, dRaw));
			return Finish(R);
		}
		default:
			return Finish(R);
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет GameInstance")); return; }
		if (AsdaWorldKind::IsLoginScene(W)) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("запускать в мире, не на сцене входа")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
		for (const int32 C : { kBait, kRodWod, kRodOwn, kSword, kHelm })
		{
			if (!IT || !IT->GetItem(C)) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, *FString::Printf(TEXT("нет предмета %d в таблице"), C)); return; }
		}
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI; R.World = W; R.bQuit = Args.bQuit; R.StepAt = 0.0;
		R.N2033 = AsdaNotice::Loc(GI, 2033).TrimStartAndEnd();
		R.N7002 = AsdaNotice::Loc(GI, 7002).TrimStartAndEnd();
		R.N112117 = AsdaNotice::Loc(GI, 112117).TrimStartAndEnd();
		R.N112118 = AsdaNotice::Loc(GI, 112118).TrimStartAndEnd();
		const FAsdaItemRecord* Bait = IT->GetItem(kBait);
		const FAsdaItemRecord* Rod = IT->GetItem(kRodWod);
		Expect(R, Bait->ItemType == 10 && Bait->FlagWord176 != 0, FString::Printf(TEXT("0: наживка — тип %d, flagWord176 %d"), Bait->ItemType, Bait->FlagWord176));
		Expect(R, Rod->ItemType == 9 && Rod->ItemSubType == 10 && Rod->ItemVariant == 1,
			FString::Printf(TEXT("0: удочка WoD — тип %d, подтип %d, вариант %d (ждём 9/10/1)"), Rod->ItemType, Rod->ItemSubType, Rod->ItemVariant));
		Expect(R, !R.N112117.IsEmpty() && !R.N112117.Contains(TEXT("#")) && !R.N112118.IsEmpty() && !R.N112118.Contains(TEXT("#")),
			FString::Printf(TEXT("0: строки на месте — 112117 «%s», 112118 «%s»"), *R.N112117, *R.N112118));
		if (IConsoleVariable* K = Key()) { R.PrevKey = K->GetInt(); K->Set(1, ECVF_SetByConsole); }
		Expect(R, Key() != nullptr, TEXT("0: ключ asda.Inven.EquipSowelGate зарегистрирован"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.1f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestRodNotices(
	TEXT("asda.SelfTest.RodNotices"),
	TEXT("Самотест: наживка/удочка при надевании не дают «нет живых камней» (2033/7002), обычные вещи без камней — дают "
	     "цветом эталона; ключ asda.Inven.EquipSowelGate 0 — прежнее поведение; отказ 4027 коды 11/12 — строки 112117/112118 "
	     "без «#id». Запускать в мире, только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaRodNoticesSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
