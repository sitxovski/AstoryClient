// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВЫБОРА ЦЕЛЕЙ ПРИ GM-СКРЫТИИ (27.09.2026, отчёт `Reports/owner_0927/pick_hidden_filter.md`). [ПОРТ]
//
// ★ ЧТО ПРОВЕРЯЕТ. Две правки по эталону:
//   1) площадной сбор персонажей `UAsdaWorldSubsystem::PickPlayerUserIdxsInGameBox` пропускает GM-скрытого
//      (`charManager_collectTargetsInRect@0x9DB560`, entity_char.c:9657 `(sub_93B218(i) & 1) != 0 → continue`);
//   2) луч под курсором (наведение/клик/aim-assist) идёт СКВОЗЬ неотрисованного персонажа
//      (`pickEntityType32ByRayObb@0x993C30` / `Input_PickEntityHover@0x993DD0` берут только `RenderCtx` с
//      включённым флагом отрисовки).
//   Групповой сбор `PickPartyUserIdxsInGameBox` эталон НЕ фильтрует (entity_char.c:9704-9749) — проверяется, что
//   он скрытого согруппника по-прежнему берёт.
//
// ★ КАК. Офлайн: `Asda.Load.CurrentMap 3` + `Asda.World.FakeSelfSpawn 256 256` (как у самотестов 4007/4008). Соседи —
//   штатный `DebugSpawnFakePeer` (путь снапшота 4005). Скрытие/показ — ШТАТНЫЙ приёмник 4025 (actionType 300/301)
//   через `UAsdaNetSubsystem::DebugFeedPacket`, никаких прямых вызовов в обход (закон diagnostic-must-use-real-path).
//   Сосед U стоит перед камерой, сосед V — на том же луче камеры за ним.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs): «сырой» луч движка `GetHitResultAtScreenPosition`
//   без фильтра обязан попадать в скрытого U (коллизия у спрятанного актора есть — иначе проверка слепа), а луч
//   порта — нет; площадной сбор до скрытия обязан содержать U.
//
// Консоль: `asda.Target.SelfTestPickHidden [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-ПИК», итог — «САМОТЕСТ-ПИК ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // RefuseDebugWhenOnline — офлайн-гейт самотестов   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaCVarSave.h"                      // ключ на время прогона с возвратом (asda.Skill.TargetRelation)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "World/AsdaCoord.h"                   // перевод UE ↔ игра
#include "World/AsdaPlayerController.h"        // TraceScreenSkippingUnrenderedChars
#include "World/AsdaWorldSubsystem.h"          // соседи-заглушки, площадной и групповой сбор
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPickHiddenTest, Log, All);

