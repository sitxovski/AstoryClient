// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ЭФФЕКТОВ ЧАСТЕЙ .PEB (07.11.2026): `asda.SelfTest.PartFx [shot=<папка>] [quit]`. [ПОРТ]
//   Баг владельца: на броне Lucifer нет пламени на шлеме, перчатках и обуви. Эталон и разбор — шапка AsdaPartFx.cpp.
//   Офлайн (`-AsdaNoNet`), карта мира. Путь игрока: сосед — пакеты 4005/4021/4023/4025 в общий разбор входящих, свой — ячейка
//   контейнера 3 (общая точка ответа 5001).
//     A) данные: все вещи с .PEB — меш части и BP каждого эффекта грузятся;
//     B) каждая вещь на соседе-мужчине и соседе-женщине (4021): число эффектов = числу записей .PEB, кость есть;
//     C) наборы вид 1..3 × пол: 4005, эффекты живут на своих костях, есть геометрия; кадры целиком и по частям;
//     D) снятие 4023, скрытие шлема 4025/111, смена на вещь без .PEB, ключ 0/1, эффект жив дольше контейнера;
//     T) длина шлейфа видов 2 и 3 (`asda.Char.PartFxTrailMeters` 0/1): замер по вершинам секций частиц, плотность, вид 1 без изменений;
//     E) свой персонаж: надел, снял, транспорт не снимает, смерть не снимает.
//   Итог: «ЧАСТИ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "Characters/AsdaCharAssembly.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "UI/AsdaInventorySubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Test/AsdaSelfTestKit.h"
#include "ProceduralMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "Misc/App.h"   // R5-fix6: FApp::GetDeltaTime — время симуляции эффекта в ожидании раздела T
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPartFxTest, Log, All);

int32 AsdaPartFxSlotsOfMesh(const FString& Mesh);   // записей в .PEB меша (AsdaPartFx.cpp)
int32 AsdaPartFxCount(const AActor* Ch);            // эффектов частей на носителе (AsdaPartFx.cpp)

