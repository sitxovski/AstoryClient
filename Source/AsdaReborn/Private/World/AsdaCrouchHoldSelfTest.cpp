// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ОРУЖИЕ И ЩИТ В ПРИСЕДЕ И СИДЯ» (07.11.2026, жалоба: присев, персонаж держит оружие неверно): `asda.SelfTest.CrouchHold [shot=<папка>] [quit]`. [ПОРТ]
//   Правка — `AAsdaPlayerCharacter::UpdateWeaponMotionHidden`, ключи `asda.Char.SitStowWeapon` и `asda.Crouch.StowWeapon`.
//   Офлайн (`-AsdaNoNet`), карта мира. Путь игрока: вещи — ячейки контейнера 3 (общая точка ответа 5001), спавн — пакет 4003, присед — `SetCrouch`
//   (Ctrl), сидение — `SetAnimState(Sit)` (та же точка, что действие «сесть»).
//     A) оба пола × все классы оружия со щитом: присед и сидение — оружие на "2_a1", щит на "_2_b1"; встал — снова в руке; кадры;
//     B) присед и замах: на время замаха оружие в руке, после него снова за спиной;
//     C) сосед (4005): присед `SetNetCrouch`, сидение — та же уборка;
//     D) контроль ключами 0: присед и сидение — оружие в руке (дефект виден).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Characters/AsdaCharAssembly.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Test/AsdaSelfTestKit.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCrouchHoldTest, Log, All);

