// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4, доля a (15.11.2026) [ПОРТ, производительность]: классы эффектов — единая точка загрузки, удержание (А) и предзагрузка (Б).
//   Разбор и правила — в заголовке `AsdaFxClassCache.h`.
#include "AsdaFxClassCache.h"

#include "AsdaDevConsole.h"
#include "AsdaPerf.h"
#include "Containers/Ticker.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "Resources/AsdaAssetPaths.h"
#include "UObject/GCObject.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#if WITH_EDITOR
#include "Editor.h"   // FEditorDelegates::EndPIE
#endif

// Запись 11 (15.11.2026): ключи `asda.Perf.FxClassKeep` и `asda.Perf.FxPreload` и прежний путь (без удержания, без предзагрузки) убраны.

namespace
{
	using AsdaFxClassCache::FStats;

	// Удержанный класс (вариант А).
	struct FKept
	{
		TObjectPtr<UClass> Cls;   // класс эффекта
		uint64 Use = 0;           // номер последнего обращения (меньше — давнее)
	};
	// Идущая асинхронная заявка.
	struct FFlight
	{
		FName Key;                              // путь класса
		TSharedPtr<FStreamableHandle> Handle;   // заявка загрузчика
		double StartS = 0.0;                    // когда начата, с
	};

	// Состояние (одно на процесс; только игровой поток). Сильные ссылки отдаёт сборщику мусора.
	struct FState : public FGCObject
	{
		TMap<FName, FKept> Kept;                 // удержанные классы
		TArray<FFlight> InFlight;                // идущие заявки
		TArray<FName> Queue;                     // очередь заявок
		TSet<FName> EverLoaded;                  // пути, которые уже загружались (счётчик «загружен заново»)
		TSet<FName> FromPreload;                 // пути, принесённые предзагрузкой и ещё не показанные
		TSet<FName> Missing;                     // пути, по которым предзагрузка класса не нашла (повторно не запрашиваются)
		FStreamableManager Streamable;           // загрузчик заявок
		uint64 UseCounter = 0;                   // счётчик обращений (порядок давности)
		int32 Limit = AsdaFxClassCache::kMaxKept;   // действующий предел удержания
		FStats Stats;                            // счётчики
		bool bTicking = false;                   // покадровый шаг предзагрузки стоит

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			for (TPair<FName, FKept>& KV : Kept) { Collector.AddReferencedObject(KV.Value.Cls); }
		}
		virtual FString GetReferencerName() const override { return TEXT("AsdaFxClassCache"); }
	};

	FState* GState = nullptr;   // создаётся при первом обращении, освобождается перед выходом из игры

	// Годится ли класс под удержание: живой и от `AActor` (то же условие, что у `LoadClass<AActor>` прежнего кода).
	bool IsFxClass(const UClass* Cls) { return IsValid(Cls) && Cls->IsChildOf(AActor::StaticClass()); }

	// Отменить идущие заявки и очистить очередь.
	void CancelAll(FState& S)
	{
		for (FFlight& F : S.InFlight)
		{
			if (F.Handle.IsValid()) { F.Handle->CancelHandle(); }
			++S.Stats.PreloadsDropped;
		}
		S.InFlight.Reset();
		S.Stats.PreloadsDropped += (uint64)S.Queue.Num();
		S.Queue.Reset();
	}

	FState& State()
	{
		if (!GState)
		{
			GState = new FState();
			// Смена карты: незавершённые заявки предзагрузки отменяются, очередь чистится (удержанные классы остаются).
			FCoreUObjectDelegates::PreLoadMap.AddLambda([](const FString&) { AsdaFxClassCache::CancelPreloads(); });
			// Выход из игры: отпустить всё до остановки сборщика мусора.
			FCoreDelegates::OnPreExit.AddLambda([]()
			{
				if (GState)
				{
					CancelAll(*GState);
					delete GState;
					GState = nullptr;
				}
			});
#if WITH_EDITOR
			// Редактор: конец игры в редакторе — отпустить классы, иначе удержанный `BP_Fx_*` нельзя удалить или переименовать.
			FEditorDelegates::EndPIE.AddLambda([](bool)
			{
				if (GState)
				{
					CancelAll(*GState);
					GState->Kept.Reset();
					GState->FromPreload.Reset();
				}
			});
#endif
		}
		return *GState;
	}

	// Вытеснить давно не использованные сверх предела.
	void Trim(FState& S)
	{
		while (S.Kept.Num() > FMath::Max(1, S.Limit))
		{
			FName Oldest;
			uint64 OldestUse = MAX_uint64;
			for (const TPair<FName, FKept>& KV : S.Kept)
			{
				if (KV.Value.Use < OldestUse) { OldestUse = KV.Value.Use; Oldest = KV.Key; }
			}
			S.Kept.Remove(Oldest);
			++S.Stats.Evicted;
		}
	}

	// Взять класс под удержание (вариант А).
	void Keep(FState& S, const FName& Key, UClass* Cls)
	{
		FKept& K = S.Kept.FindOrAdd(Key);
		K.Cls = Cls;
		K.Use = ++S.UseCounter;
		Trim(S);
	}

	// Класс показан: если его принесла предзагрузка — учесть и снять ожидание.
	void NoteShown(FState& S, const FName& Key)
	{
		if (S.FromPreload.Remove(Key) > 0)
		{
			++S.Stats.PreloadHits;
			ASDA_PERF_VALUE("Fx.ClassPreloadHit", 1);
		}
	}

	bool StepPreloads(float);

	// Шаг предзагрузки: закрыть завершённые заявки, начать следующие из очереди, отпустить просроченное.
	void Pump(FState& S)
	{
		const double Now = FPlatformTime::Seconds();
		for (int32 i = S.InFlight.Num() - 1; i >= 0; --i)
		{
			FFlight& F = S.InFlight[i];
			const bool bDone = !F.Handle.IsValid() || F.Handle->HasLoadCompleted() || F.Handle->WasCanceled();
			const bool bTimeout = Now - F.StartS > AsdaFxClassCache::kPreloadTimeoutS;
			if (!bDone && !bTimeout) { continue; }
			const bool bLoaded = F.Handle.IsValid() && F.Handle->HasLoadCompleted() && !F.Handle->WasCanceled();
			UClass* Cls = bLoaded ? Cast<UClass>(F.Handle->GetLoadedAsset()) : nullptr;
			if (bLoaded && !IsFxClass(Cls))
			{
				// Ассета нет либо это не класс актора: прежний код вернул бы nullptr — под удержание не берём и больше не запрашиваем.
				S.Missing.Add(F.Key);
				Cls = nullptr;
			}
			if (Cls)
			{
				++S.Stats.PreloadsDone;
				ASDA_PERF_VALUE("Fx.ClassPreloadDone", 1);
				S.FromPreload.Add(F.Key);
				S.EverLoaded.Add(F.Key);
				Keep(S, F.Key, Cls);
			}
			else
			{
				++S.Stats.PreloadsDropped;
			}
			if (F.Handle.IsValid())
			{
				if (!bDone) { F.Handle->CancelHandle(); } else { F.Handle->ReleaseHandle(); }   // класс теперь держим сами
			}
			S.InFlight.RemoveAtSwap(i);
		}
		while (S.InFlight.Num() < AsdaFxClassCache::kMaxInFlight && S.Queue.Num() > 0)
		{
			const FName Key = S.Queue[0];
			S.Queue.RemoveAt(0);
			const FString KeyPath = Key.ToString();
			if (UClass* Ready = FindObject<UClass>(nullptr, *KeyPath))
			{
				// Пока заявка ждала очереди, класс загрузил сам показ: грузить нечего — только не дать выгрузить до показа.
				if (IsFxClass(Ready)) { Keep(S, Key, Ready); }
				continue;
			}
			FFlight Flight;   // запись собирается отдельно: вызов загрузчика не должен держать ссылку внутрь массива
			Flight.Key = Key;
			Flight.StartS = Now;
			Flight.Handle = S.Streamable.RequestAsyncLoad(FSoftObjectPath(KeyPath));
			S.InFlight.Add(MoveTemp(Flight));
			++S.Stats.PreloadsStarted;
		}
		if (!S.bTicking && (S.InFlight.Num() > 0 || S.Queue.Num() > 0))
		{
			S.bTicking = true;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&StepPreloads), 0.f);
		}
	}

	// Покадровый шаг, пока есть заявки или ждущие показа классы. false — тикер снят.
	bool StepPreloads(float)
	{
		if (!GState) { return false; }
		FState& S = *GState;
		Pump(S);
		if (S.InFlight.Num() == 0 && S.Queue.Num() == 0)
		{
			S.bTicking = false;
			return false;
		}
		return true;
	}
}

