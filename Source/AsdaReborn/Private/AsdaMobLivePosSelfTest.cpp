// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПОЗИЦИИ МОНСТРА ПО ПАКЕТАМ СЕРВЕРА (15.10.2026, волна mobpos_swing, `Reports/anticheat/wave_PORT_mobpos_swing.md`).
//   [ПОРТ]
//
// ★ ЗАЧЕМ. Живая сессия 15.10: моб rid=212 виден в 16 клетках (бёрст 4013 «ход 164→170»), а NpcServer держит его в 22 —
//   шесть отказов 4029 код 1 «вне дальности» подряд. Правки волны: (1) сверка видимой позиции с живой клеткой NpcServer из
//   4027 (@5/@7) и 4017 (@96/@98) по эталонному порогу «≥ 3 клеток» (`UAsdaWorldSubsystem::CorrectMobToServerCell`);
//   (2) удержание боя не шире допуска NpcServer к 4028 (`asda.Combat.RangeOutServerCap`); (3) X/Z в 4026/4028 — позиция
//   СВОЕГО персонажа (эталон + перехват оригинала, `asda.Combat.AttackPosSelf`). Самотест проверяет, что:
//   • пакеты движения (4013 покой/ход, 4015 стоп/ход/погоня) ставят видимую позицию на серверную клетку в пределах допуска —
//     и в конце отрезка, и по пути (позиция по времени сервера);
//   • «призрак» (4013 «ход», которого сервер не делал) исправляется живой клеткой 4017 и 4027; расхождение меньше 3 клеток
//     не трогается;
//   • бой по «призраку»: после 4027 с живой клеткой вне дальности 4028 не уходит, уходит подход 4006 с номером цели;
//   • в 4026/4028 — своя позиция.
//   Контрольные пары (закон pixel-metric-needs-control-pairs): `asda.Mob.LiveCellSnap 0` — призрак остаётся и бой шлёт 4028
//   в пустоту (дефект виден); `asda.Combat.AttackPosSelf 0` — в 4028 позиция цели.
//
// ★ КАК. Офлайн, штатным путём: пакеты — `UAsdaNetSubsystem::DebugFeedPacket` (тот же `HandlePacket`, что у сокета), свой
//   персонаж — `Asda.World.FakeSelfSpawn`, бой — `EngageMobFromClick` → `AttackTick`, исходящие — полный перехват
//   (`AsdaSelfTestKit::ArmCapture`, сокета нет).
//
// Консоль: `asda.Mob.SelfTestLivePos [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ПОЗМОБ», итог — «САМОТЕСТ-ПОЗМОБ ИТОГ».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                         // запись полей тел пакетов
#include "AsdaCVarSave.h"                      // ключи на время прогона (вернутся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"                   // запись 4013
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, перехват, итог, выход
#include "World/AsdaPlayerController.h"        // боевой тик, клик по цели
#include "World/AsdaPlayerCharacter.h"         // свой персонаж
#include "World/AsdaWorldSubsystem.h"          // FindNetMobByRid
#include "World/AsdaCoord.h"                   // игра ↔ UE, клетки
#include "Items/AsdaItemTableSubsystem.h"      // баллиста и её дальность
#include "Npc/AsdaMobActor.h"                  // моб
#include "Components/CapsuleComponent.h"        // крупная цель: радиус капсулы-хитбокса
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobPosTest, Log, All);

namespace AsdaMobPosTestLocal
{
	constexpr int32 kMobType = 1;         // тип моба (Mob list: волк alpen) — как у `asda.Fx.StatusSelfTest`
	constexpr int32 kSpdDenom = 250;      // знаменатель скорости: 100/250 = 0.4 юн/мс (бег сервера)
	constexpr float kV = 100.f / (float)kSpdDenom;

	// Перехваченный исходящий: опкод, время, тело.
	struct FOut { int32 Op = 0; double T = 0.0; TArray<uint8> Body; };

