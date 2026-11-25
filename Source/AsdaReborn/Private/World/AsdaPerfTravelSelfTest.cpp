// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ-ЗАМЕР «КАДР ДО/ПОСЛЕ ДАНЖА» (05.11.2026, жалоба игроков): `asda.Perf.TravelSelfTest [trips=3] [warm=3] [meas=6]
//   [route=<карта:x:y,...>] [mobs=N] [profilegpu] [noexpect] [quit]`. [ПОРТ]
//   `mobs=N` — N мобов данжа (4013 штатным приёмом) вокруг точки прибытия на каждой карте, кроме Silaris: живой данж полон мобов.
//   Жалоба: «в Castle of Chess кадр падает с 70–80 до 40–50 FPS, и низкий кадр ОСТАЁТСЯ после выхода обратно в Silaris».
//   Путь игрока (закон diagnostic-must-use-real-path): каждая смена карты — `UAsdaLoadManagerSubsystem::BeginMapLoad` (тот же вызов,
//   что отложенная загрузка по порталу/6004/4055) → L_Loading → целевая карта; онлайн-ожидание 4003 — крюк `bDebugHoldScreenAsOnline`,
//   сам 4003 — `UAsdaWorldSubsystem::DebugSpawnSelfAt` в точке прибытия портала (PortalConfig сервера: Sillaris → Castle_of_Chess
//   STARTPOS 48,460; Castle_of_Chess → Sillaris 368,33). Шаги — по тикеру ядра (таймеры мира умирают с миром).
//   Маршрут по умолчанию: silalis(368,33) → [chess(48,460) → chess(453,45) → silalis(368,33)] × trips → t_castle_of_chess(48,460) →
//   silalis(368,33). На каждом отрезке: принудительная сборка мусора, прогрев `warm` с, замер `meas` с — медианы/p95 кадра (стенное
//   время между тиками), GPU (`RHIGetGPUFrameCycles`), игрового потока (`GGameThreadTime`), рисующего (`GRenderThreadTime`);
//   счётчики: объекты UObject, миры, акторы и компоненты текущего мира, тикающие акторы/компоненты, акторы/компоненты/виджеты ЧУЖИХ
//   миров (утечка через травел), виджеты всего/во вьюпорте, источники света (видимые / с тенью), FX-компоненты активные, звук играющий,
//   рендер-цели, память процесса, уровень эффект-светов (`UAsdaFxLightSubsystem::CurrentEffectsLevel`).
//   Сверка (не `noexpect`): каждый повторный заход против ПЕРВОГО захода на ту же карту в ту же точку (главное — Silaris после данжа
//   против первого Silaris) — кадр в пределах шума (≤ max(1.5 мс, 12 %)), чужих миров/
//   акторов/виджетов нет, рост объектов ≤ 3 %, ни одна консольная переменная не отличается (список отличий печатается целиком;
//   переменная, зарегистрированная движком после базового отрезка, отличием не считается — R5-fix4),
//   уровень эффект-светов вернулся к уровню игрока. Печатаются 25 классов, чьё число объектов выросло больше всего.
//   `profilegpu` — `ProfileGPU` на каждом отрезке (дерево проходов в лог; async-проходы им раздуваются — см. perf2k).
//   Итог: «ПЕРФ-ТРАВЕЛ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaTestPackets.h"                       // запись 4013 (появление моба) — как у самотеста пачки мобов
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapTable.h"                    // FAsdaMapRow::Level — уровень карты первой точки маршрута (D1)
#include "Misc/PackageName.h"                      // FPackageName::GetShortName — имя уровня из пути пакета (D1)
#include "World/AsdaWorldKind.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaFxLightSubsystem.h"
#include "Npc/AsdaMobActor.h"                       // счёт мобов отрезка
#include "Blueprint/UserWidget.h"
#include "Components/AudioComponent.h"
#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Particles/ParticleSystemComponent.h"   // UFXSystemComponent — база Niagara и Cascade
#include "Engine/TextureRenderTarget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "RenderTimer.h"                         // GGameThreadTime / GRenderThreadTime
#include "RHI.h"                                 // RHIGetGPUFrameCycles
#include "RHIStats.h"                            // FTextureMemoryStats — память текстур (утечка ресурсов через травел)
#include "DynamicRHI.h"                          // RHIGetTextureMemoryStats
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPerfTravelTest, Log, All);

