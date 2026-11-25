// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ НАВЫКОВ МОБОВ (30.10.2026) `asda.Mob.SelfTestSkill`. [ПОРТ] Только офлайн (`-AsdaNoNet`): входящие — штатный приём
//   `DebugFeedPacket` (= `HandlePacket` сокета). Свой персонаж — `Asda.World.FakeSelfSpawn`, сосед — `DebugSpawnFakePeer`, мобы — 4013.
//   Разбор — `Reports/mob_skills/FLOW.md`, автомат — `UAsdaMobSkillSubsystem`. Тела пакетов — по живым журналам игроков 19–25.10
//   (`Reports/mob_skills/server_side.md`): 8011 7 б (@2=0, @3=−1), 8012 9 + 29·N (+ хвост сервера 0..19 б), 8013 16 б.
//   Мобы: Pawn 295 (маска слотов 7: 18/19/20), Bishop 303 и Rook 319 (маска 15).
//   Ветки:
//     1) площадь, удар по мне (Pawn, навык 28): 17→16→15 без задержки (флаги фаз 0/1 = 0), клип слота 20 = anim_13, эффект атаки,
//        звук 13, на кадре B — эффект удара на мне, цифра 0xB, строка 20241; вздрагивания нет (у оригинала его в этой ветке нет);
//        конец клипа → боевая стойка, очередь пуста;
//     2) площадь: крит (0x05) — цифра 0xC и 20240; блок (0x09) — 0x10 и 20260; уклон (0x11) — DODGE; неуязвимость (0x21) — IMMUNE,
//        20261; без флага попадания (0x00) — ничего;
//     3) площадь по соседу (наблюдатель): цифра есть (режим показа «все»), строки чата нет; `asda.MobSkill.ObserverChatFix 0` — есть;
//        `asda.MobSkill.ChatFix 0`: блок — голое число в чат, уклон — без строки;
//     4) снаряд (Bishop, навык 33) по мне: пуск на t ≥ A, прилёт — цифра 0xB, вздрагивание, 20241, удар режима 2; крит — 0xA + 20240;
//        промах (урон 0) — MISS без вздрагивания; `asda.MobSkill.HitFxFix 0` — удара по прилёте нет (оригинал: режим 0);
//     5) 8011 → 8012 (Rook, навык 37: фаза каста): 17 → 16 (клип слота 19), 8012 того же навыка → голова в касте → 15;
//        8011 без 8012 — каст до таймаута (5000 мс) → 15;
//     6) перебивание: 4015 движение во время каста — состояние снято, очередь цела;
//     7) 8012 с хвостом-паддингом 13 б и двумя записями — целей 2; 20 записей + мусор — не больше 20;
//     8) 8013 лечение моба — HP полосы, эффект зелья; HP > максимума — до максимума;
//     9) навык 1 — пресет «пробуждение Кайи»; err ≠ 0 / навык 0 / моб типа 1 (записи mst нет) — без состояния;
//    10) `asda.MobSkill 0` — пакеты только в журнал.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "World/AsdaMobSkillSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaBlendAnimInstance.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobSkillTest, Log, All);

// Цвет строки «по тебе урон» (определение — AsdaWorldSubsystem_Damage.cpp, ключ asda.Chat.HitColorFix).
FColor AsdaChatColHitOnMe();

namespace AsdaMobSkillTestLocal
{
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013/4015
	constexpr int32 kPeer = 7832;            // сосед-наблюдатель
	constexpr int32 kPawn = 295, kBishop = 303, kRook = 319;

	struct FStep
	{
		FString Name; float TimeoutSec = 10.f;
		TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check;
	};

