// Copyright (c) 2026 @sitxovski (Discord)
// Журнал боя — реализация (описание и правила — в Public/World/AsdaCombatTrace.h). [ПОРТ]
#include "World/AsdaCombatTrace.h"

#if ASDA_COMBAT_TRACE

#include "AsdaDevConsole.h"
#include "AsdaPerf.h"
#include "HAL/CriticalSection.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCombatTrace, Log, All);

// 0 — выкл; 1 — события цикла; 2 — ещё и частые (тики бега, шаги плавных полос).
static ASDA_DEV_CVAR(int32, CVarAsdaCombatTrace, TEXT("asda.Combat.Trace"), 1,
	TEXT("Журнал боя (редактор/Development): 0 — выкл, 1 — все события боевого цикла (умолч.), 2 — ещё и частые (бег по тикам, плавные полосы)."),
	ECVF_Default);
// 1 — дублировать журнал в отдельный файл Saved/Logs/AsdaCombat_*.log; 0 — только основной журнал.
static ASDA_DEV_CVAR(int32, CVarAsdaCombatTraceFile, TEXT("asda.Combat.TraceFile"), 1,
	TEXT("Журнал боя в отдельный файл Saved/Logs/AsdaCombat_<дата>_<pid>.log: 1 — да (умолч.), 0 — только основной журнал."),
	ECVF_Default);
// ★ R4, доля a, запись 10 (15.11.2026): 1 — строку файла пишет фоновый поток (игровой только кладёт её в очередь); 0 — прежняя запись на месте.
static ASDA_DEV_CVAR(int32, CVarAsdaCombatTraceFileAsync, TEXT("asda.Combat.TraceFileAsync"), 1,
	TEXT("Файл журнала боя пишет фоновый поток: 1 — да (умолч.; игровой поток кладёт строку в очередь и на диске не стоит), 0 — прежняя запись в самом вызове."),
	ECVF_Default);

namespace
{
	class FAsdaCombatTraceWriter;

	// Состояние журнала (один на процесс). Доступ под замком: строки идут и из сетевого потока разбора.
	struct FAsdaCombatTraceState
	{
		FCriticalSection Lock;               // замок записи и полей цикла
		IFileHandle* File = nullptr;         // открытый файл журнала (nullptr — ещё не открыт/не удалось)
		bool bFileTried = false;             // попытка открыть файл уже была (повторно не пытаемся)
		FString FilePath;                    // полный путь открытого файла (пусто — файла нет)
		FAsdaCombatTraceWriter* Writer = nullptr;   // ★ R4, запись 10: фоновая запись файла (nullptr — ещё не понадобилась); живёт до конца процесса
		int32 Cycle = 0;                     // номер текущего цикла (0 — вне цикла)
		int32 LastCycle = 0;                 // последний выданный номер цикла
		int32 CycleRid = -1;                 // цель текущего цикла: rid моба или userIdx игрока (см. bCyclePvp)
		double CycleStartS = 0.0;            // время начала текущего цикла (FPlatformTime::Seconds)
		double ProcStartS = 0.0;             // время первой записи (точка отсчёта колонки времени)
		bool bCyclePvp = false;              // цель текущего цикла — игрок (CycleRid = userIdx), иначе моб (rid)
		int32 SelfUserIdx = -1;              // свой userIdx (SetSelfUserIdx; -1 — неизвестен)
		TMap<int32, double> Engaged;         // rid моба → время последнего нашего удара/каста по нему (NoteEngaged)
		TMap<int32, double> EngagedUsers;    // userIdx игрока → время последнего участия в нашем бою (NoteEngagedUser)
	};

	FAsdaCombatTraceState& St()
	{
		static FAsdaCombatTraceState S;
		return S;
	}

