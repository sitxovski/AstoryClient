// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4, доля a (15.11.2026) [ПОРТ]: САМОТЕСТ УДЕРЖАНИЯ И ПРЕДЗАГРУЗКИ КЛАССОВ ЭФФЕКТОВ — `asda.Perf.A.FxCacheSelfTest [quit]`,
//   и вывод счётчиков `asda.Perf.A.FxCacheStats [reset] [quit]` (строка ИТОГ — годится шагом пакетного прогона после сцены).
//
// ЧТО ДОКАЗЫВАЕТ (`AsdaFxClassCache.h`). Запись 11 (15.11.2026): ключи и прежний путь убраны, шаги «оба ключа 0» (причина
//   повторов, контроль ключа 0) сняты вместе с ними — они проверяли код, которого больше нет.
//   2. УДЕРЖАНИЕ: после сборки мусора класс остаётся в памяти, второй показ идёт без синхронной загрузки (взят из
//      удержанных), и это тот же объект класса, что даёт `LoadClass`.
//   3. ПРЕДЕЛ: при пределе 3 пятый класс вытесняет давно не использованные — удержано ровно 3.
//   4. ПРЕДЗАГРУЗКА: заявка завершается, после неё показ идёт без синхронной загрузки и даёт тот же объект класса. Время
//      завершения печатается справкой; успевает ли она до первой фазы настоящего показа — видно по счётчикам сцены.
//   5. ОТМЕНА (то, что делает смена карты): идущих заявок не больше `kMaxInFlight`, после отмены — ни идущих, ни очереди.
//   6. СПРАВКА (без приговора): длительность синхронной загрузки в кадре, где идут две асинхронные заявки, и без них.
//      Замер грубый: второй раз тот же ассет читается уже с тёплого диска, а отменённые заявки загрузчик может догружать.
// Только офлайн и только отдельной игрой (сборка мусора должна выгрузить классы самотеста; в игре из редактора (PIE) классы
//   не выгружаются — там самотест откажет). В Shipping файла нет.
#include "CoreMinimal.h"

#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "AsdaFxClassCache.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Resources/AsdaAssetPaths.h"
#include "Test/AsdaSelfTestKit.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFxCacheTest, Log, All);

namespace AsdaFxCacheTest
{
	static const TCHAR* kCmd = TEXT("asda.Perf.A.FxCacheSelfTest");     // имя команды самотеста
	static const TCHAR* kStats = TEXT("asda.Perf.A.FxCacheStats");      // имя команды счётчиков

	// Эффекты самотеста (базы `.jeb`): удар и снаряд навыка 701 и ещё шесть эффектов ударов разных навыков.
	static const TCHAR* const kJebs[] = {
		TEXT("eff_1_char_99_tshotb03-a_00"), TEXT("eff_1_char_99_tshots02-a_00"), TEXT("eff_1_char_06_dakhlpb03-a_00"),
		TEXT("eff_1_char_99_ebateb03-a_00"), TEXT("eff_1_char_99_hfireb03-a_00"), TEXT("eff_1_char_99_crashb02-a_00"),
		TEXT("eff_1_char_02_fatale01-a_00"), TEXT("eff_1_char_07_runfire01-a_00") };