// ★ 11.11.2026 [ПОРТ] (D1, «+632 StaticMesh за переход»): самотест считал, что игра уже стоит на карте первой точки маршрута
//   (Silaris), и брал первый замер как базу сверки «Silaris после данжа против первого Silaris». Пакетный запуск по умолчанию
//   поднимает мир `alpen`: база снималась с ДРУГОЙ карты (679 акторов, 4387 компонентов против 487 и 4759 у Silaris), и разница
//   наборов мешей двух карт (+632 StaticMesh, +632 InstancedStaticMeshComponent) читалась как утечка при переходе. Утечки нет:
//   после первого захода в Silaris число объектов стоит на месте. 1 — если мир старта не уровень первой точки, самотест сам
//   переходит на неё штатным BeginMapLoad и только потом снимает базу; 0 — как было (первая точка = текущий мир без проверки).
static ASDA_DEV_CVAR(int32, CVarAsdaPerfTravelStartOnRoute, TEXT("asda.Perf.TravelStartOnRoute"), 1,
	TEXT("[ПОРТ] Самотест переходов: 1 — при старте не на карте первой точки маршрута сначала перейти на неё (база сверки снимается "
	     "с той же карты, что и сравниваемые заходы); 0 — прежнее: первая точка считается текущим миром без проверки."),
	ECVF_Default);

namespace AsdaPerfTravelTest
{
	static const TCHAR* kCmd = TEXT("asda.Perf.TravelSelfTest");   // имя команды
	static constexpr uint16 kMyUserNum = 321;                        // свой userNum на прогон (как у самотеста затемнения)

	// Точка маршрута: карта и тайл прибытия.
	struct FStop
	{
		int32 Map = 0;   // номер карты (MapInfo)
		int32 X = 0;     // тайл X прибытия
		int32 Y = 0;     // тайл Y (Z сервера) прибытия
	};

	// Замер одного отрезка.
	struct FSeg
	{
		FStop Stop;                                   // где мерили
		FString MapName;                              // имя мира
		float FrameMed = 0.f, FrameP95 = 0.f;         // кадр, мс
		float GpuMed = 0.f, GtMed = 0.f, RtMed = 0.f; // GPU / игровой / рисующий, мс
		int32 Objects = 0, Worlds = 0;                // объекты UObject, живых миров (не шаблонов)
		int32 Actors = 0, Comps = 0;                  // акторы/компоненты своего мира
		int32 TickActors = 0, TickComps = 0;          // зарегистрированные тики своего мира
		int32 ForeignActors = 0, ForeignComps = 0;    // акторы/компоненты чужих миров (не шаблонов)
		int32 Widgets = 0, WidgetsInVp = 0, ForeignWidgets = 0; // виджеты UUserWidget: всего / во вьюпорте / чужого мира
		int32 Lights = 0, LightsVis = 0, LightsShadow = 0;      // свет своего мира: всего / видимые / видимые с тенью
		int32 FxActive = 0, AudioPlaying = 0, RTs = 0;          // активные FX-компоненты, играющий звук, рендер-цели
		int32 FxLightLevel = -1;                      // уровень эффект-светов на отрезке
		int32 MobActors = 0;                          // акторов мобов своего мира
		uint64 MemMB = 0;                             // физическая память процесса, МБ
		int64 TexStreamMB = 0, TexNonStreamMB = 0;    // память текстур RHI: стримящихся / прочих, МБ
		TMap<FName, int32> ClassCount;                // число объектов по классу (без шаблонов)
		TMap<FString, FString> CVars;                 // снимок всех консольных переменных
		TMap<FString, int32> LightKinds;              // видимый свет: «класс света / класс владельца [тень]» → число
	};

