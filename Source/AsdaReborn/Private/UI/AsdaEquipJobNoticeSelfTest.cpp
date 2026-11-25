// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ОТКАЗ НАДЕВАНИЯ ПО ПРОФЕССИИ» (29.10.2026): `asda.SelfTest.EquipJobNotice [quit]`.
//   Баг владельца: «Chess's Sword» 79722 (профессия 42) на персонажа другой профессии → сервер отвечает 5001 код 15
//   (`sub_4CDAA0@0x4CDAA0`: профессия проверяется раньше уровня), а порт выводил свою строку 990468 «Предмет с самоцветом
//   переместить нельзя.». Эталон `chatNoticeSowelItemBlocked@0xD62280`: 20136 «…только для игрока [ss1]», [ss1] = loc
//   (8438 + профессия предмета), цвет −21248. Исправление — `UAsdaInventorySubsystem::ApplyMoveResult` под ключом
//   `asda.Inven.EquipJobNotice` (1 по умолчанию).
//   Реальный путь (закон diagnostic-must-use-real-path), без сети: тело 5001 подаётся в ШТАТНЫЙ `HandlePacket`
//   (`UAsdaNetSubsystem::DebugFeedPacket`) → делегат → очередь инвентаря → его Tick → журнал чата 2572.
//   Разделы:
//     A. код 15, в исходной ячейке сумки 79722 → строка 20136 с профессией 8480, цвет #FFAD00; строки «самоцвет» нет;
//     B. код 14 (уровень) → строка 2030 цветом #FFAD00;
//     C. код 15, исходная ячейка пуста (нет записи) → молча, строки 20136 нет;
//     D. ключ `asda.Inven.EquipJobNotice 0` — прежняя строка 990468.
//   Запускать на карте мира (`--map world` пакета), только офлайн (-AsdaNoNet). Итог: «ПРОФ-НАДЕТЬ ИТОГ: ok=N fail=M».
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

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEquipJobTest, Log, All);

namespace AsdaEquipJobNoticeSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.EquipJobNotice");   // имя команды (итог, отказ)
	static const TCHAR* kTag = TEXT("ПРОФ-НАДЕТЬ");                     // префикс строк журнала и итога

	constexpr int32 kItem    = 79722;   // «Chess's Sword» — профессия 42 (перенос WorldOfDreams, баг владельца)
	constexpr int32 kBagCell = 29;      // ячейка сумки с предметом (содержимое сохраняется и возвращается)
	constexpr int32 kEmpty   = 28;      // ячейка сумки, которую тест держит пустой (раздел C)
	const FColor kOrange(0xFF, 0xAD, 0x00, 0xFF);   // −21248 — цвет эталона для кодов 14 и 15

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
		FString JobNeedle;                  // имя профессии предмета (loc 8438 + 42), без хвостовых пробелов
		FString PrefixNeedle;               // текст 20136 до маркера [ss1]
		FString LevelNeedle;                // текст 2030
		FString SowelNeedle;                // прежняя строка порта 990468
		int32 Jobs0 = 0, Prefix0 = 0, Level0 = 0, Sowel0 = 0;   // счёт строк журнала до шага
	};
	static TSharedPtr<FRun> GRun;

	// Записать результат проверки.
	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaEquipJobTest, Log, TEXT("%s ok   %s"), kTag, *What); }
		else { ++R.Fail; UE_LOG(LogAsdaEquipJobTest, Error, TEXT("%s FAIL %s"), kTag, *What); }
	}

	static UAsdaInventorySubsystem* Inv(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr; }
	static UAsdaNetSubsystem* Net(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
	static UAsdaChatSubsystem* Chat(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr; }

	// Ключ исправления (только дев-сборка — этот файл в Shipping не собирается).
	static IConsoleVariable* Key() { return IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Inven.EquipJobNotice")); }

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

	// Тело 5001 (24 б): код, затем эхо запроса сервера (GameServer.c:82446) — @2 слот источника, @6 его контейнер,
	//   @13 слот назначения, @17 контейнер назначения (3 = надетое).
	static TArray<uint8> Body5001(int32 Code, int32 SrcCont, int32 SrcCell, int32 DstSlot)
	{
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = (uint8)Code;
		AsdaBytes::WrI16(B.GetData() + 2, SrcCell);
		B[6] = (uint8)SrcCont;
		AsdaBytes::WrI16(B.GetData() + 13, DstSlot);
		B[17] = 3;
		return B;
	}

	// Подать 5001 штатным путём и прокрутить очередь инвентаря: офлайн-стенд не «в мире», и Tick подсистемы её не разбирает
	//   (тот же обход, что у `asda.Inven.Pump`, — `DebugPumpPendingPackets` зовёт штатный `DrainPendingPackets`).
	static void Feed(const FRun& R, const TArray<uint8>& Body)
	{
		if (UAsdaNetSubsystem* N = Net(R)) { N->DebugFeedPacket(5001, Body); }
		if (UAsdaInventorySubsystem* I = Inv(R)) { I->DebugPumpPendingPackets(); }
	}

	// Снять счёт строк журнала перед подачей пакета.
	static void Snap(FRun& R)
	{
		const UAsdaChatSubsystem* C = Chat(R);
		R.Jobs0   = C ? C->DebugCountSystemLines(R.JobNeedle) : 0;
		R.Prefix0 = C ? C->DebugCountSystemLines(R.PrefixNeedle) : 0;
		R.Level0  = C ? C->DebugCountSystemLines(R.LevelNeedle) : 0;
		R.Sowel0  = C ? C->DebugCountSystemLines(R.SowelNeedle) : 0;
	}
	static int32 Added(const FRun& R, const FString& Needle, int32 Before)
	{
		const UAsdaChatSubsystem* C = Chat(R);
		return C ? C->DebugCountSystemLines(Needle) - Before : -1;
	}
	static bool ColorIs(const FRun& R, const FString& Needle, const FColor& Want)
	{
		FColor Got;
		const UAsdaChatSubsystem* C = Chat(R);
		return C && C->DebugFindLastSystemLineColor(Needle, Got) && Got == Want;
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
		AsdaSelfTestKit::Finish(R.World.Get(), kCmd, R.bQuit, LogAsdaEquipJobTest, kTag, R.Ok, R.Fail);
		GRun.Reset();
		return false;
	}

	// Шаги: подать пакет → через 0,4 с проверить (Tick инвентаря разбирает очередь).
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		const double Now = FPlatformTime::Seconds();
		if (Now - R.StepAt < 0.4) { return true; }
		UAsdaNetSubsystem* N = Net(R);
		UAsdaInventorySubsystem* I = Inv(R);
		if (!N || !I || !Chat(R) || !R.World.IsValid()) { Expect(R, false, TEXT("подсистемы/мир пропали посреди прогона")); return Finish(R); }
		R.StepAt = Now;
		switch (R.Step++)
		{
		case 0:   // A: код 15, предмет в сумке
		{
			Save(R, 1, kBagCell);
			Save(R, 1, kEmpty);
			I->ClearCell(1, kEmpty);
			const bool bPut = I->DebugPutItem(1, kBagCell, kItem, 1);
			const FAsdaItemInstance* It = I->GetCell(1, kBagCell);
			Expect(R, bPut && It && It->ItemCode == kItem, FString::Printf(TEXT("A0: в сумке (ячейка %d) лежит %d"), kBagCell, kItem));
			Snap(R);
			Feed(R, Body5001(15, 1, kBagCell, 9));
			return true;
		}
		case 1:
		{
			const int32 dJob = Added(R, R.JobNeedle, R.Jobs0), dPre = Added(R, R.PrefixNeedle, R.Prefix0), dSow = Added(R, R.SowelNeedle, R.Sowel0);
			Expect(R, dJob == 1 && dPre == 1, FString::Printf(TEXT("A1: строка 20136 «%s…%s» добавлена (по тексту +%d, по профессии +%d)"),
				*R.PrefixNeedle, *R.JobNeedle, dPre, dJob));
			Expect(R, ColorIs(R, R.PrefixNeedle, kOrange), TEXT("A1: цвет строки #FFAD00 (−21248, как у эталона)"));
			Expect(R, dSow == 0, FString::Printf(TEXT("A1: строки «самоцвет» 990468 нет (+%d)"), dSow));
			Expect(R, I->GetCell(1, kBagCell) && I->GetCell(1, kBagCell)->ItemCode == kItem, TEXT("A1: предмет остался в исходной ячейке"));
			Snap(R);
			Feed(R, Body5001(14, 1, kBagCell, 9));
			return true;
		}
		case 2:   // B: код 14 → 2030 оранжевым
		{
			const int32 dLvl = Added(R, R.LevelNeedle, R.Level0);
			Expect(R, dLvl == 1, FString::Printf(TEXT("B1: строка 2030 «%s» добавлена (+%d)"), *R.LevelNeedle, dLvl));
			Expect(R, ColorIs(R, R.LevelNeedle, kOrange), TEXT("B1: цвет строки 2030 #FFAD00 (−21248)"));
			Snap(R);
			Feed(R, Body5001(15, 1, kEmpty, 9));
			return true;
		}
		case 3:   // C: код 15, исходная ячейка пуста → молча
		{
			const int32 dPre = Added(R, R.PrefixNeedle, R.Prefix0), dSow = Added(R, R.SowelNeedle, R.Sowel0);
			Expect(R, dPre == 0 && dSow == 0, FString::Printf(TEXT("C1: пустая ячейка — молча (20136 +%d, 990468 +%d)"), dPre, dSow));
			if (IConsoleVariable* K = Key()) { K->Set(0, ECVF_SetByConsole); }
			Snap(R);
			Feed(R, Body5001(15, 1, kBagCell, 9));
			return true;
		}
		case 4:   // D: ключ 0 — прежняя строка
		{
			const int32 dPre = Added(R, R.PrefixNeedle, R.Prefix0), dSow = Added(R, R.SowelNeedle, R.Sowel0);
			Expect(R, dSow == 1 && dPre == 0, FString::Printf(TEXT("D1: ключ 0 — прежняя строка 990468 (+%d), 20136 нет (+%d)"), dSow, dPre));
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
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(kItem) : nullptr;
		if (!Rec) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет предмета 79722 в таблице")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI; R.World = W; R.bQuit = Args.bQuit; R.StepAt = 0.0;
		R.JobNeedle = AsdaNotice::Loc(GI, 8438 + Rec->UsageCategory).TrimStartAndEnd();
		FString Full = AsdaNotice::Loc(GI, 20136);
		int32 At = Full.Find(TEXT("[ss1]"));
		R.PrefixNeedle = (At > 0 ? Full.Left(At) : Full).TrimStartAndEnd();
		R.LevelNeedle = AsdaNotice::Loc(GI, 2030).TrimStartAndEnd();
		R.SowelNeedle = AsdaNotice::Loc(GI, 990468).TrimStartAndEnd();
		Expect(R, Rec->UsageCategory == 42, FString::Printf(TEXT("0: профессия 79722 = %d (ждём 42, как у донора)"), Rec->UsageCategory));
		Expect(R, !R.JobNeedle.IsEmpty() && !R.PrefixNeedle.IsEmpty() && !R.LevelNeedle.IsEmpty() && !R.SowelNeedle.IsEmpty(),
			FString::Printf(TEXT("0: тексты на месте — профессия «%s», 20136 «%s», 2030 «%s», 990468 «%s»"),
				*R.JobNeedle, *R.PrefixNeedle, *R.LevelNeedle, *R.SowelNeedle));
		if (IConsoleVariable* K = Key()) { R.PrevKey = K->GetInt(); K->Set(1, ECVF_SetByConsole); }
		Expect(R, Key() != nullptr, TEXT("0: ключ asda.Inven.EquipJobNotice зарегистрирован"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.1f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEquipJobNotice(
	TEXT("asda.SelfTest.EquipJobNotice"),
	TEXT("Самотест: отказ надевания 5001 код 15 (не та профессия) — строка 20136 с профессией предмета цветом #FFAD00, "
	     "код 14 — 2030 тем же цветом, пустая ячейка — молча; ключ asda.Inven.EquipJobNotice 0 — прежняя строка «самоцвет». "
	     "Запускать в мире, только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaEquipJobNoticeSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
