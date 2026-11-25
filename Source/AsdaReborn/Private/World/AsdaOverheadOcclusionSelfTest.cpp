// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НАДПИСИ НАД ГОЛОВОЙ ЗА ПРЕПЯТСТВИЯМИ» — `asda.Overhead.SelfTestOcclude [delay=N] [shot] [quit]` (баг игроков 3,
//   29.10.2026; разбор оригинала — `Public/World/AsdaOverheadOcclusion.h`). Только офлайн (`-AsdaNoNet`).
//
// ПУТЬ ИГРОКА: свой персонаж (`DebugSpawnSelfAt`), сосед — снапшот 4005 (`DebugSpawnFakePeer`), его лавка — пакет 6304 штатным
//   `DebugFeedPacket` (очередь → прилавок → вывеска), облачко — `ShowBalloon`, моб — запись 4013 штатному приёмнику. Надписи
//   рисует боевой код (плашки `UAsdaNameplateComponent`, вывески `UAsdaStallSignSubsystem`, облачка `UAsdaChatBalloonSubsystem`);
//   проверяется ЖИВОЙ виджет: видимость, непрозрачность и геометрия Slate (`GetCachedGeometry`).
//   Случаи (у каждого «перекрыто» — контрольная пара с ключом asda.Overhead.Occlude 0, закон pixel-metric-needs-control-pairs):
//     1) на виду — все четыре надписи видны (непрозрачность 1, геометрия ненулевая);
//     2) заслон (куб с коллизией BlockAll) между камерой и целями — все четыре скрыты; при ключе 0 — видны;
//     3) заслон убран — снова видны;
//     4) НАСТОЯЩАЯ геометрия карты: луч из камеры по сетке экрана до стены/крыши/дерева карты, сосед ставится так, что точка
//        его плашки — за препятствием; ключ 0 — видны, ключ 1 — скрыты (что задел луч — в журнале);
//     5) цена: лучей в секунду и среднее время луча за 2 с покоя.
//   ★ R4-хвосты-c (15.11.2026): тест был красным в пакете и зелёным поодиночке по двум причинам, обе — в самом тесте.
//     а) Мерило цены было «мс на кадр», а лучи идут по ВРЕМЕНИ (10 Гц на надпись): при 20 кадрах в секунду (игры пачки разом)
//        на кадр приходится втрое больше лучей, чем при 60, хотя доля времени кадра та же. Теперь мерило — мс лучей за
//        секунду; пороги те же, пересчитанные на 60 кадров в секунду (0,05 мс × 60 = 3; 0,1 мс × 60 = 6).
//     б) Сцена бралась от соседа по пакету: свой персонаж оставался там, куда его поставил прошлый самотест (рядом может не
//        быть подходящей стены, а под землёй перед ним — не рельеф, а чужой меш), и в цену шли плашки чужих мобов и соседей
//        (46 плашек вместо 2–6). Теперь свой всегда ставится на свою клетку (256,256), а при чужих плашках порог цены
//        растёт вместе с числом лучей (цена луча та же) — чужие плашки считаются и пишутся в журнал.
//   ★ R4-хвосты-c (15.11.2026, второй заход): вердикт цены переведён со времени на счёт — лучей в секунду на надпись и запросов на луч (не зависят от
//     нагрузки процессора), время — цена луча по медиане четырёх окон с запасом. Разбор — у констант `kRaysPerInscr`.
//   Строки — «НАДПИСИ-ПЕРЕКРЫТИЕ», итог — «НАДПИСИ-ПЕРЕКРЫТИЕ ИТОГ: ok=… fail=…».
//   Дамп `asda.Overhead.OccludeProbe` — все плашки мира: принятое состояние, непрозрачность, что задевает луч сейчас.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "World/AsdaOverheadOcclusion.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaGroundTrace.h"           // рельеф карты (замер цены луча без него)
#include "World/AsdaCoord.h"                 // своя позиция → игровые координаты соседа
#include "World/AsdaPlayerCharacter.h"        // стрела камеры (наезд в самотесте отставания)
#include "World/AsdaPlayerController.h"        // SetOrbitAnglesForBench — поворот камеры (та же точка, что ПКМ-драг)
#include "UI/AsdaStallSignSubsystem.h"
#include "UI/AsdaSelfStoreSubsystem.h"
#include "UI/AsdaChatBalloonSubsystem.h"
#include "Pet/AsdaPetSubsystem.h"              // перехват исходящих (в сеть не уходит ничего)
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaTestPackets.h"                    // запись 4013 (моб)
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "PhysicsEngine/BodySetup.h"         // перепись: флаг трассировки и простая геометрия меша
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaOccludeTest, Log, All);