	struct FState
	{
		const TCHAR* Cmd = nullptr; const TCHAR* Tag = nullptr;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;
		FTimerHandle Tick; bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ TEXT(""), false };
		int32 Ok = 0, Fail = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false;
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;
		TArray<int32> Mobs, Peers;
		// Наблюдения шага.
		bool bSawFlinch = false; TArray<int32> StatesSeen;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaMobSkillSubsystem* MS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaMobSkillSubsystem>() : nullptr; }
		UAsdaChatSubsystem* Chat() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaChatSubsystem>() : nullptr; }
		UAsdaTextSubsystem* Txt() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaTextSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 Rid) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(Rid) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaMobSkillTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }   // исходное значение вернёт `Finish`
		void SpawnMob(int32 Rid, int32 Type, const FIntPoint& Tile) { Feed(4013, AsdaTestPackets::MobSpawn4013(Rid, Type, Tile, 1, kSpdDenom)); Mobs.AddUnique(Rid); }
		// ★ 30.10.2026 (порядок пакета): убрать мобов `Rids` тем же путём, что клиентский AoI-деспаун (`TickAoiDespawn`:
		//   фейд 1 с → Destroy фейд-компонентом, запись реестра гаснет сама). 4011 моба НЕ убирает (эталон `Recv_LogOutPacket`
		//   удаляет только игроков) — прежняя уборка им оставляла мобов следующему самотесту, а чужие мобы с теми же rid
		//   (волки `SelfTestObserverSync` 31051+) превращали 4013 подготовки в «rid найден»: снап без смены типа.
		void FadeOutMobs(const TArray<int32>& Rids)
		{
			for (const int32 R : Rids) { if (AAsdaMobActor* M = Mob(R)) { M->StartFadeOut(EAsdaMobFade::Aoi, AAsdaMobActor::AoiFadeSeconds); } }
		}
		// Сколько мобов из `Rids` ещё в реестре мира (в том числе гаснущих).
		int32 MobsLeft(const TArray<int32>& Rids) const { int32 C = 0; for (const int32 R : Rids) { if (Mob(R)) { ++C; } } return C; }
		void Mark() { if (UAsdaMobSkillSubsystem* M = MS()) { M->DebugClearEvents(); } bSawFlinch = false; StatesSeen.Reset(); }
		// Наблюдение кадра: вздрагивание своего персонажа, состояния автомата моба.
		void Sample(int32 Rid)
		{
			AAsdaPlayerCharacter* C = Ch();
			const UAsdaBlendAnimInstance* I = (C && C->BaseSkel) ? Cast<UAsdaBlendAnimInstance>(C->BaseSkel->GetAnimInstance()) : nullptr;
			if (I && I->IsHitFlinchActiveGT()) { bSawFlinch = true; }
			if (const UAsdaMobSkillSubsystem* M = MS())
			{
				const int32 St = M->DebugState(Rid);
				if (StatesSeen.Num() == 0 || StatesSeen.Last() != St) { StatesSeen.Add(St); }
			}
		}
		// Число событий журнала автомата вида `Kind`, в подробностях которых есть `Part`.
		int32 Ev(const TCHAR* Kind, const FString& Part = FString()) const
		{
			int32 C = 0; const UAsdaMobSkillSubsystem* M = MS(); if (!M) { return 0; }
			const FString Pre = FString(Kind) + TEXT("|");
			for (const FString& E : M->DebugEvents()) { if (E.StartsWith(Pre) && (Part.IsEmpty() || E.Contains(Part))) { ++C; } }
			return C;
		}
		int32 ChatLines(const FString& Needle) const { const UAsdaChatSubsystem* C = Chat(); return C ? C->DebugCountSystemLines(Needle) : 0; }
		FString Line(int32 Id, const FString& Name, int32 Dmg, bool bNum) const
		{
			const UAsdaTextSubsystem* T = Txt(); if (!T) { return FString(); }
			return bNum ? T->FormatById(Id, { Name }, { Dmg }) : T->FormatById(Id, { Name }, {});
		}
		FString StatesStr() const { FString S; for (int32 V : StatesSeen) { S += FString::Printf(TEXT("%d "), V); } return S; }
	};

	// 8011: навык @0, вид цели @2 (сервер — 0), цель @3 (сервер — −1), renderId моба @5.
	static TArray<uint8> Body8011(int32 Skill, int32 Rid)
	{
		TArray<uint8> B; B.SetNumZeroed(7);
		AsdaBytes::WrI16(&B[0], Skill); B[2] = 0; AsdaBytes::WrI16(&B[3], -1); AsdaBytes::WrI16(&B[5], Rid);
		return B;
	}
	struct FRec { int32 Idx; int32 Dmg; uint8 Flags; int32 TileX = 0; int32 TileZ = 0; uint8 Kind = 1; };
	// 8012: шапка 9 б {err, навык, моб, усл, вид цели, цель} + записи по 29 б {вид, индекс, урон, флаги, …, клетка @15/@17} + хвост.
	static TArray<uint8> Body8012(int32 Skill, int32 Rid, uint8 TargetType, int32 TargetIdx, const TArray<FRec>& Recs, int32 Pad = 0, uint8 Err = 0)
	{
		TArray<uint8> B; B.SetNumZeroed(9 + 29 * Recs.Num() + Pad);
		B[0] = Err; AsdaBytes::WrI16(&B[1], Skill); AsdaBytes::WrI16(&B[3], Rid); B[5] = 0; B[6] = TargetType; AsdaBytes::WrI16(&B[7], TargetIdx);
		for (int32 i = 0; i < Recs.Num(); ++i)
		{
			uint8* R = B.GetData() + 9 + 29 * i;
			R[0] = Recs[i].Kind; AsdaBytes::WrI16(R + 1, Recs[i].Idx); AsdaBytes::WrI32(R + 3, Recs[i].Dmg); R[7] = Recs[i].Flags;
			AsdaBytes::WrI16(R + 15, Recs[i].TileX); AsdaBytes::WrI16(R + 17, Recs[i].TileZ);
			AsdaBytes::WrI16(R + 19, -1);
		}
		for (int32 i = 0; i < Pad; ++i) { B[9 + 29 * Recs.Num() + i] = (uint8)(0xA0 + i); }   // хвост сервера — мусор
		return B;
	}
	// 8013: @0 i16 −1, @2 i32 −1, @6 renderId, @8 тип моба, @12 новое HP.
	static TArray<uint8> Body8013(int32 Rid, int32 Type, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(16);
		AsdaBytes::WrI16(&B[0], -1); AsdaBytes::WrI32(&B[2], -1); AsdaBytes::WrI16(&B[6], Rid); AsdaBytes::WrI32(&B[8], Type); AsdaBytes::WrI32(&B[12], Hp);
		return B;
	}
	// ★ 05.11.2026 (bug-mob-hp-zero) 4017 (100 б, `Recv_CombatHitAux@0xF33B60`): @0 тип, @2 rid, @4 флаги (0 — смерть, 3 — живой в бою),
	//   @8 28×i16 id состояний (−1), @92 HP, @96/@98 клетка — как в живом журнале Кайи 01.11 («4017: rid=0 type=286 flags=0x3 hp=…»).
	static TArray<uint8> Body4017(int32 Rid, int32 Type, const FIntPoint& Tile, int32 Flags, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], (int16)Type); AsdaBytes::WrI16(&B[2], (int16)Rid); AsdaBytes::WrI32(&B[4], Flags);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		AsdaBytes::WrI32(&B[92], Hp);
		AsdaBytes::WrI16(&B[96], (int16)Tile.X); AsdaBytes::WrI16(&B[98], (int16)Tile.Y);
		return B;
	}
	// ★ 05.11.2026 5257 (8 + 29 б, `Recv_SkillCast@0xF4E370`): @0 result 1, @1 кастер, @3 навык, @6 уровень; запись +0 вид 0, +1 rid,
	//   +3 урон, +7 флаги (бит 0 — попадание), +11 HP после, +19 состояние −1.
	static TArray<uint8> Body5257(int32 Caster, int32 Skill, int32 Rid, int32 Dmg, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1; AsdaBytes::WrI16(&B[1], (int16)Caster); AsdaBytes::WrI16(&B[3], (int16)Skill); AsdaBytes::WrI16(&B[6], 1);
		uint8* R = &B[8];
		AsdaBytes::WrI16(&R[1], (int16)Rid); AsdaBytes::WrI32(&R[3], Dmg); R[7] = 0x01; AsdaBytes::WrI32(&R[11], HpAfter); AsdaBytes::WrI16(&R[19], -1);
		return B;
	}
	// ★ 05.11.2026 4029 (49 б, `Recv_CastState@0xF41620`): @0 атакующий, @2 rid, @4 HP цели (HP − накопитель), @8 i32[4] очередь,
	//   @24 sub 0, @25 i16[4] второй канал (−1).
	static TArray<uint8> Body4029(int32 AtkU, int32 Rid, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(49);
		AsdaBytes::WrI16(&B[0], (int16)AtkU); AsdaBytes::WrI16(&B[2], (int16)Rid); AsdaBytes::WrI32(&B[4], Hp);
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
		return B;
	}
	// 4015 движение (kind 2): @0 −1, @2 тип, @4 rid, @6 = 2, клетки @7/@9 → @11/@13, скорость @15, темп @17.
	static TArray<uint8> Body4015Move(int32 Rid, int32 Type, const FIntPoint& Cur, const FIntPoint& Dst)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], -1); AsdaBytes::WrI16(&B[2], Type); AsdaBytes::WrI16(&B[4], Rid); B[6] = 2;
		AsdaBytes::WrI16(&B[7], Cur.X); AsdaBytes::WrI16(&B[9], Cur.Y); AsdaBytes::WrI16(&B[11], Dst.X); AsdaBytes::WrI16(&B[13], Dst.Y);
		AsdaBytes::WrI16(&B[15], kSpdDenom); AsdaBytes::WrI16(&B[17], 10000); AsdaBytes::WrI16(&B[19], 10000);
		AsdaBytes::WrI32(&B[25], 100);
		return B;
	}

	// 4015 kind=3 (замах по жертве): @0 жертва, @2 тип, @4 rid, @6 = 3, клетки, скорость, темп, результат @21, HP @25, отражение @31 = −1.
	static TArray<uint8> Body4015Hit(int32 Victim, int32 Rid, int32 Type, const FIntPoint& Cur, int32 Dmg)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], Victim); AsdaBytes::WrI16(&B[2], Type); AsdaBytes::WrI16(&B[4], Rid); B[6] = 3;
		AsdaBytes::WrI16(&B[7], Cur.X); AsdaBytes::WrI16(&B[9], Cur.Y); AsdaBytes::WrI16(&B[11], Cur.X); AsdaBytes::WrI16(&B[13], Cur.Y);
		AsdaBytes::WrI16(&B[15], kSpdDenom); AsdaBytes::WrI16(&B[17], 10000); AsdaBytes::WrI16(&B[19], 10000);
		AsdaBytes::WrI32(&B[21], Dmg); AsdaBytes::WrI32(&B[25], 100); AsdaBytes::WrI16(&B[31], -1);
		return B;
	}

	static void RunSteps(TSharedRef<FState> S, UWorld* W, const FAsdaSelfTestArgs& KitArgs, UAsdaNetSubsystem* Net)
	{
		TWeakObjectPtr<UWorld> WW(W);
		{
			FTimerHandle H; TWeakPtr<FState> WSg = S;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
			{
				UWorld* Wd = WW.Get(); const TSharedPtr<FState> Sg = WSg.Pin();
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
			if (!St->Steps.IsValidIndex(St->StepIdx)) { Wd->GetTimerManager().ClearTimer(St->Tick); AsdaSelfTestKit::Quit(Wd, St->bQuit); return; }
			const double Now = FPlatformTime::Seconds();
			FStep& Stp = St->Steps[St->StepIdx];
			if (!St->bStepStarted)
			{
				St->bStepStarted = true; St->StepStartS = Now;
				UE_LOG(LogAsdaMobSkillTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
				if (Stp.Start) { Stp.Start(); }
				return;
			}
			const double El = Now - St->StepStartS;
			const bool bDone = !Stp.Poll || Stp.Poll();
			if (!bDone && El < Stp.TimeoutSec) { return; }
			if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
			if (Stp.Check) { Stp.Check(); }
			++St->StepIdx; St->bStepStarted = false;
		}), 0.01f, true, KitArgs.Delay + 2.5f);
	}
}

