// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ КАМЕРЫ: ПРЕПЯТСТВИЯ И ЗЕМЛЯ (25.10.2026, волна camera). [ОРИГИНАЛ]
//
// ★ ЧТО ПРОВЕРЯЕТ. Правило камеры оригинала (`cameraUpdate@0x959880`, разбор — `World/AsdaCameraArmComponent.h`):
//   • другой персонаж, моб, NPC, дерево, стена здания и ящик между камерой и своим персонажем НЕ укорачивают стрелу
//     (`asda.Camera.OriginalNoCollide 1`): фактическая длина = заданной;
//   • глаз не опускается ниже высоты земли `.TER + 60` и смотрит на цель (склон за спиной);
//   • при повороте камеры над склоном глаз движется непрерывно (нет рывков).
//   Контрольные пары (закон pixel-metric-needs-control-pairs): для каждого случая тот же кадр при ключе 0 (прежний порт)
//   и «сырой» сферический запрос по каналу Camera (ровно запрос `USpringArmComponent`) — что он задевает и на каком
//   расстоянии; для объектов карты прежний порт обязан укоротить стрелу (иначе расстановка не перекрывает обзор).
//
// ★ КАК (путь игрока). Офлайн (`-AsdaNoNet`): свой персонаж — `Asda.World.FakeSelfSpawn 256 256`; другой персонаж —
//   `DebugSpawnFakePeer` (путь снапшота 4005); моб — запись 4013 штатному приёмнику; NPC — шаблон уровня, принятый
//   пакетом 4067; дерево/стена/ящик — настоящие статик-меши карты alpen. Угол камеры — `SetOrbitAnglesForBench`
//   (та же точка, что у ПКМ-драга), длина — `TargetArmLength` (как колесо). Своего персонажа ставим так, чтобы
//   препятствие оказалось на отрезке «цель стрелы → глаз».
//   Кадры — `shotdir=<папка>` (по умолчанию `Reports/camera`), имена `cam_<случай>_<new|old>.png`.
//
// Консоль: `asda.Camera.SelfTestOcclusion [delay=N] [shotdir=<папка>] [quit]`. Только офлайн. Строки — «САМОТЕСТ-КАМЕРА»,
//   итог — «САМОТЕСТ-КАМЕРА ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                          // запись полей тела 4067
#include "AsdaCVarSave.h"                       // ключ камеры на время прогона (вернётся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                    // запись 4013 (моб)
#include "Test/AsdaSelfTestKit.h"               // каркас: аргументы, отказ, итог, выход
#include "World/AsdaCameraArmComponent.h"       // стрела с правилом оригинала
#include "World/AsdaCoord.h"                    // игра ↔ UE
#include "World/AsdaGroundTrace.h"              // высота .TER
#include "World/AsdaPlayerController.h"         // SetOrbitAnglesForBench
#include "World/AsdaPlayerCharacter.h"          // свой персонаж
#include "World/AsdaWorldSubsystem.h"           // соседи, мобы
#include "Npc/AsdaMobActor.h"                   // моб
#include "Npc/AsdaNpcActor.h"                   // NPC
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/BodySetup.h"           // перепись: есть ли у меша геометрия для сферы
#include "PhysicsEngine/PhysicsAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"                       // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCameraTest, Log, All);