UClass* AsdaFxClassCache::Load(const FString& ClassPath)
{
	if (IsEngineExitRequested()) { return LoadClass<AActor>(nullptr, *ClassPath); }
	FState& S = State();
	const FName Key(*ClassPath);
	{
		if (FKept* K = S.Kept.Find(Key))
		{
			UClass* Cls = K->Cls.Get();
			// Класс, который редактор заменил новой версией (перекомпиляция BP), из удержанных убирается — загрузится свежий.
			if (IsValid(Cls) && !Cls->HasAnyClassFlags(CLASS_NewerVersionExists))
			{
				K->Use = ++S.UseCounter;
				++S.Stats.KeptHits;
				ASDA_PERF_VALUE("Fx.ClassKeptHit", 1);
				NoteShown(S, Key);
				return Cls;
			}
			S.Kept.Remove(Key);
		}
	}
#if ASDA_PERF
	// Счётчики: настоящая синхронная загрузка — когда класса в памяти нет.
	const bool bInMemory = FindObject<UClass>(nullptr, *ClassPath) != nullptr;
	const uint64 Tick0 = AsdaPerf::Now();
	const double T0 = FPlatformTime::Seconds();
#endif
	UClass* Cls = LoadClass<AActor>(nullptr, *ClassPath);   // прежняя загрузка в момент показа (и страховка для предзагрузки)
	// Показ обогнал предзагрузку этого же класса: заявка больше не нужна (класс уже загружен синхронно) — снять её без зачёта.
	for (int32 i = S.InFlight.Num() - 1; i >= 0; --i)
	{
		if (S.InFlight[i].Key == Key)
		{
			if (S.InFlight[i].Handle.IsValid()) { S.InFlight[i].Handle->ReleaseHandle(); }
			S.InFlight.RemoveAtSwap(i);
			++S.Stats.PreloadsDropped;
		}
	}
	if (S.Queue.Remove(Key) > 0) { ++S.Stats.PreloadsDropped; }
#if ASDA_PERF
	if (!bInMemory && Cls)
	{
		static FAsdaPerfStat& LoadStat = AsdaPerf::Register(TEXT("Fx.ClassSyncLoad"));
		if (AsdaPerf::GCollect) { LoadStat.Add(AsdaPerf::Now() - Tick0); }
		const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
		++S.Stats.SyncLoads;
		S.Stats.SyncMs += Ms;
		S.Stats.MaxSyncMs = FMath::Max(S.Stats.MaxSyncMs, Ms);
		if (Ms > 33.0) { ++S.Stats.Over33; ASDA_PERF_VALUE("Fx.ClassSyncLoadOver33", 1); }
		if (S.EverLoaded.Contains(Key)) { ++S.Stats.Reloads; ASDA_PERF_VALUE("Fx.ClassReload", 1); }
	}
	else if (bInMemory)
	{
		NoteShown(S, Key);
	}
	if (Cls) { S.EverLoaded.Add(Key); }   // любой показанный класс: если его выгрузят и загрузят снова — это «загружен заново»
#else
	NoteShown(S, Key);
#endif
	if (Cls) { Keep(S, Key, Cls); }
	return Cls;
}

