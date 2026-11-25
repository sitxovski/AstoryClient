// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ЭФФЕКТЫ НА ТРАНСПОРТЕ ЕДУТ ВМЕСТЕ С НИМ» `asda.Mount.FxSelfTest` (15.10.2026). [ПОРТ]
//   Отчёт: `Reports/anticheat/wave_PORT_mount_fx.md`. Жалоба владельца: «на велосипеде эффект подсветки отстаёт от
//   транспорта, иногда не успевает догонять».
//
//   ЧТО МЕРЯЕТ (в КОНЦЕ КАДРА — `FWorldDelegates::OnWorldPostActorTick`, после всех групп тика, т.е. ровно та картина,
//   с которой кадр уходит в рендер), по КАЖДОМУ эффекту, следующему за носителем (`IsCarrierFollower`):
//     А) АКТОР: |позиция актора эффекта − живая точка привязки носителя этого кадра| (`ComputeFollowTargetWorld`).
//     Г) ГЕОМЕТРИЯ: вершины секций мировые и построены от трансформа актора `GeomBuildXf`; на экране они стоят по
//        `GeomBuildXf · Mesh` (Mesh ≠ identity только при переносе вслед за носителем). Ошибка = наибольший сдвиг
//        пяти опорных точек (центр и ±100 юн по осям актора) между «где геометрия на экране» и «где её построил бы
//        шаг этого кадра» (текущий трансформ актора). Ловит и пропущенный шаг, и поворот.
//     С) СВЕТ ЭФФЕКТА (UAsdaFxLightComponent): положение света в осях актора эффекта не должно зависеть от скорости
//        носителя. Эталонная серия — тот же эффект на СТОЯЩЕМ носителе (по возрасту эффекта); на ходу (круг —
//        постоянная скорость с разворотом) отклонение от эталона по тому же возрасту (линейная интерполяция): среднее,
//        знаковое «против хода» (систематическое отставание) и наибольшее. Частицы случайны, поэтому второй прогон
//        стоянки даёт ШУМ двух одинаковых прогонов — допуски считаются от него (с нижним полом).
//   СЦЕНАРИИ на своём персонаже (значимость эффекта = 2, ближняя зона) для КАЖДОГО транспорта с шлейфом:
//     посадка (`SetMount` — тем же путём, что 5453) → стоянка (эталон света) → круг 600 юн/с при ключах 0 («было»)
//     → круг при ключах 1 («стало») → рывки и развороты (скорость 0…1.2 юн/мс, реверс) при ключах 1.
//   ДАЛЬНИЙ НОСИТЕЛЬ (сосед-заглушка верхом): значимость 1 (в кадре дальше 15 м) и 0 (за камерой), ключи 0 и 1 —
//     здесь ломался шаг «через кадр» и заморозка геометрии.
//   Ключи: `asda.Fx.FollowFullRate` (геометрия на носителе каждый кадр), `asda.Fx.Light.CarrierAnchor` (свет у точек
//   эмиссии). Строки `было` — свидетельство причины (воспроизводится ли отставание без правки); в итог ok/fail идут
//   проверки «стало» и проверка «было воспроизводит» у велосипеда (иначе правка ничего не доказывает).
//   Только офлайн (`-AsdaNoNet`): `asda.Mount.FxSelfTest [delay=N] [quit] [все|<код>…]`.
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "AsdaEffectActor.h"                    // эффект, признаки носителя, геометрия шага
#include "AsdaDataTableCache.h"                 // AsdaDt::RowByInt — DT_RideEffect (тот же путь, что LookupRideFx)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — отложенный старт   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaCVarSave.h"                       // ключи на время самотеста
#include "Test/AsdaSelfTestKit.h"               // каркас: аргументы, отказ, итог, выход
#include "World/AsdaPlayerCharacter.h"          // SetMount / движение / FAsdaRideEffectRow
#include "World/AsdaWorldSubsystem.h"           // LookupMount, сосед-заглушка
#include "World/AsdaFxLightComponent.h"         // свет эффекта
#include "World/AsdaCoord.h"                    // UE ↔ game
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMountFxTest, Log, All);

namespace AsdaMountFxTest
{
	static const TCHAR* const kCmd = TEXT("asda.Mount.FxSelfTest");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ТРАНСПОРТ-FX");
	// Путь DT_RideEffect — тот же объект, что читает `LookupRideFx` мира (AsdaWorldSubsystem.cpp, kRideEffectDT).
	static const TCHAR* const kRideEffectDT = TEXT("/Game/AsdaAssets/Data/DT_RideEffect.DT_RideEffect");
	static const TCHAR* const kKeyGeom = TEXT("asda.Fx.FollowFullRate");
	static const TCHAR* const kKeyLight = TEXT("asda.Fx.Light.CarrierAnchor");

	// Допуски. Геометрия/актор — субъюнитные (правильный кадр даёт ровно 0, ошибка порядка пути за кадр — единицы-десятки юн).
	constexpr double kTolActor = 0.05;      // юн
	constexpr double kTolGeom = 0.5;        // юн
	// Свет: два прогона одного эффекта расходятся и без движения (частицы случайны) — допуски считаются от ШУМА двух
	//   стоянок (ключ 1), с нижним полом. Отставание «было» на порядок выше любого из них (39…536 юн в среднем).
	constexpr double kTolLightMean = 3.0;   // юн: пол среднего отклонения хода от стоянки (иначе ≤ 3× среднего шума)
	constexpr double kTolLightMax = 25.0;   // юн: пол наибольшего отклонения (иначе ≤ 3× наибольшего шума) — грубые скачки
	constexpr double kTolLag = 3.0;         // юн: пол среднего смещения ПРОТИВ хода (иначе ≤ 2× среднего шума)
	constexpr double kReproLight = 40.0;    // юн: «было» обязано показать отставание света хотя бы на столько (велосипед)
	constexpr float kPhaseStatic = 2.2f;    // с
	constexpr float kPhaseCircle = 2.2f;    // с
	constexpr float kPhaseJerk = 1.6f;      // с
	constexpr float kPhasePeer = 1.6f;      // с
	constexpr float kCircleSpeed = 0.6f;    // юн/мс (600 юн/с)
	constexpr float kCircleOmegaDeg = 115.f; // град/с — круг радиусом ~300 юн (не уезжать в стены)

	// Один случай: транспорт (код предмета SubType 15, шлейф ездит в attach 6) или ★ ревизия 15.10.2026 (п. 7 ревизии,
	//   «свет не только у транспорта»): эффект, ПРИВЯЗАННЫЙ к бегущему персонажу без транспорта — свечение оружия
	//   (attach-10 на кости 8_a1), эффект навыка/состояния (attach-5, позиция носителя), крылья (attach-10 крыльев).
	struct FCase
	{
		int32 Code; FString Label; FString Fx;
		bool bAttached = false;                              // false — транспорт, true — эффект на персонаже без седла
		EAsdaFxAttachMode Mode = EAsdaFxAttachMode::CharWorld;   // привязка эффекта на персонаже
		FName Bone = NAME_None;                              // кость привязки
		bool bSkelBase = false;                              // скелет = BaseSkel (иначе — как у крыльев: поиск носителем)
		FVector Offset = FVector::ZeroVector;                // смещение привязки
		bool bExpectLight = true;                            // у семейства свет включён — его отсутствие = провал
	};

	// Накопитель одной метрики.
	struct FStat
	{
		double Sum = 0.0, Max = 0.0; int32 N = 0; int32 Bad = 0;
		void Add(double V, double Tol) { Sum += V; Max = FMath::Max(Max, V); ++N; if (V > Tol) { ++Bad; } }
		double Mean() const { return N > 0 ? Sum / N : 0.0; }
		FString Str() const { return FString::Printf(TEXT("кадров=%d ср=%.3f макс=%.3f выше допуска=%d"), N, Mean(), Max, Bad); }
	};

