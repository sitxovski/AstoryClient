// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ИМЕНА ЗА ДЕРЕВЬЯМИ» — `asda.Overhead.SelfTestTrees [shotdir=<папка>] [quit]` (05.11.2026, баг игроков «имена NPC
//   рисуются поверх деревьев, даже когда NPC за ними»). Только офлайн (`-AsdaNoNet`), на текущей карте с деревьями.
//
// ПУТЬ ИГРОКА: свой персонаж ставится в 14 м от настоящего дерева карты (ISM `tree*`), камера (та же точка, что ПКМ-драг) — на
//   дерево; сосед — снапшот 4005 (`DebugSpawnFakePeer`), моб — запись 4013 штатному приёмнику и выбор целью. Плашки рисует боевой
//   код. Для каждого (сосед, моб) ищется место ЗА деревом, где точка над головой видна (между стволом и кроной), а пиксели
//   строки имени закрыты кроной — ровно случай со снимков игроков. Проверяется живой виджет:
//     • ключ asda.Overhead.OccludeAtText 0 (прежнее решение по точке) — имя ВИДНО (баг воспроизведён на этом месте);
//     • ключ 1 (z-тест в пикселях текста, как оригинал) — имя СКРЫТО;
//     • контроль: тот же сосед на открытом месте в 7 м перед камерой — имя видно при ключе 1 (ложных скрытий нет).
//   Строки — «НАДПИСИ-ДЕРЕВЬЯ», итог — «НАДПИСИ-ДЕРЕВЬЯ ИТОГ: ok=… fail=…»; кадры `trees_<NN>.png` в `shotdir=`.
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
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTreesTest, Log, All);

