// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ИМЯ И ПОЛОСКА ДАЛЬНЕГО МОНСТРА» — `asda.Overhead.SelfTestFarNames [shotdir=<папка>] [cost] [quit]` (08.11.2026, пачка 14).
//   Только офлайн (`-AsdaNoNet`), карта alpen. Разбор — у ключа `asda.Overhead.ViewRangeNames` (AsdaNameplateComponent.cpp).
//
// ПУТЬ ИГРОКА: свой персонаж, моб — запись 4013 штатному приёмнику (и заново на каждом случае: в игре сервер шлёт их постоянно), выбор
//   целью — тот же вход, что ЛКМ. Ползунок — ключ `asda.Cull.Scale`, который пишет окно настроек. Плашку считает боевой код.
//   Случаи (живой виджет: видимость, непрозрачность, геометрия, имя и полоса HP):
//     1) ползунок 50/100/200 %: цель внутри дальности — имя и полоска видны, за дальностью — скрыты;
//     2) контроль ключом 0: на тех же 7000 ед. плашки нет (прежний предел 3×3 сектора), вблизи есть;
//     3) моб под курсором (не цель): за пределом мобов подсистема скрывает его вместе с плашкой — имени без тела не бывает;
//     4) стена: заслон между камерой и дальней плашкой — скрыта, заслон убран — видна; за камерой — скрыта.
//   С аргументом `cost` — цена: 340 мобов 4013 на карте, плашки у всех (худший случай) и только у цели, при ключе 1 и 0; лучи и кадр.
//   Строки — «НАДПИСИ-ДАЛЬ», итог — «НАДПИСИ-ДАЛЬ ИТОГ: ok=… fail=…»; кадры `far_<NN>.png` в `shotdir=`.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "World/AsdaOverheadOcclusion.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaCoord.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaWalkGrid.h"
#include "World/AsdaViewRangeSubsystem.h"
#include "Npc/AsdaMobActor.h"
#include "AsdaNetSubsystem.h"
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Misc/App.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFarNamesTest, Log, All);

namespace AsdaFarNamesLocal
{
	static const TCHAR* kCmd = TEXT("asda.Overhead.SelfTestFarNames");
	static const TCHAR* kPrefix = TEXT("НАДПИСИ-ДАЛЬ");
	constexpr int32 kMobRid = 29501;       // моб-цель
	constexpr int32 kHoverRid = 29502;     // моб под курсором
	constexpr int32 kMobType = 1;          // волк alpen
	constexpr int32 kCostFirstRid = 30000; // первый renderId мобов замера
	constexpr int32 kCostMobs = 340;       // мобов на Alpen в задаче
	constexpr float kSettleS = 1.0f;       // замер перекрытия и угасание
	constexpr float kCostSec = 3.0f;       // длительность одного замера цены