	// Подпись этапа фиксированной ширины (9) — колонки журнала не гуляют.
	const TCHAR* StageName(EAsdaCT Stage)
	{
		switch (Stage)
		{
		case EAsdaCT::Input:     return TEXT("ВВОД     ");
		case EAsdaCT::Target:    return TEXT("ЦЕЛЬ     ");
		case EAsdaCT::Move:      return TEXT("БЕГ      ");
		case EAsdaCT::Range:     return TEXT("ДАЛЬН    ");
		case EAsdaCT::Send:      return TEXT("ОТПР     ");
		case EAsdaCT::Recv:      return TEXT("ПРИЁМ    ");
		case EAsdaCT::Queue:     return TEXT("ОЧЕРЕДЬ  ");
		case EAsdaCT::Swing:     return TEXT("ЗАМАХ    ");
		case EAsdaCT::HitFrame:  return TEXT("КАДР     ");
		case EAsdaCT::Damage:    return TEXT("УРОН     ");
		case EAsdaCT::DmgText:   return TEXT("ЦИФРА    ");
		case EAsdaCT::HpTarget:  return TEXT("HP-ЦЕЛИ  ");
		case EAsdaCT::HpPlate:   return TEXT("HP-ПЛАШК ");
		case EAsdaCT::HpSelf:    return TEXT("HP-СВОЙ  ");
		case EAsdaCT::Death:     return TEXT("СМЕРТЬ   ");
		case EAsdaCT::Corpse:    return TEXT("ТРУП     ");
		case EAsdaCT::Reward:    return TEXT("НАГРАДА  ");
		case EAsdaCT::Exit:      return TEXT("ВЫХОД    ");
		case EAsdaCT::Refuse:    return TEXT("ОТКАЗ    ");
		case EAsdaCT::MobAttack: return TEXT("МОБ-БЬЁТ ");
		case EAsdaCT::Skill:     return TEXT("НАВЫК    ");
		case EAsdaCT::Peer:      return TEXT("СОСЕД    ");
		case EAsdaCT::Pvp:       return TEXT("PvP      ");
		case EAsdaCT::Fx:        return TEXT("ЭФФЕКТ   ");
		case EAsdaCT::State:     return TEXT("СОСТ     ");
		case EAsdaCT::Note:      return TEXT("МЕТКА    ");
		}
		return TEXT("?        ");
	}

