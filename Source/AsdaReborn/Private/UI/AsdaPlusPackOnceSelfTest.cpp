// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlusPackOnceSelfTest.cpp — самотест [ИСПРАВЛЕНИЯ] 08.11.2026 «зелье уровня не применяется, пока действует прежнее»
//   (пачка 19, баг 9; разбор — Reports/batch19/levelup_potion/REPORT.md; ключ `asda.Item.PlusPackOnce`).
//
// СЦЕНАРИЙ `asda.SelfTest.PlusPackOnce [out=<папка>] [quit]` (офлайн, -AsdaNoNet). Путь игрока — двойной щелчок по ячейке сумки
//   (`UseByDoubleClick`, тот же вход у быстрого слота). Перехват 5450 с отказом отправки, состояние Plus Package — приёмом 6585
//   (`SimulateWingState`, то же место, что заполняет пакет сервера).
//   A  набор действует, зелье 489 в сумке: 5450 нет, плашка 221 с нотисом 12849 на языке сессии;
//   B  набор не действует: 5450 уходит с кодом 489;
//   C  ключ 0 при действующем наборе: 5450 уходит (как оригинал);
//   D  действующий набор не мешает другому премиум-предмету (32 «Сокращение общего веса»): 5450 уходит;
//   E  все девять зелий уровня (подтип 120) при действующем наборе: 5450 нет.
//   Кадр `asda_pluspack_refuse.png` — окно сумки и плашка отказа. Итог — `ЗЕЛЬЕ-УРОВНЯ ИТОГ: ok=N fail=M`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CONSOLE — дев-команда вне Shipping
#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPlusPackTest, Log, All);

// Доступ к закрытой защёлке «ответ на действие ещё не пришёл» сумки (друг класса).
struct FAsdaPlusPackOnceSelfTest
{
	static void ClearPending(UAsdaInventorySubsystem* Inv) { if (Inv) { Inv->bActionPending = false; } }
};