	struct FStep
	{
		FString Name;
		float MinSec = 0.f, TimeoutSec = 10.f;
		TFunction<void()> Start;
		TFunction<bool()> Poll;
		TFunction<void()> Check;
	};

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
		FAsdaSelfTestRunGate Gate{ TEXT("asda.Mob.SelfTestLivePos"), false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0;
		TArray<FOut> Outs;
		int32 Mark = 0;                     // начало текущего сценария в Outs
		FVector2D Home = FVector2D::ZeroVector;
		FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 SavedWeapon = 0, SavedEquipped = 99;
		bool bWasInGame = false;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TArray<int32> Rids;                 // мобы прогона (убрать в итоге)
		int32 NextRid = 30901;
		int32 Rid = -1;                     // моб текущего сценария
		int32 Phase = 0;
		double PhaseS = 0.0;
		FVector2D Mid = FVector2D::ZeroVector;   // ожидаемая позиция по пути
		float MidErr = -1.f;
		FVector2D Probe = FVector2D::ZeroVector;
		int32 RangeTiles = 0;
		int32 Rid2 = -1, Rid3 = -1;         // ★ ревизия: мобы сценария «несколько сразу»
		FVector2D Probe2 = FVector2D::ZeroVector, Probe3 = FVector2D::ZeroVector;
		float Jump = -1.f;                  // сдвиг видимой позиции в кадре поправки
		bool bMovingAfter = false;          // моб идёт сразу после поправки
		float Dist0 = 0.f;                  // крупная цель: опорная дистанция
		float MinRangeOut = 1e9f;           // крупная цель: наименьшее удержание за сценарий

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(R) : nullptr; }
		AAsdaMobActor* Mob() const { return Mob(Rid); }
		int32 Count(int32 Op) const { int32 C = 0; for (int32 i = Mark; i < Outs.Num(); ++i) { if (Outs[i].Op == Op) { ++C; } } return C; }
		const FOut* First(int32 Op) const { for (int32 i = Mark; i < Outs.Num(); ++i) { if (Outs[i].Op == Op) { return &Outs[i]; } } return nullptr; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaMobPosTest, Log, TEXT("САМОТЕСТ-ПОЗМОБ %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		// Расстояние видимой позиции моба до центра клетки (игровые единицы); −1 — моба нет.
		float ErrTo(int32 R, const FIntPoint& Tile) const
		{
			const AAsdaMobActor* M = Mob(R);
			return M ? FVector2D::Distance(AsdaCoord::UeToGame2D(M->GetActorLocation()), AsdaCoord::TileCenterGame(Tile)) : -1.f;
		}
		FVector2D MobGame(int32 R) const
		{
			const AAsdaMobActor* M = Mob(R);
			return M ? AsdaCoord::UeToGame2D(M->GetActorLocation()) : FVector2D(-1.f, -1.f);
		}
		int32 NewRid() { const int32 R = NextRid++; Rids.Add(R); return R; }
	};

	// Тело 4015 (37 б, раскладка `Send_SectorInUpdateMonster_NPC_Info`, живой пакет 15.10): @0 жертва −1, @2 тип, @4 rid,
	//   @6 kind, @7/@9 cur, @11/@13 dst, @15 знаменатель скорости, @17/@19 темп клипа ×1e-4, @21 урон −1, @25 HP,
	//   @29 вариант 0xFF, @31 отражение −1.
	static TArray<uint8> Body4015(int32 Rid, uint8 Kind, const FIntPoint& Cur, const FIntPoint& Dst, int32 Hp = 100)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], -1);
		AsdaBytes::WrI16(&B[2], (int16)kMobType);
		AsdaBytes::WrI16(&B[4], (int16)Rid);
		B[6] = Kind;
		AsdaBytes::WrI16(&B[7], (int16)Cur.X);  AsdaBytes::WrI16(&B[9], (int16)Cur.Y);
		AsdaBytes::WrI16(&B[11], (int16)Dst.X); AsdaBytes::WrI16(&B[13], (int16)Dst.Y);
		AsdaBytes::WrI16(&B[15], (int16)((Kind == 1) ? 0 : kSpdDenom));
		AsdaBytes::WrI16(&B[17], (int16)((Kind == 1) ? 0 : 10000));
		AsdaBytes::WrI16(&B[19], (int16)((Kind == 1) ? 0 : 10000));
		AsdaBytes::WrI32(&B[21], -1);
		AsdaBytes::WrI32(&B[25], Hp);
		B[29] = 0xFF;
		AsdaBytes::WrI16(&B[31], -1);
		return B;
	}
	// Тело 4017 (100 б) flags=0x3 (живой моб, бит «в бою»): @0 тип, @2 rid, @4 флаги, @8 28×i16 id (−1), @92 HP, @96/@98 клетка.
	static TArray<uint8> Body4017(int32 Rid, const FIntPoint& Tile, int32 Hp = 100)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], (int16)kMobType);
		AsdaBytes::WrI16(&B[2], (int16)Rid);
		AsdaBytes::WrI32(&B[4], 3);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		AsdaBytes::WrI32(&B[92], Hp);
		AsdaBytes::WrI16(&B[96], (int16)Tile.X); AsdaBytes::WrI16(&B[98], (int16)Tile.Y);
		return B;
	}
	// Тело 4027 case 1 (53 б, `GameServer sub_4C4EF0`): @0 код, @1 атакующий, @3 rid, @5/@7 живая клетка, @9 HP,
	//   @13 4×i32 удары (0 — пусто), @29 4×i16 −1, @37 4×i32 0.
	static TArray<uint8> Body4027(int32 AtkU, int32 Rid, const FIntPoint& Tile, int32 Hp = 100)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = 1;
		AsdaBytes::WrI16(&B[1], (int16)AtkU);
		AsdaBytes::WrI16(&B[3], (int16)Rid);
		AsdaBytes::WrI16(&B[5], (int16)Tile.X); AsdaBytes::WrI16(&B[7], (int16)Tile.Y);
		AsdaBytes::WrI32(&B[9], Hp);
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[29 + 2 * i], -1); }
		return B;
	}

	// Первое оружие подтипа (тип 9, не удочка) с дальностью. 0 — нет.
	static int32 FindWeapon(const UAsdaItemTableSubsystem* IT, int32 Wt, int32* OutRange = nullptr)
	{
		if (!IT) { return 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (R && R->ItemType == 9 && R->ItemSubType == Wt && R->ItemVariant != 1 && R->AttackRangeTiles > 0)
			{
				if (OutRange) { *OutRange = R->AttackRangeTiles; }
				return Code;
			}
		}
		return 0;
	}

	static float RdF(const TArray<uint8>& B, int32 O) { float F = 0.f; if (B.Num() >= O + 4) { FMemory::Memcpy(&F, &B[O], 4); } return F; }
}