namespace AsdaPartFxTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.PartFx");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Char.PartFx");       // ключ отката
	constexpr int32 kMe = 4321;      // свой userNum (4003)
	constexpr int32 kPeerM = 71;     // сосед-мужчина
	constexpr int32 kPeerF = 72;     // сосед-женщина

	// Шаг сценария: true — завершён, false — ждёт следующего кадра. Аргумент — секунды шага.
	using FStep = TFunction<bool(float)>;

	// Вещь с .PEB: код и экип-слот.
	struct FItem { int32 Id = 0; int32 Slot = 0; };

	// Набор одного вида: коды шлема, тела, обуви, перчаток.
	struct FSet { int32 Helmet = 0, Chest = 0, Shoes = 0, Gloves = 0; };

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                      // мир
		bool bQuit = false;                            // `quit` в конце
		FString ShotDir;                               // папка кадров (пусто — без кадров)
		int32 Ok = 0, Fail = 0;                        // счётчики проверок
		TArray<FStep> Steps;                           // сценарий
		int32 Idx = 0;                                 // текущий шаг
		double StepT0 = 0.0;                           // начало шага (время мира)
		bool bAbort = false;                           // прервать сценарий
		FDelegateHandle H;                             // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;        // свой номер — вернуть после прогона
		TUniquePtr<FAsdaCVarScope> CVars;              // ключи — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;         // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;        // мир
		TWeakObjectPtr<UAsdaItemTableSubsystem> Items; // таблицы предметов
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;   // инвентарь (контейнер 3 = надетое)
		TArray<FItem> All;                             // все вещи с .PEB
		TMap<int32, FSet> Sets;                        // вид 1..3 → набор
		int32 PlainHelmet = 0;                         // шлем без .PEB (контроль)
		int32 OrigCell[5] = {};                        // надетое в слотах 0..4 до прогона
		bool bWornTouched = false;                     // надетое менялось
		TWeakObjectPtr<ACameraActor> Cam;              // камера кадров
		TWeakObjectPtr<AActor> OrigView;               // цель вида до прогона

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaPartFxTest, Log,   TEXT("ЧАСТИ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaPartFxTest, Error, TEXT("ЧАСТИ FAIL %s"), *What); }
		}
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Павн соседа.
	static AAsdaPlayerCharacter* Peer(const FState& S, int32 Uidx)
	{
		UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(Uidx)) : nullptr;
	}

	// Экип-слот вещи по ItemType (-1 — не носится): шлем 0, тело 1, обувь 3, перчатки 4.
	static int32 SlotOfType(int32 Type)
	{
		switch (Type)
		{
		case 0: return 0;
		case 1: return 1;
		case 3: return 3;
		case 4: return 4;
		default: return -1;
		}
	}

	// Сколько эффектов должно быть от меша вещи.
	static int32 Expected(const FState& S, int32 ItemId, bool bMale)
	{
		const UAsdaItemTableSubsystem* Items = S.Items.Get();
		return (Items && ItemId > 0) ? AsdaPartFxSlotsOfMesh(Items->GetItemMeshName(ItemId, bMale)) : 0;
	}

	// Сколько эффектов должно быть от набора.
	static int32 ExpectedSet(const FState& S, const FSet& V, bool bMale)
	{
		return Expected(S, V.Helmet, bMale) + Expected(S, V.Chest, bMale) + Expected(S, V.Shoes, bMale) + Expected(S, V.Gloves, bMale);
	}

	// Тело 4003 (9 б): userNum, тайлы, состояние, показ шлема.
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Тело 4005 (151 б): сосед с набором, пол 1 муж / 2 жен, позиция game XZ.
	static TArray<uint8> Body4005(int32 Uidx, uint8 Gender, const FSet& V, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = 1;                                             // шлем показан
		AsdaBytes::WrI32(B.GetData() + 11, 970000 + Uidx);    // charId — заведомо не свой
		const char* Name = "PartFxTest";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = Gender; B[38] = 1; B[40] = 100;               // пол, класс, уровень
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 0, V.Helmet);
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 1, V.Chest);
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 3, V.Shoes);
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 4, V.Gloves);
		return B;
	}

	// Тело 4021/4023 (9 б): userIdx, экип-слот, предмет, заточка.
	static TArray<uint8> BodyPart(int32 Uidx, int32 Slot, int32 ItemId)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		AsdaBytes::WrI16(B.GetData() + 2, (int16)Slot);
		AsdaBytes::WrI32(B.GetData() + 4, ItemId);
		return B;
	}

	// Тело 4025 (13 б): userIdx, действие 111, флаг показа шлема @4.
	static TArray<uint8> Body4025(int32 Uidx, uint8 Flag)
	{
		TArray<uint8> B; B.SetNumZeroed(13);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		AsdaBytes::WrI16(B.GetData() + 2, 111);
		B[4] = Flag;
		return B;
	}

	// Тело «сосед ушёл» 4011.
	static TArray<uint8> Body4011(int32 Uidx)
	{
		TArray<uint8> B; B.SetNumZeroed(2);
		AsdaBytes::WrI16(B.GetData(), (int16)Uidx);
		return B;
	}

	// Надеть/снять вещь своему — ячейка контейнера 3 (общая точка ответа 5001).
	static void Wear(FState& S, int32 Slot, int32 ItemId)
	{
		UAsdaInventorySubsystem* Inv = S.Inv.Get();
		if (Inv && Inv->DebugPutItem(3, Slot, ItemId, 1)) { return; }
		if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(Slot, ItemId, 0); }
	}

	// Эффекты частей носителя: живые, привязаны к существующей кости, есть геометрия рядом с персонажем.
	static int32 HealthyFx(const AAsdaPlayerCharacter* A, FString& Why)
	{
		int32 N = 0;
		if (!A || !A->BaseSkel) { Why = TEXT("нет носителя"); return 0; }
		TArray<AActor*> Attached;
		A->GetAttachedActors(Attached);
		for (AActor* X : Attached)
		{
			const AAsdaEffectActor* E = Cast<AAsdaEffectActor>(X);
			if (!E || !E->ActorHasTag(TEXT("PartFx"))) { continue; }
			const FBox Box = E->GetComponentsBoundingBox(/*bNonColliding*/true);
			const float Dist = Box.IsValid ? (float)FVector::Dist(Box.GetCenter(), A->GetActorLocation()) : -1.f;
			const bool bBone = A->BaseSkel->GetBoneIndex(E->FollowBone) != INDEX_NONE;
			if (bBone && Box.IsValid && Dist < 400.f) { ++N; }
			else { Why += FString::Printf(TEXT(" %s(кость %s=%d, рамка %d, дальность %.0f)"), *E->GetName(), *E->FollowBone.ToString(), bBone ? 1 : 0, Box.IsValid ? 1 : 0, Dist); }
		}
		return N;
	}

	static void AimAt(FState& S, AAsdaPlayerCharacter* A, FName Bone, float Dist);   // камера перед костью (ниже)

	// Замер шлейфа: эффекты части `Part` (подстрока метки актора).
	struct FTrail
	{
		float Len = 0.f;              // самая дальняя вершина, прошедшая альфа-тест, вдоль оси конуса от точки рождения, ед. мира (видимая длина — по кадрам)
		int32 Quads = 0;              // частиц, прошедших альфа-тест
		FVector Origin = FVector::ZeroVector;   // точка рождения
		FVector Axis = FVector::ForwardVector;  // ось конуса в мире
		bool bLifeAsData = true;      // жизнь частиц всех эмиттеров = данным класса
		float Life = 0.f;             // жизнь частицы
	};

	// Есть ли у актора метка с подстрокой `Part`.
	static bool HasPart(const AActor* A, const TCHAR* Part)
	{
		for (const FName& T : A->Tags) { if (T.ToString().Contains(Part)) { return true; } }
		return false;
	}

	// Снять замер по вершинам секций: вершины уже в кадре, альфа ниже 8/255 отсекается материалом, как альфа-тест эталона.
	static FTrail Measure(const AAsdaPlayerCharacter* A, const TCHAR* Part)
	{
		FTrail R;
		if (!A) { return R; }
		TArray<AActor*> Attached;
		A->GetAttachedActors(Attached);
		for (AActor* X : Attached)
		{
			const AAsdaEffectActor* E = Cast<AAsdaEffectActor>(X);
			if (!E || !E->ActorHasTag(TEXT("PartFx")) || !HasPart(E, Part)) { continue; }
			const AAsdaEffectActor* Def = E->GetClass()->GetDefaultObject<AAsdaEffectActor>();
			UProceduralMeshComponent* Pm = E->FindComponentByClass<UProceduralMeshComponent>();
			for (int32 I = 0; I < E->Emitters.Num(); ++I)
			{
				if (!E->IsWorldSpaceParticleEmitter(I) || E->Emitters[I].Nodes.Num() == 0) { continue; }
				const FAsdaFxEmitter& Em = E->Emitters[I];
				R.Life = Em.LifeBase;
				if (!Def || !FMath::IsNearlyEqual(Def->Emitters[I].LifeBase, Em.LifeBase)) { R.bLifeAsData = false; }
				R.Axis = E->GetActorTransform().TransformVectorNoScale(Em.Nodes[0].ConeAxis.GetSafeNormal());
				R.Origin = E->GetActorTransform().TransformPosition(Em.PosBase);
				FProcMeshSection* Sec = Pm ? Pm->GetProcMeshSection(I) : nullptr;
				if (!Sec) { continue; }
				const FTransform Xf = Pm->GetComponentTransform();
				int32 Verts = 0;
				for (const FProcMeshVertex& V : Sec->ProcVertexBuffer)
				{
					if (V.Color.A < 8) { continue; }
					++Verts;
					R.Len = FMath::Max(R.Len, (float)FVector::DotProduct(Xf.TransformPosition(FVector(V.Position)) - R.Origin, R.Axis));
				}
				R.Quads += Verts / 4;
			}
		}
		return R;
	}

	// Камера сбоку от шлейфа: ось шлейфа идёт вдоль кадра влево, точка рождения в 150 ед. правее центра, дистанция 500.
	static void AimSide(FState& S, AAsdaPlayerCharacter* A, const FTrail& R)
	{
		AimAt(S, A, NAME_None, 300.f);   // создаёт камеру и запоминает прежний вид
		if (!S.Cam.IsValid()) { return; }
		const FVector Dir = FVector::CrossProduct(R.Axis, FVector::UpVector).GetSafeNormal();
		S.Cam->SetActorLocation(R.Origin + R.Axis * 150.f + Dir * 500.f);
		S.Cam->SetActorRotation((-Dir).Rotation());
	}

	// Поставить камеру кадров перед костью носителя на дистанции `Dist`.
	static void AimAt(FState& S, AAsdaPlayerCharacter* A, FName Bone, float Dist)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!PC || !A || !A->BaseSkel) { return; }
		if (!S.Cam.IsValid())
		{
			S.OrigView = PC->GetViewTarget();
			S.Cam = W->SpawnActor<ACameraActor>();
			if (S.Cam.IsValid()) { S.Cam->GetCameraComponent()->SetConstraintAspectRatio(false); }
		}
		if (!S.Cam.IsValid()) { return; }
		const FVector At = Bone.IsNone() ? A->GetActorLocation() + FVector(0.f, 0.f, 40.f) : A->BaseSkel->GetBoneLocation(Bone);
		const FVector Dir = (A->GetActorForwardVector() + A->GetActorRightVector() * 0.35f + FVector(0.f, 0.f, 0.15f)).GetSafeNormal();
		S.Cam->SetActorLocation(At + Dir * Dist);
		S.Cam->SetActorRotation((-Dir).Rotation());
		PC->SetViewTarget(S.Cam.Get());
	}

	// Снять кадр.
	static void Shot(const FState& S, const FString& Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaPartFxTest, Log, TEXT("ЧАСТИ кадр %s"), *Path);
	}

	// Конец прогона: соседи ушли, надетое вернулось, камера снята.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			for (const int32 U : { kPeerM, kPeerF }) { N->DebugFeedPacket(4011, Body4011(U)); }
		}
		if (UWorld* W = S.W.Get())
		{
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				if (S.OrigView.IsValid()) { PC->SetViewTarget(S.OrigView.Get()); }
			}
		}
		if (S.Cam.IsValid()) { S.Cam->Destroy(); }
		if (AAsdaPlayerCharacter* Me = MeOf(S))
		{
			if (Me->IsMounted()) { Me->ClearMount(); }
			Me->SetAnimState(EAsdaPlayerState::Idle);
		}
		if (S.bWornTouched)
		{
			for (int32 Slot = 0; Slot < 5; ++Slot) { Wear(S, Slot, S.OrigCell[Slot]); }
		}
		S.CVars.Reset();
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			if (MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
		}
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaPartFxTest, TEXT("ЧАСТИ"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx) && !S.bAbort)
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		Finish(SP);
	}

	// Сценарий.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };
		// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: ожидание во ВРЕМЕНИ СИМУЛЯЦИИ эффекта. Эффект шагает не больше 0,1 с за кадр
		//   (`AAsdaEffectActor`: `Min(Dt, 0.1)`), поэтому при длинных кадрах его время отстаёт от часов, а раздел T ждёт, пока
		//   частицы прежнего поколения доживут свой срок. Накопитель — по кадрам игры (один раз на кадр).
		auto SimWait = [](float Sec)
		{
			TSharedRef<float> Acc = MakeShared<float>(0.f);            // накоплено времени симуляции, с
			TSharedRef<uint64> Frame = MakeShared<uint64>(0);          // кадр последнего учёта
			return FStep([Sec, Acc, Frame](float)
			{
				if (*Frame != GFrameCounter) { *Frame = GFrameCounter; *Acc += FMath::Min((float)FApp::GetDeltaTime(), 0.1f); }
				return *Acc >= Sec;
			});
		};

		// ═══ A. Данные ═══
		S->Steps.Add(Do([S]()
		{
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			int32 Total = 0, NotWorn = 0, BadMesh = 0;
			FString BadList;
			for (int32 Id = 20000; Id < 140000; ++Id)
			{
				const FAsdaItemRecord* Rec = Items->GetItem(Id);
				if (!Rec) { continue; }
				if (AsdaPartFxSlotsOfMesh(Items->GetItemMeshName(Id, true)) == 0 && AsdaPartFxSlotsOfMesh(Items->GetItemMeshName(Id, false)) == 0) { continue; }
				++Total;
				const int32 Slot = SlotOfType(Rec->ItemType);
				if (Slot < 0) { ++NotWorn; continue; }
				S->All.Add({ Id, Slot });
				for (const bool bM : { true, false })
				{
					const FString Mesh = Items->GetItemMeshName(Id, bM);
					if (!UAsdaCharAssembly::LoadEquipPartMesh(Mesh))
					{
						++BadMesh;
						if (BadList.Len() < 400) { BadList += FString::Printf(TEXT(" %d:%s"), Id, *Mesh); }
					}
					// Вид 1..3 — по имени меша ch1_x_00_<часть>305_0V_00_0.
					const int32 At = Mesh.Find(TEXT("305_0"));
					if (bM && At != INDEX_NONE)
					{
						FSet& V = S->Sets.FindOrAdd(Mesh[At + 5] - TEXT('0'));
						int32* Cell = Slot == 0 ? &V.Helmet : Slot == 1 ? &V.Chest : Slot == 3 ? &V.Shoes : &V.Gloves;
						if (*Cell == 0) { *Cell = Id; }
					}
				}
			}
			S->Check(S->All.Num() >= 250 && BadMesh == 0,
				FString::Printf(TEXT("A1 вещей с .PEB %d: носимых %d, не носится %d; меш не грузится %d%s"), Total, S->All.Num(), NotWorn, BadMesh, *BadList));
			int32 BpBad = 0;
			FString BpList;
			for (const TCHAR* Sex : { TEXT("m"), TEXT("f") })
			{
				for (const TCHAR* Tail : { TEXT("hcap305_00_00_1"), TEXT("hcap305_00_00_2"), TEXT("hcap305_00_00_3"), TEXT("shoe305_00_00_1"), TEXT("shoe305_00_00_2"), TEXT("shoe305_00_00_3"),
					TEXT("chst305_00_00_2_0"), TEXT("chst305_00_00_2_1"), TEXT("chst305_00_00_3_0"), TEXT("chst305_00_00_3_1") })
				{
					const FString Jeb = FString::Printf(TEXT("ch1_%s_00_%s"), Sex, Tail);
					if (!LoadClass<AAsdaEffectActor>(nullptr, *AsdaAssetPaths::FxClassPathFromJeb(Jeb))) { ++BpBad; BpList += TEXT(" ") + Jeb; }
				}
			}
			for (const TCHAR* Tail : { TEXT("chst305_00_00_1"), TEXT("chst305_00_00_2"), TEXT("chst305_00_00_3"), TEXT("grov305_00_00_01_0"), TEXT("grov305_00_00_01_1"),
				TEXT("grov305_00_00_02_0"), TEXT("grov305_00_00_02_1"), TEXT("grov305_00_00_03_0"), TEXT("grov305_00_00_03_1") })
			{
				const FString Jeb = FString::Printf(TEXT("ch1_m_00_%s"), Tail);
				if (!LoadClass<AAsdaEffectActor>(nullptr, *AsdaAssetPaths::FxClassPathFromJeb(Jeb))) { ++BpBad; BpList += TEXT(" ") + Jeb; }
			}
			S->Check(BpBad == 0, FString::Printf(TEXT("A2 BP всех 29 эффектов грузятся: нет %d%s"), BpBad, *BpList));
			for (int32 V = 1; V <= 3; ++V)
			{
				const FSet* Set = S->Sets.Find(V);
				S->Check(Set && Set->Helmet && Set->Chest && Set->Shoes && Set->Gloves, FString::Printf(TEXT("A3 набор вида %d: шлем %d тело %d обувь %d перчатки %d"),
					V, Set ? Set->Helmet : 0, Set ? Set->Chest : 0, Set ? Set->Shoes : 0, Set ? Set->Gloves : 0));
			}
			for (int32 Id = 20000; Id < 140000 && S->PlainHelmet == 0; ++Id)
			{
				const FAsdaItemRecord* Rec = Items->GetItem(Id);
				if (!Rec || Rec->ItemType != 0) { continue; }
				const FString Mesh = Items->GetItemMeshName(Id, true);
				if (!Mesh.IsEmpty() && AsdaPartFxSlotsOfMesh(Mesh) == 0 && UAsdaCharAssembly::LoadEquipPartMesh(Mesh)) { S->PlainHelmet = Id; }
			}
			if (S->All.Num() == 0 || S->Sets.Num() < 3 || BpBad > 0) { S->bAbort = true; return; }
			for (int32 Slot = 0; Slot < 5; ++Slot)
			{
				const UAsdaInventorySubsystem* Inv = S->Inv.Get();
				S->OrigCell[Slot] = Inv ? Inv->GetCellItemCode(3, Slot) : 0;
			}
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));   // карта мира с павном — как у asda.SelfTest.ShieldShow
			S->Net->DebugFeedPacket(4003, Body4003(kMe));
			// ★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: сосед с тем же номером мог остаться от прошлого теста той же игры
			//   (`asda.SelfTest.FaceExpr` оставлял соседку 71): 4005 по существующему номеру нового игрока не создаёт, и «сосед-мужчина»
			//   шага B1 оказывался женщиной — у 13 вещей эффектов 2 вместо 3 (полный прогон 14.11, 46/1). Соседи с номерами теста
			//   убираются до своих 4005; ожидание ниже покрывает их уход.
			for (const int32 U : { kPeerM, kPeerF }) { S->Net->DebugFeedPacket(4011, Body4011(U)); }
		}));
		S->Steps.Add(Wait(4.0f));   // столб света появления держится ≈3 с

		// ═══ B. Каждая вещь на соседе ═══
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(Me != nullptr, TEXT("B0 свой павн есть"));
			if (!Me) { S->bAbort = true; return; }
			const FVector2D P = Me->GetGamePosXZ();
			S->Net->DebugFeedPacket(4005, Body4005(kPeerM, 1, FSet(), P.X + 200.f, P.Y));
			S->Net->DebugFeedPacket(4005, Body4005(kPeerF, 2, FSet(), P.X + 400.f, P.Y));
		}));
		S->Steps.Add(Wait(1.0f));
		for (const int32 Uidx : { kPeerM, kPeerF })
		{
			S->Steps.Add(Do([S, Uidx]()
			{
				const bool bMale = Uidx == kPeerM;
				AAsdaPlayerCharacter* A = Peer(*S, Uidx);
				int32 Good = 0, Bad = 0;
				FString BadList;
				for (const FItem& I : S->All)
				{
					S->Net->DebugFeedPacket(4021, BodyPart(Uidx, I.Slot, I.Id));
					const int32 Want = Expected(*S, I.Id, bMale);
					const int32 Got = AsdaPartFxCount(A);
					S->Net->DebugFeedPacket(4023, BodyPart(Uidx, I.Slot, 0));
					const int32 After = AsdaPartFxCount(A);
					if (Want > 0 && Got == Want && After == 0) { ++Good; }
					else
					{
						++Bad;
						if (BadList.Len() < 400) { BadList += FString::Printf(TEXT(" %d(ждали %d, стало %d, снято %d)"), I.Id, Want, Got, After); }
					}
				}
				S->Check(A && Bad == 0 && Good == S->All.Num(), FString::Printf(TEXT("B%d 4021/4023 на соседе (%s): верно %d из %d%s"),
					bMale ? 1 : 2, bMale ? TEXT("муж") : TEXT("жен"), Good, S->All.Num(), *BadList));
			}));
			S->Steps.Add(Wait(0.3f));
		}
		S->Steps.Add(Do([S]()
		{
			for (const int32 U : { kPeerM, kPeerF }) { S->Net->DebugFeedPacket(4011, Body4011(U)); }
		}));
		S->Steps.Add(Wait(0.3f));

		// ═══ C. Наборы: 4005, кадры ═══
		for (int32 V = 1; V <= 3; ++V)
		{
			for (const int32 Uidx : { kPeerM, kPeerF })
			{
				const bool bMale = Uidx == kPeerM;
				const FString Tag = FString::Printf(TEXT("set%d_%s"), V, bMale ? TEXT("m") : TEXT("f"));
				S->Steps.Add(Do([S, V, Uidx, bMale]()
				{
					const FVector2D P = MeOf(*S)->GetGamePosXZ();
					S->Net->DebugFeedPacket(4005, Body4005(Uidx, bMale ? 1 : 2, S->Sets[V], P.X + 300.f, P.Y));
				}));
				S->Steps.Add(Wait(2.0f));
				S->Steps.Add(Do([S, V, Uidx, bMale]()
				{
					AAsdaPlayerCharacter* A = Peer(*S, Uidx);
					FString Why;
					const int32 Want = ExpectedSet(*S, S->Sets[V], bMale);
					const int32 Got = HealthyFx(A, Why);
					S->Check(A && Want >= 7 && Got == Want && AsdaPartFxCount(A) == Want,
						FString::Printf(TEXT("C вид %d %s: 4005, эффектов %d из %d, все с геометрией у тела%s"), V, bMale ? TEXT("муж") : TEXT("жен"), Got, Want, *Why));
					AimAt(*S, A, NAME_None, 300.f);
				}));
				S->Steps.Add(Wait(0.5f));
				S->Steps.Add(Do([S, Tag]() { Shot(*S, Tag + TEXT("_0_body")); }));
				struct FPart { const TCHAR* Name; const TCHAR* Bone; float Dist; };
				static const FPart kParts[] = { { TEXT("1_helmet"), TEXT("4"), 110.f }, { TEXT("2_chest"), TEXT("2"), 130.f }, { TEXT("3_gloves"), TEXT("7_b2"), 110.f }, { TEXT("4_boots"), TEXT("11"), 110.f } };
				for (const FPart& Pt : kParts)
				{
					const FPart* PP = &Pt;
					S->Steps.Add(Wait(0.25f));
					S->Steps.Add(Do([S, Uidx, PP]() { AimAt(*S, Peer(*S, Uidx), FName(PP->Bone), PP->Dist); }));
					S->Steps.Add(Wait(0.35f));
					S->Steps.Add(Do([S, Tag, PP]() { Shot(*S, Tag + TEXT("_") + PP->Name); }));
				}
				S->Steps.Add(Wait(0.3f));
				S->Steps.Add(Do([S, Uidx]() { S->Net->DebugFeedPacket(4011, Body4011(Uidx)); }));
				S->Steps.Add(Wait(0.3f));
			}
		}

		// ═══ T. Длина шлейфа: вид × пол × часть, ключ 0 и 1 ═══
		for (const int32 V : { 2, 3 })
		{
			for (const int32 Uidx : { kPeerM, kPeerF })
			{
				for (const int32 Part : { 0, 1 })   // 0 обувь, 1 шлем
				{
					const bool bMale = Uidx == kPeerM;
					const TCHAR* Name = Part == 0 ? TEXT("shoe305") : TEXT("hcap305");
					const FString Tag = FString::Printf(TEXT("trail_v%d_%s_%s"), V, bMale ? TEXT("m") : TEXT("f"), Part == 0 ? TEXT("boots") : TEXT("helmet"));
					TSharedRef<FTrail> Off = MakeShared<FTrail>();
					S->Steps.Add(Do([S, V, Uidx, bMale, Part]()
					{
						GEngine->Exec(S->W.Get(), TEXT("asda.Char.PartFxTrailMeters 0"));
						FSet One;
						if (Part == 0) { One.Shoes = S->Sets[V].Shoes; } else { One.Helmet = S->Sets[V].Helmet; }
						const FVector2D P = MeOf(*S)->GetGamePosXZ();
						S->Net->DebugFeedPacket(4005, Body4005(Uidx, bMale ? 1 : 2, One, P.X + 300.f, P.Y));
					}));
					S->Steps.Add(SimWait(3.6f));   // ★ R5-fix6: во времени симуляции эффекта (было `Wait(3.6f)` по часам)
					S->Steps.Add(Do([S, Uidx, Name, Off]()
					{
						AAsdaPlayerCharacter* A = Peer(*S, Uidx);
						*Off = Measure(A, Name);
						AimSide(*S, A, *Off);
					}));
					S->Steps.Add(Wait(0.3f));
					S->Steps.Add(Do([S, Tag]() { Shot(*S, Tag + TEXT("_key0")); GEngine->Exec(S->W.Get(), TEXT("asda.Char.PartFxTrailMeters 1")); }));
					// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: было `Wait(3.4f)` по часам. Ключ, сменённый на ходу, игра применяет к
					//   стоящему эффекту сверкой раз в 16 кадров (`AsdaPartFxTick`), а частицы прежнего поколения живут ещё свой срок
					//   (3 с данных): при 60 кадрах/с это 0,27 + 3 с — в 3,4 с укладывалось впритык, при кадре 100 мс (1,6 + 3 с) замер
					//   попадал на смесь старых и новых частиц — «путь 581 ед., плотность 24 против 31». Теперь: ждём СОБЫТИЕ «ключ
					//   применён» (жизнь эмиттера отличается от данных класса; потолок 10 с), затем 3,4 с времени симуляции эффекта.
					S->Steps.Add(FStep([S, Uidx, Name](float T)
					{
						return T >= 10.f || !Measure(Peer(*S, Uidx), Name).bLifeAsData;
					}));
					S->Steps.Add(SimWait(3.4f));
					S->Steps.Add(Do([S, Uidx, Name, Off, Tag]()
					{
						AAsdaPlayerCharacter* A = Peer(*S, Uidx);
						const FTrail On = Measure(A, Name);
						const float H = (A && A->BaseSkel) ? (float)(A->BaseSkel->GetBoneLocation(FName(TEXT("4"))).Z - A->GetActorLocation().Z) : 0.f;
						const float D0 = Off->Quads / FMath::Max(Off->Len, 1.f) * 100.f;
						const float D1 = On.Quads / FMath::Max(On.Len, 1.f) * 100.f;
						S->Check(Off->Len >= 300.f && Off->bLifeAsData, FString::Printf(TEXT("T %s ключ 0 (данные): путь частицы %.0f ед. (%.2f м), частиц %d, жизнь %.2f с"), *Tag, Off->Len, Off->Len / 100.f, Off->Quads, Off->Life));
						// ★ 11.11.2026 (D4): нижняя граница 185 → 165 ед. Длина меряется по вершинам живых частиц в момент замера и под
						//   нагрузкой плавает (эталон 7.6.0: 181 ед. — провал, на повторе зелёный). Смысл проверки тот же: при ключе 1
						//   след в разы короче, чем по данным (ключ 0: ≥ 300 ед.), и не длиннее 345.
						S->Check(On.Len >= 165.f && On.Len <= 345.f, FString::Printf(TEXT("T %s ключ 1: путь частицы %.0f ед. (%.2f м), частиц %d, жизнь %.2f с; высота до кости головы %.0f ед."), *Tag, On.Len, On.Len / 100.f, On.Quads, On.Life, H));
						S->Check(D1 >= D0 * 0.8f, FString::Printf(TEXT("T %s плотность: частиц на метр %.1f при ключе 0, %.1f при ключе 1"), *Tag, D0, D1));
						AimSide(*S, A, *Off);
					}));
					S->Steps.Add(Wait(0.3f));
					S->Steps.Add(Do([S, Tag]() { Shot(*S, Tag + TEXT("_key1")); S->CVars->Set(kKey, 0); }));
					S->Steps.Add(Wait(0.6f));
					S->Steps.Add(Do([S, Tag]() { Shot(*S, Tag + TEXT("_off")); S->CVars->Set(kKey, 1); }));
					S->Steps.Add(Wait(0.3f));
					S->Steps.Add(Do([S, Uidx]() { S->Net->DebugFeedPacket(4011, Body4011(Uidx)); }));
					S->Steps.Add(Wait(0.3f));
				}
			}
		}
		S->Steps.Add(Do([S]()
		{
			const FVector2D P = MeOf(*S)->GetGamePosXZ();
			S->Net->DebugFeedPacket(4005, Body4005(kPeerM, 1, S->Sets[1], P.X + 300.f, P.Y));
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* A = Peer(*S, kPeerM);
			const FTrail Boots = Measure(A, TEXT("shoe305"));
			const FTrail Helmet = Measure(A, TEXT("hcap305"));
			S->Check(Boots.bLifeAsData && Helmet.bLifeAsData && Boots.Life > 2.9f && Helmet.Life > 2.9f, FString::Printf(TEXT("T вид 1 при ключе 1: жизнь частиц как в данных (обувь %.2f с, шлем %.2f с)"), Boots.Life, Helmet.Life));
			S->Net->DebugFeedPacket(4011, Body4011(kPeerM));
		}));
		S->Steps.Add(Wait(0.3f));

		// ═══ D. Снятие, скрытие шлема, смена, ключ, срок жизни ═══
		S->Steps.Add(Do([S]()
		{
			const FVector2D P = MeOf(*S)->GetGamePosXZ();
			S->Net->DebugFeedPacket(4005, Body4005(kPeerM, 1, S->Sets[2], P.X + 300.f, P.Y));
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* A = Peer(*S, kPeerM);
			const FSet& V = S->Sets[2];
			const int32 Full = ExpectedSet(*S, V, true);
			const int32 HelmetFx = Expected(*S, V.Helmet, true);
			S->Net->DebugFeedPacket(4025, Body4025(kPeerM, 0));
			S->Check(AsdaPartFxCount(A) == Full - HelmetFx, FString::Printf(TEXT("D1 4025/111 шлем скрыт: эффектов %d (ждали %d)"), AsdaPartFxCount(A), Full - HelmetFx));
			S->Net->DebugFeedPacket(4025, Body4025(kPeerM, 1));
			S->Check(AsdaPartFxCount(A) == Full, FString::Printf(TEXT("D2 шлем показан: эффектов %d (ждали %d)"), AsdaPartFxCount(A), Full));
			if (S->PlainHelmet)
			{
				S->Net->DebugFeedPacket(4021, BodyPart(kPeerM, 0, S->PlainHelmet));
				S->Check(AsdaPartFxCount(A) == Full - HelmetFx, FString::Printf(TEXT("D3 шлем без .PEB %d: эффектов %d (ждали %d)"), S->PlainHelmet, AsdaPartFxCount(A), Full - HelmetFx));
				S->Net->DebugFeedPacket(4021, BodyPart(kPeerM, 0, V.Helmet));
			}
			S->CVars->Set(kKey, 0);
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* A = Peer(*S, kPeerM);
			S->Check(AsdaPartFxCount(A) == 0, FString::Printf(TEXT("D4 ключ 0 (как было): эффектов %d"), AsdaPartFxCount(A)));
			S->CVars->Set(kKey, 1);
			S->Net->DebugFeedPacket(4021, BodyPart(kPeerM, 0, S->Sets[2].Helmet));
		}));
		S->Steps.Add(Wait(12.0f));   // контейнер эффекта 3–5 с — проверка вечного цикла
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* A = Peer(*S, kPeerM);
			FString Why;
			const int32 Want = ExpectedSet(*S, S->Sets[2], true);
			const int32 Got = HealthyFx(A, Why);
			S->Check(Got == Want, FString::Printf(TEXT("D5 ключ 1 и 12 с цикла: эффектов %d из %d%s"), Got, Want, *Why));
			S->Net->DebugFeedPacket(4011, Body4011(kPeerM));
		}));
		S->Steps.Add(Wait(0.3f));

		// ═══ E. Свой персонаж ═══
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const bool bMale = Me->Sex != 2;
			const FSet& V = S->Sets[2];
			S->bWornTouched = true;
			Wear(*S, 0, V.Helmet); Wear(*S, 1, V.Chest); Wear(*S, 3, V.Shoes); Wear(*S, 4, V.Gloves);
			S->Check(AsdaPartFxCount(Me) == ExpectedSet(*S, V, bMale), FString::Printf(TEXT("E1 свой надел набор: эффектов %d (ждали %d)"), AsdaPartFxCount(Me), ExpectedSet(*S, V, bMale)));
		}));
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S]() { AimAt(*S, MeOf(*S), NAME_None, 300.f); }));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("self_set2")); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const bool bMale = Me->Sex != 2;
			const int32 Want = ExpectedSet(*S, S->Sets[2], bMale);
			Me->SetAnimState(EAsdaPlayerState::Dead);
			S->Check(AsdaPartFxCount(Me) == Want, FString::Printf(TEXT("E2 смерть (гейта у эталона нет): эффектов %d (ждали %d)"), AsdaPartFxCount(Me), Want));
			Me->SetAnimState(EAsdaPlayerState::Idle);
			Wear(*S, 4, 0);
			const int32 Rest = Want - Expected(*S, S->Sets[2].Gloves, bMale);
			S->Check(AsdaPartFxCount(Me) == Rest, FString::Printf(TEXT("E3 сняты перчатки: эффектов %d (ждали %d)"), AsdaPartFxCount(Me), Rest));
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPartFx(
	TEXT("asda.SelfTest.PartFx"),
	TEXT("Самотест эффектов частей .PEB (effLoadPebEffectSet): все вещи семейства 305 на соседе обоих полов (4021/4023), наборы 4005 с кадрами, ")
	TEXT("скрытие шлема, ключ asda.Char.PartFx, свой персонаж. Офлайн, карта мира. `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPartFxTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!Net || !WS || !Items || !Inv || !IConsoleManager::Get().FindConsoleVariable(kKey))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/предметов/инвентаря или ключа"));
			return;
		}
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS; SP->Items = Items; SP->Inv = Inv;
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		SP->CVars->Set(kKey, 1);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