	// Удалить старые файлы журнала, оставив Keep самых новых (имя содержит дату — сортировка по имени = по времени).
	void PruneOldFiles(const FString& Dir, int32 Keep)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("AsdaCombat_*.log")), /*Files=*/true, /*Dirs=*/false);
		if (Files.Num() <= Keep) { return; }
		Files.Sort();
		for (int32 i = 0; i < Files.Num() - Keep; ++i)
		{
			IFileManager::Get().Delete(*(Dir / Files[i]), /*RequireExists=*/false, /*EvenReadOnly=*/false, /*Quiet=*/true);
		}
	}

	// Открыть файл журнала (один раз за процесс). Зовётся под замком.
	void EnsureFile(FAsdaCombatTraceState& S)
	{
		if (S.bFileTried) { return; }
		S.bFileTried = true;
		const FString Dir = FPaths::ProjectLogDir();
		IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
		PruneOldFiles(Dir, 9);   // + новый = 10
		const FString Name = FString::Printf(TEXT("AsdaCombat_%s_%u.log"),
			*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), FPlatformProcess::GetCurrentProcessId());
		const FString Path = Dir / Name;
		S.File = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, /*bAppend=*/false, /*bAllowRead=*/true);
		if (S.File)
		{
			S.FilePath = FPaths::ConvertRelativePathToFull(Path);
			static const uint8 Bom[3] = { 0xEF, 0xBB, 0xBF };
			S.File->Write(Bom, 3);
			UE_LOG(LogAsdaCombatTrace, Log, TEXT("[БОЙ] журнал боя пишется в %s (asda.Combat.Trace 0 — выкл, asda.Combat.TraceFile 0 — без файла)"),
				*FPaths::ConvertRelativePathToFull(Path));
		}
		else
		{
			UE_LOG(LogAsdaCombatTrace, Warning, TEXT("[БОЙ] файл журнала боя %s не открылся — пишется только в основной журнал"), *Path);
		}
	}

	// Дописать строку в файл (UTF-8 + перевод строки). Зовётся под замком.
	//   ⚠ БЕЗ Flush: на Windows это FlushFileBuffers — принудительная запись на диск ~1.5 мс на строку (замер 18.10.2026:
	//   пакет боя в 15 строк давал рывок кадра ~25 мс и ронял тайминговые самотесты). WriteFile кладёт данные в кэш ОС —
	//   они переживают падение ПРОЦЕССА (теряются только при падении самой Windows), этого журналу достаточно.
	void WriteFileLine(FAsdaCombatTraceState& S, const FString& Line)
	{
		if (!S.File) { return; }
		FTCHARToUTF8 Utf8(*Line);
		S.File->Write(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		static const uint8 Eol[2] = { '\r', '\n' };
		S.File->Write(Eol, 2);
	}

	// ★ R4, доля a, запись 10 (15.11.2026) [ПРОИЗВОДИТЕЛЬНОСТЬ]: запись файла журнала боя фоновым потоком.
	//   ПРИЧИНА. `IFileHandle::Write` — это `WriteFile` в кэш ОС; обычно он возвращается за микросекунды, но при занятом диске
	//   (кэш переполнен грязными страницами — рядом пишет другая игра, идёт сборка) стоит, пока ОС не сбросит часть кэша.
	//   Стоял он на игровом потоке — вместе с кадром (собранная игра, 15.11.2026: кадр приёма пакета 5257 — 1,737 с).
	//   ЧТО ДЕЛАЕТ. Игровой (или сетевой) поток кладёт готовую строку в очередь под коротким замком и будит фоновый поток;
	//   перевод в UTF-8 и `Write` идут в фоновом. Порядок строк — порядок постановки в очередь (она идёт под замком журнала).
	//   Запись на месте (ключ 0 после ключа 1) сначала дописывает очередь — порядок цел и при переключении.
	//   СОХРАННОСТЬ. Раньше строка была в кэше ОС сразу и переживала падение процесса. Теперь она до нескольких миллисекунд
	//   лежит в очереди, поэтому очередь дописывается при выходе (`OnPreExit`) и в обработчике падения (`OnHandleSystemError`,
	//   замки берутся без ожидания: если фоновый поток как раз пишет, он сам допишет не всё — эти строки есть в основном журнале).
	//   Основной журнал (`UE_LOG`) не тронут.
	class FAsdaCombatTraceWriter final : public FRunnable
	{
	public:
		// Создать поток записи. Зовётся под замком журнала один раз. false — поток не создан (писать на месте).
		bool Start()
		{
			if (!FPlatformProcess::SupportsMultithreading()) { return false; }
			Wake = FPlatformProcess::GetSynchEventFromPool(/*bIsManualReset=*/false);
			Thread = FRunnableThread::Create(this, TEXT("AsdaCombatTraceWriter"), 0, TPri_BelowNormal);
			if (!Thread) { return false; }
			FCoreDelegates::OnPreExit.AddRaw(this, &FAsdaCombatTraceWriter::Shutdown);
			FCoreDelegates::OnHandleSystemError.AddRaw(this, &FAsdaCombatTraceWriter::DrainOnCrash);
			return true;
		}

		// Положить строку в очередь (любой поток). false — поток остановлен (выход из игры): писать на месте.
		bool Enqueue(IFileHandle* InFile, FString&& Line)
		{
			{
				FScopeLock L(&QLock);
				if (bDown) { return false; }
				File = InFile;
				Queue.Add(MoveTemp(Line));
				MaxQueued = FMath::Max(MaxQueued, Queue.Num());
			}
			Wake->Trigger();
			return true;
		}

		// Запись на месте при живой очереди: сначала дописать очередь, затем строку (порядок строк цел).
		void WriteNow(IFileHandle* InFile, const FString& Line)
		{
			FScopeLock L(&FileLock);
			DrainLocked();
			WriteLine(InFile, Line);
		}

		// Дописать в файл всё, что в очереди (любой поток).
		void Drain()
		{
			FScopeLock L(&FileLock);
			DrainLocked();
		}

		// Счётчики: строк записано из очереди, наибольшая очередь, самая долгая запись пачки (с).
		void GetStats(int64& OutLines, int32& OutMaxQueued, double& OutMaxWriteS)
		{
			FScopeLock L(&QLock);
			OutLines = LinesWritten; OutMaxQueued = MaxQueued; OutMaxWriteS = MaxWriteS;
		}

		// Тело потока: спать до сигнала (или 250 мс), дописать очередь.
		virtual uint32 Run() override
		{
			while (!bStop)
			{
				Wake->Wait(250);
				Drain();
			}
			return 0;
		}

		// Просьба остановиться (зовёт и `FRunnableThread::Kill`).
		virtual void Stop() override
		{
			bStop = true;
			if (Wake) { Wake->Trigger(); }
		}

	private:
		// Одна строка в файл: UTF-8 + перевод строки, без Flush (см. `WriteFileLine`).
		static void WriteLine(IFileHandle* InFile, const FString& Line)
		{
			if (!InFile) { return; }
			FTCHARToUTF8 Utf8(*Line);
			InFile->Write(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			static const uint8 Eol[2] = { '\r', '\n' };
			InFile->Write(Eol, 2);
		}

		// Забрать очередь и записать её. Зовётся под `FileLock` (он же хранит порядок пачек между потоками).
		void DrainLocked()
		{
			IFileHandle* F = nullptr;
			{
				FScopeLock L(&QLock);
				Swap(Queue, Work);
				F = File;
			}
			if (Work.Num() == 0) { return; }
			const double T0 = FPlatformTime::Seconds();
			for (const FString& Line : Work) { WriteLine(F, Line); }
			const double Dt = FPlatformTime::Seconds() - T0;
			const int32 Num = Work.Num();
			Work.Reset();
			FScopeLock L(&QLock);
			LinesWritten += Num;
			MaxWriteS = FMath::Max(MaxWriteS, Dt);
		}

		// Выход из игры: остановить поток, дописать остаток; дальше строки пишутся на месте.
		void Shutdown()
		{
			{
				FScopeLock L(&QLock);
				bDown = true;
			}
			Stop();
			if (Thread) { Thread->WaitForCompletion(); }
			Drain();
		}

		// Падение процесса: дописать очередь, не ожидая замков (их мог держать упавший или пишущий поток).
		void DrainOnCrash()
		{
			if (!FileLock.TryLock()) { return; }
			if (QLock.TryLock())
			{
				IFileHandle* F = File;
				TArray<FString> Rest = MoveTemp(Queue);
				Queue.Reset();
				QLock.Unlock();
				for (const FString& Line : Rest) { WriteLine(F, Line); }
			}
			FileLock.Unlock();
		}

		FCriticalSection QLock;            // очередь и счётчики
		FCriticalSection FileLock;         // запись в файл и порядок пачек (берётся ДО `QLock`)
		TArray<FString> Queue;             // строки, ждущие записи (под `QLock`)
		TArray<FString> Work;              // пачка в записи (под `FileLock`); память переиспользуется
		IFileHandle* File = nullptr;       // файл журнала (под `QLock`; открыт один раз за процесс и не закрывается)
		FEvent* Wake = nullptr;            // сигнал «в очереди есть строки»
		FRunnableThread* Thread = nullptr; // поток записи
		std::atomic<bool> bStop { false }; // поток просят остановиться
		bool bDown = false;                // поток остановлен: очередь больше не принимает (под `QLock`)
		int64 LinesWritten = 0;            // строк записано из очереди (под `QLock`)
		int32 MaxQueued = 0;               // наибольшая длина очереди (под `QLock`)
		double MaxWriteS = 0.0;            // самая долгая запись одной пачки, с (под `QLock`)
	};

	// Фоновая запись файла: создать при первой надобности. Зовётся под замком журнала. nullptr — потока нет.
	FAsdaCombatTraceWriter* EnsureWriter(FAsdaCombatTraceState& S)
	{
		static bool bTried = false;   // попытка создать поток уже была
		if (!bTried)
		{
			bTried = true;
			FAsdaCombatTraceWriter* W = new FAsdaCombatTraceWriter();   // живёт до конца процесса (поток держит его)
			if (W->Start()) { S.Writer = W; } else { delete W; }
		}
		return S.Writer;
	}

	// Собрать строку с колонками и отправить в оба приёмника. Зовётся под замком.
	void EmitLocked(FAsdaCombatTraceState& S, EAsdaCT Stage, const FString& Text)
	{
		const double Now = FPlatformTime::Seconds();
		if (S.ProcStartS <= 0.0) { S.ProcStartS = Now; }
		const FString Since = (S.Cycle > 0)
			? FString::Printf(TEXT("%+7.3f"), Now - S.CycleStartS)
			: FString(TEXT("   -   "));
		FString Line = FString::Printf(TEXT("[БОЙ] %08.3f к=%09llu ц#%04d %s %s| %s"),
			Now - S.ProcStartS, (unsigned long long)GFrameCounter, S.Cycle, *Since, StageName(Stage), *Text);
		{
			ASDA_PERF_SCOPE("CombatTrace.Log");   // ★ R4, запись 10: время первого приёмника (основной журнал) на игровом потоке
			UE_LOG(LogAsdaCombatTrace, Log, TEXT("%s"), *Line);
		}
		if (CVarAsdaCombatTraceFile.GetValueOnAnyThread() != 0)
		{
			{
				ASDA_PERF_SCOPE("CombatTrace.FileOpen");   // открытие файла (один раз за процесс; дальше — пустой вызов)
				EnsureFile(S);
			}
			ASDA_PERF_SCOPE("CombatTrace.File");   // ★ R4, запись 10: время второго приёмника (файл) на игровом потоке
			if (!S.File) { return; }
			// ★ R4, запись 10: при ключе 1 строка уходит в очередь фонового потока; иначе — прежняя запись на месте
			//   (если очередь уже заводилась — через неё, чтобы строка не обогнала ждущие).
			if (CVarAsdaCombatTraceFileAsync.GetValueOnAnyThread() != 0)
			{
				FAsdaCombatTraceWriter* W = EnsureWriter(S);
				if (W && W->Enqueue(S.File, MoveTemp(Line))) { return; }
			}
			if (S.Writer) { S.Writer->WriteNow(S.File, Line); }
			else { WriteFileLine(S, Line); }
		}
	}
}

namespace AsdaCombatTrace
{
	bool IsOn(int32 Level)
	{
		return CVarAsdaCombatTrace.GetValueOnAnyThread() >= Level;
	}

	void Emit(EAsdaCT Stage, const FString& Text)
	{
		FAsdaCombatTraceState& S = St();
		FScopeLock L(&S.Lock);
		EmitLocked(S, Stage, Text);
	}

	// Общее начало цикла: bPvp — цель игрок (userIdx), иначе моб (rid).
	static int32 BeginCycleImpl(int32 TargetId, bool bPvp, const TCHAR* TargetName, const TCHAR* Why)
	{
		FAsdaCombatTraceState& S = St();
		FScopeLock L(&S.Lock);
		if (!IsOn(1)) { return S.Cycle; }
		if (S.Cycle > 0)
		{
			EmitLocked(S, EAsdaCT::Exit, FString::Printf(TEXT("═══ цикл #%d закрыт: начат новый (%s)"), S.Cycle, Why ? Why : TEXT("?")));
		}
		S.Cycle = ++S.LastCycle;
		S.CycleRid = TargetId;
		S.bCyclePvp = bPvp;
		S.CycleStartS = FPlatformTime::Seconds();
		EmitLocked(S, EAsdaCT::Target, FString::Printf(TEXT("═══ ЦИКЛ #%d НАЧАТ: цель %s=%d '%s' (%s)"),
			S.Cycle, bPvp ? TEXT("игрок userIdx") : TEXT("моб rid"), TargetId, TargetName ? TargetName : TEXT(""), Why ? Why : TEXT("?")));
		return S.Cycle;
	}

	int32 BeginCycle(int32 TargetRid, const TCHAR* TargetName, const TCHAR* Why)
	{
		return BeginCycleImpl(TargetRid, /*bPvp=*/false, TargetName, Why);
	}

	int32 BeginCyclePlayer(int32 TargetUserIdx, const TCHAR* TargetName, const TCHAR* Why)
	{
		return BeginCycleImpl(TargetUserIdx, /*bPvp=*/true, TargetName, Why);
	}

	void EndCycle(const TCHAR* Why)
	{
		FAsdaCombatTraceState& S = St();
		FScopeLock L(&S.Lock);
		if (S.Cycle <= 0) { return; }
		if (IsOn(1))
		{
			EmitLocked(S, EAsdaCT::Exit, FString::Printf(TEXT("═══ ЦИКЛ #%d ЗАКРЫТ за %.3f с: %s (цель %s=%d)"),
				S.Cycle, FPlatformTime::Seconds() - S.CycleStartS, Why ? Why : TEXT("?"),
				S.bCyclePvp ? TEXT("игрок userIdx") : TEXT("моб rid"), S.CycleRid));
		}
		S.Cycle = 0;
		S.CycleRid = -1;
		S.bCyclePvp = false;
	}

	// ★ R4, запись 10: путь файла журнала.
	FString FilePath()
	{
		FAsdaCombatTraceState& S = St();
		FScopeLock L(&S.Lock);
		return S.FilePath;
	}

	// ★ R4, запись 10: дописать очередь фоновой записи (замок журнала на время записи не держится).
	void FlushFile()
	{
		FAsdaCombatTraceWriter* W = nullptr;
		{
			FAsdaCombatTraceState& S = St();
			FScopeLock L(&S.Lock);
			W = S.Writer;
		}
		if (W) { W->Drain(); }
	}

	// ★ R4, запись 10: счётчики фоновой записи.
	void FileWriterStats(int64& OutLines, int32& OutMaxQueued, double& OutMaxWriteS)
	{
		OutLines = 0; OutMaxQueued = 0; OutMaxWriteS = 0.0;
		FAsdaCombatTraceWriter* W = nullptr;
		{
			FAsdaCombatTraceState& S = St();
			FScopeLock L(&S.Lock);
			W = S.Writer;
		}
		if (W) { W->GetStats(OutLines, OutMaxQueued, OutMaxWriteS); }
	}

	int32 CurrentCycle() { return St().Cycle; }
	int32 CurrentCycleTarget() { const FAsdaCombatTraceState& S = St(); return (S.Cycle > 0 && !S.bCyclePvp) ? S.CycleRid : -1; }
	int32 CurrentCyclePlayer() { const FAsdaCombatTraceState& S = St(); return (S.Cycle > 0 && S.bCyclePvp) ? S.CycleRid : -1; }
	void SetSelfUserIdx(int32 UserIdx) { St().SelfUserIdx = UserIdx; }

	// Отметить id в карте «последний раз в нашем бою» (старше 30 с — вычищаются, когда карта растёт). Под замком.
	static void NoteRecent(TMap<int32, double>& Map, int32 Id)
	{
		const double Now = FPlatformTime::Seconds();
		if (Map.Num() > 32)
		{
			for (auto It = Map.CreateIterator(); It; ++It) { if (Now - It.Value() > 30.0) { It.RemoveCurrent(); } }
		}
		Map.Add(Id, Now);
	}

	// Был ли id в нашем бою за последние 30 с. Под замком.
	static bool IsRecent(const TMap<int32, double>& Map, int32 Id)
	{
		const double* T = Map.Find(Id);
		return T && FPlatformTime::Seconds() - *T <= 30.0;
	}

	void NoteEngaged(int32 Rid)
	{
		if (Rid < 0) { return; }
		FAsdaCombatTraceState& S = St();
		FScopeLock L(&S.Lock);
		NoteRecent(S.Engaged, Rid);
	}

	bool IsEngaged(int32 Rid)
	{
		if (Rid < 0) { return false; }
		FAsdaCombatTraceState& S = St();
		FScopeLock L(&S.Lock);
		if (S.Cycle > 0 && !S.bCyclePvp && Rid == S.CycleRid) { return true; }
		return IsRecent(S.Engaged, Rid);
	}

	void NoteEngagedUser(int32 UserIdx)
	{
		if (UserIdx < 0) { return; }
		FAsdaCombatTraceState& S = St();
		FScopeLock L(&S.Lock);
		NoteRecent(S.EngagedUsers, UserIdx);
	}

	bool IsUserRelevant(int32 UserIdx)
	{
		if (UserIdx < 0) { return false; }
		FAsdaCombatTraceState& S = St();
		FScopeLock L(&S.Lock);
		if (UserIdx == S.SelfUserIdx) { return true; }
		if (S.Cycle > 0 && S.bCyclePvp && UserIdx == S.CycleRid) { return true; }
		return IsRecent(S.EngagedUsers, UserIdx);
	}
}

// Метка в журнале боя: asda.Combat.TraceMark <текст>.
static FAutoConsoleCommand GAsdaCombatTraceMarkCmd(
	TEXT("asda.Combat.TraceMark"),
	TEXT("Вставить метку в журнал боя: asda.Combat.TraceMark <текст> (редактор/Development)."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		AsdaCombatTrace::Emit(EAsdaCT::Note, FString::Printf(TEXT("★ МЕТКА: %s"), *FString::Join(Args, TEXT(" "))));
	}));

#endif // ASDA_COMBAT_TRACE
