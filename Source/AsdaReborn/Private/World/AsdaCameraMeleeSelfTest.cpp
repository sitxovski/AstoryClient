// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «КАМЕРА В БЛИЖНЕМ БОЮ» (05.11.2026, жалоба игроков: «в ближнем бою камера прыгает/наезжает к персонажу —
//   стрела цепляется за оружие и навески врагов как за стену»). [ОРИГИНАЛ]
//
// ★ ЧТО ПРОВЕРЯЕТ. Правило камеры оригинала (`cameraUpdate@0x959880`, разбор — `World/AsdaCameraArmComponent.h`): столкновений
//   с объектами у камеры НЕТ, длину стрелы меняет только колесо; единственная поправка — подъём глаза над `.TER + 60`.
//   Значит в бою, где на линии «персонаж → глаз» стоят мобы (в том числе крупные), соседи с оружием в замахе, эффекты навыков,
//   снаряды и транспорт, фактическое расстояние глаза до цели стрелы обязано В КАЖДОМ КАДРЕ равняться заданной длине
//   (`TargetArmLength`) — кроме кадров подъёма над землёй (на ровном месте их нет).
//   Каждый кадр пишется: фактическая длина, подъём, и что задевает «сырой» запрос прежней стрелы (сфера R=12 по каналу Camera,
//   игнорируя только своего персонажа — ровно запрос `USpringArmComponent`): компонент, владелец, класс, меш, профиль.
//   КОНТРОЛЬНАЯ ПАРА: та же сцена с `asda.Camera.OriginalNoCollide 0` (прежний порт). Если сырой запрос что-то задевает, прежний
//   порт обязан укоротить стрелу (иначе самотест не видит дефекта и ничего не доказывает).
//
// ★ КАК (путь игрока, офлайн `-AsdaNoNet`): свой персонаж — `Asda.World.FakeSelfSpawn 256 256`; мобы — 4013 штатному приёмнику
//   и удары по мне 4015 вид 3; соседи — `DebugSpawnFakePeer` (путь снапшота 4005), оружие 4021, замахи 4029 по мобу; эффекты
//   навыков — `PlaySkillVisual` (тот же вход, что `Recv_SkillCast` 5257); снаряд моба — 8012 Bishop навык 33 по мне; транспорт —
//   `OnMountState` (тот же делегат, что 5453/посадка). Угол камеры — `SetOrbitAnglesForBench` (точка ПКМ-драга), длина —
//   `TargetArmLength` (как колесо). Сцены: (1) мобы вокруг, крупные позади на линии камеры; (2) соседи с мечами в замахе позади;
//   (3) эффекты навыков соседей и свои + снаряд по мне; (4) всё то же верхом, соседи тоже верхом. Каждая сцена — два угла
//   (штатный −20°/700 и низкий −6°/450) × ключ 1 и 0.
//   Кадры — `shotdir=<папка>` (по умолчанию `Reports/camera/melee`), имена `melee_<сцена>_<угол>_<new|old>.png`.
//
// Консоль: `asda.Camera.SelfTestMelee [delay=N] [shotdir=<папка>] [quit]`. Только офлайн. Строки — «САМОТЕСТ-КАМЕРА-БОЙ»,
//   итог — «САМОТЕСТ-КАМЕРА-БОЙ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                          // поля тел пакетов
#include "AsdaCVarSave.h"                       // ключи на время прогона (вернутся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                    // запись 4013 (моб)
#include "Test/AsdaSelfTestKit.h"               // каркас: аргументы, отказ, итог, выход, свой номер
#include "Test/AsdaSelfTestSelfReady.h"         // свой персонаж готов
#include "Items/AsdaItemTableSubsystem.h"       // оружие соседей
#include "Skill/AsdaSkillClipRow.h"             // строки показа навыка (выбор навыка с эффектами / снарядом)
#include "Skill/AsdaSkillFxSubsystem.h"         // показ навыка
#include "UI/AsdaInventorySubsystem.h"          // EAsdaEquipSlot — слот оружия в 4021
#include "World/AsdaCameraArmComponent.h"       // стрела с правилом оригинала
#include "World/AsdaCoord.h"                    // игра ↔ UE, клетки
#include "World/AsdaPlayerController.h"         // SetOrbitAnglesForBench
#include "World/AsdaPlayerCharacter.h"          // свой персонаж и соседи
#include "World/AsdaWorldSubsystem.h"           // соседи, мобы
#include "Npc/AsdaMobActor.h"                   // мобы
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "TimerManager.h"
#include "UnrealClient.h"                       // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCameraMelee, Log, All);