struct FAsdaMobSkillSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaMobSkillSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaMobSkillTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Mob.SelfTestSkill");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-НАВЫК-МОБА");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto T = [S](int32 Dx, int32 Dy) { return S->HomeTile + FIntPoint(Dx, Dy); };
	constexpr int32 kRPawn = 31051, kRBishop = 31052, kRRook = 31053, kRWolf = 31054, kRPawn2 = 31055;
	// Все rid мобов прогона (подготовка + шаг лечения 31056/31057) — поле под них очищается до спауна.
	const TArray<int32> kAllRids = { kRPawn, kRBishop, kRRook, kRWolf, kRPawn2, 31056, 31057, 31058, 31059, 31060, 31061 };

	// ★ 30.10.2026 (порядок пакета): самотест не зависит от предыдущего в пакете — мобы с rid прогона, оставленные другим
	//   самотестом, гаснут штатным AoI-фейдом ДО спауна (иначе 4013 подготовки обновил бы чужого моба, не меняя тип).
	Add(TEXT("чистое поле"), 4.f, [S, kAllRids]()
	{
		S->FadeOutMobs(kAllRids);
	}, [S, kAllRids]() { return S->MobsLeft(kAllRids) == 0; }, [S, kAllRids]()
	{
		S->Expect(S->MobsLeft(kAllRids) == 0, FString::Printf(TEXT("rid 31051–31057 свободны до спауна (занято %d)"), S->MobsLeft(kAllRids)));
	});

	Add(TEXT("подготовка"), 6.f, [S, Net, T]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->WS() || !S->MS()) { S->Expect(false, TEXT("нет своего персонажа/мира/подсистемы навыков мобов")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->SetKey(TEXT("asda.MobSkill"), 1); S->SetKey(TEXT("asda.MobSkill.HitFxFix"), 1); S->SetKey(TEXT("asda.MobSkill.ChatFix"), 1);
		S->SetKey(TEXT("asda.MobSkill.ObserverChatFix"), 1); S->SetKey(TEXT("asda.Char.HitFlinch"), 1);
		S->SpawnMob(kRPawn, kPawn, T(3, 0)); S->SpawnMob(kRBishop, kBishop, T(0, 5)); S->SpawnMob(kRRook, kRook, T(-4, 0));
		S->SpawnMob(kRWolf, 1, T(0, -3)); S->SpawnMob(kRPawn2, kPawn, T(5, 5));
		if (UAsdaWorldSubsystem* WSx = S->WS()) { WSx->DebugSpawnFakePeer(kPeer, TEXT("Наблюдатель"), AsdaCoord::TileCenterGame(T(2, 2))); S->Peers.Add(kPeer); }
	}, [S]() { return S->El() > 1.0 && S->Mob(kRPawn) && S->Mob(kRBishop) && S->Mob(kRRook); }, [S]()
	{
		S->Expect(S->Mob(kRPawn) && S->Mob(kRBishop) && S->Mob(kRRook) && S->Mob(kRWolf), TEXT("мобы Pawn 295 / Bishop 303 / Rook 319 / волк 1 на месте"));
		S->Expect(UAsdaMobSkillSubsystem::SlotMaskForType(kPawn) == 7 && UAsdaMobSkillSubsystem::SlotMaskForType(kBishop) == 15,
			TEXT("маска слотов навыка: Pawn 7 (18/19/20), Bishop 15 (Mob list +0xAC)"));
		S->Expect(UAsdaMobSkillSubsystem::ClipForSlot(18) == 11 && UAsdaMobSkillSubsystem::ClipForSlot(21) == 14 && UAsdaMobSkillSubsystem::ClipForSlot(17) == -1,
			TEXT("слот → файл клипа: 18→11 … 21→14 (Effect_BuildJbbSuffixByType)"));
	});

	// ── 1. площадь по мне: Pawn навык 28 ──
	struct FCnt { int32 L20241 = 0, L20240 = 0, L20260 = 0, L20261 = 0; };
	TSharedRef<FCnt> C0 = MakeShared<FCnt>();
	auto Snap = [S, C0](int32 Rid)
	{
		AAsdaMobActor* M = S->Mob(Rid); const FString Nm = M ? M->MobName : FString();
		C0->L20241 = S->ChatLines(S->Line(20241, Nm, 120, true)); C0->L20240 = S->ChatLines(S->Line(20240, Nm, 300, true));
		C0->L20260 = S->ChatLines(S->Line(20260, Nm, 40, true)); C0->L20261 = S->ChatLines(S->Line(20261, Nm, 0, false));
	};
	Add(TEXT("1 площадь по мне: Pawn навык 28 (флаги 0x01, урон 120)"), 4.f, [S, Snap]()
	{
		S->Mark(); Snap(kRPawn);
		S->Feed(8012, Body8012(28, kRPawn, 1, S->Self, { { S->Self, 120, 0x01 } }, /*Pad=*/7));
	}, [S]() { S->Sample(kRPawn); return S->El() > 2.6; }, [S, C0]()
	{
		AAsdaMobActor* M = S->Mob(kRPawn); const FString Nm = M ? M->MobName : FString();
		S->Expect(S->Ev(TEXT("8012"), TEXT("очередь пуста → 17")) == 1, TEXT("8012 без 8011: очередь пуста → состояние 17"));
		S->Expect(S->Ev(TEXT("СОСТОЯНИЕ"), TEXT("|16")) >= 1 && S->Ev(TEXT("СОСТОЯНИЕ"), TEXT("|15")) >= 1,
			FString::Printf(TEXT("17 → 16 → 15 без задержки (флаги фаз 0/1 = 0); виденные состояния: %s"), *S->StatesStr()));
		S->Expect(S->Ev(TEXT("КЛИП"), TEXT("слот 20 → anim_13")) == 1, TEXT("клип атаки слота 21 − (msif+43 & 1) = 20 → anim_13"));
		S->Expect(S->Ev(TEXT("КЛИП"), TEXT("длина 0 мс")) == 0, TEXT("клип anim_13 у Pawn есть (длина > 0)"));
		S->Expect(S->Ev(TEXT("ЭФФЕКТ"), TEXT("Mon_MeleeDamStun_AC_00")) == 1, TEXT("эффект атаки Mon_MeleeDamStun_AC_00 на мобе (привязка 15)"));
		S->Expect(S->Ev(TEXT("УДАР"), TEXT("Mon_MeleeDamStun_BO_00")) == 1, TEXT("эффект удара Mon_MeleeDamStun_BO_00 на мне (кадр B = 1000 мс)"));
		S->Expect(S->Ev(TEXT("НЕТ_BP")) == 0, TEXT("все BP эффектов навыка собраны"));
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«120» стиль 11")) == 1, TEXT("цифра «120» стилем 0xB над жертвой"));
		S->Expect(S->ChatLines(S->Line(20241, Nm, 120, true)) == C0->L20241 + 1, TEXT("строка 20241 «навык … наносит тебе урон: 120»"));
		S->Expect(!S->bSawFlinch, TEXT("вздрагивания нет (у оригинала его в ветке площади нет — только у снаряда)"));
		S->Expect(S->MS()->DebugState(kRPawn) == 0 && S->MS()->DebugQueueLen(kRPawn) == 0 && M && M->IsCombatStance(),
			TEXT("конец клипа: очередь пуста, моб в боевой стойке (состояние 6)"));
	});

	// ── 2. площадь: крит / блок / уклон / неуязвимость / без попадания ──
	Add(TEXT("2 площадь: крит 0x05, блок 0x09, уклон 0x11, неуязвимость 0x21, без попадания 0x00"), 4.f, [S, Snap]()
	{
		S->Mark(); Snap(kRPawn);
		S->Feed(8012, Body8012(28, kRPawn, 1, S->Self, { { S->Self, 300, 0x05 }, { kPeer, 40, 0x09 }, { kPeer, 0, 0x11 }, { kPeer, 0, 0x21 }, { kPeer, 77, 0x00 } }));
	}, [S]() { S->Sample(kRPawn); return S->El() > 2.6; }, [S, C0]()
	{
		AAsdaMobActor* M = S->Mob(kRPawn); const FString Nm = M ? M->MobName : FString();
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«300» стиль 12")) == 1, TEXT("крит: «300» стилем 0xC"));
		S->Expect(S->ChatLines(S->Line(20240, Nm, 300, true)) == C0->L20240 + 1, TEXT("крит по мне: строка 20240"));
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«40» стиль 16")) == 1, TEXT("блок: «40» стилем 0x10"));
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«DODGE» стиль 11")) == 1 && S->Ev(TEXT("ЦИФРА"), TEXT("«IMMUNE» стиль 11")) == 1, TEXT("уклон DODGE и неуязвимость IMMUNE стилем 0xB"));
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«77»")) == 0, TEXT("запись без флага попадания (0x00) — без цифры"));
		S->Expect(S->ChatLines(S->Line(20260, Nm, 40, true)) == C0->L20260 && S->ChatLines(S->Line(20261, Nm, 0, false)) == C0->L20261,
			TEXT("по соседу строк чата нет (asda.MobSkill.ObserverChatFix 1)"));
	});

	// ── 3. наблюдатель: контрольные ключи чата ──
	Add(TEXT("3 наблюдатель: ObserverChatFix 0 и ChatFix 0"), 4.f, [S, Snap]()
	{
		S->Mark(); Snap(kRPawn);
		S->SetKey(TEXT("asda.MobSkill.ObserverChatFix"), 0); S->SetKey(TEXT("asda.MobSkill.ChatFix"), 1);
		S->Feed(8012, Body8012(28, kRPawn, 1, kPeer, { { kPeer, 120, 0x01 }, { kPeer, 40, 0x09 }, { kPeer, 0, 0x11 } }));
	}, [S]() { return S->El() > 2.6; }, [S, C0]()
	{
		AAsdaMobActor* M = S->Mob(kRPawn); const FString Nm = M ? M->MobName : FString();
		S->Expect(S->ChatLines(S->Line(20241, Nm, 120, true)) == C0->L20241 + 1, TEXT("ObserverChatFix 0: строка 20241 и у наблюдателя (как оригинал)"));
		S->Expect(S->ChatLines(S->Line(20260, Nm, 40, true)) == C0->L20260 + 1, TEXT("ChatFix 1: блок — строка 20260"));
		S->Expect(S->ChatLines(S->Line(20261, Nm, 0, false)) == C0->L20261 + 1, TEXT("ChatFix 1: уклон — строка 20261"));
	});
	Add(TEXT("3б ChatFix 0: блок — голое число, уклон — без строки"), 4.f, [S, Snap]()
	{
		S->Mark(); Snap(kRPawn);
		S->SetKey(TEXT("asda.MobSkill.ChatFix"), 0);
		S->Feed(8012, Body8012(28, kRPawn, 1, kPeer, { { kPeer, 4321, 0x09 }, { kPeer, 0, 0x11 } }));
	}, [S]() { return S->El() > 2.6; }, [S, C0]()
	{
		AAsdaMobActor* M = S->Mob(kRPawn); const FString Nm = M ? M->MobName : FString();
		S->Expect(S->ChatLines(TEXT("4321")) >= 1 && S->ChatLines(S->Line(20260, Nm, 4321, true)) == 0, TEXT("блок: в чат ушло голое «4321» (оригинал)"));
		S->Expect(S->ChatLines(S->Line(20261, Nm, 0, false)) == C0->L20261, TEXT("уклон: строки нет"));
		S->SetKey(TEXT("asda.MobSkill.ChatFix"), 1); S->SetKey(TEXT("asda.MobSkill.ObserverChatFix"), 1);
	});

	// ── 4. снаряд Bishop навык 33 ──
	Add(TEXT("4 снаряд Bishop навык 33 по мне (урон 80)"), 6.f, [S, Snap]()
	{
		S->Mark(); Snap(kRBishop);
		S->Feed(8012, Body8012(33, kRBishop, 1, S->Self, { { S->Self, 80, 0x01 } }));
	}, [S]() { S->Sample(kRBishop); return S->El() > 4.0; }, [S]()
	{
		AAsdaMobActor* M = S->Mob(kRBishop); const FString Nm = M ? M->MobName : FString();
		S->Expect(S->Ev(TEXT("КЛИП"), TEXT("слот 20 → anim_13")) == 1, TEXT("клип атаки Bishop: слот 20 → anim_13"));
		S->Expect(S->Ev(TEXT("СНАРЯД"), TEXT("Mon_BishopNormal_AR_00")) == 1, TEXT("снаряд Mon_BishopNormal_AR_00 пущен на t ≥ A (1000 мс)"));
		S->Expect(S->Ev(TEXT("СНАРЯД"), TEXT("режим удара 2")) == 1, TEXT("режим точки удара 2 (до обнуления +356, HitFxFix 1)"));
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«80» стиль 11")) == 1, TEXT("по прилёте цифра «80» стилем 0xB (жертва — я)"));
		S->Expect(S->bSawFlinch, TEXT("по прилёте я вздрогнул (0xA51C72)"));
		S->Expect(S->ChatLines(S->Line(20241, Nm, 80, true)) >= 1, TEXT("строка 20241"));
		S->Expect(S->Ev(TEXT("УДАР"), TEXT("Mon_BishopNormal_BO_00 режим 2")) == 1, TEXT("удар Mon_BishopNormal_BO_00 у ног (режим 2)"));
		S->Expect(S->MS()->DebugState(kRBishop) == 0 && S->MS()->DebugFlights(kRBishop) == 0, TEXT("навык окончен, снарядов нет"));
	});
	Add(TEXT("4б снаряд: крит 0x05 по мне"), 6.f, [S, Snap]()
	{
		S->Mark(); Snap(kRBishop);
		S->Feed(8012, Body8012(33, kRBishop, 1, S->Self, { { S->Self, 300, 0x05 } }));
	}, [S]() { S->Sample(kRBishop); return S->El() > 4.0; }, [S, C0]()
	{
		AAsdaMobActor* M = S->Mob(kRBishop); const FString Nm = M ? M->MobName : FString();
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«300» стиль 10")) == 1, TEXT("крит снаряда по мне: «300» стилем 0xA"));
		S->Expect(S->Ev(TEXT("ВЗДРОГ")) == 1 && S->bSawFlinch, TEXT("крит: вздрагивание (0xA5196B)"));
		S->Expect(S->ChatLines(S->Line(20240, Nm, 300, true)) == C0->L20240 + 1, TEXT("крит: строка 20240"));
	});
	Add(TEXT("4в снаряд: промах (урон 0)"), 6.f, [S, Snap]()
	{
		S->Mark(); Snap(kRBishop);
		S->Feed(8012, Body8012(33, kRBishop, 1, S->Self, { { S->Self, 0, 0x00 } }));
	}, [S]() { S->Sample(kRBishop); return S->El() > 4.0; }, [S, C0]()
	{
		AAsdaMobActor* M = S->Mob(kRBishop); const FString Nm = M ? M->MobName : FString();
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«MISS» стиль 5")) == 1, TEXT("промах снаряда: MISS стилем 5"));
		S->Expect(S->Ev(TEXT("ВЗДРОГ")) == 0 && !S->bSawFlinch, TEXT("промах: вздрагивания нет"));
		S->Expect(S->ChatLines(S->Line(20261, Nm, 0, false)) == C0->L20261 + 1, TEXT("промах: строка 20261"));
	});
	Add(TEXT("4г HitFxFix 0: удара по прилёте нет (оригинал)"), 6.f, [S]()
	{
		S->Mark(); S->SetKey(TEXT("asda.MobSkill.HitFxFix"), 0);
		S->Feed(8012, Body8012(33, kRBishop, 1, S->Self, { { S->Self, 50, 0x01 } }));
	}, [S]() { return S->El() > 4.0; }, [S]()
	{
		S->Expect(S->Ev(TEXT("СНАРЯД"), TEXT("режим удара 0")) == 1, TEXT("режим точки удара 0 (прочитан после обнуления)"));
		S->Expect(S->Ev(TEXT("УДАР")) == 0 && S->Ev(TEXT("ЦИФРА"), TEXT("«50»")) == 1, TEXT("цифра есть, эффекта удара нет"));
		S->SetKey(TEXT("asda.MobSkill.HitFxFix"), 1);
	});

	// ── 5. 8011 → 8012: Rook навык 37 (флаг каста) ──
	Add(TEXT("5 8011 → каст (слот 19) → 8012 того же навыка → 15"), 5.f, [S]()
	{
		S->Mark();
		S->Feed(8011, Body8011(37, kRRook));
	}, [S]()
	{
		S->Sample(kRRook);
		static bool bSent = false;
		if (S->El() < 0.1) { bSent = false; }
		if (!bSent && S->El() > 1.0)
		{
			bSent = true;
			S->Expect(S->MS()->DebugState(kRRook) == 16 && S->Ev(TEXT("КЛИП"), TEXT("слот 19 → anim_12")) == 1,
				FString::Printf(TEXT("через 1 с после 8011 — каст 16, клип слота 19 (состояния: %s)"), *S->StatesStr()));
			S->Feed(8012, Body8012(37, kRRook, 1, S->Self, {}));
		}
		return S->El() > 3.8;
	}, [S]()
	{
		S->Expect(S->Ev(TEXT("8012"), TEXT("голова в касте → 15")) == 1, TEXT("8012: голова очереди в касте (+404 = 2) → 15"));
		S->Expect(S->Ev(TEXT("ЭФФЕКТ"), TEXT("Mon_20mAreaPowerUp_AC_00")) == 1, TEXT("эффект атаки Mon_20mAreaPowerUp_AC_00"));
		S->Expect(S->MS()->DebugState(kRRook) == 0 && S->MS()->DebugQueueLen(kRRook) == 0, TEXT("навык окончен, очередь пуста"));
	});
	Add(TEXT("5б 8011 без 8012: каст до таймаута 5000 мс → 15"), 12.f, [S]()
	{
		S->Mark();
		S->Feed(8011, Body8011(37, kRRook));
	}, [S]() { S->Sample(kRRook); return S->El() > 5.3 && S->MS()->DebugState(kRRook) == 0; }, [S]()
	{
		S->Expect(S->StatesStr().Contains(TEXT("16 15")) || S->StatesStr().Contains(TEXT("16 0")),
			FString::Printf(TEXT("каст держится до таймаута и уходит в 15 (состояния: %s)"), *S->StatesStr()));
		S->Expect(S->Ev(TEXT("ЭФФЕКТ"), TEXT("Mon_20mAreaPowerUp_AC_00")) == 1 && S->MS()->DebugState(kRRook) == 0, TEXT("атака сыграна, навык окончен"));
	});

	// ── 6. перебивание движением ──
	Add(TEXT("6 4015 движение во время каста перебивает навык, очередь цела"), 3.f, [S, T]()
	{
		S->Mark();
		S->Feed(8011, Body8011(37, kRRook));
		FTimerHandle H; TWeakPtr<FState> Wk = S;
		if (UWorld* W = S->World.Get())
		{
			W->GetTimerManager().SetTimer(H, [Wk, T]() { if (TSharedPtr<FState> St = Wk.Pin()) { St->Feed(4015, Body4015Move(kRRook, kRook, T(-4, 0), T(-6, 0))); } }, 0.6f, false);
		}
	}, [S]() { return S->El() > 1.5; }, [S]()
	{
		S->Expect(S->Ev(TEXT("ПЕРЕБИТ")) == 1 && S->MS()->DebugState(kRRook) == 0, TEXT("движение сняло состояние навыка (эффект фазы погашен)"));
		S->Expect(S->MS()->DebugQueueLen(kRRook) == 1, TEXT("очередь при этом не чистится (эталон: SetActionState очередь не трогает)"));
	});

	// ── 6б. 8012 во время замаха моба: конец замаха навык НЕ перебивает (ревизия п.1) ──
	Add(TEXT("6б 8012 во время замаха моба (4015 kind=3) — навык доигрывает"), 5.f, [S, T]()
	{
		S->Mark();
		S->Feed(4015, Body4015Hit(S->Self, kRPawn2, kPawn, T(5, 5), 5));
		S->Feed(8012, Body8012(28, kRPawn2, 1, S->Self, { { S->Self, 33, 0x01 } }));
	}, [S]() { return S->El() > 3.5; }, [S]()
	{
		AAsdaMobActor* M = S->Mob(kRPawn2);
		S->Expect(S->Ev(TEXT("ПЕРЕБИТ")) == 0, TEXT("конец замаха/слив буфера не перебил навык"));
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«33» стиль 11")) == 1, TEXT("навык доигран: цифра «33» на кадре B"));
		S->Expect(S->MS()->DebugState(kRPawn2) == 0 && M && !M->IsAttackAnimActive(), TEXT("навык окончен, замаха нет"));
	});

	// ── 7. длина 8012 ──
	Add(TEXT("7 8012: хвост-паддинг и предел 20 записей"), 3.f, [S]()
	{
		S->Mark();
		S->Feed(8012, Body8012(17, kRPawn2, 1, S->Self, { { S->Self, 10, 0x01 }, { kPeer, 11, 0x01 } }, /*Pad=*/13));
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		S->Expect(S->Ev(TEXT("8012"), TEXT("записей 2 (80 б)")) == 1, TEXT("2 записи + 13 б хвоста: целей 2 (хвост отброшен)"));
		TArray<FRec> R22; for (int32 i = 0; i < 22; ++i) { R22.Add({ kPeer, 1, 0x01 }); }
		S->Mark();
		S->Feed(8012, Body8012(17, kRPawn2, 1, S->Self, R22, /*Pad=*/5));
		S->Expect(S->Ev(TEXT("8012"), TEXT("записей 20")) == 1, TEXT("22 записи: принято не больше 20"));
	});

	// ── 8. 8013 лечение ──
	Add(TEXT("8 8013 лечение моба"), 2.f, [S]()
	{
		S->Mark();
		S->Feed(8013, Body8013(kRBishop, kBishop, 60));
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AAsdaMobActor* M = S->Mob(kRBishop);
		S->Expect(M && M->CurHp == 60 && S->Ev(TEXT("8013"), TEXT("эффект зелья")) == 1, FString::Printf(TEXT("HP моба %d (ждём 60) + эффект зелья"), M ? M->CurHp : -1));
		const int32 Max = M ? (M->MobTypeMaxHp > 0 ? M->MobTypeMaxHp : M->MaxHp) : 0;
		S->Feed(8013, Body8013(kRBishop, kBishop, Max + 5000));
		S->Expect(M && M->CurHp == Max, FString::Printf(TEXT("HP выше максимума — до максимума %d (сейчас %d)"), Max, M ? M->CurHp : -1));
	});

	// ── 9. особые ветки ──
	Add(TEXT("9 навык 1 (пресет Кайи), err ≠ 0, навык 0, моб типа 1"), 2.f, [S]()
	{
		S->Mark();
		S->Feed(8012, Body8012(1, kRBishop, 1, S->Self, {}));
		S->Feed(8012, Body8012(33, kRBishop, 1, S->Self, { { S->Self, 5, 0x01 } }, 0, /*Err=*/3));
		S->Feed(8012, Body8012(0, kRBishop, 1, S->Self, {}));
		S->Feed(8012, Body8012(28, kRWolf, 1, S->Self, { { S->Self, 5, 0x01 } }));
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		S->Expect(S->Ev(TEXT("ЭФФЕКТ"), TEXT("Mon_KaiyaRevive_AC_00")) == 1, TEXT("навык 1: эффект пробуждения Кайи на мобе"));
		S->Expect(S->Ev(TEXT("8012_ОШИБКА")) == 1 && S->MS()->DebugState(kRBishop) == 0, TEXT("err ≠ 0: без состояния"));
		S->Expect(S->Ev(TEXT("8012_НЕТ_МОБА"), TEXT("навык 0")) == 1, TEXT("навык 0: без состояния"));
		S->Expect(S->Ev(TEXT("НЕТ_ЗАПИСИ"), TEXT("тип 1")) == 1, TEXT("моб типа 1: записи mst нет (индекс 0) — в очередь не кладётся"));
	});

	// ── 11. таблица «слот → файл» клипов моба (Effect_BuildJbbSuffixByType@0xF717C0) ──
	Add(TEXT("11 слот → файл клипа моба"), 1.f, nullptr, [S]() { return S->El() > 0.05; }, [S]()
	{
		const int32 Want[22] = { 0, 1, 2, 3, 4, 5, 5, 7, 8, -1, -1, -1, -1, -1, 9, 10, -1, -1, 11, 12, 13, 14 };
		int32 Bad = 0; FString Got;
		for (int32 s = 0; s < 22; ++s) { const int32 F = AAsdaMobActor::ClipFileForSlot(s); Got += FString::Printf(TEXT("%d "), F); if (F != Want[s]) { ++Bad; } }
		S->Expect(Bad == 0, FString::Printf(TEXT("слоты 0..21 → %s(6 → 5, 14 → 9, 15 → 10, 18–21 → 11–14, прочие пусты)"), *Got));
		S->SetKey(TEXT("asda.Mob.ClipSlotMap"), 0);
		S->Expect(AAsdaMobActor::ClipFileForSlot(6) == 6 && AAsdaMobActor::ClipFileForSlot(20) == 13, TEXT("ClipSlotMap 0: слот 6 → _6 (прежний порт), навык — как оригинал"));
		S->SetKey(TEXT("asda.Mob.ClipSlotMap"), 1);
	});

	// ── 12. слот замаха: вариант 1 → файл _5, крит/код < −1 → слот 7 ──
	struct FAnimName { static FString Of(const AAsdaMobActor* M)
	{
		const UAnimSingleNodeInstance* I = (M && M->Mesh) ? M->Mesh->GetSingleNodeInstance() : nullptr;
		const UAnimationAsset* A = I ? I->GetAnimationAsset() : nullptr;
		return A ? A->GetName() : FString();
	} };
	// Замахи подаются по одному (4015 kind=3), клип снимается через 0.4 с после каждого.
	struct FSwing { int32 Variant; int32 Dmg; bool bLegacy; const TCHAR* Want; const TCHAR* What; };
	static const FSwing kSwings[] = {
		{ 1, 40, false, TEXT("anim_05"), TEXT("вариант 1 → слот 6 = файл _5") },
		{ 0, (int32)0x80000032, false, TEXT("anim_07"), TEXT("крит (бит 31) → слот 7") },
		{ 0, -2, false, TEXT("anim_07"), TEXT("код −2 → слот 7") },
		{ 1, 40, true, TEXT("anim_06"), TEXT("ClipSlotMap 0, вариант 1 → _6 (прежний порт)") },
	};
	for (int32 k = 0; k < UE_ARRAY_COUNT(kSwings); ++k)
	{
		Add(FString::Printf(TEXT("12.%d замах моба: %s"), k + 1, kSwings[k].What), 3.f, [S, T, k]()
		{
			S->SetKey(TEXT("asda.Mob.ClipSlotMap"), kSwings[k].bLegacy ? 0 : 1);
			TArray<uint8> B = Body4015Hit(S->Self, kRPawn, kPawn, T(3, 0), kSwings[k].Dmg); B[29] = (uint8)kSwings[k].Variant;
			S->Feed(4015, B);
		}, [S]() { return S->El() > 0.4; }, [S, k]()
		{
			const FString A = FAnimName::Of(S->Mob(kRPawn));
			S->Expect(A.EndsWith(kSwings[k].Want), FString::Printf(TEXT("%s: клип %s"), kSwings[k].What, *A));
			S->SetKey(TEXT("asda.Mob.ClipSlotMap"), 1);
		});
	}
	Add(TEXT("12.5 пауза — замах доигран"), 3.f, nullptr, [S]() { return S->El() > 2.5; }, nullptr);

	// ── 13. вариант «меш +0x04 + клипы базы +0x24»: Haughty Pawn 297 (меш pawn02, клипы pawn01) ──
	constexpr int32 kPawnH = 297, kRPawnH = 31056, kRPawnH0 = 31057;
	Add(TEXT("13 Haughty Pawn 297: скелетный вариант pawn02__pawn01, навык с анимацией"), 5.f, [S, T]()
	{
		S->Mark();
		S->SpawnMob(kRPawnH, kPawnH, T(-2, 4));
	}, [S]()
	{
		static bool bSent = false;
		if (S->El() < 0.1) { bSent = false; }
		if (!bSent && S->El() > 1.0 && S->Mob(kRPawnH)) { bSent = true; S->Feed(8012, Body8012(28, kRPawnH, 1, S->Self, { { S->Self, 21, 0x01 } })); }
		return S->El() > 4.0;
	}, [S, T]()
	{
		AAsdaMobActor* M = S->Mob(kRPawnH);
		const USkeletalMesh* Sk = (M && M->Mesh) ? M->Mesh->GetSkeletalMeshAsset() : nullptr;
		S->Expect(Sk && Sk->GetName().Contains(TEXT("pawn02-a_00__mon_s_0_pawn01-a_00")),
			FString::Printf(TEXT("скелетал моба: %s (был статик без анимации)"), Sk ? *Sk->GetName() : TEXT("НЕТ")));
		S->Expect(S->Ev(TEXT("КЛИП"), TEXT("слот 20 → anim_13")) == 1 && S->Ev(TEXT("КЛИП"), TEXT("длина 0 мс")) == 0, TEXT("навык 28: клип слота 20 = anim_13 из набора pawn01, длина > 0"));
		S->Expect(S->Ev(TEXT("ЦИФРА"), TEXT("«21»")) == 1, TEXT("навык доигран (цифра «21»)"));
		S->SetKey(TEXT("asda.Mob.ClipBaseModel"), 0);
		S->SpawnMob(kRPawnH0, kPawnH, T(-3, 4));
		AAsdaMobActor* M0 = S->Mob(kRPawnH0);
		S->Expect(M0 && !(M0->Mesh && M0->Mesh->GetSkeletalMeshAsset()) && M0->StaticMeshComp && M0->StaticMeshComp->GetStaticMesh(),
			TEXT("ClipBaseModel 0: прежний статик pawn02 без скелета"));
		S->SetKey(TEXT("asda.Mob.ClipBaseModel"), 1);
	});

	// ── 14. цвет строк чата −33517 = 0xFFFF7D13 ──
	Add(TEXT("14 цвет строки «по тебе урон» −33517 = FF7D13"), 1.f, nullptr, [S]() { return S->El() > 0.05; }, [S]()
	{
		AAsdaMobActor* MH = S->Mob(31056);
		const FString L = S->Line(20241, MH ? MH->MobName : FString(), 21, true);
		FColor C; const bool bHas = S->Chat() && !L.IsEmpty() && S->Chat()->DebugFindLastSystemLineColor(L, C);
		S->Expect(bHas && C == FColor(0xFF, 0x7D, 0x13, 0xFF), FString::Printf(TEXT("строка 20241 навыка моба: цвет %s"), bHas ? *C.ToHex() : TEXT("нет строки")));
		S->Expect(AsdaChatColHitOnMe() == FColor(0xFF, 0x7D, 0x13, 0xFF), TEXT("общий цвет 4102/PvP/DoT «по тебе урон» = FF7D13"));
		S->SetKey(TEXT("asda.Chat.HitColorFix"), 0);
		S->Expect(AsdaChatColHitOnMe() == FColor(0xFF, 0x7B, 0x93, 0xFF), TEXT("HitColorFix 0: прежний FF7B93"));
		S->SetKey(TEXT("asda.Chat.HitColorFix"), 1);
	});

	// ── 15. ★ 05.11.2026 (bug-mob-hp-zero) Кайя: лечение 8013 посреди боя — тела и числа из живого журнала игрока 01.11 23:08:53–23:09:20
	//   (LogDrop 20261101-233556, Кайя тип 286 max 28224): 4017 hp 11138 → 8013 28224 → запись 5257, посчитанная сервером ДО лечения
	//   (урон 2040, HP после 11160) → 4017 28191 / 4029 27792 / 4017 27781 → 5257 урон 698, HP после 24836 → 4017 24000 → смерть 4017 flags 0.
	//   Три прогона: все ключи 1 (полоса после лечения не ниже 20000 ни на кадре, итог 24000, смерть — 0); только HealEpoch 0
	//   (подъём большого разрыва LiftLargeGap после истечения непродлеваемого прогноза — итог 24000); все ключи 0 (дефект до 05.11:
	//   полоса остаётся ≤ 11160 при серверных 24000).
	//   Четвёртый прогон — лечение ИИ BATTLE_HEAL сервера (+20 % max = 5644 от 11138, NpcServer sub_446A50), остальные значения — со
	//   сдвигом от HP лечения как в журнале (−33, −432, −443, запись 698 → −3388, −4175, −4224).
	struct FKaiyaRun { int32 Rid; int32 KeysMode; int32 Heal; const TCHAR* What; };   // KeysMode: 1 — все 1, 2 — HealEpoch 0, 0 — все 0; Heal — HP после 8013
	static const FKaiyaRun kKaiya[] = {
		{ 31058, 1, 28224, TEXT("ключи 05.11 = 1") },
		{ 31059, 2, 28224, TEXT("asda.Combat.HealEpoch 0 (только непродлеваемый прогноз + подъём большого разрыва)") },
		{ 31060, 0, 28224, TEXT("все три ключа 0 — дефект до 05.11") },
		{ 31061, 1, 11138 + 5644, TEXT("лечение ИИ BATTLE_HEAL +20 % max, ключи 05.11 = 1") },
	};
	struct FKaiyaObs { int32 Phase = 0; int32 MinAfterHeal = INT32_MAX; int32 AtFinal = -1; bool bDead = false; };   // наблюдения прогона
	constexpr int32 kKaiyaType = 286, kKaiyaMax = 28224;   // Кайя (alpen), Monster.ai +0x2D = DT_MobStats = 28224
	for (const FKaiyaRun& KR : kKaiya)
	{
		TSharedRef<FKaiyaObs> O = MakeShared<FKaiyaObs>();
		const FKaiyaRun Run = KR;   // копия для лямбд
		Add(FString::Printf(TEXT("15 Кайя: лечение 8013 посреди боя, %s"), Run.What), 12.f, [S, T, Run, O]()
		{
			S->Mark();
			const int32 On = Run.KeysMode == 0 ? 0 : 1;
			S->SetKey(TEXT("asda.Combat.ForecastNoExtend"), On);
			S->SetKey(TEXT("asda.Combat.LiftLargeGap"), On);
			S->SetKey(TEXT("asda.Combat.HealEpoch"), Run.KeysMode == 1 ? 1 : 0);
			TArray<uint8> B = AsdaTestPackets::MobSpawn4013(Run.Rid, kKaiyaType, T(1, -6), 1, kSpdDenom);
			AsdaBytes::WrI32(&B[9], kKaiyaMax);   // живой 4013: hp=28224/28224
			S->Feed(4013, B); S->Mobs.AddUnique(Run.Rid);
		}, [S, T, Run, O]()
		{
			const double E = S->El();
			const FIntPoint Tile = T(1, -6);
			auto At = [&](int32 Ph, double Sec) { if (O->Phase == Ph && E >= Sec) { ++O->Phase; return true; } return false; };
			if (At(0, 1.0)) { S->Feed(4017, Body4017(Run.Rid, kKaiyaType, Tile, 3, 11138)); }
			if (At(1, 1.5)) { S->Feed(8013, Body8013(Run.Rid, kKaiyaType, Run.Heal)); }
			if (At(2, 1.6)) { S->Feed(5257, Body5257(kPeer, 1243, Run.Rid, 2040, 11160)); }
			if (At(3, 2.6)) { S->Feed(4017, Body4017(Run.Rid, kKaiyaType, Tile, 3, Run.Heal - 33)); S->Feed(4029, Body4029(kPeer, Run.Rid, Run.Heal - 432)); }
			if (At(4, 3.0)) { S->Feed(4017, Body4017(Run.Rid, kKaiyaType, Tile, 3, Run.Heal - 443)); }
			if (At(5, 4.0)) { S->Feed(5257, Body5257(kPeer, 707, Run.Rid, 698, Run.Heal - 3388)); }
			if (At(6, 5.0)) { S->Feed(4017, Body4017(Run.Rid, kKaiyaType, Tile, 3, Run.Heal - 4175)); }
			if (At(7, 6.5)) { S->Feed(4017, Body4017(Run.Rid, kKaiyaType, Tile, 3, Run.Heal - 4224)); }
			if (At(8, 7.5)) { if (AAsdaMobActor* M = S->Mob(Run.Rid)) { O->AtFinal = M->CurHp; } S->Feed(4017, Body4017(Run.Rid, kKaiyaType, Tile, 0, 0)); }
			if (const AAsdaMobActor* M = S->Mob(Run.Rid); M && O->Phase >= 2 && O->Phase <= 8) { O->MinAfterHeal = FMath::Min(O->MinAfterHeal, M->CurHp); }
			if (O->Phase >= 9 && E >= 9.0) { const AAsdaMobActor* M = S->Mob(Run.Rid); O->bDead = !M || (M->IsDead() && M->CurHp <= 0); return true; }
			return false;
		}, [S, Run, O]()
		{
			if (Run.KeysMode == 0)
			{
				S->Expect(O->AtFinal >= 0 && O->AtFinal <= 11160, FString::Printf(TEXT("дефект воспроизведён: после лечения полоса %d ≤ 11160 при серверных 24000"), O->AtFinal));
			}
			else
			{
				S->Expect(O->AtFinal == Run.Heal - 4224, FString::Printf(TEXT("итог полосы %d = серверным %d"), O->AtFinal, Run.Heal - 4224));
				if (Run.KeysMode == 1)
				{
					S->Expect(O->MinAfterHeal >= Run.Heal - 4224, FString::Printf(TEXT("после лечения полоса не опускалась ниже серверного итога %d (минимум %d) — запись до лечения цифру показала, полосу не уронила"), Run.Heal - 4224, O->MinAfterHeal));
				}
				S->Expect(O->MinAfterHeal > 0, FString::Printf(TEXT("живой босс: полоса ни на кадре не была 0 (минимум %d)"), O->MinAfterHeal));
			}
			S->Expect(O->bDead, TEXT("4017 flags 0: смерть, полоса 0"));
			S->SetKey(TEXT("asda.Combat.ForecastNoExtend"), 1); S->SetKey(TEXT("asda.Combat.LiftLargeGap"), 1); S->SetKey(TEXT("asda.Combat.HealEpoch"), 1);
		});
	}

	// ── 10. ключ asda.MobSkill 0 ──
	Add(TEXT("10 asda.MobSkill 0 — пакеты только в журнал"), 2.f, [S]()
	{
		S->Mark(); S->SetKey(TEXT("asda.MobSkill"), 0);
		S->Feed(8012, Body8012(28, kRPawn, 1, S->Self, { { S->Self, 9, 0x01 } }));
		S->Feed(8011, Body8011(37, kRRook));
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		S->Expect(S->Ev(TEXT("8012_ВЫКЛ")) == 1 && S->Ev(TEXT("8011_ВЫКЛ")) == 1 && S->Ev(TEXT("СОСТОЯНИЕ")) == 0, TEXT("автомат не запускался"));
		S->SetKey(TEXT("asda.MobSkill"), 1);
	});

	Add(TEXT("итог"), 4.f, [S, Net]()
	{
		// ★ 30.10.2026: мобы — AoI-фейдом (4011 их не убирает), соседи — штатным 4011 (игроков он убирает).
		S->FadeOutMobs(S->Mobs);
		for (const int32 Idx : S->Peers) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Idx); S->Feed(4011, B); }
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3 && S->MobsLeft(S->Mobs) == 0; }, [S]()
	{
		S->Expect(S->MobsLeft(S->Mobs) == 0, FString::Printf(TEXT("мобы прогона убраны (в реестре осталось %d)"), S->MobsLeft(S->Mobs)));
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaMobSkillTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMobSkill(
	TEXT("asda.Mob.SelfTestSkill"),
	TEXT("Самотест навыков мобов (30.10): 8011/8012/8013 — автомат 17/16/15, клипы слотов 18–21, эффекты фаз, снаряд, цифры и строки чата, "
	     "вздрагивание, перебивание, разбор длины, лечение, пресет Кайи; ключи asda.MobSkill / HitFxFix / ChatFix / ObserverChatFix. "
	     "Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Mob.SelfTestSkill"));
		FAsdaMobSkillSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