	// Серия «свет в осях актора эффекта по возрасту эффекта» на стоящем носителе (эталон для хода).
	struct FSeries
	{
		TArray<double> Age; TArray<FVector> L;
		void Reset() { Age.Reset(); L.Reset(); }
		int32 Num() const { return Age.Num(); }
		// Линейная интерполяция по возрасту. false — возраст вне серии.
		bool At(double A, FVector& Out) const
		{
			for (int32 k = 1; k < Age.Num(); ++k)
			{
				if (A >= Age[k - 1] && A <= Age[k] && Age[k] > Age[k - 1])
				{
					Out = FMath::Lerp(L[k - 1], L[k], (A - Age[k - 1]) / (Age[k] - Age[k - 1]));
					return true;
				}
			}
			return false;
		}
	};

	// ★ Ревизия 15.10.2026: свет может быть «свой у каждого эмиттера» (навыки, `scope per_emitter`) — серии по номеру
	//   эмиттера света (INDEX_NONE = общий свет эффекта), сравнение хода со стоянкой — того же номера.
	struct FLightSeries
	{
		TMap<int32, FSeries> ByIdx;
		void Reset() { ByIdx.Reset(); }
		int32 Num() const { int32 N = 0; for (const auto& KV : ByIdx) { N += KV.Value.Num(); } return N; }
		void Add(int32 Idx, double Age, const FVector& L) { FSeries& Sr = ByIdx.FindOrAdd(Idx); Sr.Age.Add(Age); Sr.L.Add(L); }
		bool At(int32 Idx, double Age, FVector& Out) const { const FSeries* Sr = ByIdx.Find(Idx); return Sr && Sr->At(Age, Out); }
	};

	enum class EPhase : uint8 { Mount, Settle, StaticOn, StaticOn2, StaticOff, CircleOff, CircleOn, Jerk, PeerFarOff, PeerFarOn, PeerOffOff, PeerOffOn, Done };

	struct FState
	{
		TWeakObjectPtr<UWorld> W;
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;
		TWeakObjectPtr<AAsdaPlayerCharacter> Peer;
		TArray<FCase> Cases;
		TArray<int32> PeerCases;            // индексы Cases, на которых гоняется дальний носитель
		int32 CaseIdx = 0;
		int32 PeerCaseIdx = 0;
		EPhase Phase = EPhase::Mount;
		double PhaseStart = 0.0;
		bool bPhaseInit = false;            // подготовка фазы дальнего носителя выполнена
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		FDelegateHandle Handle;
		// Текущий транспорт
		FString RideFx;
		// Отслеживаемый эффект для света (спавн тем же вызовом, что шлейф `SpawnRideTrailFx`)
		TWeakObjectPtr<AAsdaEffectActor> Tracked;
		double TrackedSpawn = 0.0;
		FLightSeries StaticOn, StaticOff;   // свет на стоянке при ключе 1 / 0 (эталон своего ключа)
		FLightSeries StaticOn2;             // второй прогон стоянки при ключе 1 — ШУМ (случайные частицы другого прогона)
		FStat LightOff, LightOn;            // отклонение света на ходу от стоянки того же ключа (0 / 1)
		FStat Noise;                        // отклонение двух стоянок друг от друга (ключ 1) — пол шума облака частиц
		// Смещение света ПРОТИВ хода (знаковое, юн): (эталон стоянки − свет на ходу) · направление хода в осях эффекта.
		//   Отставание даёт систематический плюс; шум случайных частиц — ноль в среднем.
		double LagSumOn = 0.0, LagSumOff = 0.0; int32 LagNOn = 0, LagNOff = 0;
		int32 CircleFramesOn = 0;           // кадров фазы хода при ключе 1 — длина кадра для допуска (см. ReportCase)
		FVector PrevMeLoc = FVector::ZeroVector; bool bHavePrevMe = false;
		FStat ActorOn, GeomOn, ActorOff, GeomOff; // актор/геометрия по всем эффектам носителя
		double PathLen = 0.0; FVector PrevCarrier = FVector::ZeroVector; bool bHavePrev = false;
		int32 VisHist[3] = {0, 0, 0};       // уровни значимости у измеренных эффектов дальнего носителя
		int32 PeerUserNum = 7731;
		FVector2D HomeGame = FVector2D::ZeroVector;   // своя точка старта (game XZ) — сюда возвращаемся перед каждым транспортом
		FVector2D PeerHome = FVector2D::ZeroVector;
		FVector2D PeerAway = FVector2D::ZeroVector;   // точка за камерой (фазы «за камерой»)
		bool bPeerAwayDone = false;                   // телепорт за камеру выполнен
		float JerkSpeed = 0.f; double JerkNext = 0.0; int32 JerkStep = 0;

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaMountFxTest, Log, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaMountFxTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Info(const FString& What) const { UE_LOG(LogAsdaMountFxTest, Log, TEXT("%s инфо %s"), kPrefix, *What); }
	};

	// Ключи правки (оба сразу): 1 — «стало», 0 — «было».
	static void SetKeys(int32 V)
	{
		AsdaCVarSave::SetForTest(kKeyGeom, V);
		AsdaCVarSave::SetForTest(kKeyLight, V);
	}

	// Живая точка привязки носителя этого кадра — ТА ЖЕ цепочка, что строит Tick эффекта (Д3: единая точка эффекта).
	static bool LiveAnchor(const AAsdaEffectActor* Fx, FVector& Out)
	{
		if (!Fx) { return false; }
		if (Fx->bFollowCharWorld || Fx->bFollowBoneFull)
		{
			FVector Loc; FQuat Q;
			if (!Fx->ComputeFollowTargetWorld(Loc, Q)) { return false; }
			Out = Loc + Q.RotateVector(Fx->FollowOffset);
			return true;
		}
		if (Fx->bFollowCharPos && Fx->FollowActor)
		{
			Out = Fx->FollowActor->GetActorLocation() + Fx->FollowOffset;
			return true;
		}
		return false;
	}

	// Носитель эффекта (персонаж): FollowActor, иначе владелец.
	static const AActor* CarrierOf(const AAsdaEffectActor* Fx)
	{
		if (!Fx) { return nullptr; }
		if (Fx->FollowActor) { return Fx->FollowActor.Get(); }
		if (Fx->FollowComponent) { return Fx->FollowComponent->GetOwner(); }
		return Fx->GetOwner();
	}

	// Ошибка геометрии: наибольший сдвиг опорных точек «на экране» против «построил бы шаг этого кадра».
	static double GeomError(const AAsdaEffectActor* Fx)
	{
		const FTransform Build = Fx->GetGeomBuildXf();
		const FTransform Shown = Build * Fx->GetGeomComponentXf();   // вершина v = Build(l) показана в Mesh(v)
		const FTransform Now = Fx->GetActorTransform();
		static const FVector kProbe[] = { FVector::ZeroVector, FVector(100, 0, 0), FVector(-100, 0, 0),
			FVector(0, 100, 0), FVector(0, 0, 100) };
		double E = 0.0;
		for (const FVector& L : kProbe)
		{
			E = FMath::Max(E, (double)FVector::Dist(Shown.TransformPosition(L), Now.TransformPosition(L)));
		}
		return E;
	}

	// Горящие светы эффекта в осях актора эффекта: (номер эмиттера света, положение). Пусто — света нет / не горит.
	static void LightLocalAll(const AAsdaEffectActor* Fx, TArray<TPair<int32, FVector>>& Out)
	{
		Out.Reset();
		if (!Fx) { return; }
		TInlineComponentArray<UAsdaFxLightComponent*> Ls(Fx);
		for (const UAsdaFxLightComponent* L : Ls)
		{
			if (L && L->IsLit()) { Out.Emplace(L->GetEmitterIndex(), Fx->GetActorTransform().InverseTransformPosition(L->GetComponentLocation())); }
		}
	}

