// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4, доля a, запись 10 (15.11.2026): самотест и счётчики записи файла журнала боя фоновым потоком. [ПОРТ]
//
// ЧТО ДОКАЗЫВАЕТ (`World/AsdaCombatTrace.h`, ключ `asda.Combat.TraceFileAsync`):
//   А. ключ 1 — строки, отданные фоновому потоку, после `FlushFile` лежат в файле все, по одной, в порядке выдачи;
//   Б. ключ 0 — прежняя запись на месте даёт то же;
//   В. переключение ключа посреди потока строк порядок не ломает (запись на месте сначала дописывает очередь);
//   Г. строки с другого потока (так пишет сетевой разбор) не теряются и идут в своём порядке;
//   Д. текст строки в файле один и тот же при ключе 0 и 1 (всё после колонок времени и кадра совпадает дословно).
// Счётчики: `asda.Combat.TraceFileStats` — время обоих приёмников на игровом потоке (области `CombatTrace.Log`,
//   `CombatTrace.File`, `CombatTrace.FileOpen`) и работа фонового потока (строк, наибольшая очередь, самая долгая запись).
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "AsdaPerf.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaCombatTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCombatTraceFileTest, Log, All);

#if ASDA_COMBAT_TRACE

namespace AsdaCombatTraceFileTest
{
	static const TCHAR* kCmd = TEXT("asda.Combat.SelfTestTraceFile");     // имя команды самотеста
	static const TCHAR* kStats = TEXT("asda.Combat.TraceFileStats");      // имя команды счётчиков
	static const TCHAR* kAsync = TEXT("asda.Combat.TraceFileAsync");      // ключ фоновой записи
	static const TCHAR* kPrefix = TEXT("ЖУРНАЛ-БОЯ-ФАЙЛ");                // префикс строк самотеста

