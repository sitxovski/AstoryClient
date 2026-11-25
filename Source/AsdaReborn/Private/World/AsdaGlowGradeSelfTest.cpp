// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ЦВЕТА СВЕЧЕНИЯ ПО КАЧЕСТВУ (08.11.2026): `asda.SelfTest.GlowGrade [shot=<папка>] [quit]`. [ПОРТ]
//   Баг владельца: свечение оружия Hero белое или серое, а должно быть зелёным. Эталон — `CCharacter_BuildWeaponJebFileName@0xA4CB10`
//   (оружие) и `itemBuildJebEffectName@0xA4E790` (щит): «<база>_<буква>_<качество>_ma_<ступень>», качество 0 co, 1 uc, 2 ra, 3 he, 4 le.
//   Офлайн (`-AsdaNoNet`), карта мира. Сосед — пакеты 4005/4021 в общий разбор входящих.
//     A) все оружия и щиты таблицы: имя .jeb по формуле, BP грузится либо файла нет и у эталона;
//     B) класс × качество на заточке +7: свечение на соседе; кадры с эффектом и без него (цвет считает внешний разбор кадров).
//   Итог: «КАЧЕСТВО ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "AsdaFxNames.h"
#include "Characters/AsdaCharAssembly.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "World/AsdaFxLightSettings.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Test/AsdaSelfTestKit.h"
#include "ProceduralMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGlowGrade, Log, All);

