// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4, шаг M0 (доля c; 14.11.2026) [ПОРТ]: СЕРИИ ЗАМЕРА «КЛЮЧ 0/1 В ОДНОМ ПРОЦЕССЕ» — `asda.Perf.Series`.
//
// ЗАЧЕМ. Правка волны R4 живёт под ключом `asda.Perf.<Имя>` (1 — новое, 0 — старый код). Сравнивать два запуска игры нельзя:
//   фон машины и RDP владельца плавают сильнее, чем выгода. Команда в ОДНОЙ игре и одной сцене гонит серии с чередованием
//   ключа (по умолчанию 0,1,1,0,0,1,1,0 — так гасится и медленный уход фона), в каждой серии обнуляет накопители областей
//   (`AsdaPerf.h`), ждёт заданное число кадров и снимает нужные области. Итог — файл: строки по сериям и сводка
//   «ключ 0 против ключа 1» с разбросом между сериями и приговором «измеримо / не измеримо».
//
// КОМАНДА.
//   asda.Perf.Series key=<ключ> [scopes=<имя,имя>] [pairs=4] [frames=240] [warm=30] [order=abba|abab]
//                    [cmd=<команда+аргументы>] [tick=<команда+аргументы>] [reps=1] [file=<путь>] [delay=N] [quit]
//     key     — консольный ключ правки (целый); после серий возвращается исходное значение;
//     scopes  — области замера через запятую (часть имени не годится — точные имена); пусто — все, где были вызовы;
//     pairs   — пар серий (серий 2×pairs);          frames — кадров замера в серии;      warm — кадров прогрева после смены ключа;
//     cmd     — консольная команда один раз в начале каждой серии (после смены ключа, до прогрева); пробел — знаком `+`;
//     tick    — консольная команда КАЖДЫЙ кадр серии (и прогрева) `reps` раз — нагрузчик участка; пробел — знаком `+`;
//     file    — куда писать (без пробелов); нет — Saved/AsdaPerf/series_<ключ>_<время>.tsv.
//   Своих ожиданий вне игры нет: шаги идут по кадрам игры (`FTSTicker`).
// СВОДКА. По каждой области и значению ключа: медиана по сериям от «среднего на вызов», «медианы», «p95», «среднего за кадр»;
//   разброс = (максимум − минимум) «среднего на вызов» между сериями одного значения. Выгода = (ключ0 − ключ1) / ключ0 по
//   «среднему на вызов». «измеримо» — когда |ключ0 − ключ1| больше большего из двух разбросов; иначе «не измеримо».
//   Второй приговор (`приговор_кадр`) — то же по «среднему за кадр» (сумма участка за кадр): по нему судят правки, которые
//   меняют ЧИСЛО вызовов (участок зовётся реже) — у них среднее на вызов может вырасти при меньшей сумме за кадр.
#include "CoreMinimal.h"

#include "AsdaDevConsole.h"
#include "AsdaPerf.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "Misc/App.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPerfSeries, Log, All);

namespace AsdaPerfSeries
{
	static const TCHAR* kCmd = TEXT("asda.Perf.Series");   // имя команды

	// Снимок одной серии.
	struct FSeries
	{
		int32 KeyValue = 0;            // значение ключа в серии
		TArray<FAsdaPerfRow> Rows;     // области серии
	};

	// Состояние прогона.
	struct FCtx
	{
		FString Key;                    // имя ключа правки
		TArray<FString> Scopes;         // точные имена областей (пусто — все)
		int32 Pairs = 4;                // пар серий
		int32 Frames = 240;             // кадров замера
		int32 Warm = 30;                // кадров прогрева
		bool bAbab = false;             // порядок 0,1,0,1 вместо 0,1,1,0
		FString Cmd;                    // команда в начале серии
		FString Tick;                   // команда каждый кадр
		int32 Reps = 1;                 // повторов `Tick` за кадр
		FString File;                   // файл итога
		bool bQuit = false;             // выйти в конце
		float Delay = 0.f;              // задержка старта, с
		TWeakObjectPtr<UWorld> World;   // мир прогона
		int32 Index = -1;               // текущая серия (−1 — ещё не начата)
		int32 Phase = 0;                // 0 — начать серию, 1 — прогрев, 2 — замер
		int32 Left = 0;                 // кадров до конца фазы
		double StartAt = 0.0;           // время старта (с учётом задержки)
		TArray<FSeries> Done;           // снятые серии
	};

	// Значение ключа серии `I`.
	static int32 KeyOf(const FCtx& C, int32 I) { return C.bAbab ? (I & 1) : (((I + 1) >> 1) & 1); }

	// Медиана набора (пустой — 0).
	static double Median(TArray<double> A)
	{
		if (A.Num() == 0) { return 0.0; }
		A.Sort();
		const int32 N = A.Num();
		return (N & 1) ? A[N / 2] : 0.5 * (A[N / 2 - 1] + A[N / 2]);
	}