namespace AsdaTreesTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Overhead.SelfTestTrees");
	static const TCHAR* kPrefix = TEXT("НАДПИСИ-ДЕРЕВЬЯ");
	constexpr int32 kPeer = 61;            // сосед (charId 900061)
	constexpr int32 kMobRid = 29401;       // моб-цель
	constexpr int32 kMobType = 1;          // волк alpen
	constexpr float kSettleS = 1.0f;       // ≥ 2 замеров по 10 Гц × 2 голоса + угасание 0,15 с

	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		FString ShotDir;
		int32 ShotN = 0;
		int32 Ok = 0, Fail = 0;
		int32 SavedKey = 1;                // asda.Overhead.OccludeAtText до прогона
		FVector TreeBase = FVector::ZeroVector;   // основание выбранного дерева (UE)
		FBox TreeBox;                      // рамка дерева (UE)
		FString TreeName;
		int32 Polls = 0;
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaTreesTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaTreesTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	using AsdaSelfTestCtx::ByFn::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	static UAsdaWorldSubsystem* WSOf(const TSharedRef<FCtx>& C) { return C->W.Get() ? C->W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
	static AActor* Peer(const TSharedRef<FCtx>& C) { UAsdaWorldSubsystem* WS = WSOf(C); return WS ? WS->ActorByUserIdx(kPeer) : nullptr; }
	static AActor* Mob(const TSharedRef<FCtx>& C) { UAsdaWorldSubsystem* WS = WSOf(C); return WS ? (AActor*)WS->FindNetMobByRid(kMobRid) : nullptr; }
	static void SetKey(int32 V)
	{
		if (IConsoleVariable* K = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Overhead.OccludeAtText"))) { K->Set(V, ECVF_SetByConsole); }
	}

	// Живая плашка: видима и непрозрачна (true) / скрыта (false); строка для журнала.
	static bool PlateShown(const AActor* A, FString& Out)
	{
		const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		const UUserWidget* Wd = NP ? NP->DebugScreenWidget() : nullptr;
		if (!Wd) { Out = TEXT("виджета нет"); return false; }
		const ESlateVisibility V = Wd->GetVisibility();
		const bool bVis = Wd->IsInViewport() && V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
		const float Op = Wd->GetRenderOpacity();
		const FVector2D Sz = Wd->GetCachedGeometry().GetLocalSize();
		FAsdaOccludeHit H;
		AsdaOverheadOcclusion::TraceNow(A->GetWorld(), NP->GetPlateWorldPoint(), A, &H);
		Out = FString::Printf(TEXT("«%s» видим=%d α=%.2f размер=%.0f×%.0f | принято=%d | луч в точку над головой=%d %s"),
			*NP->DisplayName, bVis ? 1 : 0, Op, Sz.X, Sz.Y, NP->DebugOccState().bOccluded, H.bOccluded ? 1 : 0, *H.What);
		return bVis && Op > 0.99f && Sz.X > 0.f;
	}

	static void Shot(TSharedRef<FCtx> C, const TCHAR* Case)
	{
		if (C->ShotDir.IsEmpty() || !GEngine || !C->W.Get()) { return; }
		const FString Name = FString::Printf(TEXT("trees_%02d"), ++C->ShotN);
		GEngine->Exec(C->W.Get(), *FString::Printf(TEXT("asda.UI.Shot %s"), *(C->ShotDir / Name)));
		UE_LOG(LogAsdaTreesTest, Log, TEXT("%s   кадр %s.png — %s"), kPrefix, *Name, Case);
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		SetKey(C->SavedKey);
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaTreesTest, kPrefix, C->Ok, C->Fail);
	}

	// Смещение центра строки имени от проекции точки над головой (px вьюпорта) и полуширина — ТА ЖЕ формула, что в
	//   `UAsdaNameplateComponent::UpdateScreenPosition` (якорь окна канваса 130×80, окно поднято на 50; геометрия контрола имени).
	static bool TextOffsetPx(const AActor* A, FVector2D& OutCtr, float& OutHalf)
	{
		const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		const UUserWidget* Wd = NP ? NP->DebugScreenWidget() : nullptr;
		FVector2D NPos, NSize;
		if (!Wd || !NP->GetNameAbsRect(NPos, NSize) || NSize.X <= 0.f) { return false; }
		const FGeometry& WG = Wd->GetCachedGeometry();
		if (WG.Scale <= 0.f || WG.GetLocalSize().X <= 0.f) { return false; }
		const float DPI = UWidgetLayoutLibrary::GetViewportScale(A->GetWorld());
		const FVector2D L = WG.AbsoluteToLocal(NPos + NSize * 0.5f);
		OutCtr = (L - FVector2D(130.f + NP->ExtraScreenOffsetX, 80.f + 50.f)) * DPI;
		OutHalf = NSize.X * 0.5f / WG.Scale * DPI;
		return true;
	}

	// Перекрыта ли «строка имени» над точкой Anchor (центр CtrPx от проекции, полуширина HalfPx) — та же выборка, что у
	//   `EvaluateText` (края с отступом 30%, решение 2 из 3). OutWhat — что закрыло центр строки.
	static bool TextRowOccluded(UWorld* W, const FVector& Anchor, const AActor* Owner, const FVector2D& CtrPx, float HalfPx, FString& OutWhat)
	{
		int32 Occ = 0;
		OutWhat.Reset();
		const float Hw = HalfPx * 0.7f;
		for (float Dx : { 0.f, -Hw, Hw })
		{
			FVector P;
			AsdaOverheadOcclusion::PointAtScreenOffset(W, Anchor, CtrPx + FVector2D(Dx, 0.f), P);
			FAsdaOccludeHit H;
			Occ += AsdaOverheadOcclusion::TraceNow(W, P, Owner, &H) ? 1 : 0;
			if (Dx == 0.f) { OutWhat = H.What; }
		}
		return Occ >= 2;
	}

	// Поставить актёра A так, чтобы его точка над головой была ВИДНА, а строка имени — закрыта ДЕРЕВОМ карты. Сетка по кадру
	//   (вперёд 4…30 м, вбок ±12 м от взгляда камеры); высота — по сетке .TER.
	static bool PlaceBehindCrown(TSharedRef<FCtx> C, AActor* A, FString& OutWhere)
	{
		UWorld* W = C->W.Get();
		const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		FVector Cam;
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!W || !NP || !PC || !PC->PlayerCameraManager || !AsdaOverheadOcclusion::CameraLocation(W, Cam)) { return false; }
		FVector2D CtrPx; float HalfPx = 0.f;
		if (!TextOffsetPx(A, CtrPx, HalfPx)) { return false; }   // плашка ещё не разложена — следующий поворот
		const FVector Off = NP->GetPlateWorldPoint() - A->GetActorLocation();
		FVector Dir = PC->PlayerCameraManager->GetCameraRotation().Vector(); Dir.Z = 0.f; Dir.Normalize();
		const FVector Side(-Dir.Y, Dir.X, 0.f);
		const UAsdaWalkGrid* G = AsdaGround::GetGrid();
		for (float D = 400.f; D <= 3000.f; D += 100.f)
		{
			for (float L = -1200.f; L <= 1200.f; L += 100.f)
			{
				FVector P = FVector(Cam.X, Cam.Y, 0.f) + Dir * D + Side * L;
				float Y = 0.f;
				if (!G || !G->GetGroundY(P.Y, -P.X, Y)) { continue; }
				P.Z = Y;
				const FVector Pt = P + Off;
				if (AsdaOverheadOcclusion::TraceNow(W, Pt, A)) { continue; }               // точка над головой закрыта — не наш случай
				FString What;
				if (!TextRowOccluded(W, Pt, A, CtrPx, HalfPx, What)) { continue; }          // текст открыт
				if (!What.Contains(TEXT("I_tree"), ESearchCase::IgnoreCase)) { continue; } // текст закрыт не деревом
				A->SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
				OutWhere = FString::Printf(TEXT("вперёд %.0f, вбок %.0f; строка имени %+.0f,%+.0f px ±%.0f; текст закрыт: %s"), D, L, CtrPx.X, CtrPx.Y, HalfPx, *What);
				return true;
			}
		}
		return false;
	}

	// Поиск с поворотами камеры (до 8 по 45°): место, где текст закрыт деревом, а точка над головой — нет.
	static void SearchAndCheck(TSharedRef<FCtx> C, AActor* A, const TCHAR* Who, TFunction<void(TSharedRef<FCtx>)> Next, int32 Try);

	static void CheckPair(TSharedRef<FCtx> C, AActor* A, const TCHAR* Who, TFunction<void(TSharedRef<FCtx>)> Next)
	{
		SetKey(0);
		After(C, kSettleS, [A = TWeakObjectPtr<AActor>(A), Who, Next](TSharedRef<FCtx> C2)
		{
			FString S0;
			const bool bShown0 = PlateShown(A.Get(), S0);
			C2->Expect(bShown0, FString::Printf(TEXT("%s за кроной, ключ OccludeAtText 0 (прежнее): имя видно — баг воспроизведён (%s)"), Who, *S0));
			Shot(C2, TEXT("ключ 0"));
			SetKey(1);
			After(C2, kSettleS, [A, Who, Next](TSharedRef<FCtx> C3)
			{
				FString S1;
				const bool bShown1 = PlateShown(A.Get(), S1);
				C3->Expect(!bShown1, FString::Printf(TEXT("%s за кроной, ключ 1 (z-тест в пикселях текста): имя скрыто (%s)"), Who, *S1));
				Shot(C3, TEXT("ключ 1"));
				After(C3, 0.3f, Next);
			});
		});
	}

	static void SearchAndCheck(TSharedRef<FCtx> C, AActor* A, const TCHAR* Who, TFunction<void(TSharedRef<FCtx>)> Next, int32 Try)
	{
		FString Where;
		if (A && PlaceBehindCrown(C, A, Where))
		{
			UE_LOG(LogAsdaTreesTest, Log, TEXT("%s %s поставлен (поворот %d): %s"), kPrefix, Who, Try, *Where);
			CheckPair(C, A, Who, Next);
			return;
		}
		AAsdaPlayerController* PC = C->W.Get() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
		if (!A || !PC || Try >= 8)
		{
			C->Expect(false, FString::Printf(TEXT("%s: за 8 поворотов камеры не нашлось места, где имя закрыто деревом, а точка над головой видна"), Who));
			Next(C);
			return;
		}
		PC->SetOrbitAnglesForBench(PC->GetCameraYawUE() + 45.f, PC->GetCameraPitchUE());
		After(C, 0.3f, [A = TWeakObjectPtr<AActor>(A), Who, Next, Try](TSharedRef<FCtx> C2) { SearchAndCheck(C2, A.Get(), Who, Next, Try + 1); });
	}

	static void ControlPhase(TSharedRef<FCtx> C)
	{
		AActor* P = Peer(C);
		FVector Cam;
		if (!P || !AsdaOverheadOcclusion::CameraLocation(C->W.Get(), Cam)) { C->Expect(false, TEXT("контроль: нет соседа")); Finish(C); return; }
		// Открытое место: 7 м прямо перед камерой (по взгляду), высота — по сетке .TER.
		APlayerController* PCc = C->W->GetFirstPlayerController();
		FVector Fwd = (PCc && PCc->PlayerCameraManager) ? PCc->PlayerCameraManager->GetCameraRotation().Vector() : FVector::ForwardVector;
		Fwd.Z = 0.f; Fwd.Normalize();
		FVector Q = FVector(Cam.X, Cam.Y, 0.f) + Fwd * 700.f;
		if (const UAsdaWalkGrid* G = AsdaGround::GetGrid()) { float Y = 0.f; if (G->GetGroundY(Q.Y, -Q.X, Y)) { Q.Z = Y; } }
		else { Q.Z = C->TreeBase.Z; }
		P->SetActorLocation(Q, false, nullptr, ETeleportType::TeleportPhysics);
		SetKey(1);
		After(C, kSettleS, [](TSharedRef<FCtx> C2)
		{
			FString S;
			const bool b = PlateShown(Peer(C2), S);
			C2->Expect(b, FString::Printf(TEXT("контроль: сосед на открытом месте в 7 м перед камерой, ключ 1 — имя видно (%s)"), *S));
			Shot(C2, TEXT("контроль"));
			After(C2, 0.3f, &Finish);
		});
	}

	static void MobPhase(TSharedRef<FCtx> C)
	{
		SearchAndCheck(C, Mob(C), TEXT("цель-моб (имя+HP)"), &ControlPhase, 0);
	}

	static void PeerPhase(TSharedRef<FCtx> C)
	{
		AActor* P = Peer(C);
		AActor* M = Mob(C);
		if (!P || !M)
		{
			if (++C->Polls > 40) { C->Expect(false, TEXT("подготовка: сосед/моб не появились")); Finish(C); return; }
			After(C, 0.2f, &PeerPhase);
			return;
		}
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController())) { PC->SetTarget(M); }
		SearchAndCheck(C, P, TEXT("сосед (имя игрока)"), &MobPhase, 0);
	}

	static void FaceTree(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		UAsdaWorldSubsystem* WS = WSOf(C);
		UAsdaNetSubsystem* Net = W ? AsdaTestNet::Get(W->GetGameInstance()) : nullptr;
		const AActor* Self = WS ? WS->LocalPlayerActor.Get() : nullptr;
		if (!PC || !Self || !Net) { C->Expect(false, TEXT("подготовка: нет своего персонажа/камеры")); Finish(C); return; }
		const FVector D = C->TreeBase - Self->GetActorLocation();
		PC->SetOrbitAnglesForBench(FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)), PC->GetCameraPitchUE());
		const FVector2D G0 = AsdaCoord::UeToGame2D(Self->GetActorLocation());
		WS->DebugSpawnFakePeer(kPeer, TEXT("Лесник"), G0 + FVector2D(150.f, 150.f));
		AsdaTestNet::DebugFeedPacket(Net, 4013, AsdaTestPackets::MobSpawn4013(kMobRid, kMobType, FIntPoint((int32)(G0.X / 100.f) + 2, (int32)(G0.Y / 100.f) - 2), 1, 100));
		C->Expect(true, FString::Printf(TEXT("подготовка: дерево «%s» высота %.0f, свой в 14 м, камера на дерево"), *C->TreeName, C->TreeBox.Max.Z - C->TreeBox.Min.Z));
		After(C, 1.0f, &PeerPhase);
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWorldSubsystem* WS = WSOf(C);
		if (!W || !WS) { Finish(C); return; }
		if (IConsoleVariable* K = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Overhead.OccludeAtText"))) { C->SavedKey = K->GetInt(); }
		// Дерево: самый высокий экземпляр с широкой кроной (≥ 4 м), до 25 м высоты.
		float BestH = 0.f;
		for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
		{
			UInstancedStaticMeshComponent* I = *It;
			if (!I || I->GetWorld() != W || !I->IsRegistered() || !I->GetStaticMesh()) { continue; }
			const FString N = I->GetStaticMesh()->GetName();
			if (!N.StartsWith(TEXT("SM_tree"), ESearchCase::IgnoreCase)) { continue; }
			for (int32 k = 0; k < FMath::Min(I->GetInstanceCount(), 8); ++k)
			{
				FTransform T;
				if (!I->GetInstanceTransform(k, T, true)) { continue; }
				const FBox B = I->GetStaticMesh()->GetBoundingBox().TransformBy(T);
				const float H = B.Max.Z - B.Min.Z;
				const float Wd = FMath::Min(B.Max.X - B.Min.X, B.Max.Y - B.Min.Y);
				if (H < 500.f || H > 2500.f || Wd < 400.f || H <= BestH) { continue; }
				BestH = H; C->TreeBox = B; C->TreeName = N;
				C->TreeBase = FVector(B.GetCenter().X, B.GetCenter().Y, B.Min.Z);
			}
		}
		if (BestH <= 0.f) { C->Expect(false, TEXT("на карте нет дерева 5–25 м с кроной ≥ 4 м")); Finish(C); return; }
		// Свой — в 14 м от ствола (направление — от центра карты к дереву не важно; берём +X).
		const FVector SelfUe = C->TreeBase + FVector(1400.f, 0.f, 0.f);
		const FVector2D SG = AsdaCoord::UeToGame2D(SelfUe);
		if (GEngine) { GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3")); }
		WS->DebugSpawnSelfAt((int32)(SG.X / 100.f), (int32)(SG.Y / 100.f), TEXT("СамТест"));
		After(C, 1.5f, &FaceTree);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaOverheadSelfTestTrees(
	TEXT("asda.Overhead.SelfTestTrees"),
	TEXT("asda.Overhead.SelfTestTrees [shotdir=<папка>] [quit] — ОФЛАЙН: имя соседа и цели-моба за кроной настоящего дерева карты скрыто ")
	TEXT("(ключ asda.Overhead.OccludeAtText 1) и видно при 0; контроль — перед деревом видно"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaTreesTestLocal;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		TSharedRef<FCtx> C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		for (const FString& Arg : KA.Pos) { if (Arg.StartsWith(TEXT("shotdir="))) { C->ShotDir = Arg.Mid(8).Replace(TEXT("\\"), TEXT("/")); } }
		Run(C);
	}));

#endif // !UE_BUILD_SHIPPING