namespace AsdaPickHiddenTestLocal
{
	// userNum соседей-заглушек (заведомо вне диапазона живой сессии офлайн-прогона).
	constexpr int32 kPeerFront = 91;           // U — стоит перед камерой, его прячем
	constexpr int32 kPeerBehind = 92;          // V — на том же луче камеры за U
	constexpr float kBoxHalf = 60.f;           // полуразмер игрового бокса сбора вокруг соседа

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		FTimerHandle Tick;
		int32 Phase = 0;
		double PhaseStartS = 0.0;
		double DeadlineS = 0.0;
		int32 Ok = 0, Fail = 0;
		bool bQuit = false;
		FVector2D ScreenU = FVector2D::ZeroVector;   // экранная точка центра U

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaPickHiddenTest, Log, TEXT("САМОТЕСТ-ПИК [ok]   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaPickHiddenTest, Error, TEXT("САМОТЕСТ-ПИК [FAIL] %s"), *What); }
		}
		UGameInstance* GI() const { return World.IsValid() ? World->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* Ws() const { return GI() ? GI()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaNetSubsystem* Net() const { return GI() ? GI()->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
		AAsdaPlayerController* Pc() const { return World.IsValid() ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr; }
		void NextPhase() { ++Phase; PhaseStartS = FPlatformTime::Seconds(); }
	};

	// Подать 4025 (13 б: userIdx u16@0, actionType u16@2, шлем u8@4, X f32@5, Z f32@9) штатному приёмнику.
	static void Feed4025(UAsdaNetSubsystem* Net, int32 UserIdx, int32 Action)
	{
		TArray<uint8> B; B.SetNumZeroed(13);
		B[0] = (uint8)(UserIdx & 0xFF); B[1] = (uint8)((UserIdx >> 8) & 0xFF);
		B[2] = (uint8)(Action & 0xFF);  B[3] = (uint8)((Action >> 8) & 0xFF);
		Net->DebugFeedPacket(4025, B);
	}

	// Есть ли userIdx в выборке.
	static bool Has(const TArray<int32>& A, int32 V) { return A.Contains(V); }

	// Имя актора попадания для лога.
	static FString HitName(const FHitResult& H) { return H.GetActor() ? H.GetActor()->GetName() : FString(TEXT("—")); }
}

static void AsdaRunPickHiddenSelfTest(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaPickHiddenTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
	if (!W) { AsdaSelfTestKit::Refused(nullptr, TEXT("asda.Target.SelfTestPickHidden"), KitArgs.bQuit, TEXT("нет мира")); return; }
	if (AsdaSelfTestKit::RefuseStart(W->GetGameInstance(), TEXT("asda.Target.SelfTestPickHidden"), KitArgs)) { return; }

	TSharedPtr<FState> S = MakeShared<FState>();
	S->World = W;
	float Delay = 6.f;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { S->bQuit = true; }
	}
	Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
	S->PhaseStartS = FPlatformTime::Seconds() + Delay;
	S->DeadlineS = S->PhaseStartS + 60.0;
	UE_LOG(LogAsdaPickHiddenTest, Log, TEXT("САМОТЕСТ-ПИК старт (delay=%.1f)"), Delay);
	// ★ 11.11.2026 (D4, разбор красных эталона): с 26.10.2026 площадной сбор берёт персонажей только по PvP-отношению
	//   (`AsdaWorldSubsystem_Peers.cpp`, режим [92] = 0 — никого; ключ `asda.Skill.TargetRelation`). Этот самотест проверяет
	//   ДРУГОЙ фильтр той же строки эталона — GM-скрытых — и PvP-режим не ставит, поэтому контроль «видимого берёт» стал
	//   красным (выборка 0). На время прогона фильтр отношений выключается; у него свой самотест.
	AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Target.SelfTestPickHidden"));
	AsdaCVarSave::SetForTest(TEXT("asda.Skill.TargetRelation"), 0);

	// Офлайн-спавн своего персонажа, если его ещё нет (штатные крюки).
	TWeakObjectPtr<UWorld> WW(W);
	const bool bQuit = S->bQuit;   // выйти при отказе (флаг `quit`)
	// ★ Р-2 (10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим таймером;
	//   при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
	// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
	TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Target.SelfTestPickHidden"), bQuit);   // общий флаг отказа всех шагов прогона
	auto GateRefused = [Gate, WW]() { return !Gate->Pass(WW.Get()); };   // true — прогон отменён; проверка один раз, первым сработавшим шагом
	{
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(W, [WW, GateRefused]()
		{
			UWorld* Wd = WW.Get();
			if (Wd && GateRefused()) { return; }   // Р-2: офлайн повторно — на отложенном старте
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, Delay * 0.5f), false);
	}

	// Держим состояние живым, пока крутится таймер (лямбда хранит сильную ссылку до конца прогона).
	TSharedPtr<FState> Keep = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(W, [Keep, WW, GateRefused]()
	{
		FState* St = Keep.Get(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		const double Now = FPlatformTime::Seconds();
		if (Now < St->PhaseStartS) { return; }
		if (GateRefused()) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }   // Р-2: офлайн повторно — на отложенном старте
		UAsdaWorldSubsystem* Ws = St->Ws();
		UAsdaNetSubsystem* Net = St->Net();
		AAsdaPlayerController* Pc = St->Pc();
		auto Finish = [St, Wd]()
		{
			UE_LOG(LogAsdaPickHiddenTest, Log, TEXT("САМОТЕСТ-ПИК ИТОГ: ok=%d fail=%d"), St->Ok, St->Fail);
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaCVarSave::RestoreAll(TEXT("asda.Target.SelfTestPickHidden"));   // вернуть asda.Skill.TargetRelation
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
		};
		if (Now > St->DeadlineS)
		{
			St->Expect(false, FString::Printf(TEXT("прогон не уложился в 60 с (фаза %d)"), St->Phase));
			Finish();
			return;
		}
		if (!Ws || !Net || !Pc) { return; }

		switch (St->Phase)
		{
		case 0:   // ждём свой павн, ставим U перед камерой
		{
			if (!Pc || !Pc->GetPawn() || !Pc->PlayerCameraManager) { return; }
			const FVector Self = Pc->GetPawn()->GetActorLocation();
			FVector Fwd = Pc->PlayerCameraManager->GetCameraRotation().Vector(); Fwd.Z = 0.f;
			if (!Fwd.Normalize()) { Fwd = FVector(1, 0, 0); }
			// Вбок от оси камеры: камера смотрит на свой павн сзади, и луч к точке прямо впереди упирался бы
			//   в СВОЙ павн (первый прогон: все лучи попали в AsdaPlayerCharacter_0 = себя).
			const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
			const FVector UeU = Self + Fwd * 400.f + Right * 250.f;
			Ws->DebugSpawnFakePeer(kPeerFront, TEXT("ПикU"), AsdaCoord::UeToGame2D(UeU));
			St->NextPhase();
			return;
		}
		case 1:   // U появился → ставим V на луче камеры за ним
		{
			AActor* U = Ws->ActorByUserIdx(kPeerFront);
			if (!U || Now - St->PhaseStartS < 1.0) { return; }
			const FVector C = Pc->PlayerCameraManager->GetCameraLocation();
			const FVector P = U->GetActorLocation();
			const FVector D = (P - C).GetSafeNormal();
			Ws->DebugSpawnFakePeer(kPeerBehind, TEXT("ПикV"), AsdaCoord::UeToGame2D(P + D * 150.f));
			St->NextPhase();
			return;
		}
		case 2:   // оба на месте (фейд-ин дожит) → проверки ВИДИМОГО U, затем скрываем его штатным 4025/300
		{
			AActor* U = Ws->ActorByUserIdx(kPeerFront);
			AActor* V = Ws->ActorByUserIdx(kPeerBehind);
			if (!U || !V || Now - St->PhaseStartS < 1.5) { return; }
			FVector2D Scr;
			const bool bProj = Pc->ProjectWorldLocationToScreen(U->GetActorLocation(), Scr);
			St->Expect(bProj, FString::Printf(TEXT("центр U проецируется на экран (%.0f, %.0f)"), Scr.X, Scr.Y));
			// Точка экрана, где U заслоняет V: сырой луч попадает в U, тот же луч без U — в V. Ищем её перебором
			//   вокруг проекции U (оба соседа приземлены на рельеф, поэтому центр U не обязан лежать на линии V).
			//   Не нашлась — остаётся центр U, и проверка «берёт V» честно провалится.
			St->ScreenU = Scr;
			{
				bool bFound = false;
				FCollisionQueryParams NoU(SCENE_QUERY_STAT(AsdaPickHiddenProbe), true);
				NoU.AddIgnoredActor(U);
				for (int32 dy = -60; dy <= 60 && !bFound; dy += 4)
				{
					for (int32 dx = -40; dx <= 40 && !bFound; dx += 4)
					{
						const FVector2D Pt(Scr.X + dx, Scr.Y + dy);
						FHitResult H1, H2;
						if (Pc->GetHitResultAtScreenPosition(Pt, ECC_Visibility, true, H1) && H1.GetActor() == U
							&& Pc->GetHitResultAtScreenPosition(Pt, ECC_Visibility, NoU, H2) && H2.GetActor() == V)
						{
							St->ScreenU = Pt; bFound = true;
						}
					}
				}
				St->Expect(bFound, FString::Printf(TEXT("расстановка: есть точка, где U заслоняет V (%.0f, %.0f)"), St->ScreenU.X, St->ScreenU.Y));
			}
			Scr = St->ScreenU;
			UE_LOG(LogAsdaPickHiddenTest, Log, TEXT("САМОТЕСТ-ПИК акторы: свой=%s U=%s V=%s"),
				Pc->GetPawn() ? *Pc->GetPawn()->GetName() : TEXT("—"), *U->GetName(), *V->GetName());
			FHitResult Raw, Our;
			Pc->GetHitResultAtScreenPosition(Scr, ECC_Visibility, true, Raw);
			const bool bOur = Pc->TraceScreenSkippingUnrenderedChars(Scr, Our);
			St->Expect(Raw.GetActor() == U, FString::Printf(TEXT("видимый U: сырой луч движка попадает в U (попал: %s)"), *HitName(Raw)));
			St->Expect(bOur && Our.GetActor() == U, FString::Printf(TEXT("видимый U: луч порта попадает в U — выбирается кликом (попал: %s)"), *HitName(Our)));
			const FVector2D GU = AsdaCoord::UeToGame2D(U->GetActorLocation());
			TArray<int32> Out;
			Ws->PickPlayerUserIdxsInGameBox(GU, kBoxHalf, Out, 20, -1);
			St->Expect(Has(Out, kPeerFront), FString::Printf(TEXT("видимый U: площадной сбор его берёт (контроль, выборка %d)"), Out.Num()));
			Feed4025(Net, kPeerFront, 300);
			St->NextPhase();
			return;
		}
		case 3:   // U скрыт → луч проходит сквозь, площадной сбор его не берёт, групповой — берёт (эталон)
		{
			if (Now - St->PhaseStartS < 0.3) { return; }
			AActor* U = Ws->ActorByUserIdx(kPeerFront);
			AActor* V = Ws->ActorByUserIdx(kPeerBehind);
			St->Expect(U && U->IsHidden(), TEXT("4025/300: актор U спрятан штатным приёмником"));
			FHitResult Raw, Our;
			Pc->GetHitResultAtScreenPosition(St->ScreenU, ECC_Visibility, true, Raw);
			Pc->TraceScreenSkippingUnrenderedChars(St->ScreenU, Our);
			St->Expect(U && Raw.GetActor() == U, FString::Printf(TEXT("скрытый U: сырой луч движка ВСЁ ЕЩЁ попадает в U — дефект до правки воспроизводим (контроль; попал: %s)"), *HitName(Raw)));
			St->Expect(U && Our.GetActor() != U, FString::Printf(TEXT("скрытый U: луч порта сквозь него (попал: %s)"), *HitName(Our)));
			St->Expect(V && Our.GetActor() == V, FString::Printf(TEXT("скрытый U: луч порта берёт стоящего ЗА ним V (попал: %s)"), *HitName(Our)));
			if (U)
			{
				const FVector2D GU = AsdaCoord::UeToGame2D(U->GetActorLocation());
				TArray<int32> Out;
				Ws->PickPlayerUserIdxsInGameBox(GU, kBoxHalf, Out, 20, -1);
				St->Expect(!Has(Out, kPeerFront), FString::Printf(TEXT("скрытый U: площадной сбор его НЕ берёт (эталон 9657; выборка %d)"), Out.Num()));
				Ws->SetPartyRoster(TArray<int32>{ kPeerFront });
				TArray<int32> Party;
				Ws->PickPartyUserIdxsInGameBox(GU, kBoxHalf, Party, 20, -1);
				St->Expect(Has(Party, kPeerFront), TEXT("скрытый согруппник U: групповой сбор его БЕРЁТ (эталон 9704-9749 без фильтра)"));
				Ws->SetPartyRoster(TArray<int32>());   // офлайн ростер пуст — возвращаем как было
			}
			if (V)
			{
				TArray<int32> OutV;
				Ws->PickPlayerUserIdxsInGameBox(AsdaCoord::UeToGame2D(V->GetActorLocation()), kBoxHalf, OutV, 20, -1);
				St->Expect(Has(OutV, kPeerBehind), TEXT("видимый V: площадной сбор его берёт (контроль: фильтр режет только скрытых)"));
			}
			Feed4025(Net, kPeerFront, 301);
			St->NextPhase();
			return;
		}
		case 4:   // U снова виден → всё вернулось
		{
			if (Now - St->PhaseStartS < 0.3) { return; }
			AActor* U = Ws->ActorByUserIdx(kPeerFront);
			St->Expect(U && !U->IsHidden(), TEXT("4025/301: актор U показан"));
			FHitResult Our;
			Pc->TraceScreenSkippingUnrenderedChars(St->ScreenU, Our);
			St->Expect(U && Our.GetActor() == U, FString::Printf(TEXT("показанный U: луч порта снова берёт U (попал: %s)"), *HitName(Our)));
			if (U)
			{
				TArray<int32> Out;
				Ws->PickPlayerUserIdxsInGameBox(AsdaCoord::UeToGame2D(U->GetActorLocation()), kBoxHalf, Out, 20, -1);
				St->Expect(Has(Out, kPeerFront), TEXT("показанный U: площадной сбор снова его берёт"));
			}
			Finish();
			return;
		}
		default: return;
		}
	}), 0.1f, true, 0.1f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPickHidden(
	TEXT("asda.Target.SelfTestPickHidden"),
	TEXT("Самотест выбора целей при GM-скрытии: луч под курсором идёт сквозь скрытого персонажа (клик/наведение), "
	     "площадной сбор персонажей его пропускает, групповой сбор согруппников — нет (как эталон). Скрытие — штатным 4025. "
	     "Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaRunPickHiddenSelfTest(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
