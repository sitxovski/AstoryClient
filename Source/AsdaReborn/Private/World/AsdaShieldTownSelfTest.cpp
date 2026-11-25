// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ЩИТ И ОРУЖИЕ В МИРНОЙ СТОЙКЕ» (07.11.2026, жалоба: щит воина в городе перевёрнут): `asda.SelfTest.ShieldTown [shot=<папка>] [quit]`. [ПОРТ]
//   Правка и разбор — `UAsdaCharAssembly::WeaponGlowBone`, ключ `asda.Char.ShieldStowBackBone`.
//   Офлайн (`-AsdaNoNet`), карта мира. Путь игрока: вещи — ячейки контейнера 3 (`DebugPutItem` → общая точка ответа 5001), спавн — пакет 4003,
//   мирная зона — свой персонаж ставится на клетку с битом 0x80, оружие и щит уезжают за спину штатным тиком `UpdateWeaponMotionHidden`.
//   Проверка — геометрия в пространстве компонента скелета («назад» — перпендикуляр линии плеч, «вверх» — UE +Z): лицевая сторона щита (локальная UE +Z =
//   игровая +Y) смотрит наружу от спины, верх (локальная UE −Y = игровая +Z) — вверх.
//     A) оба пола × все классы оружия: поле — щит на "_7_a1"; город — на "_2_b1", лицо наружу, верх вверх; кадры;
//     B) оба пола × все меши щитов таблицы: кость и ориентация в городе, толщина меша — вдоль локальной UE Z;
//     C) транспорт и сосед (4005 со щитом в городе) — та же кость и ориентация;
//     D) контроль `asda.Char.ShieldStowBackBone 0`: щит на "_2_a1", лицо к телу, верх вниз — дефект виден.
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
#include "World/AsdaWalkGrid.h"
#include "World/AsdaCoord.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogAsdaShieldTownTest, Log, All);