namespace AsdaCameraMeleeLocal
{
	static const TCHAR* const kCmd = TEXT("asda.Camera.SelfTestMelee");     // имя самотеста
	static const TCHAR* const kTag = TEXT("САМОТЕСТ-КАМЕРА-БОЙ");            // префикс строк журнала
	static const TCHAR* const kKey = TEXT("asda.Camera.OriginalNoCollide");  // ключ правила камеры (меняется на прогон)
	constexpr int32 kSelfFallback = 4323;      // свой userNum, если 4003 не приходил
	constexpr int32 kSpdDenom = 250;           // знаменатель скорости 4013/4015 (как у самотеста навыков мобов)
	constexpr int32 kWolf = 1, kPawn = 295, kBishop = 303, kRook = 319;   // типы мобов (Mob list): волк alpen и «шахматные» крупные
	constexpr int32 kRid0 = 31201;             // первый renderId мобов прогона
	constexpr int32 kPeer0 = 7861;             // первый userNum соседей прогона
	constexpr int32 kMountCode = 522;          // транспорт (subType 15) — тот же, что у самотеста высадки
	constexpr float kProbeR = 12.f;            // радиус сферы `USpringArmComponent::ProbeSize` (умолчание движка)
	constexpr float kLenTol = 2.f;             // допуск длины стрелы, юниты UE
	constexpr double kSampleS = 3.0;           // длительность замера одного угла × ключа, с
	constexpr double kWarmS = 1.2;             // прогрев сцены до замера (спаун, загрузка мешей), с

	// Угол камеры одного замера.
	struct FAngle { const TCHAR* Name; float Pitch; float Len; };
	static const FAngle kAngles[] = { { TEXT("std"), -20.f, 700.f }, { TEXT("low"), -6.f, 450.f } };

	// Итог одного замера (угол × ключ).
	struct FRun
	{
		int32 Frames = 0;                 // кадров замера
		int32 LiftFrames = 0;             // кадров с подъёмом над землёй (длина в них не сверяется)
		int32 Short = 0;                  // кадров, где стрела короче заданной больше допуска (без подъёма)
		float MinLen = 1e9f;              // наименьшая фактическая длина (без подъёма)
		float MaxDev = 0.f;               // наибольшее |факт − задано| (без подъёма)
		float MaxStep = 0.f;              // наибольший скачок глаза за кадр (рывок)
		int32 SweepHitFrames = 0;         // кадров, где сырой запрос прежней стрелы что-то задел
		TMap<FString, int32> Hits;        // что задевал сырой запрос: описание → кадров
		int32 DynActors = 0;              // наибольшее число динамических акторов рядом, блокирующих Camera с геометрией
	};

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> World;                 // мир прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть (приёмник пакетов)
		TUniquePtr<FAsdaNetSelfScope> SelfScope;      // свой номер на время прогона (вернётся в итоге)
		FAsdaSelfTestRunGate Gate{ kCmd, false };     // офлайн-сторож (`quit` выставляет `Run`)
		bool bQuit = false;                           // выйти по итогу
		FString ShotDir;                              // папка кадров
		int32 Ok = 0, Fail = 0;                       // счётчики
		int32 Self = 0;                               // свой userNum
		bool bWasInGame = false;                      // исходный признак игрового соединения
		FIntPoint Home = FIntPoint::ZeroValue;        // своя клетка
		FVector HomeUe = FVector::ZeroVector;         // свой корень (UE) — держим на месте каждый кадр
		float CamYaw = 0.f;                           // мировой yaw стрелы: глаз над клеткой «позади» (+Z игры)
		TArray<int32> Rids;                           // мобы прогона
		TArray<int32> Peers;                          // соседи прогона
		int32 SwordItem = 0;                          // меч соседей (4021)
		int32 FxSkill = 0, ProjSkill = 0;             // навык с эффектами каста / со снарядом (DT_SkillClips)
		int32 Scene = 0;                              // текущая сцена 1..4
		int32 AngleIdx = 0;                           // текущий угол
		int32 Key = 1;                                // текущий ключ
		double PhaseT0 = 0.0;                         // начало фазы (FPlatformTime)
		double NextAct = 0.0;                         // время следующего «действия боя»
		int32 ActN = 0;                               // номер действия боя в фазе
		bool bShot = false;                           // кадр фазы снят
		FVector PrevCam = FVector::ZeroVector;        // глаз прошлого кадра
		bool bPrevCam = false;                        // глаз прошлого кадра записан
		FRun Cur;                                     // текущий замер
		TMap<FString, FRun> Done;                     // замеры: «сцена/угол/ключ» → итог
		int32 Phase = 0;                              // 0 ожидание, 1 подготовка, 2 сцены, 3 уборка, 4 итог
		double DeadlineS = 0.0;                       // потолок прогона