namespace AsdaCrouchHoldTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.CrouchHold");   // имя команды
	constexpr int32 kMe = 4321;        // свой userNum (4003)
	constexpr int32 kPeer = 55;        // сосед со щитом и мечом

	// Шаг сценария: true — шаг завершён, false — ждёт следующего кадра. Аргумент — секунды с начала шага.
	using FStep = TFunction<bool(float)>;

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                     // мир
		bool bQuit = false;                           // `quit` в конце
		FString ShotDir;                              // папка кадров (пусто — без кадров)
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		TArray<FStep> Steps;                          // сценарий
		int32 Idx = 0;                                // текущий шаг
		double StepT0 = 0.0;                          // начало шага (время мира)
		FDelegateHandle H;                            // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер/уровень — вернуть после прогона
		TUniquePtr<FAsdaCVarScope> CVars;             // ключи — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TWeakObjectPtr<UAsdaItemTableSubsystem> Items;// таблицы предметов
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;  // инвентарь (контейнер 3 = надетое)
		TWeakObjectPtr<ACameraActor> Cam;             // камера кадров
		TWeakObjectPtr<AActor> OrigView;              // прежняя цель обзора
		FVector2D FieldPos = FVector2D::ZeroVector;   // клетка поля (спавн)
		int32 Shield = 0;                             // щит прогона
		int32 Sword = 0;                              // меч сценариев B и C
		uint8 OrigSex = 1;                            // пол до прогона
		int32 OrigShieldCell = 0, OrigWeaponCell = 0; // надетое до прогона
		bool bAbort = false;                          // прервать сценарий

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaCrouchHoldTest, Log,   TEXT("ПРИСЕД-ОРУЖИЕ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaCrouchHoldTest, Error, TEXT("ПРИСЕД-ОРУЖИЕ FAIL %s"), *What); }
		}
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Компонент части по метке (живой, зарегистрированный); nullptr — нет.
	static UStaticMeshComponent* PartOf(const AActor* A, FName Tag)
	{
		if (!A) { return nullptr; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* C : Comps)
		{
			if (IsValid(C) && C->IsRegistered() && C->ComponentHasTag(Tag)) { return C; }
		}
		return nullptr;
	}

	// Предмет ItemType `Type` (подтип `Sub`, −1 — любой) с загружаемым жёстким мешем; 0 — нет.
	static int32 FindStatic(UAsdaItemTableSubsystem* Items, int32 Type, int32 Sub, bool bMale)
	{
		for (int32 Id = 20000; Id < 140000; ++Id)
		{
			const FAsdaItemRecord* Rec = Items->GetItem(Id);
			if (!Rec || Rec->ItemType != Type || (Sub >= 0 && Rec->ItemSubType != Sub)) { continue; }
			const FString Mesh = Items->GetItemMeshName(Id, bMale);
			if (Mesh.IsEmpty() || !LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(Mesh))) { continue; }
			return Id;
		}
		return 0;
	}

	// Предмет ItemType `Type`, имя меша которого содержит `Part`; 0 — нет.
	static int32 FindByMesh(UAsdaItemTableSubsystem* Items, int32 Type, const TCHAR* Part, bool bMale)
	{
		for (int32 Id = 20000; Id < 140000; ++Id)
		{
			const FAsdaItemRecord* Rec = Items->GetItem(Id);
			if (!Rec || Rec->ItemType != Type) { continue; }
			const FString Mesh = Items->GetItemMeshName(Id, bMale);
			if (!Mesh.Contains(Part) || !LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(Mesh))) { continue; }
			return Id;
		}
		return 0;
	}

	// Тело 4003 (9 б): userNum, тайлы, состояние, показ шлема.
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Тело 4005 (151 б): сосед-мужчина, слоты экипа 8 (щит, +136) и 9 (оружие, +141), позиция game XZ.
	static TArray<uint8> Body4005(int32 Uidx, int32 ShieldId, int32 WeaponId, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Uidx);
		B[6] = 1;                                             // шлем показан
		AsdaBytes::WrI32(B.GetData() + 11, 980000 + Uidx);    // charId — заведомо не свой
		const char* Name = "CrouchHold";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 1; B[38] = 1; B[40] = 10;                     // пол, класс, уровень
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 8, ShieldId); // слот 8 — щит
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * 9, WeaponId); // слот 9 — оружие
		return B;
	}

	// Надеть/снять вещь своему — ячейка контейнера 3 (общая точка ответа 5001).
	static void Wear(FState& S, int32 Slot, int32 ItemId)
	{
		UAsdaInventorySubsystem* Inv = S.Inv.Get();
		if (Inv && Inv->DebugPutItem(3, Slot, ItemId, 1)) { return; }
		if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(Slot, ItemId, 0); }
	}

	// Камера кадров (создаётся один раз); обзор переключается на неё.
	static ACameraActor* CamOf(FState& S)
	{
		UWorld* W = S.W.Get();
		if (!W) { return nullptr; }
		if (!S.Cam.IsValid())
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACameraActor* C = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator, P);
			if (C && C->GetCameraComponent())
			{
				C->GetCameraComponent()->SetConstraintAspectRatio(false);
				C->GetCameraComponent()->SetFieldOfView(40.f);
			}
			S.Cam = C;
			if (APlayerController* PC = W->GetFirstPlayerController()) { S.OrigView = PC->GetViewTarget(); PC->SetViewTarget(C); }
		}
		return S.Cam.Get();
	}

	// Поставить камеру вокруг персонажа: `AngleDeg` — от направления «вперёд» актёра (перед актёра = левый бок модели: 0 левый бок, 90 перед, 180 правый бок, 270 спина).
	static void Aim(FState& S, float AngleDeg)
	{
		AAsdaPlayerCharacter* Me = MeOf(S);
		ACameraActor* C = CamOf(S);
		if (!Me || !C || !Me->BaseSkel) { return; }
		const FVector Mid = Me->BaseSkel->GetSocketLocation(FName(TEXT("2")));   // грудь: кость «2»
		const FVector Dir = Me->GetActorForwardVector().RotateAngleAxis(AngleDeg, FVector::UpVector);
		const FVector Eye = Mid + Dir * 190.f + FVector(0.f, 0.f, 25.f);
		C->SetActorLocationAndRotation(Eye, (Mid - Eye).Rotation());
	}

	// Заказать кадр.
	static void Shot(const FState& S, const FString& Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(S.ShotDir, Name + TEXT(".png")), /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
	}

	// Конец прогона.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (UWorld* W = S.W.Get())
		{
			if (APlayerController* PC = W->GetFirstPlayerController()) { if (S.OrigView.IsValid()) { PC->SetViewTarget(S.OrigView.Get()); } }
		}
		if (ACameraActor* C = S.Cam.Get()) { C->Destroy(); }
		if (UAsdaWorldSubsystem* WS = S.WS.Get())
		{
			if (AAsdaPlayerCharacter* Me = MeOf(S))
			{
				Me->NetSnap(S.FieldPos);
				WS->SelfSex = S.OrigSex;
				Me->InitAppearance(S.OrigSex, WS->VisualSelfParts());
			}
		}
		Wear(S, 9, S.OrigWeaponCell);
		Wear(S, 8, S.OrigShieldCell);
		S.CVars.Reset();
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			if (MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
		}
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaCrouchHoldTest, TEXT("ПРИСЕД-ОРУЖИЕ"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;   // своя ссылка: `Finish` снимает подписку вместе с копией в лямбде
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

	// Журнал позы: признак уборки, кости оружия и щита, направление клинка (локальная UE −Y) в пространстве компонента.
	static void LogHold(FState& S, const FString& Lbl)
	{
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (!Me || !Me->BaseSkel) { return; }
		const UStaticMeshComponent* Sh = PartOf(Me, UAsdaCharAssembly::ShieldMeshTag);
		const UStaticMeshComponent* Wc = PartOf(Me, UAsdaCharAssembly::WeaponMeshTag);
		FVector Tip = FVector::ZeroVector;
		if (Wc) { Tip = Me->BaseSkel->GetSocketTransform(Wc->GetAttachSocketName(), RTS_Component).GetRotation().RotateVector(FVector(0.f, -1.f, 0.f)); }
		UE_LOG(LogAsdaCrouchHoldTest, Log, TEXT("ПРИСЕД-ОРУЖИЕ поза %s: убрано %d, присед на экране %d, сидит %d, оружие на %s (клинок %.2f %.2f %.2f), щит на %s"), *Lbl, (int32)Me->bWeaponMotionHidden,
			(int32)Me->IsCrouchWalkShown(), Me->GetEtalonSitState(), Wc ? *Wc->GetAttachSocketName().ToString() : TEXT("нет"), Tip.X, Tip.Y, Tip.Z, Sh ? *Sh->GetAttachSocketName().ToString() : TEXT("нет"));
	}

	// Проверка кости оружия и щита: `bStowed` — за спиной ("2_a1", "_2_b1"), иначе в руке (щит "_7_a1", оружие "8_a1" или "_8_a1").
	static void CheckHold(FState& S, const AAsdaPlayerCharacter* Ch, bool bStowed, const FString& What)
	{
		const UStaticMeshComponent* Sh = PartOf(Ch, UAsdaCharAssembly::ShieldMeshTag);
		const UStaticMeshComponent* Wc = PartOf(Ch, UAsdaCharAssembly::WeaponMeshTag);
		const FName WB = Wc ? Wc->GetAttachSocketName() : NAME_None;
		const bool bW = !Wc || (bStowed ? WB == FName(TEXT("2_a1")) : (WB == FName(TEXT("8_a1")) || WB == FName(TEXT("_8_a1"))));
		const bool bS = Sh && Sh->GetAttachSocketName() == (bStowed ? FName(TEXT("_2_b1")) : FName(TEXT("_7_a1")));
		S.Check(Ch && Ch->bWeaponMotionHidden == bStowed && bW && bS, FString::Printf(TEXT("%s: убрано %d, оружие на %s, щит на %s"), *What, Ch ? (int32)Ch->bWeaponMotionHidden : -1,
			*WB.ToString(), Sh ? *Sh->GetAttachSocketName().ToString() : TEXT("нет")));
	}

	// Сценарий.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };
		auto Shots = [S, Do, Wait](const FString& Lbl)
		{
			for (const TPair<const TCHAR*, float>& V : TArray<TPair<const TCHAR*, float>>{ { TEXT("left"), 0.f }, { TEXT("front"), 90.f }, { TEXT("right"), 180.f }, { TEXT("back"), 270.f } })
			{
				const FString Name = Lbl + TEXT("_") + V.Key; const float A = V.Value;
				S->Steps.Add(Do([S, A]() { Aim(*S, A); }));
				S->Steps.Add(Wait(0.25f));
				S->Steps.Add(Do([S, Name]() { Shot(*S, Name); }));
				S->Steps.Add(Wait(0.25f));
			}
		};

		S->Steps.Add(Do([S]()
		{
			if (const UAsdaInventorySubsystem* Inv = S->Inv.Get())
			{
				const FAsdaItemInstance* C8 = Inv->GetCell(3, 8);
				const FAsdaItemInstance* C9 = Inv->GetCell(3, 9);
				S->OrigShieldCell = (C8 && !C8->IsEmpty()) ? C8->ItemCode : 0;
				S->OrigWeaponCell = (C9 && !C9->IsEmpty()) ? C9->ItemCode : 0;
			}
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			S->Net->DebugFeedPacket(4003, Body4003(kMe));
		}));
		S->Steps.Add(Wait(4.0f));   // столб света появления
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(Me != nullptr, TEXT("свой павн есть"));
			if (!Me) { S->bAbort = true; return; }
			S->OrigSex = S->WS->SelfSex;
			S->FieldPos = Me->GetGamePosXZ();
		}));

		// Классы оружия (подтипы таблицы), −1 — только щит, без оружия.
		const int32 Classes[] = { 0, 1, 2, 3, 5, 9, 10, 11, -1 };
		for (const uint8 Sex : { (uint8)1, (uint8)2 })
		{
			for (const int32 Wt : Classes)
			{
				const FString Lbl = FString::Printf(TEXT("%s_w%d"), Sex == 1 ? TEXT("m") : TEXT("f"), Wt);
				S->Steps.Add(Do([S, Sex, Wt]()
				{
					AAsdaPlayerCharacter* Me = MeOf(*S);
					UAsdaItemTableSubsystem* Items = S->Items.Get();
					S->WS->SelfSex = Sex;
					const bool bMale = Sex == 1;
					S->Shield = FindByMesh(Items, 8, TEXT("shid103"), bMale);
					const int32 Wp = Wt >= 0 ? FindStatic(Items, 9, Wt, bMale) : 0;
					Me->NetSnap(S->FieldPos);
					Wear(*S, 9, Wp);
					Wear(*S, 8, S->Shield);
					Me->InitAppearance(Sex, S->WS->VisualSelfParts());
				}));
				S->Steps.Add(Wait(0.6f));
				S->Steps.Add(Do([S, Lbl]() { S->Check(MeOf(*S)->SetCrouch(true, TEXT("самотест")), FString::Printf(TEXT("%s присед включён"), *Lbl)); }));
				S->Steps.Add(Wait(0.8f));
				S->Steps.Add(Do([S, Lbl]() { LogHold(*S, Lbl + TEXT(" присед")); CheckHold(*S, MeOf(*S), true, Lbl + TEXT(" присед")); }));
				Shots(Lbl + TEXT("_crouch"));
				S->Steps.Add(Do([S]() { MeOf(*S)->SetCrouch(false, TEXT("самотест")); }));
				S->Steps.Add(Wait(0.4f));
				S->Steps.Add(Do([S]() { MeOf(*S)->SetAnimState(EAsdaPlayerState::Sit); }));
				S->Steps.Add(Wait(2.0f));
				S->Steps.Add(Do([S, Lbl]() { LogHold(*S, Lbl + TEXT(" сидит")); CheckHold(*S, MeOf(*S), true, Lbl + TEXT(" сидит")); }));
				Shots(Lbl + TEXT("_sit"));
				S->Steps.Add(Do([S]() { MeOf(*S)->SetAnimState(EAsdaPlayerState::Idle); }));
				S->Steps.Add(Wait(3.0f));   // хвост вставания: состояние 9 держит оружие за спиной
				S->Steps.Add(Do([S, Lbl]() { LogHold(*S, Lbl + TEXT(" встал")); CheckHold(*S, MeOf(*S), false, Lbl + TEXT(" встал")); }));
			}
		}

		// ═══ B. Присед и замах ═══
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			UAsdaItemTableSubsystem* Items = S->Items.Get();
			S->WS->SelfSex = 1;
			S->Shield = FindByMesh(Items, 8, TEXT("shid103"), true);
			S->Sword = FindStatic(Items, 9, 0, true);
			Wear(*S, 9, S->Sword);
			Wear(*S, 8, S->Shield);
			Me->InitAppearance(1, S->WS->VisualSelfParts());
			Me->SetCrouch(true, TEXT("самотест"));
		}));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			CheckHold(*S, Me, true, TEXT("B присед до замаха"));
			Me->StartAttackSwing(0, /*HoldSec=*/1.5f);
		}));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]() { CheckHold(*S, MeOf(*S), false, TEXT("B замах из приседа: оружие в руке")); }));
		S->Steps.Add(FStep([S](float T) { AAsdaPlayerCharacter* Me = MeOf(*S); return T > 6.f || (!Me->IsAttackAnimActive() && !Me->IsAttackLoopAlive()); }));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]()
		{
			CheckHold(*S, MeOf(*S), true, TEXT("B замах кончился: снова за спиной"));
			MeOf(*S)->SetCrouch(false, TEXT("самотест"));
			const FVector2D P = S->FieldPos + FVector2D(200.f, 0.f);
			S->Net->DebugFeedPacket(4005, Body4005(kPeer, S->Shield, S->Sword, (float)P.X, (float)P.Y));
		}));

		// ═══ C. Сосед ═══
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(S->WS->GetPlayerActorByUserNum(kPeer));
			S->Check(P != nullptr, TEXT("сосед со щитом и мечом стоит в поле"));
			if (!P) { S->bAbort = true; return; }
			CheckHold(*S, P, false, TEXT("C сосед стоит"));
			P->SetNetCrouch(true);
		}));
		S->Steps.Add(Wait(0.6f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(S->WS->GetPlayerActorByUserNum(kPeer));
			CheckHold(*S, P, true, TEXT("C сосед присел"));
			P->SetNetCrouch(false);
			P->SetAnimState(EAsdaPlayerState::Sit);
		}));
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(S->WS->GetPlayerActorByUserNum(kPeer));
			CheckHold(*S, P, true, TEXT("C сосед сидит"));
			P->SetAnimState(EAsdaPlayerState::Idle);
			TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(B.GetData(), (int16)kPeer);
			S->Net->DebugFeedPacket(4011, B);   // сосед ушёл из видимости
		}));

		// ═══ D. Контроль ключами 0: дефект виден ═══
		S->Steps.Add(Do([S]()
		{
			S->CVars->Set(TEXT("asda.Crouch.StowWeapon"), 0);
			S->CVars->Set(TEXT("asda.Char.SitStowWeapon"), 0);
			MeOf(*S)->SetCrouch(true, TEXT("самотест"));
		}));
		S->Steps.Add(Wait(0.8f));
		S->Steps.Add(Do([S]()
		{
			CheckHold(*S, MeOf(*S), false, TEXT("КОНТРОЛЬ ключ asda.Crouch.StowWeapon 0: присед, оружие в руке"));
			MeOf(*S)->SetCrouch(false, TEXT("самотест"));
		}));
		S->Steps.Add(Wait(0.4f));
		S->Steps.Add(Do([S]() { MeOf(*S)->SetAnimState(EAsdaPlayerState::Sit); }));
		S->Steps.Add(Wait(2.0f));
		S->Steps.Add(Do([S]()
		{
			CheckHold(*S, MeOf(*S), false, TEXT("КОНТРОЛЬ ключ asda.Char.SitStowWeapon 0: сидит, оружие в руке"));
			MeOf(*S)->SetAnimState(EAsdaPlayerState::Idle);
		}));
		S->Steps.Add(Wait(3.0f));
		S->Steps.Add(Do([S]()
		{
			S->CVars->Set(TEXT("asda.Crouch.StowWeapon"), 1);
			S->CVars->Set(TEXT("asda.Char.SitStowWeapon"), 1);
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCrouchHold(
	TEXT("asda.SelfTest.CrouchHold"),
	TEXT("Самотест оружия и щита в приседе и сидя: оба пола, все классы оружия, признак уборки, кости и кадры вблизи. Офлайн, карта мира. "
	     "`shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaCrouchHoldTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!Net || !WS || !Items || !Inv) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/предметов/инвентаря")); return; }
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
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