namespace AsdaShieldTownTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.ShieldTown");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Char.ShieldStowBackBone");   // ключ исправления
	constexpr int32 kMe = 4321;        // свой userNum (4003)
	constexpr int32 kPeer = 54;        // сосед со щитом

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
		FVector2D TownPos = FVector2D::ZeroVector;    // клетка мирной зоны
		bool bTownFound = false;                      // мирная клетка найдена
		int32 Shield = 0;                             // щит прогона
		int32 MountCode = 0;                          // транспорт (SubType 15)
		uint8 OrigSex = 1;                            // пол до прогона
		int32 OrigShieldCell = 0, OrigWeaponCell = 0; // надетое до прогона
		bool bAbort = false;                          // прервать сценарий

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaShieldTownTest, Log,   TEXT("ЩИТ-ГОРОД ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaShieldTownTest, Error, TEXT("ЩИТ-ГОРОД FAIL %s"), *What); }
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
		const char* Name = "ShieldTown";
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

	// Ориентация щита в пространстве компонента скелета: `OutBack` — косинус «лицо ↔ назад от спины», `OutUp` — косинус «верх щита ↔ вверх».
	static bool ShieldAxes(const AAsdaPlayerCharacter* Ch, float& OutBack, float& OutUp)
	{
		const UStaticMeshComponent* Sh = PartOf(Ch, UAsdaCharAssembly::ShieldMeshTag);
		if (!Sh || !Ch->BaseSkel) { return false; }
		const FTransform Bone = Ch->BaseSkel->GetSocketTransform(Sh->GetAttachSocketName(), RTS_Component);
		// «Назад от спины» — перпендикуляр линии плеч: женский покой развёрнут корпусом на ~50° от оси модели, ось модели спиной не считается.
		const FVector Shoulders = Ch->BaseSkel->GetSocketTransform(FName(TEXT("2_a1")), RTS_Component).GetLocation() - Ch->BaseSkel->GetSocketTransform(FName(TEXT("_2_a1")), RTS_Component).GetLocation();
		const FVector BackDir = FVector(-Shoulders.Y, Shoulders.X, 0.f).GetSafeNormal();
		OutBack = (float)FVector::DotProduct(Bone.GetRotation().RotateVector(FVector::UpVector), BackDir);
		OutUp = (float)FVector::DotProduct(Bone.GetRotation().RotateVector(FVector(0.f, -1.f, 0.f)), FVector::UpVector);
		return true;
	}

	// Проверка «щит за спиной как надо»: кость `_2_b1`, лицо наружу, верх вверх (порог — косинус 45°).
	static void CheckStowed(FState& S, const AAsdaPlayerCharacter* Ch, const FString& What)
	{
		const UStaticMeshComponent* Sh = PartOf(Ch, UAsdaCharAssembly::ShieldMeshTag);
		float Back = 0.f, Up = 0.f;
		const bool bAxes = ShieldAxes(Ch, Back, Up);
		S.Check(Sh && bAxes && Sh->GetAttachSocketName() == FName(TEXT("_2_b1")) && Back > 0.7f && Up > 0.7f,
			FString::Printf(TEXT("%s: щит на %s, лицо наружу %.2f, верх вверх %.2f"), *What, Sh ? *Sh->GetAttachSocketName().ToString() : TEXT("нет"), Back, Up));
	}

	// Журнал позы: кости в пространстве компонента (сверка с сырым клипом).
	static void LogBones(const FState& S, const TCHAR* Tag)
	{
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (!Me || !Me->BaseSkel) { return; }
		for (const TCHAR* Bn : { TEXT("2"), TEXT("2_a1"), TEXT("_2_a1"), TEXT("_2_b1"), TEXT("_7_a1"), TEXT("8_a1"), TEXT("_8_a1") })
		{
			const FTransform T = Me->BaseSkel->GetSocketTransform(FName(Bn), RTS_Component);
			const FQuat Q = T.GetRotation();
			UE_LOG(LogAsdaShieldTownTest, Log, TEXT("ЩИТ-ГОРОД кость %s %s: pos %.2f %.2f %.2f q %.4f %.4f %.4f %.4f s %.3f %.3f %.3f"), Tag, Bn,
				T.GetLocation().X, T.GetLocation().Y, T.GetLocation().Z, Q.X, Q.Y, Q.Z, Q.W, T.GetScale3D().X, T.GetScale3D().Y, T.GetScale3D().Z);
		}
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
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaShieldTownTest, TEXT("ЩИТ-ГОРОД"), S.Ok, S.Fail);
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

	// Сценарий.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };

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
			const UAsdaWalkGrid* G = Me->WalkGrid;
			// Мирная клетка: первая при обходе сетки, вокруг которой мирная зона тоже есть.
			if (G)
			{
				for (int32 Tz = 8; Tz < G->Height - 8 && !S->bTownFound; ++Tz)
				{
					for (int32 Tx = 8; Tx < G->Width - 8 && !S->bTownFound; ++Tx)
					{
						const FVector2D P = AsdaCoord::TileCenterGame(FIntPoint(Tx, Tz));
						// Вокруг — окно 13×13 мирных проходимых клеток: камера кадра не упирается в здание.
						bool bOpen = true;
						for (int32 Dz = -6; Dz <= 6 && bOpen; ++Dz)
						{
							for (int32 Dx = -6; Dx <= 6 && bOpen; ++Dx)
							{
								const FVector2D Q = AsdaCoord::TileCenterGame(FIntPoint(Tx + Dx, Tz + Dz));
								bOpen = G->IsWorldPeaceZone(Q.X, Q.Y) && G->IsTileWalkable(Tx + Dx, Tz + Dz);
							}
						}
						if (bOpen) { S->TownPos = P; S->bTownFound = true; }
					}
				}
			}
			S->Check(S->bTownFound, FString::Printf(TEXT("мирная клетка карты: game (%.0f, %.0f)"), S->TownPos.X, S->TownPos.Y));
			if (!S->bTownFound) { S->bAbort = true; }
		}));

		// Классы оружия (подтипы таблицы), −1 — только щит, без оружия.
		const int32 Classes[] = { 0, 1, 2, 3, 4, 5, 8, 9, 10, 11, -1 };
		for (const uint8 Sex : { (uint8)1, (uint8)2 })
		{
			for (const int32 Wt : Classes)
			{
				const FString Lbl = FString::Printf(TEXT("%s_w%d"), Sex == 1 ? TEXT("m") : TEXT("f"), Wt);
				const bool bFull = Wt == 0;   // полный набор кадров — на первом классе, остальные — спина и бок
				S->Steps.Add(Do([S, Sex, Wt]()
				{
					AAsdaPlayerCharacter* Me = MeOf(*S);
					UAsdaItemTableSubsystem* Items = S->Items.Get();
					S->WS->SelfSex = Sex;
					const bool bMale = Sex == 1;
					S->Shield = FindByMesh(Items, 8, TEXT("shid103"), bMale);   // каплевидный щит: верх и низ видно сразу
					if (S->Shield == 0) { S->Shield = FindStatic(Items, 8, -1, bMale); }
					const int32 Wp = Wt >= 0 ? FindStatic(Items, 9, Wt, bMale) : 0;
					Me->NetSnap(S->FieldPos);
					Wear(*S, 9, Wp);
					Wear(*S, 8, S->Shield);
					Me->InitAppearance(Sex, S->WS->VisualSelfParts());
					UE_LOG(LogAsdaShieldTownTest, Log, TEXT("ЩИТ-ГОРОД вещи: пол %d, подтип %d, оружие %d «%s», щит %d «%s»"), (int32)Sex, Wt, Wp, *Items->GetItemMeshName(Wp, bMale),
						S->Shield, *Items->GetItemMeshName(S->Shield, bMale));
				}));
				S->Steps.Add(Wait(0.8f));
				S->Steps.Add(Do([S, Lbl]()
				{
					const UStaticMeshComponent* Sh = PartOf(MeOf(*S), UAsdaCharAssembly::ShieldMeshTag);
					S->Check(Sh && Sh->GetAttachSocketName() == FName(TEXT("_7_a1")), FString::Printf(TEXT("%s поле: щит в руке на %s"), *Lbl, Sh ? *Sh->GetAttachSocketName().ToString() : TEXT("нет")));
				}));
				if (bFull)
				{
					S->Steps.Add(Do([S, Lbl]()
					{
						LogBones(*S, *(Lbl + TEXT(" поле")));
						Aim(*S, 0.f);
					}));
					S->Steps.Add(Wait(0.3f));
					S->Steps.Add(Do([S, Lbl]() { Shot(*S, Lbl + TEXT("_field_left")); }));
					S->Steps.Add(Wait(0.3f));
				}
				S->Steps.Add(Do([S]() { if (AAsdaPlayerCharacter* Me = MeOf(*S)) { Me->NetSnap(S->TownPos); } }));
				S->Steps.Add(Wait(1.2f));
				S->Steps.Add(Do([S, Lbl]()
				{
					AAsdaPlayerCharacter* Me = MeOf(*S);
					UStaticMeshComponent* Sh = PartOf(Me, UAsdaCharAssembly::ShieldMeshTag);
					UStaticMeshComponent* Wc = PartOf(Me, UAsdaCharAssembly::WeaponMeshTag);
					S->Check(Me && Me->bWeaponMotionHidden, FString::Printf(TEXT("%s город: оружие убрано по биту 0x80 (%d)"), *Lbl, Me ? (int32)Me->bWeaponMotionHidden : -1));
					CheckStowed(*S, Me, Lbl + TEXT(" город"));
					UE_LOG(LogAsdaShieldTownTest, Log, TEXT("ЩИТ-ГОРОД %s: оружие на %s"), *Lbl, Wc ? *Wc->GetAttachSocketName().ToString() : TEXT("нет"));
					if (Sh && Me && Sh->GetStaticMesh())
					{
						const FTransform Rel = Sh->GetComponentTransform().GetRelativeTransform(Me->BaseSkel->GetComponentTransform());
						const FBox Bb = Sh->GetStaticMesh()->GetBoundingBox();
						const FQuat Q = Rel.GetRotation();
						UE_LOG(LogAsdaShieldTownTest, Log, TEXT("ЩИТ-ГОРОД меш %s: %s bbox min %s max %s; к компоненту скелета pos %s q %.4f %.4f %.4f %.4f s %s; мировой масштаб %s"), *Lbl,
							*Sh->GetStaticMesh()->GetName(), *Bb.Min.ToString(), *Bb.Max.ToString(), *Rel.GetLocation().ToString(), Q.X, Q.Y, Q.Z, Q.W, *Rel.GetScale3D().ToString(),
							*Sh->GetComponentScale().ToString());
					}
					LogBones(*S, *(Lbl + TEXT(" город")));
					Aim(*S, 270.f);
				}));
				S->Steps.Add(Wait(0.3f));
				S->Steps.Add(Do([S, Lbl]() { Shot(*S, Lbl + TEXT("_town_back")); }));
				S->Steps.Add(Wait(0.3f));
				S->Steps.Add(Do([S]() { Aim(*S, 0.f); }));
				S->Steps.Add(Wait(0.3f));
				S->Steps.Add(Do([S, Lbl]() { Shot(*S, Lbl + TEXT("_town_left")); }));
				S->Steps.Add(Wait(0.3f));
				if (bFull)
				{
					S->Steps.Add(Do([S]() { Aim(*S, 90.f); }));
					S->Steps.Add(Wait(0.3f));
					S->Steps.Add(Do([S, Lbl]() { Shot(*S, Lbl + TEXT("_town_front")); }));
					S->Steps.Add(Wait(0.3f));
				}
			}

			// ═══ B. Все меши щитов таблицы: город, тот же пол ═══
			S->Steps.Add(Do([S, Sex]()
			{
				AAsdaPlayerCharacter* Me = MeOf(*S);
				UAsdaItemTableSubsystem* Items = S->Items.Get();
				const bool bMale = Sex == 1;
				TSet<FString> Seen;
				int32 Bad = 0;
				FString BadList;
				for (int32 Id = 20000; Id < 140000; ++Id)
				{
					const FAsdaItemRecord* Rec = Items->GetItem(Id);
					if (!Rec || Rec->ItemType != 8) { continue; }
					const FString Mesh = Items->GetItemMeshName(Id, bMale);
					if (Mesh.IsEmpty() || Seen.Contains(Mesh)) { continue; }
					const UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(Mesh));
					if (!SM) { continue; }
					Seen.Add(Mesh);
					Wear(*S, 8, Id);
					const UStaticMeshComponent* Sh = PartOf(Me, UAsdaCharAssembly::ShieldMeshTag);
					float Back = 0.f, Up = 0.f;
					const FVector Sz = SM->GetBoundingBox().GetSize();
					const bool bOk = Sh && Sh->GetStaticMesh() == SM && Sh->GetAttachSocketName() == FName(TEXT("_2_b1")) && ShieldAxes(Me, Back, Up) && Back > 0.7f && Up > 0.7f
						&& Sz.Z < Sz.Y;   // толщина щита меньше высоты: «лицо» — локальная Z (у шипастого shid301 шип шире самого щита по X)
					if (!bOk) { ++Bad; if (BadList.Len() < 400) { BadList += FString::Printf(TEXT(" %d(%s %.0f/%.0f/%.0f)"), Id, *Mesh, Sz.X, Sz.Y, Sz.Z); } }
				}
				S->Check(Seen.Num() >= 10 && Bad == 0, FString::Printf(TEXT("пол %d: все меши щитов за спиной: %d мешей, ошибок %d%s"), (int32)Sex, Seen.Num(), Bad, *BadList));
				Wear(*S, 8, S->Shield);
			}));
		}

		// ═══ C. Транспорт и сосед ═══
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->WS->SelfSex = 1;
			Me->NetSnap(S->FieldPos);
			Wear(*S, 9, FindStatic(S->Items.Get(), 9, 0, true));
			Wear(*S, 8, S->Shield);
			Me->InitAppearance(1, S->WS->VisualSelfParts());
			for (int32 Id = 1; Id <= 4000 && S->MountCode == 0; ++Id)
			{
				FString Mdl, Set; float H = 0.f;
				if (S->WS->LookupMount(Id, Mdl, Set, H)) { S->MountCode = Id; }
			}
			S->Check(S->MountCode != 0, FString::Printf(TEXT("транспорт для проверки: %d"), S->MountCode));
			if (S->MountCode) { S->Net->OnMountState.Broadcast(kMe, S->MountCode, true); }
		}));
		S->Steps.Add(Wait(1.0f));
		S->Steps.Add(Do([S]()
		{
			CheckStowed(*S, MeOf(*S), TEXT("транспорт в поле"));
			Aim(*S, 270.f);
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("mount_back")); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			S->Net->OnMountState.Broadcast(kMe, S->MountCode, false);
			MeOf(*S)->NetSnap(S->TownPos);
			const FVector2D P = S->TownPos + FVector2D(200.f, 0.f);
			S->Net->DebugFeedPacket(4005, Body4005(kPeer, S->Shield, FindStatic(S->Items.Get(), 9, 0, true), (float)P.X, (float)P.Y));
		}));
		S->Steps.Add(Wait(1.5f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(S->WS->GetPlayerActorByUserNum(kPeer));
			S->Check(P != nullptr, TEXT("сосед со щитом стоит в городе"));
			if (P) { CheckStowed(*S, P, TEXT("сосед в городе")); }
		}));

		// ═══ D. Контроль: ключ 0 — как эталон, дефект виден ═══
		S->Steps.Add(Do([S]()
		{
			S->CVars->Set(kKey, 0);
			Wear(*S, 8, 0);
			Wear(*S, 8, S->Shield);   // пересборка щита по ключу
			if (AAsdaPlayerCharacter* Me = MeOf(*S)) { Me->RefreshWeaponStow(); }
		}));
		S->Steps.Add(Wait(0.5f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const UStaticMeshComponent* Sh = PartOf(Me, UAsdaCharAssembly::ShieldMeshTag);
			float Back = 0.f, Up = 0.f;
			const bool bAxes = ShieldAxes(Me, Back, Up);
			S->Check(Sh && bAxes && Sh->GetAttachSocketName() == FName(TEXT("_2_a1")) && Back < -0.5f && Up < 0.f,
				FString::Printf(TEXT("КОНТРОЛЬ ключ 0: дефект виден — щит на %s, лицо наружу %.2f, верх вверх %.2f"), Sh ? *Sh->GetAttachSocketName().ToString() : TEXT("нет"), Back, Up));
			Aim(*S, 270.f);
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]() { Shot(*S, TEXT("control_key0_back")); }));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			S->CVars->Set(kKey, 1);
			Wear(*S, 8, S->Shield);
			if (AAsdaPlayerCharacter* Me = MeOf(*S)) { Me->RefreshWeaponStow(); }
			TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(B.GetData(), (int16)kPeer);
			S->Net->DebugFeedPacket(4011, B);   // сосед ушёл из видимости
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestShieldTown(
	TEXT("asda.SelfTest.ShieldTown"),
	TEXT("Самотест щита и оружия в мирной стойке: оба пола, все классы оружия, поле и город (бит 0x80), кости и кадры вблизи. Офлайн, карта мира. "
	     "`shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaShieldTownTest;
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
		SP->CVars->Set(kKey, 1);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
