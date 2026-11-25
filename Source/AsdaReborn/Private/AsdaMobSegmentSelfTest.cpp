// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОТРЕЗКА СЕРВЕРА У МОБА (07.11.2026, баг «монстры телепортируются к игроку», `Reports/mob_stun_teleport/`). [ИСПРАВЛЕНИЕ]
//
// ★ ЗАЧЕМ. Попадание снимает у актора траекторию, а NpcServer ведёт отрезок дальше (на флинч не останавливается, под станом замирает
//   и продолжает без нового 4015). Прежде актор стоял и его ставили рывком: доводчик в конце клипа, снап ≥ 3 клеток на 4015 kind=3,
//   «призрак» по 4017. Самотест прогоняет живые последовательности пакетов и меряет наибольший скачок видимой позиции за кадр:
//   • бег → попадание: добег к концу отрезка за время сервера, без скачка;
//   • бег → попадание + стан 4017 → живая клетка впереди → снятие стана: стоит под станом, потом добегает, без скачка;
//   • бег пришёл в клипе попадания: из буфера моб бежит со скоростью по остатку отрезка;
//   • «стой» в клипе попадания: отрезок снят, бега нет;
//   • удар при расхождении ≥ 3 клеток: короткий проход;
//   • контрольные пары с ключами 0: дефект виден (скачок ≥ порога).
//
// ★ КАК. Офлайн: пакеты — `UAsdaNetSubsystem::DebugFeedPacket` (штатный `HandlePacket`), попадание — `NetHitFlinch` (его зовут обе точки показа
//   урона). Консоль: `asda.Mob.SelfTestSegment [delay=N] [quit]`. Строки — «САМОТЕСТ-ОТРЕЗОК».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                         // запись полей тел пакетов
#include "AsdaCVarSave.h"                      // ключи на время прогона
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, итог, выход
#include "World/AsdaPlayerController.h"        // свой контроллер
#include "World/AsdaPlayerCharacter.h"         // свой персонаж
#include "World/AsdaWorldSubsystem.h"          // FindNetMobByRid
#include "World/AsdaCoord.h"                   // игра ↔ UE, клетки
#include "Npc/AsdaMobActor.h"                  // моб
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobSegTest, Log, All);

namespace AsdaMobSegTestLocal
{
	constexpr int32 kMobType = 1;         // тип моба (волк alpen)
	constexpr int32 kSpdDenom = 250;      // 100/250 = 0.4 юн/мс (бег сервера)
	constexpr float kJumpTeleport = 280.f;   // скачок за кадр, который глаз видит как телепорт (почти 3 клетки)
	constexpr int32 kStun = 3;            // состояние «оглушение»
	constexpr int32 kStunSkill = 909;     // «Землетрясение»: первым слотом вешает оглушение (живой журнал 30.10)

	// Шаг сценария.
	struct FStep
	{
		FString Name;
		float MinSec = 0.f, TimeoutSec = 10.f;
		TFunction<void()> Start;
		TFunction<bool()> Poll;
		TFunction<void()> Check;
	};

