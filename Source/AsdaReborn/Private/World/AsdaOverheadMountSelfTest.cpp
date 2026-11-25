// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НАДПИСИ НАД ВСАДНИКОМ» — `asda.Overhead.SelfTestMount [shotdir=<папка>] [quit]` (06.11.2026, баг «плашка имени не
//   поднимается на парящем маунте»; разбор эталона — у `AsdaOverheadOcclusion::MountLiftZ`). Только офлайн (`-AsdaNoNet`).
//
// ПУТЬ ИГРОКА: сосед — снапшот 4005 (`DebugSpawnFakePeer`), посадка и высадка — настоящие 5453/5454 штатному приёмнику, лавка — 6304,
//   облачко — `ShowBalloon`; надписи считает и рисует боевой код. Перебираются ВСЕ модели транспорта таблицы `special_itemlist`
//   (SubType 15, 81 модель, в том числе 28 моделей Fable). На каждой:
//     • плашка: точка над головой = пешая + heightOffset транспорта из таблицы (ключ asda.Overhead.MountLift 1);
//     • облачко: та же прибавка; вывеска лавки: та же прибавка (была и раньше);
//     • журнал: верх всадника (по костям позы) и верх транспорта относительно точки плашки (строки «МОДЕЛЬ»).
//   Случаи: облако 0006 и модель Fable 0055 — кадр и живой виджет при ключе 1 и 0; свой персонаж — облачко поднято (5453 своего);
//   высадка — точка возвращается; перекрытие — узкий заслон закрывает ТОЛЬКО поднятую строку имени: ключ 1 — имя скрыто,
//   ключ 0 — строка ниже заслона, имя видно (то есть `OccludeAtText` считает от новой высоты).
//   Строки — «НАДПИСИ-ВСАДНИК», итог — «НАДПИСИ-ВСАДНИК ИТОГ: ok=… fail=…»; кадры `mount_<NN>.png` в `shotdir=`.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "World/AsdaOverheadOcclusion.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "UI/AsdaChatBalloonSubsystem.h"
#include "UI/AsdaStallSignSubsystem.h"
#include "UI/AsdaSelfStoreSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMountPlateTest, Log, All);

namespace AsdaMountPlateLocal
{
	static const TCHAR* kCmd = TEXT("asda.Overhead.SelfTestMount");
	static const TCHAR* kPrefix = TEXT("НАДПИСИ-ВСАДНИК");
	constexpr int32 kSelfUserNum = 18;     // свой userNum на прогоне
	constexpr int32 kSelfCharId = 424260;  // свой charId на прогоне
	constexpr int32 kPeer = 62;            // сосед (charId 900062)
	constexpr int32 kMaxCode = 800;        // верхняя граница перебора кодов предметов (таблица кончается на 637)
	constexpr float kStepS = 0.12f;        // шаг перебора моделей: посадка синхронна, нужен кадр на кости и плашку
	constexpr float kSettleS = 1.0f;       // замеры перекрытия: 10 Гц × 2 голоса + угасание 0,15 с

	// Одна модель транспорта таблицы.
	struct FMount { int32 Code = 0; FString Model; float H = 0.f; };

