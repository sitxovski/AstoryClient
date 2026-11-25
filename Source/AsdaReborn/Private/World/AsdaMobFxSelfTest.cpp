// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ЭФФЕКТНЫЕ ПОДСЕТКИ МОБОВ» (01.11.2026, mob_fx_audit): [ПОРТ]
//   `asda.SelfTest.MobFx ids=<тип>,… [npc=<модель>,…] [loot=<код>,…] [scene=charselect] [shot=<папка>] [quit]`
//   [map=<номер карты>] [tile=<X>,<Z>] [exec=<к1>|<к2>] [bare] [far=<k>] (09.11: карта и клетка своего персонажа, умолч. 3 и 256,256;
//   exec — консольные команды до кадров, «:» вместо пробела; bare — те же ракурсы со скрытым мобом, маска для сверки тона)
//   (npc — актор NPC с мешем /Models/NPC/<модель>: NPC и декор карт; loot — актор лута по коду предмета; scene=charselect —
//   кадр сцены выбора персонажа офлайн, запускать на карте Login)
//   Жалоба владельца: у Чепухона (тип 806, модель mon_S_0_pheseus03_a_00) из глаз — жёлтые НЕПРОЗРАЧНЫЕ конусы вместо
//   света. Эталон: подсетка с битом очереди рисуется в мире фиксированным конвейером с альфой текстуры (render_core.c:1423-1446,
//   разбор — Public/Npc/AsdaMobFxMaterial.h). Сценарий офлайн (`-AsdaNoNet`), карта мира: свой персонаж (камера), моб каждого
//   типа спавнится общей точкой сетевого спавна (`UAsdaWorldSubsystem::SpawnNetMobDynamic`) на месте своего персонажа.
//     A) ключи 1 (исправление): у слота-эффекта (`_asdafx` или материал `__mobfx`) мастер M_AsdaMobFxBlend и режим Translucent;
//        у слотов прочих классов — НЕ M_AsdaObjectLit, если строка DT_MobTextures даёт `__char`; у варианта клипов базы
//        `<меш>__<база>` пер-типовой материал применён (имя материала слота — из DT, а не ассета меша); у моба с эффектом
//        включён Custom Depth (ключ asda.Mob.FxDepth — видна только ближняя оболочка);
//     B) ключи 0 (прежний порт): ни у одного слота нет материала с хвостом `__mobfx`/`__char`;
//     C) кадры `<папка>/mobfx_<тип>_<fix|old>_<front|side|close>.png` (close — крупный план, три четверти).
//   Итог: «ЭФФЕКТЫ МОБОВ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaMobFxMaterial.h"
#include "Npc/AsdaNpcActor.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "World/AsdaLootActor.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Test/AsdaSelfTestKit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"   // приближение камеры для кадров
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobFxTest, Log, All);