	// Общее состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0;
		bool bStepStarted = false;
		double StepStartS = 0.0;
		FTimerHandle Tick;
		bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ TEXT("asda.Mob.SelfTestSegment"), false };
		int32 Ok = 0, Fail = 0;
		FIntPoint HomeTile = FIntPoint::ZeroValue;
		FVector2D Home = FVector2D::ZeroVector;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TArray<int32> Rids;                 // мобы прогона
		int32 NextRid = 30951;
		int32 Rid = -1;                     // моб сценария
		int32 Phase = 0;                    // этап внутри сценария
		double T0 = 0.0;                    // момент пакета бега
		FVector2D Last = FVector2D(-1.f, -1.f);   // прошлая видимая позиция
		float MaxJump = 0.f;                // наибольший скачок за замер
		FVector2D StunPos = FVector2D::ZeroVector;
		float StunDrift = 0.f;              // сдвиг под станом
		double ArriveS = -1.0;              // момент прибытия от T0
		bool bStopped = false, bResumed = false;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UAsdaWorldSubsystem* WS() const { UAsdaNetSubsystem* Nn = N(); UGameInstance* G = Nn ? Nn->GetGameInstance() : nullptr; return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		AAsdaMobActor* Mob() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(Rid) : nullptr; }
		FVector2D Pos() const { const AAsdaMobActor* M = Mob(); return M ? AsdaCoord::UeToGame2D(M->GetActorLocation()) : FVector2D(-1.f, -1.f); }
		float ErrTo(const FIntPoint& Tile) const { return FVector2D::Distance(Pos(), AsdaCoord::TileCenterGame(Tile)); }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaMobSegTest, Log, TEXT("САМОТЕСТ-ОТРЕЗОК %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		// Замер скачка видимой позиции; зовётся каждым опросом.
		void Sample()
		{
			const FVector2D P = Pos();
			if (P.X < 0.f) { return; }
			if (Last.X >= 0.f) { MaxJump = FMath::Max(MaxJump, FVector2D::Distance(P, Last)); }
			Last = P;
		}
	};

	// Тело 4015 (37 б): @0 жертва, @2 тип, @4 rid, @6 вид, @7/@9 cur, @11/@13 dst, @15 знаменатель скорости, @17/@19 темп, @21 урон, @25 HP, @29 вариант, @31 отражение.
	static TArray<uint8> Body4015(int32 Rid, uint8 Kind, const FIntPoint& Cur, const FIntPoint& Dst, int32 Victim = -1)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		const bool bMove = (Kind == 2 || Kind == 5 || Kind == 7 || Kind == 0xE);
		AsdaBytes::WrI16(&B[0], (int16)Victim);
		AsdaBytes::WrI16(&B[2], (int16)kMobType);
		AsdaBytes::WrI16(&B[4], (int16)Rid);
		B[6] = Kind;
		AsdaBytes::WrI16(&B[7], (int16)Cur.X);  AsdaBytes::WrI16(&B[9], (int16)Cur.Y);
		AsdaBytes::WrI16(&B[11], (int16)Dst.X); AsdaBytes::WrI16(&B[13], (int16)Dst.Y);
		AsdaBytes::WrI16(&B[15], (int16)(bMove ? kSpdDenom : 0));
		AsdaBytes::WrI16(&B[17], (int16)(bMove ? 10000 : 0));
		AsdaBytes::WrI16(&B[19], (int16)(bMove ? 10000 : 0));
		AsdaBytes::WrI32(&B[21], (Kind == 3) ? 10 : -1);
		AsdaBytes::WrI32(&B[25], 100);
		B[29] = (Kind == 3) ? 0 : 0xFF;
		AsdaBytes::WrI16(&B[31], -1);
		return B;
	}
	// Тело 4102 (17 б): вид@0, цель@1, навык@3, урон@5, HP@9, наложить@13, кастер@14, стек@16.
	static TArray<uint8> Body4102(int32 Rid, int32 Skill, int32 Caster)
	{
		TArray<uint8> B; B.SetNumZeroed(17);
		AsdaBytes::WrI16(&B[1], (int16)Rid); AsdaBytes::WrI16(&B[3], (int16)Skill); AsdaBytes::WrI32(&B[9], 100);
		B[13] = 1; AsdaBytes::WrI16(&B[14], (int16)Caster);
		return B;
	}
	// Тело 4017 (100 б) flags=3: @0 тип, @2 rid, @4 флаги, @8 28×i16 состояния (−1), @92 HP, @96/@98 живая клетка.
	static TArray<uint8> Body4017(int32 Rid, const FIntPoint& Tile, int32 State)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], (int16)kMobType);
		AsdaBytes::WrI16(&B[2], (int16)Rid);
		AsdaBytes::WrI32(&B[4], 3);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		if (State >= 0) { AsdaBytes::WrI16(&B[8], (int16)State); }
		AsdaBytes::WrI32(&B[92], 100);
		AsdaBytes::WrI16(&B[96], (int16)Tile.X); AsdaBytes::WrI16(&B[98], (int16)Tile.Y);
		return B;
	}
}