namespace AsdaGlowGradeTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.GlowGrade");   // имя команды
	constexpr int32 kMe = 4321;      // свой userNum (4003)
	constexpr int32 kPeer = 81;      // сосед с оружием
	constexpr int32 kEnh = 7;        // заточка кадров (ступень 03)

	// Шаг сценария: true — завершён, false — ждёт следующего кадра. Аргумент — секунды шага.
	using FStep = TFunction<bool(float)>;

	// Случай кадра: предмет, слот, имя кадра.
	struct FCase { int32 Item = 0; int32 Slot = 9; FString Name; };

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                      // мир
		bool bQuit = false;                            // `quit` в конце
		FString ShotDir;
		FString Solo;                                  // случай разбора по слоям
		TArray<FString> FxList;                        // `fx=a;b;c` — кадры эффектов на стенде вместо случаев заточки
		FString Tag;                                   // `tag=` — приписка к именам кадров стенда
		TArray<FIntVector> Wear;                       // `wear=id:enh[:самоцвет];...` — надеть предмет с заточкой (свой и сосед), серия кадров
		int32 Ok = 0, Fail = 0;                        // счётчики проверок
		TArray<FStep> Steps;                           // сценарий
		int32 Idx = 0;                                 // текущий шаг
		double StepT0 = 0.0;                           // начало шага
		bool bAbort = false;                           // прервать сценарий
		FDelegateHandle H;                             // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;        // свой номер — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;         // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;        // мир
		TWeakObjectPtr<UAsdaItemTableSubsystem> Items; // таблицы предметов
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;   // инвентарь — своё надетое (контейнер 3)
		int32 Orig8 = 0, Orig9 = 0;                    // надетое в 3/8 и 3/9 до прогона (0 — пусто)
		bool bWornTouched = false;                     // сценарий надевал своему
		TWeakObjectPtr<ACameraActor> Cam;              // камера кадров
		TWeakObjectPtr<AActor> OrigView;               // цель вида до прогона

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaGlowGrade, Log,   TEXT("КАЧЕСТВО ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaGlowGrade, Error, TEXT("КАЧЕСТВО FAIL %s"), *What); }
		}
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Павн соседа.
	static AAsdaPlayerCharacter* Peer(const FState& S)
	{
		UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeer)) : nullptr;
	}

	// Тело 4003 (9 б).
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Тело 4005 (151 б): сосед-мужчина с предметом в слоте и заточкой.
	static TArray<uint8> Body4005(int32 Slot, int32 ItemId, int32 Grade, float GameX, float GameZ)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)kPeer);
		B[6] = 1;
		AsdaBytes::WrI32(B.GetData() + 11, 960000 + kPeer);
		const char* Name = "GlowGrade";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 1; B[38] = 1; B[40] = 100;
		FMemory::Memcpy(B.GetData() + 56, &GameX, 4);
		FMemory::Memcpy(B.GetData() + 60, &GameZ, 4);
		AsdaBytes::WrI32(B.GetData() + 96 + 5 * Slot, ItemId);
		B[96 + 5 * Slot + 4] = (uint8)Grade;
		return B;
	}

	// Тело 4011: сосед ушёл.
	static TArray<uint8> Body4011()
	{
		TArray<uint8> B; B.SetNumZeroed(2);
		AsdaBytes::WrI16(B.GetData(), (int16)kPeer);
		return B;
	}

	// Эффект свечения на носителе (имя .jeb актора; пусто — нет).
	static FString GlowOf(const AActor* A, const TCHAR* Tag)
	{
		if (!A) { return FString(); }
		TArray<AActor*> Attached;
		A->GetAttachedActors(Attached);
		for (const AActor* X : Attached)
		{
			if (IsValid(X) && X->ActorHasTag(Tag)) { return X->GetClass()->GetName(); }
		}
		return FString();
	}

	// Меш эффекта свечения на носителе.
	static UProceduralMeshComponent* GlowMesh(const AActor* A, const TCHAR* Tag)
	{
		if (!A) { return nullptr; }
		TArray<AActor*> Attached;
		A->GetAttachedActors(Attached);
		for (const AActor* X : Attached)
		{
			if (IsValid(X) && X->ActorHasTag(Tag)) { return X->FindComponentByClass<UProceduralMeshComponent>(); }
		}
		return nullptr;
	}

	// Камера у кости `Bone` носителя.
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
		const FVector At = A->BaseSkel->GetBoneLocation(Bone);
		const FVector Dir = (A->GetActorForwardVector() + A->GetActorRightVector() * 0.5f + FVector(0.f, 0.f, 0.2f)).GetSafeNormal();
		S.Cam->SetActorLocation(At + Dir * Dist);
		S.Cam->SetActorRotation((-Dir).Rotation());
		PC->SetViewTarget(S.Cam.Get());
	}

	// Снять кадр.
	static void Shot(const FState& S, const FString& Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(S.ShotDir, Name + TEXT(".png")), /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
	}

	// Надеть своему предмет с заточкой путём игрока: инстанс в контейнере 3 (как ответ 5001) и общая точка ApplySelfEquipPart.
	static void WearSelf(FState& S, int32 Slot, int32 ItemId, int32 Enh, int32 Sowel = 0)
	{
		if (UAsdaInventorySubsystem* Inv = S.Inv.Get(); Inv && Inv->DebugPutItem(3, Slot, ItemId, 1))
		{
			if (FAsdaItemInstance* C = Inv->GetCellMutable(3, Slot))
			{
				C->Raw[16] = (uint8)(Sowel & 0xFF);          // гнездо 0 (i16) — самоцвет, обводка совела
				C->Raw[17] = (uint8)((Sowel >> 8) & 0xFF);
				C->Raw[24] = (uint8)Enh;
				C->SyncFromRaw();
			}
		}
		if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(Slot, ItemId, Enh); }
	}

	// Конец прогона.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (UAsdaNetSubsystem* N = S.Net.Get()) { N->DebugFeedPacket(4011, Body4011()); }
		if (UWorld* W = S.W.Get())
		{
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				if (S.OrigView.IsValid()) { PC->SetViewTarget(S.OrigView.Get()); }
			}
		}
		if (S.Cam.IsValid()) { S.Cam->Destroy(); }
		if (S.bWornTouched)
		{
			WearSelf(S, 9, S.Orig9, 0);
			WearSelf(S, 8, S.Orig8, 0);
		}
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			if (MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
		}
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaGlowGrade, TEXT("КАЧЕСТВО"), S.Ok, S.Fail);
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

		// Перепись таблицы и случаи кадров: первый предмет с мешем на каждую пару база × качество.
		TSharedRef<TArray<FCase>> Cases = MakeShared<TArray<FCase>>();
		UAsdaItemTableSubsystem* Items = S->Items.Get();
		static const TCHAR* kGrade[] = { TEXT("co"), TEXT("uc"), TEXT("ra"), TEXT("he"), TEXT("le") };
		TMap<FString, int32> Seen;           // имя .jeb → число предметов
		int32 PerGrade[5] = {}, NoBp[5] = {};
		FString NoBpList;
		TSet<FString> Done;
		for (int32 Id = 1; Id < 140000; ++Id)
		{
			const FAsdaItemRecord* Rec = Items->GetItem(Id);
			if (!Rec || (Rec->ItemType != 9 && Rec->ItemType != 8) || Rec->ItemGrade < 0 || Rec->ItemGrade > 4) { continue; }
			const FAsdaImgRecord* Img = Items->GetImgRecord(Rec->ImgIndex);
			if (!Img) { continue; }
			const bool bShield = Rec->ItemType == 8;
			const FString Jeb = AsdaWeaponGlow::ComposeJeb(bShield, Rec->ItemSubType, Img->ImgSubType, Rec->ItemGrade, kEnh);
			if (Jeb.IsEmpty()) { continue; }
			++PerGrade[Rec->ItemGrade];
			if (!Seen.Contains(Jeb) && !LoadClass<AAsdaEffectActor>(nullptr, *AsdaAssetPaths::FxClassPathFromJeb(Jeb)))
			{
				++NoBp[Rec->ItemGrade];
				if (NoBpList.Len() < 300) { NoBpList += TEXT(" ") + Jeb; }
			}
			Seen.FindOrAdd(Jeb)++;
			const FString Base = bShield ? FString(AsdaWeaponGlow::ShieldBase()) : FString(AsdaWeaponGlow::BaseByWeaponSubType(Rec->ItemSubType));
			const FString Key = Base + TEXT("_") + kGrade[Rec->ItemGrade];
			if (!Done.Contains(Key) && !Items->GetItemMeshName(Id, true).IsEmpty()
				&& LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(Items->GetItemMeshName(Id, true)), nullptr, LOAD_NoWarn))
			{
				Done.Add(Key);
				Cases->Add({ Id, bShield ? 8 : 9, Key });
			}
		}
		UE_LOG(LogAsdaGlowGrade, Log, TEXT("КАЧЕСТВО перепись: предметов co %d uc %d ra %d he %d le %d; имён %d; случаев кадра %d"),
			PerGrade[0], PerGrade[1], PerGrade[2], PerGrade[3], PerGrade[4], Seen.Num(), Cases->Num());
		S->Check(NoBp[0] + NoBp[1] + NoBp[2] + NoBp[3] == 0, FString::Printf(TEXT("A1 BP есть у всех имён качеств co..he: нет co %d uc %d ra %d he %d%s"), NoBp[0], NoBp[1], NoBp[2], NoBp[3], *NoBpList));
		UE_LOG(LogAsdaGlowGrade, Log, TEXT("КАЧЕСТВО качество le: предметов %d, имён без файла %d (файлов le в данных нет)"), PerGrade[4], NoBp[4]);

		S->Steps.Add(Do([S]()
		{
			if (const UAsdaInventorySubsystem* Inv = S->Inv.Get())
			{
				const FAsdaItemInstance* C8 = Inv->GetCell(3, 8);
				const FAsdaItemInstance* C9 = Inv->GetCell(3, 9);
				S->Orig8 = (C8 && !C8->IsEmpty()) ? C8->ItemCode : 0;
				S->Orig9 = (C9 && !C9->IsEmpty()) ? C9->ItemCode : 0;
			}
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			S->Net->DebugFeedPacket(4003, Body4003(kMe));
		}));
		S->Steps.Add(Wait(4.0f));
		S->Steps.Add(Do([S]()
		{
			S->Check(MeOf(*S) != nullptr, TEXT("B0 свой павн есть"));
			if (!MeOf(*S)) { S->bAbort = true; }
		}));
		// Режим «надеть»: предмет с любой заточкой (в т. ч. за пределом таблицы ступеней) — свой и сосед, серия кадров.
		if (S->Wear.Num() > 0)
		{
			for (const FIntVector& WI : S->Wear)
			{
				const int32 Id = WI.X, Enh = WI.Y, Sow = WI.Z;
				const FAsdaItemRecord* Rec = Items->GetItem(Id);
				const int32 Slot = (Rec && Rec->ItemType == 8) ? 8 : 9;
				const TCHAR* Tag = Slot == 8 ? AsdaCharFxTags::kShieldGlow : AsdaCharFxTags::kWeaponGlow;
				const FName Bone(Slot == 8 ? TEXT("_7_a1") : TEXT("8_a1"));
				S->Steps.Add(Do([S, Id, Enh, Slot, Sow]() { S->bWornTouched = true; WearSelf(*S, Slot, Id, Enh, Sow); }));
				S->Steps.Add(Wait(1.2f));
				S->Steps.Add(Do([S, Id, Enh, Tag, Bone]()
				{
					AAsdaPlayerCharacter* Me = MeOf(*S);
					const FString Fx = GlowOf(Me, Tag);
					S->Check(true, FString::Printf(TEXT("W свой %d +%d: эффект %s"), Id, Enh, Fx.IsEmpty() ? TEXT("нет") : *Fx));
					AimAt(*S, Me, Bone, 220.f);
				}));
				for (int32 K = 0; K < 4; ++K)
				{
					S->Steps.Add(Wait(0.25f));
					S->Steps.Add(Do([S, Id, Enh, K]() { Shot(*S, FString::Printf(TEXT("wear_self_%d_%d_%d"), Id, Enh, K)); }));
				}
				S->Steps.Add(Do([S, Id, Slot]() { WearSelf(*S, Slot, Id, 0); }));
				S->Steps.Add(Wait(0.4f));
				S->Steps.Add(Do([S, Id]() { Shot(*S, FString::Printf(TEXT("wear_self_%d_0"), Id)); }));
				S->Steps.Add(Wait(0.2f));
				S->Steps.Add(Do([S, Id, Enh, Slot]()
				{
					const FVector2D P = MeOf(*S)->GetGamePosXZ();
					S->Net->DebugFeedPacket(4005, Body4005(Slot, Id, Enh, P.X + 300.f, P.Y));
				}));
				S->Steps.Add(Wait(1.2f));
				S->Steps.Add(Do([S, Id, Enh, Tag, Bone]()
				{
					const FString Fx = GlowOf(Peer(*S), Tag);
					S->Check(true, FString::Printf(TEXT("W сосед %d +%d: эффект %s"), Id, Enh, Fx.IsEmpty() ? TEXT("нет") : *Fx));
					AimAt(*S, Peer(*S), Bone, 220.f);
				}));
				for (int32 K = 0; K < 4; ++K)
				{
					S->Steps.Add(Wait(0.25f));
					S->Steps.Add(Do([S, Id, Enh, K]() { Shot(*S, FString::Printf(TEXT("wear_peer_%d_%d_%d"), Id, Enh, K)); }));
				}
				S->Steps.Add(Do([S]() { S->Net->DebugFeedPacket(4011, Body4011()); }));
				S->Steps.Add(Wait(0.3f));
			}
			return;
		}
		// Режим стенда: каждый эффект перед камерой своего персонажа, кадр, снос. Случаи заточки не идут.
		if (S->FxList.Num() > 0)
		{
			for (const FString& Name : S->FxList)
			{
				S->Steps.Add(Do([S, Name]() { GEngine->Exec(S->W.Get(), *FString::Printf(TEXT("asda.Fx.ShotStand %s 450 30"), *Name)); }));
				S->Steps.Add(Wait(0.35f));   // ранний кадр — короткие эффекты (удар, вспышка навыка) к 1.5 с уже гаснут
				S->Steps.Add(Do([S, Name]() { Shot(*S, FString::Printf(TEXT("fx_%s_%s_early"), *Name, *S->Tag)); }));
				S->Steps.Add(Wait(1.15f));
				S->Steps.Add(Do([S, Name]() { Shot(*S, FString::Printf(TEXT("fx_%s_%s"), *Name, *S->Tag)); }));
				S->Steps.Add(Wait(0.3f));
				S->Steps.Add(Do([S]() { GEngine->Exec(S->W.Get(), TEXT("asda.Fx.ShotStand clear")); }));
				S->Steps.Add(Wait(0.5f));
				S->Steps.Add(Do([S, Name]() { Shot(*S, FString::Printf(TEXT("fx_%s_%s_off"), *Name, *S->Tag)); }));
				S->Steps.Add(Wait(0.2f));
			}
			return;
		}
		for (int32 I = 0; I < Cases->Num(); ++I)
		{
			const FCase C = (*Cases)[I];
			S->Steps.Add(Do([S, C]()
			{
				const FVector2D P = MeOf(*S)->GetGamePosXZ();
				S->Net->DebugFeedPacket(4005, Body4005(C.Slot, C.Item, kEnh, P.X + 300.f, P.Y));
			}));
			S->Steps.Add(Wait(1.2f));
			S->Steps.Add(Do([S, C]()
			{
				AAsdaPlayerCharacter* A = Peer(*S);
				const TCHAR* Tag = C.Slot == 8 ? AsdaCharFxTags::kShieldGlow : AsdaCharFxTags::kWeaponGlow;
				const FString Fx = GlowOf(A, Tag);
				S->Check(!Fx.IsEmpty() || C.Name.EndsWith(TEXT("_le")), FString::Printf(TEXT("B %s предмет %d +%d: эффект %s"), *C.Name, C.Item, kEnh, Fx.IsEmpty() ? TEXT("нет") : *Fx));
				AimAt(*S, A, FName(C.Slot == 8 ? TEXT("_7_a1") : TEXT("8_a1")), 170.f);
			}));
			S->Steps.Add(Wait(0.4f));
			S->Steps.Add(Do([S, C]() { Shot(*S, TEXT("glow_") + C.Name + TEXT("_on")); }));
			S->Steps.Add(Wait(0.2f));
			// Разбор по слоям: один случай `solo=<база>_<качество>`, кадр на каждую секцию эффекта отдельно.
			if (S->Solo == C.Name)
			{
				for (int32 K = 0; K < 20; ++K)
				{
					S->Steps.Add(Do([S, C, K]()
					{
						if (UProceduralMeshComponent* M = GlowMesh(Peer(*S), C.Slot == 8 ? AsdaCharFxTags::kShieldGlow : AsdaCharFxTags::kWeaponGlow))
						{
							for (int32 I2 = 0; I2 < M->GetNumSections(); ++I2) { M->SetMeshSectionVisible(I2, I2 == K); }
						}
					}));
					S->Steps.Add(Wait(0.15f));
					S->Steps.Add(Do([S, C, K]() { Shot(*S, FString::Printf(TEXT("solo_%s_%02d"), *C.Name, K)); }));
					S->Steps.Add(Wait(0.15f));
				}
			}
			S->Steps.Add(Do([S, C]()
			{
				const FVector2D P = MeOf(*S)->GetGamePosXZ();
				S->Net->DebugFeedPacket(4011, Body4011());
				S->Net->DebugFeedPacket(4005, Body4005(C.Slot, C.Item, 0, P.X + 300.f, P.Y));
			}));
			S->Steps.Add(Wait(0.6f));
			S->Steps.Add(Do([S, C]() { AimAt(*S, Peer(*S), FName(C.Slot == 8 ? TEXT("_7_a1") : TEXT("8_a1")), 170.f); }));
			S->Steps.Add(Wait(0.3f));
			S->Steps.Add(Do([S, C]() { Shot(*S, TEXT("glow_") + C.Name + TEXT("_off")); }));
			S->Steps.Add(Wait(0.2f));
			S->Steps.Add(Do([S]() { S->Net->DebugFeedPacket(4011, Body4011()); }));
			S->Steps.Add(Wait(0.2f));
		}
		// Свой персонаж — тот же набор случаев путём игрока (контейнер 3).
		for (int32 I = 0; I < Cases->Num(); ++I)
		{
			const FCase C = (*Cases)[I];
			S->Steps.Add(Do([S, C]()
			{
				S->bWornTouched = true;
				WearSelf(*S, C.Slot, C.Item, kEnh);
			}));
			S->Steps.Add(Wait(1.0f));
			S->Steps.Add(Do([S, C]()
			{
				AAsdaPlayerCharacter* Me = MeOf(*S);
				const TCHAR* Tag = C.Slot == 8 ? AsdaCharFxTags::kShieldGlow : AsdaCharFxTags::kWeaponGlow;
				const FString Fx = GlowOf(Me, Tag);
				S->Check(!Fx.IsEmpty() || C.Name.EndsWith(TEXT("_le")), FString::Printf(TEXT("C свой %s предмет %d +%d: эффект %s"), *C.Name, C.Item, kEnh, Fx.IsEmpty() ? TEXT("нет") : *Fx));
				AimAt(*S, Me, FName(C.Slot == 8 ? TEXT("_7_a1") : TEXT("8_a1")), 170.f);
			}));
			S->Steps.Add(Wait(0.4f));
			S->Steps.Add(Do([S, C]() { Shot(*S, TEXT("self_") + C.Name + TEXT("_on")); }));
			S->Steps.Add(Wait(0.2f));
			S->Steps.Add(Do([S, C]() { WearSelf(*S, C.Slot, C.Item, 0); }));
			S->Steps.Add(Wait(0.3f));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestGlowGrade(
	TEXT("asda.SelfTest.GlowGrade"),
	TEXT("Самотест цвета свечения по качеству: все оружия и щиты таблицы, кадры класс × качество на соседе. Офлайн. `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGlowGradeTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!Net || !WS || !Items)
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/предметов"));
			return;
		}
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS; SP->Items = Items;
		SP->Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
			else if (P.StartsWith(TEXT("solo="), ESearchCase::IgnoreCase)) { SP->Solo = P.Mid(5); }
			else if (P.StartsWith(TEXT("fx="), ESearchCase::IgnoreCase)) { P.Mid(3).ParseIntoArray(SP->FxList, TEXT(";")); }
			else if (P.StartsWith(TEXT("tag="), ESearchCase::IgnoreCase)) { SP->Tag = P.Mid(4); }
			else if (P.StartsWith(TEXT("wear="), ESearchCase::IgnoreCase))
			{
				TArray<FString> Parts;
				P.Mid(5).ParseIntoArray(Parts, TEXT(";"));
				for (const FString& X : Parts)
				{
					TArray<FString> F;
					X.ParseIntoArray(F, TEXT(":"));
					if (F.Num() >= 2) { SP->Wear.Add(FIntVector(FCString::Atoi(*F[0]), FCString::Atoi(*F[1]), F.Num() > 2 ? FCString::Atoi(*F[2]) : 0)); }
				}
			}
			else if (P.StartsWith(TEXT("exec="), ESearchCase::IgnoreCase))
			{
				// Ключ консоли на время замера: `exec=<ключ>,<значение>`.
				GEngine->Exec(W, *P.Mid(5).Replace(TEXT(","), TEXT(" ")));
			}
			else if (P.StartsWith(TEXT("vc="), ESearchCase::IgnoreCase))
			{
				// Режим цвета вершин на время замера: сравнение кадров без правки умолчания.
				GEngine->Exec(W, *FString::Printf(TEXT("asda.Fx.VertexColorMode %s"), *P.Mid(3)));
			}
			else if (P.StartsWith(TEXT("boost="), ESearchCase::IgnoreCase))
			{
				// Сила свечения семейства weapon_glow на время замера.
				UAsdaFxLightSettings* LS = const_cast<UAsdaFxLightSettings*>(UAsdaFxLightSettings::Get());
				for (FAsdaFxLightRule& R : LS->Families)
				{
					if (R.Id == TEXT("weapon_glow")) { R.Spec.bOverrideEmissive = true; R.Spec.EmissiveBoost = FCString::Atof(*P.Mid(6)); }
				}
				UAsdaFxLightSettings::BumpTablesRevision();
			}
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