namespace AsdaMobFxTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.MobFx");             // имя команды
	static const TCHAR* kKeys[] = { TEXT("asda.Mob.FxBlend"), TEXT("asda.Mob.TexClassMasters"), TEXT("asda.Mob.TexGuardClipBase"),
		TEXT("asda.Mob.FxDepth") };
	static const TCHAR* kFxMaster = TEXT("M_AsdaMobFxBlend");           // мастер эффектных подсеток
	constexpr int32 kMe = 4321;                                          // свой userNum (4003)

	// Шаг сценария: true — шаг завершён, false — ждёт следующего кадра. Аргумент — секунды от начала шага.
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
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер — вернуть после прогона
		TUniquePtr<FAsdaCVarScope> CVars;             // ключи — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TArray<int32> Ids;                            // типы мобов
		TWeakObjectPtr<AAsdaMobActor> Mob;            // текущий моб
		TArray<FString> NpcModels;                    // модели NPC/декора (`npc=`), скелеты /Models/NPC
		TArray<int32> LootCodes;                      // коды предметов лута (`loot=`)
		bool bCharSelect = false;                     // `scene=charselect` — кадр сцены выбора персонажа (карта Login)
		TWeakObjectPtr<AActor> Other;                 // текущий NPC/лут
		int32 GlowWas = -1;                           // пункт свечения до проверки (−1 — не менялся)
		bool bAbort = false;                          // прервать сценарий
		float OrigArm = -1.f;                         // длина штанги камеры до кадров (−1 — не менялась)
		bool bMeHidden = false;                       // свой персонаж скрыт на время кадров
		int32 MapId = 3;                              // `map=<id>` — номер карты для Asda.Load.CurrentMap (09.11, умолч. 3 = alpen)
		int32 TileX = 256, TileZ = 256;               // `tile=X,Z` — клетка своего персонажа в синтетическом 4003 (09.11, умолч. 256,256)
		float FarK = 0.f;                             // `far=<k>` — 4-й ракурс «far»: штанга k×радиус габарита (дистанция игрока; 09.11)
		bool bBare = false;                           // `bare` — доп. кадры тех же ракурсов со скрытым мобом (маска моба; 09.11)
		TArray<FString> Exec;                         // `exec=к1|к2` — консольные команды до кадров («:» = пробел; 09.11, разбор источников света)

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaMobFxTest, Log,   TEXT("ЭФФЕКТЫ МОБОВ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaMobFxTest, Error, TEXT("ЭФФЕКТЫ МОБОВ FAIL %s"), *What); }
		}
	};

	// Подсистема видео (пункт свечения).
	static UAsdaVideoSettingsSubsystem* VideoOf(const FState& S)
	{
		const UWorld* W = S.W.Get();
		return W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
	}

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Видимый меш моба (скелетный, иначе статический).
	static UMeshComponent* MeshOf(const AAsdaMobActor* M)
	{
		if (!M) { return nullptr; }
		if (M->Mesh && M->Mesh->GetSkeletalMeshAsset()) { return M->Mesh; }
		if (M->StaticMeshComp && M->StaticMeshComp->GetStaticMesh()) { return M->StaticMeshComp; }
		return nullptr;
	}

	// Меш любого актора (NPC/лут): первый скелетный/статический компонент с мешем.
	static UMeshComponent* MeshOfAny(const AActor* A)
	{
		if (!A) { return nullptr; }
		TInlineComponentArray<UMeshComponent*> Comps(A);
		for (UMeshComponent* C : Comps)
		{
			if (const USkeletalMeshComponent* K = Cast<USkeletalMeshComponent>(C)) { if (K->GetSkeletalMeshAsset()) { return C; } }
			else if (const UStaticMeshComponent* T = Cast<UStaticMeshComponent>(C)) { if (T->GetStaticMesh()) { return C; } }
		}
		return nullptr;
	}

	// Слоты NPC/лута: у слота на мастере M_AsdaMobFxBlend — режим Translucent и Custom Depth у компонента (материал в ассете).
	static void CheckOther(FState& S, const FString& What)
	{
		UMeshComponent* C = MeshOfAny(S.Other.Get());
		if (!C) { S.Check(false, What + TEXT(": не заспавнен")); return; }
		const TArray<FName> Slots = C->GetMaterialSlotNames();
		FString Desc;
		bool bAll = true;
		int32 Fx = 0;   // слотов на мастере эффектов
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			const UMaterialInterface* Mi = C->GetMaterial(i);
			const UMaterial* B = Mi ? Mi->GetMaterial() : nullptr;
			const bool bFx = B && B->GetName() == kFxMaster;
			Desc += FString::Printf(TEXT(" [%d %s] %s/%s blend=%d"), i, Slots.IsValidIndex(i) ? *Slots[i].ToString() : TEXT("?"),
				*GetNameSafe(Mi), *GetNameSafe(B), Mi ? (int32)Mi->GetBlendMode() : -1);
			if (bFx) { ++Fx; bAll &= Mi->GetBlendMode() == BLEND_Translucent && C->bRenderCustomDepth; }
		}
		const AActor* O = S.Other.Get();
		const AAsdaPlayerCharacter* Me = MeOf(S);
		S.Check(bAll, FString::Printf(TEXT("%s слотов %d, эффектов %d:%s | позиция %s (свой %s), габарит %.1f, видим %d"), *What,
			C->GetNumMaterials(), Fx, *Desc, *O->GetActorLocation().ToCompactString(),
			Me ? *Me->GetActorLocation().ToCompactString() : TEXT("?"), C->Bounds.SphereRadius, C->IsVisible() && !O->IsHidden() ? 1 : 0));
	}

	// Ключи разом: 1 — исправление, 0 — прежний порт; материалы моба — заново (ассет меша → DT → ключи).
	static void SetKeys(FState& S, int32 V)
	{
		for (const TCHAR* K : kKeys) { S.CVars->Set(K, V); }
		if (AAsdaMobActor* M = S.Mob.Get())
		{
			if (UMeshComponent* C = MeshOf(M)) { C->EmptyOverrideMaterials(); }
			M->ApplyMobTextures();
		}
	}

	// Проверить слоты моба при ключах V.
	static void CheckMob(FState& S, int32 Id, int32 V)
	{
		AAsdaMobActor* M = S.Mob.Get();
		UMeshComponent* C = MeshOf(M);
		if (!C) { S.Check(false, FString::Printf(TEXT("тип %d: моб не заспавнен"), Id)); return; }
		const FString MeshName = M->Mesh && M->Mesh->GetSkeletalMeshAsset() ? M->Mesh->GetSkeletalMeshAsset()->GetName()
			: GetNameSafe(M->StaticMeshComp->GetStaticMesh());
		const TArray<FName> Slots = C->GetMaterialSlotNames();
		FString Desc;
		bool bAll = true;
		int32 Fx = 0;   // слотов-эффектов
		for (int32 i = 0; i < C->GetNumMaterials(); ++i)
		{
			const UMaterialInterface* Mi = C->GetMaterial(i);
			const UMaterial* B = Mi ? Mi->GetMaterial() : nullptr;
			const FString N = GetNameSafe(Mi);
			const FString SN = Slots.IsValidIndex(i) ? Slots[i].ToString() : TEXT("?");
			const bool bFxMat = N.EndsWith(AsdaMobFx::kFxMatSuffix);
			Desc += FString::Printf(TEXT(" [%d %s] %s/%s blend=%d"), i, *SN, *N, *GetNameSafe(B), Mi ? (int32)Mi->GetBlendMode() : -1);
			if (V != 0)
			{
				if (bFxMat)
				{
					++Fx;
					bAll &= B && B->GetName() == kFxMaster && Mi->GetBlendMode() == BLEND_Translucent && C->bRenderCustomDepth;
				}
				// Слот `_asdafx` без материала-эффекта допустим только при непрозрачной текстуре типа (тогда у него __char/прежний).
			}
			else
			{
				bAll &= !bFxMat && !N.EndsWith(AsdaMobFx::kCharMatSuffix);
			}
		}
		S.Check(bAll, FString::Printf(TEXT("тип %d ключи %d меш %s слотов %d, эффектов %d:%s"), Id, V, *MeshName,
			C->GetNumMaterials(), Fx, *Desc));
	}

	// Поставить моба на место своего персонажа лицом к камере (YawOff — от «лицом») и снять кадр.
	static void Shot(FState& S, const FString& Name, float YawOff, float ArmK)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		AAsdaPlayerCharacter* Me = MeOf(S);
		AActor* M = S.Mob.IsValid() ? (AActor*)S.Mob.Get() : S.Other.Get();
		if (PC && Me && M)
		{
			M->SetActorRotation(FRotator(0.f, PC->GetControlRotation().Yaw + 180.f + YawOff, 0.f));
			if (Me->CameraArm)
			{
				// Крупный план по габариту моба (возвращается в Finish).
				if (S.OrigArm < 0.f) { S.OrigArm = Me->CameraArm->TargetArmLength; }
				const UMeshComponent* C = MeshOfAny(M);
				const float R = C ? C->Bounds.SphereRadius : 100.f;
				Me->CameraArm->TargetArmLength = FMath::Clamp(R * ArmK, 120.f, 1600.f);
				Me->CameraArm->bEnableCameraLag = false;
			}
		}
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaMobFxTest, Log, TEXT("ЭФФЕКТЫ МОБОВ кадр %s"), *Path);
	}

	// Убрать текущего моба.
	static void DropMob(FState& S)
	{
		if (AAsdaMobActor* M = S.Mob.Get()) { M->Destroy(); }
		S.Mob.Reset();
		if (AActor* O = S.Other.Get()) { O->Destroy(); }
		S.Other.Reset();
	}

	// Конец прогона: моб, камера, свой персонаж, ключи, номер.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		DropMob(S);
		if (AAsdaPlayerCharacter* Me = MeOf(S))
		{
			if (S.OrigArm >= 0.f && Me->CameraArm) { Me->CameraArm->TargetArmLength = S.OrigArm; }
			if (S.bMeHidden) { Me->SetActorHiddenInGame(false); }
		}
		S.CVars.Reset();
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get())
		{
			if (MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
		}
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaMobFxTest, TEXT("ЭФФЕКТЫ МОБОВ"), S.Ok, S.Fail);
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

		if (S->bCharSelect)
		{
			// Сцена выбора персонажа офлайн (карта Login): задник cre_creback, 697 акторов уровня.
			S->Steps.Add(Do([S]() { GEngine->Exec(S->W.Get(), TEXT("asda.Login.CharSelectOffline")); }));
			S->Steps.Add(Wait(8.0f));
			S->Steps.Add(Do([S]()
			{
				S->Check(true, TEXT("сцена выбора персонажа"));
				if (!S->ShotDir.IsEmpty())
				{
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(S->ShotDir, TEXT("backdrop_charselect.png")), false, false);
				}
			}));
			S->Steps.Add(Wait(1.5f));
			return;
		}
		S->Steps.Add(Do([S]()
		{
			GEngine->Exec(S->W.Get(), *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), S->MapId));
			for (const FString& C : S->Exec) { GEngine->Exec(S->W.Get(), *C); }   // `exec=` — до появления своего и моба
			TArray<uint8> B; B.SetNumZeroed(9);
			AsdaBytes::WrI16(B.GetData() + 0, (int16)kMe);
			AsdaBytes::WrI16(B.GetData() + 2, (int16)S->TileX);
			AsdaBytes::WrI16(B.GetData() + 4, (int16)S->TileZ);
			B[8] = 1;
			S->Net->DebugFeedPacket(4003, B);
		}));
		S->Steps.Add(Wait(4.0f));   // эффект появления
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			const int32 NT = S->Ids.Num() + S->NpcModels.Num() + S->LootCodes.Num();   // всего целей
			S->Check(Me && NT > 0, FString::Printf(TEXT("павн %s, целей %d"), Me ? TEXT("есть") : TEXT("НЕТ"), NT));
			if (!Me || NT == 0) { S->bAbort = true; return; }
			Me->SetActorHiddenInGame(true);   // моб встаёт на место своего персонажа — свой не должен заслонять
			S->bMeHidden = true;
		}));
		static const TCHAR* kState[] = { TEXT("fix"), TEXT("old") };
		static const int32 kV[] = { 1, 0 };
		static const TCHAR* kView[] = { TEXT("front"), TEXT("side"), TEXT("close"), TEXT("far") };
		static const float kYaw[] = { -25.f, 70.f, -45.f, -25.f };
		const float kArmK[] = { 2.4f, 2.4f, 1.0f, S->FarK };   // крупный план — штанга по радиусу габарита; far — `far=<k>`
		const int32 NV = S->FarK > 0.f ? 4 : 3;                 // число ракурсов (4-й только с `far=`)
		for (const int32 Id : S->Ids)
		{
			S->Steps.Add(Do([S, Id]()
			{
				DropMob(*S);
				AAsdaPlayerCharacter* Me = MeOf(*S);
				for (const TCHAR* K : kKeys) { S->CVars->Set(K, 1); }
				AAsdaMobActor* M = (Me && S->WS.Get()) ? S->WS->SpawnNetMobDynamic(Id, Me->GetActorTransform()) : nullptr;
				S->Mob = M;
				S->Check(M != nullptr, FString::Printf(TEXT("тип %d заспавнен"), Id));
			}));
			S->Steps.Add(Wait(1.5f));
			for (int32 st = 0; st < 2; ++st)
			{
				S->Steps.Add(Do([S, st]() { SetKeys(*S, kV[st]); }));
				S->Steps.Add(Wait(1.0f));
				S->Steps.Add(Do([S, Id, st]() { CheckMob(*S, Id, kV[st]); }));
				if (st == 0)
				{
					// Свечение мобов включить и выключить: снятие пометки 11 не должно гасить Custom Depth «записи глубины».
					S->Steps.Add(Do([S]() { if (UAsdaVideoSettingsSubsystem* V = VideoOf(*S)) { S->GlowWas = V->GetGlowLevel(); V->SetGlowLevel(3, false); } }));
					S->Steps.Add(Wait(0.5f));
					S->Steps.Add(Do([S]() { if (UAsdaVideoSettingsSubsystem* V = VideoOf(*S)) { V->SetGlowLevel(0, false); } }));
					S->Steps.Add(Wait(0.5f));
					S->Steps.Add(Do([S, Id]()
					{
						UMeshComponent* C = MeshOf(S->Mob.Get());
						const bool bNeed = C && AsdaMobFx::WantsCustomDepth(C);
						S->Check(!bNeed || C->bRenderCustomDepth, FString::Printf(TEXT("тип %d: свечение 3→0, Custom Depth %d (нужен %d, трафарет %d)"),
							Id, C && C->bRenderCustomDepth ? 1 : 0, bNeed ? 1 : 0, C ? C->CustomDepthStencilValue : -1));
						if (UAsdaVideoSettingsSubsystem* V = VideoOf(*S)) { if (S->GlowWas >= 0) { V->SetGlowLevel(S->GlowWas, false); } }
					}));
				}
				for (int32 v = 0; v < NV; ++v)
				{
					const float K = kArmK[v];   // плечо ракурса (захват по значению — массив локальный)
					S->Steps.Add(Do([S, Id, st, v, K]() { Shot(*S, FString::Printf(TEXT("mobfx_%d_%s_%s"), Id, kState[st], kView[v]), kYaw[v], K); }));
					S->Steps.Add(Wait(1.0f));
				}
				// ★ 09.11.2026 (dungeon_mob_purple) `bare` — те же ракурсы с СКРЫТЫМ мобом: разница кадров = маска моба,
				//   остальное = окружение (сверка тона моба с тоном освещения карты, Reports/dungeon_mob_purple).
				if (st == 0 && S->bBare)
				{
					for (int32 v = 0; v < NV; ++v)
					{
						const float K = kArmK[v];   // плечо ракурса
						S->Steps.Add(Do([S, Id, v, K]()
						{
							if (AAsdaMobActor* M = S->Mob.Get()) { M->SetActorHiddenInGame(true); }
							Shot(*S, FString::Printf(TEXT("mobfx_%d_bare_%s"), Id, kView[v]), kYaw[v], K);
						}));
						S->Steps.Add(Wait(1.0f));
					}
					S->Steps.Add(Do([S]() { if (AAsdaMobActor* M = S->Mob.Get()) { M->SetActorHiddenInGame(false); } }));
				}
			}
		}
		// Ключи — исправление (цикл мобов закончился состоянием «прежний порт»): NPC/лут проверяются при боевых значениях.
		S->Steps.Add(Do([S]() { DropMob(*S); for (const TCHAR* K : kKeys) { S->CVars->Set(K, 1); } }));
		// NPC/декор: актор NPC с мешем модели (материалы — как в ассете), лут — актор лута по коду предмета.
		for (const FString& Model : S->NpcModels)
		{
			S->Steps.Add(Do([S, Model]()
			{
				DropMob(*S);
				AAsdaPlayerCharacter* Me = MeOf(*S);
				USkeletalMesh* SK = UAsdaModelAnimSubsystem::LoadMeshChecked(
					UAsdaModelAnimSubsystem::BuildSkelPath(AsdaAssetPaths::kSkelRootNpc, Model));
				FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				SP.bDeferConstruction = true;
				AAsdaNpcActor* N = (Me && SK) ? S->W->SpawnActor<AAsdaNpcActor>(AAsdaNpcActor::StaticClass(), SP) : nullptr;
				if (N)
				{
					N->bNetSpawned = true;
					N->Mesh->SetSkeletalMesh(SK);
					FTransform X = Me->GetActorTransform(); X.SetScale3D(FVector(0.01f));
					N->FinishSpawning(X);
				}
				S->Other = N;
				S->Check(N != nullptr, FString::Printf(TEXT("NPC %s заспавнен"), *Model));
			}));
			S->Steps.Add(Wait(1.5f));
			S->Steps.Add(Do([S, Model]() { CheckOther(*S, TEXT("NPC ") + Model); }));
			for (int32 v = 0; v < 3; ++v)
			{
				const float K = kArmK[v];   // плечо ракурса (массив локальный — захват по значению)
				S->Steps.Add(Do([S, Model, v, K]() { Shot(*S, FString::Printf(TEXT("npc_%s_%s"), *Model, kView[v]), kYaw[v], K); }));
				S->Steps.Add(Wait(1.0f));
			}
		}
		for (const int32 Code : S->LootCodes)
		{
			S->Steps.Add(Do([S, Code]()
			{
				DropMob(*S);
				AAsdaPlayerCharacter* Me = MeOf(*S);
				FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AAsdaLootActor* L = Me ? S->W->SpawnActor<AAsdaLootActor>(AAsdaLootActor::StaticClass(), Me->GetActorTransform(), SP) : nullptr;
				if (L) { L->InitFromItemCode(Code, 1, FIntPoint(256, 256)); L->SetActorLocation(Me->GetActorLocation() + FVector(0.f, 0.f, 60.f)); }   // на высоту взгляда камеры
				S->Other = L;
				S->Check(L != nullptr, FString::Printf(TEXT("лут %d заспавнен"), Code));
			}));
			S->Steps.Add(Wait(1.5f));
			S->Steps.Add(Do([S, Code]() { CheckOther(*S, FString::Printf(TEXT("лут %d"), Code)); }));
			for (int32 v = 0; v < 3; ++v)
			{
				S->Steps.Add(Do([S, Code, v]() { Shot(*S, FString::Printf(TEXT("loot_%d_%s"), Code, kView[v]), kYaw[v], 3.5f); }));
				S->Steps.Add(Wait(1.0f));
			}
		}
		S->Steps.Add(Do([S]() { DropMob(*S); for (const TCHAR* K : kKeys) { S->CVars->Set(K, 1); } }));
		S->Steps.Add(Wait(0.3f));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMobFx(
	TEXT("asda.SelfTest.MobFx"),
	TEXT("Самотест эффектных подсеток мобов: `ids=<тип>,…` — моб каждого типа на месте своего персонажа (офлайн, карта мира), мастера "
	     "слотов при ключах asda.Mob.FxBlend/TexClassMasters/TexGuardClipBase 1 и 0, кадры спереди/сбоку. `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaMobFxTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		bool bKeys = true;
		for (const TCHAR* K : kKeys) { bKeys &= IConsoleManager::Get().FindConsoleVariable(K) != nullptr; }
		if (!Net || !bKeys)
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира или ключей эффектов мобов"));
			return;
		}
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS;
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
			else if (P.StartsWith(TEXT("npc="), ESearchCase::IgnoreCase)) { P.Mid(4).ParseIntoArray(SP->NpcModels, TEXT(",")); }
			else if (P.StartsWith(TEXT("loot="), ESearchCase::IgnoreCase))
			{
				TArray<FString> List;
				P.Mid(5).ParseIntoArray(List, TEXT(","));
				for (const FString& E : List) { if (const int32 C = FCString::Atoi(*E)) { SP->LootCodes.Add(C); } }
			}
			else if (P.Equals(TEXT("scene=charselect"), ESearchCase::IgnoreCase)) { SP->bCharSelect = true; }
			// ★ 09.11.2026 (dungeon_mob_purple): кадры моба в освещении любой карты — номер карты и клетка своего персонажа.
			else if (P.StartsWith(TEXT("map="), ESearchCase::IgnoreCase)) { SP->MapId = FCString::Atoi(*P.Mid(4)); }
			else if (P.Equals(TEXT("bare"), ESearchCase::IgnoreCase)) { SP->bBare = true; }
			else if (P.StartsWith(TEXT("far="), ESearchCase::IgnoreCase)) { SP->FarK = FCString::Atof(*P.Mid(4)); }
			else if (P.StartsWith(TEXT("exec="), ESearchCase::IgnoreCase))
			{
				P.Mid(5).ParseIntoArray(SP->Exec, TEXT("|"));
				for (FString& C : SP->Exec) { C.ReplaceInline(TEXT(":"), TEXT(" ")); }
			}
			else if (P.StartsWith(TEXT("tile="), ESearchCase::IgnoreCase))
			{
				FString X, Z;   // части «X,Z»
				if (P.Mid(5).Split(TEXT(","), &X, &Z)) { SP->TileX = FCString::Atoi(*X); SP->TileZ = FCString::Atoi(*Z); }
			}
			else if (P.StartsWith(TEXT("ids="), ESearchCase::IgnoreCase))
			{
				TArray<FString> List;
				P.Mid(4).ParseIntoArray(List, TEXT(","));
				for (const FString& E : List) { if (const int32 Id = FCString::Atoi(*E)) { SP->Ids.Add(Id); } }
			}
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