	// Контекст прогона (живёт между шагами тикера, переживает смену мира).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                       // счёт
		bool bQuit = false;                           // выйти после итога
		bool bExpect = true;                          // сверять Silaris до/после
		bool bProfileGpu = false;                     // ProfileGPU на каждом отрезке
		bool bRegion = false;                         // `region`: окно замера — область трассы Insights `perf_<NN>_<карта>`
		FString RegionName;                           // открытая область трассы
		float WarmSec = 3.f, MeasSec = 6.f;           // прогрев / замер, с
		TArray<FStop> Route;                          // маршрут
		int32 Idx = 0;                                // текущая точка
		bool bStartOnRoute = true;                    // мир старта — уровень первой точки маршрута (иначе на неё сначала переходим)
		int32 Stage = 0;                              // этап точки
		double T0 = 0.0;                              // начало этапа
		double LastTick = 0.0;                        // прошлый тик (кадр)
		TArray<float> SFrame, SGpu, SGt, SRt;         // выборки отрезка
		TArray<FSeg> Segs;                            // готовые отрезки
		TWeakObjectPtr<UGameInstance> GI;             // игра
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой userNum — вернуть в конце
		bool bSpread = false;                         // `spread`: мобы равномерно по всей карте, а не кольцами вокруг точки
		int32 Mobs = 0;                               // `mobs=N`: мобов данжа вокруг точки прибытия (4013 штатным приёмом) на картах ≠ Silaris
		int32 NextRid = 20000;                        // следующий rid эмулируемого моба (rid в 4013 — i16: не выше 32767)
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaPerfTravelTest, Error, TEXT("ПЕРФ-ТРАВЕЛ FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Медиана/квантиль выборки (копия сортируется).
	static float Quant(TArray<float> V, float Q)
	{
		if (V.Num() == 0) { return 0.f; }
		V.Sort();
		return V[FMath::Clamp((int32)(Q * (V.Num() - 1) + 0.5f), 0, V.Num() - 1)];
	}

	// Объект — шаблон (CDO/архетип) или уже мусор: в счётчики не идёт.
	static bool IsTemplateOrDead(const UObject* O)
	{
		return !O || O->IsTemplate() || !IsValid(O);
	}

	// Мобы данжа вокруг точки прибытия: 4013 штатным приёмом (`DebugFeedPacket` → тот же HandlePacket, что у сокета). Типы — группа
	//   castle_of_chess из живого журнала 26.10 (Knight 314, Pawn 298, Bishop 306); сетка 2 тайла вокруг точки, по одной записи на пакет.
	static void FeedMobs(const FCtxRef& C, UGameInstance* GI, const FStop& Stop)
	{
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		if (!Net || C->Mobs <= 0 || Stop.Map == 0) { return; }
		static constexpr int32 kTypes[3] = { 314, 298, 306 };   // Knight / Pawn / Bishop
		int32 Fed = 0;
		// Приём 4013 — только «в игре» (как у самотеста пачки мобов); флаг возвращается сразу после подачи. Отправки офлайн-гейт режет сам.
		const bool bWasInGame = AsdaTestNet::bInGameConnected(Net);
		AsdaTestNet::bInGameConnected(Net) = true;
		// `spread`: мобы по всей карте равномерной сеткой, как сервер с сектором 256 шлёт живую карту (замер дальности обзора).
		if (C->bSpread)
		{
			const int32 Side = FMath::Max(2, FMath::CeilToInt(FMath::Sqrt((float)C->Mobs)));
			for (int32 i = 0; i < C->Mobs; ++i)
			{
				const FIntPoint T(8 + (i % Side) * 496 / Side, 8 + (i / Side) * 496 / Side);
				AsdaTestNet::DebugFeedPacket(Net, 4013, AsdaTestPackets::MobSpawn4013(C->NextRid++, kTypes[Fed % 3], T, /*State*/1, /*Speed*/1666));
				++Fed;
			}
		}
		for (int32 R = 1; Fed < C->Mobs && R < 20; ++R)
		{
			for (int32 Dx = -R; Dx <= R && Fed < C->Mobs; ++Dx)
			{
				for (int32 Dy = -R; Dy <= R && Fed < C->Mobs; ++Dy)
				{
					if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != R) { continue; }   // только кольцо радиуса R
					if ((Dx & 1) || (Dy & 1)) { continue; }                             // шаг сетки 2 тайла
					const FIntPoint T(FMath::Clamp(Stop.X + Dx, 1, 510), FMath::Clamp(Stop.Y + Dy, 1, 510));
					AsdaTestNet::DebugFeedPacket(Net, 4013, AsdaTestPackets::MobSpawn4013(C->NextRid++, kTypes[Fed % 3], T, /*State*/1, /*Speed*/1666));
					++Fed;
				}
			}
		}
		AsdaTestNet::bInGameConnected(Net) = bWasInGame;
		UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ: подано мобов %d вокруг %d,%d"), Fed, Stop.X, Stop.Y);
	}