namespace AsdaOccludeTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Overhead.SelfTestOcclude");
	constexpr int32 kSelfCharId = 424243;   // свой charId на прогон
	constexpr int32 kSelfUserNum = 18;      // свой userNum
	constexpr int32 kOther = 57;            // сосед-продавец (charId 900057)
	constexpr int32 kMobRid = 29201;        // renderId моба-заглушки
	constexpr int32 kMobType = 1;           // тип моба (Mob list: волк alpen) — как у прочих самотестов
	constexpr float kSettleS = 1.0f;        // ожидание после смены сцены: ≥ 2 луча (10 Гц × 2 голоса) + угасание 0,15 с

	// Тело 6304 (56 б): [0] открыта, [1] премиум, @2 charId, @6 название[50].
	static TArray<uint8> Make6304(uint8 Open, int32 CharId, const FString& Title)
	{
		TArray<uint8> B; B.SetNumZeroed(56);
		B[0] = Open; B[1] = 0;
		AsdaBytes::WrI32(B.GetData() + 2, CharId);
		FAsdaLoginData::EncodeCp1251(Title, B.GetData() + 6, 49);
		return B;
	}

	struct FCtx
	{
		FAsdaNetSelfScope SelfScope;       // «свой» — вернуть как было
		int32 Ok = 0, Fail = 0;
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false, bShot = false;
		FString ShotDir;                   // папка кадров (shotdir=…; пусто — <Saved>/Screenshots, при пакетном прогоне временная)
		int32 ShotN = 0;                   // номер кадра (имя файла — ASCII, случай — в журнале)
		uint32 Gen = 0;                    // перехват исходящих
		int32 SavedKey = 1;                // asda.Overhead.Occlude до прогона
		bool bLag = false;                 // ★ 30.10.2026: прогон `asda.Overhead.SelfTestLag` (отставание от камеры) вместо перекрытия
		int32 SavedLateKey = 1;            // asda.Overhead.LateProject до прогона
		int32 Polls = 0;                   // ожидание соседа/моба
		int32 YawTry = 0;                  // попыток поворота камеры при поиске препятствия карты
		float BaseYaw = 0.f, BasePitch = 0.f;
		TWeakObjectPtr<AActor> Blocker;    // заслон-куб
		FVector MapPoint = FVector::ZeroVector;   // точка плашки соседа за препятствием карты
		FString MapWhat;                   // что задел луч к ней
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaOccludeTest, Log, TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaOccludeTest, Error, TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ FAIL %s"), *What); }
		}
	};

	using AsdaSelfTestCtx::ByFn::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	static UGameInstance* GIOf(const TSharedRef<FCtx>& C) { return C->W.Get() ? C->W.Get()->GetGameInstance() : nullptr; }
	static UAsdaWorldSubsystem* WSOf(const TSharedRef<FCtx>& C) { UGameInstance* GI = GIOf(C); return GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
	static IConsoleVariable* Key() { return IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Overhead.Occlude")); }
	static void SetKey(int32 V) { if (IConsoleVariable* K = Key()) { K->Set(V, ECVF_SetByConsole); } }

	static AActor* Peer(const TSharedRef<FCtx>& C) { UAsdaWorldSubsystem* WS = WSOf(C); return WS ? WS->ActorByUserIdx(kOther) : nullptr; }
	static AActor* Mob(const TSharedRef<FCtx>& C) { UAsdaWorldSubsystem* WS = WSOf(C); return WS ? (AActor*)WS->FindNetMobByRid(kMobRid) : nullptr; }

	// Поставить актёра перед камерой: вперёд Fwd, вбок Side (от своего персонажа, по курсу камеры).
	static void PlaceInFront(UWorld* W, AActor* A, float Fwd, float Side)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		APawn* Self = PC ? PC->GetPawn() : nullptr;
		if (!A || !Self || !PC->PlayerCameraManager || A == Self) { return; }
		const float Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
		const FVector F = FRotator(0.f, Yaw, 0.f).Vector();
		const FVector R = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
		A->SetActorLocation(Self->GetActorLocation() + F * Fwd + R * Side, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Состояние одной надписи: живой виджет (или снимок облачка).
	struct FShown
	{
		bool bHave = false;      // надпись существует
		bool bVisible = false;   // не Collapsed/Hidden
		float Opacity = 0.f;     // непрозрачность виджета
		FVector2D Size = FVector2D::ZeroVector;   // живая геометрия Slate
		bool IsShown() const { return bHave && bVisible && Opacity > 0.99f && Size.X > 0.f && Size.Y > 0.f; }
		bool IsHidden() const { return !bHave || !bVisible || Opacity < 0.01f; }
		FString Str() const { return FString::Printf(TEXT("есть=%d видим=%d α=%.2f размер=%.0f×%.0f"), bHave ? 1 : 0, bVisible ? 1 : 0, Opacity, Size.X, Size.Y); }
	};

	static FShown OfWidget(const UUserWidget* Wd)
	{
		FShown S;
		if (!Wd) { return S; }
		S.bHave = true;
		const ESlateVisibility V = Wd->GetVisibility();
		S.bVisible = Wd->IsInViewport() && V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
		S.Opacity = Wd->GetRenderOpacity();
		S.Size = Wd->GetCachedGeometry().GetLocalSize();
		return S;
	}

	static FShown PlateOf(const AActor* A)
	{
		const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		return OfWidget(NP ? NP->DebugScreenWidget() : nullptr);
	}

	// Подробности плашки для журнала: гейты показа, принятое перекрытие и свежий луч (что задел).
	static FString PlateDiag(const AActor* A)
	{
		const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		if (!NP) { return TEXT("плашки нет"); }
		FAsdaOccludeHit H;
		AsdaOverheadOcclusion::TraceNow(A->GetWorld(), NP->GetPlateWorldPoint(), A, &H);
		const FAsdaOccludeState& S = NP->DebugOccState();
		FVector Cam = FVector::ZeroVector;
		AsdaOverheadOcclusion::CameraLocation(A->GetWorld(), Cam);
		return FString::Printf(TEXT("имя «%s» всегда=%d скрыт=%d | точка %s, до камеры %.0f | замер=%d принято=%d α=%.2f | луч=%d %s %.0f (отброшено %d)"),
			*NP->DisplayName, NP->bAlwaysShowName ? 1 : 0, A->IsHidden() ? 1 : 0, *NP->GetPlateWorldPoint().ToCompactString(),
			FVector::Dist(Cam, NP->GetPlateWorldPoint()), S.bInit, S.bOccluded, S.Alpha, H.bOccluded ? 1 : 0, *H.What, H.Distance, H.Skipped);
	}

	static FShown SignOf(const TSharedRef<FCtx>& C)
	{
		UGameInstance* GI = GIOf(C);
		UAsdaStallSignSubsystem* G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
		FShown S = OfWidget(G ? G->DebugSignWidget(kOther) : nullptr);
		if (G && !G->DebugSignShown(kOther)) { S.bVisible = false; }
		return S;
	}

	static FShown BalloonOf(const TSharedRef<FCtx>& C)
	{
		FShown S;
		UWorld* W = C->W.Get();
		UAsdaChatBalloonSubsystem* B = W ? W->GetSubsystem<UAsdaChatBalloonSubsystem>() : nullptr;
		FAsdaBalloonDebugInfo I;
		if (!B || !B->DebugGetBalloon(Peer(C), EAsdaTalkBox::White, I) || !I.bActive) { return S; }
		S.bHave = true;
		S.bVisible = I.bOnScreen;
		S.Opacity = I.Opacity;
		S.Size = I.PanelSize;   // размер окна облачка (виджета снимок не даёт)
		return S;
	}

	// Обновить вывеску и облачко (таймер облачка 5 с — показываем заново перед каждым случаем).
	static void Refresh(const TSharedRef<FCtx>& C)
	{
		UGameInstance* GI = GIOf(C);
		UWorld* W = C->W.Get();
		if (UAsdaStallSignSubsystem* G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr)
		{
			G->UpdateNameStatusList(true);
			G->UpdateStalls(true);
		}
		if (UAsdaChatBalloonSubsystem* B = W ? W->GetSubsystem<UAsdaChatBalloonSubsystem>() : nullptr)
		{
			B->ShowBalloon(Peer(C), EAsdaTalkBox::White, TEXT("Проверка перекрытия"), FColor(0, 0, 0, 255), false);
		}
	}

	// Проверка всех надписей случая: bWant = true — все видны, false — все скрыты. bPeerOnly — только надписи соседа.
	static void CheckAll(const TSharedRef<FCtx>& C, const TCHAR* Case, bool bWant, bool bPeerOnly)
	{
		struct FItem { const TCHAR* Name; FShown S; };
		TArray<FItem> Items;
		Items.Add({ TEXT("плашка соседа (имя/титул)"), PlateOf(Peer(C)) });
		Items.Add({ TEXT("вывеска лавки соседа"), SignOf(C) });
		Items.Add({ TEXT("облачко чата соседа"), BalloonOf(C) });
		if (!bPeerOnly) { Items.Add({ TEXT("плашка моба (имя/HP)"), PlateOf(Mob(C)) }); }
		for (const FItem& It : Items)
		{
			C->Expect(bWant ? It.S.IsShown() : It.S.IsHidden(),
				FString::Printf(TEXT("%s: %s %s (%s)"), Case, It.Name, bWant ? TEXT("видна") : TEXT("скрыта"), *It.S.Str()));
		}
		UE_LOG(LogAsdaOccludeTest, Log, TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ   [%s] сосед: %s"), Case, *PlateDiag(Peer(C)));
		if (!bPeerOnly) { UE_LOG(LogAsdaOccludeTest, Log, TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ   [%s] моб: %s"), Case, *PlateDiag(Mob(C))); }
		if (C->bShot && GEngine && C->W.Get())
		{
			const FString Name = FString::Printf(TEXT("occlude_%02d"), ++C->ShotN);
			const FString Path = C->ShotDir.IsEmpty() ? Name : (C->ShotDir / Name);   // путь со «/» движок берёт как есть
			GEngine->Exec(C->W.Get(), *FString::Printf(TEXT("asda.UI.Shot %s"), *Path));
			UE_LOG(LogAsdaOccludeTest, Log, TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ   кадр %s.png — %s"), *Name, Case);
		}
	}

	// Следующий шаг после проверки: при кадрах — через 0,3 с (снимок пишется кадром ПОЗЖЕ заказа; смена ключа или снятие заслона
	//   в том же кадре попала бы на снимок вместо проверенного состояния).
	static void Then(TSharedRef<FCtx> C, TFunction<void(TSharedRef<FCtx>)> Fn)
	{
		After(C, C->bShot ? 0.3f : 0.01f, MoveTemp(Fn));
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		if (AActor* B = C->Blocker.Get()) { B->Destroy(); }
		SetKey(C->SavedKey);
		if (IConsoleVariable* LK = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Overhead.LateProject"))) { LK->Set(C->SavedLateKey, ECVF_SetByConsole); }
		if (C->Gen && GI) { if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>()) { Pet->EndSendCapture(); } }
		UE_LOG(LogAsdaOccludeTest, Log, TEXT("%s ИТОГ: ok=%d fail=%d"), C->bLag ? TEXT("НАДПИСИ-ОТСТАВАНИЕ") : TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ"), C->Ok, C->Fail);
		C->SelfScope.Restore();
		AsdaSelfTestKit::Quit(C->W.Get(), C->bQuit);
	}

	// ── 6) цена в толпе: 30 соседей (плашки игроков видны всегда) веером перед камерой на 600…2400 ед. ──
	constexpr int32 kCrowdFirst = 60;   // userNum первого соседа толпы
	constexpr int32 kCrowdN = 30;       // соседей в толпе

	// ★ R4-хвосты-c (15.11.2026, второй заход): вердикт цены не зависит от нагрузки процессора и частоты кадров. Судится СЧЁТ: лучей в секунду на надпись
	//   (частота 10 Гц × до 3 лучей на замер) и запросов физики на луч — оба числа заданы сценой и не меняются от того, сколько
	//   игр идёт на машине. Время судится только грубо — цена луча по МЕДИАНЕ четырёх окон по 0,5 с с запасом: ловит луч,
	//   подорожавший в разы, а не соседа по процессору. Прежний порог «мс за секунду» при чужих плашках оставлял запас 5,47 из 6,59.
	constexpr double kRaysPerInscr = 33.0;    // потолок лучей в секунду на одну надпись: 3 луча × 10 Гц × 1,1
	constexpr double kQueriesPerRay = 2.0;    // потолок запросов физики на луч (обычно ровно 1; больше — повторы после отброшенных попаданий)
	constexpr double kUsPerRayLim = 40.0;     // потолок цены луча, мкс, по медиане окон (наблюдалось 3,5–16)
	constexpr int32 kCostWindows = 4;         // окон замера цены
	constexpr float kCostWindowS = 0.5f;      // длина окна, с (всего те же 2 с покоя)
	// Плашки мира с принятым замером: свои (сосед, моб, свой персонаж, толпа теста) и чужие (оставлены прошлыми самотестами).
	struct FPlateCount
	{
		int32 Own = 0;       // плашки сцены теста
		int32 Foreign = 0;   // плашки чужих сущностей
		int32 Shown = 0;     // из всех — видимых
		int32 Hidden = 0;    // из всех — перекрытых
	};
	static FPlateCount CountPlates(const TSharedRef<FCtx>& C)
	{
		FPlateCount R;
		UAsdaWorldSubsystem* WS = WSOf(C);
		TSet<const AActor*> Mine;
		Mine.Add(Peer(C));
		Mine.Add(Mob(C));
		if (WS)
		{
			Mine.Add(WS->LocalPlayerActor.Get());
			for (int32 i = 0; i < kCrowdN; ++i) { Mine.Add(WS->ActorByUserIdx(kCrowdFirst + i)); }
		}
		Mine.Remove(nullptr);
		for (TObjectIterator<UAsdaNameplateComponent> It; It; ++It)
		{
			if (It->GetWorld() != C->W.Get() || !It->DebugOccState().bInit) { continue; }
			(Mine.Contains(It->GetOwner()) ? R.Own : R.Foreign) += 1;
			(It->DebugOccState().bOccluded ? R.Hidden : R.Shown) += 1;
		}
		return R;
	}
	// Замер цены окнами: каждые `kCostWindowS` с снимается прирост лучей и тактов, по окну считается цена луча; после
	//   `kCostWindows` окон зовётся `Done` с медианой цены луча и числом окон, где лучи были. Счётчики до вызова сброшены вызывающим.
	struct FCostRun
	{
		int64 LastTraces = 0;        // лучей на конец прошлого окна
		uint64 LastCycles = 0;       // тактов на конец прошлого окна
		int32 N = 0;                 // окон пройдено
		TArray<double> UsPerRay;     // цена луча по окнам, мкс (окна без лучей не входят)
		TFunction<void(TSharedRef<FCtx>, double, int32)> Done;   // итог: медиана цены луча, окон с лучами
	};
	static double MedianOf(TArray<double> A)
	{
		if (A.Num() == 0) { return 0.0; }
		A.Sort();
		const int32 M = A.Num() / 2;
		return (A.Num() % 2) ? A[M] : (A[M - 1] + A[M]) * 0.5;
	}
	static void CostStep(TSharedRef<FCtx> C, TSharedRef<FCostRun> R)
	{
		After(C, kCostWindowS, [R](TSharedRef<FCtx> C2)
		{
			int64 Tr = 0, Q = 0; uint64 Cy = 0;
			AsdaOverheadOcclusion::GetStats(Tr, Q, Cy);
			const int64 DT = Tr - R->LastTraces;
			if (DT > 0) { R->UsPerRay.Add(FPlatformTime::ToMilliseconds64(Cy - R->LastCycles) * 1000.0 / (double)DT); }
			R->LastTraces = Tr; R->LastCycles = Cy;
			if (++R->N < kCostWindows) { CostStep(C2, R); return; }
			R->Done(C2, MedianOf(R->UsPerRay), R->UsPerRay.Num());
		});
	}
	static void CostRun(TSharedRef<FCtx> C, TFunction<void(TSharedRef<FCtx>, double, int32)> Done)
	{
		TSharedRef<FCostRun> R = MakeShared<FCostRun>();
		R->Done = MoveTemp(Done);
		CostStep(C, R);
	}
	static void CrowdPhase(TSharedRef<FCtx> C)
	{
		UAsdaWorldSubsystem* WS = WSOf(C);
		const AActor* Self = WS ? WS->LocalPlayerActor.Get() : nullptr;
		if (!WS || !Self) { Finish(C); return; }
		const FVector2D G0 = AsdaCoord::UeToGame2D(Self->GetActorLocation());
		for (int32 i = 0; i < kCrowdN; ++i)
		{
			WS->DebugSpawnFakePeer(kCrowdFirst + i, FString::Printf(TEXT("Толпа%02d"), i), G0 + FVector2D(100.f + 10.f * i, 0.f));
		}
		After(C, 1.5f, [](TSharedRef<FCtx> C2)
		{
			UAsdaWorldSubsystem* WS2 = WSOf(C2);
			int32 Placed = 0;
			for (int32 i = 0; i < kCrowdN && WS2; ++i)
			{
				AActor* A = WS2->ActorByUserIdx(kCrowdFirst + i);
				if (!A) { continue; }
				PlaceInFront(C2->W.Get(), A, 600.f + 60.f * i, (float)((i % 7) - 3) * 180.f);
				++Placed;
			}
			After(C2, 1.0f, [Placed](TSharedRef<FCtx> C3)
			{
				AsdaOverheadOcclusion::ResetStats();
				const uint64 F0 = GFrameCounter;
				const double T0 = FPlatformTime::Seconds();
				CostRun(C3, [F0, T0, Placed](TSharedRef<FCtx> C4, double UsMed, int32 Wins)   // ★ R4-хвосты-c (15.11.2026, второй заход): четыре окна по 0,5 с
				{
					int64 Traces = 0, Queries = 0; uint64 Cycles = 0;
					AsdaOverheadOcclusion::GetStats(Traces, Queries, Cycles);
					const double Sec = FMath::Max(0.001, FPlatformTime::Seconds() - T0);
					const int64 Frames = FMath::Max<int64>(1, (int64)(GFrameCounter - F0));
					const double Ms = FPlatformTime::ToMilliseconds64(Cycles);
					const FPlateCount PC4 = CountPlates(C4);   // ★ R4-хвосты-c (15.11.2026): свои и чужие плашки
					const int32 Shown = PC4.Shown, Hidden = PC4.Hidden;
					const double MsPerSec = Ms / Sec;
					const int32 Inscr = PC4.Own + PC4.Foreign + 2;   // плашки с замером + вывеска и облачко соседа
					const double RayLim = kRaysPerInscr * Inscr;
					UE_LOG(LogAsdaOccludeTest, Log,
						TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ толпа: соседей %d, надписей с замером %d (видно %d, перекрыто %d); лучей %.1f/с, %.2f мкс на луч, %.4f мс на кадр (кадров %lld за %.1f с)"),
						Placed, Shown + Hidden, Shown, Hidden, Traces / Sec, Traces > 0 ? Ms * 1000.0 / Traces : 0.0, Ms / Frames, Frames, Sec);
					// ★ R4-хвосты-c (15.11.2026, второй заход): вердикт — по счёту (лучей на надпись, запросов на луч) и по медиане цены луча; мс за секунду и на кадр — справочно.
					C4->Expect(Placed >= kCrowdN - 2 && Traces > 0 && Traces / Sec <= RayLim && (double)Queries <= kQueriesPerRay * Traces && Wins >= 2 && UsMed < kUsPerRayLim,
						FString::Printf(TEXT("толпа %d соседей: лучей %.1f/с при %d надписях (порог %.0f), запросов на луч %.2f (порог %.0f), цена луча %.1f мкс по медиане %d окон (порог %.0f); чужих плашек %d; справочно %.3f мс лучей за секунду, %.4f мс на кадр"),
							Placed, Traces / Sec, Inscr, RayLim, Traces > 0 ? (double)Queries / Traces : 0.0, kQueriesPerRay, UsMed, Wins, kUsPerRayLim, PC4.Foreign, MsPerSec, Ms / Frames));
					Finish(C4);
				});
			});
		});
	}

	// ── 5) цена ──
	static void PerfPhase(TSharedRef<FCtx> C)
	{
		// Рельеф (сетка .TER): точка на 200 ед. ПОД землёй в 900 ед. перед своим персонажем — луч обязан упереться в рельеф.
		{
			UWorld* W = C->W.Get();
			APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
			APawn* Self = PC ? PC->GetPawn() : nullptr;
			if (Self && PC->PlayerCameraManager)
			{
				const FVector F = FRotator(0.f, PC->PlayerCameraManager->GetCameraRotation().Yaw, 0.f).Vector();
				FVector P = Self->GetActorLocation() + F * 900.f;
				P.Z = AsdaGround::GroundZ(W, P.X, P.Y, P.Z) - 200.f;
				FAsdaOccludeHit H;
				const bool bOcc = AsdaOverheadOcclusion::TraceNow(W, P, nullptr, &H);
				C->Expect(bOcc && H.What.Contains(TEXT(".TER")),
					FString::Printf(TEXT("рельеф: точка под землёй закрыта рельефом по сетке .TER (перекрыто=%d, %s на %.0f)"), bOcc ? 1 : 0, *H.What, H.Distance));
			}
		}
		AsdaOverheadOcclusion::ResetStats();
		const uint64 F0 = GFrameCounter;
		const double T0 = FPlatformTime::Seconds();
		CostRun(C, [F0, T0](TSharedRef<FCtx> C2, double UsMed, int32 Wins)   // ★ R4-хвосты-c (15.11.2026, второй заход): четыре окна по 0,5 с
		{
			int64 Traces = 0, Queries = 0; uint64 Cycles = 0;
			AsdaOverheadOcclusion::GetStats(Traces, Queries, Cycles);
			const double Sec = FMath::Max(0.001, FPlatformTime::Seconds() - T0);
			const int64 Frames = FMath::Max<int64>(1, (int64)(GFrameCounter - F0));
			const double Ms = FPlatformTime::ToMilliseconds64(Cycles);
			const FPlateCount PC2 = CountPlates(C2);   // ★ R4-хвосты-c (15.11.2026): свои и чужие плашки
			const int32 Plates = PC2.Own + PC2.Foreign;
			const double MsPerSec = Ms / Sec;
			const int32 Inscr = Plates + 2;   // плашки с замером + вывеска и облачко соседа
			const double RayLim = kRaysPerInscr * Inscr;
			UE_LOG(LogAsdaOccludeTest, Log,
				TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ цена: %.1f с, кадров %lld, лучей %lld (%.1f/с, %.2f на кадр), запросов %lld, всего %.3f мс, ")
				TEXT("%.1f мкс на луч, %.4f мс на кадр; плашек с замером %d"),
				Sec, Frames, Traces, Traces / Sec, (double)Traces / Frames, Queries, Ms,
				Traces > 0 ? Ms * 1000.0 / Traces : 0.0, Ms / Frames, Plates);
			// ★ R4-хвосты-c (15.11.2026, второй заход): вердикт — по счёту (лучей на надпись, запросов на луч) и по медиане цены луча; мс за секунду и на кадр — справочно.
			C2->Expect(Traces > 0 && Traces / Sec <= RayLim && (double)Queries <= kQueriesPerRay * Traces && Wins >= 2 && UsMed < kUsPerRayLim,
				FString::Printf(TEXT("цена: лучей %.1f/с при %d надписях (порог %.0f, частота 10 Гц на надпись), запросов на луч %.2f (порог %.0f), цена луча %.1f мкс по медиане %d окон (порог %.0f); чужих плашек %d; справочно %.3f мс лучей за секунду, %.4f мс на кадр"),
					Traces / Sec, Inscr, RayLim, Traces > 0 ? (double)Queries / Traces : 0.0, kQueriesPerRay, UsMed, Wins, kUsPerRayLim, PC2.Foreign, MsPerSec, Ms / Frames));
			if (GEngine && C2->W.Get()) { GEngine->Exec(C2->W.Get(), TEXT("asda.Overhead.OccludeBench")); }   // разбор цены луча — в журнал
			CrowdPhase(C2);
		});
	}

	// ── 4) настоящая геометрия карты ──
	static void MapPhase(TSharedRef<FCtx> C);

	static void MapCheck(TSharedRef<FCtx> C)
	{
		// Контрольная пара: ключ 0 — видны (надписи не скрыты чем-то другим: AoI, фрустум, заморозка значимости).
		SetKey(0);
		Refresh(C);
		After(C, kSettleS, [](TSharedRef<FCtx> C2)
		{
			CheckAll(C2, TEXT("за препятствием карты, ключ 0"), true, true);
			Then(C2, [](TSharedRef<FCtx> C2b)
			{
			SetKey(1);
			Refresh(C2b);
			After(C2b, kSettleS, [](TSharedRef<FCtx> C3)
			{
				FAsdaOccludeHit H;
				AsdaOverheadOcclusion::TraceNow(C3->W.Get(), C3->MapPoint, Peer(C3), &H);
				UE_LOG(LogAsdaOccludeTest, Log, TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ карта: луч к точке плашки — перекрыто=%d, задел %s на %.0f (отброшено %d)"),
					H.bOccluded ? 1 : 0, *H.What, H.Distance, H.Skipped);
				CheckAll(C3, TEXT("за препятствием карты"), false, true);
				Then(C3, &PerfPhase);
			});
			});
		});
	}

	static void MapPhase(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		AActor* P = Peer(C);
		const UAsdaNameplateComponent* NP = P ? P->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		FVector Cam;
		if (!PC || !P || !NP || !AsdaOverheadOcclusion::CameraLocation(W, Cam)) { C->Expect(false, TEXT("карта: нет камеры/соседа")); PerfPhase(C); return; }
		int32 VX = 0, VY = 0;
		PC->GetViewportSize(VX, VY);
		// Сетка экрана 6×4: первый луч, который на 400…2500 ед. упирается в геометрию карты (не рельеф).
		bool bFound = false;
		FVector Dir = FVector::ZeroVector;
		FAsdaOccludeHit Best;
		for (int32 gy = 1; gy <= 4 && !bFound; ++gy)
		{
			for (int32 gx = 1; gx <= 6 && !bFound; ++gx)
			{
				FVector O, D;
				if (!PC->DeprojectScreenPositionToWorld(VX * gx / 7.f, VY * gy / 5.f, O, D)) { continue; }
				FAsdaOccludeHit H;
				const bool bHit = AsdaOverheadOcclusion::TraceNow(W, Cam + D * 3000.f, P, &H);
				if (gy == 2 || gy == 3)
				{
					UE_LOG(LogAsdaOccludeTest, Log, TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ   поиск (поворот %d, клетка %d,%d): попадание=%d %s %.0f (отброшено %d)"),
						C->YawTry, gx, gy, bHit ? 1 : 0, *H.What, H.Distance, H.Skipped);
				}
				if (!bHit) { continue; }
				// ★ 05.11.2026: рельеф по сетке .TER подписан «рельеф» — его тоже мимо (нужна стена/крыша/дерево карты).
				if (H.Distance < 400.f || H.Distance > 2500.f || H.What.Contains(TEXT("terrain"), ESearchCase::IgnoreCase)
					|| H.What.Contains(TEXT("рельеф"))) { continue; }
				// ★ 06.11.2026: плашка скрыта, когда перекрыты 2 из 3 лучей по ширине строки. Тонкий ствол ловит один луч, и тест падал не из-за плашки:
				//   препятствие годится, только если закрывает и края строки.
				const auto Wide = [&](float Dx, float Dy)
				{
					FVector O2, D2; FAsdaOccludeHit H2;
					return PC->DeprojectScreenPositionToWorld(VX * gx / 7.f + Dx, VY * gy / 5.f + Dy, O2, D2) && AsdaOverheadOcclusion::TraceNow(W, Cam + D2 * 3000.f, P, &H2)
						&& H2.Distance >= 400.f && H2.Distance <= 2600.f && !H2.What.Contains(TEXT("рельеф"));
				};
				// Точка плашки лежит ниже строки имени, вывеска и облачко смотрят в неё: препятствие должно закрывать и её.
				if (!Wide(-90.f, 0.f) || !Wide(90.f, 0.f) || !Wide(0.f, -120.f) || !Wide(0.f, 120.f)) { continue; }
				bFound = true; Dir = D; Best = H;
			}
		}
		if (!bFound)
		{
			if (++C->YawTry <= 8)
			{
				PC->SetOrbitAnglesForBench(C->BaseYaw + 45.f * C->YawTry, C->BasePitch);
				After(C, 0.3f, &MapPhase);
				return;
			}
			C->Expect(false, TEXT("карта: за 8 поворотов камеры луч не нашёл стены/крыши/дерева карты на 400…2500 ед."));
			PerfPhase(C);
			return;
		}
		// Точка плашки соседа — на 250 ед. ЗА препятствием по тому же лучу (в кадре, в пределах AoI). ★ 05.11.2026
		//   (asda.Overhead.OccludeAtText): за препятствием должна оказаться СТРОКА ИМЕНИ (её пиксели проходят z-тест у оригинала),
		//   поэтому точка опускается на экранный сдвиг строки от точки (та же формула, что у плашки: якорь канваса 130×80, окно +50).
		C->MapPoint = Cam + Dir * (Best.Distance + 250.f);
		{
			FVector2D NPos, NSize;
			const UUserWidget* Wd = NP->DebugScreenWidget();
			if (Wd && NP->GetNameAbsRect(NPos, NSize) && NSize.X > 0.f && Wd->GetCachedGeometry().Scale > 0.f)
			{
				const FGeometry& WG = Wd->GetCachedGeometry();
				const float DPI = UWidgetLayoutLibrary::GetViewportScale(W);
				const FVector2D Ctr = (WG.AbsoluteToLocal(NPos + NSize * 0.5f) - FVector2D(130.f + NP->ExtraScreenOffsetX, 130.f)) * DPI;
				FVector Q;
				if (AsdaOverheadOcclusion::PointAtScreenOffset(W, C->MapPoint, -Ctr, Q)) { C->MapPoint = Q; }
			}
		}
		C->MapWhat = Best.What;
		const FVector Off = NP->GetPlateWorldPoint() - P->GetActorLocation();
		P->SetActorLocation(C->MapPoint - Off, false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogAsdaOccludeTest, Log, TEXT("НАДПИСИ-ПЕРЕКРЫТИЕ карта: препятствие %s на %.0f ед. (поворот %d), сосед за ним"),
			*Best.What, Best.Distance, C->YawTry);
		MapCheck(C);
	}

	// ── 2–3) заслон ──
	static void BlockerPhase(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		AActor* P = Peer(C);
		AActor* M = Mob(C);
		const UAsdaNameplateComponent* NPP = P ? P->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		const UAsdaNameplateComponent* NPM = M ? M->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
		FVector Cam;
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!W || !NPP || !NPM || !Cube || !AsdaOverheadOcclusion::CameraLocation(W, Cam))
		{
			C->Expect(false, TEXT("заслон: нет мира/плашек/куба/камеры"));
			MapPhase(C);
			return;
		}
		// Заслон — плита 50 × 1400 × 1400 ед. посередине между камерой и надписями, лицом к камере.
		const FVector Mid = (NPP->GetPlateWorldPoint() + NPM->GetPlateWorldPoint()) * 0.5f;
		const FVector Loc = Cam + (Mid - Cam) * 0.5f;
		const FRotator Rot(0.f, (Mid - Cam).Rotation().Yaw, 0.f);
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* A = W->SpawnActor<AStaticMeshActor>(Loc, Rot, SP);
		if (!A) { C->Expect(false, TEXT("заслон: не заспавнился")); MapPhase(C); return; }
		A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		A->GetStaticMeshComponent()->SetStaticMesh(Cube);
		A->GetStaticMeshComponent()->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		A->SetActorScale3D(FVector(0.5f, 14.f, 14.f));
		C->Blocker = A;
		Refresh(C);
		After(C, kSettleS, [](TSharedRef<FCtx> C2)
		{
			CheckAll(C2, TEXT("за заслоном"), false, false);
			Then(C2, [](TSharedRef<FCtx> C2b)
			{
			SetKey(0);   // контрольная пара: то же самое без исправления — видно сквозь заслон (прежнее поведение)
			After(C2b, 0.5f, [](TSharedRef<FCtx> C3)
			{
				CheckAll(C3, TEXT("за заслоном, ключ 0"), true, false);
				Then(C3, [](TSharedRef<FCtx> C3b)
				{
				SetKey(1);
				After(C3b, kSettleS, [](TSharedRef<FCtx> C4)
				{
					CheckAll(C4, TEXT("за заслоном, ключ снова 1"), false, false);
					Then(C4, [](TSharedRef<FCtx> C4b)
					{
					if (AActor* B = C4b->Blocker.Get()) { B->Destroy(); }
					C4b->Blocker = nullptr;
					Refresh(C4b);
					After(C4b, kSettleS, [](TSharedRef<FCtx> C5)
					{
						CheckAll(C5, TEXT("заслон убран"), true, false);
						Then(C5, &MapPhase);
					});
					});
				});
				});
			});
			});
		});
	}

	// ════ ★ 30.10.2026: ОТСТАВАНИЕ НАДПИСЕЙ ОТ КАМЕРЫ (`asda.Overhead.SelfTestLag`, баг владельца «names lag behind camera») ════
	//   ПУТЬ ИГРОКА. В начале кадра N+1 (`OnWorldTickStart`: кадр N уже нарисован Slate, кэш камеры — ещё камера кадра N) у каждой
	//   надписи берётся НАРИСОВАННЫЙ прямоугольник (`GetCachedGeometry`) и проекция её мировой точки камерой кадра N
	//   (`ProjectWorldLocationToScreen` читает именно этот кэш). Разность «рамка − проекция» при неподвижной камере —
	//   калибровка (постоянные экранные сдвиги окна); при движении камеры отклонение от неё = отставание надписи, в пикселях.
	//   Камера ведётся покадрово: поворот 5°/кадр, затем наезд 400↔1000 и наклон. Прогон дважды: ключ asda.Overhead.LateProject 0
	//   (контроль — прежний порт обязан отставать) и 1 (отклонение ≤ 1,5 px: порог дедупа слота 0,5 + округление вывески до пикселя).
	struct FLagItem
	{
		const TCHAR* Name = TEXT("");
		FVector2D Calib = FVector2D::ZeroVector;   // средняя «рамка − проекция» при неподвижной камере
		int32 CalN = 0;                            // кадров калибровки
		float MaxErr = 0.f;                        // наибольшее отклонение при движении, px
		int32 N = 0;                               // кадров замера при движении
		int32 Logged = 0;                          // выбросов, выведенных в журнал (не больше 6)
		FVector PrevPt = FVector::ZeroVector;      // мировая точка прошлого замера
		bool bPrev = false;                        // прошлый замер был
		int32 Moved = 0;                           // кадров, где сама точка сдвинулась (сущность шла/снап) — в отставание от камеры не входят
		int32 Offscreen = 0;                       // кадров, где точка за краем кадра (не судятся)
	};
	struct FLagRun
	{
		int32 Frame = 0;              // кадр прогона
		int32 KeyVal = 0;             // значение asda.Overhead.LateProject в этом прогоне
		FLagItem Items[4];            // плашка соседа, плашка моба-цели, вывеска, облачко
		FDelegateHandle H;            // подписка на начало кадра
		float Yaw0 = 0.f, Pitch0 = -30.f, Arm0 = 700.f;
	};
	constexpr int32 kLagCalibEnd = 12;     // кадры 1..12 — камера стоит (с 6-го копится калибровка)
	constexpr int32 kLagYawEnd = 72;       // 13..72 — поворот
	constexpr int32 kLagEnd = 132;         // 73..132 — наезд и наклон

	// Надпись: живой виджет и мировая точка последнего расчёта. false — надписи нет/не видна.
	static bool LagItemOf(const TSharedRef<FCtx>& C, int32 i, const UWidget*& OutW, FVector& OutPt)
	{
		UGameInstance* GI = GIOf(C);
		UWorld* W = C->W.Get();
		OutW = nullptr;
		if (i <= 1)
		{
			const AActor* A = (i == 0) ? Peer(C) : Mob(C);
			const UAsdaNameplateComponent* NP = A ? A->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
			if (!NP) { return false; }
			OutW = NP->DebugScreenWidget();
			OutPt = NP->GetPlateWorldPoint();
		}
		else if (i == 2)
		{
			UAsdaStallSignSubsystem* G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
			if (!G || !G->DebugSignShown(kOther) || !G->DebugSignWorldPoint(kOther, OutPt)) { return false; }
			OutW = G->DebugSignWidget(kOther);
		}
		else
		{
			UAsdaChatBalloonSubsystem* B = W ? W->GetSubsystem<UAsdaChatBalloonSubsystem>() : nullptr;
			FAsdaBalloonDebugInfo I;
			if (!B || !B->DebugGetBalloon(Peer(C), EAsdaTalkBox::White, I) || !I.bActive || !I.bOnScreen) { return false; }
			OutW = I.Widget;
			OutPt = I.WorldPt;
		}
		if (!OutW) { return false; }
		const ESlateVisibility V = OutW->GetVisibility();
		return V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden && OutW->GetCachedGeometry().GetLocalSize().X > 0.f;
	}

	static void LagFrame(TSharedRef<FCtx> C, TSharedRef<FLagRun> R);
	static void LagRun(TSharedRef<FCtx> C, int32 KeyVal);

	static void LagStart(TSharedRef<FCtx> C)
	{
		LagRun(C, 0);   // контроль: прежний порядок
	}

	static void LagRun(TSharedRef<FCtx> C, int32 KeyVal)
	{
		if (IConsoleVariable* LK = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Overhead.LateProject"))) { LK->Set(KeyVal, ECVF_SetByConsole); }
		Refresh(C);   // облачко живёт 5 с — показываем заново на каждый прогон
		TSharedRef<FLagRun> R = MakeShared<FLagRun>();
		R->KeyVal = KeyVal;
		R->Items[0].Name = TEXT("плашка соседа");
		R->Items[1].Name = TEXT("плашка моба-цели");
		R->Items[2].Name = TEXT("вывеска лавки");
		R->Items[3].Name = TEXT("облачко чата");
		UWorld* W = C->W.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		const AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (!PC || !Ch || !Ch->CameraArm) { C->Expect(false, TEXT("отставание: нет контроллера/персонажа/стрелы камеры")); Finish(C); return; }
		R->Yaw0 = PC->GetCameraYawUE();
		R->Pitch0 = PC->GetCameraPitchUE();
		R->Arm0 = Ch->CameraArm->TargetArmLength;
		TWeakPtr<FLagRun> WeakR = R;
		R->H = FWorldDelegates::OnWorldTickStart.AddLambda([C, WeakR](UWorld* TW, ELevelTick, float)
		{
			TSharedPtr<FLagRun> RR = WeakR.Pin();
			if (!RR.IsValid() || TW != C->W.Get()) { return; }
			LagFrame(C, RR.ToSharedRef());
		});
		// Держим прогон живым, пока он идёт (подписка — по слабой ссылке; таймер просто хранит сильную).
		After(C, 30.f, [R](TSharedRef<FCtx>) {});
	}

	static void LagFrame(TSharedRef<FCtx> C, TSharedRef<FLagRun> R)
	{
		UWorld* W = C->W.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (!PC || !Ch || !Ch->CameraArm) { return; }
		++R->Frame;
		// ── замер кадра N (нарисован) по камере кадра N (кэш камеры в этом кадре ещё не обновлён) ──
		for (int32 i = 0; i < 4; ++i)
		{
			const UWidget* Wd = nullptr;
			FVector Pt;
			FVector2D S;
			if (!LagItemOf(C, i, Wd, Pt) || !PC->ProjectWorldLocationToScreen(Pt, S, /*bPlayerViewportRelative=*/true)) { continue; }
			const FVector2D D = Wd->GetCachedGeometry().GetAbsolutePosition() - S;
			FLagItem& It = R->Items[i];
			// Сама сущность сдвинулась за кадр (шаг моба, снап к тайлу сервера) — это не камера: кадр не судим.
			const bool bPtMoved = It.bPrev && FVector::Dist(It.PrevPt, Pt) > 0.5f;
			It.PrevPt = Pt;
			It.bPrev = true;
			if (bPtMoved) { ++It.Moved; continue; }
			// Точка за краем кадра: плашка там по правилу оригинала клампится/скрывается, вывеска держит прежнюю позицию
			//   (`cullVisible2 == 0`) — это не отставание, такой кадр не судим.
			int32 VX = 0, VY = 0;
			PC->GetViewportSize(VX, VY);
			if (S.X < 0.f || S.Y < 0.f || S.X > VX || S.Y > VY) { ++It.Offscreen; continue; }
			if (R->Frame >= 6 && R->Frame <= kLagCalibEnd)
			{
				It.Calib = (It.Calib * It.CalN + D) / (It.CalN + 1);
				++It.CalN;
			}
			else if (R->Frame > kLagCalibEnd + 1 && It.CalN > 0)
			{
				const float E = (float)(D - It.Calib).Size();
				It.MaxErr = FMath::Max(It.MaxErr, E);
				++It.N;
				if (E > 1.5f && It.Logged < 6)
				{
					++It.Logged;
					UE_LOG(LogAsdaOccludeTest, Log, TEXT("НАДПИСИ-ОТСТАВАНИЕ   ключ %d кадр %d %s: отклонение %.1f px | рамка %s проекция %s калибровка %s | точка %s"),
						R->KeyVal, R->Frame, It.Name, E, *Wd->GetCachedGeometry().GetAbsolutePosition().ToString(), *S.ToString(),
						*It.Calib.ToString(), *Pt.ToCompactString());
				}
			}
		}
		// ── камера на следующий кадр ──
		const int32 F = R->Frame;
		if (F > kLagCalibEnd && F <= kLagYawEnd)
		{
			PC->SetOrbitAnglesForBench(R->Yaw0 + 5.f * (F - kLagCalibEnd), R->Pitch0);
		}
		else if (F > kLagYawEnd && F <= kLagEnd)
		{
			const float T = (F - kLagYawEnd) / 6.f;
			PC->SetOrbitAnglesForBench(R->Yaw0, FMath::Clamp(R->Pitch0 + 12.f * FMath::Sin(T), -75.f, -10.f));
			Ch->CameraArm->TargetArmLength = 700.f + 300.f * FMath::Sin(T * 1.3f);
		}
		if (F < kLagEnd) { return; }
		// ── итог прогона ──
		FWorldDelegates::OnWorldTickStart.Remove(R->H);
		PC->SetOrbitAnglesForBench(R->Yaw0, R->Pitch0);
		Ch->CameraArm->TargetArmLength = R->Arm0;
		for (const FLagItem& It : R->Items)
		{
			UE_LOG(LogAsdaOccludeTest, Log, TEXT("НАДПИСИ-ОТСТАВАНИЕ ключ %d: %s — калибровка %d кадров, замер %d кадров, наибольшее отклонение %.2f px (не судятся: сдвиг самой точки %d, точка за кадром %d)"),
				R->KeyVal, It.Name, It.CalN, It.N, It.MaxErr, It.Moved, It.Offscreen);
		}
		if (R->KeyVal == 0)
		{
			// Контроль: прежний порт отставал — плашки (2 кадра) и облачко (1 кадр) обязаны разойтись с камерой заметно.
			C->Expect(R->Items[0].N > 20 && R->Items[0].MaxErr > 5.f,
				FString::Printf(TEXT("контроль, ключ 0: плашка соседа отстаёт от камеры (наибольшее %.1f px, порог > 5)"), R->Items[0].MaxErr));
			C->Expect(R->Items[3].N > 20 && R->Items[3].MaxErr > 3.f,
				FString::Printf(TEXT("контроль, ключ 0: облачко отстаёт от камеры (наибольшее %.1f px, порог > 3)"), R->Items[3].MaxErr));
			After(C, 0.3f, [](TSharedRef<FCtx> C2) { LagRun(C2, 1); });
			return;
		}
		for (const FLagItem& It : R->Items)
		{
			C->Expect(It.N > 20 && It.MaxErr <= 1.5f,
				FString::Printf(TEXT("ключ 1: %s идёт за камерой кадр в кадр (наибольшее отклонение %.2f px ≤ 1,5; кадров %d)"), It.Name, It.MaxErr, It.N));
		}
		Finish(C);
	}

	// ── 1) на виду ──
	static void Arrange(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = GIOf(C);
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaStallSignSubsystem* G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
		AActor* P = Peer(C);
		AActor* M = Mob(C);
		if (!P || !M)
		{
			if (++C->Polls > 40) { C->Expect(false, FString::Printf(TEXT("подготовка: сосед=%d моб=%d не появились за 8 с"), P ? 1 : 0, M ? 1 : 0)); Finish(C); return; }
			After(C, 0.2f, &Arrange);
			return;
		}
		if (!Net || !G) { C->Expect(false, TEXT("подготовка: нет подсистем")); Finish(C); return; }
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
		{
			C->BaseYaw = PC->GetCameraYawUE();
			C->BasePitch = PC->GetCameraPitchUE();
		}
		PlaceInFront(W, P, 650.f, -120.f);
		PlaceInFront(W, M, 650.f, 140.f);
		// Плашка моба (имя + HP, окно 1600) у оригинала и у нас — только у ВЫБРАННОЙ цели или под курсором: выбираем моба целью
		//   (тот же вход, что клик ЛКМ) — ровно случай со снимка владельца (цель «Worm» за крышей лавки).
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController())) { PC->SetTarget(M); }
		if (UAsdaNameplateComponent* NP = P->FindComponentByClass<UAsdaNameplateComponent>())
		{
			NP->bAlwaysShowName = true;
			if (NP->DisplayName.IsEmpty()) { NP->DisplayName = TEXT("Заслон"); }
			NP->RefreshNameplate();
		}
		AsdaTestNet::DebugFeedPacket(Net, 6304, Make6304(1, 900000 + kOther, TEXT("Магазин 'Заслон'")));
		G->PumpPending();
		Refresh(C);
		C->Expect(true, TEXT("подготовка: свой, сосед (57) с лавкой и облачком, моб (29201) в мире"));
		After(C, kSettleS, [](TSharedRef<FCtx> C2)
		{
			CheckAll(C2, TEXT("на виду"), true, false);
			Then(C2, C2->bLag ? &LagStart : &BlockerPhase);
		});
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaStallSignSubsystem* G = GI ? GI->GetSubsystem<UAsdaStallSignSubsystem>() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!G || !S || !Net || !WS) { C->Expect(false, TEXT("нет подсистем")); Finish(C); return; }
		if (IConsoleVariable* K = Key()) { C->SavedKey = K->GetInt(); }
		if (IConsoleVariable* LK = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Overhead.LateProject"))) { C->SavedLateKey = LK->GetInt(); }
		SetKey(1);
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine) { GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3")); }
		// ★ R4-хвосты-c (15.11.2026): свой — ВСЕГДА на свою клетку (та же точка, что вход 4003; так же делает `asda.ViewRange.SelfTest`). Прежде, если свой уже
		//   стоял от прошлого самотеста пакета, сцена бралась там, где он остался: поиск стены карты и проверка рельефа зависели от соседа.
		WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		C->SelfScope.Capture(Net, kCmd);
		AsdaTestNet::SelectedCharId(Net) = kSelfCharId;
		AsdaTestNet::SetSelfUserNumForDebug(Net, kSelfUserNum);
		AsdaTestNet::SelfLevel(Net) = 30;
		S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		G->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>())
		{
			C->Gen = Pet->BeginSendCapture([](int32, const TArray<uint8>&, bool, TCHAR) {}, 0) ? 1u : 0u;   // в сеть не уходит ничего
		}
		const AActor* Self = WS->LocalPlayerActor.Get();
		const FVector2D G0 = Self ? AsdaCoord::UeToGame2D(Self->GetActorLocation()) : FVector2D(25600.f, 25600.f);
		WS->DebugSpawnFakePeer(kOther, TEXT("Заслон"), G0 + FVector2D(200.f, 0.f));
		const FIntPoint T((int32)(G0.X / 100.f) + 2, (int32)(G0.Y / 100.f) + 2);
		AsdaTestNet::DebugFeedPacket(Net, 4013, AsdaTestPackets::MobSpawn4013(kMobRid, kMobType, T, /*State*/1, 100));
		After(C, 1.0f, &Arrange);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaOverheadSelfTestOcclude(
	TEXT("asda.Overhead.SelfTestOcclude"),
	TEXT("asda.Overhead.SelfTestOcclude [delay=N] [shot] [shotdir=<папка>] [quit] — ОФЛАЙН: надписи над головой (плашки соседа и моба, вывеска лавки, ")
	TEXT("облачко чата) видны на виду и скрыты за заслоном и за геометрией карты; контрольные пары с asda.Overhead.Occlude 0; цена лучей"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaOccludeTestLocal::kCmd, KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;
		TSharedRef<AsdaOccludeTestLocal::FCtx> C = MakeShared<AsdaOccludeTestLocal::FCtx>();
		C->W = W;
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { C->bQuit = true; }
			else if (Arg.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { C->bShot = true; }
			else if (Arg.StartsWith(TEXT("shotdir="))) { C->ShotDir = Arg.Mid(8).Replace(TEXT("\\"), TEXT("/")); C->bShot = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		if (Delay <= 0.f) { AsdaOccludeTestLocal::Run(C); return; }
		AsdaOccludeTestLocal::After(C, Delay, [](TSharedRef<AsdaOccludeTestLocal::FCtx> C2)
		{
			if (AsdaSelfTestKit::RefuseDeferred(C2->W.Get(), AsdaOccludeTestLocal::kCmd, C2->bQuit)) { return; }
			AsdaOccludeTestLocal::Run(C2);
		});
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaOverheadSelfTestLag(
	TEXT("asda.Overhead.SelfTestLag"),
	TEXT("asda.Overhead.SelfTestLag [delay=N] [quit] — ОФЛАЙН: надписи над головой (плашки соседа и моба-цели, вывеска лавки, облачко) ")
	TEXT("при повороте/наезде камеры совпадают с проекцией своей точки камерой того же кадра (нарисованная геометрия); контроль — ключ asda.Overhead.LateProject 0"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Overhead.SelfTestLag"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;
		TSharedRef<AsdaOccludeTestLocal::FCtx> C = MakeShared<AsdaOccludeTestLocal::FCtx>();
		C->W = W;
		C->bLag = true;
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { C->bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		if (Delay <= 0.f) { AsdaOccludeTestLocal::Run(C); return; }
		AsdaOccludeTestLocal::After(C, Delay, [](TSharedRef<AsdaOccludeTestLocal::FCtx> C2)
		{
			if (AsdaSelfTestKit::RefuseDeferred(C2->W.Get(), TEXT("asda.Overhead.SelfTestLag"), C2->bQuit)) { return; }
			AsdaOccludeTestLocal::Run(C2);
		});
	}));

// Дамп: каждая плашка мира — принятое «перекрыто», показанная непрозрачность и что задевает луч сейчас.
static FAutoConsoleCommandWithWorld GAsdaOverheadOccludeProbe(
	TEXT("asda.Overhead.OccludeProbe"),
	TEXT("asda.Overhead.OccludeProbe — дамп перекрытия надписей над головой: по каждой плашке с замером — состояние и что задевает луч"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		int32 N = 0, Occ = 0;
		for (TObjectIterator<UAsdaNameplateComponent> It; It; ++It)
		{
			if (It->GetWorld() != W || !It->DebugOccState().bInit) { continue; }
			FAsdaOccludeHit H;
			AsdaOverheadOcclusion::TraceNow(W, It->GetPlateWorldPoint(), It->GetOwner(), &H);
			const FAsdaOccludeState& S = It->DebugOccState();
			++N; Occ += S.bOccluded ? 1 : 0;
			UE_LOG(LogAsdaOccludeTest, Log, TEXT("ПЕРЕКРЫТИЕ %s «%s»: принято=%d α=%.2f | луч сейчас=%d %s %.0f (отброшено %d)"),
				*GetNameSafe(It->GetOwner()), *It->DisplayName, S.bOccluded ? 1 : 0, S.Alpha, H.bOccluded ? 1 : 0, *H.What, H.Distance, H.Skipped);
		}
		int64 Tr = 0, Qr = 0; uint64 Cy = 0;
		AsdaOverheadOcclusion::GetStats(Tr, Qr, Cy);
		UE_LOG(LogAsdaOccludeTest, Log, TEXT("ПЕРЕКРЫТИЕ итог: плашек с замером %d, перекрыто %d; лучей с обнуления %lld, запросов %lld, %.3f мс"),
			N, Occ, Tr, Qr, FPlatformTime::ToMilliseconds64(Cy));
	}));

// Перепись коллизий статик-мешей мира: какие каналы блокирует геометрия карты (выбор канала луча — по данным, не наугад).
static FAutoConsoleCommandWithWorld GAsdaOverheadCollisionCensus(
	TEXT("asda.Overhead.CollisionCensus"),
	TEXT("asda.Overhead.CollisionCensus — перепись статик-мешей мира: режим коллизии, тип объекта, ответ каналам Visibility/Camera, флаг трассировки меша"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		struct FRow { int32 N = 0; FString Example; };
		TMap<FString, FRow> Rows;
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			UStaticMeshComponent* C = *It;
			if (!C || C->GetWorld() != W || !C->IsRegistered() || !C->GetStaticMesh()) { continue; }
			const UBodySetup* BS = C->GetStaticMesh()->GetBodySetup();
			const FString Key = FString::Printf(TEXT("режим=%d тип=%d Vis=%d Cam=%d WS=%d трасс=%d простая=%d мобильн=%d %s"),
				(int32)C->GetCollisionEnabled(), (int32)C->GetCollisionObjectType(),
				(int32)C->GetCollisionResponseToChannel(ECC_Visibility), (int32)C->GetCollisionResponseToChannel(ECC_Camera),
				(int32)C->GetCollisionResponseToChannel(ECC_WorldStatic), BS ? (int32)BS->CollisionTraceFlag : -1,
				BS ? BS->AggGeom.GetElementCount() : -1, (int32)C->Mobility, *C->GetCollisionProfileName().ToString());
			FRow& R = Rows.FindOrAdd(Key);
			if (R.N++ < 3) { R.Example += C->GetStaticMesh()->GetName() + TEXT(" "); }
		}
		for (const TPair<FString, FRow>& P : Rows)
		{
			UE_LOG(LogAsdaOccludeTest, Log, TEXT("ПЕРЕПИСЬ %5d × %s | %s"), P.Value.N, *P.Key, *P.Value.Example);
		}
	}));

// Микро-замер цены луча: по 200 лучей к точкам «на виду» над своим персонажем на разных удалениях и направлениях,
//   полный луч (как в бою) и тот же луч без рельефа (`SM_terrain` исключён) — какая часть цены приходится на рельеф.
static FAutoConsoleCommandWithWorld GAsdaOverheadOccludeBench(
	TEXT("asda.Overhead.OccludeBench"),
	TEXT("asda.Overhead.OccludeBench — цена луча перекрытия: 200 лучей к 8 точкам вокруг своего персонажа, с рельефом и без"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		APawn* Self = PC ? PC->GetPawn() : nullptr;
		FVector Cam;
		if (!Self || !AsdaOverheadOcclusion::CameraLocation(W, Cam)) { return; }
		UStaticMeshComponent* Terrain = AsdaGround::FindTerrainComponent(W);
		for (int32 k = 0; k < 8; ++k)
		{
			const float Dist = (k % 2) ? 2500.f : 900.f;
			const FVector P = Self->GetActorLocation() + FRotator(0.f, 45.f * k, 0.f).Vector() * Dist + FVector(0, 0, 200.f);
			FAsdaOccludeHit H;
			AsdaOverheadOcclusion::TraceNow(W, P, nullptr, &H);
			const uint64 T0 = FPlatformTime::Cycles64();
			for (int32 i = 0; i < 200; ++i) { AsdaOverheadOcclusion::TraceNow(W, P, nullptr); }
			const double UsFull = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T0) * 1000.0 / 200.0;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaOccludeBench), true);
			Q.AddIgnoredActor(Self);
			if (Terrain) { Q.AddIgnoredComponent(Terrain); }
			FHitResult Hn;
			const uint64 T1 = FPlatformTime::Cycles64();
			for (int32 i = 0; i < 200; ++i) { W->LineTraceSingleByChannel(Hn, Cam, P, ECC_Visibility, Q); }
			const double UsNoTer = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T1) * 1000.0 / 200.0;
			FCollisionQueryParams Qs(SCENE_QUERY_STAT(AsdaOccludeBenchS), false);
			Qs.AddIgnoredActor(Self);
			const uint64 T2 = FPlatformTime::Cycles64();
			for (int32 i = 0; i < 200; ++i) { W->LineTraceSingleByChannel(Hn, Cam, P, ECC_Visibility, Qs); }
			const double UsSimple = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - T2) * 1000.0 / 200.0;
			UE_LOG(LogAsdaOccludeTest, Log, TEXT("ЛУЧ-ЦЕНА точка %d (%.0f ед., курс %d°): полный %.1f мкс | без рельефа %.1f мкс | простой %.1f мкс | перекрыто=%d %s %.0f"),
				k, Dist, 45 * k, UsFull, UsNoTer, UsSimple, H.bOccluded ? 1 : 0, *H.What, H.Distance);
		}
	}));

#endif // !UE_BUILD_SHIPPING