namespace AsdaCameraTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Camera.SelfTestOcclusion");   // имя самотеста (итог, сторож ключей)
	static const TCHAR* kKey = TEXT("asda.Camera.OriginalNoCollide");   // ключ правила камеры (свой, меняется на прогон)
	constexpr int32 kPeerUser = 93;        // userNum соседа-заглушки (вне диапазона живой сессии офлайна)
	constexpr int32 kMobRid = 29101;       // renderId моба-заглушки
	constexpr int32 kNpcRid = 29102;       // renderId принятого NPC
	constexpr int32 kMobType = 1;          // тип моба (Mob list: волк alpen) — как у прочих самотестов
	constexpr int32 kDecorRid = 29103;     // renderId моба-«декорации» (статик-фолбэк: дерево/камень-моб)
	constexpr float kProbeR = 12.f;        // радиус сферы `USpringArmComponent::ProbeSize` (по умолчанию движка)
	constexpr float kLenTol = 2.f;         // допуск длины стрелы, юниты UE
	constexpr double kTickS = 0.05;        // шаг таймера прогона

	// Вид препятствия.
	enum class EKind : uint8 { Peer, Mob, Decor, Npc, Tree, Wall, Box };

	// Один случай «препятствие между камерой и персонажем».
	struct FCase
	{
		FString Name;                                  // имя случая (лог и кадр)
		EKind Kind = EKind::Peer;                      // вид препятствия
		TWeakObjectPtr<AActor> Actor;                  // препятствие-актор (сосед/моб/NPC) или владелец меша карты
		TWeakObjectPtr<UPrimitiveComponent> Comp;      // меш карты (для объектов карты)
		int32 Item = -1;                               // экземпляр ISM/HISM (−1 — обычный меш)
		FBox Box = FBox(ForceInit);                    // габарит препятствия (мир)
		bool bFound = false;                           // препятствие есть и расставлено
		bool bGeom = false;                            // у препятствия есть геометрия для сферы прежней стрелы (иначе она сквозь него)
		int32 DecorType = 0;                           // тип моба-«декорации» (Decor)
		FVector P = FVector::ZeroVector;               // позиция своего персонажа (корень)
		float Yaw = 0.f, Pitch = -20.f, Len = 700.f;   // угол стрелы и её длина
		float Lower = 0.f;                             // насколько персонаж опущен (низкий ящик на ровном месте)
		float NewLen = -1.f, OldLen = -1.f;            // фактическая длина при ключе 1 / 0
		bool bNewLift = false;                         // при ключе 1 сработал подъём над землёй
		FString SweepHit;                              // что задевает сфера по каналу Camera (как прежняя стрела)
		float SweepDist = -1.f;                        // расстояние до этого попадания от цели стрелы
	};

	// Замер стрелы в текущем кадре.
	struct FMeasure
	{
		FVector Origin = FVector::ZeroVector;   // цель стрелы (точка вращения)
		FVector Cam = FVector::ZeroVector;      // фактический глаз
		FVector Ideal = FVector::ZeroVector;    // глаз без препятствий и без подъёма
		float Actual = 0.f;                     // |глаз − цель|
		float GroundZ = NAN;                    // .TER под глазом
		bool bLift = false;                     // подъём над землёй в этом кадре
		float Lift = 0.f;                       // величина подъёма
		float LookErrDeg = 0.f;                 // отклонение взгляда камеры от направления на цель, градусы
	};

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> World;          // мир прогона
		FTimerHandle Tick;                     // таймер шага
		int32 Ok = 0, Fail = 0;                // счётчики
		bool bQuit = false;                    // выйти по итогу
		FString ShotDir;                       // папка кадров
		TArray<FCase> Cases;                   // случаи препятствий
		int32 CaseIdx = 0;                     // текущий случай
		int32 Step = 0;                        // шаг внутри случая / фазы
		double StepS = 0.0;                    // время начала шага
		double DeadlineS = 0.0;                // потолок прогона
		FVector Home = FVector::ZeroVector;    // точка спавна своего персонажа (поиск объектов карты — от неё)
		bool bHomeSet = false;                 // точка спавна записана
		TWeakObjectPtr<AAsdaMobActor> Mob;     // моб-заглушка (удаляется в конце)
		TWeakObjectPtr<AAsdaMobActor> Decor;   // моб-«декорация» (удаляется в конце)
		// Склон.
		bool bSlopeFound = false;              // склон найден
		FVector SlopeP = FVector::ZeroVector;  // позиция персонажа у склона
		float SlopeYaw = 0.f, SlopePitch = -6.f;   // угол стрелы к склону
		float SweepYaw0 = 0.f;                 // начальный yaw качания
		int32 SweepN = 0;                      // шаг качания
		FVector PrevCam = FVector::ZeroVector; // глаз прошлого шага качания
		float PrevLift = 0.f;                  // подъём прошлого шага
		float MaxJump = 0.f, MaxLiftJump = 0.f;     // наибольший шаг глаза / подъёма за шаг качания (ключ 1)
		float MaxJumpOld = 0.f;                // то же при ключе 0 (сравнение)
		int32 Phase = 0;                       // 0 — ожидание персонажа, 1 — случаи, 2 — склон, 3 — качание, 4 — итог

		// Отметка проверки.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА [ok]   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaCameraTest, Error, TEXT("САМОТЕСТ-КАМЕРА [FAIL] %s"), *What); }
		}
		UGameInstance* GI() const { return World.IsValid() ? World->GetGameInstance() : nullptr; }                 // экземпляр игры
		UAsdaWorldSubsystem* Ws() const { return GI() ? GI()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }    // мир порта
		UAsdaNetSubsystem* Net() const { return GI() ? GI()->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }        // сеть (приёмник)
		AAsdaPlayerController* Pc() const                                                                         // контроллер
		{ return World.IsValid() ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = Pc(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }   // свой персонаж
		UAsdaCameraArmComponent* Arm() const { AAsdaPlayerCharacter* C = Ch(); return C ? Cast<UAsdaCameraArmComponent>(C->CameraArm.Get()) : nullptr; }   // стрела
		void Next() { ++Step; StepS = FPlatformTime::Seconds(); }   // следующий шаг
	};

	// Высота .TER (NaN — нет данных).
	static float Ground(UWorld* W, const FVector2D& P) { return AsdaGround::GroundZ(W, (float)P.X, (float)P.Y, NAN); }

	// Ключ правила камеры на время прогона.
	static void SetMode(int32 V) { AsdaCVarSave::SetForTest(kKey, V); }

	// Есть ли у компонента геометрия, которую видит запрос-сфера (простая коллизия или «сложная как простая»).
	static bool HasSweepGeom(const UPrimitiveComponent* P)
	{
		if (const UStaticMeshComponent* Sm = Cast<UStaticMeshComponent>(P))
		{
			const UBodySetup* Bs = Sm->GetStaticMesh() ? Sm->GetStaticMesh()->GetBodySetup() : nullptr;
			return Bs && (Bs->AggGeom.GetElementCount() > 0 || Bs->CollisionTraceFlag == CTF_UseComplexAsSimple);
		}
		if (const USkeletalMeshComponent* Sk = Cast<USkeletalMeshComponent>(P)) { return Sk->GetPhysicsAsset() != nullptr; }
		return P != nullptr;   // примитивы-формы (капсула, бокс, сфера) — всегда
	}

	// Габарит препятствия-актора: объединение видимых мешей (без плашек и хитбоксов).
	static FBox MeshBox(const AActor* A)
	{
		FBox B(ForceInit);
		if (!A) { return B; }
		TInlineComponentArray<UMeshComponent*> Ms(A);
		for (UMeshComponent* M : Ms)
		{
			if (!M || !M->IsVisible() || !M->IsRegistered()) { continue; }
			if (!M->IsA<USkeletalMeshComponent>() && !M->IsA<UStaticMeshComponent>()) { continue; }
			B += M->Bounds.GetBox();
		}
		return B;
	}

	// Описание попадания: компонент, владелец, меш, профиль коллизии, отклик на канал Camera.
	static FString HitText(const FHitResult& H)
	{
		const UPrimitiveComponent* C = H.GetComponent();
		if (!C) { return TEXT("—"); }
		FString Mesh;
		if (const UStaticMeshComponent* S = Cast<UStaticMeshComponent>(C)) { Mesh = S->GetStaticMesh() ? S->GetStaticMesh()->GetName() : TEXT("?"); }
		if (const USkeletalMeshComponent* K = Cast<USkeletalMeshComponent>(C)) { Mesh = K->GetSkeletalMeshAsset() ? K->GetSkeletalMeshAsset()->GetName() : TEXT("?"); }
		return FString::Printf(TEXT("%s.%s [%s, меш %s, профиль %s, экз %d]"),
			C->GetOwner() ? *C->GetOwner()->GetName() : TEXT("?"), *C->GetName(), *C->GetClass()->GetName(), *Mesh,
			*C->GetCollisionProfileName().ToString(), H.Item);
	}

	// Сфера по каналу Camera от цели к глазу — ровно запрос прежней стрелы (`USpringArmComponent::UpdateDesiredArmLocation`).
	static bool CameraSweep(UWorld* W, const AActor* Self, const FVector& From, const FVector& To, FHitResult& Out)
	{
		FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaCameraSelfTest), false, Self);
		return W->SweepSingleByChannel(Out, From, To, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(kProbeR), Q);
	}

	// Попадание — в препятствие случая.
	static bool HitIsObstacle(const FCase& C, const FHitResult& H)
	{
		if (C.Comp.IsValid()) { return H.GetComponent() == C.Comp.Get() && (C.Item < 0 || H.Item == C.Item); }
		return C.Actor.IsValid() && H.GetActor() == C.Actor.Get();
	}

	// Замер стрелы.
	static FMeasure Measure(FState& S)
	{
		FMeasure M;
		AAsdaPlayerCharacter* C = S.Ch();
		UAsdaCameraArmComponent* A = S.Arm();
		if (!C || !A || !C->Camera) { return M; }
		M.Origin = A->GetLastArmOrigin();
		M.Cam = C->Camera->GetComponentLocation();
		M.Ideal = M.Origin - A->GetComponentRotation().Vector() * A->TargetArmLength;
		M.Actual = (float)FVector::Dist(M.Cam, M.Origin);
		M.GroundZ = Ground(S.World.Get(), FVector2D(M.Cam.X, M.Cam.Y));
		M.bLift = A->WasGroundLiftedLastUpdate();
		M.Lift = A->GetLastGroundLift();
		const FVector ToTarget = (M.Origin - M.Cam).GetSafeNormal();
		const FVector Fwd = C->Camera->GetForwardVector();
		M.LookErrDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((float)FVector::DotProduct(ToTarget, Fwd), -1.f, 1.f)));
		return M;
	}

	// Поставить персонажа и камеру случая (повторяется каждый шаг: стоящего персонажа ничто не обязано держать на месте).
	static void Apply(FState& S, const FVector& P, float Yaw, float Pitch, float Len)
	{
		AAsdaPlayerCharacter* C = S.Ch();
		AAsdaPlayerController* Pc = S.Pc();
		if (!C || !Pc) { return; }
		C->SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
		// Угол стрелы задаётся относительно корня персонажа — мировой yaw переводим в относительный.
		Pc->SetOrbitAnglesForBench(FRotator::NormalizeAxis(Yaw - (float)C->GetActorRotation().Yaw), Pitch);
		if (C->CameraArm) { C->CameraArm->TargetArmLength = Len; }
	}

	// Причины отказа расстановки (диагностика): 0 — отрезок мимо габарита, 1 — сфера не задела ничего, 2 — задела другое.
	static int32 GRej[3] = { 0, 0, 0 };
	static FString GRejHit;   // последнее «другое» попадание

	// Расстановка: найти позицию персонажа, с которой препятствие лежит на отрезке «цель стрелы → глаз», а сфера
	//   прежней стрелы задевает его первым (для объектов карты) или ничего перед ним (для сущностей).
	static bool Place(FState& S, FCase& K)
	{
		UWorld* W = S.World.Get();
		AAsdaPlayerCharacter* Self = S.Ch();
		if (!W || !Self || !K.Box.IsValid) { return false; }
		const FVector Ext = K.Box.GetExtent();
		const FVector Ctr = K.Box.GetCenter();
		// Точка прицела: середина габарита по высоте, но для высоких объектов — не выше 3 м над низом (ствол/стена).
		FVector Aim = Ctr;
		Aim.Z = FMath::Min((double)Ctr.Z, K.Box.Min.Z + FMath::Max(80.0, FMath::Min((double)Ext.Z, 300.0)));
		const float Hs[] = { 350.f, 450.f, 550.f, 700.f };
		for (int32 Pass = 0; Pass < 2; ++Pass)   // 0 — без опускания персонажа, 1 — с опусканием до 150 (низкий ящик)
		{
			for (const float H : Hs)
			{
				for (int32 k = 0; k < 16; ++k)
				{
					const float Yd = k * 22.5f;
					const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(Yd)), FMath::Sin(FMath::DegreesToRadians(Yd)));
					const FVector2D P2 = FVector2D(Aim.X, Aim.Y) - Dir * (H + FMath::Max(Ext.X, Ext.Y) * 0.5f);
					if (K.Box.ExpandBy(80.f).IsInsideXY(FVector(P2.X, P2.Y, 0.f))) { continue; }   // не внутри препятствия
					const float G = Ground(W, P2);
					if (!FMath::IsFinite(G)) { continue; }
					float Lower = 0.f;
					FVector O(P2.X, P2.Y, G + 120.f);
					const float Hd = (float)FVector2D::Distance(P2, FVector2D(Aim.X, Aim.Y));
					float Need = Hd * FMath::Tan(FMath::DegreesToRadians(10.f));   // подъём прицела над целью для угла 10°
					if (Aim.Z - O.Z < Hd * FMath::Tan(FMath::DegreesToRadians(6.f)))
					{
						if (Pass == 0) { continue; }
						Lower = (float)(O.Z - (Aim.Z - Need));
						if (Lower > 150.f || Lower < 0.f) { continue; }
						O.Z -= Lower;
					}
					const FVector D = Aim - O;
					const float Dl = (float)D.Size();
					const float PitchUp = FMath::RadiansToDegrees(FMath::Asin((float)D.Z / Dl));
					if (PitchUp < 6.f || PitchUp > 70.f) { continue; }
					const float Len = FMath::Clamp(Dl + (float)Ext.Size2D() + 200.f, 170.f, 1270.f);
					if (Len < Dl + 120.f) { continue; }   // глаз обязан стоять ЗА препятствием
					const FVector Eye = O + D / Dl * Len;
					const float Ge = Ground(W, FVector2D(Eye.X, Eye.Y));
					if (FMath::IsFinite(Ge) && Ge + UAsdaCameraArmComponent::kGroundClearance > Eye.Z - 10.f) { continue; }   // без подъёма
					FVector HitP, HitN; float HitT;
					if (!FMath::LineExtentBoxIntersection(K.Box, O, Eye, FVector::ZeroVector, HitP, HitN, HitT)) { ++GRej[0]; continue; }
					FHitResult Hr;
					const bool bHit = CameraSweep(W, Self, O, Eye, Hr);
					if (K.bGeom && !bHit) { ++GRej[1]; continue; }                         // сфера не задела ничего
					if (K.bGeom && !HitIsObstacle(K, Hr)) { ++GRej[2]; GRejHit = HitText(Hr); continue; }   // задела другое раньше
					if (!K.bGeom && bHit && Hr.Distance < Dl) { ++GRej[2]; GRejHit = HitText(Hr); continue; }   // перед препятствием чужая помеха
					const FRotator R = (O - Aim).Rotation();
					K.P = FVector(P2.X, P2.Y, G - Lower);
					K.Yaw = (float)R.Yaw; K.Pitch = (float)R.Pitch; K.Len = Len; K.Lower = Lower;
					return true;
				}
			}
		}
		return false;
	}

	// Объект карты вида Kind, ближайший к точке спавна (только блокирующие канал Camera — иначе контрольной пары нет).
	static bool FindLevelObstacle(FState& S, FCase& K)
	{
		UWorld* W = S.World.Get();
		if (!W) { return false; }
		struct FCand { UStaticMeshComponent* C; int32 Item; FBox Box; double D2; };
		TArray<FCand> Cands;
		auto Want = [&K](const FString& N, const FBox& B) -> bool
		{
			const FVector Sz = B.GetSize();
			switch (K.Kind)
			{
			case EKind::Tree: return N.Contains(TEXT("tree")) && Sz.Z >= 300.0;
			case EKind::Wall: return (N.Contains(TEXT("shop")) || N.Contains(TEXT("tower"))) && !N.Contains(TEXT("sos"))
				&& FMath::Max(Sz.X, Sz.Y) >= 500.0 && Sz.Z >= 300.0;
			case EKind::Box:  return (N.Contains(TEXT("bpx")) || N.Contains(TEXT("box")) || N.Contains(TEXT("milk"))) && Sz.Z <= 400.0;
			default: return false;
			}
		};
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			TInlineComponentArray<UStaticMeshComponent*> Ms(*It);
			for (UStaticMeshComponent* M : Ms)
			{
				if (!M || !M->GetStaticMesh() || !M->IsRegistered() || !M->IsVisible()) { continue; }
				if (!M->IsQueryCollisionEnabled() || M->GetCollisionResponseToChannel(ECC_Camera) != ECR_Block) { continue; }
				const FString N = M->GetStaticMesh()->GetName().ToLower();
				if (N.Contains(TEXT("terrain")) || N.Contains(TEXT("sky")) || N.Contains(TEXT("water"))) { continue; }
				if (UInstancedStaticMeshComponent* I = Cast<UInstancedStaticMeshComponent>(M))
				{
					const FBox Mb = M->GetStaticMesh()->GetBounds().GetBox();
					for (int32 i = 0; i < I->GetInstanceCount(); ++i)
					{
						FTransform T;
						if (!I->GetInstanceTransform(i, T, true)) { continue; }
						const FBox B = Mb.TransformBy(T);
						if (!Want(N, B)) { continue; }
						Cands.Add({ M, i, B, FVector::DistSquared2D(B.GetCenter(), S.Home) });
					}
				}
				else
				{
					const FBox B = M->Bounds.GetBox();
					if (!Want(N, B)) { continue; }
					Cands.Add({ M, -1, B, FVector::DistSquared2D(B.GetCenter(), S.Home) });
				}
			}
		}
		// С геометрией для сферы — вперёд (только у них прежний порт мог укорачивать стрелу), затем по удалению.
		Cands.Sort([](const FCand& A, const FCand& B)
		{
			const bool Ga = HasSweepGeom(A.C), Gb = HasSweepGeom(B.C);
			return Ga != Gb ? Ga : A.D2 < B.D2;
		});
		// У запроса-сферы попадание возможно, только если у меша есть простая коллизия или «сложная как простая».
		int32 NWithGeom = 0;
		for (const FCand& Cd : Cands)
		{
			const UBodySetup* Bs = Cd.C->GetStaticMesh() ? Cd.C->GetStaticMesh()->GetBodySetup() : nullptr;
			if (Bs && (Bs->AggGeom.GetElementCount() > 0 || Bs->CollisionTraceFlag == CTF_UseComplexAsSimple)) { ++NWithGeom; }
		}
		UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА %s: кандидатов на карте %d, из них с геометрией для сферы %d"), *K.Name, Cands.Num(), NWithGeom);
		GRej[0] = GRej[1] = GRej[2] = 0; GRejHit.Reset();
		for (int32 i = 0; i < Cands.Num() && i < 40; ++i)
		{
			K.Comp = Cands[i].C; K.Item = Cands[i].Item; K.Box = Cands[i].Box; K.Actor = Cands[i].C->GetOwner(); K.bGeom = HasSweepGeom(Cands[i].C);
			if (Place(S, K))
			{
				UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА %s: %s экз %d, габарит %s, до спавна %.0f, геометрия для сферы: %s"),
					*K.Name, *Cands[i].C->GetStaticMesh()->GetName(), K.Item, *K.Box.GetSize().ToString(), FMath::Sqrt(Cands[i].D2),
					K.bGeom ? TEXT("есть") : TEXT("НЕТ"));
				return true;
			}
		}
		UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА %s: отказы расстановки — мимо габарита %d, сфера не задела ничего %d, задела другое %d (%s)"),
			*K.Name, GRej[0], GRej[1], GRej[2], *GRejHit);
		K.Comp = nullptr; K.Actor = nullptr;
		return false;
	}

	// Принять ближайший к спавну шаблон NPC пакетом 4067 (как сервер при входе в зону видимости).
	static void FeedNearestNpc(FState& S)
	{
		UWorld* W = S.World.Get();
		UAsdaNetSubsystem* Net = S.Net();
		if (!W || !Net) { return; }
		AAsdaNpcActor* Best = nullptr; double BestD = TNumericLimits<double>::Max();
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
		{
			if (It->bNetSpawned || It->bDisabled || It->NpcId <= 0) { continue; }
			const double D = FVector::DistSquared2D(It->GetActorLocation(), S.Home);
			if (D < BestD) { BestD = D; Best = *It; }
		}
		if (!Best) { return; }
		TArray<uint8> B; B.SetNumZeroed(108);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)kNpcRid);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)Best->NpcId);
		AsdaBytes::WrI16(B.GetData() + 8, (int16)Best->TileX);
		AsdaBytes::WrI16(B.GetData() + 10, (int16)Best->TileZ);
		Net->DebugFeedPacket(4067, B);
		UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА NPC: 4067 для шаблона %s (NpcId %d)"), *Best->GetName(), Best->NpcId);
	}

	// Принятый NPC (renderId kNpcRid).
	static AAsdaNpcActor* NetNpc(FState& S)
	{
		UWorld* W = S.World.Get();
		if (!W) { return nullptr; }
		for (TActorIterator<AAsdaNpcActor> It(W); It; ++It) { if (It->bNetSpawned && It->NetRenderId == kNpcRid && !It->IsHidden()) { return *It; } }
		return nullptr;
	}

	// Перепись того, что в мире блокирует канал Camera И имеет геометрию для сферы (иначе запрос стрелы сквозь него проходит):
	//   группы «класс владельца / компонент / меш без хвостовых цифр» с числом. Это и есть ответ «что съедает камеру».
	static void CensusCameraBlockers(UWorld* W)
	{
		TMap<FString, int32> Groups;
		int32 NoGeom = 0;
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			TInlineComponentArray<UPrimitiveComponent*> Ps(*It);
			for (UPrimitiveComponent* P : Ps)
			{
				if (!P || !P->IsRegistered() || P->GetCollisionResponseToChannel(ECC_Camera) != ECR_Block) { continue; }
				// Скрытые шаблоны (мобы/NPC уровня) коллизию выключают, но их рантайм-копии наследуют компоненты — считаем отдельно.
				const bool bLive = P->IsQueryCollisionEnabled() && !It->IsHidden();
				bool bGeom = true;
				FString Mesh = TEXT("-");
				if (const UStaticMeshComponent* Sm = Cast<UStaticMeshComponent>(P))
				{
					const UBodySetup* Bs = Sm->GetStaticMesh() ? Sm->GetStaticMesh()->GetBodySetup() : nullptr;
					bGeom = Bs && (Bs->AggGeom.GetElementCount() > 0 || Bs->CollisionTraceFlag == CTF_UseComplexAsSimple);
					Mesh = Sm->GetStaticMesh() ? Sm->GetStaticMesh()->GetName() : TEXT("?");
				}
				else if (const USkeletalMeshComponent* Sk = Cast<USkeletalMeshComponent>(P))
				{
					bGeom = Sk->GetPhysicsAsset() != nullptr;
					Mesh = Sk->GetSkeletalMeshAsset() ? Sk->GetSkeletalMeshAsset()->GetName() : TEXT("?");
				}
				if (!bGeom) { ++NoGeom; continue; }
				while (Mesh.Len() > 1 && (FChar::IsDigit(Mesh[Mesh.Len() - 1]) || Mesh[Mesh.Len() - 1] == TCHAR('_'))) { Mesh.LeftChopInline(1); }
				const FString Key = FString::Printf(TEXT("%s %s / %s[%s,%s] / %s"), bLive ? TEXT("ЖИВОЙ ") : TEXT("шаблон"), *It->GetClass()->GetName(), *P->GetClass()->GetName(),
					*P->GetCollisionProfileName().ToString(), P->IsA<UInstancedStaticMeshComponent>() ? TEXT("ISM") : TEXT("-"), *Mesh);
				Groups.FindOrAdd(Key) += 1;
			}
		}
		Groups.ValueSort([](int32 A, int32 B) { return A > B; });
		UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА ПЕРЕПИСЬ: блокируют Camera с геометрией — групп %d; блокируют без геометрии (сфера их не видит) — %d компонентов"),
			Groups.Num(), NoGeom);
		int32 N = 0;
		for (const TPair<FString, int32>& G : Groups)
		{
			UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА ПЕРЕПИСЬ %5d  %s"), G.Value, *G.Key);
			if (++N >= 40) { break; }
		}
	}

	// Кадр в папку отчёта.
	static void Shot(const FState& S, const FString& Name)
	{
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddUniqueSuffix=*/false);
	}

	// Склон: позиция и угол, при которых глаз на полной длине ушёл бы под землю минимум на 80 (ищем у спавна).
	static bool FindSlope(FState& S)
	{
		UWorld* W = S.World.Get();
		if (!W) { return false; }
		const float Len = 1270.f, Pitch = 6.f;
		float BestScore = TNumericLimits<float>::Max();
		for (int32 gx = -12; gx <= 12; ++gx)
		{
			for (int32 gy = -12; gy <= 12; ++gy)
			{
				const FVector2D P2(S.Home.X + gx * 250.f, S.Home.Y + gy * 250.f);
				const float G = Ground(W, P2);
				if (!FMath::IsFinite(G)) { continue; }
				const FVector O(P2.X, P2.Y, G + 120.f);
				for (int32 k = 0; k < 16; ++k)
				{
					const float Yd = k * 22.5f;
					const FVector U = FRotator(Pitch, Yd, 0.f).Vector();   // от цели к глазу (вверх на 6°)
					const FVector Eye = O + U * Len;
					const float Ge = Ground(W, FVector2D(Eye.X, Eye.Y));
					if (!FMath::IsFinite(Ge)) { continue; }
					const float Lift = Ge + UAsdaCameraArmComponent::kGroundClearance - (float)Eye.Z;
					if (Lift < 80.f || Lift > 700.f) { continue; }
					const float Score = FMath::Abs(Lift - 250.f);
					if (Score < BestScore)
					{
						BestScore = Score;
						const FRotator R = (-U).Rotation();
						S.SlopeP = FVector(P2.X, P2.Y, G); S.SlopeYaw = (float)R.Yaw; S.SlopePitch = (float)R.Pitch;
						S.bSlopeFound = true;
					}
				}
			}
		}
		return S.bSlopeFound;
	}
}