	// Число игр на машине (процессы UnrealEditor*/AsdaReborn*, включая эту).
	static int32 CountGames()
	{
		int32 N = 0;
		FPlatformProcess::FProcEnumerator It;
		while (It.MoveNext())
		{
			const FString Name = It.GetCurrent().GetName();
			if (Name.StartsWith(TEXT("UnrealEditor")) || Name.StartsWith(TEXT("AsdaReborn"))) { ++N; }
		}
		return N;
	}

	// Выполнить консольную команду (`+` → пробел).
	static void Exec(const FCtx& C, const FString& Cmd)
	{
		if (Cmd.IsEmpty() || !GEngine) { return; }
		GEngine->Exec(C.World.Get(), *Cmd.Replace(TEXT("+"), TEXT(" ")));
	}

	// Строка области из снимка серии (nullptr — области в серии не было).
	static const FAsdaPerfRow* RowOf(const FSeries& S, const FString& Name)
	{
		for (const FAsdaPerfRow& R : S.Rows) { if (R.Name == Name) { return &R; } }
		return nullptr;
	}

	// Итог: файл, журнал, возврат ключа, выход.
	static void Finish(const TSharedRef<FCtx>& C)
	{
		AsdaCVarSave::RestoreAll(kCmd);
		UWorld* W = C->World.Get();
		FString T;
		T += FString::Printf(TEXT("# asda.Perf.Series %s: ключ %s, серий %d (%s), кадров замера %d, прогрев %d\n"),
			*FDateTime::Now().ToString(), *C->Key, C->Done.Num(), C->bAbab ? TEXT("0,1,0,1") : TEXT("0,1,1,0"), C->Frames, C->Warm);
		T += FString::Printf(TEXT("# адаптер: %s; игр на машине (UnrealEditor*/AsdaReborn*, с этой): %d; мир %s; язык %s; сборка %s\n"),
			*GRHIAdapterName, CountGames(), W ? *W->GetMapName() : TEXT("?"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"),
			LexToString(FApp::GetBuildConfiguration()));
		T += FString::Printf(TEXT("# команда серии: %s; команда кадра: %s ×%d; тактов в мкс %.2f; время — мкс игрового потока\n"),
			C->Cmd.IsEmpty() ? TEXT("-") : *C->Cmd, C->Tick.IsEmpty() ? TEXT("-") : *C->Tick, C->Reps, AsdaPerf::TicksPerUs());
		T += TEXT("серия\tключ\t") + AsdaPerf::TsvHeader() + TEXT("\n");
		TArray<FString> Names;   // области сводки — в порядке появления
		for (int32 I = 0; I < C->Done.Num(); ++I)
		{
			for (const FAsdaPerfRow& R : C->Done[I].Rows)
			{
				T += FString::Printf(TEXT("%d\t%d\t"), I, C->Done[I].KeyValue) + AsdaPerf::TsvRow(R) + TEXT("\n");
				Names.AddUnique(R.Name);
			}
		}
		T += TEXT("# СВОДКА (медиана по сериям; разброс — макс−мин «среднего на вызов» между сериями одного значения ключа)\n");
		T += TEXT("сводка\tобласть\tключ\tсерий\tвызовов_в_серии\tсреднее\tмедиана\tp95\tкадр_среднее\tразброс\n");
		int32 NVerdicts = 0;
		for (const FString& Name : Names)
		{
			double Mean[2] = { 0.0, 0.0 }, Spread[2] = { 0.0, 0.0 };
			double FMean[2] = { 0.0, 0.0 }, FSpread[2] = { 0.0, 0.0 };   // то же по «среднему за кадр»
			int32 NSer[2] = { 0, 0 };
			for (int32 V = 0; V < 2; ++V)
			{
				TArray<double> AMean, AMed, AP95, AFrame, ACalls;
				for (const FSeries& S : C->Done)
				{
					if (S.KeyValue != V) { continue; }
					if (const FAsdaPerfRow* R = RowOf(S, Name))
					{
						AMean.Add(R->Mean); AMed.Add(R->Med); AP95.Add(R->P95); AFrame.Add(R->FrameMean); ACalls.Add((double)R->Calls);
					}
				}
				NSer[V] = AMean.Num();
				Mean[V] = Median(AMean);
				Spread[V] = AMean.Num() ? FMath::Max(AMean) - FMath::Min(AMean) : 0.0;
				FMean[V] = Median(AFrame);
				FSpread[V] = AFrame.Num() ? FMath::Max(AFrame) - FMath::Min(AFrame) : 0.0;
				T += FString::Printf(TEXT("сводка\t%s\t%d\t%d\t%.0f\t%.4f\t%.4f\t%.4f\t%.3f\t%.4f\n"),
					*Name, V, NSer[V], Median(ACalls), Mean[V], Median(AMed), Median(AP95), Median(AFrame), Spread[V]);
			}
			// ★ R4-c (14.11.2026, ревизия) С-1: приговор — только при двух и более сериях на КАЖДОЕ значение ключа. При одной серии разброс
			//   равен нулю, и любая разница (в том числе у постороннего ключа) получала «ИЗМЕРИМО».
			if ((NSer[0] > 0 && NSer[1] > 0) && (NSer[0] < 2 || NSer[1] < 2))
			{
				T += FString::Printf(TEXT("без_приговора\t%s: серий на значение ключа %d и %d — для приговора нужно не меньше двух (pairs=2 и больше)\n"),
					*Name, NSer[0], NSer[1]);
			}
			if (NSer[0] > 1 && NSer[1] > 1)
			{
				const double Diff = Mean[0] - Mean[1];
				const double Noise = FMath::Max(Spread[0], Spread[1]);
				const bool bMeasurable = FMath::Abs(Diff) > Noise;
				const FString Line = FString::Printf(TEXT("%s: ключ 0 — %.4f, ключ 1 — %.4f, разница %.4f (%+.1f%%), разброс %.4f → %s"),
					*Name, Mean[0], Mean[1], Diff, Mean[0] > 0.0 ? 100.0 * Diff / Mean[0] : 0.0, Noise,
					bMeasurable ? (Diff > 0.0 ? TEXT("ИЗМЕРИМО, новое быстрее") : TEXT("ИЗМЕРИМО, новое МЕДЛЕННЕЕ")) : TEXT("НЕ ИЗМЕРИМО"));
				T += TEXT("приговор\t") + Line + TEXT("\n");
				UE_LOG(LogAsdaPerfSeries, Log, TEXT("ПЕРФ-СЕРИИ приговор %s"), *Line);
				++NVerdicts;
				const double FDiff = FMean[0] - FMean[1];
				const double FNoise = FMath::Max(FSpread[0], FSpread[1]);
				const bool bFMeasurable = FMath::Abs(FDiff) > FNoise;
				const FString FLine = FString::Printf(TEXT("%s за кадр: ключ 0 — %.3f, ключ 1 — %.3f, разница %.3f (%+.1f%%), разброс %.3f → %s"),
					*Name, FMean[0], FMean[1], FDiff, FMean[0] > 0.0 ? 100.0 * FDiff / FMean[0] : 0.0, FNoise,
					bFMeasurable ? (FDiff > 0.0 ? TEXT("ИЗМЕРИМО, новое быстрее") : TEXT("ИЗМЕРИМО, новое МЕДЛЕННЕЕ")) : TEXT("НЕ ИЗМЕРИМО"));
				T += TEXT("приговор_кадр\t") + FLine + TEXT("\n");
				UE_LOG(LogAsdaPerfSeries, Log, TEXT("ПЕРФ-СЕРИИ приговор %s"), *FLine);
			}
		}
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(C->File), true);
		const bool bSaved = FFileHelper::SaveStringToFile(T, *C->File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogAsdaPerfSeries, Log, TEXT("ПЕРФ-СЕРИИ файл %s: %s (серий %d, областей %d, приговоров %d)"),
			bSaved ? TEXT("записан") : TEXT("НЕ ЗАПИСАН"), *C->File, C->Done.Num(), Names.Num(), NVerdicts);
		// Итог команды: ok — файл записан и по каждой заказанной области есть приговор; fail — иначе.
		const int32 Want = C->Scopes.Num() ? C->Scopes.Num() : 1;
		// ★ R4-c (14.11.2026, ревизия): оборванный прогон (мир ушёл — серий меньше заказанных) — отказ; при pairs=1 приговоров нет по правилу
		//   выше, такой прогон — просто снимок областей, итог ok по записанному файлу.
		const bool bAll = C->Done.Num() == C->Pairs * 2;
		const bool bOk = bSaved && bAll && (C->Pairs < 2 || NVerdicts >= Want);
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaPerfSeries, TEXT("ПЕРФ-СЕРИИ"), bOk ? 1 : 0, bOk ? 0 : 1,
			*FString::Printf(TEXT(" (ключ %s, приговоров %d из %d)"), *C->Key, NVerdicts, Want));
	}

	// Шаг по кадру игры. false — тикер снят.
	static bool Step(const TSharedRef<FCtx>& C)
	{
		if (FPlatformTime::Seconds() < C->StartAt) { return true; }
		if (!C->World.IsValid()) { Finish(C); return false; }
		if (C->Phase == 0)
		{
			++C->Index;
			if (C->Index >= C->Pairs * 2) { Finish(C); return false; }
			AsdaCVarSave::SetForTest(*C->Key, KeyOf(*C, C->Index));
			Exec(*C, C->Cmd);
			C->Phase = 1; C->Left = C->Warm;
		}
		if (C->Phase == 1)
		{
			if (C->Left-- > 0) { for (int32 I = 0; I < C->Reps; ++I) { Exec(*C, C->Tick); } return true; }
			AsdaPerf::Reset();
			C->Phase = 2; C->Left = C->Frames;
		}
		if (C->Left-- > 0) { for (int32 I = 0; I < C->Reps; ++I) { Exec(*C, C->Tick); } return true; }
		FSeries& S = C->Done.AddDefaulted_GetRef();
		S.KeyValue = KeyOf(*C, C->Index);
		TArray<FAsdaPerfRow> All;
		AsdaPerf::Snapshot(All);
		for (FAsdaPerfRow& R : All) { if (C->Scopes.Num() == 0 || C->Scopes.Contains(R.Name)) { S.Rows.Add(MoveTemp(R)); } }
		UE_LOG(LogAsdaPerfSeries, Log, TEXT("ПЕРФ-СЕРИИ серия %d из %d: ключ %s=%d, областей %d"),
			C->Index + 1, C->Pairs * 2, *C->Key, S.KeyValue, S.Rows.Num());
		C->Phase = 0;
		return true;
	}

	// Разбор аргументов и старт.
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const TSharedRef<FCtx> C = MakeShared<FCtx>();
		C->World = W;
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("key="))) { C->Key = A.Mid(4); }
			else if (A.StartsWith(TEXT("scopes="))) { A.Mid(7).ParseIntoArray(C->Scopes, TEXT(","), true); }
			else if (A.StartsWith(TEXT("pairs="))) { C->Pairs = FMath::Clamp(FCString::Atoi(*A.Mid(6)), 1, 64); }
			else if (A.StartsWith(TEXT("frames="))) { C->Frames = FMath::Clamp(FCString::Atoi(*A.Mid(7)), 1, 100000); }
			else if (A.StartsWith(TEXT("warm="))) { C->Warm = FMath::Clamp(FCString::Atoi(*A.Mid(5)), 0, 100000); }
			else if (A.StartsWith(TEXT("order="))) { C->bAbab = A.Mid(6).Equals(TEXT("abab"), ESearchCase::IgnoreCase); }
			else if (A.StartsWith(TEXT("cmd="))) { C->Cmd = A.Mid(4); }
			else if (A.StartsWith(TEXT("tick="))) { C->Tick = A.Mid(5); }
			else if (A.StartsWith(TEXT("reps="))) { C->Reps = FMath::Clamp(FCString::Atoi(*A.Mid(5)), 1, 100000); }
			else if (A.StartsWith(TEXT("file="))) { C->File = A.Mid(5); }
			else if (A.StartsWith(TEXT("delay="))) { C->Delay = FMath::Max(0.f, FCString::Atof(*A.Mid(6))); }
			else if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { C->bQuit = true; }
		}
		if (!W) { AsdaSelfTestKit::Refused(W, kCmd, C->bQuit, TEXT("нет мира")); return; }
		IConsoleVariable* Var = C->Key.IsEmpty() ? nullptr : IConsoleManager::Get().FindConsoleVariable(*C->Key);
		if (!Var) { AsdaSelfTestKit::Refused(W, kCmd, C->bQuit, *FString::Printf(TEXT("нет ключа «%s» (key=<ключ правки>)"), *C->Key)); return; }
		if (C->File.IsEmpty())
		{
			C->File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AsdaPerf"),
				FString::Printf(TEXT("series_%s_%s.tsv"), *C->Key, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		C->StartAt = FPlatformTime::Seconds() + (double)C->Delay;
		UE_LOG(LogAsdaPerfSeries, Log, TEXT("ПЕРФ-СЕРИИ старт: ключ %s (сейчас %d), серий %d, кадров %d, прогрев %d, областей %d, файл %s"),
			*C->Key, Var->GetInt(), C->Pairs * 2, C->Frames, C->Warm, C->Scopes.Num(), *C->File);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return Step(C); }), 0.f);
	}
}

// Консольная команда серий замера.
static FAutoConsoleCommandWithWorldAndArgs GAsdaPerfSeries(
	TEXT("asda.Perf.Series"),
	TEXT("[ПОРТ] Серии замера «ключ правки 0/1 в одном процессе» по областям AsdaPerf.h: key=<ключ> [scopes=<имя,имя>] [pairs=4] "
	     "[frames=240] [warm=30] [order=abba|abab] [cmd=<команда+аргументы>] [tick=<команда+аргументы>] [reps=1] [file=<путь>] "
	     "[delay=N] [quit]. Итог — файл со строками серий, сводкой и приговором «измеримо / не измеримо»."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaPerfSeries::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