	// Снять счётчики и снимки в мире W.
	static void Snapshot(FSeg& S, UWorld* W)
	{
		S.MapName = W ? W->GetMapName() : FString();
		S.Objects = GUObjectArray.GetObjectArrayNumMinusAvailable();
		for (TObjectIterator<UObject> It; It; ++It)
		{
			if (IsTemplateOrDead(*It)) { continue; }
			S.ClassCount.FindOrAdd(It->GetClass()->GetFName())++;
		}
		for (TObjectIterator<UWorld> It; It; ++It)
		{
			if (!IsTemplateOrDead(*It)) { ++S.Worlds; }
		}
		for (TObjectIterator<AActor> It; It; ++It)
		{
			AActor* A = *It;
			if (IsTemplateOrDead(A)) { continue; }
			if (A->GetWorld() == W)
			{
				++S.Actors;
				if (A->IsA<AAsdaMobActor>()) { ++S.MobActors; }
				if (A->IsActorTickEnabled() && A->PrimaryActorTick.IsTickFunctionRegistered()) { ++S.TickActors; }
			}
			else if (A->GetWorld()) { ++S.ForeignActors; }
		}
		for (TObjectIterator<UActorComponent> It; It; ++It)
		{
			UActorComponent* C = *It;
			if (IsTemplateOrDead(C)) { continue; }
			const UWorld* CW = C->GetWorld();
			if (CW == W)
			{
				++S.Comps;
				if (C->IsComponentTickEnabled() && C->PrimaryComponentTick.IsTickFunctionRegistered()) { ++S.TickComps; }
				if (const ULightComponent* L = Cast<ULightComponent>(C))
				{
					++S.Lights;
					if (L->IsRegistered() && L->IsVisible() && L->Intensity > 0.f)
					{
						++S.LightsVis;
						if (L->CastShadows) { ++S.LightsShadow; }
						const AActor* Ow = L->GetOwner();
						S.LightKinds.FindOrAdd(FString::Printf(TEXT("%s / %s%s"), *L->GetClass()->GetName(), Ow ? *Ow->GetClass()->GetName() : TEXT("-"),
							L->CastShadows ? TEXT(" [тень]") : TEXT("")))++;
					}
				}
				else if (const UFXSystemComponent* F = Cast<UFXSystemComponent>(C))
				{
					if (F->IsRegistered() && F->IsActive()) { ++S.FxActive; }
				}
				else if (const UAudioComponent* Au = Cast<UAudioComponent>(C))
				{
					if (Au->IsPlaying()) { ++S.AudioPlaying; }
				}
			}
			else if (CW && C->IsRegistered()) { ++S.ForeignComps; }
		}
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* Wd = *It;
			if (IsTemplateOrDead(Wd)) { continue; }
			++S.Widgets;
			if (Wd->IsInViewport()) { ++S.WidgetsInVp; }
			const UWorld* WW = Wd->GetWorld();
			if (WW && WW != W) { ++S.ForeignWidgets; }
		}
		for (TObjectIterator<UTextureRenderTarget> It; It; ++It)
		{
			if (!IsTemplateOrDead(*It)) { ++S.RTs; }
		}
		S.FxLightLevel = UAsdaFxLightSubsystem::CurrentEffectsLevel();
		S.MemMB = FPlatformMemory::GetStats().UsedPhysical / (1024ull * 1024ull);
		{
			FTextureMemoryStats TS;
			RHIGetTextureMemoryStats(TS);
			S.TexStreamMB = (int64)(TS.StreamingMemorySize / (1024ull * 1024ull));
			S.TexNonStreamMB = (int64)(TS.NonStreamingMemorySize / (1024ull * 1024ull));
		}
		IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda(
			[&S](const TCHAR* Name, IConsoleObject* O)
			{
				if (IConsoleVariable* V = O ? O->AsVariable() : nullptr) { S.CVars.Add(Name, V->GetString()); }
			}), TEXT(""));
	}

	// Строка отрезка в лог.
	static void LogSeg(int32 N, const FSeg& S)
	{
		UE_LOG(LogAsdaPerfTravelTest, Log,
			TEXT("ПЕРФ-ТРАВЕЛ отрезок %02d карта=%d %s тайл=%d,%d | кадр мед %.2f p95 %.2f (%.1f FPS) GPU %.2f игровой %.2f рисующий %.2f | "
			     "мобов %d объектов %d миров %d акторов %d компонентов %d тиков акторов %d компонентов %d | чужие: акторы %d компоненты %d виджеты %d | "
			     "виджетов %d во вьюпорте %d | свет %d видимых %d с тенью %d | FX активных %d звук %d рендер-целей %d | уровень эф-светов %d | память %llu МБ, текстуры %lld + %lld МБ"),
			N, S.Stop.Map, *S.MapName, S.Stop.X, S.Stop.Y, S.FrameMed, S.FrameP95, S.FrameMed > 0.f ? 1000.f / S.FrameMed : 0.f, S.GpuMed, S.GtMed, S.RtMed,
			S.MobActors, S.Objects, S.Worlds, S.Actors, S.Comps, S.TickActors, S.TickComps, S.ForeignActors, S.ForeignComps, S.ForeignWidgets,
			S.Widgets, S.WidgetsInVp, S.Lights, S.LightsVis, S.LightsShadow, S.FxActive, S.AudioPlaying, S.RTs, S.FxLightLevel, S.MemMB, S.TexStreamMB, S.TexNonStreamMB);
	}

	// Разбивка видимого света отрезка (по убыванию).
	static void LogLights(int32 N, const FSeg& S)
	{
		TArray<TPair<FString, int32>> K;
		for (const TPair<FString, int32>& P : S.LightKinds) { K.Add(P); }
		K.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
		for (const TPair<FString, int32>& P : K) { UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ отрезок %02d: свет %s ×%d"), N, *P.Key, P.Value); }
	}

	// Сверка отрезка с базой — первым заходом на ту же карту в ту же точку (для Silaris — «Silaris после данжа против первого Silaris»).
	static void Compare(const FCtxRef& C, const FSeg& Base, const FSeg& S, int32 N)
	{
		// Отличия консольных переменных (все — списком).
		// ★ R5-fix4 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: переменная, которой в базовом снимке не было, отличием НЕ считается — она только
		//   печатается. Движок регистрирует часть переменных лениво, при первом обращении (`r.SkinCache.Visualize` —
		//   `GPUSkinCacheVisualizationData.cpp`, когда нужный меш впервые попал в кадр): такая появлялась между отрезками и давала
		//   красное «консольных переменных, отличных…, 1» без единой смены настройки. Изменённое значение существовавшей
		//   переменной считается по-прежнему.
		int32 Diff = 0, Fresh = 0;
		for (const TPair<FString, FString>& P : S.CVars)
		{
			const FString* B = Base.CVars.Find(P.Key);
			if (!B)
			{
				++Fresh;
				UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ отрезок %02d: переменная %s = «%s» зарегистрирована после базового отрезка — отличием не считается"),
					N, *P.Key, *P.Value);
			}
			else if (*B != P.Value)
			{
				++Diff;
				UE_LOG(LogAsdaPerfTravelTest, Warning, TEXT("ПЕРФ-ТРАВЕЛ отрезок %02d: переменная %s = «%s» (в базовом отрезке «%s»)"),
					N, *P.Key, *P.Value, **B);
			}
		}
		// Классы, чьё число объектов выросло больше всего.
		TArray<TPair<FName, int32>> Grow;
		for (const TPair<FName, int32>& P : S.ClassCount)
		{
			const int32 B = Base.ClassCount.FindRef(P.Key);
			if (P.Value > B) { Grow.Add(TPair<FName, int32>(P.Key, P.Value - B)); }
		}
		Grow.Sort([](const TPair<FName, int32>& A, const TPair<FName, int32>& B) { return A.Value > B.Value; });
		for (int32 i = 0; i < FMath::Min(25, Grow.Num()); ++i)
		{
			UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ отрезок %02d: рост класса %s +%d (было %d)"),
				N, *Grow[i].Key.ToString(), Grow[i].Value, Base.ClassCount.FindRef(Grow[i].Key));
		}
		if (!C->bExpect) { return; }
		const float Tol = FMath::Max(1.5f, Base.FrameMed * 0.12f);
		// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: время кадра сверяется вместе со временем потоков самой игры. Кадр — это ещё и
		//   ожидание процессора и вывода: при трёх играх разом медиана кадра прыгает на десятки процентов (журнал rx_r5fix3 [en],
		//   отрезок 03: кадр 54,74 против 38,22 мс при GPU 4,20/3,99, игровом 3,98/4,59, рисующем 4,64/5,01 — своя работа игры не
		//   выросла). Правило: отрезок красный, если кадр вышел за допуск И хотя бы один из потоков игры (GPU, игровой, рисующий)
		//   вышел за свой допуск по той же формуле. Допуск кадра и база прежние; замедление от самой игры (лишние акторы, тики,
		//   отрисовка) растит время своего потока и ловится, как раньше.
		//   ★ Ревизия: пол допуска потока — 0,5 мс (у кадра 1,5 мс): потоки на порядок короче кадра, пол 1,5 мс был бы для них 37 %.
		auto Over = [](float Now, float Was) { return Now > Was + FMath::Max(0.5f, Was * 0.12f); };   // значение вышло за допуск своей базы
		const bool bFrameOver = S.FrameMed > Base.FrameMed + Tol;
		const bool bThreadOver = Over(S.GpuMed, Base.GpuMed) || Over(S.GtMed, Base.GtMed) || Over(S.RtMed, Base.RtMed);
		if (bFrameOver && !bThreadOver)
		{
			UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ отрезок %02d: кадр вышел за допуск (%.2f против %.2f + %.2f), но время потоков игры в допуске — нагрузка машины, не игра"),
				N, S.FrameMed, Base.FrameMed, Tol);
		}
		C->Expect(!bFrameOver || !bThreadOver,
			FString::Printf(TEXT("отрезок %02d: кадр %s при повторном заходе %.2f мс против первого захода %.2f (допуск %.2f), GPU %.2f/%.2f, игровой %.2f/%.2f, рисующий %.2f/%.2f%s"),
				N, *S.MapName, S.FrameMed, Base.FrameMed, Tol, S.GpuMed, Base.GpuMed, S.GtMed, Base.GtMed, S.RtMed, Base.RtMed,
				(bFrameOver && !bThreadOver) ? TEXT(" — кадр вырос без роста потоков игры (нагрузка машины)") : TEXT("")));
		C->Expect(S.Worlds <= Base.Worlds && S.ForeignActors == 0 && S.ForeignComps == 0 && S.ForeignWidgets == 0,
			FString::Printf(TEXT("отрезок %02d: чужих миров/акторов/компонентов/виджетов нет (миров %d/%d, акторов %d, компонентов %d, виджетов %d)"),
				N, S.Worlds, Base.Worlds, S.ForeignActors, S.ForeignComps, S.ForeignWidgets));
		C->Expect(S.Objects <= Base.Objects * 103 / 100,
			FString::Printf(TEXT("отрезок %02d: объектов %d против %d (рост ≤ 3 %%)"), N, S.Objects, Base.Objects));
		C->Expect(S.TickComps <= Base.TickComps + 5 && S.TickActors <= Base.TickActors + 5,
			FString::Printf(TEXT("отрезок %02d: тиков компонентов %d/%d, акторов %d/%d"), N, S.TickComps, Base.TickComps, S.TickActors, Base.TickActors));
		C->Expect(S.LightsVis <= Base.LightsVis + 2 && S.LightsShadow <= Base.LightsShadow,
			FString::Printf(TEXT("отрезок %02d: видимого света %d/%d, с тенью %d/%d"), N, S.LightsVis, Base.LightsVis, S.LightsShadow, Base.LightsShadow));
		C->Expect(S.TexNonStreamMB <= Base.TexNonStreamMB + 64,
			FString::Printf(TEXT("отрезок %02d: память нестримящихся текстур %lld МБ против %lld (рост ≤ 64 МБ)"), N, S.TexNonStreamMB, Base.TexNonStreamMB));
		C->Expect(S.FxLightLevel == Base.FxLightLevel,
			FString::Printf(TEXT("отрезок %02d: уровень эффект-светов %d = первому заходу %d"), N, S.FxLightLevel, Base.FxLightLevel));
		C->Expect(Diff == 0, FString::Printf(TEXT("отрезок %02d: консольных переменных, отличных от первого захода, %d (новых, не считаются: %d)"), N, Diff, Fresh));
	}

	static void Finish(const FCtxRef& C)
	{
		UGameInstance* GI = C->GI.Get();
		if (UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr) { LM->bDebugHoldScreenAsOnline = false; }
		// Свод: по карте — средние медиан.
		TMap<int32, TArray<float>> ByMap;
		for (const FSeg& S : C->Segs) { ByMap.FindOrAdd(S.Stop.Map).Add(S.FrameMed); }
		for (const TPair<int32, TArray<float>>& P : ByMap)
		{
			FString L;
			for (float F : P.Value) { L += FString::Printf(TEXT(" %.2f"), F); }
			UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ свод карта %d: кадры по заходам (мс):%s"), P.Key, *L);
		}
		C->NetScope.Reset();
		AsdaSelfTestKit::Finish(GI ? GI->GetWorld() : nullptr, kCmd, C->bQuit, LogAsdaPerfTravelTest, TEXT("ПЕРФ-ТРАВЕЛ"), C->Ok, C->Fail);
	}

	// Шаг тикера: true — тикать дальше.
	static bool Step(const FCtxRef& C)
	{
		UGameInstance* GI = C->GI.Get();
		if (!GI) { return false; }
		UWorld* W = GI->GetWorld();
		UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
		UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		if (!LM || !WS) { return false; }
		const double Now = FPlatformTime::Seconds();
		const double Dt = Now - C->T0;
		const FStop& Stop = C->Route[C->Idx];
		switch (C->Stage)
		{
		case 0:   // переход: на первой точке — уже на карте; иначе BeginMapLoad
			// ★ 11.11.2026 [ПОРТ] (D1): первая точка — «уже на карте», только если мир старта и есть её уровень (`bStartOnRoute`);
			//   при ключе 0 флаг всегда true — прежнее поведение.
			if ((C->Idx == 0 && C->bStartOnRoute) || (C->Idx > 0 && LM->CurrentMapId == Stop.Map && W && !AsdaWorldKind::IsLoadingMap(W)))
			{
				if (AsdaSelfTestKit::NeedSelfSpawn(W) || C->Idx > 0) { WS->DebugSpawnSelfAt(Stop.X, Stop.Y); }
				FeedMobs(C, GI, Stop);
				C->Stage = 3; C->T0 = Now;
				return true;
			}
			LM->bDebugHoldScreenAsOnline = true;
			UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ точка %02d: BeginMapLoad(%d)"), C->Idx, Stop.Map);
			LM->BeginMapLoad(Stop.Map);
			C->Stage = 1; C->T0 = Now;
			return true;
		case 1:   // ждём целевую карту
			if (W && !AsdaWorldKind::IsLoadingMap(W) && W->HasBegunPlay() && Dt > 0.5)
			{
				C->Stage = 2; C->T0 = Now;
				return true;
			}
			if (Dt > 120.0) { C->Expect(false, FString::Printf(TEXT("точка %02d: карта %d не началась за 120 с"), C->Idx, Stop.Map)); Finish(C); return false; }
			return true;
		case 2:   // 1 с на целевой карте до «4003», затем спавн в точке прибытия
			if (Dt < 1.0) { return true; }
			UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ точка %02d: эмуляция 4003 на %s (%d,%d)"), C->Idx, W ? *W->GetMapName() : TEXT("?"), Stop.X, Stop.Y);
			WS->DebugSpawnSelfAt(Stop.X, Stop.Y);
			FeedMobs(C, GI, Stop);
			C->Stage = 3; C->T0 = Now;
			return true;
		case 3:   // экран загрузки уходит (или 10 с), затем сборка мусора и прогрев
			if (LM->GetScreenFadeState() != UAsdaLoadManagerSubsystem::EAsdaScreenFade::None && Dt < 10.0) { return true; }
			LM->bDebugHoldScreenAsOnline = false;
			if (GEngine) { GEngine->ForceGarbageCollection(true); }
			C->Stage = 4; C->T0 = Now;
			return true;
		case 4:   // прогрев
			if (Dt < C->WarmSec) { return true; }
			C->SFrame.Reset(); C->SGpu.Reset(); C->SGt.Reset(); C->SRt.Reset();
			C->LastTick = Now;
			if (C->bProfileGpu && GEngine && W) { GEngine->Exec(W, TEXT("ProfileGPU")); }
			if (C->bRegion && GEngine && W)
			{
				C->RegionName = FString::Printf(TEXT("perf_%02d_%d"), C->Segs.Num(), Stop.Map);
				GEngine->Exec(W, *FString::Printf(TEXT("Trace.RegionBegin %s"), *C->RegionName));
			}
			C->Stage = 5; C->T0 = Now;
			return true;
		case 5:   // замер
		{
			C->SFrame.Add((float)((Now - C->LastTick) * 1000.0));
			C->SGpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
			C->SGt.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
			C->SRt.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
			C->LastTick = Now;
			if (Dt < C->MeasSec) { return true; }
			if (!C->RegionName.IsEmpty() && GEngine && W) { GEngine->Exec(W, *FString::Printf(TEXT("Trace.RegionEnd %s"), *C->RegionName)); C->RegionName.Reset(); }
			FSeg S;
			S.Stop = Stop;
			S.FrameMed = Quant(C->SFrame, 0.5f); S.FrameP95 = Quant(C->SFrame, 0.95f);
			S.GpuMed = Quant(C->SGpu, 0.5f); S.GtMed = Quant(C->SGt, 0.5f); S.RtMed = Quant(C->SRt, 0.5f);
			Snapshot(S, W);
			const int32 N = C->Segs.Num();
			LogSeg(N, S);
			LogLights(N, S);
			// Первый отрезок на той же карте и в той же точке — база сверки.
			for (int32 i = 0; i < N; ++i)
			{
				const FSeg& B = C->Segs[i];
				if (B.Stop.Map == S.Stop.Map && B.Stop.X == S.Stop.X && B.Stop.Y == S.Stop.Y) { Compare(C, B, S, N); break; }
			}
			C->Segs.Add(MoveTemp(S));
			if (++C->Idx >= C->Route.Num()) { Finish(C); return false; }
			C->Stage = 0; C->T0 = Now;
			return true;
		}
		default:
			return false;
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 3600.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		if (!Net || !WS || !LM) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->GI = GI; C->bQuit = KA.bQuit; C->bExpect = !KA.Has(TEXT("noexpect")); C->bProfileGpu = KA.Has(TEXT("profilegpu")); C->bRegion = KA.Has(TEXT("region"));
		int32 Trips = 3;
		FString RouteArg;
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("trips="))) { Trips = FMath::Clamp(FCString::Atoi(*P.Mid(6)), 1, 10); }
			if (P.StartsWith(TEXT("warm=")))  { C->WarmSec = FMath::Max(0.5f, FCString::Atof(*P.Mid(5))); }
			if (P.StartsWith(TEXT("meas=")))  { C->MeasSec = FMath::Max(1.f, FCString::Atof(*P.Mid(5))); }
			if (P.StartsWith(TEXT("route="))) { RouteArg = P.Mid(6); }
			if (P.StartsWith(TEXT("mobs=")))  { C->Mobs = FMath::Clamp(FCString::Atoi(*P.Mid(5)), 0, 400); }
			if (P == TEXT("spread"))          { C->bSpread = true; }
		}
		if (!RouteArg.IsEmpty())
		{
			TArray<FString> Parts;
			RouteArg.ParseIntoArray(Parts, TEXT(","));
			for (const FString& Pt : Parts)
			{
				TArray<FString> F;
				Pt.ParseIntoArray(F, TEXT(":"));
				if (F.Num() == 3) { C->Route.Add({ FCString::Atoi(*F[0]), FCString::Atoi(*F[1]), FCString::Atoi(*F[2]) }); }
			}
		}
		else
		{
			// PortalConfig сервера: прибытие в Castle_of_Chess 48,460 (второй вход 458,43 → точка у него 453,45); в Sillaris из данжа 368,33.
			const FStop Sil{ 0, 368, 33 }, ChA{ 9, 48, 460 }, ChB{ 9, 453, 45 }, TCh{ 11, 48, 460 };
			C->Route.Add(Sil);
			for (int32 t = 0; t < Trips; ++t) { C->Route.Add(ChA); C->Route.Add(ChB); C->Route.Add(Sil); }
			C->Route.Add(TCh); C->Route.Add(Sil);
		}
		if (C->Route.Num() == 0) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("пустой маршрут")); return; }
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		AsdaTestNet::SetSelfUserNumForDebug(Net, kMyUserNum);
		// ★ 11.11.2026 [ПОРТ] (D1): мир старта — уровень первой точки маршрута? Сверяем имя мира с уровнем карты из реестра.
		if (CVarAsdaPerfTravelStartOnRoute.GetValueOnGameThread() != 0)
		{
			const FAsdaMapRow* Row0 = LM->GetMapRow(C->Route[0].Map);                                     // строка реестра первой точки
			const FString Want = Row0 ? FPackageName::GetShortName(Row0->Level.ToSoftObjectPath().GetLongPackageName()) : FString();   // имя её уровня
			const FString Have = W ? UWorld::RemovePIEPrefix(W->GetMapName()) : FString();                // имя мира старта (без префикса PIE)
			// Только точное совпадение: `t_castle_of_chess` оканчивается на `castle_of_chess`, сверка по хвосту путала бы эти карты.
			C->bStartOnRoute = Want.IsEmpty() || Have.Equals(Want, ESearchCase::IgnoreCase);
			if (!C->bStartOnRoute)
			{
				UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ старт не на карте первой точки: мир %s, нужен %s (карта %d) — сначала переход"),
					*Have, *Want, C->Route[0].Map);
			}
		}
		// Номер текущей карты: без него спавн «до загрузки уровня» отложился бы до следующего PostLoadMap.
		//   ★ D1: только если стоим на карте первой точки — иначе чужому миру приписывался бы её номер (журнал писал «карта=0 alpen»).
		if (LM->CurrentMapId < 0 && C->bStartOnRoute) { LM->CurrentMapId = C->Route[0].Map; }
		UE_LOG(LogAsdaPerfTravelTest, Log, TEXT("ПЕРФ-ТРАВЕЛ старт: точек %d, прогрев %.1f с, замер %.1f с, мир %s, язык %s"),
			C->Route.Num(), C->WarmSec, C->MeasSec, W ? *W->GetMapName() : TEXT("?"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"));
		C->T0 = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return Step(C); }), 0.f);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaPerfTravelSelfTest(
	TEXT("asda.Perf.TravelSelfTest"),
	TEXT("Замер кадра и утечек через травел (05.11, жалоба «после Castle of Chess FPS не возвращается»): Silaris → Castle of Chess (оба входа) → "
	     "Silaris × trips → Distorted Castle of Chess → Silaris штатным BeginMapLoad. [trips=3] [warm=3] [meas=6] [route=<карта:x:y,...>] "
	     "[mobs=N] [profilegpu] [region] [noexpect] `quit`. Офлайн; старт на любой карте мира (не на первой точке маршрута — сначала переход на неё)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaPerfTravelTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