		UGameInstance* GI() const { return World.IsValid() ? World->GetGameInstance() : nullptr; }                 // экземпляр игры
		UAsdaWorldSubsystem* Ws() const { return GI() ? GI()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }    // мир порта
		UAsdaSkillFxSubsystem* Fx() const { return GI() ? GI()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr; } // показ навыков
		AAsdaPlayerController* Pc() const                                                                         // свой контроллер
		{ return World.IsValid() ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = Pc(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }   // свой персонаж
		UAsdaCameraArmComponent* Arm() const { AAsdaPlayerCharacter* C = Ch(); return C ? Cast<UAsdaCameraArmComponent>(C->CameraArm.Get()) : nullptr; }   // стрела
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = Ws(); return W ? W->FindNetMobByRid(R) : nullptr; }   // моб по rid
		AAsdaPlayerCharacter* Peer(int32 U) const                                                                 // сосед по номеру
		{ const UAsdaWorldSubsystem* W = Ws(); return W ? Cast<AAsdaPlayerCharacter>(W->GetPlayerActorByUserNum(U)) : nullptr; }
		void Feed(int32 Op, const TArray<uint8>& B) const { if (UAsdaNetSubsystem* N = Net.Get()) { N->DebugFeedPacket(Op, B); } }   // пакет приёмнику
		// Отметка проверки.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCameraMelee, Log, TEXT("%s [ok]   %s"), kTag, *What); }
			else   { ++Fail; UE_LOG(LogAsdaCameraMelee, Error, TEXT("%s [FAIL] %s"), kTag, *What); }
		}
	};

	// Имя сцены.
	static const TCHAR* SceneName(int32 Sc)
	{
		switch (Sc)
		{
		case 1: return TEXT("mobs");      // мобы вокруг, крупные позади на линии камеры, удары по мне
		case 2: return TEXT("peers");     // соседи с мечами в замахе позади
		case 3: return TEXT("skills");    // эффекты навыков соседей и свои, снаряд моба по мне
		case 4: return TEXT("mounted");   // то же верхом, соседи тоже верхом
		default: return TEXT("?");
		}
	}

	// 4015 вид 3 (замах моба по жертве): @0 жертва, @2 тип, @4 rid, @6 = 3, клетки, скорость, темп, результат @21, HP @25, отражение @31.
	static TArray<uint8> Body4015Hit(int32 Victim, int32 Rid, int32 Type, const FIntPoint& Cur, int32 Dmg)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], (int16)Victim); AsdaBytes::WrI16(&B[2], (int16)Type); AsdaBytes::WrI16(&B[4], (int16)Rid); B[6] = 3;
		AsdaBytes::WrI16(&B[7], (int16)Cur.X); AsdaBytes::WrI16(&B[9], (int16)Cur.Y); AsdaBytes::WrI16(&B[11], (int16)Cur.X); AsdaBytes::WrI16(&B[13], (int16)Cur.Y);
		AsdaBytes::WrI16(&B[15], (int16)kSpdDenom); AsdaBytes::WrI16(&B[17], 10000); AsdaBytes::WrI16(&B[19], 10000);
		AsdaBytes::WrI32(&B[21], Dmg); AsdaBytes::WrI32(&B[25], 100); AsdaBytes::WrI16(&B[31], -1);
		return B;
	}

	// 4029 (замах атакующего по мобу): @0 атакующий, @2 rid, @4 HP, @8 i32[4] очередь урона, @24 подвид, @25 i16[4] второй канал (−1).
	static TArray<uint8> Body4029(int32 AtkU, int32 Rid, int32 Hp, int32 Dmg)
	{
		TArray<uint8> B; B.SetNumZeroed(49);
		AsdaBytes::WrI16(&B[0], (int16)AtkU); AsdaBytes::WrI16(&B[2], (int16)Rid); AsdaBytes::WrI32(&B[4], Hp);
		AsdaBytes::WrI32(&B[8], Dmg);
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
		return B;
	}

	// 4021 (часть экипировки, 9 б): @0 userIdx, @2 слот (9 — оружие), @4 предмет, @8 заточка.
	static TArray<uint8> Body4021Weapon(int32 UserIdx, int32 ItemId)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(&B[0], (int16)UserIdx); AsdaBytes::WrI16(&B[2], (int16)EAsdaEquipSlot::Weapon); AsdaBytes::WrI32(&B[4], ItemId);
		return B;
	}

	// 8012 (навык моба по цели): шапка 9 б {err, навык, моб, усл, вид цели, цель} + запись 29 б {вид, индекс, урон, флаги, …}.
	static TArray<uint8> Body8012(int32 Skill, int32 Rid, int32 TargetIdx, int32 Dmg)
	{
		TArray<uint8> B; B.SetNumZeroed(9 + 29);
		AsdaBytes::WrI16(&B[1], (int16)Skill); AsdaBytes::WrI16(&B[3], (int16)Rid); B[6] = 1; AsdaBytes::WrI16(&B[7], (int16)TargetIdx);
		uint8* R = B.GetData() + 9;
		R[0] = 1; AsdaBytes::WrI16(R + 1, (int16)TargetIdx); AsdaBytes::WrI32(R + 3, Dmg); R[7] = 0x01; AsdaBytes::WrI16(R + 19, -1);
		return B;
	}

	// 4011 (уход игрока из зоны видимости): @0 userIdx.
	static TArray<uint8> Body4011(int32 UserIdx)
	{
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], (int16)UserIdx);
		return B;
	}

	// Первое оружие подтипа (тип 9, не удочка) с дальностью. 0 — нет.
	static int32 FindWeapon(const UAsdaItemTableSubsystem* IT, int32 Wt)
	{
		if (!IT) { return 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (R && R->ItemType == 9 && R->ItemSubType == Wt && R->ItemVariant != 1 && R->AttackRangeTiles > 0) { return Code; }
		}
		return 0;
	}

	// Навык с показом: bProj — со снарядом (Proj > 0), иначе — с эффектом каста. 0 — нет.
	static int32 FindSkill(const UAsdaSkillFxSubsystem* Fx, bool bProj)
	{
		if (!Fx) { return 0; }
		TArray<const FAsdaSkillClipRow*> Rows;
		for (int32 Id = 1; Id < 12000; ++Id)
		{
			Rows.Reset();
			Fx->CollectRows(Id, Rows);
			for (const FAsdaSkillClipRow* R : Rows)
			{
				if (!R) { continue; }
				if (bProj ? R->Proj > 0 : (!R->FxCast.IsEmpty() && R->Proj == 0)) { return Id; }
			}
		}
		return 0;
	}

	// Описание компонента для журнала: владелец, класс владельца, компонент, класс, меш, профиль коллизии.
	static FString CompText(const UPrimitiveComponent* C)
	{
		if (!C) { return TEXT("—"); }
		FString Mesh = TEXT("-");
		if (const UStaticMeshComponent* S = Cast<UStaticMeshComponent>(C)) { Mesh = S->GetStaticMesh() ? S->GetStaticMesh()->GetName() : TEXT("?"); }
		if (const USkeletalMeshComponent* K = Cast<USkeletalMeshComponent>(C)) { Mesh = K->GetSkeletalMeshAsset() ? K->GetSkeletalMeshAsset()->GetName() : TEXT("?"); }
		const AActor* O = C->GetOwner();
		return FString::Printf(TEXT("%s(%s).%s [%s, меш %s, профиль %s]"), O ? *O->GetName() : TEXT("?"), O ? *O->GetClass()->GetName() : TEXT("?"),
			*C->GetName(), *C->GetClass()->GetName(), *Mesh, *C->GetCollisionProfileName().ToString());
	}

	// Есть ли у компонента геометрия, которую видит запрос-сфера.
	static bool HasSweepGeom(const UPrimitiveComponent* P)
	{
		if (const UStaticMeshComponent* Sm = Cast<UStaticMeshComponent>(P))
		{
			const UBodySetup* Bs = Sm->GetStaticMesh() ? Sm->GetStaticMesh()->GetBodySetup() : nullptr;
			return Bs && (Bs->AggGeom.GetElementCount() > 0 || Bs->CollisionTraceFlag == CTF_UseComplexAsSimple);
		}
		if (const USkeletalMeshComponent* Sk = Cast<USkeletalMeshComponent>(P)) { return Sk->GetPhysicsAsset() != nullptr; }
		return P != nullptr;
	}

	// Динамические (не карты) акторы в радиусе 1500 от своего персонажа, у которых есть компонент, блокирующий Camera с геометрией.
	static int32 CountDynBlockers(UWorld* W, const AActor* Self, TSet<FString>* OutNames)
	{
		int32 N = 0;
		const FVector P = Self->GetActorLocation();
		for (TActorIterator<AActor> It(W); It; ++It)
		{
			AActor* A = *It;
			if (!A || A == Self || A->IsHidden() || FVector::DistSquared(A->GetActorLocation(), P) > 1500.0 * 1500.0) { continue; }
			if (A->GetLevel() == W->PersistentLevel && A->GetRootComponent() && A->GetRootComponent()->Mobility == EComponentMobility::Static) { continue; }
			TInlineComponentArray<UPrimitiveComponent*> Ps(A);
			for (const UPrimitiveComponent* C : Ps)
			{
				if (!C || !C->IsRegistered() || !C->IsQueryCollisionEnabled() || C->GetCollisionResponseToChannel(ECC_Camera) != ECR_Block) { continue; }
				if (!HasSweepGeom(C)) { continue; }
				++N;
				if (OutNames) { OutNames->Add(CompText(C)); }
				break;
			}
		}
		return N;
	}

	// Ключ правила камеры на фазу.
	static void SetKey(int32 V) { AsdaCVarSave::SetForTest(kKey, V); }

	// Поставить персонажа и камеру фазы (каждый кадр: стоящего персонажа ничто не обязано держать на месте).
	static void Apply(FState& S)
	{
		AAsdaPlayerCharacter* C = S.Ch();
		AAsdaPlayerController* Pc = S.Pc();
		if (!C || !Pc) { return; }
		const FAngle& A = kAngles[S.AngleIdx];
		Pc->SetOrbitAnglesForBench(FRotator::NormalizeAxis(S.CamYaw - (float)C->GetActorRotation().Yaw), A.Pitch);
		if (C->CameraArm) { C->CameraArm->TargetArmLength = A.Len; }
	}

	// Клетка «позади» (сторона глаза камеры): +k клеток по Z игры от своей клетки, со сдвигом dx вбок.
	static FIntPoint Behind(const FState& S, int32 K, int32 Dx = 0) { return S.Home + FIntPoint(Dx, K); }

	// Спавн моба 4013.
	static void SpawnMob(FState& S, int32 Rid, int32 Type, const FIntPoint& Tile)
	{
		S.Feed(4013, AsdaTestPackets::MobSpawn4013(Rid, Type, Tile, 1, kSpdDenom));
		S.Rids.AddUnique(Rid);
	}

	// Спавн соседа (путь снапшота) с мечом (4021).
	static void SpawnPeer(FState& S, int32 U, const FIntPoint& Tile)
	{
		if (UAsdaWorldSubsystem* W = S.Ws()) { W->DebugSpawnFakePeer(U, FString::Printf(TEXT("Боец%d"), U), AsdaCoord::TileCenterGame(Tile)); }
		S.Peers.AddUnique(U);
		if (S.SwordItem > 0) { S.Feed(4021, Body4021Weapon(U, S.SwordItem)); }
	}

	// Разметка сцены: мобы (rid) и соседи (номера) по клеткам. Одна раскладка на все сцены — в каждой включаются свои действия.
	struct FLayout { int32 Rid; int32 Type; FIntPoint Off; };
	static const FLayout kMobs[] = {
		{ kRid0 + 0, kRook,   FIntPoint( 0, 2) },   // крупный — прямо позади, на линии камеры
		{ kRid0 + 1, kBishop, FIntPoint( 1, 3) },   // крупный — позади сбоку
		{ kRid0 + 2, kPawn,   FIntPoint(-1, 1) },   // позади сбоку вплотную
		{ kRid0 + 3, kWolf,   FIntPoint( 0, 1) },   // вплотную позади
		{ kRid0 + 4, kWolf,   FIntPoint( 1, 0) },   // сбоку
		{ kRid0 + 5, kWolf,   FIntPoint( 0,-1) },   // перед персонажем
	};
	static const FIntPoint kPeerOff[] = { FIntPoint(0, 1), FIntPoint(-1, 2), FIntPoint(1, 1) };   // соседи позади (на линии камеры)
	constexpr int32 kPeerTargetIdx = 3;   // соседи бьют волка вплотную позади (rid kRid0+3)

	// Действие боя сцены (каждые 0.7 с): удары мобов по мне, замахи соседей, показы навыков, снаряд.
	static void Act(FState& S)
	{
		const int32 N = S.ActN++;
		if (S.Scene == 1 || S.Scene == 4)
		{
			for (const FLayout& L : kMobs) { S.Feed(4015, Body4015Hit(S.Self, L.Rid, L.Type, S.Home + L.Off, 1)); }
		}
		if (S.Scene == 2 || S.Scene == 4)
		{
			const FLayout& T = kMobs[kPeerTargetIdx];
			for (const int32 U : S.Peers)
			{
				if (AAsdaPlayerCharacter* P = S.Peer(U)) { P->AttackFaceTarget = S.Mob(T.Rid); }
				S.Feed(4029, Body4029(U, T.Rid, 100, 1));
			}
		}
		if (S.Scene == 3 || S.Scene == 4)
		{
			UAsdaSkillFxSubsystem* Fx = S.Fx();
			const int32 Sk = (N % 2 == 0) ? S.FxSkill : S.ProjSkill;
			if (Fx && Sk > 0)
			{
				for (const int32 U : S.Peers)
				{
					if (AAsdaPlayerCharacter* P = S.Peer(U)) { P->AttackFaceTarget = S.Ch(); Fx->PlaySkillVisual(P, Sk, U); }
				}
				if (AAsdaPlayerCharacter* C = S.Ch()) { C->AttackFaceTarget = S.Mob(kMobs[0].Rid); Fx->PlaySkillVisual(C, S.FxSkill, S.Self); }
			}
			S.Feed(8012, Body8012(33, kMobs[1].Rid, S.Self, 1));   // снаряд Bishop по мне (летит вдоль линии камеры)
		}
	}

	// Замер кадра.
	static void Sample(FState& S)
	{
		UWorld* W = S.World.Get();
		AAsdaPlayerCharacter* C = S.Ch();
		UAsdaCameraArmComponent* A = S.Arm();
		if (!W || !C || !A || !C->Camera) { return; }
		const FVector Origin = A->GetLastArmOrigin();
		const FVector Cam = C->Camera->GetComponentLocation();
		const float Actual = (float)FVector::Dist(Cam, Origin);
		FRun& R = S.Cur;
		++R.Frames;
		if (S.bPrevCam) { R.MaxStep = FMath::Max(R.MaxStep, (float)FVector::Dist(Cam, S.PrevCam)); }
		S.PrevCam = Cam; S.bPrevCam = true;
		const bool bLift = A->WasGroundLiftedLastUpdate();
		if (bLift) { ++R.LiftFrames; }
		else
		{
			const float Dev = FMath::Abs(Actual - A->TargetArmLength);
			R.MaxDev = FMath::Max(R.MaxDev, Dev);
			R.MinLen = FMath::Min(R.MinLen, Actual);
			if (Actual < A->TargetArmLength - kLenTol) { ++R.Short; }
		}
		// Сырой запрос прежней стрелы: сфера R=12 по Camera от цели к ИДЕАЛЬНОМУ глазу, игнорируя только своего персонажа.
		const FVector Ideal = Origin - A->GetComponentRotation().Vector() * A->TargetArmLength;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaCameraMeleeSelfTest), false, C);
		FHitResult H;
		if (W->SweepSingleByChannel(H, Origin, Ideal, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(kProbeR), Q))
		{
			++R.SweepHitFrames;
			R.Hits.FindOrAdd(FString::Printf(TEXT("%s на %.0f"), *CompText(H.GetComponent()), FMath::RoundToFloat(H.Distance / 25.f) * 25.f)) += 1;
		}
		if ((R.Frames % 10) == 1) { R.DynActors = FMath::Max(R.DynActors, CountDynBlockers(W, C, nullptr)); }
		// Покадровый журнал (каждый 5-й кадр — чтобы журнал не раздувался; итоги считаются по всем кадрам).
		if ((R.Frames % 5) == 1)
		{
			UE_LOG(LogAsdaCameraMelee, Log, TEXT("%s кадр %s/%s/ключ %d #%d: длина факт %.1f задано %.0f подъём %s, сырой запрос: %s"), kTag,
				SceneName(S.Scene), kAngles[S.AngleIdx].Name, S.Key, R.Frames, Actual, A->TargetArmLength, bLift ? TEXT("да") : TEXT("нет"),
				H.bBlockingHit ? *FString::Printf(TEXT("%s на %.0f"), *CompText(H.GetComponent()), H.Distance) : TEXT("ничего"));
		}
	}

	// Итог фазы (угол × ключ): проверки и запись.
	static void CloseRun(FState& S)
	{
		const FString Id = FString::Printf(TEXT("%s/%s"), SceneName(S.Scene), kAngles[S.AngleIdx].Name);
		const FRun& R = S.Cur;
		UE_LOG(LogAsdaCameraMelee, Log, TEXT("%s ЗАМЕР %s ключ %d: кадров %d (подъём %d), короче заданной %d, мин. длина %.1f, макс. откл. %.2f, макс. шаг глаза %.1f, ")
			TEXT("сырой запрос задевал %d кадров, динамических блокирующих рядом до %d"), kTag, *Id, S.Key, R.Frames, R.LiftFrames, R.Short,
			R.MinLen < 1e8f ? R.MinLen : -1.f, R.MaxDev, R.MaxStep, R.SweepHitFrames, R.DynActors);
		TArray<TPair<FString, int32>> Hs;
		for (const TPair<FString, int32>& P : R.Hits) { Hs.Add(P); }
		Hs.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
		for (int32 i = 0; i < Hs.Num() && i < 12; ++i)
		{
			UE_LOG(LogAsdaCameraMelee, Log, TEXT("%s ЗАМЕР %s ключ %d: сырой запрос задел %4d кадров — %s"), kTag, *Id, S.Key, Hs[i].Value, *Hs[i].Key);
		}
		S.Done.Add(FString::Printf(TEXT("%s/%d"), *Id, S.Key), R);
		if (S.Key == 1)
		{
			S.Expect(R.Frames >= 20, FString::Printf(TEXT("%s: замер шёл (%d кадров)"), *Id, R.Frames));
			S.Expect(R.Short == 0 && R.MaxDev <= kLenTol, FString::Printf(TEXT("%s: [ОРИГИНАЛ] стрела не укорачивается в бою — кадров короче заданной %d, макс. отклонение %.2f (допуск %.0f)"),
				*Id, R.Short, R.MaxDev, kLenTol));
			S.Expect(R.LiftFrames == 0, FString::Printf(TEXT("%s: на ровном месте подъёма над землёй нет (%d кадров)"), *Id, R.LiftFrames));
		}
		else
		{
			// Контрольная пара: прежний порт обязан укоротить стрелу, если её запрос что-то задевает — иначе сцена ничего не доказывает.
			const FRun* New = S.Done.Find(FString::Printf(TEXT("%s/1"), *Id));
			const int32 HitsBoth = R.SweepHitFrames + (New ? New->SweepHitFrames : 0);
			if (HitsBoth > 0)
			{
				S.Expect(R.Short > 0, FString::Printf(TEXT("%s: контроль (ключ 0, прежний порт) — стрела укорачивалась %d кадров, мин. длина %.1f из %.0f (дефект виден)"),
					*Id, R.Short, R.MinLen < 1e8f ? R.MinLen : -1.f, kAngles[S.AngleIdx].Len));
			}
			else
			{
				UE_LOG(LogAsdaCameraMelee, Warning, TEXT("%s %s: сырой запрос прежней стрелы ни разу ничего не задел — контрольная пара не показывает дефекта в этом угле"), kTag, *Id);
			}
		}
	}

	// Кадр в папку отчёта.
	static void Shot(const FState& S)
	{
		const FString Name = FString::Printf(TEXT("melee_%s_%s_%s.png"), SceneName(S.Scene), kAngles[S.AngleIdx].Name, S.Key ? TEXT("new") : TEXT("old"));
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(S.ShotDir, Name), /*bShowUI=*/false, /*bAddUniqueSuffix=*/false);
	}

	// Включить сцену: транспорт (сцена 4) и прогрев.
	static void BeginScene(FState& S)
	{
		UAsdaNetSubsystem* N = S.Net.Get();
		const bool bMount = S.Scene == 4;
		if (N)
		{
			N->OnMountState.Broadcast(S.Self, kMountCode, bMount);
			for (const int32 U : S.Peers) { N->OnMountState.Broadcast(U, kMountCode, bMount); }
		}
		if (bMount)
		{
			AAsdaPlayerCharacter* C = S.Ch();
			S.Expect(C && C->IsMounted(), TEXT("mounted: свой персонаж верхом (транспорт 522)"));
			int32 PeersMounted = 0;
			for (const int32 U : S.Peers) { if (const AAsdaPlayerCharacter* P = S.Peer(U)) { PeersMounted += P->IsMounted() ? 1 : 0; } }
			S.Expect(PeersMounted == S.Peers.Num(), FString::Printf(TEXT("mounted: соседи верхом %d из %d"), PeersMounted, S.Peers.Num()));
		}
		S.AngleIdx = 0; S.Key = 1; S.PhaseT0 = FPlatformTime::Seconds(); S.NextAct = S.PhaseT0; S.ActN = 0;
		S.Cur = FRun(); S.bShot = false; S.bPrevCam = false;
		SetKey(S.Key);
		UE_LOG(LogAsdaCameraMelee, Log, TEXT("%s СЦЕНА %s"), kTag, SceneName(S.Scene));
	}

	// Шаг прогона (каждый кадр).
	static void Step(TSharedRef<FState> S);
	static void ScheduleNext(TSharedRef<FState> S)
	{
		UWorld* W = S->World.Get();
		if (!W) { return; }
		W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([S]() { Step(S); }));
	}

	// Итог прогона.
	static void Finish(FState& S)
	{
		UWorld* W = S.World.Get();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			N->OnMountState.Broadcast(S.Self, kMountCode, false);
			for (const int32 U : S.Peers) { N->DebugFeedPacket(4011, Body4011(U)); }
			N->bInGameConnected = S.bWasInGame;
		}
		for (const int32 R : S.Rids) { if (AAsdaMobActor* M = S.Mob(R)) { M->StartFadeOut(EAsdaMobFade::Aoi, AAsdaMobActor::AoiFadeSeconds); } }
		if (AAsdaPlayerCharacter* C = S.Ch()) { C->ClearMount(); C->StopAttackAnim(); C->AttackFaceTarget = nullptr; }
		if (S.SelfScope) { S.SelfScope->Restore(); }
		AsdaCVarSave::RestoreAll(kCmd);
		AsdaSelfTestKit::Finish(W, kCmd, S.bQuit, LogAsdaCameraMelee, kTag, S.Ok, S.Fail);
	}

	static void Step(TSharedRef<FState> S)
	{
		UWorld* W = S->World.Get();
		if (!W) { return; }
		if (!S->Gate.Pass(W)) { return; }
		const double Now = FPlatformTime::Seconds();
		if (Now > S->DeadlineS && S->Phase < 4)
		{
			S->Expect(false, TEXT("прогон не уложился в потолок времени"));
			S->Phase = 4; Finish(*S); return;
		}
		AAsdaPlayerCharacter* C = S->Ch();
		switch (S->Phase)
		{
		case 0:   // ожидание своего персонажа
			if (C && S->Arm() && AsdaSelfTestSelfReady::IsReady(W)) { S->Phase = 1; }
			break;
		case 1:   // подготовка: свой номер, клетка, угол, оружие, навыки, мобы, соседи
		{
			UAsdaNetSubsystem* N = S->Net.Get();
			if (!N->bSelfUserNumKnown) { N->SelfUserNum = (uint16)kSelfFallback; N->bSelfUserNumKnown = true; }
			N->bSelfDead = false;
			S->Self = (int32)N->SelfUserNum;
			S->bWasInGame = N->bInGameConnected; N->bInGameConnected = true;
			C->StopMove(); C->SetAnimState(EAsdaPlayerState::Idle);
			S->Home = AsdaCoord::GameToTile(C->GetGamePosXZ());
			S->HomeUe = C->GetActorLocation();
			// Глаз камеры — над клетками «позади» (+Z игры): стрела смотрит из них на персонажа.
			const FVector2D BehindUe = AsdaCoord::GameToUe2D(AsdaCoord::TileCenterGame(Behind(*S, 3)));
			const FVector2D SelfUe(S->HomeUe.X, S->HomeUe.Y);
			S->CamYaw = (float)FVector(SelfUe.X - BehindUe.X, SelfUe.Y - BehindUe.Y, 0.0).Rotation().Yaw;
			const UAsdaItemTableSubsystem* IT = S->GI() ? S->GI()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			S->SwordItem = FindWeapon(IT, 1);
			S->FxSkill = FindSkill(S->Fx(), false);
			S->ProjSkill = FindSkill(S->Fx(), true);
			S->Expect(S->SwordItem > 0, FString::Printf(TEXT("меч соседей найден: предмет %d"), S->SwordItem));
			S->Expect(S->FxSkill > 0 && S->ProjSkill > 0, FString::Printf(TEXT("навыки с показом: с эффектом каста %d, со снарядом %d"), S->FxSkill, S->ProjSkill));
			for (const FLayout& L : kMobs) { SpawnMob(*S, L.Rid, L.Type, S->Home + L.Off); }
			for (int32 i = 0; i < UE_ARRAY_COUNT(kPeerOff); ++i) { SpawnPeer(*S, kPeer0 + i, S->Home + kPeerOff[i]); }
			UE_LOG(LogAsdaCameraMelee, Log, TEXT("%s подготовка: свой %d клетка %d,%d, yaw стрелы %.1f, мобов %d, соседей %d"), kTag,
				S->Self, S->Home.X, S->Home.Y, S->CamYaw, S->Rids.Num(), S->Peers.Num());
			S->PhaseT0 = Now; S->Phase = 5;
			break;
		}
		case 5:   // ждём мобов и соседей
		{
			int32 Mobs = 0, Peers = 0;
			for (const int32 R : S->Rids) { Mobs += S->Mob(R) ? 1 : 0; }
			for (const int32 U : S->Peers) { Peers += S->Peer(U) ? 1 : 0; }
			if ((Mobs == S->Rids.Num() && Peers == S->Peers.Num() && Now - S->PhaseT0 > 1.0) || Now - S->PhaseT0 > 6.0)
			{
				S->Expect(Mobs == S->Rids.Num(), FString::Printf(TEXT("мобы на месте: %d из %d (волки, Pawn 295, Bishop 303, Rook 319)"), Mobs, S->Rids.Num()));
				S->Expect(Peers == S->Peers.Num(), FString::Printf(TEXT("соседи на месте: %d из %d"), Peers, S->Peers.Num()));
				int32 Armed = 0;
				for (const int32 U : S->Peers) { if (const AAsdaPlayerCharacter* P = S->Peer(U)) { Armed += P->EquippedWeaponSubType == 1 ? 1 : 0; } }
				S->Expect(Armed == S->Peers.Num(), FString::Printf(TEXT("соседи с мечом (4021): %d из %d"), Armed, S->Peers.Num()));
				// Перепись: что рядом блокирует канал Camera и имеет геометрию для сферы прежней стрелы.
				TSet<FString> Names;
				const int32 Nb = CountDynBlockers(W, C, &Names);
				UE_LOG(LogAsdaCameraMelee, Log, TEXT("%s ПЕРЕПИСЬ: динамических акторов рядом, блокирующих Camera с геометрией: %d"), kTag, Nb);
				for (const FString& Nm : Names) { UE_LOG(LogAsdaCameraMelee, Log, TEXT("%s ПЕРЕПИСЬ   %s"), kTag, *Nm); }
				S->Scene = 1; BeginScene(*S); S->Phase = 2;
			}
			break;
		}
		case 2:   // сцены: прогрев → замер ключ 1 → замер ключ 0 → следующий угол → следующая сцена
		{
			if (C) { C->SetActorLocation(S->HomeUe, false, nullptr, ETeleportType::TeleportPhysics); }
			Apply(*S);
			if (Now >= S->NextAct) { Act(*S); S->NextAct = Now + 0.7; }
			const double El = Now - S->PhaseT0;
			if (El < kWarmS) { break; }
			Sample(*S);
			if (!S->bShot && El > kWarmS + kSampleS * 0.5) { Shot(*S); S->bShot = true; }
			if (El < kWarmS + kSampleS) { break; }
			CloseRun(*S);
			S->Cur = FRun(); S->bShot = false; S->bPrevCam = false; S->PhaseT0 = Now;
			if (S->Key == 1) { S->Key = 0; SetKey(0); break; }
			S->Key = 1; SetKey(1);
			if (++S->AngleIdx < UE_ARRAY_COUNT(kAngles)) { break; }
			if (++S->Scene <= 4) { BeginScene(*S); break; }
			S->Phase = 4; Finish(*S); return;
		}
		default: return;
		}
		ScheduleNext(S);
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(W);
		if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, A.bQuit, TEXT("нет подсистемы сети")); return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		TSharedRef<FState> S = MakeShared<FState>();
		S->World = W; S->Net = Net; S->bQuit = A.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, A.bQuit };
		S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		S->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/camera/melee")));
		for (const FString& Arg : Args) { if (Arg.StartsWith(TEXT("shotdir="))) { S->ShotDir = Arg.Mid(8); } }
		S->DeadlineS = FPlatformTime::Seconds() + A.Delay + 150.0;
		TWeakObjectPtr<UWorld> WW(W);
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda([WW, S]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !S->Gate.Pass(Wd)) { return; }
			if (!AsdaSelfTestSelfReady::IsReady(Wd) && GEngine)
			{
				APlayerController* P = Wd->GetFirstPlayerController();
				if (!P || !P->GetPawn()) { GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3")); }
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
			ScheduleNext(S);
		}), FMath::Max(0.01f, A.Delay), false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCameraMelee(
	TEXT("asda.Camera.SelfTestMelee"),
	TEXT("Самотест камеры в ближнем бою (жалоба 05.11): мобы (в том числе крупные) бьют меня, соседи с мечами в замахе, эффекты навыков, "
	     "снаряд, транспорт — на линии камеры. Каждый кадр: фактическая длина стрелы = заданной (ключ asda.Camera.OriginalNoCollide 1, как "
	     "оригинал) и что задевает запрос прежней стрелы; контроль ключом 0. Только офлайн. `delay=N` `shotdir=<папка>` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCameraMeleeLocal::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