	// Спавн отслеживаемого эффекта: шлейф — ТОТ ЖЕ вызов, что `AAsdaPlayerCharacter::SpawnRideTrailFx` (attach 6 от
	//   M_DUMMY); эффект на персонаже — та же единая точка `SpawnOnCarrier` с привязкой случая.
	static AAsdaEffectActor* SpawnTracked(UWorld* W, AAsdaPlayerCharacter* Ch, const FCase& C)
	{
		if (!W || !Ch || C.Fx.IsEmpty()) { return nullptr; }
		FVector Start = Ch->GetActorLocation();
		if (C.bAttached)
		{
			return AAsdaEffectActor::SpawnOnCarrier(W, C.Fx, Start, Ch, C.bSkelBase ? ToRawPtr(Ch->BaseSkel) : nullptr, C.Bone, C.Mode, C.Offset);
		}
		if (Ch->BaseSkel && Ch->BaseSkel->DoesSocketExist(TEXT("M_DUMMY"))) { Start = Ch->BaseSkel->GetSocketLocation(TEXT("M_DUMMY")); }
		return AAsdaEffectActor::SpawnOnCarrier(W, C.Fx, Start, Ch, Ch->BaseSkel, NAME_None, EAsdaFxAttachMode::CharWorld);
	}

	// Посадить носителя на транспорт `Code` тем же путём, что мир при 5453 (LookupMount + DT_RideEffect + SetMount).
	static bool MountOn(UWorld* W, AAsdaPlayerCharacter* Ch, int32 Code, FString& OutFx)
	{
		UAsdaWorldSubsystem* WS = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS || !Ch) { return false; }
		FString Model, AnimSet; float H = 0.f;
		if (!WS->LookupMount(Code, Model, AnimSet, H)) { return false; }
		const FAsdaRideEffectRow* R = AsdaDt::RowByInt<FAsdaRideEffectRow>(kRideEffectDT, Code, kCmd);
		if (!R) { return false; }
		OutFx = R->EffectName;
		if (Ch->IsMounted()) { Ch->ClearMount(); }
		Ch->SetMount(Model, AnimSet, H, R->EffectName, R->SoundName, R->SoundName2, R->SoundName3);
		return Ch->IsMounted();
	}

	// Гнать носителя по кругу (game XZ): направление поворачивается с угловой скоростью kCircleOmegaDeg.
	static void DriveCircle(AAsdaPlayerCharacter* Ch, double T)
	{
		if (!Ch) { return; }
		const double A = FMath::DegreesToRadians(kCircleOmegaDeg * T);
		const FVector2D D((float)FMath::Cos(A), (float)FMath::Sin(A));
		Ch->SetMoveDirection(D, kCircleSpeed, D, false);
	}

	// Рывки: каждые 0.2 с новая скорость из ряда, через шаг — разворот на 180° и поворот на 90°.
	static void DriveJerk(FState& S, AAsdaPlayerCharacter* Ch, double Now)
	{
		if (!Ch) { return; }
		static const float kSpeeds[] = { 0.2f, 1.2f, 0.f, 0.8f, 1.0f, 0.f, 0.35f, 1.2f };
		if (Now >= S.JerkNext)
		{
			S.JerkSpeed = kSpeeds[S.JerkStep % UE_ARRAY_COUNT(kSpeeds)];
			++S.JerkStep;
			S.JerkNext = Now + 0.2;
		}
		const double A = FMath::DegreesToRadians(90.0 * (S.JerkStep % 4)) + (S.JerkStep % 2 ? PI : 0.0);
		const FVector2D D((float)FMath::Cos(A), (float)FMath::Sin(A));
		if (S.JerkSpeed <= 0.f) { if (Ch->IsMoving()) { Ch->StopMove(); } }
		else { Ch->SetMoveDirection(D, S.JerkSpeed, D, false); }
	}

	// Замер А/Г по всем эффектам, следующим за носителем `Carrier`. bOn — в какой накопитель.
	static void MeasureFollowers(FState& S, UWorld* W, const AActor* Carrier, bool bOn, bool bVisHist)
	{
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
		{
			AAsdaEffectActor* Fx = *It;
			if (!Fx || Fx->IsActorBeingDestroyed() || !Fx->IsCarrierFollower() || CarrierOf(Fx) != Carrier) { continue; }
			if (Fx->GetGeomBuildFrame() == 0) { continue; }   // ещё ни разу не строилась (первый кадр после спавна)
			FVector T;
			if (LiveAnchor(Fx, T))
			{
				(bOn ? S.ActorOn : S.ActorOff).Add(FVector::Dist(Fx->GetActorLocation(), T), kTolActor);
			}
			(bOn ? S.GeomOn : S.GeomOff).Add(GeomError(Fx), kTolGeom);
			if (bVisHist) { S.VisHist[FMath::Clamp((int32)Fx->GetVisLevel(), 0, 2)]++; }
		}
		if (const AActor* C = Carrier)
		{
			const FVector P = C->GetActorLocation();
			if (S.bHavePrev) { S.PathLen += FVector::Dist(P, S.PrevCarrier); }
			S.PrevCarrier = P; S.bHavePrev = true;
		}
	}

	// Сброс накопителей фазы.
	static void ResetMotionStats(FState& S)
	{
		S.ActorOn = S.GeomOn = S.ActorOff = S.GeomOff = FStat();
		S.PathLen = 0.0; S.bHavePrev = false;
		S.VisHist[0] = S.VisHist[1] = S.VisHist[2] = 0;
	}

	static void Begin(FState& S, EPhase P, double Now)
	{
		S.Phase = P; S.PhaseStart = Now; S.bPhaseInit = false;
	}

	static void Finish(TSharedRef<FState> SP)
	{
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (S.Handle.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(S.Handle); S.Handle.Reset(); }
		if (AAsdaPlayerCharacter* Ch = S.Me.Get()) { Ch->StopMove(); if (Ch->IsMounted()) { Ch->ClearMount(); } }
		if (AAsdaPlayerCharacter* P = S.Peer.Get()) { P->StopMove(); if (P->IsMounted()) { P->ClearMount(); } }
		if (AAsdaEffectActor* Fx = S.Tracked.Get()) { Fx->Destroy(); }
		AsdaSelfTestKit::Finish(W, kCmd, S.bQuit, LogAsdaMountFxTest, kPrefix, S.Ok, S.Fail);
	}

	// Велосипед/байк — транспорт жалобы владельца: на нём «было» обязано воспроизводить отставание.
	static bool IsReproCase(const FCase& C)
	{
		return C.Fx == TEXT("itm1_n_96_vehicle_0012_e") || C.Fx == TEXT("itm1_n_96_vehicle_0018_e");
	}

	// Итог одного транспорта (свои сценарии).
	static void ReportCase(FState& S, const FCase& C)
	{
		const double LagOn = S.LagNOn > 0 ? S.LagSumOn / S.LagNOn : 0.0;
		const double LagOff = S.LagNOff > 0 ? S.LagSumOff / S.LagNOff : 0.0;
		S.Info(FString::Printf(TEXT("[%s] свет на стоянке: проб ключ 1 %d (+%d повтор), ключ 0 %d; шум двух стоянок: %s; на ходу ключ 0 (было): %s, против хода ср %.1f юн; ключ 1 (стало): %s, против хода ср %.2f юн"),
			*C.Label, S.StaticOn.Num(), S.StaticOn2.Num(), S.StaticOff.Num(), *S.Noise.Str(), *S.LightOff.Str(), LagOff, *S.LightOn.Str(), LagOn));
		if (S.StaticOn.Num() >= 10 && S.LightOn.N >= 10 && S.LagNOn >= 10)
		{
			// Систематическое отставание ловят среднее отклонение и знаковое «против хода»; наибольшее — только грубые скачки.
			// ★ 11.11.2026 (D4b, разбор красных эталона 7.6.0). Что меряется: положение света в осях эффекта на ходу против стоянки
			//   при том же возрасте эффекта. Свет стоит у точек эмиссии случайных частиц, поэтому и две стоянки расходятся («шум»),
			//   а частицы рождаются по кадрам: чем длиннее кадр, тем грубее шаг эмиссии и тем больше случайный разброс. Замеры:
			//   12 мс/кадр — ср. 2.0 юн; 22 мс — 5.67 (допуск 5.62, провал); 28 мс — 14.15 (допуск 5.81, провал); на повторе те же
			//   транспорты зелёные, красные — другие. Отставание, которое ловит проверка, даёт 75–150 юн и знаковое смещение
			//   против хода. Поэтому допуски умножаются на (длина кадра / 16.7 мс)², не меньше 1 и не больше 4: при 60 кадрах/с
			//   проверка та же, что была, при 30 — вчетверо шире (до ≈24 юн), всё ещё в разы ниже настоящего отставания.
			const double FrameMs = S.CircleFramesOn > 0 ? 1000.0 * kPhaseCircle / S.CircleFramesOn : 16.7;
			const double Slow = FMath::Clamp(FMath::Square(FrameMs / 16.7), 1.0, 4.0);
			const double TolMean = FMath::Max(kTolLightMean, 3.0 * S.Noise.Mean()) * Slow;
			const double TolLag = FMath::Max(kTolLag, 2.0 * S.Noise.Mean()) * Slow;
			const double TolMax = FMath::Max(kTolLightMax, 3.0 * S.Noise.Max);
			S.Expect(S.LightOn.Mean() <= TolMean && FMath::Abs(LagOn) <= TolLag && S.LightOn.Max <= TolMax,
				FString::Printf(TEXT("[%s] свет на ходу едет вместе с транспортом: отклонение от стоянки ср %.2f (допуск %.2f), против хода ср %.2f (допуск %.2f), макс %.2f (допуск %.2f) юн; шум стоянок ср %.2f макс %.2f"),
					*C.Label, S.LightOn.Mean(), TolMean, LagOn, TolLag, S.LightOn.Max, TolMax, S.Noise.Mean(), S.Noise.Max));
		}
		else if (!C.bExpectLight && S.StaticOn.Num() == 0 && S.LightOn.N == 0)
		{
			S.Info(FString::Printf(TEXT("[%s] свет у семейства выключен (Tools/asda_fx_light.json) — проверены актор и геометрия"), *C.Label));
		}
		else
		{
			S.Expect(false, FString::Printf(TEXT("[%s] свет эффекта не горел — проб стоянки %d, хода %d (нужно ≥10)"),
				*C.Label, S.StaticOn.Num(), S.LightOn.N));
		}
		// «Было» обязано воспроизводить отставание у велосипеда — иначе правка ничего не доказывает.
		if (!C.bAttached && IsReproCase(C))
		{
			S.Expect(S.LightOff.N >= 10 && S.LightOff.Max >= kReproLight && LagOff >= kReproLight * 0.5,
				FString::Printf(TEXT("[%s] без правки (ключ 0) свет отстаёт — воспроизведено: против хода ср %.1f юн, макс %.1f юн (порог %.0f)"),
					*C.Label, LagOff, S.LightOff.Max, kReproLight));
		}
	}

	static void Tick(TSharedRef<FState> SP, UWorld* InW)
	{
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W || InW != W) { return; }
		AAsdaPlayerCharacter* Me = S.Me.Get();
		if (!Me) { S.Expect(false, TEXT("свой персонаж пропал")); Finish(SP); return; }
		const double Now = W->GetTimeSeconds();
		const double T = Now - S.PhaseStart;
		const FCase* C = S.Cases.IsValidIndex(S.CaseIdx) ? &S.Cases[S.CaseIdx] : nullptr;

		switch (S.Phase)
		{
		case EPhase::Mount:
		{
			if (!C) { Begin(S, S.PeerCases.Num() > 0 ? EPhase::PeerFarOff : EPhase::Done, Now); S.PeerCaseIdx = 0; break; }
			Me->StopMove();
			Me->NetSnap(S.HomeGame);   // каждый транспорт — с одной точки (круг/рывки не уводят в стены сетки проходимости)
			if (C->bAttached)
			{
				// Эффект на персонаже БЕЗ седла: снять транспорт прошлого случая, эффект — свой.
				if (Me->IsMounted()) { Me->ClearMount(); }
				S.RideFx = C->Fx;
				if (!AAsdaEffectActor::ResolveJebClass(S.RideFx))
				{
					S.Expect(false, FString::Printf(TEXT("[%s] эффект %s не собран"), *C->Label, *S.RideFx));
					++S.CaseIdx; Begin(S, EPhase::Mount, Now); break;
				}
				S.Info(FString::Printf(TEXT("[%s] эффект на бегущем персонаже без транспорта: %s"), *C->Label, *S.RideFx));
				S.StaticOn.Reset(); S.StaticOn2.Reset(); S.StaticOff.Reset(); S.LightOff = S.LightOn = S.Noise = FStat();
				S.LagSumOn = S.LagSumOff = 0.0; S.LagNOn = S.LagNOff = 0; S.CircleFramesOn = 0;
				SetKeys(1);
				Begin(S, EPhase::Settle, Now);
				break;
			}
			if (!MountOn(W, Me, C->Code, S.RideFx) || S.RideFx.IsEmpty())
			{
				S.Expect(false, FString::Printf(TEXT("[%s] посадка: код %d не транспорт или без шлейфа"), *C->Label, C->Code));
				++S.CaseIdx; Begin(S, EPhase::Mount, Now); break;
			}
			// Шлейф без ассета (у `itm1_n_96_vehicle_0038_e` исходного .jeb нет ни в одном срезе клиента — mount_system_re.md §6.5):
			//   оригинал на таком `Effect_SpawnByName` ничего не создаёт, порт — тоже (SetMount пишет предупреждение). Мерить нечего.
			if (!AAsdaEffectActor::ResolveJebClass(S.RideFx))
			{
				S.Info(FString::Printf(TEXT("[%s] шлейф %s не собран (исходника .jeb нет в клиенте) — эффекта нет, как в оригинале; пропуск"),
					*C->Label, *S.RideFx));
				++S.CaseIdx; Begin(S, EPhase::Mount, Now); break;
			}
			S.Info(FString::Printf(TEXT("[%s] посадка: код %d, шлейф %s"), *C->Label, C->Code, *S.RideFx));
			S.StaticOn.Reset(); S.StaticOn2.Reset(); S.StaticOff.Reset(); S.LightOff = S.LightOn = S.Noise = FStat();
			S.LagSumOn = S.LagSumOff = 0.0; S.LagNOn = S.LagNOff = 0; S.CircleFramesOn = 0;
			SetKeys(1);
			Begin(S, EPhase::Settle, Now);
			break;
		}
		case EPhase::Settle:
			if (T >= 0.4) { Begin(S, EPhase::StaticOn, Now); S.Tracked = SpawnTracked(W, Me, *C); S.TrackedSpawn = Now;
				if (!S.Tracked.IsValid()) { S.Expect(false, FString::Printf(TEXT("[%s] шлейф %s не заспавнился"), *C->Label, *S.RideFx)); } }
			break;
		case EPhase::StaticOn:
		case EPhase::StaticOn2:
		case EPhase::StaticOff:
		{
			FLightSeries& Ser = (S.Phase == EPhase::StaticOn) ? S.StaticOn : (S.Phase == EPhase::StaticOn2 ? S.StaticOn2 : S.StaticOff);
			TArray<TPair<int32, FVector>> Ls;
			LightLocalAll(S.Tracked.Get(), Ls);
			const double Age = Now - S.TrackedSpawn;
			for (const TPair<int32, FVector>& L : Ls)
			{
				Ser.Add(L.Key, Age, L.Value);
				FVector Ref;
				if (S.Phase == EPhase::StaticOn2 && S.StaticOn.At(L.Key, Age, Ref)) { S.Noise.Add(FVector::Dist(L.Value, Ref), 1.0e9); }
			}
			if (T >= kPhaseStatic)
			{
				if (AAsdaEffectActor* Fx = S.Tracked.Get()) { Fx->Destroy(); }
				const EPhase Next = (S.Phase == EPhase::StaticOn) ? EPhase::StaticOn2
					: (S.Phase == EPhase::StaticOn2 ? EPhase::StaticOff : EPhase::CircleOff);
				SetKeys(Next == EPhase::StaticOn2 ? 1 : 0);
				ResetMotionStats(S);
				S.bHavePrevMe = false;
				Begin(S, Next, Now);
				S.Tracked = SpawnTracked(W, Me, *C); S.TrackedSpawn = Now;
			}
			break;
		}
		case EPhase::CircleOff:
		case EPhase::CircleOn:
		{
			const bool bOn = (S.Phase == EPhase::CircleOn);
			MeasureFollowers(S, W, Me, bOn, false);
			if (bOn) { ++S.CircleFramesOn; }
			// Направление хода этого кадра (мир) — для знакового «против хода».
			const FVector MeLoc = Me->GetActorLocation();
			const FVector Vel = S.bHavePrevMe ? (MeLoc - S.PrevMeLoc) : FVector::ZeroVector;
			S.PrevMeLoc = MeLoc; S.bHavePrevMe = true;
			if (AAsdaEffectActor* Fx = S.Tracked.Get())
			{
				TArray<TPair<int32, FVector>> Ls;
				LightLocalAll(Fx, Ls);
				for (const TPair<int32, FVector>& L : Ls)
				{
					FVector Ref;
					if (!(bOn ? S.StaticOn : S.StaticOff).At(L.Key, Now - S.TrackedSpawn, Ref)) { continue; }
					(bOn ? S.LightOn : S.LightOff).Add(FVector::Dist(L.Value, Ref), 1.0e9);
					if (Vel.SizeSquared() > 1.0)
					{
						const FVector DirLocal = Fx->GetActorTransform().InverseTransformVectorNoScale(Vel.GetSafeNormal());
						const double Lag = FVector::DotProduct(Ref - L.Value, DirLocal);
						if (bOn) { S.LagSumOn += Lag; ++S.LagNOn; } else { S.LagSumOff += Lag; ++S.LagNOff; }
					}
				}
			}
			DriveCircle(Me, T);
			if (T >= kPhaseCircle)
			{
				if (AAsdaEffectActor* Fx = S.Tracked.Get()) { Fx->Destroy(); }
				S.Info(FString::Printf(TEXT("[%s] круг ключ %d: путь %.0f юн; актор %s; геометрия %s"), *C->Label, bOn ? 1 : 0,
					S.PathLen, *(bOn ? S.ActorOn : S.ActorOff).Str(), *(bOn ? S.GeomOn : S.GeomOff).Str()));
				if (bOn)
				{
					S.Expect(S.PathLen > 500.0, FString::Printf(TEXT("[%s] носитель ехал по кругу: путь %.0f юн"), *C->Label, S.PathLen));
				// Эффект на персонаже без седла, живой ли он к концу круга (иначе мерить было нечего).
				S.Expect(S.ActorOn.N > 0, FString::Printf(TEXT("[%s] эффект жил на носителе весь круг (кадров замера %d)"), *C->Label, S.ActorOn.N));
					S.Expect(S.ActorOn.N > 0 && S.ActorOn.Bad == 0 && S.GeomOn.Bad == 0,
						FString::Printf(TEXT("[%s] круг: эффекты на носителе в том же кадре — актор макс %.3f, геометрия макс %.3f юн"),
							*C->Label, S.ActorOn.Max, S.GeomOn.Max));
					Me->StopMove();
					ResetMotionStats(S);
					S.JerkStep = 0; S.JerkNext = 0.0;
					Begin(S, EPhase::Jerk, Now);
					// Эффект на персонаже без седла сам не рождается (шлейф транспорта рождает TickRideEffect) — свой на рывки.
					if (C->bAttached) { S.Tracked = SpawnTracked(W, Me, *C); S.TrackedSpawn = Now; }
				}
				else
				{
					Me->StopMove();
					SetKeys(1);
					ResetMotionStats(S);
					S.bHavePrevMe = false;
					Begin(S, EPhase::CircleOn, Now);
					S.Tracked = SpawnTracked(W, Me, *C); S.TrackedSpawn = Now;
				}
			}
			break;
		}
		case EPhase::Jerk:
		{
			MeasureFollowers(S, W, Me, true, false);
			DriveJerk(S, Me, Now);
			if (T >= kPhaseJerk)
			{
				Me->StopMove();
				if (C->bAttached) { if (AAsdaEffectActor* Fx = S.Tracked.Get()) { Fx->Destroy(); } }
				S.Info(FString::Printf(TEXT("[%s] рывки/развороты: путь %.0f юн; актор %s; геометрия %s"), *C->Label,
					S.PathLen, *S.ActorOn.Str(), *S.GeomOn.Str()));
				S.Expect(S.ActorOn.N > 0 && S.ActorOn.Bad == 0 && S.GeomOn.Bad == 0,
					FString::Printf(TEXT("[%s] рывки/развороты: эффекты на носителе в том же кадре — актор макс %.3f, геометрия макс %.3f юн"),
						*C->Label, S.ActorOn.Max, S.GeomOn.Max));
				ReportCase(S, *C);
				++S.CaseIdx;
				Begin(S, EPhase::Mount, Now);
			}
			break;
		}
		case EPhase::PeerFarOff:
		case EPhase::PeerFarOn:
		case EPhase::PeerOffOff:
		case EPhase::PeerOffOn:
		{
			const bool bOn = (S.Phase == EPhase::PeerFarOn || S.Phase == EPhase::PeerOffOn);
			const bool bFar = (S.Phase == EPhase::PeerFarOff || S.Phase == EPhase::PeerFarOn);
			AAsdaPlayerCharacter* P = S.Peer.Get();
			const FCase& PC = S.Cases[S.PeerCases[S.PeerCaseIdx]];
			if (!S.bPhaseInit)
			{
				S.bPhaseInit = true;
				// Начало фазы: поставить соседа в кадр дальше ближней зоны (Far) или за камеру (Off), посадить, ключи.
				UAsdaWorldSubsystem* WS = W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
				APlayerController* PCtl = W->GetFirstPlayerController();
				FVector CamLoc = Me->GetActorLocation(); FRotator CamRot = FRotator::ZeroRotator;
				if (PCtl && PCtl->PlayerCameraManager) { CamLoc = PCtl->PlayerCameraManager->GetCameraLocation(); CamRot = PCtl->PlayerCameraManager->GetCameraRotation(); }
				FVector Fwd = CamRot.Vector(); Fwd.Z = 0.f; Fwd = Fwd.GetSafeNormal();
				// За камеру сосед уходит НЕ сразу: сначала 0.6 с в кадре (шлейф успевает построиться), затем телепорт назад —
				//   «всадник уехал из кадра»; иначе у шлейфа, рождённого уже вне кадра, геометрии нет вовсе и мерить нечего.
				const FVector Spot = Me->GetActorLocation() + Fwd * 2600.f;
				S.PeerHome = AsdaCoord::UeToGame2D(Spot);
				S.PeerAway = AsdaCoord::UeToGame2D(Me->GetActorLocation() - Fwd * 3000.f);
				if (!P && WS)
				{
					WS->DebugSpawnFakePeer(S.PeerUserNum, TEXT("ВсадникFX"), S.PeerHome);
					P = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(S.PeerUserNum));
					S.Peer = P;
				}
				if (!P) { S.Expect(false, TEXT("сосед-заглушка не появился")); Begin(S, EPhase::Done, Now); break; }
				P->NetSnap(S.PeerHome);
				FString PFx;
				if (S.Phase == EPhase::PeerFarOff || !P->IsMounted())   // новый транспорт соседа — посадка; дальше — тот же
				{
					if (!MountOn(W, P, PC.Code, PFx)) { S.Expect(false, FString::Printf(TEXT("[%s] сосед: посадка не удалась"), *PC.Label)); }
				}
				SetKeys(bOn ? 1 : 0);
				ResetMotionStats(S);
				S.bPeerAwayDone = bFar;
				S.PhaseStart = Now + (bFar ? 0.25 : 0.85);   // прогрев значимости/первый шлейф (за камерой — ещё 0.6 с в кадре) — не меряем
				break;
			}
			if (!S.bPeerAwayDone && T >= -0.25)
			{
				S.bPeerAwayDone = true;
				P->NetSnap(S.PeerAway);   // всадник «уехал» за камеру
			}
			if (T < 0.0) { DriveCircle(P, T + 1.0); break; }
			MeasureFollowers(S, W, P, bOn, true);
			DriveCircle(P, T);
			if (T >= kPhasePeer)
			{
				P->StopMove();
				const FStat& G = bOn ? S.GeomOn : S.GeomOff;
				const FStat& A = bOn ? S.ActorOn : S.ActorOff;
				const TCHAR* Where = bFar ? TEXT("в кадре дальше 15 м") : TEXT("за камерой");
				S.Info(FString::Printf(TEXT("[%s] дальний носитель %s, ключ %d: путь %.0f; значимость 0/1/2 = %d/%d/%d; актор %s; геометрия %s"),
					*PC.Label, Where, bOn ? 1 : 0, S.PathLen, S.VisHist[0], S.VisHist[1], S.VisHist[2], *A.Str(), *G.Str()));
				const int32 WantLevel = bFar ? 1 : 0;
				const bool bLevelSeen = S.VisHist[WantLevel] > 0;
				if (bOn)
				{
					S.Expect(bLevelSeen && A.N > 0 && A.Bad == 0 && G.Bad == 0,
						FString::Printf(TEXT("[%s] дальний носитель %s (значимость %d встречена %d раз): геометрия едет с ним — макс %.3f юн, актор макс %.3f"),
							*PC.Label, Where, WantLevel, S.VisHist[WantLevel], G.Max, A.Max));
				}
				else
				{
					S.Expect(bLevelSeen && G.Bad > 0,
						FString::Printf(TEXT("[%s] без правки (ключ 0) геометрия дальнего носителя %s отстаёт — воспроизведено: кадров с отставанием %d, макс %.1f юн"),
							*PC.Label, Where, G.Bad, G.Max));
				}
				// Следующая фаза: FarOff → FarOn → OffOff → OffOn → следующий транспорт соседа
				EPhase Next = EPhase::Done;
				switch (S.Phase)
				{
				case EPhase::PeerFarOff: Next = EPhase::PeerFarOn; break;
				case EPhase::PeerFarOn:  Next = EPhase::PeerOffOff; break;
				case EPhase::PeerOffOff: Next = EPhase::PeerOffOn; break;
				default:
					++S.PeerCaseIdx;
					Next = S.PeerCases.IsValidIndex(S.PeerCaseIdx) ? EPhase::PeerFarOff : EPhase::Done;
					if (P->IsMounted()) { P->ClearMount(); }
					break;
				}
				Begin(S, Next, Now);
			}
			break;
		}
		case EPhase::Done:
		default:
			Finish(SP);
			return;
		}
	}

	// Транспорты прогона — ИЗ ДАННЫХ, без ручного списка: каждый код SubType 15 (`LookupMount` мира — тот же индекс, что
	//   у посадки 5453) с непустым шлейфом в DT_RideEffect; на каждый уникальный .jeb — первый код. Позиционные аргументы
	//   (коды предметов) сужают прогон.
	static TArray<FCase> BuildCases(UWorld* W, const FAsdaSelfTestArgs& A)
	{
		TArray<FCase> Out;
		UAsdaWorldSubsystem* WS = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS) { return Out; }
		TSet<int32> Want;
		for (const FString& P : A.Pos) { if (P.IsNumeric()) { Want.Add(FCString::Atoi(*P)); } }
		TSet<FString> Seen;
		for (int32 Code = 1; Code <= 4000; ++Code)
		{
			if (Want.Num() > 0 && !Want.Contains(Code)) { continue; }
			FString Model, AnimSet; float H = 0.f;
			if (!WS->LookupMount(Code, Model, AnimSet, H)) { continue; }
			const FAsdaRideEffectRow* R = AsdaDt::RowByInt<FAsdaRideEffectRow>(kRideEffectDT, Code, kCmd);
			if (!R || R->EffectName.IsEmpty() || Seen.Contains(R->EffectName)) { continue; }
			Seen.Add(R->EffectName);
			Out.Add({ Code, FString::Printf(TEXT("%s/код %d"), *R->EffectName.RightChop(R->EffectName.Len() > 9 ? R->EffectName.Len() - 7 : 0), Code), R->EffectName });
		}
		// ★ Ревизия 15.10.2026: эффекты на бегущем персонаже без транспорта (те же привязки, что в мире:
		//   свечение оружия — AsdaWorldSubsystem RefreshWeaponGlow (BaseSkel, 8_a1, WeaponGlow); крылья — SetWings
		//   (кость M, скелет поиском, Wing); навык/состояние — канал состояний attach-5 (CharPos, +15 по высоте)).
		//   Берутся при прогоне без кодов или с аргументом `носители`.
		if (Want.Num() == 0 || A.Has(TEXT("носители")))
		{
			auto AddAtt = [&Out](const TCHAR* Label, const TCHAR* Jeb, EAsdaFxAttachMode Mode, const TCHAR* Bone, bool bBase, float Rise, bool bLight)
			{
				FCase C{ 0, Label, Jeb };
				C.bAttached = true; C.Mode = Mode; C.Bone = Bone ? FName(Bone) : NAME_None; C.bSkelBase = bBase;
				C.Offset = FVector(0.f, 0.f, Rise); C.bExpectLight = bLight;
				Out.Add(C);
			};
			AddAtt(TEXT("оружие ohs_a_co_ma_01"), TEXT("ohs_a_co_ma_01"), EAsdaFxAttachMode::WeaponGlow, TEXT("8_a1"), true, 0.f, true);
			AddAtt(TEXT("навык roarug01"), TEXT("eff_1_char_01_roarug01-a_00"), EAsdaFxAttachMode::CharPos, nullptr, false, AAsdaEffectActor::kEtalonTrackRiseZ, true);
			AddAtt(TEXT("навык mpsons02"), TEXT("eff_1_char_99_mpsons02-a_00"), EAsdaFxAttachMode::CharPos, nullptr, false, AAsdaEffectActor::kEtalonTrackRiseZ, true);
			AddAtt(TEXT("крылья eff_wing_m_ss_00"), TEXT("eff_wing_m_ss_00"), EAsdaFxAttachMode::Wing, TEXT("M"), false, 0.f, false);
		}
		return Out;
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TWeakObjectPtr<UWorld> WW(W);
		// t = delay: офлайн-спавн своего павна штатными крюками (как у `asda.Action.SelfTest`).
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda([WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, A.Delay), false);
		// t = delay + 2: прогон.
		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, Gate, A]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			APlayerController* PCtl = Wd->GetFirstPlayerController();
			AAsdaPlayerCharacter* Me = PCtl ? Cast<AAsdaPlayerCharacter>(PCtl->GetPawn()) : nullptr;
			const bool bQuit = A.bQuit;
			if (!Me) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа")); return; }
			TSharedRef<FState> SP = MakeShared<FState>();
			SP->W = Wd; SP->Me = Me; SP->bQuit = bQuit; SP->Cases = BuildCases(Wd, A);
			SP->HomeGame = Me->GetGamePosXZ();
			if (SP->Cases.Num() == 0) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет транспорта со шлейфом (DT_special_itemlist/DT_RideEffect)")); return; }
			// Дальний носитель — на байке 0012 и велосипеде 0018 (одноразовые шлейфы с квадами и частицами).
			for (int32 i = 0; i < SP->Cases.Num(); ++i)
			{
				if (!SP->Cases[i].bAttached && IsReproCase(SP->Cases[i])) { SP->PeerCases.Add(i); }
			}
			FString List;
			for (const FCase& Cs : SP->Cases) { List += FString::Printf(TEXT(" %d:%s"), Cs.Code, *Cs.Fx); }
			UE_LOG(LogAsdaMountFxTest, Log, TEXT("%s транспорты:%s"), kPrefix, *List);
			// Гейт значимости и свет эффектов — штатно включены (проверяем их взаимодействие с носителем).
			AsdaCVarSave::SetForTest(TEXT("asda.Vis.Enable"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Vis.Effects"), 1);
			UE_LOG(LogAsdaMountFxTest, Log, TEXT("%s старт: транспортов %d, дальний носитель на %d из них"), kPrefix, SP->Cases.Num(), SP->PeerCases.Num());
			SP->Phase = EPhase::Mount; SP->PhaseStart = Wd->GetTimeSeconds();
			SP->Handle = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
		}), A.Delay + 2.f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaMountFxSelfTest(
	TEXT("asda.Mount.FxSelfTest"),
	TEXT("Самотест эффектов транспорта: шлейф/свет едут вместе с носителем в том же кадре (стоянка, круг, рывки, развороты; "
	     "дальний носитель в кадре и за камерой), ключи asda.Fx.FollowFullRate / asda.Fx.Light.CarrierAnchor 0 и 1. "
	     "Только офлайн. `delay=N` `quit` [коды транспорта…]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaMountFxTest::Run(A, W);
	}));