void AsdaFxClassCache::Preload(TConstArrayView<FString> ClassPaths)
{
	if (IsEngineExitRequested()) { return; }
	FState& S = State();
	for (const FString& Path : ClassPaths)
	{
		if (Path.IsEmpty()) { continue; }
		const FName Key(*Path);
		if (S.Kept.Contains(Key)) { continue; }
		if (S.Missing.Contains(Key)) { continue; }
		if (S.Queue.Contains(Key) || S.InFlight.ContainsByPredicate([&Key](const FFlight& F) { return F.Key == Key; })) { continue; }
		if (UClass* Cls = FindObject<UClass>(nullptr, *Path))
		{
			// Уже в памяти (жив экземпляр либо сборщик ещё не дошёл): грузить нечего — только не дать выгрузить до показа.
			if (IsFxClass(Cls)) { Keep(S, Key, Cls); }
			continue;
		}
		if (S.Queue.Num() >= kMaxQueued) { ++S.Stats.PreloadsDropped; continue; }
		S.Queue.Add(Key);
	}
	Pump(S);
}

void AsdaFxClassCache::PreloadJebs(TConstArrayView<FString> Jebs)
{
	TArray<FString> Paths;
	for (const FString& Jeb : Jebs)
	{
		if (!Jeb.IsEmpty()) { Paths.AddUnique(AsdaAssetPaths::FxClassPathFromJeb(Jeb)); }
	}
	Preload(Paths);
}

bool AsdaFxClassCache::IsInMemory(const FString& ClassPath)
{
	return FindObject<UClass>(nullptr, *ClassPath) != nullptr;
}

AsdaFxClassCache::FStats AsdaFxClassCache::GetStats()
{
	if (!GState && IsEngineExitRequested()) { return FStats(); }   // после выхода состояние не создаётся заново
	FState& S = State();
	FStats Out = S.Stats;
	Out.Kept = S.Kept.Num();
	Out.InFlight = S.InFlight.Num();
	Out.Queued = S.Queue.Num();
	Out.Limit = S.Limit;
	return Out;
}

void AsdaFxClassCache::DebugReset(bool bDropAll)
{
	if (!GState && IsEngineExitRequested()) { return; }
	FState& S = State();
	if (bDropAll)
	{
		CancelAll(S);
		S.Kept.Reset();
		S.FromPreload.Reset();
		S.EverLoaded.Reset();
		S.Missing.Reset();
	}
	S.Stats = FStats();
}

void AsdaFxClassCache::DebugSetLimit(int32 Limit)
{
	if (!GState && IsEngineExitRequested()) { return; }
	FState& S = State();
	S.Limit = Limit > 0 ? Limit : kMaxKept;
	Trim(S);
}

void AsdaFxClassCache::CancelPreloads()
{
	if (GState) { CancelAll(*GState); }
}