	// Счёт проверок.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;   // пройдено, провалено
	};

	// Одна проверка.
	static void Chk(FCtx& C, bool bOk, const TCHAR* Name, const FString& Detail)
	{
		bOk ? ++C.Ok : ++C.Fail;
		UE_LOG(LogAsdaCombatTraceFileTest, Log, TEXT("%s %s %s — %s"), kPrefix, bOk ? TEXT("ok  ") : TEXT("FAIL"), Name, *Detail);
	}

	// Текст строки самотеста: метка прогона, серия, номер.
	static FString LineText(const FString& Tag, const TCHAR* Series, int32 I)
	{
		return FString::Printf(TEXT("★ самотест файла %s серия %s строка %04d"), *Tag, Series, I);
	}

	// Прочитать файл журнала (он открыт на запись) и вернуть строки.
	static bool ReadLines(TArray<FString>& Out)
	{
		FString All;
		if (!FFileHelper::LoadFileToString(All, *AsdaCombatTrace::FilePath(), FFileHelper::EHashOptions::None, FILEREAD_AllowWrite)) { return false; }
		All.ParseIntoArray(Out, TEXT("\r\n"), /*CullEmpty=*/true);
		return true;
	}

	// Строки серии в файле: сколько найдено, идут ли подряд по номерам 0…N-1 и по одной. `OutFirst` — номер первой строки файла.
	static bool SeriesInOrder(const TArray<FString>& Lines, const FString& Tag, const TCHAR* Series, int32 N, int32& OutFound, int32& OutFirst)
	{
		const FString Key = FString::Printf(TEXT("★ самотест файла %s серия %s строка "), *Tag, Series);
		OutFound = 0; OutFirst = -1;
		bool bOrder = true;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			const int32 At = Lines[i].Find(Key, ESearchCase::CaseSensitive);
			if (At < 0) { continue; }
			if (OutFirst < 0) { OutFirst = i; }
			const int32 No = FCString::Atoi(*Lines[i].Mid(At + Key.Len()));
			if (No != OutFound) { bOrder = false; }
			if (!Lines[i].StartsWith(TEXT("[БОЙ] "), ESearchCase::CaseSensitive) || !Lines[i].EndsWith(LineText(Tag, Series, No), ESearchCase::CaseSensitive)) { bOrder = false; }
			++OutFound;
		}
		return bOrder && OutFound == N;
	}

	// Команда самотеста (весь прогон — в одном кадре: файл дочитывается через `FlushFile`).
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		FCtx C;
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.Trace"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.TraceFile"), 1);
		const FString Tag = FString::Printf(TEXT("%08x"), (uint32)FPlatformTime::Cycles64());   // метка прогона: повтор в том же процессе не путается
		const int32 N = 200;

		int64 Lines0 = 0; int32 MaxQ = 0; double MaxW = 0.0;
		AsdaCombatTrace::FileWriterStats(Lines0, MaxQ, MaxW);

		// А. фоновый поток.
		AsdaCVarSave::SetForTest(kAsync, 1);
		for (int32 i = 0; i < N; ++i) { AsdaCombatTrace::Emit(EAsdaCT::Note, LineText(Tag, TEXT("А"), i)); }
		AsdaCombatTrace::FlushFile();
		int64 Lines1 = 0;
		AsdaCombatTrace::FileWriterStats(Lines1, MaxQ, MaxW);
		TArray<FString> L;
		int32 Found = 0, FirstA = -1, FirstB = -1, FirstV = -1;
		const bool bReadA = ReadLines(L);
		Chk(C, bReadA, TEXT("А0 файл читается"), AsdaCombatTrace::FilePath());
		const bool bSerA = SeriesInOrder(L, Tag, TEXT("А"), N, Found, FirstA);
		Chk(C, bSerA, TEXT("А1 ключ 1: все строки в файле, по порядку"), FString::Printf(TEXT("найдено %d из %d"), Found, N));
		Chk(C, Lines1 - Lines0 == N, TEXT("А2 строки записал фоновый поток"), FString::Printf(TEXT("фоновым потоком %lld из %d"), Lines1 - Lines0, N));

		// Б. прежняя запись на месте.
		AsdaCVarSave::SetForTest(kAsync, 0);
		for (int32 i = 0; i < N; ++i) { AsdaCombatTrace::Emit(EAsdaCT::Note, LineText(Tag, TEXT("Б"), i)); }
		int64 Lines2 = 0;
		AsdaCombatTrace::FileWriterStats(Lines2, MaxQ, MaxW);
		L.Reset(); ReadLines(L);   // без FlushFile: запись на месте уже в файле
		const bool bSerB = SeriesInOrder(L, Tag, TEXT("Б"), N, Found, FirstB);
		Chk(C, bSerB, TEXT("Б1 ключ 0: все строки в файле, по порядку"), FString::Printf(TEXT("найдено %d из %d"), Found, N));
		Chk(C, Lines2 == Lines1, TEXT("Б2 при ключе 0 фоновый поток не пишет"), FString::Printf(TEXT("фоновым потоком %lld"), Lines2 - Lines1));
		Chk(C, FirstA >= 0 && FirstB > FirstA + N - 1, TEXT("Б3 серия Б после серии А"), FString::Printf(TEXT("строки файла: А с %d, Б с %d"), FirstA, FirstB));

		// В. переключение ключа каждые 7 строк.
		for (int32 i = 0; i < N; ++i)
		{
			AsdaCVarSave::SetForTest(kAsync, (i / 7) % 2);
			AsdaCombatTrace::Emit(EAsdaCT::Note, LineText(Tag, TEXT("В"), i));
		}
		AsdaCombatTrace::FlushFile();
		L.Reset(); ReadLines(L);
		const bool bSerV = SeriesInOrder(L, Tag, TEXT("В"), N, Found, FirstV);
		Chk(C, bSerV, TEXT("В1 переключение ключа: порядок цел"), FString::Printf(TEXT("найдено %d из %d"), Found, N));

		// Г. второй поток пишет одновременно с игровым.
		AsdaCVarSave::SetForTest(kAsync, 1);
		TFuture<void> Other = Async(EAsyncExecution::Thread, [Tag, N]()
		{
			for (int32 i = 0; i < N; ++i) { AsdaCombatTrace::Emit(EAsdaCT::Note, LineText(Tag, TEXT("Г2"), i)); }
		});
		for (int32 i = 0; i < N; ++i) { AsdaCombatTrace::Emit(EAsdaCT::Note, LineText(Tag, TEXT("Г1"), i)); }
		Other.Wait();
		AsdaCombatTrace::FlushFile();
		L.Reset(); ReadLines(L);
		int32 F1 = 0, F2 = 0, First1 = -1, First2 = -1;
		const bool bG1 = SeriesInOrder(L, Tag, TEXT("Г1"), N, F1, First1);
		const bool bG2 = SeriesInOrder(L, Tag, TEXT("Г2"), N, F2, First2);
		Chk(C, bG1 && bG2, TEXT("Г1 два потока: обе серии целы и по порядку"), FString::Printf(TEXT("игровой %d из %d, второй %d из %d"), F1, N, F2, N));

		// Д. текст строки при ключе 0 и 1 один и тот же (после колонок времени и кадра).
		{
			// Хвосты строк серии: «[БОЙ] 0123.456 к=000123456 » — 6 + 8 + 1 + 11 + 1 = 27 знаков; дальше номер цикла, этап и текст.
			auto Tails = [&L, &Tag](const TCHAR* Series)
			{
				TArray<FString> Out;
				const FString Key = FString::Printf(TEXT("★ самотест файла %s серия %s строка "), *Tag, Series);
				const FString Mark = FString::Printf(TEXT("серия %s"), Series);
				for (const FString& Ln : L) { if (Ln.Contains(Key, ESearchCase::CaseSensitive)) { Out.Add(Ln.Mid(27).Replace(*Mark, TEXT("серия ?"), ESearchCase::CaseSensitive)); } }
				return Out;
			};
			const TArray<FString> TA = Tails(TEXT("А")), TB = Tails(TEXT("Б"));
			bool bSame = TA.Num() == N && TB.Num() == N;
			for (int32 i = 0; bSame && i < N; ++i) { bSame = TA[i] == TB[i]; }
			Chk(C, bSame, TEXT("Д1 текст строки при ключе 0 и 1 совпадает"), FString::Printf(TEXT("сверено %d пар строк"), N));
		}

		AsdaSelfTestKit::Finish(W, kCmd, A.bQuit, LogAsdaCombatTraceFileTest, kPrefix, C.Ok, C.Fail);
	}

	// Команда счётчиков: строка замера и строка ИТОГ (ok=1).
	static void Stats(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		TArray<FAsdaPerfRow> Rows;
		AsdaPerf::Snapshot(Rows, TEXT("CombatTrace."));
		for (const FAsdaPerfRow& R : Rows)
		{
			UE_LOG(LogAsdaCombatTraceFileTest, Log, TEXT("%s ЗАМЕР %s: вызовов %llu, сумма %.2f мс, среднее %.1f мкс, p95 %.1f мкс, самый долгий %.3f мс | за кадр: самый долгий %.3f мс"),
				kPrefix, *R.Name, R.Calls, R.Sum / 1000.0, R.Mean, R.P95, R.Max / 1000.0, R.FrameMax / 1000.0);
		}
		int64 Lines = 0; int32 MaxQ = 0; double MaxW = 0.0;
		AsdaCombatTrace::FileWriterStats(Lines, MaxQ, MaxW);
		const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kAsync);
		UE_LOG(LogAsdaCombatTraceFileTest, Log, TEXT("%s СЧЁТ ключ %s=%d | фоновым потоком записано строк %lld, наибольшая очередь %d строк, самая долгая запись пачки %.3f мс"),
			kPrefix, kAsync, V ? V->GetInt() : -1, Lines, MaxQ, MaxW * 1000.0);
		AsdaSelfTestKit::Finish(W, kStats, A.bQuit, LogAsdaCombatTraceFileTest, TEXT("ЖУРНАЛ-БОЯ-ФАЙЛ СЧЁТ"), 1, 0);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaCombatTraceFileSelfTest(
	TEXT("asda.Combat.SelfTestTraceFile"),
	TEXT("[ПОРТ] R4: самотест записи файла журнала боя фоновым потоком (ключ asda.Combat.TraceFileAsync): строки не теряются, порядок и текст прежние."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaCombatTraceFileTest::Run(A, W); }));

// Консольная команда счётчиков.
static FAutoConsoleCommandWithWorldAndArgs GAsdaCombatTraceFileStats(
	TEXT("asda.Combat.TraceFileStats"),
	TEXT("[ПОРТ] R4: время приёмников журнала боя на игровом потоке и счётчики фоновой записи файла — в журнал."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaCombatTraceFileTest::Stats(A, W); }));

#endif // ASDA_COMBAT_TRACE

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