// ═══ ★★★ БЕНЧ ЦЕНЫ `asda.Mount.FxBench [всадников=30] [quit]` (ревизия 15.10.2026, review_PORT_mobpos_mountfx.md п. 4) ═══
//   ЗАЧЕМ. `asda.Fx.LoopBench` гонит SimulateStep МИМО гейта значимости — цену правки гейта он не видит. Здесь — живой
//   путь: N соседей-заглушек в кадре дальше 15 м (значимость 1), каждый на байке 0012 (шлейф 42 эмиттера), со свечением
//   оружия (BaseSkel/8_a1) и крыльями (кость M). Фазы по 3 с замера: ХОД ключ 0 → ХОД ключ 1 → СТОЯНКА ключ 0 → СТОЯНКА
//   ключ 1 (`asda.Fx.FollowFullRate`). Счётчики — `AsdaFxBenchCounters` (такты Tick всех эффектов, число Tick и шагов).
//   ПРОВЕРКИ: на стоянке ключ 1 шагов/кадр не больше, чем ключ 0 (экономия стоячих носителей сохранена); эффекты
//   соседей действительно в значимости 1. На ходу ключ 1 — цена корректности (печатается).
namespace AsdaMountFxBench
{
	static const TCHAR* const kCmdB = TEXT("asda.Mount.FxBench");
	static const TCHAR* const kPrefixB = TEXT("БЕНЧ-ТРАНСПОРТ-FX");
	constexpr int32 kBikeCode = 22;           // первый код с шлейфом itm1_n_96_vehicle_0012_e (42 эмиттера)
	constexpr int32 kUserBase = 7800;         // номера соседей бенча
	constexpr float kMeasureSec = 3.f;        // замер фазы, с