static void AsdaRunCameraOcclusionSelfTest(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaCameraTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, KitArgs.bQuit, TEXT("нет мира")); return; }
	if (AsdaSelfTestKit::RefuseStart(W->GetGameInstance(), kCmd, KitArgs)) { return; }

	TSharedPtr<FState> S = MakeShared<FState>();
	S->World = W;
	S->bQuit = KitArgs.bQuit;
	S->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/camera")));
	for (const FString& A : Args)
	{
		if (A.StartsWith(TEXT("shotdir="), ESearchCase::IgnoreCase)) { S->ShotDir = A.Mid(8); }
	}
	FPaths::NormalizeDirectoryName(S->ShotDir);
	IFileManager::Get().MakeDirectory(*S->ShotDir, true);
	const float Delay = KitArgs.bHasDelay ? KitArgs.Delay : 6.f;
	S->StepS = FPlatformTime::Seconds() + Delay;
	S->DeadlineS = S->StepS + 240.0;
	UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА старт (delay=%.1f, кадры → %s)"), Delay, *S->ShotDir);

	auto AddCase = [&S](const TCHAR* Name, EKind Kind) { FCase C; C.Name = Name; C.Kind = Kind; S->Cases.Add(C); };
	AddCase(TEXT("peer"), EKind::Peer);
	AddCase(TEXT("mob"), EKind::Mob);
	AddCase(TEXT("decor"), EKind::Decor);
	AddCase(TEXT("npc"), EKind::Npc);
	AddCase(TEXT("tree"), EKind::Tree);
	AddCase(TEXT("wall"), EKind::Wall);
	AddCase(TEXT("box"), EKind::Box);

	TWeakObjectPtr<UWorld> WW(W);
	TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, KitArgs.bQuit);
	auto GateRefused = [Gate, WW]() { return !Gate->Pass(WW.Get()); };
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(W, [WW, GateRefused]()
		{
			UWorld* Wd = WW.Get();
			if (Wd && GateRefused()) { return; }
			if (Wd && AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, Delay * 0.5f), false);
	}

	TSharedPtr<FState> Keep = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(W, [Keep, WW, GateRefused]()
	{
		FState* St = Keep.Get(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		const double Now = FPlatformTime::Seconds();
		if (Now < St->StepS) { return; }
		if (GateRefused()) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
		auto Finish = [St, Wd]()
		{
			if (AAsdaMobActor* M = St->Mob.Get()) { M->Destroy(); }
			if (AAsdaMobActor* M = St->Decor.Get()) { M->Destroy(); }
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Finish(Wd, kCmd, St->bQuit, LogAsdaCameraTest, TEXT("САМОТЕСТ-КАМЕРА"), St->Ok, St->Fail);
		};
		if (Now > St->DeadlineS)
		{
			St->Expect(false, FString::Printf(TEXT("прогон не уложился в 240 с (фаза %d, случай %d, шаг %d)"), St->Phase, St->CaseIdx, St->Step));
			Finish();
			return;
		}
		UAsdaWorldSubsystem* Ws = St->Ws();
		UAsdaNetSubsystem* Net = St->Net();
		AAsdaPlayerCharacter* Ch = St->Ch();
		UAsdaCameraArmComponent* Arm = St->Arm();
		const double InStep = Now - St->StepS;

		// ── Фаза 0: свой персонаж на месте, стрела — нового класса ──
		if (St->Phase == 0)
		{
			if (!Ch || !Ws || !Net) { return; }
			if (InStep < 3.0) { return; }
			St->Expect(Arm != nullptr, FString::Printf(TEXT("стрела камеры своего персонажа — UAsdaCameraArmComponent (класс %s)"),
				Ch->CameraArm ? *Ch->CameraArm->GetClass()->GetName() : TEXT("нет")));
			if (!Arm) { Finish(); return; }
			St->Home = Ch->GetActorLocation(); St->bHomeSet = true;
			CensusCameraBlockers(Wd);
			AsdaCVarSave::ArmWorldGuard(Wd, kCmd);
			SetMode(1);
			St->Phase = 1; St->CaseIdx = 0; St->Step = 0; St->StepS = Now;
			return;
		}

		// ── Фаза 1: случаи препятствий ──
		if (St->Phase == 1)
		{
			if (St->CaseIdx >= St->Cases.Num()) { St->Phase = 2; St->Step = 0; St->StepS = Now; return; }
			FCase& K = St->Cases[St->CaseIdx];
			auto NextCase = [St, Now]() { ++St->CaseIdx; St->Step = 0; St->StepS = Now; };
			switch (St->Step)
			{
			case 0:   // препятствие: заспавнить / принять / найти на карте
			{
				SetMode(1);
				const FVector2D G = AsdaCoord::UeToGame2D(St->Home);
				if (K.Kind == EKind::Peer) { Ws->DebugSpawnFakePeer(kPeerUser, TEXT("КамераП"), G + FVector2D(300.f, 200.f)); }
				else if (K.Kind == EKind::Mob)
				{
					const FIntPoint T((int32)(G.X / 100.f) + 3, (int32)(G.Y / 100.f) - 2);
					Net->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(kMobRid, kMobType, T, /*State*/1, 100));
				}
				else if (K.Kind == EKind::Decor)
				{
					// Шаблон моба уровня со статик-фолбэком, у меша которого есть геометрия для сферы (дерево/камень-моб).
					for (TActorIterator<AAsdaMobActor> It(Wd); It; ++It)
					{
						const UStaticMeshComponent* Sm = It->StaticMeshComp;
						if (!It->bNetSpawned && It->MobId > 0 && Sm && Sm->GetStaticMesh() && HasSweepGeom(Sm)) { K.DecorType = It->MobId; break; }
					}
					UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА decor: тип моба со статик-мешем с геометрией — %d"), K.DecorType);
					if (K.DecorType <= 0)
					{
						UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА [инфо] decor: на карте нет мобов-декораций с геометрией для сферы — случай пропущен"));
						NextCase();
						return;
					}
					const FIntPoint T((int32)(G.X / 100.f) - 3, (int32)(G.Y / 100.f) + 3);
					Net->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(kDecorRid, K.DecorType, T, /*State*/1, 100));
				}
				else if (K.Kind == EKind::Npc) { FeedNearestNpc(*St); }
				St->Next();
				return;
			}
			case 1:   // дождаться препятствия и расставить
			{
				AActor* A = nullptr;
				if (K.Kind == EKind::Peer) { A = Ws->ActorByUserIdx(kPeerUser); }
				else if (K.Kind == EKind::Mob) { AAsdaMobActor* M = Ws->FindNetMobByRid(kMobRid); St->Mob = M; A = M; }
				else if (K.Kind == EKind::Decor) { AAsdaMobActor* M = Ws->FindNetMobByRid(kDecorRid); St->Decor = M; A = M; }
				else if (K.Kind == EKind::Npc) { A = NetNpc(*St); }
				if (K.Kind == EKind::Peer || K.Kind == EKind::Mob || K.Kind == EKind::Decor || K.Kind == EKind::Npc)
				{
					if (!A || InStep < 1.5)
					{
						if (InStep > 8.0) { St->Expect(false, FString::Printf(TEXT("%s: препятствие не появилось за 8 с"), *K.Name)); NextCase(); }
						return;
					}
					K.Actor = A; K.Box = MeshBox(A);
					// Что у сущности блокирует канал Camera (причина «перекрывают игроки»): перечень компонентов.
					TInlineComponentArray<UPrimitiveComponent*> Ps(A);
					FString Blk;
					UPrimitiveComponent* FirstBlk = nullptr;   // первый блокирующий Camera компонент — прицел через него
					for (UPrimitiveComponent* P : Ps)
					{
						if (P && P->IsRegistered() && P->IsQueryCollisionEnabled() && P->GetCollisionResponseToChannel(ECC_Camera) == ECR_Block)
						{
							Blk += FString::Printf(TEXT(" %s[%s,%s]"), *P->GetName(), *P->GetClass()->GetName(), *P->GetCollisionProfileName().ToString());
							if (!FirstBlk && P->Bounds.SphereRadius > 1.f && HasSweepGeom(P)) { FirstBlk = P; }
						}
					}
					UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА %s: %s, габарит %s; блокируют канал Camera:%s"),
						*K.Name, *A->GetName(), *K.Box.GetSize().ToString(), Blk.IsEmpty() ? TEXT(" ничего") : *Blk);
					// Сначала прицел через блокирующий компонент (если он есть) — проверка «прежний порт укорачивает»;
					//   не вышло — через габарит видимых мешей.
					K.bGeom = FirstBlk != nullptr;
					if (FirstBlk)
					{
						const FBox Keep0 = K.Box;
						K.Box = FirstBlk->Bounds.GetBox();
						K.bFound = Place(*St, K);
						if (!K.bFound) { K.Box = Keep0; K.bGeom = false; }
						else { UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА %s: прицел через %s"), *K.Name, *FirstBlk->GetName()); }
					}
					if (!K.bFound) { K.bFound = K.Box.IsValid && Place(*St, K); }
				}
				else
				{
					K.bFound = FindLevelObstacle(*St, K);
				}
				St->Expect(K.bFound, FString::Printf(TEXT("%s: расстановка — препятствие на отрезке «цель стрелы → глаз»%s"), *K.Name,
					K.Lower > 0.f ? *FString::Printf(TEXT(" (персонаж опущен на %.0f — низкий объект на ровном месте)"), K.Lower) : TEXT("")));
				if (!K.bFound) { NextCase(); return; }
				Apply(*St, K.P, K.Yaw, K.Pitch, K.Len);
				St->Next();
				return;
			}
			case 2:   // ключ 1 (оригинал): выдержка → замер
			{
				Apply(*St, K.P, K.Yaw, K.Pitch, K.Len);
				if (InStep < 0.7) { return; }
				const FMeasure M = Measure(*St);
				K.NewLen = M.Actual; K.bNewLift = M.bLift;
				FHitResult Hr;
				const bool bHit = CameraSweep(Wd, Ch, M.Origin, M.Ideal, Hr);
				K.SweepHit = bHit ? HitText(Hr) : TEXT("ничего");
				K.SweepDist = bHit ? (float)Hr.Distance : -1.f;
				FVector HitP, HitN; float HitT;
				St->Expect(FMath::LineExtentBoxIntersection(K.Box, M.Origin, M.Ideal, FVector::ZeroVector, HitP, HitN, HitT),
					FString::Printf(TEXT("%s: геометрия — отрезок цель→глаз проходит через габарит препятствия (длина %.0f, угол %.1f/%.1f)"),
						*K.Name, K.Len, K.Yaw, K.Pitch));
				St->Expect(FMath::Abs(M.Actual - K.Len) <= kLenTol && !M.bLift,
					FString::Printf(TEXT("%s: ключ 1 (оригинал) — стрела НЕ укорочена: факт %.1f из %.1f (подъём %s); сфера Camera задевает: %s на %.0f"),
						*K.Name, M.Actual, K.Len, M.bLift ? TEXT("да") : TEXT("нет"), *K.SweepHit, K.SweepDist));
				St->Expect(!FMath::IsFinite(M.GroundZ) || M.Cam.Z >= M.GroundZ + UAsdaCameraArmComponent::kGroundClearance - 1.f,
					FString::Printf(TEXT("%s: ключ 1 — глаз не ниже земли + 60 (глаз %.0f, земля %.0f)"), *K.Name, M.Cam.Z, M.GroundZ));
				Shot(*St, FString::Printf(TEXT("cam_%s_new"), *K.Name));
				St->Next();
				return;
			}
			case 3:   // кадр записан → ключ 0 (прежний порт)
			{
				if (InStep < 0.4) { return; }
				SetMode(0);
				Apply(*St, K.P, K.Yaw, K.Pitch, K.Len);
				St->Next();
				return;
			}
			case 4:   // ключ 0: замер (контрольная пара)
			{
				Apply(*St, K.P, K.Yaw, K.Pitch, K.Len);
				if (InStep < 0.7) { return; }
				const FMeasure M = Measure(*St);
				K.OldLen = M.Actual;
				const FString Txt = FString::Printf(TEXT("%s: ключ 0 (прежний порт) — факт %.1f из %.1f (укорочено на %.0f); сфера Camera задевает: %s"),
					*K.Name, M.Actual, K.Len, K.Len - M.Actual, *K.SweepHit);
				// Контроль: у препятствия есть геометрия для сферы → прежний порт обязан укоротить стрелу (дефект воспроизводим);
				//   нет геометрии → прежний порт тоже сквозь него (информация: такой объект камеру не «съедал»).
				if (K.bGeom) { St->Expect(M.Actual < K.Len - 20.f, Txt + TEXT(" — дефект воспроизводим (контроль)")); }
				else { UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА [инфо] %s — геометрии для сферы нет, прежний порт тоже не укорачивал"), *Txt); }
				Shot(*St, FString::Printf(TEXT("cam_%s_old"), *K.Name));
				St->Next();
				return;
			}
			case 5:
			{
				if (InStep < 0.4) { return; }
				SetMode(1);
				NextCase();
				return;
			}
			default: return;
			}
		}

		// ── Фаза 2: склон — подъём глаза над землёй ──
		if (St->Phase == 2)
		{
			switch (St->Step)
			{
			case 0:
			{
				SetMode(1);
				const bool b = FindSlope(*St);
				St->Expect(b, FString::Printf(TEXT("склон: найдена позиция, где глаз на длине 1270 ушёл бы под землю (у %s)"), *St->Home.ToString()));
				if (!b) { St->Phase = 4; return; }
				Apply(*St, St->SlopeP, St->SlopeYaw, St->SlopePitch, 1270.f);
				St->Next();
				return;
			}
			case 1:
			{
				Apply(*St, St->SlopeP, St->SlopeYaw, St->SlopePitch, 1270.f);
				if (InStep < 0.7) { return; }
				const FMeasure M = Measure(*St);
				const float HorIdeal = (float)FVector::Dist2D(M.Ideal, M.Origin);
				const float HorCam = (float)FVector::Dist2D(M.Cam, M.Origin);
				St->Expect(M.bLift && M.Lift > 50.f, FString::Printf(TEXT("склон, ключ 1: глаз поднят над землёй на %.0f"), M.Lift));
				St->Expect(FMath::IsFinite(M.GroundZ) && FMath::Abs((float)M.Cam.Z - (M.GroundZ + 60.f)) <= 1.5f,
					FString::Printf(TEXT("склон, ключ 1: глаз = земля + 60 (глаз %.1f, земля %.1f)"), M.Cam.Z, M.GroundZ));
				St->Expect(FMath::Abs(HorCam - HorIdeal) <= 1.5f,
					FString::Printf(TEXT("склон, ключ 1: подъём в той же точке XZ — гориз. расстояние %.1f = %.1f (стрела не укорочена)"), HorCam, HorIdeal));
				St->Expect(M.LookErrDeg <= 0.5f, FString::Printf(TEXT("склон, ключ 1: камера смотрит на цель (отклонение %.2f°)"), M.LookErrDeg));
				Shot(*St, TEXT("cam_slope_new"));
				St->Next();
				return;
			}
			case 2:
			{
				if (InStep < 0.4) { return; }
				SetMode(0);
				Apply(*St, St->SlopeP, St->SlopeYaw, St->SlopePitch, 1270.f);
				St->Next();
				return;
			}
			case 3:
			{
				Apply(*St, St->SlopeP, St->SlopeYaw, St->SlopePitch, 1270.f);
				if (InStep < 0.7) { return; }
				const FMeasure M = Measure(*St);
				FHitResult Hr;
				const bool bHit = CameraSweep(Wd, Ch, M.Origin, M.Ideal, Hr);
				UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА [инфо] склон, ключ 0 (прежний порт): факт %.1f из 1270, глаз %.0f, земля %.0f; сфера задевает %s"),
					M.Actual, M.Cam.Z, M.GroundZ, bHit ? *HitText(Hr) : TEXT("ничего"));
				Shot(*St, TEXT("cam_slope_old"));
				St->Next();
				return;
			}
			case 4:
			{
				if (InStep < 0.4) { return; }
				SetMode(1);
				St->SweepYaw0 = St->SlopeYaw - 15.f; St->SweepN = 0; St->MaxJump = 0.f; St->MaxLiftJump = 0.f; St->MaxJumpOld = 0.f;
				Apply(*St, St->SlopeP, St->SweepYaw0, St->SlopePitch, 1270.f);
				St->Phase = 3; St->Step = 0; St->StepS = Now;
				return;
			}
			default: return;
			}
		}

		// ── Фаза 3: качание камеры над склоном — непрерывность (ключ 1, затем 0 для сравнения) ──
		if (St->Phase == 3)
		{
			const bool bOld = St->Step == 1;
			if (InStep < 0.5 && St->SweepN == 0) { return; }   // выдержка до первого шага
			const FMeasure M = Measure(*St);
			if (St->SweepN > 0)
			{
				const float J = (float)FVector::Dist(M.Cam, St->PrevCam);
				if (bOld) { St->MaxJumpOld = FMath::Max(St->MaxJumpOld, J); }
				else { St->MaxJump = FMath::Max(St->MaxJump, J); St->MaxLiftJump = FMath::Max(St->MaxLiftJump, FMath::Abs(M.Lift - St->PrevLift)); }
			}
			St->PrevCam = M.Cam; St->PrevLift = M.Lift;
			++St->SweepN;
			if (St->SweepN <= 60)
			{
				Apply(*St, St->SlopeP, St->SweepYaw0 + St->SweepN * 0.5f, St->SlopePitch, 1270.f);
				return;
			}
			if (!bOld)
			{
				// Шаг 0.5° на длине 1270 даёт ≈11 юнитов дуги; подъём над склоном добавляет не больше того же порядка.
				St->Expect(St->MaxJump <= 35.f && St->MaxLiftJump <= 25.f,
					FString::Printf(TEXT("качание над склоном, ключ 1: глаз движется без рывков (наиб. шаг %.1f, наиб. скачок подъёма %.1f за 0.5°)"),
						St->MaxJump, St->MaxLiftJump));
				SetMode(0);
				St->SweepN = 0; St->Step = 1; St->StepS = Now;
				Apply(*St, St->SlopeP, St->SweepYaw0, St->SlopePitch, 1270.f);
				return;
			}
			UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА [инфо] качание над склоном, ключ 0 (прежний порт): наиб. шаг глаза %.1f за 0.5°"), St->MaxJumpOld);
			SetMode(1);
			St->Phase = 4;
			return;
		}

		// ── Итог: перепись блокирующих Camera (все сущности на месте) и сводка по случаям ──
		CensusCameraBlockers(Wd);
		for (const FCase& K : St->Cases)
		{
			UE_LOG(LogAsdaCameraTest, Log, TEXT("САМОТЕСТ-КАМЕРА СВОДКА %-5s задано %.0f | ключ 1: %.1f | ключ 0: %.1f | сфера: %s"),
				*K.Name, K.Len, K.NewLen, K.OldLen, *K.SweepHit);
		}
		Finish();
	}), (float)kTickS, true, (float)kTickS);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCameraOcclusion(
	TEXT("asda.Camera.SelfTestOcclusion"),
	TEXT("Самотест камеры (правило оригинала cameraUpdate@0x959880): персонаж, моб, NPC, дерево, стена и ящик между камерой и "
	     "своим персонажем не укорачивают стрелу; глаз не ниже земли .TER + 60 и смотрит на цель; качание над склоном без рывков. "
	     "Контроль — тот же кадр при asda.Camera.OriginalNoCollide 0. Только офлайн. `delay=N` `shotdir=<папка>` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaRunCameraOcclusionSelfTest(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