struct FAsdaMobLivePosSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaMobLivePosSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaMobPosTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 4026, 4028 };
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Mob.SelfTestLivePos"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaMobPosTest, Warning, TEXT("САМОТЕСТ-ПОЗМОБ: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;

	auto Add = [S](const FString& Name, float MinSec, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.MinSec = MinSec; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto Feed = [S](int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = S->N()) { Nn->DebugFeedPacket(Op, B); } };
	// Сброс боя и сценария (мобы прошлых сценариев убираются).
	auto Reset = [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		if (PC)
		{
			PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false; PC->bWaitingCombatAck = false;
			PC->NextAttackTickTime = 0.0; PC->TargetActor = nullptr;
			PC->GetWorldTimerManager().ClearTimer(PC->DeferredAttackSendTimer);
			PC->CancelRoute();   // подход прошлого сценария (офлайн 4007 не придёт)
		}
		// Офлайн ответа 4007 на подход нет — эталонный бит 2 «ход ждёт ответа» иначе навсегда закрыл бы новые 4026.
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест позиции моба: сброс сценария")); }
		if (C) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; C->NetSnap(S->Home); }
		for (const int32 R : S->Rids) { if (AAsdaMobActor* M = S->Mob(R)) { M->Destroy(); } }
		S->Rids.Reset();
		S->Mark = S->Outs.Num(); S->Phase = 0; S->PhaseS = 0.0; S->MidErr = -1.f; S->Rid = -1;
	};
	auto T = [S](int32 Dx, int32 Dz) { return S->HomeTile + FIntPoint(Dx, Dz); };

	// ─────────────────────── Подготовка ───────────────────────
	Add(TEXT("подготовка"), 0.f, 1.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
		if (!C || !PC) { S->Expect(false, TEXT("нет своего персонажа/контроллера после FakeSelfSpawn")); return; }
		S->Home = AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()));
		S->HomeTile = AsdaCoord::GameToTile(S->Home);
		C->NetSnap(S->Home);
		S->SavedWeapon = Net->SelfWeaponItemId;
		S->SavedEquipped = C->EquippedWeaponSubType;
		S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, TEXT("asda.Mob.SelfTestLivePos"));
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* Data, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (!St || (Op != 4006 && Op != 4008 && Op != 4026 && Op != 4028)) { return; }
			FOut O; O.Op = Op; O.T = FPlatformTime::Seconds();
			if (Data && Len > 0) { O.Body.Append(Data, Len); }
			St->Outs.Add(MoveTemp(O));
		}, (uint16)0, TEXT("asda.Mob.SelfTestLivePos"));
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Mob.LiveCellSnap"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.AttackPosSelf"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.RangeOutServerCap"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.RhythmFromAnim"), 1);
		UE_LOG(LogAsdaMobPosTest, Log, TEXT("САМОТЕСТ-ПОЗМОБ дом: клетка (%d,%d)"), S->HomeTile.X, S->HomeTile.Y);
	}, []() { return true; }, []() {});

	// ─────────────────────── 1. Пакеты движения → видимая позиция = клетка сервера ───────────────────────
	Add(TEXT("4013 покой: моб на клетке пакета"), 0.6f, 3.f, [S, Reset, Feed, T]()
	{
		Reset();
		S->Rid = S->NewRid();
		Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(4, 0), /*State*/1, kSpdDenom));
	}, [S]() { return S->Mob() != nullptr; }, [S, T]()
	{
		const float E = S->ErrTo(S->Rid, T(4, 0));
		S->Expect(E >= 0.f && E <= 15.f, FString::Printf(TEXT("4013 состояние 1: видимая позиция — центр клетки (%d,%d), ошибка %.1f ед. (≤ 15)"), T(4, 0).X, T(4, 0).Y, E));
	});
	Add(TEXT("4015 ход (kind 2): по пути и в конце"), 2.2f, 4.f, [S, Feed, T]()
	{
		Feed(4015, Body4015(S->Rid, 2, T(4, 0), T(9, 0)));
		S->PhaseS = FPlatformTime::Seconds(); S->MidErr = -1.f;
	}, [S, T]()
	{
		const double El = FPlatformTime::Seconds() - S->PhaseS;
		if (S->MidErr < 0.f && El >= 0.6)
		{
			// Позиция сервера по времени: cur + v·t (отрезок 500 ед. за 1.25 с).
			const FVector2D A = AsdaCoord::TileCenterGame(T(4, 0)), Bp = AsdaCoord::TileCenterGame(T(9, 0));
			const float Along = FMath::Min((float)(El * 1000.0) * kV, FVector2D::Distance(A, Bp));
			S->Mid = A + (Bp - A).GetSafeNormal() * Along;
			S->MidErr = FVector2D::Distance(S->MobGame(S->Rid), S->Mid);
		}
		return true;
	}, [S, T]()
	{
		S->Expect(S->MidErr >= 0.f && S->MidErr <= 100.f, FString::Printf(TEXT("по пути видимая позиция совпадает с позицией сервера по времени: ошибка %.1f ед. (≤ 1 клетки)"), S->MidErr));
		const float E = S->ErrTo(S->Rid, T(9, 0));
		const AAsdaMobActor* M = S->Mob();
		S->Expect(E >= 0.f && E <= 30.f && M && !M->IsMoving(), FString::Printf(TEXT("в конце отрезка моб на клетке назначения (ошибка %.1f ед. ≤ 30: эталонный сдвиг цели 0..9) и стоит"), E));
	});
	Add(TEXT("4015 стоп (kind 1) на месте"), 0.4f, 2.f, [S, Feed, T]()
	{
		Feed(4015, Body4015(S->Rid, 1, T(9, 0), T(9, 0)));
	}, []() { return true; }, [S, T]()
	{
		const float E = S->ErrTo(S->Rid, T(9, 0));
		S->Expect(E >= 0.f && E <= 30.f, FString::Printf(TEXT("стоп на своей клетке: ошибка %.1f ед."), E));
	});
	Add(TEXT("4015 погоня (kind 5): моб приходит на клетку сервера"), 1.6f, 4.f, [S, Feed, T]()
	{
		Feed(4015, Body4015(S->Rid, 5, T(9, 0), T(9, 4)));
	}, []() { return true; }, [S, T]()
	{
		const float E = S->ErrTo(S->Rid, T(9, 4));
		S->Expect(E >= 0.f && E <= 50.f, FString::Printf(TEXT("погоня: моб на клетке (%d,%d), ошибка %.1f ед. (≤ 50: эталонный джиттер ±30)"), T(9, 4).X, T(9, 4).Y, E));
	});

	// ─────────────────────── 2. «Призрак» 4013 и живая клетка ───────────────────────
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("призрак 4013 «ход» + живая клетка 4017, LiveCellSnap %d"), K), 2.0f, 6.f, [S, Reset, Feed, T, K]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Mob.LiveCellSnap"), K);
			S->Rid = S->NewRid();
			// Живой случай: «действие 2, cur → dst», а сервер моба не ведёт.
			Feed(4013, AsdaTestPackets::MobSpawn4013Move(S->Rid, kMobType, T(-5, 3), T(1, 3), /*State*/2, kSpdDenom));
		}, [S, T, Feed]()
		{
			AAsdaMobActor* M = S->Mob();
			if (S->Phase == 0 && M && !M->IsMoving() && FPlatformTime::Seconds() - S->StepStartS > 1.9)
			{
				S->Probe = S->MobGame(S->Rid);
				Feed(4017, Body4017(S->Rid, T(-5, 3)));   // живая клетка сервера — там, откуда «ушёл»
				S->Phase = 1; S->PhaseS = FPlatformTime::Seconds();
			}
			return S->Phase == 1 && FPlatformTime::Seconds() - S->PhaseS > 0.4;
		}, [S, T, K]()
		{
			const float Ghost = FVector2D::Distance(S->Probe, AsdaCoord::TileCenterGame(T(1, 3)));
			S->Expect(Ghost <= 30.f, FString::Printf(TEXT("до 4017 клиент, как эталон, довёл моба до dst записи 4013 (ошибка %.1f ед.)"), Ghost));
			const float E = S->ErrTo(S->Rid, T(-5, 3));
			const AAsdaMobActor* M = S->Mob();
			if (K)
			{
				S->Expect(E >= 0.f && E <= 15.f && M && !M->IsMoving(),
					FString::Printf(TEXT("4017 с живой клеткой (расхождение 6 клеток): моб на клетке сервера (ошибка %.1f ед.) и стоит"), E));
			}
			else
			{
				S->Expect(E > 500.f, FString::Printf(TEXT("контроль, LiveCellSnap 0: призрак остаётся в 6 клетках от сервера (%.0f ед.) — дефект виден"), E));
			}
			AsdaCVarSave::SetForTest(TEXT("asda.Mob.LiveCellSnap"), 1);
		});
	}
	Add(TEXT("живая клетка 4027 case 1 (ответ на 4026)"), 0.3f, 3.f, [S, Reset, Feed, T]()
	{
		Reset();
		S->Rid = S->NewRid();
		Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(2, -4), /*State*/1, kSpdDenom));
	}, [S, T, Feed]()
	{
		if (S->Phase == 0 && S->Mob())
		{
			const UAsdaNetSubsystem* Nn = S->N();
			Feed(4027, Body4027(Nn ? Nn->SelfUserNum : 0, S->Rid, T(-3, -4)));
			S->Phase = 1;
		}
		return S->Phase == 1;
	}, [S, T]()
	{
		const float E = S->ErrTo(S->Rid, T(-3, -4));
		S->Expect(E >= 0.f && E <= 15.f, FString::Printf(TEXT("4027: моб поставлен на живую клетку сервера (5 клеток), ошибка %.1f ед."), E));
	});
	Add(TEXT("порог: расхождение 2 клетки не трогается"), 0.3f, 3.f, [S, Feed, T]()
	{
		S->Probe = S->MobGame(S->Rid);
		Feed(4017, Body4017(S->Rid, T(-1, -4)));
	}, []() { return true; }, [S]()
	{
		const float D = FVector2D::Distance(S->Probe, S->MobGame(S->Rid));
		S->Expect(D <= 1.f, FString::Printf(TEXT("4017 с клеткой в 2 клетках: позиция не тронута (сдвиг %.1f ед.; эталонный порог — 3 клетки)"), D));
	});

	// ─────────────────────── 2б. Ревизия СРЕДН-2: идущий моб + 4017 посреди хода — без остановки и рывка ───────────────
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("идущий моб отстал на 4 клетки, 4017 посреди хода, LiveCellSnap %d"), K), 0.f, 6.f, [S, Reset, Feed, T, K]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Mob.LiveCellSnap"), K);
			S->Rid = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(-5, 6), /*State*/1, kSpdDenom));
		}, [S, T, Feed]()
		{
			AAsdaMobActor* M = S->Mob();
			const double Now = FPlatformTime::Seconds();
			if (S->Phase == 0 && M && Now - S->StepStartS > 0.4)
			{
				Feed(4015, Body4015(S->Rid, 2, T(-5, 6), T(5, 6)));   // 10 клеток за 2.5 с
				S->Phase = 1; S->PhaseS = Now;
			}
			if (S->Phase == 1 && M && Now - S->PhaseS > 1.0)
			{
				// Сервер прошёл 4 клетки; клиент искусственно отстал — стоит в начале отрезка, но траектория жива.
				const FVector2D A = AsdaCoord::GameToUe2D(AsdaCoord::TileCenterGame(T(-5, 6)));
				M->SetActorLocation(FVector(A.X, A.Y, M->GetActorLocation().Z));
				const FVector2D Before = S->MobGame(S->Rid);
				Feed(4017, Body4017(S->Rid, T(-1, 6)));
				S->Jump = FVector2D::Distance(Before, S->MobGame(S->Rid));
				S->bMovingAfter = M->IsMoving();
				S->Phase = 2;   // время сервера считаем от 4015 (PhaseS не трогаем)
			}
			return S->Phase == 2 && Now - S->PhaseS > 2.75;   // сервер в цели через 2.5 с
		}, [S, T, K]()
		{
			const float E = S->ErrTo(S->Rid, T(5, 6));
			if (K)
			{
				S->Expect(S->Jump >= 0.f && S->Jump <= 1.f && S->bMovingAfter,
					FString::Printf(TEXT("4017 в 4 клетках от идущего моба: без рывка (сдвиг %.1f ед.) и без остановки (идёт: %s)"), S->Jump, S->bMovingAfter ? TEXT("да") : TEXT("нет")));
				S->Expect(E >= 0.f && E <= 30.f, FString::Printf(TEXT("догнал нормировкой скорости: к приходу сервера (2.5 с) на клетке назначения, ошибка %.1f ед."), E));
			}
			else
			{
				S->Expect(E > 150.f, FString::Printf(TEXT("контроль, LiveCellSnap 0: отставший моб к приходу сервера ещё в пути (%.0f ед.) — догонка не слепая"), E));
			}
			AsdaCVarSave::SetForTest(TEXT("asda.Mob.LiveCellSnap"), 1);
		});
	}
	Add(TEXT("несколько монстров сразу: призрак, идущий, совпадающий"), 0.f, 6.f, [S, Reset, Feed, T]()
	{
		Reset();
		S->Rid = S->NewRid(); S->Rid2 = S->NewRid(); S->Rid3 = S->NewRid();
		Feed(4013, AsdaTestPackets::MobSpawn4013Move(S->Rid, kMobType, T(-6, -7), T(0, -7), /*State*/2, kSpdDenom));   // призрак
		Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid2, kMobType, T(-6, -9), /*State*/1, kSpdDenom));
		Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid3, kMobType, T(3, -9), /*State*/1, kSpdDenom));
	}, [S, T, Feed]()
	{
		const double Now = FPlatformTime::Seconds();
		AAsdaMobActor* G = S->Mob(S->Rid); AAsdaMobActor* Wm = S->Mob(S->Rid2); AAsdaMobActor* Sm = S->Mob(S->Rid3);
		if (S->Phase == 0 && G && Wm && Sm && Now - S->StepStartS > 1.8 && !G->IsMoving())
		{
			Feed(4015, Body4015(S->Rid2, 2, T(-6, -9), T(4, -9)));
			S->Phase = 1; S->PhaseS = Now;
		}
		if (S->Phase == 1 && Wm && Now - S->PhaseS > 1.0)
		{
			const FVector2D A = AsdaCoord::GameToUe2D(AsdaCoord::TileCenterGame(T(-6, -9)));
			Wm->SetActorLocation(FVector(A.X, A.Y, Wm->GetActorLocation().Z));   // идущий отстал на 4 клетки
			S->Probe = S->MobGame(S->Rid2); S->Probe3 = S->MobGame(S->Rid3);
			Feed(4017, Body4017(S->Rid, T(-6, -7)));    // призраку — его настоящая клетка (6 клеток)
			Feed(4017, Body4017(S->Rid2, T(-2, -9)));   // идущему — клетка сервера впереди
			Feed(4017, Body4017(S->Rid3, T(3, -9)));    // совпадающему — его же клетка
			S->Jump = FVector2D::Distance(S->Probe, S->MobGame(S->Rid2));
			S->bMovingAfter = Wm->IsMoving();
			S->Probe2 = S->MobGame(S->Rid3);
			S->Phase = 2;
		}
		return S->Phase == 2;
	}, [S, T]()
	{
		const AAsdaMobActor* G = S->Mob(S->Rid);
		const float Eg = S->ErrTo(S->Rid, T(-6, -7));
		S->Expect(Eg >= 0.f && Eg <= 15.f && G && !G->IsMoving(), FString::Printf(TEXT("призрак: на клетке сервера (%.1f ед.) и стоит"), Eg));
		S->Expect(S->Jump >= 0.f && S->Jump <= 1.f && S->bMovingAfter, FString::Printf(TEXT("идущий: без рывка (%.1f ед.) и без остановки"), S->Jump));
		S->Expect(FVector2D::Distance(S->Probe3, S->Probe2) <= 0.5f, TEXT("совпадающий: позиция не тронута"));
	});

	// ─────────────────────── 3. Бой по «призраку» (живой случай 15.10) ───────────────────────
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("бой по призраку: 4027 с живой клеткой вне дальности, LiveCellSnap %d"), K), 0.f, 14.f, [S, Reset, Feed, T, K]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Mob.LiveCellSnap"), K);
			UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N();
			if (!G || !C || !Nn) { return; }
			int32 Range = 0;
			Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 11, &Range);   // баллиста (живой случай)
			C->EquippedWeaponSubType = 11;
			S->RangeTiles = Range;
			S->Expect(Nn->SelfWeaponItemId > 0 && Range > 4, FString::Printf(TEXT("баллиста %d, дальность %d клеток"), Nn->SelfWeaponItemId, Range));
			S->Rid = S->NewRid();
			// Сервер: моб в «дальность + 6» (живой случай: 22 при 16); запись 4013 «ход» — в «дальность − 4» (призрак 12).
			Feed(4013, AsdaTestPackets::MobSpawn4013Move(S->Rid, kMobType, T(Range + 6, 0), T(Range - 4, 0), /*State*/2, kSpdDenom));
		}, [S, T, Feed]()
		{
			AAsdaMobActor* M = S->Mob(); AAsdaPlayerController* PC = S->PC();
			const double Now = FPlatformTime::Seconds();
			if (S->Phase == 0 && M && PC && !M->IsMoving() && Now - S->StepStartS > 2.0)
			{
				S->Probe = S->MobGame(S->Rid);   // видимая позиция (призрак) — в дальности
				PC->SetTarget(M);
				PC->EngageMobFromClick(M);   // второй клик: бой
				S->Phase = 1; S->PhaseS = Now;
			}
			if (S->Phase == 1 && S->Count(4026) > 0)
			{
				const UAsdaNetSubsystem* Nn = S->N();
				Feed(4027, Body4027(Nn ? Nn->SelfUserNum : 0, S->Rid, T(S->RangeTiles + 6, 0)));   // ответ: живая клетка
				S->Phase = 2; S->PhaseS = Now;
			}
			return S->Phase == 2 && Now - S->PhaseS > 4.0;
		}, [S, T, K]()
		{
			const float Ghost = FVector2D::Distance(S->Probe, AsdaCoord::TileCenterGame(T(S->RangeTiles - 4, 0)));
			S->Expect(Ghost <= 30.f, FString::Printf(TEXT("видимый моб до боя — призрак в дальности (ошибка %.1f ед. до клетки записи 4013)"), Ghost));
			S->Expect(S->Count(4026) == 1, FString::Printf(TEXT("по видимой дальности ушла одна 4026 (%d)"), S->Count(4026)));
			if (const FOut* O = S->First(4026))
			{
				// ★ Ревизия (НИЗК-9): побайтно — X/Z 4026 (@26..33, дубль @34..41) = X/Z предшествующего 4008 (тело 27 б с @16:
				//   X @21, Z @25), как в перехвате оригинала; 4008 не уходил (стояли на месте) — побайтно с float своей позиции.
				const FOut* Stop = S->First(4008);
				uint8 Want[8];
				if (Stop && Stop->Body.Num() >= 29) { FMemory::Memcpy(Want, &Stop->Body[21], 8); }
				else
				{
					const FVector2D Me = S->Ch() ? S->Ch()->GetGamePosXZ() : S->Home;
					const float X = (float)Me.X, Z = (float)Me.Y;
					FMemory::Memcpy(Want, &X, 4); FMemory::Memcpy(Want + 4, &Z, 4);
				}
				const bool bSame = O->Body.Num() >= 42 && FMemory::Memcmp(&O->Body[26], Want, 8) == 0 && FMemory::Memcmp(&O->Body[34], Want, 8) == 0;
				const FVector2D P(RdF(O->Body, 26), RdF(O->Body, 30));
				S->Expect(bSame, FString::Printf(TEXT("4026 несёт СВОЮ позицию побайтно = %s (%.0f,%.0f), с дублем (эталон + перехват оригинала)"),
					Stop ? TEXT("позиции 4008") : TEXT("своей позиции (4008 не было)"), P.X, P.Y));
			}
			const int32 N28 = S->Count(4028);
			const int32 N06 = S->Count(4006);
			if (K)
			{
				const float E = S->ErrTo(S->Rid, T(S->RangeTiles + 6, 0));
				S->Expect(E >= 0.f && E <= 15.f, FString::Printf(TEXT("после 4027 моб на живой клетке сервера (ошибка %.1f ед.)"), E));
				S->Expect(N28 == 0, FString::Printf(TEXT("4028 «вне дальности сервера» не ушёл ни один (%d) — живой лог: шесть отказов 4029 код 1"), N28));
				bool bApproach = false;
				for (int32 i = S->Mark; i < S->Outs.Num(); ++i)
				{
					const FOut& O = S->Outs[i];
					if (O.Op != 4006 || O.Body.Num() < 43) { continue; }
					const int32 Tgt = AsdaBytes::RdI16(&O.Body[41]);
					const FVector2D Dst(RdF(O.Body, 29), RdF(O.Body, 33));
					if (Tgt == S->Rid && Dst.X > S->Home.X + 100.f) { bApproach = true; }
				}
				S->Expect(N06 > 0 && bApproach, FString::Printf(TEXT("вместо удара — подход 4006 с номером цели к клетке сервера (4006: %d, к цели: %s)"),
					N06, bApproach ? TEXT("да") : TEXT("нет")));
			}
			else
			{
				S->Expect(N28 > 0, FString::Printf(TEXT("контроль, LiveCellSnap 0: клиент бьёт призрака — ушло 4028: %d (дефект виден)"), N28));
			}
			AsdaCVarSave::SetForTest(TEXT("asda.Mob.LiveCellSnap"), 1);
		});
	}

	// ─────────────────────── 4. Гистерезис не шире допуска сервера к 4028 ───────────────────────
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("моб ушёл на дальность + 5 посреди боя, RangeOutServerCap %d"), K), 0.f, 14.f, [S, Reset, Feed, T, K]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.RangeOutServerCap"), K);
			UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N();
			if (!G || !C || !Nn) { return; }
			int32 Range = 0;
			Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 11, &Range);
			C->EquippedWeaponSubType = 11;
			S->RangeTiles = Range;
			S->Rid = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(5, 0), /*State*/1, kSpdDenom));
		}, [S, T, Feed]()
		{
			AAsdaMobActor* M = S->Mob(); AAsdaPlayerController* PC = S->PC();
			const double Now = FPlatformTime::Seconds();
			if (S->Phase == 0 && M && PC && Now - S->StepStartS > 0.5)
			{
				PC->SetTarget(M);
				PC->EngageMobFromClick(M);
				S->Phase = 1;
			}
			if (S->Phase == 1 && S->Count(4026) > 0)
			{
				// Сервер увёл моба на «дальность + 5»: 4015 стоп в новой клетке (> 150 ед. — эталонный снап).
				Feed(4015, Body4015(S->Rid, 1, T(S->RangeTiles + 5, 0), T(S->RangeTiles + 5, 0)));
				S->Phase = 2; S->PhaseS = Now; S->Mark = S->Outs.Num();
			}
			return S->Phase == 2 && Now - S->PhaseS > 3.5;
		}, [S, K]()
		{
			const int32 N28 = S->Count(4028);
			if (K) { S->Expect(N28 == 0, FString::Printf(TEXT("моб в «дальность + 5» (сервер примет не дальше «+3 + радиус»): 4028 не уходит (%d), подход 4006: %d"), N28, S->Count(4006))); }
			else   { S->Expect(N28 > 0, FString::Printf(TEXT("контроль, RangeOutServerCap 0: прежний гистерезис 1.6× бьёт дальше допуска сервера — 4028: %d (дефект виден)"), N28)); }
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.RangeOutServerCap"), 1);
		});
	}

	// ─────────────────────── 4б. Ревизия СРЕДН-1: крупная цель (капсула 300 ед.), ближний бой, моб бродит ±60 ───────────
	Add(TEXT("крупная цель, меч: бродит у границы — без метания «подход ↔ удар»"), 0.f, 12.f, [S, Reset, Feed, T]()
	{
		Reset();
		UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N();
		if (!G || !C || !Nn) { return; }
		int32 Range = 0;
		Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 1, &Range);
		C->EquippedWeaponSubType = 1;
		S->RangeTiles = Range;
		S->MinRangeOut = 1e9f;
		S->Rid = S->NewRid();
		Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(8, 0), /*State*/1, kSpdDenom));
	}, [S]()
	{
		AAsdaMobActor* M = S->Mob(); AAsdaPlayerController* PC = S->PC();
		const double Now = FPlatformTime::Seconds();
		auto Place = [S, M](float D)
		{
			const FVector2D U = AsdaCoord::GameToUe2D(S->Home + FVector2D(D, 0.f));
			M->SetActorLocation(FVector(U.X, U.Y, M->GetActorLocation().Z));
		};
		if (S->Phase == 0 && M && PC && M->PickHitbox && Now - S->StepStartS > 0.5)
		{
			const float Sc = FMath::Max(1e-3f, FMath::Abs(M->PickHitbox->GetComponentScale().X));
			M->PickHitbox->SetCapsuleRadius(300.f / Sc);          // босс: |GetScaledCapsuleRadius| = 300
			S->Dist0 = (float)S->RangeTiles * 100.f + 300.f;      // = порог входа − 35 (RangeIn = дальность + радиус + 35)
			Place(S->Dist0);
			PC->SetTarget(M);
			PC->EngageMobFromClick(M);
			S->Phase = 1; S->PhaseS = Now;
		}
		if (S->Phase == 1 && M && S->Count(4026) > 0 && Now - S->PhaseS > 0.5)
		{
			S->Phase = 2; S->PhaseS = Now; S->Mark = S->Outs.Num();
		}
		if (S->Phase == 2 && M && PC)
		{
			// Бродит: +60 / −60 каждые 0.4 с — до «порог входа + 25»: за входом, но внутри удержания.
			const int32 Half = (int32)((Now - S->PhaseS) / 0.4);
			Place(S->Dist0 + ((Half % 2) ? -60.f : 60.f));
			if (PC->LastCombatRangeOut > 0.f) { S->MinRangeOut = FMath::Min(S->MinRangeOut, PC->LastCombatRangeOut); }
		}
		return S->Phase == 2 && Now - S->PhaseS > 5.0;
	}, [S]()
	{
		const int32 N06 = S->Count(4006), N28 = S->Count(4028);
		S->Expect(S->MinRangeOut >= S->Dist0 + 60.f + 40.f, FString::Printf(TEXT("удержание %.0f ≥ дальней точки брожения %.0f + 40 (пол гистерезиса: порог входа + 100)"),
			S->MinRangeOut, S->Dist0 + 60.f));
		S->Expect(N06 == 0 && N28 >= 3, FString::Printf(TEXT("за 5 с брожения: подходов 4006 — %d (ждём 0), ударов 4028 — %d (≥ 3): метания нет"), N06, N28));
	});

	// ─────────────────────── 5. 4028 несёт свою позицию ───────────────────────
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("4028: X/Z — своя позиция, AttackPosSelf %d"), K), 0.f, 12.f, [S, Reset, Feed, T, K]()
		{
			Reset();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.AttackPosSelf"), K);
			UGameInstance* G = S->GI(); AAsdaPlayerCharacter* C = S->Ch(); UAsdaNetSubsystem* Nn = S->N();
			if (!G || !C || !Nn) { return; }
			Nn->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 1);   // меч: 4028 через 0.9 с
			C->EquippedWeaponSubType = 1;
			S->Rid = S->NewRid();
			Feed(4013, AsdaTestPackets::MobSpawn4013(S->Rid, kMobType, T(1, 0), /*State*/1, kSpdDenom));
		}, [S]()
		{
			AAsdaMobActor* M = S->Mob(); AAsdaPlayerController* PC = S->PC();
			if (S->Phase == 0 && M && PC && FPlatformTime::Seconds() - S->StepStartS > 0.5)
			{
				PC->SetTarget(M);
				PC->EngageMobFromClick(M);
				S->Phase = 1;
			}
			return S->Count(4028) >= 1;
		}, [S, K]()
		{
			const FOut* O = S->First(4028);
			const FVector2D P = O ? FVector2D(RdF(O->Body, 32), RdF(O->Body, 36)) : FVector2D(-1.f, -1.f);
			const FVector2D Self = S->Ch() ? S->Ch()->GetGamePosXZ() : S->Home;
			const FVector2D MobP = S->MobGame(S->Rid);
			if (K) { S->Expect(O && FVector2D::Distance(P, Self) <= 2.f, FString::Printf(TEXT("4028 X/Z = своя позиция (%.0f,%.0f), свой (%.0f,%.0f)"), P.X, P.Y, Self.X, Self.Y)); }
			else   { S->Expect(O && FVector2D::Distance(P, MobP) <= 2.f, FString::Printf(TEXT("контроль, AttackPosSelf 0: в 4028 позиция цели (%.0f,%.0f) — прежний порт"), P.X, P.Y)); }
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.AttackPosSelf"), 1);
		});
	}

	// ─────────────────────── Итог ───────────────────────
	Add(TEXT("итог"), 0.f, 1.f, [S, Reset, Net]()
	{
		Reset();
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		Net->SelfWeaponItemId = S->SavedWeapon;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->SavedEquipped; }
		S->SelfScope.Reset();
		int32 N06 = 0, N26 = 0, N28 = 0;
		for (const FOut& O : S->Outs) { N06 += (O.Op == 4006); N26 += (O.Op == 4026); N28 += (O.Op == 4028); }
		AsdaSelfTestKit::Finish(S->World.Get(), TEXT("asda.Mob.SelfTestLivePos"), false, LogAsdaMobPosTest, TEXT("САМОТЕСТ-ПОЗМОБ"),
			S->Ok, S->Fail, *FString::Printf(TEXT(" (перехвачено 4006: %d, 4026: %d, 4028: %d)"), N06, N26, N28));
	}, []() { return true; }, []() {});

	// ─────────────────────── Спавн и исполнитель ───────────────────────
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
			UE_LOG(LogAsdaMobPosTest, Log, TEXT("САМОТЕСТ-ПОЗМОБ шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = El >= Stp.MinSec && (!Stp.Poll || Stp.Poll());
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMobLivePos(
	TEXT("asda.Mob.SelfTestLivePos"),
	TEXT("Самотест позиции монстра по пакетам сервера: 4013/4015 → видимая позиция = клетка сервера (по пути и в конце); "
	     "«призрак» 4013 исправляется живой клеткой 4017/4027 (порог 3 клетки); бой по призраку — подход вместо 4028; удержание "
	     "боя не шире допуска сервера; в 4026/4028 своя позиция. Контрольные пары asda.Mob.LiveCellSnap 0, "
	     "asda.Combat.RangeOutServerCap 0, asda.Combat.AttackPosSelf 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Mob.SelfTestLivePos"));
		FAsdaMobLivePosSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