	struct FCtx
	{
		FAsdaNetSelfScope SelfScope;       // «свой» — вернуть как было
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		FString ShotDir;
		int32 ShotN = 0;
		int32 Ok = 0, Fail = 0;
		TArray<FMount> Mounts;             // по одному коду на модель
		int32 Idx = 0;                     // текущая модель перебора
		int32 Polls = 0;                   // ожидание соседа
		float FootPlateZ = 0.f;            // точка плашки пешего относительно актора
		float FootBalloonZ = 0.f;          // точка облачка пешего относительно актора
		float FootSignZ = 0.f;             // точка вывески пешего относительно актора
		int32 NBelow = 0;                  // моделей, где верх всадника выше точки плашки
		TWeakObjectPtr<AActor> Blocker;    // узкий заслон
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaMountPlateTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaMountPlateTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	using AsdaSelfTestCtx::ByFn::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	static UGameInstance* GIOf(const TSharedRef<FCtx>& C) { return C->W.Get() ? C->W->GetGameInstance() : nullptr; }
	template <class T> static T* Sub(const TSharedRef<FCtx>& C) { UGameInstance* GI = GIOf(C); return GI ? GI->GetSubsystem<T>() : nullptr; }
	static AAsdaPlayerCharacter* Peer(const TSharedRef<FCtx>& C)
	{
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		return WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeer)) : nullptr;
	}
	static void SetKey(int32 V)
	{
		if (IConsoleVariable* K = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Overhead.MountLift"))) { K->Set(V, ECVF_SetByConsole); }
	}

	// Посадка 5453 (10 б: i16 userIdx, i32 код, i32 срок) и высадка 5454 (6 б) — штатным приёмником.
	static void FeedMount(const TSharedRef<FCtx>& C, int32 UserNum, int32 Code, bool bOn)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		if (!Net) { return; }
		TArray<uint8> B; B.SetNumZeroed(bOn ? 10 : 6);
		AsdaBytes::WrI16(B.GetData(), UserNum);
		AsdaBytes::WrI32(B.GetData() + 2, Code);
		if (bOn) { AsdaBytes::WrI32(B.GetData() + 6, 3600); }
		Net->DebugFeedPacket(bOn ? 5453 : 5454, B);
	}

	// Показать облачко заново (таймер жизни 5 с) и обновить вывески.
	static void Refresh(const TSharedRef<FCtx>& C, AActor* A)
	{
		if (UAsdaChatBalloonSubsystem* B = C->W.Get() ? C->W->GetSubsystem<UAsdaChatBalloonSubsystem>() : nullptr)
		{
			B->ShowBalloon(A, EAsdaTalkBox::White, TEXT("Проверка высоты"), FColor(0, 0, 0, 255), false);
		}
		if (UAsdaStallSignSubsystem* G = Sub<UAsdaStallSignSubsystem>(C)) { G->UpdateNameStatusList(true); G->UpdateStalls(true); }
	}

	static float PlateRelZ(const AActor* A)
	{
		const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		return NP ? (float)(NP->GetPlateWorldPoint().Z - A->GetActorLocation().Z) : 0.f;
	}
	static bool BalloonRelZ(const TSharedRef<FCtx>& C, const AActor* A, float& Out)
	{
		UAsdaChatBalloonSubsystem* B = C->W.Get() ? C->W->GetSubsystem<UAsdaChatBalloonSubsystem>() : nullptr;
		FAsdaBalloonDebugInfo I;
		if (!A || !B || !B->DebugGetBalloon(A, EAsdaTalkBox::White, I) || !I.bActive) { return false; }
		Out = (float)(I.WorldPt.Z - A->GetActorLocation().Z);
		return true;
	}
	static bool SignRelZ(const TSharedRef<FCtx>& C, const AActor* A, float& Out)
	{
		UAsdaStallSignSubsystem* G = Sub<UAsdaStallSignSubsystem>(C);
		FVector P;
		if (!A || !G || !G->DebugSignWorldPoint(kPeer, P)) { return false; }
		Out = (float)(P.Z - A->GetActorLocation().Z);
		return true;
	}
	static void Shot(const TSharedRef<FCtx>& C, const FString& Case)
	{
		if (C->ShotDir.IsEmpty() || !GEngine || !C->W.Get()) { return; }
		const FString Name = FString::Printf(TEXT("mount_%02d"), ++C->ShotN);
		GEngine->Exec(C->W.Get(), *FString::Printf(TEXT("asda.UI.Shot %s"), *(C->ShotDir / Name)));
		UE_LOG(LogAsdaMountPlateTest, Log, TEXT("%s   кадр %s.png — %s"), kPrefix, *Name, *Case);
	}
	static bool PlateShown(const AActor* A, FString& Out)
	{
		const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		const UUserWidget* Wd = NP ? NP->DebugScreenWidget() : nullptr;
		if (!Wd) { Out = TEXT("виджета нет"); return false; }
		const ESlateVisibility V = Wd->GetVisibility();
		const bool bVis = Wd->IsInViewport() && V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
		Out = FString::Printf(TEXT("«%s» видим=%d α=%.2f принято=%d"), *NP->DisplayName, bVis ? 1 : 0, Wd->GetRenderOpacity(), NP->DebugOccState().bOccluded);
		return bVis && Wd->GetRenderOpacity() > 0.99f;
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		SetKey(1);
		if (AActor* B = C->Blocker.Get()) { B->Destroy(); }
		C->SelfScope.Restore();
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaMountPlateTest, kPrefix, C->Ok, C->Fail);
	}

	static int32 FindModel(const TSharedRef<FCtx>& C, const TCHAR* Tail)
	{
		for (int32 i = 0; i < C->Mounts.Num(); ++i) { if (C->Mounts[i].Model.EndsWith(Tail)) { return i; } }
		return INDEX_NONE;
	}

	// ── свой персонаж: облачко всадника поднято (своя ветка эталона game_main.c:4478-4491) ──
	static void SelfPhase(TSharedRef<FCtx> C)
	{
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		AActor* Me = WS ? WS->LocalPlayerActor.Get() : nullptr;
		const int32 M = FindModel(C, TEXT("_0006"));
		if (!Me || M == INDEX_NONE) { C->Expect(false, TEXT("свой: нет персонажа/облака 0006")); Finish(C); return; }
		Refresh(C, Me);
		After(C, 0.2f, [Me = TWeakObjectPtr<AActor>(Me), M](TSharedRef<FCtx> C2)
		{
			float Foot = 0.f;
			const bool bFoot = BalloonRelZ(C2, Me.Get(), Foot);
			FeedMount(C2, kSelfUserNum, C2->Mounts[M].Code, true);
			Refresh(C2, Me.Get());
			After(C2, 0.3f, [Me, M, bFoot, Foot](TSharedRef<FCtx> C3)
			{
				float Up = 0.f;
				const bool bUp = BalloonRelZ(C3, Me.Get(), Up);
				C3->Expect(bFoot && bUp && FMath::IsNearlyEqual(Up - Foot, C3->Mounts[M].H, 1.f),
					FString::Printf(TEXT("свой на облаке: облачко %.0f → %.0f, ждём +%.0f"), Foot, Up, C3->Mounts[M].H));
				Shot(C3, TEXT("свой на облаке, облачко поднято"));
				FeedMount(C3, kSelfUserNum, C3->Mounts[M].Code, false);
				After(C3, 0.3f, &Finish);
			});
		});
	}

	// ── перекрытие: узкий заслон закрывает только поднятую строку имени ──
	static void OccludePhase(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		AAsdaPlayerCharacter* P = Peer(C);
		const UAsdaNameplateComponent* NP = P ? P->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		const UUserWidget* Wd = NP ? NP->DebugScreenWidget() : nullptr;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		FVector Cam;
		FVector2D NPos, NSize;
		const int32 M = FindModel(C, TEXT("_0006"));
		if (!W || !Wd || !Cube || M == INDEX_NONE || !AsdaOverheadOcclusion::CameraLocation(W, Cam) || !NP->GetNameAbsRect(NPos, NSize))
		{
			C->Expect(false, TEXT("перекрытие: нет плашки/куба/камеры"));
			SelfPhase(C);
			return;
		}
		// Точка центра поднятой строки имени (та же формула, что в `UpdateScreenPosition`), заслон — на 60% пути камера → она:
		//   высота 40 ед. (тоньше подъёма 108), ширина 900 — все три луча строки в него, строка на пешей высоте под ним.
		const FGeometry& WG = Wd->GetCachedGeometry();
		const float DPI = UWidgetLayoutLibrary::GetViewportScale(W);
		const FVector2D Ctr = (WG.AbsoluteToLocal(NPos + NSize * 0.5f) - FVector2D(130.f + NP->ExtraScreenOffsetX, 130.f)) * DPI;
		FVector Row;
		AsdaOverheadOcclusion::PointAtScreenOffset(W, NP->GetPlateWorldPoint(), Ctr, Row);
		const FVector Loc = Cam + (Row - Cam) * 0.6f;
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(Loc, FRotator(0.f, (Row - Cam).Rotation().Yaw, 0.f), SP);
		if (!A) { C->Expect(false, TEXT("перекрытие: заслон не заспавнился")); SelfPhase(C); return; }
		A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(Cube);
		A->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		A->GetStaticMeshComponent()->SetHiddenInGame(true);   // только коллизия: кадр показывает надпись, а не куб
		A->SetActorScale3D(FVector(0.2f, 9.f, 0.4f));
		C->Blocker = A;
		SetKey(1);
		After(C, kSettleS, [](TSharedRef<FCtx> C2)
		{
			FString S1;
			const bool b1 = PlateShown(Peer(C2), S1);
			C2->Expect(!b1, FString::Printf(TEXT("заслон на высоте поднятой строки, ключ MountLift 1: имя скрыто — перекрытие считается от новой высоты (%s)"), *S1));
			Shot(C2, TEXT("заслон, ключ 1"));
			SetKey(0);
			After(C2, kSettleS, [](TSharedRef<FCtx> C3)
			{
				FString S0;
				const bool b0 = PlateShown(Peer(C3), S0);
				C3->Expect(b0, FString::Printf(TEXT("тот же заслон, ключ 0: строка ниже заслона, имя видно (%s)"), *S0));
				SetKey(1);
				if (AActor* B = C3->Blocker.Get()) { B->Destroy(); }
				C3->Blocker = nullptr;
				After(C3, 0.3f, &SelfPhase);
			});
		});
	}

	// ── кадры и живой виджет: облако, Fable, высадка ──
	static void CaseMount(TSharedRef<FCtx> C, const TCHAR* Tail, TFunction<void(TSharedRef<FCtx>)> Next)
	{
		const int32 M = FindModel(C, Tail);
		if (M == INDEX_NONE) { C->Expect(false, FString::Printf(TEXT("нет модели %s"), Tail)); Next(C); return; }
		FeedMount(C, kPeer, C->Mounts[M].Code, true);
		Refresh(C, Peer(C));
		const FString Model = C->Mounts[M].Model;
		After(C, 0.6f, [Model, Next](TSharedRef<FCtx> C2)
		{
			FString S;
			C2->Expect(PlateShown(Peer(C2), S), FString::Printf(TEXT("%s, ключ 1: имя над всадником видно (%s)"), *Model, *S));
			Shot(C2, Model + TEXT(", ключ 1"));
			After(C2, 0.3f, [Model, Next](TSharedRef<FCtx> C3)
			{
				SetKey(0);
				After(C3, 0.3f, [Model, Next](TSharedRef<FCtx> C4)
				{
					C4->Expect(FMath::IsNearlyEqual(PlateRelZ(Peer(C4)), C4->FootPlateZ, 1.f), FString::Printf(TEXT("%s, ключ 0: точка на пешей высоте (прежнее поведение)"), *Model));
					Shot(C4, Model + TEXT(", ключ 0"));
					After(C4, 0.3f, [Next](TSharedRef<FCtx> C5) { SetKey(1); Next(C5); });
				});
			});
		});
	}

	// ── перебор всех моделей ──
	static void SweepStep(TSharedRef<FCtx> C);
	static void SweepCheck(TSharedRef<FCtx> C)
	{
		const FMount& M = C->Mounts[C->Idx];
		AAsdaPlayerCharacter* P = Peer(C);
		if (!P) { C->Expect(false, TEXT("перебор: соседа нет")); Finish(C); return; }
		const float Plate = PlateRelZ(P);
		float Bal = 0.f, Sign = 0.f;
		const bool bBal = BalloonRelZ(C, P, Bal);
		const bool bSign = SignRelZ(C, P, Sign);
		const float BaseZ = (float)P->GetActorLocation().Z;
		// Верх всадника — по костям текущей позы (кэшированные габариты скелета посадку не отражают).
		float RiderTop = 0.f;
		if (P->BaseSkel)
		{
			for (int32 b = 0; b < P->BaseSkel->GetNumBones(); ++b)
			{
				RiderTop = FMath::Max(RiderTop, (float)P->BaseSkel->GetBoneTransform(b).GetLocation().Z - BaseZ);
			}
		}
		const float MountTop = P->MountMesh ? (float)(P->MountMesh->Bounds.Origin.Z + P->MountMesh->Bounds.BoxExtent.Z) - BaseZ : 0.f;
		const bool bMounted = P->IsMounted();
		if (RiderTop > Plate) { ++C->NBelow; }
		UE_LOG(LogAsdaMountPlateTest, Log, TEXT("%s   МОДЕЛЬ %s код %d H=%.0f: плашка %.0f облачко %.0f вывеска %.0f | верх всадника %.0f, верх транспорта %.0f"),
			kPrefix, *M.Model, M.Code, M.H, Plate, Bal, Sign, RiderTop, MountTop);
		C->Expect(bMounted && FMath::IsNearlyEqual(Plate, C->FootPlateZ + M.H, 1.f)
			&& bBal && FMath::IsNearlyEqual(Bal, C->FootBalloonZ + M.H, 1.f) && bSign && FMath::IsNearlyEqual(Sign, C->FootSignZ + M.H, 1.f),
			FString::Printf(TEXT("%s (код %d): верхом=%d, плашка %.0f (ждём %.0f), облачко %.0f (ждём %.0f), вывеска %.0f (ждём %.0f)"), *M.Model, M.Code, bMounted ? 1 : 0,
				Plate, C->FootPlateZ + M.H, Bal, C->FootBalloonZ + M.H, Sign, C->FootSignZ + M.H));
		FeedMount(C, kPeer, M.Code, false);
		++C->Idx;
		After(C, 0.03f, &SweepStep);
	}
	static void SweepStep(TSharedRef<FCtx> C)
	{
		if (C->Idx >= C->Mounts.Num())
		{
			// Высадка возвращает пешую высоту.
			C->Expect(FMath::IsNearlyEqual(PlateRelZ(Peer(C)), C->FootPlateZ, 1.f), FString::Printf(TEXT("после высадки точка плашки %.0f (пешая %.0f)"), PlateRelZ(Peer(C)), C->FootPlateZ));
			UE_LOG(LogAsdaMountPlateTest, Log, TEXT("%s перебрано моделей %d, верх всадника выше точки плашки у %d"), kPrefix, C->Mounts.Num(), C->NBelow);
			CaseMount(C, TEXT("_0006"), [](TSharedRef<FCtx> C2)
			{
				FeedMount(C2, kPeer, C2->Mounts[FindModel(C2, TEXT("_0006"))].Code, false);
				CaseMount(C2, TEXT("_0055"), [](TSharedRef<FCtx> C3)
				{
					FeedMount(C3, kPeer, C3->Mounts[FindModel(C3, TEXT("_0055"))].Code, false);
					FeedMount(C3, kPeer, C3->Mounts[FindModel(C3, TEXT("_0006"))].Code, true);
					After(C3, 0.5f, &OccludePhase);
				});
			});
			return;
		}
		FeedMount(C, kPeer, C->Mounts[C->Idx].Code, true);
		if (C->Idx % 15 == 0) { Refresh(C, Peer(C)); }   // облачко живёт 5 с — обновляем с запасом (шаг перебора ≈ 0,2 с)
		After(C, kStepS, &SweepCheck);
	}

	static void Arrange(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		AAsdaPlayerCharacter* P = Peer(C);
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		const AActor* Self = WS ? WS->LocalPlayerActor.Get() : nullptr;
		if (!P || !PC || !PC->PlayerCameraManager || !Self)
		{
			if (++C->Polls > 40) { C->Expect(false, TEXT("подготовка: сосед не появился за 8 с")); Finish(C); return; }
			After(C, 0.2f, &Arrange);
			return;
		}
		const float Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
		P->SetActorLocation(Self->GetActorLocation() + FRotator(0.f, Yaw, 0.f).Vector() * 700.f, false, nullptr, ETeleportType::TeleportPhysics);
		// Лавка соседа: 6304 (56 б: [0] открыта, [1] премиум, @2 charId, @6 название[50]).
		TArray<uint8> B; B.SetNumZeroed(56);
		B[0] = 1;
		AsdaBytes::WrI32(B.GetData() + 2, 900000 + kPeer);
		FAsdaLoginData::EncodeCp1251(TEXT("Высота"), B.GetData() + 6, 49);
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C)) { Net->DebugFeedPacket(6304, B); }
		if (UAsdaStallSignSubsystem* G = Sub<UAsdaStallSignSubsystem>(C)) { G->PumpPending(); }
		Refresh(C, P);
		After(C, 1.0f, [](TSharedRef<FCtx> C2)
		{
			AAsdaPlayerCharacter* P2 = Peer(C2);
			C2->FootPlateZ = PlateRelZ(P2);
			const bool bBal = BalloonRelZ(C2, P2, C2->FootBalloonZ);
			const bool bSign = SignRelZ(C2, P2, C2->FootSignZ);
			FString S;
			C2->Expect(PlateShown(P2, S) && bBal && bSign, FString::Printf(TEXT("пеший: плашка %.0f, облачко %.0f, вывеска %.0f (%s)"), C2->FootPlateZ, C2->FootBalloonZ, C2->FootSignZ, *S));
			Shot(C2, TEXT("пеший"));
			After(C2, 0.3f, &SweepStep);
		});
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		if (!W || !WS || !Net) { C->Expect(false, TEXT("нет подсистем")); Finish(C); return; }
		// Все модели: код → модель через тот же индекс, что посадка (`LookupMount`), по одному коду на модель.
		TSet<FString> Seen;
		for (int32 Code = 1; Code <= kMaxCode; ++Code)
		{
			FString Model, Anim; float H = 0.f;
			if (!WS->LookupMount(Code, Model, Anim, H) || Seen.Contains(Model)) { continue; }
			Seen.Add(Model);
			FMount M; M.Code = Code; M.Model = Model; M.H = H;
			C->Mounts.Add(M);
		}
		C->Expect(C->Mounts.Num() >= 81, FString::Printf(TEXT("моделей транспорта в таблице %d (ждём не меньше 81)"), C->Mounts.Num()));
		SetKey(1);
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		}
		C->SelfScope.Capture(Net, kCmd);
		Net->SelectedCharId = kSelfCharId;
		Net->SetSelfUserNumForDebug(kSelfUserNum);
		const AActor* Self = WS->LocalPlayerActor.Get();
		const FVector2D G0 = Self ? AsdaCoord::UeToGame2D(Self->GetActorLocation()) : FVector2D(25600.f, 25600.f);
		WS->DebugSpawnFakePeer(kPeer, TEXT("Всадник"), G0 + FVector2D(200.f, 0.f));
		After(C, 1.0f, &Arrange);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaOverheadSelfTestMount(
	TEXT("asda.Overhead.SelfTestMount"),
	TEXT("asda.Overhead.SelfTestMount [shotdir=<папка>] [quit] — ОФЛАЙН: плашка, облачко и вывеска всадника поднимаются на heightOffset транспорта на всех ")
	TEXT("моделях таблицы (5453/5454), перекрытие считается от новой высоты; контроль — ключ asda.Overhead.MountLift 0"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaMountPlateLocal;
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