	// Полный путь класса эффекта номер `I`.
	static FString Path(int32 I) { return AsdaAssetPaths::FxClassPathFromJeb(kJebs[I]); }

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> World;     // мир
		bool bQuit = false;               // выйти в конце
		int32 Ok = 0, Fail = 0;           // счёт проверок
		int32 Step = 0;                   // номер шага
		uint64 LastFrame = MAX_uint64;    // кадр прошлого шага
		double WaitFromS = 0.0;           // начало ожидания предзагрузки, с
		uint64 WaitFrame = 0;             // кадр начала ожидания предзагрузки
	};

	// Одна проверка.
	static void Chk(FCtx& C, bool bOk, const TCHAR* Name, const FString& Detail)
	{
		bOk ? ++C.Ok : ++C.Fail;
		UE_LOG(LogAsdaFxCacheTest, Log, TEXT("ЭФФЕКТ-КЛАССЫ %s %s — %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), Name, *Detail);
	}

	// Отпустить всё удержанное и собрать мусор: классы эффектов без ссылок уходят из памяти.
	static void Fresh()
	{
		AsdaFxClassCache::DebugReset(/*bDropAll=*/true);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge=*/true);
	}

	// Длительность одного `Load`, мс.
	static double TimedLoad(int32 I, UClass*& Out)
	{
		const double T0 = FPlatformTime::Seconds();
		Out = AsdaFxClassCache::Load(Path(I));
		return (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	// Шаг по кадру. false — тикер снят.
	static bool StepFn(const TSharedRef<FCtx>& C)
	{
		if (C->LastFrame == GFrameCounter) { return true; }
		C->LastFrame = GFrameCounter;
		using namespace AsdaFxClassCache;
		switch (C->Step)
		{
		case 0:   // 2. удержание и 3. предел
		{
			Fresh();
			// Классы самотеста держит живой эффект (самотест пущен в одной игре после сцены с навыком 701) — выгрузить их нельзя,
			//   проверять нечего: отказ с причиной, а не провал.
			if (IsInMemory(Path(0)) || IsInMemory(Path(1)))
			{
				AsdaSelfTestKit::Refused(C->World.Get(), kCmd, C->bQuit, TEXT("классы эффектов самотеста заняты живыми эффектами — запускать отдельной игрой"));
				return false;
			}
			UClass* A = Load(Path(0));
			const FString PathA = A ? A->GetPathName() : FString();
			A = nullptr;
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
			const bool bStays = IsInMemory(Path(0));
			UClass* B = Load(Path(0));
			const FStats S = GetStats();
			const UClass* Plain = LoadClass<AActor>(nullptr, *Path(0));   // то, что отдаёт прежний код (класс в памяти — загрузки нет)
			Chk(*C, bStays && B && S.SyncLoads == 1 && S.Reloads == 0 && S.KeptHits == 1 && !PathA.IsEmpty() && B == Plain,
				TEXT("2 удержание: второй показ после сборки мусора — без синхронной загрузки, класс тот же"),
				FString::Printf(TEXT("после сборки мусора в памяти: %d; синхронных загрузок %llu (ждём 1), заново %llu (ждём 0), из удержанных %llu (ждём 1); тот же объект класса, что у прежнего кода: %d"),
					bStays ? 1 : 0, S.SyncLoads, S.Reloads, S.KeptHits, (B && B == Plain) ? 1 : 0));
			DebugSetLimit(3);
			for (int32 I = 2; I <= 5; ++I) { Load(Path(I)); }
			const FStats L = GetStats();
			const bool bOldGone = Load(Path(0)) != nullptr && GetStats().KeptHits == S.KeptHits;   // первый класс вытеснен: показ не «из удержанных»
			Chk(*C, L.Kept == 3 && L.Evicted == 2 && L.Limit == 3 && bOldGone, TEXT("3 предел: давно не использованные вытеснены"),
				FString::Printf(TEXT("удержано %d (ждём 3), вытеснено %llu (ждём 2), самый старый класс уже не из удержанных: %d"), L.Kept, L.Evicted, bOldGone ? 1 : 0));
			DebugSetLimit(0);
			++C->Step;
			return true;
		}
		case 1:   // 4. запуск предзагрузки
		{
			Fresh();
			const FString P1 = Path(1);
			const bool bGone = !IsInMemory(P1);
			Preload(MakeArrayView(&P1, 1));
			const FStats S2 = GetStats();
			Chk(*C, bGone && S2.InFlight == 1 && S2.PreloadsStarted == 1, TEXT("4б предзагрузка начата"),
				FString::Printf(TEXT("до заявки в памяти нет: %d; идущих заявок %d (ждём 1)"), bGone ? 1 : 0, S2.InFlight));
			C->WaitFromS = FPlatformTime::Seconds();
			C->WaitFrame = GFrameCounter;
			++C->Step;
			return true;
		}
		case 2:   // 4. ждём завершения предзагрузки
		{
			const FStats S = GetStats();
			const double Waited = FPlatformTime::Seconds() - C->WaitFromS;
			if (S.PreloadsDone == 0 && S.InFlight > 0 && Waited < 5.0) { return true; }
			// Приговор — «завершена» (в пределах 5 с). Время — справкой: оно зависит от длины кадра (загрузчик шагает по кадрам),
			//   а ассет здесь может быть тёплым (читался прошлым прогоном). Успевает ли предзагрузка до первой фазы настоящего показа —
			//   видно по счётчикам сцены (`asda.Perf.A.FxCacheStats`: показы из предзагруженных против синхронных загрузок).
			Chk(*C, S.PreloadsDone == 1 && S.InFlight == 0, TEXT("4в предзагрузка завершена"),
				FString::Printf(TEXT("завершено %llu (ждём 1), идущих заявок %d; справка: за %.3f с, кадров %llu"), S.PreloadsDone, S.InFlight, Waited, GFrameCounter - C->WaitFrame));
			UClass* A = Load(Path(1));
			const FStats S1 = GetStats();
			const UClass* Plain = LoadClass<AActor>(nullptr, *Path(1));   // то, что отдаёт прежний код (класс в памяти — загрузки нет)
			Chk(*C, A && S1.SyncLoads == 0 && S1.PreloadHits == 1 && A == Plain, TEXT("4г показ после предзагрузки — без синхронной загрузки, класс тот же"),
				FString::Printf(TEXT("синхронных загрузок %llu (ждём 0), показов из предзагруженных %llu (ждём 1), тот же объект класса, что у прежнего кода: %d"),
					S1.SyncLoads, S1.PreloadHits, (A && A == Plain) ? 1 : 0));
			++C->Step;
			return true;
		}
		case 3:   // 5. отмена заявок
		{
			Fresh();
			TArray<FString> Six;
			for (int32 I = 2; I < 8; ++I) { Six.Add(Path(I)); }
			Preload(Six);
			const FStats S0 = GetStats();
			CancelPreloads();
			const FStats S1 = GetStats();
			Chk(*C, S0.InFlight == kMaxInFlight && S0.Queued == 6 - kMaxInFlight && S1.InFlight == 0 && S1.Queued == 0 && S1.PreloadsDropped == 6,
				TEXT("5 отмена: порция не больше предела, после отмены заявок и очереди нет"),
				FString::Printf(TEXT("идущих %d (ждём %d), в очереди %d (ждём %d); после отмены идущих %d, в очереди %d, снято %llu (ждём 6)"),
					S0.InFlight, kMaxInFlight, S0.Queued, 6 - kMaxInFlight, S1.InFlight, S1.Queued, S1.PreloadsDropped));
			++C->Step;
			return true;
		}
		case 4:   // 6. справка: синхронная загрузка рядом с двумя асинхронными заявками
		{
			double WithMs[2] = { 0.0, 0.0 }, AloneMs[2] = { 0.0, 0.0 };
			for (int32 R = 0; R < 2; ++R)
			{
				UClass* X = nullptr;
				Fresh();
				const FString Two[2] = { Path(2), Path(3) };
				Preload(MakeArrayView(Two, 2));
				WithMs[R] = TimedLoad(4 + R, X);
				X = nullptr;
				Fresh();
				AloneMs[R] = TimedLoad(4 + R, X);
			}
			UE_LOG(LogAsdaFxCacheTest, Log, TEXT("ЭФФЕКТ-КЛАССЫ инфо 6 синхронная загрузка в кадре с двумя идущими заявками: %.1f и %.1f мс; та же загрузка без заявок (следом, ассет уже читался с диска): %.1f и %.1f мс"),
				WithMs[0], WithMs[1], AloneMs[0], AloneMs[1]);
			AsdaFxClassCache::DebugReset(true);
			AsdaSelfTestKit::Finish(C->World.Get(), kCmd, C->bQuit, LogAsdaFxCacheTest, TEXT("ЭФФЕКТ-КЛАССЫ"), C->Ok, C->Fail);
			return false;
		}
		default:
			return false;
		}
	}

	// Команда самотеста.
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }   // только офлайн: самотест собирает мусор и отпускает удержанные классы
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		const TSharedRef<FCtx> C = MakeShared<FCtx>();
		C->World = W; C->bQuit = A.bQuit;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return StepFn(C); }), 0.f);
	}

	// Команда счётчиков: строка состояния и строка ИТОГ (ok=1), `reset` — обнулить счётчики после вывода.
	static void Stats(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		const AsdaFxClassCache::FStats S = AsdaFxClassCache::GetStats();
		UE_LOG(LogAsdaFxCacheTest, Log, TEXT("ЭФФЕКТ-КЛАССЫ СЧЁТ синхронных загрузок %llu, сумма %.1f мс, самая долгая %.1f мс, дольше 33 мс %llu, ")
			TEXT("загружено заново %llu | из удержанных %llu, из предзагруженных %llu | предзагрузок начато %llu, завершено %llu, снято %llu | ")
			TEXT("удержано %d из %d, вытеснено %llu, идущих заявок %d, в очереди %d"),
			S.SyncLoads, S.SyncMs, S.MaxSyncMs, S.Over33, S.Reloads, S.KeptHits, S.PreloadHits,
			S.PreloadsStarted, S.PreloadsDone, S.PreloadsDropped, S.Kept, S.Limit, S.Evicted, S.InFlight, S.Queued);
		if (A.Has(TEXT("reset"))) { AsdaFxClassCache::DebugReset(false); }
		AsdaSelfTestKit::Finish(W, kStats, A.bQuit, LogAsdaFxCacheTest, TEXT("ЭФФЕКТ-КЛАССЫ СЧЁТ"), 1, 0);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxCacheSelfTest(
	TEXT("asda.Perf.A.FxCacheSelfTest"),
	TEXT("[ПОРТ] R4: самотест удержания и предзагрузки классов эффектов: удержанный класс переживает сборку мусора, предел, предзагрузка, отмена заявок."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaFxCacheTest::Run(A, W); }));

// Консольная команда счётчиков.
static FAutoConsoleCommandWithWorldAndArgs GAsdaFxCacheStats(
	TEXT("asda.Perf.A.FxCacheStats"),
	TEXT("[ПОРТ] R4: счётчики загрузок классов эффектов в журнал (синхронные загрузки, самая долгая, дольше 33 мс, загружено заново, показы из удержанных и предзагруженных). [reset] [quit]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaFxCacheTest::Stats(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