// Запуск самотеста (друг каркаса не нужен: только публичные методы).
struct FAsdaMobSegmentSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaMobSegmentSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaMobSegTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028 };
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Mob.SelfTestSegment"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaMobSegTest, Warning, TEXT("САМОТЕСТ-ОТРЕЗОК: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;

	auto Add = [S](const FString& Name, float MinSec, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.MinSec = MinSec; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto Feed = [S](int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = S->N()) { Nn->DebugFeedPacket(Op, B); } };
	auto T = [S](int32 Dx, int32 Dz) { return S->HomeTile + FIntPoint(Dx, Dz); };
	auto Keys = [](int32 K) { AsdaCVarSave::SetForTest(TEXT("asda.Mob.ResumeServerSegment"), K); AsdaCVarSave::SetForTest(TEXT("asda.Mob.SnapAsGlide"), K); };
	// Новый сценарий: прежний моб убран, новый стоит в клетке (4,20): от своего персонажа дальше 250 ед. по обеим осям.
	auto Fresh = [S, Feed, T]()
	{
		for (const int32 R : S->Rids) { S->Rid = R; if (AAsdaMobActor* M = S->Mob()) { M->Destroy(); } }
		S->Rids.Reset();
		S->Rid = S->NextRid++; S->Rids.Add(S->Rid);
		S->Phase = 0; S->T0 = 0.0; S->Last = FVector2D(-1.f, -1.f); S->MaxJump = 0.f; S->StunDrift = 0.f; S->ArriveS = -1.0;
		S->bStopped = false; S->bResumed = false;
		Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(4, 20), /*State*/1, kSpdDenom));
	};
	// Бег на 20 клеток: 5 с по серверу.
	auto Run5 = [S, Feed, T]() { Feed(4015, Body4015(S->Rid, 5, T(4, 20), T(24, 20))); S->T0 = FPlatformTime::Seconds(); };
	auto Since = [S]() { return FPlatformTime::Seconds() - S->T0; };

	Add(TEXT("подготовка"), 0.f, 1.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C) { S->Expect(false, TEXT("нет своего персонажа после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home);
		S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, TEXT("asda.Mob.SelfTestSegment"));
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		AsdaCVarSave::SetForTest(TEXT("asda.Mob.ControlFreeze"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Mob.LiveCellSnap"), 1);
	}, []() { return true; }, []() {});

	// Сценарии бег + попадание и бег + стан, с ключами 1 и с ключами 0.
	for (const int32 K : { 1, 0 })
	{
		const FString Tag = FString::Printf(TEXT("ключи %d"), K);
		Add(TEXT("бег → попадание, ") + Tag, 6.2f, 8.f, [S, Keys, Fresh, K]() { Keys(K); Fresh(); }, [S, Run5, Since, T]()
		{
			AAsdaMobActor* M = S->Mob();
			if (!M) { return false; }
			if (S->Phase == 0) { Run5(); S->Phase = 1; return false; }
			S->Sample();
			if (S->Phase == 1 && Since() >= 0.8) { M->NetHitFlinch(FVector2D::ZeroVector); S->bStopped = !M->IsMoving(); S->Phase = 2; }
			if (S->Phase == 2 && !M->IsHitAnimActive()) { S->Phase = 3; }
			if (S->Phase == 3 && M->IsMoving()) { S->bResumed = true; }
			if (S->ArriveS < 0.0 && S->ErrTo(T(24, 20)) <= 60.f) { S->ArriveS = Since(); }
			return false;
		}, [S, K]()
		{
			S->Expect(S->bStopped, TEXT("попадание остановило актора (эталон, состояние 13)"));
			if (K)
			{
				S->Expect(S->bResumed, TEXT("после клипа моб снова бежит"));
				S->Expect(S->MaxJump < kJumpTeleport, FString::Printf(TEXT("скачка нет: наибольший %.0f ед. за кадр"), S->MaxJump));
				S->Expect(S->ArriveS > 0.0 && S->ArriveS <= 5.4, FString::Printf(TEXT("пришёл к концу отрезка вместе с сервером: %.2f с (сервер 5.00)"), S->ArriveS));
			}
			else
			{
				S->Expect(S->MaxJump >= kJumpTeleport, FString::Printf(TEXT("контроль: доводчик поставил моба рывком, скачок %.0f ед."), S->MaxJump));
			}
		});
		Add(TEXT("бег → попадание + стан → снятие, ") + Tag, 8.0f, 10.f, [S, Keys, Fresh, K]() { Keys(K); Fresh(); }, [S, Run5, Since, T, Feed]()
		{
			AAsdaMobActor* M = S->Mob();
			if (!M) { return false; }
			if (S->Phase == 0) { Run5(); S->Phase = 1; return false; }
			S->Sample();
			if (S->Phase == 1 && Since() >= 0.8)
			{
				if (UAsdaNetSubsystem* Nn = S->N()) { Feed(4102, Body4102(S->Rid, kStunSkill, Nn->SelfUserNum)); }   // штатный путь: запись урона вешает состояние и даёт флинч
				M->NetHitFlinch(FVector2D::ZeroVector);
				Feed(4017, Body4017(S->Rid, T(7, 20), kStun));
				S->Phase = 2;
			}
			if (S->Phase == 2 && Since() >= 1.5) { S->StunPos = S->Pos(); S->Phase = 3; }
			if (S->Phase == 3)
			{
				S->StunDrift = FMath::Max(S->StunDrift, FVector2D::Distance(S->Pos(), S->StunPos));
				if (Since() >= 2.8)   // сервер снял стан; живая клетка уже на 4+ клетки впереди актора
				{
					Feed(4017, Body4017(S->Rid, T(12, 20), -1));
					S->Phase = 4;
				}
			}
			if (S->ArriveS < 0.0 && S->ErrTo(T(24, 20)) <= 60.f) { S->ArriveS = Since(); }
			return false;
		}, [S, K]()
		{
			S->Expect(S->StunDrift <= 5.f, FString::Printf(TEXT("под станом стоит: сдвиг %.0f ед."), S->StunDrift));
			if (K)
			{
				S->Expect(S->MaxJump < kJumpTeleport, FString::Printf(TEXT("скачка нет: наибольший %.0f ед. за кадр"), S->MaxJump));
				S->Expect(S->ArriveS >= 6.0 && S->ArriveS <= 7.6, FString::Printf(TEXT("добежал к концу отрезка: %.2f с (сервер с учётом стана ~7.0)"), S->ArriveS));
			}
			else
			{
				S->Expect(S->MaxJump >= kJumpTeleport, FString::Printf(TEXT("контроль: живая клетка 4017 поставила моба рывком, скачок %.0f ед."), S->MaxJump));
			}
		});
	}

	// Страх (вид 0xE): то же, что бег; контроль — рывок доводчика.
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("страх → попадание, ключи %d"), K), 6.2f, 8.f, [S, Keys, Fresh, K]() { Keys(K); Fresh(); }, [S, Feed, Since, T]()
		{
			AAsdaMobActor* M = S->Mob();
			if (!M) { return false; }
			if (S->Phase == 0) { Feed(4015, Body4015(S->Rid, 0xE, T(4, 20), T(24, 20))); S->T0 = FPlatformTime::Seconds(); S->Phase = 1; return false; }
			S->Sample();
			if (S->Phase == 1 && Since() >= 0.8) { M->NetHitFlinch(FVector2D::ZeroVector); S->Phase = 2; }
			if (S->ArriveS < 0.0 && S->ErrTo(T(24, 20)) <= 60.f) { S->ArriveS = Since(); }
			return false;
		}, [S, K]()
		{
			if (K)
			{
				S->Expect(S->MaxJump < kJumpTeleport, FString::Printf(TEXT("скачка нет: %.0f ед."), S->MaxJump));
				S->Expect(S->ArriveS > 0.0 && S->ArriveS <= 5.4, FString::Printf(TEXT("пришёл вместе с сервером: %.2f с"), S->ArriveS));
			}
			else { S->Expect(S->MaxJump >= kJumpTeleport, FString::Printf(TEXT("контроль: рывок доводчика, скачок %.0f ед."), S->MaxJump)); }
		});
	}

	// Сильный отлёт в беге: сервер переставил моба на 450 ед. назад и ведёт к той же цели, время отрезка больше.
	Add(TEXT("бег → отлёт: остаток от точки отброса"), 6.8f, 8.f, [S, Keys, Fresh]() { Keys(1); Fresh(); }, [S, Run5, Since, T]()
	{
		AAsdaMobActor* M = S->Mob();
		if (!M) { return false; }
		if (S->Phase == 0) { Run5(); S->Phase = 1; return false; }
		S->Sample();
		if (S->Phase == 1 && Since() >= 0.8) { M->NetKnockBack(1, FVector2D(-1.f, 0.f), FVector2D(1.f, 0.f)); S->Phase = 2; }
		if (S->ArriveS < 0.0 && S->ErrTo(T(24, 20)) <= 60.f) { S->ArriveS = Since(); }
		return false;
	}, [S]()
	{
		S->Expect(S->MaxJump < kJumpTeleport, FString::Printf(TEXT("скачка нет: %.0f ед."), S->MaxJump));
		S->Expect(S->ArriveS >= 5.5 && S->ArriveS <= 6.6, FString::Printf(TEXT("пришёл вместе с сервером: %.2f с (сервер ~6.1)"), S->ArriveS));
	});

	// Стан при разрыве ≥ 3 клеток: сервер стоит, живая клетка точна — короткий проход, а не ожидание снятия.
	Add(TEXT("стан и живая клетка в 5 клетках"), 3.0f, 5.f, [S, Keys, Fresh]() { Keys(1); Fresh(); }, [S, Run5, Since, Feed, T]()
	{
		AAsdaMobActor* M = S->Mob();
		if (!M) { return false; }
		if (S->Phase == 0) { Run5(); S->Phase = 1; return false; }
		S->Sample();
		if (S->Phase == 1 && Since() >= 0.8)
		{
			if (UAsdaNetSubsystem* Nn = S->N()) { Feed(4102, Body4102(S->Rid, kStunSkill, Nn->SelfUserNum)); }
			M->NetHitFlinch(FVector2D::ZeroVector);
			Feed(4017, Body4017(S->Rid, T(7, 20), kStun));
			S->Phase = 2;
		}
		if (S->Phase == 2 && !M->IsHitAnimActive() && Since() >= 1.8) { Feed(4017, Body4017(S->Rid, T(13, 20), kStun)); S->Phase = 3; }
		return false;
	}, [S, T]()
	{
		const float E = S->ErrTo(T(13, 20));
		S->Expect(E <= 60.f, FString::Printf(TEXT("моб на живой клетке: ошибка %.0f ед."), E));
		S->Expect(S->MaxJump < kJumpTeleport, FString::Printf(TEXT("скачка нет: %.0f ед."), S->MaxJump));
	});

	Add(TEXT("бег пришёл в клипе попадания: скорость по остатку"), 5.8f, 7.f, [S, Keys, Fresh]() { Keys(1); Fresh(); }, [S, Run5, Since, T]()
	{
		AAsdaMobActor* M = S->Mob();
		if (!M) { return false; }
		if (S->Phase == 0) { M->NetHitFlinch(FVector2D::ZeroVector); Run5(); S->Phase = 1; return false; }
		S->Sample();
		if (S->ArriveS < 0.0 && S->ErrTo(T(24, 20)) <= 60.f) { S->ArriveS = Since(); }
		return false;
	}, [S]()
	{
		S->Expect(S->MaxJump < kJumpTeleport, FString::Printf(TEXT("скачка нет: %.0f ед."), S->MaxJump));
		S->Expect(S->ArriveS > 0.0 && S->ArriveS <= 5.4, FString::Printf(TEXT("пришёл вместе с сервером: %.2f с"), S->ArriveS));
	});

	Add(TEXT("«стой» в клипе попадания снимает отрезок"), 2.2f, 4.f, [S, Keys, Fresh]() { Keys(1); Fresh(); }, [S, Run5, Since, Feed, T]()
	{
		AAsdaMobActor* M = S->Mob();
		if (!M) { return false; }
		if (S->Phase == 0) { Run5(); S->Phase = 1; return false; }
		if (S->Phase == 1 && Since() >= 0.6)
		{
			M->NetHitFlinch(FVector2D::ZeroVector);
			Feed(4015, Body4015(S->Rid, 1, T(6, 20), T(6, 20)));
			S->Phase = 2;
		}
		if (S->Phase == 2 && !M->IsHitAnimActive()) { S->Phase = 3; }
		if (S->Phase == 3 && M->IsMoving()) { S->bResumed = true; }
		return false;
	}, [S]()
	{
		const AAsdaMobActor* M = S->Mob();
		S->Expect(M && !M->HasServerSegment() && !S->bResumed, TEXT("отрезка нет, моб не побежал"));
	});

	Add(TEXT("4013 в клипе попадания снимает отрезок"), 2.2f, 4.f, [S, Keys, Fresh]() { Keys(1); Fresh(); }, [S, Run5, Since, Feed, T]()
	{
		AAsdaMobActor* M = S->Mob();
		if (!M) { return false; }
		if (S->Phase == 0) { Run5(); S->Phase = 1; return false; }
		if (S->Phase == 1 && Since() >= 0.6)
		{
			M->NetHitFlinch(FVector2D::ZeroVector);
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(6, 20), /*State*/3, kSpdDenom));
			S->Phase = 2;
		}
		if (S->Phase == 2 && !M->IsHitAnimActive()) { S->Phase = 3; }
		if (S->Phase == 3 && M->IsMoving()) { S->bResumed = true; }
		return false;
	}, [S]()
	{
		const AAsdaMobActor* M = S->Mob();
		S->Expect(M && !M->HasServerSegment() && !S->bResumed, TEXT("отрезка нет, моб не побежал"));
	});

	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("удар при расхождении 6 клеток, ключи %d"), K), 1.0f, 3.f, [S, Keys, Fresh, K]() { Keys(K); Fresh(); }, [S, Feed, T]()
		{
			AAsdaMobActor* M = S->Mob();
			UAsdaNetSubsystem* Nn = S->N();
			if (!M || !Nn) { return false; }
			if (S->Phase == 0) { S->Sample(); Feed(4015, Body4015(S->Rid, 3, T(10, 20), T(10, 20), Nn->SelfUserNum)); Feed(4017, Body4017(S->Rid, T(10, 20), -1)); S->Phase = 1; }   // живая клетка в окне прохода
			S->Sample();
			return false;
		}, [S, T, K]()
		{
			const float E = S->ErrTo(T(10, 20));
			S->Expect(E <= 60.f, FString::Printf(TEXT("моб на клетке удара: ошибка %.0f ед."), E));
			S->Expect(K ? S->MaxJump < kJumpTeleport : S->MaxJump >= kJumpTeleport, FString::Printf(TEXT("%s: скачок %.0f ед."), K ? TEXT("короткий проход") : TEXT("контроль, мгновенный снап"), S->MaxJump));
		});
	}

	Add(TEXT("итог"), 0.f, 1.f, [S]()
	{
		for (const int32 R : S->Rids) { S->Rid = R; if (AAsdaMobActor* M = S->Mob()) { M->Destroy(); } }
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), TEXT("asda.Mob.SelfTestSegment"), false, LogAsdaMobSegTest, TEXT("САМОТЕСТ-ОТРЕЗОК"), S->Ok, S->Fail, TEXT(""));
	}, []() { return true; }, []() {});

	// Спавн своего персонажа и исполнитель шагов.
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H;
		TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get();
			const TSharedPtr<FState> Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KitArgs.Delay), false);
	}
	TWeakPtr<FState> WSt = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
	{
		TSharedPtr<FState> St = WSt.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaMobSegTest, Log, TEXT("САМОТЕСТ-ОТРЕЗОК шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bPolled = !Stp.Poll || Stp.Poll();
		if (!(El >= Stp.MinSec && (bPolled || El >= Stp.MinSec)) && El < Stp.TimeoutSec) { return; }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMobSegment(
	TEXT("asda.Mob.SelfTestSegment"),
	TEXT("Самотест отрезка сервера у моба: бег → попадание/стан → добег без рывка, «стой» в клипе, удар при расхождении; контроль asda.Mob.ResumeServerSegment 0, asda.Mob.SnapAsGlide 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Mob.SelfTestSegment"));
		FAsdaMobSegmentSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