namespace AsdaPlusPackTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.PlusPackOnce");
	static const TCHAR* kPfx = TEXT("ЗЕЛЬЕ-УРОВНЯ");
	static const int32 kCodes[] = { 14, 475, 476, 489, 490, 536, 537, 538, 544 };   // special_itemlist подтип 120

	// Состояние прогона.
	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;
		bool bQuit = false;
		FString OutDir;
		int32 Ok = 0, Fail = 0;
		int32 Cell = -1;                        // ячейка сумки стенда
		uint32 Gen = 0;                         // поколение перехвата
		TSharedRef<TArray<int32>> Sent = MakeShared<TArray<int32>>();   // коды из перехваченных 5450
		TUniquePtr<FAsdaNetSelfScope> Self;     // «свой персонаж» сети

		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		void Check(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaPlusPackTest, Display, TEXT("%s %s %s"), kPfx, b ? TEXT("ok  ") : TEXT("FAIL"), *What);
		}
		// Положить предмет в ячейку стенда и применить его двойным щелчком; вернуть, ушёл ли 5450 с этим кодом.
		bool Use(int32 Code)
		{
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>();
			FAsdaPlusPackOnceSelfTest::ClearPending(Inv);
			Inv->ClearCell(1, Cell);
			Inv->DebugPutItem(1, Cell, Code, 1);
			const int32 N = Sent->Num();
			Inv->UseByDoubleClick(1, Cell);
			return Sent->Num() > N && Sent->Last() == Code;
		}
	};
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPlusPackOnce(
	TEXT("asda.SelfTest.PlusPackOnce"),
	TEXT("Самотест отказа повторного зелья уровня (Plus Package, подтип 120): нотис 12849 и 5450 по ключу asda.Item.PlusPackOnce. [out=<папка>] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPlusPackTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 30.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = GI;
		R->bQuit = KA.bQuit;
		for (const FString& A : KA.Pos) { if (A.StartsWith(TEXT("out="))) { R->OutDir = A.Mid(4); } }
		UAsdaInventorySubsystem* Inv = R->Sub<UAsdaInventorySubsystem>();
		UAsdaMiniStateSubsystem* Mini = R->Sub<UAsdaMiniStateSubsystem>();
		UAsdaChatSubsystem* Chat = R->Sub<UAsdaChatSubsystem>();
		UAsdaNetSubsystem* Net = R->Sub<UAsdaNetSubsystem>();
		const UAsdaItemTableSubsystem* IT = R->Sub<UAsdaItemTableSubsystem>();
		if (!Inv || !Mini || !Chat || !Net || !IT) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		AsdaCVarSave::SetForTest(TEXT("asda.Item.PlusPackOnce"), 1);
		R->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelfUserNum = 321; Net->bSelfUserNumKnown = true;
		TSharedRef<TArray<int32>> Sent = R->Sent;
		R->Gen = AsdaSelfTestKit::ArmCapture(Net, [Sent](uint16, const uint8* D, int32 Len, bool, TCHAR)
		{
			// Тело 5450: @0 userNum (i16), @2 код (i32) — сервер `Char_FunctionItem_Use` читает код с +2.
			if (D && Len >= 6) { Sent->Add(AsdaBytes::RdI32(D + 2)); }
		}, 5450, kCmd);
		R->Check(R->Gen != 0, TEXT("перехват 5450 взведён"));
		Inv->OpenWindow();
		R->Cell = Inv->FindFreeSlot(1);
		R->Check(R->Cell >= 0, FString::Printf(TEXT("свободная ячейка сумки %d"), R->Cell));
		const FString Txt = AsdaNotice::Loc(GI, 12849);
		R->Check(!Txt.IsEmpty(), FString::Printf(TEXT("строка 12849 «%s»"), *Txt));

		// A: набор действует.
		Mini->SimulateWingState(2, 7 * 86400, true);
		const int32 W0 = Chat->DebugCountWarLines(Txt);
		R->Check(!R->Use(489), TEXT("A набор действует: 489 — 5450 не ушёл"));
		R->Check(Chat->DebugCountWarLines(Txt) == W0 + 1, FString::Printf(TEXT("A плашка 221: «%s»"), *Txt));
		// E: все зелья уровня.
		int32 Refused = 0;
		for (const int32 C : kCodes)
		{
			FAsdaSpecialItemlistRow Sp;
			const bool bRow = IT->GetSpecialItem(C, Sp) && Sp.SubType == 120;
			if (bRow && !R->Use(C)) { ++Refused; }
		}
		R->Check(Refused == UE_ARRAY_COUNT(kCodes), FString::Printf(TEXT("E зелий уровня отказано %d из %d"), Refused, (int32)UE_ARRAY_COUNT(kCodes)));
		// D: другой премиум-предмет при действующем наборе.
		R->Check(R->Use(32), TEXT("D набор действует, предмет 32 — 5450 ушёл"));
		// B: набор не действует.
		Mini->SimulateWingState(2, 0, false);
		R->Check(R->Use(489), TEXT("B набор не действует: 489 — 5450 ушёл"));
		// C: ключ 0.
		Mini->SimulateWingState(2, 7 * 86400, true);
		AsdaCVarSave::SetForTest(TEXT("asda.Item.PlusPackOnce"), 0);
		R->Check(R->Use(489), TEXT("C ключ 0: 489 — 5450 ушёл, как оригинал"));
		AsdaCVarSave::SetForTest(TEXT("asda.Item.PlusPackOnce"), 1);
		// Кадр отказа.
		R->Use(489);
		FScreenshotRequest::RequestScreenshot(TEXT("asda_pluspack_refuse"), /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
		// Сильная ссылка: прогон живёт до финального шага (слабая освобождала его сразу после выхода из команды).
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R, W](float)
		{
			const TSharedRef<FRun>& X = R;
			UAsdaInventorySubsystem* I = X->Sub<UAsdaInventorySubsystem>();
			UAsdaNetSubsystem* N = X->Sub<UAsdaNetSubsystem>();
			if (!X->OutDir.IsEmpty())
			{
				const FString Src = FPaths::ScreenShotDir() / TEXT("asda_pluspack_refuse.png");
				IFileManager::Get().MakeDirectory(*X->OutDir, true);
				X->Check(IFileManager::Get().Copy(*(X->OutDir / TEXT("asda_pluspack_refuse.png")), *Src) == COPY_OK, TEXT("кадр asda_pluspack_refuse"));
			}
			if (I) { I->ClearCell(1, X->Cell); FAsdaPlusPackOnceSelfTest::ClearPending(I); }
			if (UAsdaMiniStateSubsystem* M = X->Sub<UAsdaMiniStateSubsystem>()) { M->SimulateWingState(-1, 0, false); }
			if (N) { AsdaSelfTestKit::DisarmCapture(N, X->Gen); }
			X->Self.Reset();
			AsdaSelfTestKit::Finish(W, kCmd, X->bQuit, LogAsdaPlusPackTest, kPfx, X->Ok, X->Fail);
			return false;
		}), 2.0f);
	}));
#endif