	// Один случай ползунка: масштаб, удаление, ожидание и ключ.
	struct FCase { float Scale; float Dist; bool bWant; int32 Key; };
	static const FCase kCases[] = {
		{ 0.5f,  5000.f, true,  1 }, { 0.5f, 11000.f, true,  1 }, { 0.5f, 13000.f, false, 1 },
		{ 1.0f, 13000.f, true,  1 }, { 1.0f, 23000.f, true,  1 },
		{ 2.0f, 23000.f, true,  1 },
		{ 1.0f,  7000.f, false, 0 }, { 1.0f,  2000.f, true,  0 },
	};

	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false, bCost = false;
		FString ShotDir;
		int32 ShotN = 0, Ok = 0, Fail = 0, Polls = 0, CaseIdx = 0, Try = 0, CostStep = 0;
		TWeakObjectPtr<AActor> Blocker;
		TArray<TWeakObjectPtr<AAsdaMobActor>> CostMobs;
		double SumDt = 0.0; int32 Frames = 0; FTSTicker::FDelegateHandle Tick;
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaFarNamesTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaFarNamesTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	using AsdaSelfTestCtx::ByFn::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	template <class T> static T* Sub(const TSharedRef<FCtx>& C) { return C->W.Get() ? C->W->GetGameInstance()->GetSubsystem<T>() : nullptr; }
	static AAsdaMobActor* MobOf(const TSharedRef<FCtx>& C, int32 Rid) { UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C); return WS ? WS->FindNetMobByRid(Rid) : nullptr; }
	static void SetVar(const TCHAR* Name, float V) { if (IConsoleVariable* K = IConsoleManager::Get().FindConsoleVariable(Name)) { K->Set(*FString::SanitizeFloat(V), ECVF_SetByConsole); } }

	static void Shot(const TSharedRef<FCtx>& C, const FString& Case)
	{
		if (C->ShotDir.IsEmpty() || !GEngine || !C->W.Get()) { return; }
		const FString Name = FString::Printf(TEXT("far_%02d"), ++C->ShotN);
		GEngine->Exec(C->W.Get(), *FString::Printf(TEXT("asda.UI.Shot %s"), *(C->ShotDir / Name)));
		UE_LOG(LogAsdaFarNamesTest, Log, TEXT("%s   кадр %s.png — %s"), kPrefix, *Name, *Case);
	}

	// Точка на земле по взгляду камеры от своего персонажа: Dist вперёд (отрицательное — за спину).
	static bool GroundAhead(const TSharedRef<FCtx>& C, float Dist, FVector& Out)
	{
		UWorld* W = C->W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		const AActor* Self = WS ? WS->LocalPlayerActor.Get() : nullptr;
		const UAsdaWalkGrid* G = AsdaGround::GetGrid();
		if (!PC || !PC->PlayerCameraManager || !Self || !G) { return false; }
		FVector Dir = PC->PlayerCameraManager->GetCameraRotation().Vector(); Dir.Z = 0.f; Dir.Normalize();
		Out = Self->GetActorLocation() + Dir * Dist;
		float Y = 0.f;
		if (!G->GetGroundY(Out.Y, -Out.X, Y)) { return false; }
		Out.Z = Y;
		return true;
	}

	// Поставить моба: новая запись 4013 (живой поток сервера) и точная позиция.
	static void PutMob(const TSharedRef<FCtx>& C, int32 Rid, const FVector& P)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		if (!Net) { return; }
		const FVector2D G = AsdaCoord::UeToGame2D(P);
		Net->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(Rid, kMobType, FIntPoint((int32)(G.X / 100.f), (int32)(G.Y / 100.f)), 1, 100));
		if (AAsdaMobActor* M = MobOf(C, Rid)) { M->SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics); }
	}

	// Плашка видна игроку: виджет во вьюпорте, непрозрачен, геометрия ненулевая.
	static bool Shown(const AActor* A, FString& Out)
	{
		const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		const UUserWidget* Wd = NP ? NP->DebugScreenWidget() : nullptr;
		if (!Wd) { Out = TEXT("виджета нет"); return false; }
		const ESlateVisibility V = Wd->GetVisibility();
		const bool bVis = Wd->IsInViewport() && V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
		const FVector2D Sz = Wd->GetCachedGeometry().GetLocalSize();
		FAsdaOccludeHit H;
		AsdaOverheadOcclusion::TraceNow(A->GetWorld(), NP->GetPlateWorldPoint(), A, &H);
		Out = FString::Printf(TEXT("«%s» HP=%d всегда=%d владелец скрыт=%d видим=%d α=%.2f размер=%.0f | принято=%d луч=%d %s"), *NP->DisplayName, NP->bShowHp ? 1 : 0,
			NP->bAlwaysShowName ? 1 : 0, A->IsHidden() ? 1 : 0, bVis ? 1 : 0, Wd->GetRenderOpacity(), Sz.X, NP->DebugOccState().bOccluded, H.bOccluded ? 1 : 0, *H.What);
		return bVis && Wd->GetRenderOpacity() > 0.99f && Sz.X > 0.f;
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		SetVar(TEXT("asda.Cull.Scale"), 1.f);
		SetVar(TEXT("asda.Overhead.ViewRangeNames"), 1.f);
		SetVar(TEXT("asda.Overhead.Occlude"), 1.f);
		if (AActor* B = C->Blocker.Get()) { B->Destroy(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaFarNamesTest, kPrefix, C->Ok, C->Fail);
	}

	// ── цена: 340 мобов, четыре замера ──
	static void CostStep(TSharedRef<FCtx> C);
	static void CostMeasure(TSharedRef<FCtx> C, const FString& Label)
	{
		AsdaOverheadOcclusion::ResetStats();
		C->SumDt = 0.0; C->Frames = 0;
		TSharedRef<FCtx> CC = C;
		C->Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([CC](float Dt) { CC->SumDt += Dt; ++CC->Frames; return true; }));
		After(C, kCostSec, [Label](TSharedRef<FCtx> C2)
		{
			FTSTicker::GetCoreTicker().RemoveTicker(C2->Tick);
			int64 Tr = 0, Qr = 0; uint64 Cy = 0;
			AsdaOverheadOcclusion::GetStats(Tr, Qr, Cy);
			int32 NVis = 0, NLive = 0;
			for (const TWeakObjectPtr<AAsdaMobActor>& M : C2->CostMobs)
			{
				if (!M.IsValid()) { continue; }
				++NLive;
				FString S;
				NVis += Shown(M.Get(), S) ? 1 : 0;
			}
			const double Sec = FMath::Max(C2->SumDt, 0.001);
			UE_LOG(LogAsdaFarNamesTest, Log, TEXT("%s ЦЕНА [%s]: мобов %d, плашек видно %d | лучей %.0f/с, запросов %.0f/с, луч %.3f мс на кадр | кадр %.2f мс (%d кадров)"),
				kPrefix, *Label, NLive, NVis, Tr / Sec, Qr / Sec, FPlatformTime::ToMilliseconds64(Cy) / FMath::Max(1, C2->Frames), Sec * 1000.0 / FMath::Max(1, C2->Frames), C2->Frames);
			++C2->CostStep;
			CostStep(C2);
		});
	}
	static void CostStep(TSharedRef<FCtx> C)
	{
		// Шаги: 0 — плашки всех, ключ 1; 1 — плашки всех, ключ 0; 2 — плашка только у цели, ключ 1; 3 — то же, ключ 0.
		if (C->CostStep >= 4) { Finish(C); return; }
		const bool bAll = C->CostStep < 2;
		const int32 Key = (C->CostStep % 2 == 0) ? 1 : 0;
		SetVar(TEXT("asda.Overhead.ViewRangeNames"), (float)Key);
		for (const TWeakObjectPtr<AAsdaMobActor>& M : C->CostMobs)
		{
			if (UAsdaNameplateComponent* NP = M.IsValid() ? M->FindComponentByClass<UAsdaNameplateComponent>() : nullptr) { NP->bAlwaysShowName = bAll; }
		}
		After(C, 1.0f, [bAll, Key](TSharedRef<FCtx> C2) { CostMeasure(C2, FString::Printf(TEXT("%s, ключ %d"), bAll ? TEXT("плашки у всех") : TEXT("плашка только у цели"), Key)); });
	}
	static void CostPhase(TSharedRef<FCtx> C)
	{
		if (!C->bCost) { Finish(C); return; }
		SetVar(TEXT("asda.Cull.Scale"), 1.f);
		SetVar(TEXT("asda.Overhead.Occlude"), 1.f);
		// Веер по кадру: 340 мобов на 10…150 тайлах, ±40° от взгляда.
		for (int32 i = 0; i < kCostMobs; ++i)
		{
			const float Dist = 1000.f + 14000.f * (float)(i % 34) / 33.f;
			APlayerController* PC = C->W->GetFirstPlayerController();
			UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
			const AActor* Self = WS ? WS->LocalPlayerActor.Get() : nullptr;
			if (!PC || !PC->PlayerCameraManager || !Self) { break; }
			const float Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw - 40.f + 80.f * (float)(i / 34) / 9.f;
			FVector P = Self->GetActorLocation() + FRotator(0.f, Yaw, 0.f).Vector() * Dist;
			float Y = 0.f;
			if (const UAsdaWalkGrid* G = AsdaGround::GetGrid()) { if (G->GetGroundY(P.Y, -P.X, Y)) { P.Z = Y; } }
			PutMob(C, kCostFirstRid + i, P);
			if (AAsdaMobActor* M = MobOf(C, kCostFirstRid + i)) { C->CostMobs.Add(M); }
		}
		C->Expect(C->CostMobs.Num() == kCostMobs, FString::Printf(TEXT("цена: мобов на карте %d (ждём %d)"), C->CostMobs.Num(), kCostMobs));
		After(C, 2.0f, [](TSharedRef<FCtx> C2) { Shot(C2, TEXT("340 мобов")); CostStep(C2); });
	}

	// ── за камерой ──
	static void BehindPhase(TSharedRef<FCtx> C)
	{
		FVector P;
		if (!GroundAhead(C, -9000.f, P)) { C->Expect(false, TEXT("за камерой: нет точки")); CostPhase(C); return; }
		PutMob(C, kMobRid, P);
		After(C, kSettleS, [](TSharedRef<FCtx> C2)
		{
			FString S;
			const bool bShown = Shown(MobOf(C2, kMobRid), S);
			C2->Expect(!bShown, FString::Printf(TEXT("моб за камерой в 9000 ед.: плашка скрыта (%s)"), *S));
			CostPhase(C2);
		});
	}

	// ── стена ──
	static void WallPhase(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		AAsdaMobActor* M = MobOf(C, kMobRid);
		const UAsdaNameplateComponent* NP = M ? M->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		FVector Cam;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!W || !NP || !Cube || !AsdaOverheadOcclusion::CameraLocation(W, Cam)) { C->Expect(false, TEXT("стена: нет плашки/куба")); BehindPhase(C); return; }
		SetVar(TEXT("asda.Overhead.Occlude"), 1.f);
		// Дальняя точка, которую не закрывает сама карта.
		bool bFound = false;
		for (float D = 16000.f; D >= 6500.f && !bFound; D -= 250.f)
		{
			FVector P;
			if (!GroundAhead(C, D, P)) { continue; }
			M->SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
			// Открыта и точка, и строка текста (решение плашки — по трём лучам строки, `EvaluateText`).
			bFound = !AsdaOverheadOcclusion::TraceNow(W, NP->GetPlateWorldPoint(), M);
			for (float Dx : { -60.f, 0.f, 60.f })
			{
				FVector Row;
				AsdaOverheadOcclusion::PointAtScreenOffset(W, NP->GetPlateWorldPoint(), FVector2D(Dx, -24.f), Row);
				bFound = bFound && !AsdaOverheadOcclusion::TraceNow(W, Row, M);
			}
			if (bFound) { PutMob(C, kMobRid, P); UE_LOG(LogAsdaFarNamesTest, Log, TEXT("%s стена: моб на %.0f ед."), kPrefix, D); }
		}
		if (!bFound)
		{
			// Рельеф и деревья закрыли этот взгляд: тот же вход, что ПКМ-драг, следующий курс.
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
			if (PC && ++C->Try <= 8)
			{
				PC->SetOrbitAnglesForBench(PC->GetCameraYawUE() + 45.f, PC->GetCameraPitchUE());
				After(C, 0.3f, &WallPhase);
				return;
			}
			C->Expect(false, TEXT("стена: за 8 поворотов нет открытой дальней точки"));
			BehindPhase(C);
			return;
		}
		const FVector Pt = NP->GetPlateWorldPoint();
		const FVector Loc = Cam + (Pt - Cam) * 0.5f;
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(Loc, FRotator(0.f, (Pt - Cam).Rotation().Yaw, 0.f), SP);
		if (!A) { C->Expect(false, TEXT("стена: заслон не заспавнился")); BehindPhase(C); return; }
		A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(Cube);
		A->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		A->SetActorScale3D(FVector(0.5f, 14.f, 14.f));
		C->Blocker = A;
		After(C, kSettleS, [](TSharedRef<FCtx> C2)
		{
			FString S;
			const bool bShown = Shown(MobOf(C2, kMobRid), S);
			C2->Expect(!bShown, FString::Printf(TEXT("дальний моб за заслоном: плашка скрыта (%s)"), *S));
			Shot(C2, TEXT("за заслоном"));
			if (AActor* B = C2->Blocker.Get()) { B->Destroy(); }
			C2->Blocker = nullptr;
			After(C2, kSettleS, [](TSharedRef<FCtx> C3)
			{
				FString S2;
				const bool bShown2 = Shown(MobOf(C3, kMobRid), S2);
				C3->Expect(bShown2, FString::Printf(TEXT("заслон убран: плашка видна (%s)"), *S2));
				Shot(C3, TEXT("заслон убран"));
				BehindPhase(C3);
			});
		});
	}

	// ── моб под курсором: имени без тела нет ──
	static void HoverPhase(TSharedRef<FCtx> C)
	{
		AAsdaMobActor* H = MobOf(C, kHoverRid);
		UAsdaNameplateComponent* NP = H ? H->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		FVector P;
		if (!NP || !GroundAhead(C, 11000.f, P)) { C->Expect(false, TEXT("наведение: нет моба")); WallPhase(C); return; }
		SetVar(TEXT("asda.Cull.Scale"), 0.5f);
		SetVar(TEXT("asda.Overhead.ViewRangeNames"), 1.f);
		NP->bAlwaysShowName = true;   // то, что делает наведение (`SetHoverActor`)
		PutMob(C, kHoverRid, P);
		After(C, 2.5f, [](TSharedRef<FCtx> C2)
		{
			AAsdaMobActor* H2 = MobOf(C2, kHoverRid);
			FString S;
			const bool bShown = Shown(H2, S);
			C2->Expect(H2 && H2->IsHidden() && !bShown, FString::Printf(TEXT("50 %%: моб не цель в 11000 ед. (за пределом мобов 8000): скрыт вместе с плашкой (%s)"), *S));
			if (UAsdaNameplateComponent* NP2 = H2 ? H2->FindComponentByClass<UAsdaNameplateComponent>() : nullptr) { NP2->bAlwaysShowName = false; }
			SetVar(TEXT("asda.Cull.Scale"), 1.f);
			After(C2, 0.5f, &WallPhase);
		});
	}

	// ── ползунок ──
	static void CaseStep(TSharedRef<FCtx> C)
	{
		if (C->CaseIdx >= UE_ARRAY_COUNT(kCases)) { HoverPhase(C); return; }
		const FCase& K = kCases[C->CaseIdx];
		FVector P;
		if (!GroundAhead(C, K.Dist, P))
		{
			C->Expect(false, FString::Printf(TEXT("случай %d: %.0f ед. вне карты"), C->CaseIdx, K.Dist));
			++C->CaseIdx;
			CaseStep(C);
			return;
		}
		SetVar(TEXT("asda.Cull.Scale"), K.Scale);
		SetVar(TEXT("asda.Overhead.ViewRangeNames"), (float)K.Key);
		PutMob(C, kMobRid, P);
		After(C, kSettleS, [](TSharedRef<FCtx> C2)
		{
			const FCase& K2 = kCases[C2->CaseIdx];
			AAsdaMobActor* M = MobOf(C2, kMobRid);
			const UAsdaNameplateComponent* NP = M ? M->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
			FString S;
			const bool bShown = Shown(M, S);
			const float R = UAsdaViewRangeSubsystem::RangeUnits(C2->W.Get());
			C2->Expect(bShown == K2.bWant && (!K2.bWant || (NP && NP->bShowHp)), FString::Printf(TEXT("ползунок %.0f %%, дальность %.0f, цель в %.0f ед., ключ %d: ждём %s (%s)"),
				K2.Scale * 100.f, R, K2.Dist, K2.Key, K2.bWant ? TEXT("видно") : TEXT("скрыто"), *S));
			Shot(C2, FString::Printf(TEXT("%.0f %%, %.0f ед., ключ %d"), K2.Scale * 100.f, K2.Dist, K2.Key));
			++C2->CaseIdx;
			After(C2, 0.3f, &CaseStep);
		});
	}

	static void Arrange(TSharedRef<FCtx> C)
	{
		AAsdaMobActor* M = MobOf(C, kMobRid);
		AAsdaMobActor* H = MobOf(C, kHoverRid);
		if (!M || !H)
		{
			if (++C->Polls > 40) { C->Expect(false, TEXT("подготовка: мобы не появились")); Finish(C); return; }
			After(C, 0.2f, &Arrange);
			return;
		}
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController())) { PC->SetTarget(M); }
		CaseStep(C);
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		if (!W || !WS || !Net) { C->Expect(false, TEXT("нет подсистем")); Finish(C); return; }
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		}
		// Перекрытие картой на случаях ползунка выключено: они проверяют только дальность; стена — отдельный случай с ключом 1.
		SetVar(TEXT("asda.Overhead.Occlude"), 0.f);
		const AActor* Self = WS->LocalPlayerActor.Get();
		const FVector2D G0 = Self ? AsdaCoord::UeToGame2D(Self->GetActorLocation()) : FVector2D(25600.f, 25600.f);
		const FIntPoint T((int32)(G0.X / 100.f), (int32)(G0.Y / 100.f));
		Net->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(kMobRid, kMobType, T + FIntPoint(3, 3), 1, 100));
		Net->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(kHoverRid, kMobType, T + FIntPoint(-3, 3), 1, 100));
		After(C, 1.0f, &Arrange);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaOverheadSelfTestFarNames(
	TEXT("asda.Overhead.SelfTestFarNames"),
	TEXT("asda.Overhead.SelfTestFarNames [shotdir=<папка>] [cost] [quit] — ОФЛАЙН: имя и полоска дальнего моба в пределах дальности обзора при ползунке 50–200 %, ")
	TEXT("контроль ключом asda.Overhead.ViewRangeNames 0, заслон, за камерой; cost — цена на 340 мобах"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaFarNamesLocal;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		TSharedRef<FCtx> C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		for (const FString& Arg : KA.Pos)
		{
			if (Arg.StartsWith(TEXT("shotdir="))) { C->ShotDir = Arg.Mid(8).Replace(TEXT("\\"), TEXT("/")); }
			if (Arg.Equals(TEXT("cost"), ESearchCase::IgnoreCase)) { C->bCost = true; }
		}
		Run(C);
	}));

#endif // !UE_BUILD_SHIPPING