	struct FPhaseRes { double MsPerFrame = 0.0, StepsPerFrame = 0.0, TicksPerFrame = 0.0; int32 Frames = 0; int32 Vis[3] = {0, 0, 0}; };

	struct FBench
	{
		TWeakObjectPtr<UWorld> W;
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;
		TArray<TWeakObjectPtr<AAsdaPlayerCharacter>> Peers;
		TArray<TWeakObjectPtr<AAsdaEffectActor>> Extra;   // свечения оружия и крылья (живут весь бенч)
		int32 N = 30;
		int32 Phase = 0;                   // 0 подготовка, 1 ход/0, 2 ход/1, 3 стоянка/0, 4 стоянка/1, 5 конец
		double PhaseStart = 0.0, SettleSec = 1.0;
		bool bMeasuring = false, bQuit = false;
		uint64 C0 = 0, T0 = 0, S0 = 0; int32 F = 0;
		FPhaseRes Res[5];
		int32 Ok = 0, Fail = 0;
		FDelegateHandle Handle;
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaMountFxTest, Log, TEXT("%s ok   %s"), kPrefixB, *What); }
			else   { ++Fail; UE_LOG(LogAsdaMountFxTest, Error, TEXT("%s FAIL %s"), kPrefixB, *What); }
		}
	};

	static void Setup(FBench& B, UWorld* W)
	{
		AAsdaPlayerCharacter* Me = B.Me.Get();
		UAsdaWorldSubsystem* WS = W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		APlayerController* PCtl = W->GetFirstPlayerController();
		if (!Me || !WS || !PCtl || !PCtl->PlayerCameraManager) { B.Expect(false, TEXT("нет своего персонажа/мира/камеры")); B.Phase = 5; return; }
		FVector Fwd = PCtl->PlayerCameraManager->GetCameraRotation().Vector(); Fwd.Z = 0.f; Fwd = Fwd.GetSafeNormal();
		const FVector Rgt = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
		for (int32 i = 0; i < B.N; ++i)
		{
			const FVector P = Me->GetActorLocation() + Fwd * (2200.f + 350.f * (i / 6)) + Rgt * (350.f * ((i % 6) - 2.5f));
			const int32 Uid = kUserBase + i;
			WS->DebugSpawnFakePeer(Uid, FString::Printf(TEXT("БенчВсадник%d"), i), AsdaCoord::UeToGame2D(P));
			AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(Uid));
			if (!Ch) { continue; }
			FString Fx;
			AsdaMountFxTest::MountOn(W, Ch, kBikeCode, Fx);
			// Свечение оружия и крылья — без срока жизни (как в мире: живут, пока надеты).
			if (AAsdaEffectActor* G = AAsdaEffectActor::SpawnByJebName(W, TEXT("ohs_a_co_ma_01"), Ch->GetActorLocation(), FRotator::ZeroRotator, Ch))
			{
				G->ConfigureBoneAttach(Ch, Ch->BaseSkel, FName(TEXT("8_a1")), EAsdaFxAttachMode::WeaponGlow);
				B.Extra.Add(G);
			}
			if (AAsdaEffectActor* Wg = AAsdaEffectActor::SpawnByJebName(W, TEXT("eff_wing_m_ss_00"), Ch->GetActorLocation(), FRotator::ZeroRotator, Ch))
			{
				Wg->ConfigureBoneAttach(Ch, nullptr, FName(TEXT("M")), EAsdaFxAttachMode::Wing);
				B.Extra.Add(Wg);
			}
			B.Peers.Add(Ch);
		}
		B.Expect(B.Peers.Num() == B.N, FString::Printf(TEXT("соседей-всадников поставлено %d из %d"), B.Peers.Num(), B.N));
	}

	static void BeginPhase(FBench& B, int32 P, double Now)
	{
		B.Phase = P; B.PhaseStart = Now; B.bMeasuring = false; B.F = 0;
		const bool bOn = (P == 2 || P == 4);
		AsdaCVarSave::SetForTest(TEXT("asda.Fx.FollowFullRate"), bOn ? 1 : 0);
		// Стоянка после хода: дождаться, пока догорят шлейфы (жизнь 3 с) — меряем стоячих носителей, а не хвосты хода.
		B.SettleSec = (P == 3) ? 3.5 : 1.0;
		if (P >= 3) { for (auto& Wp : B.Peers) { if (AAsdaPlayerCharacter* Ch = Wp.Get()) { Ch->StopMove(); } } }
	}

	static void Finish(TSharedRef<FBench> BP)
	{
		FBench& B = *BP;
		UWorld* W = B.W.Get();
		if (B.Handle.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(B.Handle); B.Handle.Reset(); }
		static const TCHAR* kNames[] = { TEXT("—"), TEXT("ход, ключ 0 (было)"), TEXT("ход, ключ 1 (стало)"), TEXT("стоянка, ключ 0"), TEXT("стоянка, ключ 1") };
		for (int32 P = 1; P <= 4; ++P)
		{
			const FPhaseRes& R = B.Res[P];
			UE_LOG(LogAsdaMountFxTest, Log, TEXT("%s инфо %-22s: кадров %d, Tick эффектов %.3f мс/кадр, шагов %.1f/кадр, Tick %.1f/кадр, значимость 0/1/2 = %d/%d/%d"),
				kPrefixB, kNames[P], R.Frames, R.MsPerFrame, R.StepsPerFrame, R.TicksPerFrame, R.Vis[0], R.Vis[1], R.Vis[2]);
		}
		const FPhaseRes &M0 = B.Res[1], &M1 = B.Res[2], &S0 = B.Res[3], &S1 = B.Res[4];
		UE_LOG(LogAsdaMountFxTest, Log, TEXT("%s инфо цена полного темпа на ходу: +%.3f мс/кадр на %d всадников (+%.1f мкс на всадника), шагов +%.1f/кадр"),
			kPrefixB, M1.MsPerFrame - M0.MsPerFrame, B.N, B.N > 0 ? (M1.MsPerFrame - M0.MsPerFrame) * 1000.0 / B.N : 0.0, M1.StepsPerFrame - M0.StepsPerFrame);
		B.Expect(S1.Frames > 30 && S1.StepsPerFrame <= S0.StepsPerFrame * 1.05 + 0.5,
			FString::Printf(TEXT("стоячие носители — прежняя экономия: шагов/кадр ключ 1 %.1f против ключ 0 %.1f; Tick %.3f против %.3f мс/кадр"),
				S1.StepsPerFrame, S0.StepsPerFrame, S1.MsPerFrame, S0.MsPerFrame));
		B.Expect(M1.Vis[1] > (M1.Vis[0] + M1.Vis[2]),
			FString::Printf(TEXT("эффекты всадников в значимости 1 (кадр, дальше 15 м): 0/1/2 = %d/%d/%d"), M1.Vis[0], M1.Vis[1], M1.Vis[2]));
		B.Expect(M1.StepsPerFrame > M0.StepsPerFrame,
			FString::Printf(TEXT("на ходу ключ 1 шагает каждый кадр: шагов/кадр %.1f против %.1f у ключа 0"), M1.StepsPerFrame, M0.StepsPerFrame));
		for (auto& E : B.Extra) { if (AAsdaEffectActor* Fx = E.Get()) { Fx->Destroy(); } }
		for (auto& Wp : B.Peers) { if (AAsdaPlayerCharacter* Ch = Wp.Get()) { Ch->StopMove(); if (Ch->IsMounted()) { Ch->ClearMount(); } Ch->Destroy(); } }
		AsdaSelfTestKit::Finish(W, kCmdB, B.bQuit, LogAsdaMountFxTest, kPrefixB, B.Ok, B.Fail);
	}

	static void Tick(TSharedRef<FBench> BP, UWorld* InW)
	{
		FBench& B = *BP;
		UWorld* W = B.W.Get();
		if (!W || InW != W) { return; }
		const double Now = W->GetTimeSeconds();
		if (B.Phase == 0) { Setup(B, W); if (B.Phase == 5) { Finish(BP); return; } BeginPhase(B, 1, Now); return; }
		if (B.Phase >= 5) { Finish(BP); return; }
		// Ход: каждый сосед по своему кругу (фаза угла разная — не синхронно).
		if (B.Phase <= 2)
		{
			for (int32 i = 0; i < B.Peers.Num(); ++i)
			{
				if (AAsdaPlayerCharacter* Ch = B.Peers[i].Get())
				{
					const double A = FMath::DegreesToRadians(115.0 * (Now - B.PhaseStart) + 37.0 * i);
					const FVector2D D((float)FMath::Cos(A), (float)FMath::Sin(A));
					Ch->SetMoveDirection(D, 0.6f, D, false);
				}
			}
		}
		const double T = Now - B.PhaseStart;
		if (!B.bMeasuring && T >= B.SettleSec)
		{
			B.bMeasuring = true; B.F = 0;
			AsdaFxBenchCounters(B.C0, B.T0, B.S0);
			B.Res[B.Phase] = FPhaseRes();
			return;
		}
		if (!B.bMeasuring) { return; }
		++B.F;
		// Значимость эффектов соседей (выборка раз в 10 кадров).
		if ((B.F % 10) == 0)
		{
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
			{
				const AActor* Car = It->FollowActor ? It->FollowActor.Get() : It->GetOwner();
				if (!It->IsCarrierFollower() || !Car) { continue; }
				if (B.Peers.ContainsByPredicate([Car](const TWeakObjectPtr<AAsdaPlayerCharacter>& Wp) { return Wp.Get() == Car; }))
				{
					B.Res[B.Phase].Vis[FMath::Clamp((int32)It->GetVisLevel(), 0, 2)]++;
				}
			}
		}
		if (T >= B.SettleSec + kMeasureSec)
		{
			uint64 C1 = 0, T1 = 0, S1 = 0;
			AsdaFxBenchCounters(C1, T1, S1);
			FPhaseRes& R = B.Res[B.Phase];
			R.Frames = B.F;
			R.MsPerFrame = FPlatformTime::ToMilliseconds64(C1 - B.C0) / FMath::Max(B.F, 1);
			R.StepsPerFrame = double(S1 - B.S0) / FMath::Max(B.F, 1);
			R.TicksPerFrame = double(T1 - B.T0) / FMath::Max(B.F, 1);
			BeginPhase(B, B.Phase + 1, Now);
		}
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmdB, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmdB, A)) { return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmdB);
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmdB, A.bQuit);
		TWeakObjectPtr<UWorld> WW(W);
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda([WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, A.Delay), false);
		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, Gate, A]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			APlayerController* PCtl = Wd->GetFirstPlayerController();
			AAsdaPlayerCharacter* Me = PCtl ? Cast<AAsdaPlayerCharacter>(PCtl->GetPawn()) : nullptr;
			if (!Me) { AsdaSelfTestKit::Refused(Wd, kCmdB, A.bQuit, TEXT("нет своего персонажа")); return; }
			TSharedRef<FBench> BP = MakeShared<FBench>();
			BP->W = Wd; BP->Me = Me; BP->bQuit = A.bQuit; BP->N = FMath::Clamp(A.Int(0, 30), 1, 60);
			AsdaCVarSave::SetForTest(TEXT("asda.Vis.Enable"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Vis.Effects"), 1);
			UE_LOG(LogAsdaMountFxTest, Log, TEXT("%s старт: всадников %d (байк, свечение оружия, крылья), значимость 1"), kPrefixB, BP->N);
			BP->Handle = FWorldDelegates::OnWorldPostActorTick.AddLambda([BP](UWorld* InW, ELevelTick, float) { Tick(BP, InW); });
		}), A.Delay + 2.f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaMountFxBench(
	TEXT("asda.Mount.FxBench"),
	TEXT("Бенч цены эффектов на дальних носителях (живой путь гейта значимости): N соседей на байке со свечением оружия и крыльями, "
	     "ход/стоянка × asda.Fx.FollowFullRate 0/1, мс Tick эффектов и шагов на кадр. Только офлайн. [всадников=30] `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaMountFxBench::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
