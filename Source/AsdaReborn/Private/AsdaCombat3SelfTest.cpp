// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТЫ «ТРИ БАГА БОЯ» (29.10.2026): рывок Charge 537 `asda.Skill.SelfTestCharge` и вздрагивание от удара моба
//   `asda.Char.SelfTestHitFlinch`. [ПОРТ] Только офлайн (`-AsdaNoNet`): входящие — штатный приём `DebugFeedPacket` (= `HandlePacket`
//   сокета), исходящие — перехват `AsdaSelfTestKit::ArmCapture` (настоящие отправители 8215 / 4008 / 4006). Свой персонаж —
//   `Asda.World.FakeSelfSpawn`, соседи — `DebugSpawnFakePeer`, мобы — запись 4013. Разбор — `AsdaSkillFxSubsystem.cpp` (`TickDash`),
//   `AsdaWorldSubsystem_Hit.cpp` (`AsdaMobHitVictimReaction`), `AsdaBlendAnimInstance.h` (вздрагивание).
//   Charge:
//     1) свой 5257 «Charge» по мобу в 6 клетках (флаги 0x03): персонаж едет к мобу (на трети пути — между стартом и концом), стан
//        3 на мобе появляется только по прибытии; по прибытии 8215 `[0]=1, rid, 537, тайл`, 4008 вид 15, моб отброшен ~100,
//        стоит (+6986), показ закрыт на COMBO0;
//     2) флаги 0x01 (состояние не выпало): рывок есть, 8215 нет, над мобом «MISS», стана нет;
//     3) контроль `asda.Skill.Dash 0`: персонаж стоит, стан сразу (прежний порт);
//     4) наблюдатель: 5257 соседа — сосед едет к мобу, 8215 уходит и от нас (как эталон), 4008 вида 15 нет;
//     5) подход автоатаки к дальней цели при заявке навыка (состояние 25): 4006 нет; навык освободился — 4006 уходит;
//        контроль `asda.Combat.ApproachWaitsSkill 0` — 4006 уходит сразу;
//     6) ★ 05.11: Charge по ИГРОКУ (путь PvP/дуэли) — 5257 вид 1 и 5272 стана 3 в ОДНОМ тике (так шлёт сервер: `sub_471C70` →
//        `sub_477F60(…, 3, цель)` для 537/597): свой едет к цели и встаёт за 100 до неё, 4008 вид 15 с конечной точкой, 8215 нет,
//        на цели состояние 3.
//     `shot=<папка>` — кадры шага 1 (старт / середина пути / прибытие / после): `charge_<n>_<фаза>.png`. В середине пути
//     проверяется, что на скелете стоит клип рывка (показ идёт, клип не стойки/бега).
//   Вздрагивание:
//     1) удар моба 4015 kind=3 по мне (урон 50): поворот кости «4» лёг, через 0.3 с — снят; покой → боевая стойка;
//     2) уклонение (−1): вздрагивания нет, стойка — есть;
//     3) барьер 51 на мне: вздрагивания нет;
//     4) удар по соседу — сосед вздрагивает;
//     5) контроль `asda.Char.HitFlinch 0` / `asda.Char.HitStance 0`: ни вздрагивания, ни стойки (прежний порт);
//     6) повторный старт во время вздрагивания не перезапускает (эталон: флаг 0x200).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "Skill/AsdaSkillFxSubsystem.h"
#include "UI/AsdaSkillDatabase.h"   // ★ 05.11.2026 отлёт: выбор навыка по behaviorType
#include "UI/AsdaChatSubsystem.h"   // ★ 08.11.2026: шаг 7 Charge — строка нотиса 13218
#include "Text/AsdaTextSubsystem.h" // ★ 08.11.2026: текст нотиса 13218 по id
#include "Text/AsdaLang.h"          // ★ 08.11.2026: язык в имени кадра
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaBlendAnimInstance.h"
#include "World/AsdaFloatingTextSubsystem.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "AsdaEffectActor.h"
#include "World/AsdaProjectileActor.h"        // ★ 05.11: снаряд гранаты (дуга)
#include "Camera/PlayerCameraManager.h"       // ★ 05.11: мобы гранаты — перед камерой
#include "EngineUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"                      // FScreenshotRequest — кадры рывка (`shot=`)
#include "Animation/AnimSingleNodeInstance.h"  // клип на скелете в середине рывка
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCombat3Test, Log, All);

namespace AsdaCombat3TestLocal
{
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013/4015
	// ★ R5-fix7: наибольшая законная скорость хода моба теста, юн/с: добег 3 × скорости сервера (100/kSpdDenom юн/мс).
	constexpr double kWalkMaxUuS = 3.0 * 100.0 / kSpdDenom * 1000.0;
	constexpr int32 kP = 7831;               // сосед

	struct FOut { uint16 Op = 0; TArray<uint8> Payload; };   // перехваченный исходящий

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
		uint32 Gen = 0; TArray<FOut> Outs; int32 OutFrom = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false;
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;
		TArray<int32> Peers, Mobs;
		int32 WasWeaponSub = 99;
		// Наблюдения шага.
		FVector2D Start2D = FVector2D::ZeroVector; FVector MobPos0 = FVector::ZeroVector;
		bool bSampled = false, bSawFlinch = false, bStateEarly = false; float MidDist = -1.f; int32 FloatBase = 0, FloatMax = 0;
		float TintMax = 0.f;   // ★ максимум CPD[1] (покраснение) у тела жертвы за шаг
		FString ShotDir;       // ★ 05.11: `shot=<папка>` — кадры рывка шага 1 (пусто — без кадров)
		int32 ShotMask = 0;    // снятые кадры шага (биты)
		FString MidClip;       // клип на скелете своего в середине рывка
		bool bMidActive = false;   // показ навыка шёл в середине рывка
		bool bPressed = false;     // шаг 6: навык нажат (ждём заявку 5256)
		bool bWasStandSkill = false;
		int32 RushLines0 = 0;      // ★ 08.11: шаг 7 — строк 13218 в журнале на старте шага   // шаг 6: прежнее `bStandCaptureSkillUse` (заявка 5256 офлайн уходит в перехват, а не в сокет)
		double GameT0 = 0.0;       // ★ 05.11: игровое время мира на подаче 5257 — замер «середины рывка» по нему (рывок идёт по dt мира;
		                           //   со снятием кадров игра идёт 2–4 кадра/с, и настенные 0.3 с наступали раньше первого тика рывка)

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* Fx() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		UAsdaSkillFxSubsystem* SF() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr; }
		UAsdaFloatingTextSubsystem* FT() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaFloatingTextSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaPlayerCharacter* PeerCh(int32 Idx) const { const UAsdaWorldSubsystem* W = WS(); return W ? Cast<AAsdaPlayerCharacter>(W->ActorByTarget(EAsdaTargetKind::Character, Idx)) : nullptr; }
		AAsdaMobActor* Mob(int32 Rid) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(Rid) : nullptr; }
		UAsdaBlendAnimInstance* Anim(AAsdaPlayerCharacter* C) const { return (C && C->BaseSkel) ? Cast<UAsdaBlendAnimInstance>(C->BaseSkel->GetAnimInstance()) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		double GEl() const { const UWorld* Wd = World.Get(); return Wd ? Wd->GetTimeSeconds() - GameT0 : 0.0; }
		// 4206 (13 б) команда 1 — PvP-режим своего: подтип @7 (1 — дуэль, 0 — отбой), соперник @8 (эталон netTradeRequestHandler@0xF4A190).
		void Duel(int32 Rival, bool bOn)
		{
			TArray<uint8> B; B.SetNumZeroed(13);
			AsdaBytes::WrI16(&B[0], Self); B[6] = bOn ? 1 : 0; B[7] = bOn ? 1 : 0; AsdaBytes::WrI32(&B[8], bOn ? Rival : -1); B[12] = 1;
			Feed(4206, B);
		}
		void MarkGame() { if (const UWorld* Wd = World.Get()) { GameT0 = Wd->GetTimeSeconds(); } }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaCombat3Test, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		TArray<FOut> OutsOf(uint16 Op) const { TArray<FOut> R; for (int32 i = OutFrom; i < Outs.Num(); ++i) { if (Outs[i].Op == Op) { R.Add(Outs[i]); } } return R; }
		// Кадр `charge_<Tag>.png` в папку `shot=` (один раз на бит `Bit` за шаг).
		FString ShotPrefix = TEXT("charge");   // ★ 05.11: префикс имени кадра (у гранаты — «grenade»)
		void Shot(int32 Bit, const TCHAR* Name)
		{
			if (ShotDir.IsEmpty() || (ShotMask & Bit)) { return; }
			ShotMask |= Bit;
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *ShotPrefix, Name)), false, false);
		}
		bool CharHas(int32 Uidx, int32 State) const
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return false; }
			for (const FAsdaEffectEntry& E : F->GetList(Uidx)) { if (E.Type == 0 && E.Id == State) { return true; } }
			return false;
		}
		void Mark()
		{
			OutFrom = Outs.Num(); bSampled = false; bSawFlinch = false; bStateEarly = false; MidDist = -1.f; TintMax = 0.f;
			ShotMask = 0; MidClip.Reset(); bMidActive = false; bPressed = false;
			FloatBase = FT() ? FT()->DebugLiveCount() : 0; FloatMax = 0;
		}
		bool MobHas(int32 Rid, int32 State) const
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return false; }
			for (const FAsdaEffectEntry& E : F->GetMobList(Rid)) { if (E.Type == 0 && E.Id == State) { return true; } }
			return false;
		}
		void SpawnMob(int32 Rid, const FIntPoint& Tile) { Feed(4013, AsdaTestPackets::MobSpawn4013(Rid, kMobType, Tile, 1, kSpdDenom)); Mobs.AddUnique(Rid); }
		void SpawnPeer(int32 Idx, const TCHAR* Name, const FIntPoint& Tile)
		{
			if (UAsdaWorldSubsystem* W = WS()) { W->DebugSpawnFakePeer(Idx, Name, AsdaCoord::TileCenterGame(Tile)); Peers.AddUnique(Idx); }
		}
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }   // исходное значение вернёт `Finish` (RestoreAll)
		void CalmSkill(int32 Code) { if (UAsdaSkillUseSubsystem* S = SU()) { S->SimulateCastResult5257(0, Code); } }
		static float Tint(const AAsdaPlayerCharacter* C)
		{
			if (!C || !C->BaseSkel) { return 0.f; }
			const TArray<float>& D = C->BaseSkel->GetCustomPrimitiveData().Data;
			return D.IsValidIndex(1) ? D[1] : 0.f;
		}
		int32 CountFx(const TCHAR* Part) const
		{
			int32 N = 0; UWorld* W = World.Get(); if (!W) { return 0; }
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It) { if (It->GetClass()->GetName().Contains(Part)) { ++N; } }
			return N;
		}
		void TrackFloat() { if (UAsdaFloatingTextSubsystem* F = FT()) { FloatMax = FMath::Max(FloatMax, F->DebugLiveCount() - FloatBase); } }
	};

	// 5257 (шапка 8 б + 29·N): [0] результат, [1..2] кастер, [3..4] навык, [5] 1, [6..7] уровень; запись — вид@0 (0 = моб), цель@1,
	//   урон@3, флаги@7, стеки buff0/1/2 @8..10, HP после @11, состояние @19 (−1). Живое тело Player15 25.10 05:11:49.375.
	static TArray<uint8> Body5257(int32 Caster, int32 Skill, int32 Rid, uint8 Flags, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		uint8* R = B.GetData() + 8;
		R[0] = 0; AsdaBytes::WrI16(R + 1, Rid); AsdaBytes::WrI32(R + 3, 0); R[7] = Flags; R[8] = 1; R[9] = 1; R[10] = 0;
		AsdaBytes::WrI32(R + 11, HpAfter); AsdaBytes::WrI16(R + 19, -1);
		return B;
	}
	// 5257 с одной записью любого вида: Kind 1 — персонаж (userIdx), 0 — моб (renderId).
	static TArray<uint8> Body5257K(int32 Caster, int32 Skill, uint8 Kind, int32 Idx, int32 Dmg, uint8 Flags, int32 HpAfter)
	{
		TArray<uint8> B = Body5257(Caster, Skill, Idx, Flags, HpAfter);
		uint8* R = B.GetData() + 8;
		R[0] = Kind; AsdaBytes::WrI32(R + 3, Dmg);
		return B;
	}
	// 4015 kind=3 (37 б): @0 жертва userIdx, @2 тип моба, @4 rid, @6 = 3, @7/@9 клетка, @11/@13 цель, @15 скорость, @17/@19 темп,
	//   @21 результат удара, @25 HP, @29 вариант замаха, @30 тип удара, @31 отражение (−1), @33 его урон.
	static TArray<uint8> Body4015Hit(int32 Victim, int32 Rid, const FIntPoint& Cur, int32 Dmg)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], Victim); AsdaBytes::WrI16(&B[2], kMobType); AsdaBytes::WrI16(&B[4], Rid); B[6] = 3;
		AsdaBytes::WrI16(&B[7], Cur.X); AsdaBytes::WrI16(&B[9], Cur.Y); AsdaBytes::WrI16(&B[11], Cur.X); AsdaBytes::WrI16(&B[13], Cur.Y);
		AsdaBytes::WrI16(&B[15], kSpdDenom); AsdaBytes::WrI16(&B[17], 10000); AsdaBytes::WrI16(&B[19], 10000);
		AsdaBytes::WrI32(&B[21], Dmg); AsdaBytes::WrI32(&B[25], 100); B[29] = 0; B[30] = 0; AsdaBytes::WrI16(&B[31], -1);
		return B;
	}

	// Общий исполнитель (как у `asda.Skill.SelfTestBowFx`): спавн своего персонажа, шаги по таймеру, итог.
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
				UE_LOG(LogAsdaCombat3Test, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
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

	// Подготовка: перехват, свой номер, «в игре», оружие подтипа 1 (одноручный меч — строка клипа 537 0|1).
	static void Prepare(TSharedRef<FState> S, UAsdaNetSubsystem* Net)
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS() || !S->Fx()) { S->Expect(false, TEXT("нет своего персонажа/мира/эффектов после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin(); if (!St) { return; }
			FOut& O = St->Outs.AddDefaulted_GetRef(); O.Op = Op; if (P && Len > 0) { O.Payload.Append(P, Len); }
		}, (uint16)0, S->Cmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->WasWeaponSub = C->EquippedWeaponSubType; C->EquippedWeaponSubType = 1;
	}

	static void Cleanup(TSharedRef<FState> S, UAsdaNetSubsystem* Net)
	{
		for (const int32 Rid : S->Mobs) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Rid); S->Feed(4011, B); }
		for (const int32 Idx : S->Peers) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Idx); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); P->bAutoAttack = false; P->ExitCombatStance(); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->WasWeaponSub; }
		// ★ 31.10.2026: офлайн гранта 4007 нет — «ход в пути» (бит 2) после подхода (шаг 5в Charge) снимаем, иначе следующий самотест
		//   в той же игре упирается в гейт хода навыка (`AsdaSkillUseSubsystem.cpp`: «4006 ещё без ответа 4007») и 5256 не уходит.
		Net->ClearMoveRequestInFlight(TEXT("самотест Combat3: итог"));
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
	}
}

struct FAsdaCombat3SelfTest
{
	static void RunCharge(const TArray<FString>& Args, UWorld* W);
	static void RunFlinch(const TArray<FString>& Args, UWorld* W);
	static void RunFace(const TArray<FString>& Args, UWorld* W);
	static void RunGrenade(const TArray<FString>& Args, UWorld* W);
};

// ═══ asda.Skill.SelfTestCharge ══════════════════════════════════════════════════════════════════════════════════════════
void FAsdaCombat3SelfTest::RunCharge(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaCombat3TestLocal;
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestCharge");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256, 8215 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-CHARGE");
	for (const FString& A : Args) { if (A.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase)) { S->ShotDir = A.Mid(5); IFileManager::Get().MakeDirectory(*S->ShotDir, true); } }
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto T = [S](int32 Dx, int32 Dy) { return S->HomeTile + FIntPoint(Dx, Dy); };
	constexpr int32 kRid1 = 30951, kRid2 = 30952, kRid3 = 30953, kRid4 = 30954, kRid5 = 30955;

	Add(TEXT("подготовка"), 5.f, [S, Net, T]()
	{
		Prepare(S, Net);
		S->SetKey(TEXT("asda.Skill.Dash"), 1); S->SetKey(TEXT("asda.Skill.OwnHitStates"), 1); S->SetKey(TEXT("asda.Mob.ControlFreeze"), 1);
		S->SetKey(TEXT("asda.Combat.ApproachWaitsSkill"), 1);
		S->SpawnMob(kRid1, T(6, 0));
	}, [S]() { return S->El() > 1.0 && S->Mob(kRid1); }, [S]()
	{
		S->Expect(S->Mob(kRid1) != nullptr, TEXT("моб в 6 клетках"));
	});

	// ★ 05.11: кадр «до» — отдельным шагом: первый скриншот даёт долгий кадр, внутри шага 1 он сдвигал замер середины пути.
	Add(TEXT("кадр до рывка"), 2.f, [S]() { S->Mark(); S->Shot(1, TEXT("1_before")); }, [S]() { return S->El() > 1.0; }, nullptr);

	// ── 1. свой Charge: рывок, стан на прибытии, 8215, 4008 вид 15, отброс ──
	Add(TEXT("1 свой Charge (флаги 0x03)"), 4.f, [S]()
	{
		S->Mark();
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaMobActor* M = S->Mob(kRid1);
		if (!C || !M) { return; }
		S->Start2D = C->GetGamePosXZ(); S->MobPos0 = M->GetActorLocation();
		S->Feed(5257, Body5257(S->Self, 537, kRid1, 0x03, 391));
		S->MarkGame();
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		// ★ 05.11: «в пути» — ЛЮБОЙ кадр рывка, где персонаж строго между стартом и целью (кадры со снятием снимков длятся до 0.8 с,
		//   один такой кадр перескакивал через 0.3 с игрового времени раньше тика рывка — замер по одной точке был ложным).
		if (C)
		{
			const float Full0 = FVector2D::Distance(S->Start2D, AsdaCoord::UeToGame2D(S->MobPos0));
			const float D = FVector2D::Distance(C->GetGamePosXZ(), S->Start2D);
			if (D > 0.15f * Full0 && D < 0.85f * Full0) { S->MidDist = D; }
		}
		if (C && !S->bSampled && S->GEl() > 0.30)
		{
			S->bSampled = true;
			S->bStateEarly = S->MobHas(kRid1, 3);
			S->bMidActive = S->SF() && S->SF()->IsSkillVisualActive(C);
			const UAnimSingleNodeInstance* Sn = C->BaseSkel ? C->BaseSkel->GetSingleNodeInstance() : nullptr;
			S->MidClip = Sn ? GetNameSafe(Sn->GetAnimationAsset()) : FString(TEXT("нет одноузлового инстанса"));
			S->Shot(2, TEXT("2_mid"));
		}
		if (S->GEl() > 0.75) { S->Shot(4, TEXT("3_arrive")); }
		if (S->GEl() > 1.40) { S->Shot(8, TEXT("4_after")); }
		return S->GEl() > 1.6 && S->El() > 1.6;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaMobActor* M = S->Mob(kRid1);
		const FVector2D Mob0G = AsdaCoord::UeToGame2D(S->MobPos0);
		const float Full = FVector2D::Distance(S->Start2D, Mob0G);
		S->Expect(S->MidDist > 0.15f * Full && S->MidDist < 0.85f * Full,
			FString::Printf(TEXT("в кадре рывка персонаж в пути: %.0f из %.0f (рывок, не телепорт и не стоит)"), S->MidDist, Full));
		S->Expect(!S->bStateEarly, TEXT("стан 3 НЕ наложен до прибытия (Skill_ApplyEffectResult — фаза 1)"));
		S->Expect(S->bMidActive, FString::Printf(TEXT("в середине рывка идёт показ навыка, клип на скелете: %s"), *S->MidClip));
		UE_LOG(LogAsdaCombat3Test, Log, TEXT("%s позиция до (%.0f,%.0f), моб (%.0f,%.0f), после (%.0f,%.0f)"), S->Tag, S->Start2D.X, S->Start2D.Y,
			AsdaCoord::UeToGame2D(S->MobPos0).X, AsdaCoord::UeToGame2D(S->MobPos0).Y, C ? C->GetGamePosXZ().X : 0.f, C ? C->GetGamePosXZ().Y : 0.f);
		const float EndDist = C ? FVector2D::Distance(C->GetGamePosXZ(), Mob0G) : -1.f;
		S->Expect(C && EndDist < 60.f, FString::Printf(TEXT("персонаж прибыл к мобу: до исходной точки моба %.0f"), EndDist));
		S->Expect(S->MobHas(kRid1, 3) && S->Fx()->HasMobMoveLock(kRid1), TEXT("по прибытии на мобе стан 3, моб стоит (+6986)"));
		const float Push = M ? FVector2D::Distance(AsdaCoord::UeToGame2D(M->GetActorLocation()), Mob0G) : -1.f;
		S->Expect(Push > 80.f && Push < 120.f, FString::Printf(TEXT("моб отброшен на %.0f (эталон +100 по курсу кастера)"), Push));
		const TArray<FOut> D = S->OutsOf(8215);
		const bool b8215 = D.Num() == 1 && D[0].Payload.Num() == 17 && D[0].Payload[0] == 1 && AsdaBytes::RdI16(&D[0].Payload[7]) == kRid1
			&& AsdaBytes::RdI16(&D[0].Payload[9]) == 537 && M
			&& AsdaBytes::RdI16(&D[0].Payload[13]) == AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(M->GetActorLocation())).X;
		S->Expect(b8215, FString::Printf(TEXT("8215: штук %d, тело 17 б [0]=1 rid навык 537 тайл X новой точки моба"), D.Num()));
		const TArray<FOut> St = S->OutsOf(4008);
		S->Expect(St.Num() == 1 && St[0].Payload.Num() == 27 && St[0].Payload[0] == 15, FString::Printf(TEXT("4008 вид 15 (точка прибытия): штук %d"), St.Num()));
		S->Expect(S->OutsOf(4006).Num() == 0, TEXT("ход 4006 во время рывка не уходил"));
		S->Expect(C && !S->SF()->IsSkillVisualActive(C), TEXT("показ закрыт (клип обрывается на COMBO0)"));
		S->CalmSkill(537);
	});

	// ── 2. флаги 0x01: MISS, без 8215 и стана ──
	Add(TEXT("2 Charge без состояния (флаги 0x01)"), 4.f, [S]()
	{
		S->Mark();
		S->SpawnMob(kRid2, S->HomeTile + FIntPoint(0, 6));
	}, [S]()
	{
		if (S->Mob(kRid2) && !S->bSampled)
		{
			S->Mark(); S->bSampled = true;
			if (AAsdaPlayerCharacter* C = S->Ch()) { S->Start2D = C->GetGamePosXZ(); }
			S->Feed(5257, Body5257(S->Self, 537, kRid2, 0x01, 300));
			S->StepStartS = FPlatformTime::Seconds();
		}
		S->TrackFloat();
		return S->bSampled && S->El() > 1.4;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		const float Moved = C ? FVector2D::Distance(C->GetGamePosXZ(), S->Start2D) : -1.f;
		S->Expect(Moved > 300.f, FString::Printf(TEXT("рывок есть и без состояния: сдвиг %.0f"), Moved));
		S->Expect(S->OutsOf(8215).Num() == 0, TEXT("8215 не уходит (бит 0x02 не стоит)"));
		S->Expect(!S->MobHas(kRid2, 3), TEXT("стана нет"));
		S->Expect(S->FloatMax >= 1, FString::Printf(TEXT("исход «MISS» над мобом (живых надписей до %d)"), S->FloatMax));
		S->CalmSkill(537);
	});

	// ── 3. контроль asda.Skill.Dash 0 ──
	Add(TEXT("3 контроль asda.Skill.Dash 0"), 3.f, [S]()
	{
		S->Mark();
		S->SetKey(TEXT("asda.Skill.Dash"), 0);
		S->SetKey(TEXT("asda.Skill.HitFrameAlways"), 0);   // ★ 31.10: прежний порт выпускал записи 5257 сразу (ключ кадра удара — AsdaSkillFxSubsystem.cpp)
		S->SpawnMob(kRid3, S->HomeTile + FIntPoint(-6, 0));
	}, [S]()
	{
		if (S->Mob(kRid3) && !S->bSampled)
		{
			S->Mark(); S->bSampled = true;
			if (AAsdaPlayerCharacter* C = S->Ch()) { S->Start2D = C->GetGamePosXZ(); }
			S->Feed(5257, Body5257(S->Self, 537, kRid3, 0x03, 391));
			S->StepStartS = FPlatformTime::Seconds();
		}
		return S->bSampled && S->El() > 0.5;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		const float Moved = C ? FVector2D::Distance(C->GetGamePosXZ(), S->Start2D) : -1.f;
		S->Expect(Moved < 30.f && S->MobHas(kRid3, 3) && S->OutsOf(8215).Num() == 0,
			FString::Printf(TEXT("прежний порт: персонаж стоит (сдвиг %.0f), стан сразу, 8215 нет (дефект воспроизведён)"), Moved));
		S->SetKey(TEXT("asda.Skill.Dash"), 1);
		S->SetKey(TEXT("asda.Skill.HitFrameAlways"), 1);
		S->CalmSkill(537);
	});

	// ── 4. наблюдатель: Charge соседа ──
	Add(TEXT("4 Charge соседа (наблюдатель)"), 4.f, [S]()
	{
		S->Mark();
		S->SpawnPeer(kP, TEXT("Рыцарь"), S->HomeTile + FIntPoint(2, -4));
		S->SpawnMob(kRid4, S->HomeTile + FIntPoint(8, -4));
	}, [S]()
	{
		AAsdaPlayerCharacter* P = S->PeerCh(kP);
		if (P && S->Mob(kRid4) && !S->bSampled)
		{
			S->Mark(); S->bSampled = true;
			P->EquippedWeaponSubType = 1;
			S->Start2D = P->GetGamePosXZ(); S->MobPos0 = S->Mob(kRid4)->GetActorLocation();
			S->Feed(5257, Body5257(kP, 537, kRid4, 0x03, 391));
			S->StepStartS = FPlatformTime::Seconds();
		}
		return S->bSampled && S->El() > 1.4;
	}, [S]()
	{
		AAsdaPlayerCharacter* P = S->PeerCh(kP);
		const float End = P ? FVector2D::Distance(P->GetGamePosXZ(), AsdaCoord::UeToGame2D(S->MobPos0)) : -1.f;
		S->Expect(P && End < 60.f, FString::Printf(TEXT("сосед доехал до моба: %.0f"), End));
		const TArray<FOut> D = S->OutsOf(8215);
		S->Expect(D.Num() == 1 && D[0].Payload.Num() == 17 && AsdaBytes::RdI16(&D[0].Payload[1]) == kP,
			FString::Printf(TEXT("8215 уходит и от наблюдателя (дизасм 0xB3B7F8 без проверки «свой»), кастер в теле = сосед: штук %d"), D.Num()));
		S->Expect(S->OutsOf(4008).Num() == 0, TEXT("4008 вида 15 наблюдатель не шлёт (только свой)"));
		S->Expect(!S->MobHas(kRid4, 3), TEXT("состояние чужого удара не ставится наблюдателем (Skill_ApplyEffectResult — только свой)"));
	});

	// ── 5. подход автоатаки ждёт навык ──
	Add(TEXT("5 подход ждёт навык (заявка 25)"), 3.f, [S]()
	{
		S->Mark();
		S->SpawnMob(kRid5, S->HomeTile + FIntPoint(0, -12));
	}, [S]()
	{
		AAsdaPlayerController* PC = S->PC();
		if (PC && S->Mob(kRid5) && !S->bSampled)
		{
			S->Mark(); S->bSampled = true;
			if (UAsdaSkillUseSubsystem* SU = S->SU()) { SU->DebugSetCastState(25, 537); }
			PC->TargetActor = S->Mob(kRid5); PC->bAutoAttack = true;
			S->StepStartS = FPlatformTime::Seconds();
		}
		return S->bSampled && S->El() > 0.8;
	}, [S]()
	{
		S->Expect(S->OutsOf(4006).Num() == 0, FString::Printf(TEXT("при заявке навыка подход не идёт: 4006 штук %d"), S->OutsOf(4006).Num()));
		S->CalmSkill(537);
		if (UAsdaSkillUseSubsystem* SU = S->SU()) { SU->DebugSetCastState(0, -1); }
	});
	Add(TEXT("5б навык освободился — подход"), 2.f, nullptr, [S]() { return S->OutsOf(4006).Num() > 0; }, [S]()
	{
		S->Expect(S->OutsOf(4006).Num() > 0, TEXT("навык освободился — 4006 подхода ушёл"));
		if (AAsdaPlayerController* PC = S->PC()) { PC->bAutoAttack = false; PC->CancelRoute(); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->StopMoving(); }
		// Офлайн ответа-гранта 4007 нет — «ход в пути» (бит 2) снимаем, иначе следующий подход ждал бы гранта.
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест Charge: сброс сценария 5б")); }
	});
	Add(TEXT("5в контроль asda.Combat.ApproachWaitsSkill 0"), 2.f, [S]()
	{
		S->Mark();
		S->SetKey(TEXT("asda.Combat.ApproachWaitsSkill"), 0);
		if (UAsdaSkillUseSubsystem* SU = S->SU()) { SU->DebugSetCastState(25, 537); }
		if (AAsdaPlayerController* PC = S->PC()) { PC->TargetActor = S->Mob(kRid5); PC->bAutoAttack = true; }
	}, [S]() { return S->OutsOf(4006).Num() > 0; }, [S]()
	{
		S->Expect(S->OutsOf(4006).Num() > 0, TEXT("прежний порт: подход посреди заявки навыка (дефект воспроизведён)"));
		S->SetKey(TEXT("asda.Combat.ApproachWaitsSkill"), 1);
		if (UAsdaSkillUseSubsystem* SU = S->SU()) { SU->DebugSetCastState(0, -1); }
		if (AAsdaPlayerController* PC = S->PC()) { PC->bAutoAttack = false; PC->CancelRoute(); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->StopMoving(); }
	});

	// ── 6. Charge по игроку: 5257 вид 1 + 5272 стана 3 в одном тике ──
	constexpr int32 kV = 7832;
	Add(TEXT("6 Charge по игроку (5257 вид 1 + 5272 стан)"), 5.f, [S]()
	{
		S->Mark();
		if (AAsdaPlayerCharacter* C = S->Ch()) { S->SpawnPeer(kV, TEXT("Цель"), AsdaCoord::GameToTile(C->GetGamePosXZ()) + FIntPoint(6, 0)); }
		// Путь игрока: навык изучен (5251), MP полная — нажатие уходит заявкой 5256 (`DispatchByKind`, как с панели).
		TArray<uint8> L; L.SetNumZeroed(34); AsdaBytes::WrI16(&L[0], 537); L[2] = 5; L[3] = 1;
		S->Feed(5251, L);
		if (UAsdaNetSubsystem* Nn = S->N())
		{
			Nn->SetVitalsMp(S->Self, 99999, 99999, S->Cmd, true);
			// Офлайн гранта 4007 нет: «ход в пути» (бит 2) от подхода шага 5в снимаем, иначе нажатие упрётся в гейт хода навыка.
			Nn->ClearMoveRequestInFlight(TEXT("самотест Charge: перед шагом 6"));
			S->bWasStandSkill = Nn->bStandCaptureSkillUse; Nn->bStandCaptureSkillUse = true;   // как вариант `fight` AnimAudit
		}
		// Враждебный навык по игроку эталон шлёт только в PvP (`[160] == 1`, skill_buff.c:31922): сервер включает дуэль с целью — 4206.
		S->Duel(kV, true);
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerCharacter* V = S->PeerCh(kV);
		if (C && V && !S->bSampled && !S->bPressed && S->El() > 0.5)   // 5251/4206 разбираются тиком — нажатие после них
		{
			// Путь игрока: выбор цели-игрока и нажатие навыка — дальше ждём 5256; ответ сервера 5257 + 5272 — в одном тике.
			S->bPressed = true;
			if (AAsdaPlayerController* PC = S->PC()) { PC->SetTarget(V); }
			if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(537, -1); }
			return false;
		}
		if (C && V && !S->bSampled && S->OutsOf(5256).Num() > 0)
		{
			S->bSampled = true;
			S->Start2D = C->GetGamePosXZ(); S->MobPos0 = V->GetActorLocation();
			S->Feed(5257, Body5257K(S->Self, 537, 1, kV, 120, 0x03, 900));
			TArray<uint8> St; St.SetNumZeroed(14);   // 5272: userIdx@0, состояние@2, стек@9, вид@10, остаток@11 (с)
			AsdaBytes::WrI16(&St[0], kV); AsdaBytes::WrI16(&St[2], 3); St[9] = 1; St[10] = 0; AsdaBytes::WrI16(&St[11], 4);
			S->Feed(5272, St);
			S->StepStartS = FPlatformTime::Seconds();
			S->MarkGame();
		}
		if (S->bSampled && C)
		{
			const float Full0 = FVector2D::Distance(S->Start2D, AsdaCoord::UeToGame2D(S->MobPos0));
			const float D = FVector2D::Distance(C->GetGamePosXZ(), S->Start2D);
			if (D > 0.15f * Full0 && D < 0.85f * Full0) { S->MidDist = D; }
		}
		return S->bSampled && S->El() > 1.4 && S->GEl() > 1.4;
	}, [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		const FVector2D V0 = AsdaCoord::UeToGame2D(S->MobPos0);
		const float Full = FVector2D::Distance(S->Start2D, V0);
		const float End = C ? FVector2D::Distance(C->GetGamePosXZ(), V0) : -1.f;
		S->Expect(S->OutsOf(5256).Num() == 1, FString::Printf(TEXT("нажатие по цели-игроку ушло заявкой 5256: штук %d"), S->OutsOf(5256).Num()));
		S->Expect(S->MidDist > 0.15f * Full && S->MidDist < Full, FString::Printf(TEXT("в кадре рывка свой в пути к игроку: %.0f из %.0f"), S->MidDist, Full));
		S->Expect(End > 60.f && End < 140.f, FString::Printf(TEXT("свой встал перед игроком: до цели %.0f (эталон — позиция цели − курс × 100)"), End));
		const float FromStart = C ? FVector2D::Distance(C->GetGamePosXZ(), S->Start2D) : -1.f;
		S->Expect(FromStart > 0.f && FromStart < Full, FString::Printf(TEXT("встал по СВОЮ сторону цели (курс — к цели): от старта %.0f < %.0f"), FromStart, Full));
		// 4008 вид 15 — ровно один (ещё один 4008 — штатный «стоп перед заявкой» незакрытого хода, вид 0).
		int32 N15 = 0; for (const FOut& O : S->OutsOf(4008)) { if (O.Payload.Num() == 27 && O.Payload[0] == 15) { ++N15; } }
		S->Expect(N15 == 1, FString::Printf(TEXT("4008 вид 15 с конечной точкой: штук %d (всего 4008: %d)"), N15, S->OutsOf(4008).Num()));
		S->Expect(S->OutsOf(8215).Num() == 0, TEXT("8215 нет (ветка игрока — отброса нет)"));
		S->Expect(S->CharHas(kV, 3), TEXT("на цели-игроке состояние 3 (стан от 5272 сервера)"));
		S->Duel(kV, false);
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->bStandCaptureSkillUse = S->bWasStandSkill; }
		UE_LOG(LogAsdaCombat3Test, Log, TEXT("%s игрок: до (%.0f,%.0f), цель (%.0f,%.0f), после (%.0f,%.0f)"), S->Tag, S->Start2D.X, S->Start2D.Y, V0.X, V0.Y,
			C ? C->GetGamePosXZ().X : 0.f, C ? C->GetGamePosXZ().Y : 0.f);
		S->CalmSkill(537);
	});

	// ── 7. ★ 08.11.2026 (пачка 18 баг 8, `asda.Skill.RushRangeGate`): минимальная дистанция рывка. Эталон `skillUseOnTargetDispatch`
	//   (skill_buff.c:31619): `invItemTargetInRange(…, 600.0)` — до цели ближе 600 → нотис 13218, заявки нет. Путь игрока: выбор моба,
	//   нажатие навыка (`DispatchByKind`), заявка 5256 офлайн уходит в перехват. Кадры отказа — `Reports/batch18/skills/`.
	struct FRush { const TCHAR* Name; int32 Skill; int32 Dx; bool bKey; bool bRefuse; int32 Rid; const TCHAR* Shot; };
	const FRush kRush[] = {
		{ TEXT("7а 537 по мобу в 300: нотис 13218, заявки нет"),                     537, 3, true,  true,  31731, TEXT("537_close") },
		{ TEXT("7б контроль asda.Skill.RushRangeGate 0: 537 в 300 — заявка ушла"),   537, 3, false, false, 31732, nullptr },
		{ TEXT("7в 537 по мобу в 900: заявка ушла, нотиса нет"),                      537, 9, true,  false, 31733, nullptr },
		{ TEXT("7г 597 «Mad Charge» по мобу в 300: нотис 13218, заявки нет"),        597, 3, true,  true,  31734, TEXT("597_close") },
	};
	for (const FRush& K : kRush)
	{
		auto CountTooClose = [S]()
		{
			const UGameInstance* G = S->GI();
			const UAsdaTextSubsystem* T = G ? G->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
			const UAsdaChatSubsystem* Ch = G ? G->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
			const FString L = T ? T->FormatById(13218, {}, {}) : FString();
			return (Ch && !L.IsEmpty()) ? Ch->DebugCountSystemLines(L) : -1;
		};
		Add(K.Name, 4.f, [S, K, CountTooClose]()
		{
			S->Mark();
			S->bPressed = false; S->bSampled = false;
			S->SetKey(TEXT("asda.Skill.RushRangeGate"), K.bKey ? 1 : 0);
			if (AAsdaPlayerCharacter* C = S->Ch()) { S->SpawnMob(K.Rid, AsdaCoord::GameToTile(C->GetGamePosXZ()) + FIntPoint(K.Dx, 0)); }
			TArray<uint8> L; L.SetNumZeroed(34); AsdaBytes::WrI16(&L[0], K.Skill); L[2] = 1; L[3] = 1;   // 5251: навык изучен
			S->Feed(5251, L);
			if (UAsdaNetSubsystem* Nn = S->N())
			{
				Nn->SetVitalsMp(S->Self, 99999, 99999, S->Cmd, true);
				Nn->ClearMoveRequestInFlight(TEXT("самотест Charge: шаг 7"));
				S->bWasStandSkill = Nn->bStandCaptureSkillUse; Nn->bStandCaptureSkillUse = true;
			}
			S->RushLines0 = CountTooClose();
		}, [S, K]()
		{
			AAsdaMobActor* M = S->Mob(K.Rid);
			if (M && !S->bPressed && S->El() > 0.5)
			{
				S->bPressed = true; S->StepStartS = FPlatformTime::Seconds();
				if (AAsdaPlayerController* PC = S->PC()) { PC->SetTarget(M); }
				if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(K.Skill, -1); }
			}
			return S->bPressed && S->El() > 1.0;
		}, [S, K, CountTooClose]()
		{
			const AAsdaPlayerCharacter* C = S->Ch(); const AAsdaMobActor* M = S->Mob(K.Rid);
			const float Dist = (C && M) ? FVector::Dist(C->GetActorLocation(), M->GetActorLocation()) : -1.f;
			const int32 NLines = CountTooClose() - S->RushLines0;
			const int32 N5256 = S->OutsOf(5256).Num();
			UE_LOG(LogAsdaCombat3Test, Log, TEXT("%s [рывок] навык %d, ключ %d: до цели %.0f, заявок 5256 %d, нотисов 13218 +%d"), S->Tag, K.Skill, K.bKey ? 1 : 0, Dist, N5256, NLines);
			if (K.bRefuse)
			{
				S->Expect(Dist > 0.f && Dist < 600.f, FString::Printf(TEXT("до цели %.0f < 600"), Dist));
				S->Expect(N5256 == 0 && NLines == 1, FString::Printf(TEXT("отказ до заявки: 5256 %d (ждём 0), строка 13218 +%d (ждём 1)"), N5256, NLines));
				if (K.Shot)
				{
					const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/batch18/skills"));
					IFileManager::Get().MakeDirectory(*Dir, true);
					FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), K.Shot), true, false);
				}
			}
			else
			{
				S->Expect(N5256 == 1 && NLines == 0, FString::Printf(TEXT("заявка ушла: 5256 %d (ждём 1), строк 13218 +%d (ждём 0); до цели %.0f"), N5256, NLines, Dist));
			}
			S->SetKey(TEXT("asda.Skill.RushRangeGate"), 1);
			if (UAsdaNetSubsystem* Nn = S->N()) { Nn->bStandCaptureSkillUse = S->bWasStandSkill; }
			if (AAsdaPlayerController* PC = S->PC()) { PC->bAutoAttack = false; PC->CancelRoute(); }
			if (AAsdaPlayerCharacter* Ch = S->Ch()) { Ch->StopMoving(); }
			S->CalmSkill(K.Skill);
			TArray<uint8> B4; B4.SetNumZeroed(2); AsdaBytes::WrI16(&B4[0], K.Rid); S->Feed(4011, B4);
		});
	}

	Add(TEXT("итог"), 2.f, [S, Net]() { Cleanup(S, Net); }, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaCombat3Test, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

// ═══ asda.Char.SelfTestHitFlinch ════════════════════════════════════════════════════════════════════════════════════════
void FAsdaCombat3SelfTest::RunFlinch(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaCombat3TestLocal;
	const TCHAR* const kCmd = TEXT("asda.Char.SelfTestHitFlinch");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-FLINCH");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	constexpr int32 kRidA = 30961, kRidB = 30962, kRidC = 30963, kRidD = 30964, kRidE = 30965, kRidW = 30966;

	// Поллер «удар → вздрагивание»: ловит хотя бы одну оценку позы с лёгшим поворотом и проверяет снятие через 0.3 с.
	auto HitPoll = [S](AAsdaPlayerCharacter* (*Who)(const FState&), float Until)
	{
		return [S, Who, Until]()
		{
			AAsdaPlayerCharacter* C = Who(*S);
			if (UAsdaBlendAnimInstance* I = S->Anim(C); I && I->WasHitFlinchAppliedGT()) { S->bSawFlinch = true; }
			S->TintMax = FMath::Max(S->TintMax, FState::Tint(C));
			return S->El() > Until;
		};
	};
	static AAsdaPlayerCharacter* (*Me)(const FState&) = [](const FState& St) { return St.Ch(); };
	static AAsdaPlayerCharacter* (*Peer)(const FState&) = [](const FState& St) { return St.PeerCh(kP); };
	auto Calm = [S]()
	{
		if (AAsdaPlayerController* P = S->PC()) { P->ExitCombatStance(); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->SetCombatStance(false); }
	};

	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		Prepare(S, Net);
		S->SetKey(TEXT("asda.Char.HitFlinch"), 1); S->SetKey(TEXT("asda.Char.HitStance"), 1);
		S->SpawnMob(kRidA, S->HomeTile + FIntPoint(1, 0)); S->SpawnMob(kRidB, S->HomeTile + FIntPoint(-1, 0));
		S->SpawnMob(kRidC, S->HomeTile + FIntPoint(0, 1)); S->SpawnMob(kRidE, S->HomeTile + FIntPoint(1, 1));
		S->SpawnPeer(kP, TEXT("Сосед"), S->HomeTile + FIntPoint(3, 3)); S->SpawnMob(kRidD, S->HomeTile + FIntPoint(4, 3));
		S->SpawnMob(kRidW, S->HomeTile + FIntPoint(-1, -1));
	}, [S]() { return S->El() > 1.0 && S->Mob(kRidA) && S->Mob(kRidD) && S->PeerCh(kP); }, [S]()
	{
		S->Expect(S->Mob(kRidA) && S->Mob(kRidD) && S->PeerCh(kP), TEXT("мобы и сосед на месте"));
		S->Expect(S->Anim(S->Ch()) != nullptr, TEXT("у своего тела инстанс со смешиванием (точка вздрагивания)"));
	});

	// ── 0б. прогрев: первый удар грузит клип стойки и импакт синхронно (`FlushAsyncLoading`) — кадр длиннее 125 мс, и окно вздрагивания
	//   проходит без оценки позы (как у эталона при таком кадре: таймер += dt). Замер — со второго удара.
	Add(TEXT("0б прогрев (первый удар моба)"), 3.f, [S, Calm]()
	{
		Calm(); S->Mark();
		S->Feed(4015, Body4015Hit(S->Self, kRidW, S->HomeTile + FIntPoint(-1, -1), 50));
	}, [S]() { return S->El() > 1.8; }, [S, Calm]() { Calm(); });

	// ── 1. удар по мне ──
	Add(TEXT("1 удар моба по мне (урон 50)"), 3.f, [S, Calm]()
	{
		Calm(); S->Mark();
		S->Feed(4015, Body4015Hit(S->Self, kRidA, S->HomeTile + FIntPoint(1, 0), 50));
	}, HitPoll(Me, 1.8f), [S]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); UAsdaBlendAnimInstance* I = S->Anim(C);
		S->Expect(S->bSawFlinch, TEXT("поворот кости «4» лёг на оценке позы (вздрагивание)"));
		S->Expect(I && !I->IsHitFlinchActiveGT() && !I->WasHitFlinchAppliedGT(), TEXT("через 125 мс вздрагивание снято"));
		S->Expect(FMath::IsNearlyEqual(S->TintMax, 0.7f, 0.01f), FString::Printf(TEXT("покраснение: CPD[1] тела во вздрагивании %.2f (ждём 0.70 = 1 − 0.3)"), S->TintMax));
		S->Expect(FState::Tint(C) == 0.f, TEXT("покраснение снято (CPD[1] = 0)"));
		bool bPartsTinted = true; int32 NParts = 0;
		if (C) { TInlineComponentArray<UMeshComponent*> Ms(C); for (UMeshComponent* M : Ms) { if (M && (M->IsA<USkeletalMeshComponent>() || M->IsA<UStaticMeshComponent>())) { ++NParts; const TArray<float>& D = M->GetCustomPrimitiveData().Data; if (D.IsValidIndex(1) && D[1] != 0.f) { bPartsTinted = false; } } } }
		S->Expect(bPartsTinted && NParts > 0, FString::Printf(TEXT("у всех %d мешей персонажа CPD[1] вернулся в 0"), NParts));
		S->Expect(C && C->IsCombatStance(), TEXT("покой → боевая стойка (эталон CharFx_AutoSabzil_State(…, 20))"));
		S->Expect(S->PC() && S->PC()->CombatStanceUntil > FPlatformTime::Seconds(), TEXT("кулдаун стойки взведён (выход через 10 с)"));
	});

	// ── 2. уклонение ──
	Add(TEXT("2 уклонение (−1)"), 3.f, [S, Calm]()
	{
		Calm(); S->Mark();
		S->Feed(4015, Body4015Hit(S->Self, kRidB, S->HomeTile + FIntPoint(-1, 0), -1));
	}, HitPoll(Me, 1.8f), [S]()
	{
		S->Expect(!S->bSawFlinch, TEXT("уклонение: вздрагивания нет"));
		S->Expect(S->Ch() && S->Ch()->IsCombatStance(), TEXT("уклонение: стойка есть (после любого исхода)"));
	});

	// ── 3. барьер 51 ──
	Add(TEXT("3 барьер 51 на мне"), 3.f, [S, Calm]()
	{
		Calm(); S->Mark();
		if (UAsdaEffectsSubsystem* F = S->Fx()) { F->AddOrUpdateSkill(S->Self, 51, 0, 1, 0, 30, 0, -1); }
		S->Feed(4015, Body4015Hit(S->Self, kRidC, S->HomeTile + FIntPoint(0, 1), 50));
	}, [S]()
	{
		if (UAsdaBlendAnimInstance* I = S->Anim(S->Ch()); I && I->WasHitFlinchAppliedGT()) { S->bSawFlinch = true; }
		if (S->CountFx(TEXT("brieg02")) > 0) { S->bStateEarly = true; }
		return S->El() > 1.8;
	}, [S]()
	{
		S->Expect(S->Fx() && S->Fx()->FindByTypeSub(S->Self, 51, 0) != nullptr, TEXT("состояние 51 на мне"));
		S->Expect(!S->bSawFlinch, TEXT("барьер 51: вздрагивания нет (эталон — звук barrior3 и эффект brieg02)"));
		S->Expect(S->bStateEarly, TEXT("барьер 51: эффект brieg02 заспавнен (BP_Fx_eff_1_char_06_brieg02_a_00)"));
		if (UAsdaEffectsSubsystem* F = S->Fx()) { F->RemoveBySkillState(S->Self, 51, 0); }
	});

	// ── 3б. барьер 547 ──
	Add(TEXT("3б барьер 547 на мне"), 3.f, [S, Calm]()
	{
		Calm(); S->Mark();
		if (UAsdaEffectsSubsystem* F = S->Fx()) { F->AddOrUpdateSkill(S->Self, 547, 0, 1, 0, 30, 0, -1); }
		S->Feed(4015, Body4015Hit(S->Self, kRidE, S->HomeTile + FIntPoint(1, 1), 50));
	}, [S]()
	{
		if (UAsdaBlendAnimInstance* I = S->Anim(S->Ch()); I && I->WasHitFlinchAppliedGT()) { S->bSawFlinch = true; }
		if (S->CountFx(TEXT("defen01")) > 0) { S->bStateEarly = true; }
		return S->El() > 1.8;
	}, [S]()
	{
		S->Expect(!S->bSawFlinch, TEXT("барьер 547: вздрагивания нет"));
		S->Expect(S->bStateEarly, TEXT("барьер 547: эффект defen01 заспавнен (BP_Fx_eff_1_char_99_defen01_a_00)"));
		if (UAsdaEffectsSubsystem* F = S->Fx()) { F->RemoveBySkillState(S->Self, 547, 0); }
	});

	// ── 3в. крит моба ──
	Add(TEXT("3в крит моба (бит 31)"), 3.f, [S, Calm]()
	{
		Calm(); S->Mark();
		S->Feed(4015, Body4015Hit(S->Self, kRidC, S->HomeTile + FIntPoint(0, 1), (int32)(0x80000000u | 70u)));
	}, [S]()
	{
		if (UAsdaBlendAnimInstance* I = S->Anim(S->Ch()); I && I->WasHitFlinchAppliedGT()) { S->bSawFlinch = true; }
		if (S->CountFx(TEXT("ImpactMobCrit")) > 0) { S->bStateEarly = true; }
		return S->El() > 1.8;
	}, [S]()
	{
		S->Expect(S->bStateEarly, TEXT("крит: импакт cattk01 (BP_ImpactMobCrit) вместо battk01"));
		S->Expect(S->bSawFlinch, TEXT("крит: вздрагивание есть (эталон :7949)"));
	});

	// ── 4. удар по соседу ──
	Add(TEXT("4 удар моба по соседу"), 3.f, [S]()
	{
		S->Mark();
		S->Feed(4015, Body4015Hit(kP, kRidD, S->HomeTile + FIntPoint(4, 3), 40));
	}, HitPoll(Peer, 1.8f), [S]()
	{
		S->Expect(S->bSawFlinch, TEXT("сосед вздрогнул (эталон :8159, модель собрана)"));
	});

	// ── 4б–4д. навыки по персонажу (5257, `ApplySkillHit` цель-персонаж) ──
	struct FSkCase { const TCHAR* Name; bool bPeerCasts; int32 Skill; int32 Dmg; uint8 Flags; bool bExpect; };
	static const FSkCase kSk[] = {
		{ TEXT("4б навык соседа 501 по мне, урон 30 (Combat_ShowDamageFeedback :3745)"), true, 501, 30, 0x01, true },
		{ TEXT("4в навык соседа 501 по мне, БЛОК 0x08 — без вздрагивания"),               true, 501, 30, 0x09, false },
		{ TEXT("4г «Обстрел» 714 соседа по мне без флагов (CharSkill_UpdateBlastFx2)"),   true, 714, 0, 0x00, true },
		{ TEXT("4д моя граната 604 по соседу, промах (обработчик результата, флаг 1)"),    false, 604, 0, 0x01, true },
	};
	// Прогрев: первый показ навыка грузит его клип и эффекты синхронно — кадр длиннее 125 мс, окно вздрагивания проходит без оценки
	//   позы (у эталона так же: таймер += dt). Замер — со второго показа.
	Add(TEXT("4а прогрев показов 501/714/604"), 4.f, [S, Calm]()
	{
		Calm(); S->Mark();
		S->Feed(5257, Body5257K(kP, 501, 1, S->Self, 1, 0x01, 900));
		S->Feed(5257, Body5257K(S->Self, 604, 1, kP, 0, 0x01, 900));
	}, [S]() { return S->El() > 3.0; }, [S, Calm]() { S->CalmSkill(604); Calm(); });
	for (const FSkCase& K : kSk)
	{
		Add(K.Name, 5.f, [S, Calm, K]()
		{
			Calm(); S->Mark();
			const int32 Caster = K.bPeerCasts ? kP : S->Self;
			const int32 Target = K.bPeerCasts ? S->Self : kP;
			S->Feed(5257, Body5257K(Caster, K.Skill, 1, Target, K.Dmg, K.Flags, 900));
		}, [S, K]()
		{
			AAsdaPlayerCharacter* V = K.bPeerCasts ? S->Ch() : S->PeerCh(kP);
			if (UAsdaBlendAnimInstance* I = S->Anim(V); I && I->WasHitFlinchAppliedGT()) { S->bSawFlinch = true; }
			return S->El() > 3.5;
		}, [S, K]()
		{
			S->Expect(S->bSawFlinch == K.bExpect, FString::Printf(TEXT("вздрагивание жертвы: %s (ждём %s)"), S->bSawFlinch ? TEXT("было") : TEXT("нет"), K.bExpect ? TEXT("было") : TEXT("нет")));
			S->CalmSkill(K.Skill);
		});
	}

	// ── 5. контроль ключами 0 ──
	Add(TEXT("5 контроль asda.Char.HitFlinch 0 / HitStance 0"), 3.f, [S, Calm]()
	{
		Calm(); S->Mark();
		S->SetKey(TEXT("asda.Char.HitFlinch"), 0); S->SetKey(TEXT("asda.Char.HitStance"), 0);
		S->Feed(4015, Body4015Hit(S->Self, kRidE, S->HomeTile + FIntPoint(1, 1), 50));
	}, HitPoll(Me, 1.8f), [S]()
	{
		S->Expect(!S->bSawFlinch && S->Ch() && !S->Ch()->IsCombatStance(), TEXT("прежний порт: ни вздрагивания, ни стойки (дефект воспроизведён)"));
		S->SetKey(TEXT("asda.Char.HitFlinch"), 1); S->SetKey(TEXT("asda.Char.HitStance"), 1);
	});

	// ── 6. повторный старт не перезапускает ──
	Add(TEXT("6 повтор во время вздрагивания"), 1.f, [S]()
	{
		UAsdaBlendAnimInstance* I = S->Anim(S->Ch());
		const bool b1 = I && I->StartHitFlinchGT();
		const bool b2 = I && I->StartHitFlinchGT();
		S->Expect(b1 && !b2, TEXT("первый старт идёт, второй во время вздрагивания отклонён (флаг 0x200)"));
	}, [S]() { return S->El() > 0.3; }, nullptr);

	Add(TEXT("итог"), 2.f, [S, Net, Calm]() { Calm(); Cleanup(S, Net); }, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaCombat3Test, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCharge(
	TEXT("asda.Skill.SelfTestCharge"),
	TEXT("Самотест рывка Charge 537 (29.10): ход к цели за COMBO0, стан на прибытии, 8215, 4008 вид 15, отброс, MISS, наблюдатель, "
	     "подход автоатаки ждёт навык; контрольные пары asda.Skill.Dash 0 и asda.Combat.ApproachWaitsSkill 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestCharge"));
		FAsdaCombat3SelfTest::RunCharge(A, W);
	}));

// ═══ asda.Skill.SelfTestFaceTarget (31.10.2026, баг владельца «при атаке персонаж смотрит не в сторону противника») ═══════════════
//   Путь игрока: выбор цели `SetTarget`, навык — `DispatchByKind` (вход быстрой панели), исходящий 5256 — перехват, ответ сервера —
//   штатный приём 5257. Разбор — ключ `asda.Skill.FaceTargetEtalon` (AsdaSkillUseSubsystem.cpp), `OnSkillCastFaceTarget`.
void FAsdaCombat3SelfTest::RunFace(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaCombat3TestLocal;
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestFaceTarget");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256, 4206 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-РАЗВОРОТ");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	constexpr int32 kRidN = 30971, kRidE = 30972;   // моб на севере (прежняя цель), моб на востоке (цель навыка)
	constexpr int32 kQ = 7832;                      // второй сосед (на западе)
	static bool bWasStand = false;                  // прежний перехват заявки навыка (вернуть в итоге)
	// 4206 (13 б): userIdx@0, [88]@6, режим@7, соперник/команда@8, код@12. 5251 — изученные навыки (34 б на запись).
	auto Body4206 = [](int32 Uidx, uint8 Mode) { TArray<uint8> B; B.SetNumZeroed(13); AsdaBytes::WrI16(&B[0], Uidx); B[6] = Mode; B[7] = Mode; B[12] = 1; return B; };
	auto Body5251 = [](const TArray<int32>& Codes)
	{
		TArray<uint8> B; B.SetNumZeroed(34 * Codes.Num());
		for (int32 i = 0; i < Codes.Num(); ++i) { uint8* P = B.GetData() + 34 * i; AsdaBytes::WrI16(P, Codes[i]); P[2] = 5; P[3] = 1; }
		return B;
	};
	// Угол между курсом своего персонажа и направлением на актор, градусы (bVisual — плюс отставание видимой модели от курса).
	auto AngleTo = [S](const AActor* T, bool bVisual)
	{
		const AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !T) { return 999.f; }
		const FVector2D D = AsdaCoord::UeToGame2D(T->GetActorLocation()) - C->GetGamePosXZ();
		const FVector2D F = C->GetFacingDirGame();
		const float Logical = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((float)FVector2D::DotProduct(F, D.GetSafeNormal()), -1.f, 1.f)));
		return bVisual ? Logical + FMath::Abs(FMath::FindDeltaAngleDegrees(C->GetVisualFacingYawUE(), C->GetFacingYawUE())) : Logical;
	};
	auto Sent = [S](int32 Code) { for (const FOut& O : S->OutsOf(5256)) { if (O.Payload.Num() >= 26 && AsdaBytes::RdI16(&O.Payload[24]) == Code) { return true; } } return false; };
	auto Idle = [S]() { const UAsdaSkillUseSubsystem* U = S->SU(); return U && U->GetActiveSkill() < 0 && U->GetSelfCharState() == 0; };

	Add(TEXT("подготовка"), 5.f, [S, Net, Body4206, Body5251]()
	{
		Prepare(S, Net);
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 10; }   // лук: навыки лучника 705/710 (маска 0x400); Cleanup вернёт прежний
		bWasStand = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->Feed(5251, Body5251({ 705, 710 }));
		Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест разворота"), true);
		S->SetKey(TEXT("asda.Skill.FaceTargetEtalon"), 1); S->SetKey(TEXT("asda.Skill.TargetRelation"), 1);
		S->Feed(4206, Body4206(S->Self, 2));   // свободный бой: навык по соседу уходит
		S->SpawnMob(kRidN, S->HomeTile + FIntPoint(0, -4));
		S->SpawnMob(kRidE, S->HomeTile + FIntPoint(4, 0));
		S->SpawnPeer(kP, TEXT("ВостокП"), S->HomeTile + FIntPoint(4, 1));
		S->SpawnPeer(kQ, TEXT("ЗападQ"), S->HomeTile + FIntPoint(-4, 0));
	}, [S]() { return S->El() > 1.0 && S->Mob(kRidN) && S->Mob(kRidE) && S->PeerCh(kP) && S->PeerCh(kQ); }, [S]()
	{
		S->Expect(S->Mob(kRidN) && S->Mob(kRidE) && S->PeerCh(kP) && S->PeerCh(kQ), TEXT("мобы и соседи на местах (север/восток/запад)"));
	});

	// Сценарий: прежняя цель доворота PrevMob (≥0 — моб, −2 — сосед П, −1 — нет), курс на север, выбор цели, навык, ответ 5257 по цели.
	struct FCase { const TCHAR* Name; int32 Code; bool bTgtPeer; int32 TgtIdx; int32 PrevMob; bool bKeyOn; bool bExpectFace; };
	static const FCase kCases[] = {
		{ TEXT("А игрок на востоке (710), прежняя цель — моб на севере"),           710, true,  kP,    kRidN, true,  true },
		{ TEXT("Б моб на востоке (705), персонаж стоит лицом на север"),            705, false, kRidE, -1,    true,  true },
		{ TEXT("В игрок на западе (710), прежняя цель — игрок на востоке"),         710, true,  kQ,    -2,    true,  true },
		{ TEXT("Г контроль asda.Skill.FaceTargetEtalon 0 — прежний порт (как А)"), 710, true,  kP,    kRidN, false, false },
	};
	for (const FCase& K : kCases)
	{
		Add(K.Name, 4.f, [S, K]()
		{
			S->Mark();
			S->SetKey(TEXT("asda.Skill.FaceTargetEtalon"), K.bKeyOn ? 1 : 0);
			AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
			if (!C || !PC) { return; }
			C->FaceGameDir(FVector2D(0.f, -1.f));   // лицом на север (−Z игры)
			C->AttackFaceTarget = (K.PrevMob >= 0) ? Cast<AActor>(S->Mob(K.PrevMob)) : (K.PrevMob == -2 ? Cast<AActor>(S->PeerCh(kP)) : nullptr);
			AActor* T = K.bTgtPeer ? Cast<AActor>(S->PeerCh(K.TgtIdx)) : Cast<AActor>(S->Mob(K.TgtIdx));
			PC->SetTarget(T);
			if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(K.Code, -1); }
		}, [S, K, Sent]()
		{
			if (!S->bSampled && Sent(K.Code))
			{
				S->bSampled = true;
				S->Feed(5257, Body5257K(S->Self, K.Code, K.bTgtPeer ? 1 : 0, K.TgtIdx, 10, 0x01, 900));
				S->StepStartS = FPlatformTime::Seconds();
			}
			return S->bSampled && S->El() > 0.6;
		}, [S, K, AngleTo, Sent]()
		{
			AActor* T = K.bTgtPeer ? Cast<AActor>(S->PeerCh(K.TgtIdx)) : Cast<AActor>(S->Mob(K.TgtIdx));
			const float A = AngleTo(T, false), AV = AngleTo(T, true);
			S->Expect(Sent(K.Code), FString::Printf(TEXT("заявка 5256 навыка %d ушла путём игрока"), K.Code));
			if (K.bExpectFace)
			{
				S->Expect(A <= 10.f, FString::Printf(TEXT("курс персонажа на цель: %.1f° (допуск 10°)"), A));
				S->Expect(AV <= 15.f, FString::Printf(TEXT("видимый поворот модели на цель: %.1f° (допуск 15°, доворот 600°/с)"), AV));
				S->Expect(S->Ch() && S->Ch()->AttackFaceTarget.Get() == T, TEXT("цель доворота = цель навыка"));
			}
			else
			{
				S->Expect(A > 30.f, FString::Printf(TEXT("прежний порт: персонаж смотрит не на цель (%.1f°) — дефект воспроизведён"), A));
			}
			S->CalmSkill(K.Code);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->SkillFaceUntilS = 0.0; C->AttackFaceTarget = nullptr; }
		});
		Add(TEXT("пауза (навык свободен)"), 3.f, nullptr, [Idle, S]() { return Idle() && S->El() > 0.3; }, nullptr);
	}

	Add(TEXT("итог"), 2.f, [S, Net, Body4206]()
	{
		S->Feed(4206, Body4206(S->Self, 0));
		Net->bStandCaptureSkillUse = bWasStand;
		Cleanup(S, Net);
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaCombat3Test, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestFaceTarget(
	TEXT("asda.Skill.SelfTestFaceTarget"),
	TEXT("Самотест разворота к цели навыка (31.10): путь игрока SetTarget + DispatchByKind, ответ 5257 — персонаж повёрнут к цели "
	     "(игрок/моб, прежняя цель другая, ответ без смены цели); контроль asda.Skill.FaceTargetEtalon 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestFaceTarget"));
		FAsdaCombat3SelfTest::RunFace(A, W);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestHitFlinch(
	TEXT("asda.Char.SelfTestHitFlinch"),
	TEXT("Самотест вздрагивания от удара моба (29.10): кость «4» 125 мс, боевая стойка в покое, уклонение, барьер 51, сосед; "
	     "контроль asda.Char.HitFlinch 0 / HitStance 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Char.SelfTestHitFlinch"));
		FAsdaCombat3SelfTest::RunFlinch(A, W);
	}));

// ═══ asda.Skill.SelfTestGrenade (05.11.2026, баг игроков «граната бьёт мгновенно и невидимо; убитые встают зомби») ═══════════════════
//   [ПОРТ] Только офлайн. Путь игрока: свой каст 604 «Граната» ответом сервера 5257 с ТРЕМЯ записями по мобам (HP после = 0, флаги 0x01).
//   1) снаряд: на первом кадре показа летит ДУГОЙ (вершина выше старта и конца ≥ 300 юн), прилёт через ~1.5 с игрового времени;
//      кадры `shot=<папка>`: grenade_1_before / 2_throw / 3_apex / 4_blast / 5_after;
//   2) по прилёте: взрыв grebom01 у КАЖДОЙ цели (≥ 3 эффекта), все три моба мертвы, ушло три 8215 вида 0 (rid целей, навык 604) —
//      подтверждение попадания эталона `netSendSkillCastGroundTarget`, без которого NpcServer не списывал урон (журналы игроков);
//   3) «зомби»: сервер присылает смерть 4017 flags=0 по каждой цели и ЗАПОЗДАВШИЙ удар 4015 kind=3 одной из них с HP > 0 — труп не встаёт;
//      через 4.5 с ни один из трёх не жив;
//   4) контроль `asda.Skill.HitConfirm8215 0`: 8215 нет (дефект воспроизведён); 5) контроль `asda.Skill.GrenadeArc 0`: прямой носитель,
//      прилёт < 0.6 с (прежний порт); 6) навыки 504/707/820/906/912/1062 по трём мобам — 8215 вид 0 на каждый показанный удар, рывок 537 — ни одного;
//   7) дуэль: в заявке 5256 гранаты нет себя, есть соперник (+ контроль); 8) состояние 285 «Отпор» на мобе и на себе — 8215 вид 1 (+ контроль).
namespace AsdaGrenadeTestLocal
{
	using namespace AsdaCombat3TestLocal;
	// 5257 своего кастера с N записями по мобам: [0] 1, [1..2] кастер, [3..4] навык, [5] 1, [6..7] ур.; записи 29 б (вид 0, rid, урон, флаги,
	//   HP после @11, −1 @19). Число записей сервер не пишет — оно = (размер − 8) / 29.
	static TArray<uint8> Body5257N(int32 Caster, int32 Skill, const TArray<int32>& Rids, int32 Dmg, uint8 Flags, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29 * Rids.Num());
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		for (int32 i = 0; i < Rids.Num(); ++i)
		{
			uint8* Rc = B.GetData() + 8 + 29 * i;
			Rc[0] = 0; AsdaBytes::WrI16(Rc + 1, Rids[i]); AsdaBytes::WrI32(Rc + 3, Dmg); Rc[7] = Flags;
			AsdaBytes::WrI32(Rc + 11, HpAfter); AsdaBytes::WrI16(Rc + 19, -1);
		}
		return B;
	}
	// 4017 (100 б): тип@0, rid@2, флаги@4 (0 — смерть), 28 состояний −1, HP@92.
	static TArray<uint8> Body4017Dead(int32 Rid)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], kMobType); AsdaBytes::WrI16(&B[2], Rid); AsdaBytes::WrI32(&B[4], 0);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		AsdaBytes::WrI32(&B[92], 0);
		return B;
	}
	// Наблюдения шага гранаты.
	struct FObs
	{
		bool bSawProj = false; float StartZ = 0.f, MaxZ = -1e9f, EndZ = 0.f; double ArriveG = -1.0; int32 Shots = 0;
		float MaxAboveChord = -1e9f;   // наибольший подъём точки над хордой старт→конец (по доле пройденного плоского пути)
		int32 Samples = 0;
		double PrevTrackG = -1.0;      // ★ R5-fix6: игровое время прошлого опроса полёта
		double ArriveLagS = 0.0;       // ★ R5-fix6: сколько прошло между опросом «снаряд есть» и опросом «снаряда нет» сверх кадра 1/60 с
		void Reset() { bSawProj = false; StartZ = 0.f; MaxZ = -1e9f; EndZ = 0.f; ArriveG = -1.0; Shots = 0; MaxAboveChord = -1e9f; Samples = 0; PrevTrackG = -1.0; ArriveLagS = 0.0; }
	};
}

void FAsdaCombat3SelfTest::RunGrenade(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaGrenadeTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestGrenade");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256, 8215 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-GRENADE"); S->ShotPrefix = TEXT("grenade");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	for (const FString& A : Args) { if (A.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase)) { S->ShotDir = A.Mid(5); IFileManager::Get().MakeDirectory(*S->ShotDir, true); } }
	TSharedRef<FObs> O = MakeShared<FObs>();
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	static const TArray<int32> kR = { 30961, 30962, 30963 };      // основной заход
	static const TArray<int32> kR2 = { 30964, 30965, 30966 };     // контроль 8215
	static const TArray<int32> kR3 = { 30967, 30968, 30969 };     // контроль дуги
	auto SpawnTrio = [S](const TArray<int32>& Rids, const FIntPoint& Base0)
	{
		// ★ Мобы — ПЕРЕД камерой (кадры): база смещения поворачивается на курс камеры (клетка = 100 игровых юн).
		FIntPoint Base = Base0;
		if (APlayerController* Pc = S->PC(); Pc && Pc->PlayerCameraManager)
		{
			const FVector Fw = Pc->PlayerCameraManager->GetCameraRotation().Vector();
			const FVector2D G0 = AsdaCoord::UeToGame2D(FVector::ZeroVector), G1 = AsdaCoord::UeToGame2D(Fw * 100.f);
			const FVector2D Dir = (G1 - G0).GetSafeNormal();
			const float Dist = (float)FMath::Sqrt((float)(Base0.X * Base0.X + Base0.Y * Base0.Y));
			const float Ang = FMath::Atan2((float)Base0.Y, (float)Base0.X);   // поворот исходного смещения относительно «вперёд»
			const FVector2D D2 = Dir.GetRotated(FMath::RadiansToDegrees(Ang)) * Dist;
			Base = FIntPoint(FMath::RoundToInt(D2.X), FMath::RoundToInt(D2.Y));
		}
		S->SpawnMob(Rids[0], S->HomeTile + Base);
		S->SpawnMob(Rids[1], S->HomeTile + Base + FIntPoint(1, 1));
		S->SpawnMob(Rids[2], S->HomeTile + Base + FIntPoint(1, -1));
	};
	// Наблюдение полёта: снаряд(ы) в мире — высота старта/максимум/конец; прилёт = первый 8215 (или конец снаряда при выключенном 8215).
	auto Track = [S, O]()
	{
		UWorld* Wd = S->World.Get(); if (!Wd) { return; }
		int32 N = 0;
		for (TActorIterator<AAsdaProjectileActor> It(Wd); It; ++It)
		{
			++N;
			const FVector L = It->GetActorLocation();
			const float Z = (float)L.Z;
			if (!O->bSawProj) { O->bSawProj = true; O->StartZ = Z; }
			O->MaxZ = FMath::Max(O->MaxZ, Z); O->EndZ = (float)It->EndPos.Z;
			// Подъём над хордой: доля плоского пути f, высота хорды = старт + (конец − старт)·f.
			const FVector Sp = It->StartPos, Ep = It->EndPos;
			const float Flat = (float)FVector::Dist2D(Sp, Ep);
			const float F = Flat > 1.f ? FMath::Clamp((float)FVector::Dist2D(Sp, L) / Flat, 0.f, 1.f) : 0.f;
			O->MaxAboveChord = FMath::Max(O->MaxAboveChord, Z - (float)(Sp.Z + (Ep.Z - Sp.Z) * F));
			++O->Samples;
		}
		if (O->bSawProj && N == 0 && O->ArriveG < 0.0)
		{
			O->ArriveG = S->GEl();
			// ★ R5-fix6: прилёт виден только на опросе ПОСЛЕ него — запаздывание не больше шага опроса (кадра).
			O->ArriveLagS = (O->PrevTrackG >= 0.0) ? FMath::Max(0.0, (O->ArriveG - O->PrevTrackG) - 1.0 / 60.0) : 0.0;
		}
		O->PrevTrackG = S->GEl();
	};

	Add(TEXT("подготовка"), 5.f, [S, Net, SpawnTrio]()
	{
		Prepare(S, Net);
		S->SetKey(TEXT("asda.Skill.GrenadeArc"), 1); S->SetKey(TEXT("asda.Skill.HitConfirm8215"), 1);
		SpawnTrio(kR, FIntPoint(6, 0));
	}, [S]() { return S->El() > 1.0 && S->Mob(kR[0]) && S->Mob(kR[1]) && S->Mob(kR[2]); }, [S]()
	{
		S->Expect(S->Mob(kR[0]) && S->Mob(kR[1]) && S->Mob(kR[2]), TEXT("три моба в 6–7 клетках"));
	});
	Add(TEXT("кадр до броска"), 2.f, [S]() { S->Mark(); S->Shot(1, TEXT("1_before")); }, [S]() { return S->El() > 1.0; }, nullptr);

	// ── 1–2. бросок, дуга, взрыв у каждой цели, 8215 по каждой, смерть ──
	Add(TEXT("1 граната по трём мобам"), 6.f, [S, O]()
	{
		S->Mark(); O->Reset();
		S->Feed(5257, Body5257N(S->Self, 604, kR, 500, 0x01, 0));
		S->MarkGame();
	}, [S, O, Track]()
	{
		Track();
		if (S->GEl() > 0.15) { S->Shot(2, TEXT("2_throw")); }
		if (S->GEl() > 0.75) { S->Shot(4, TEXT("3_apex")); }
		if (O->ArriveG > 0.0 && S->GEl() > O->ArriveG + 0.25) { S->Shot(8, TEXT("4_blast")); }
		if (O->ArriveG > 0.0 && S->GEl() > O->ArriveG + 2.0) { S->Shot(16, TEXT("5_after")); }
		return O->ArriveG > 0.0 && S->GEl() > O->ArriveG + 2.2;
	}, [S, O]()
	{
		S->Expect(O->bSawProj, TEXT("снаряд гранаты в мире"));
		// Безье с вершиной «конец + 750» над серединой: на середине пути точка выше хорды на ≈ 375 юн.
		S->Expect(O->MaxAboveChord >= 200.f, FString::Printf(TEXT("полёт ДУГОЙ: наибольший подъём над хордой %.0f юн (замеров %d; эталон — вершина конец+750, на середине ≈ +375)"),
			O->MaxAboveChord, O->Samples));
		S->Expect(O->ArriveG > 1.3 && O->ArriveG < 1.9, FString::Printf(TEXT("прилёт через %.2f с игрового времени (эталон 1.5 с)"), O->ArriveG));
		TSet<int32> Got; int32 Bad = 0;
		for (const FOut& D : S->OutsOf(8215))
		{
			if (D.Payload.Num() == 17 && D.Payload[0] == 0 && AsdaBytes::RdI16(&D.Payload[9]) == 604) { Got.Add(AsdaBytes::RdI16(&D.Payload[7])); } else { ++Bad; }
		}
		S->Expect(Got.Num() == 3 && Got.Contains(kR[0]) && Got.Contains(kR[1]) && Got.Contains(kR[2]) && Bad == 0,
			FString::Printf(TEXT("8215 вид 0 по КАЖДОЙ цели: целей %d, лишних %d (подтверждение попадания — NpcServer списывает урон)"), Got.Num(), Bad));
		const int32 NBlast = S->CountFx(TEXT("grebom01"));
		// Эталон: цикл effectBeamUpdate — по взрыву у каждой цели (3) + обработчик записи вида 13 — ещё один у основной цели-моба.
		S->Expect(NBlast >= 4, FString::Printf(TEXT("взрыв grebom01 у каждой цели и второй у основной (эталон 3 + 1): эффектов %d"), NBlast));
		int32 Dead = 0; for (int32 Rid : kR) { if (AAsdaMobActor* M = S->Mob(Rid)) { if (M->IsDead()) { ++Dead; } } else { ++Dead; } }
		S->Expect(Dead == 3, FString::Printf(TEXT("все три моба мертвы: %d из 3"), Dead));
	});

	// ── 3. «зомби»: смерть от сервера + запоздавший удар трупа ──
	Add(TEXT("3 трупы не встают"), 7.f, [S]()
	{
		S->Mark();
		for (int32 Rid : kR) { S->Feed(4017, Body4017Dead(Rid)); }
		if (AAsdaMobActor* M = S->Mob(kR[1])) { S->Feed(4015, Body4015Hit(S->Self, kR[1], AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(M->GetActorLocation())), 40)); }
	}, [S]() { return S->El() > 4.5; }, [S]()
	{
		int32 Alive = 0;
		for (int32 Rid : kR) { if (AAsdaMobActor* M = S->Mob(Rid)) { if (!M->IsDead()) { ++Alive; } } }
		S->Expect(Alive == 0, FString::Printf(TEXT("через 4.5 с живых из убитых: %d (запоздавший 4015 по трупу не оживляет — эталон: состояние 10)"), Alive));
	});

	// ── 4. контроль asda.Skill.HitConfirm8215 0 ──
	Add(TEXT("4 контроль asda.Skill.HitConfirm8215 0"), 7.f, [S, O, SpawnTrio]()
	{
		S->Mark(); O->Reset();
		S->SetKey(TEXT("asda.Skill.HitConfirm8215"), 0);
		SpawnTrio(kR2, FIntPoint(-6, 0));
	}, [S, O, Track]()
	{
		if (!S->bSampled && S->Mob(kR2[0]) && S->Mob(kR2[1]) && S->Mob(kR2[2]) && S->El() > 1.0)
		{
			S->bSampled = true; S->Mark(); S->bSampled = true;
			S->Feed(5257, Body5257N(S->Self, 604, kR2, 500, 0x01, 0)); S->MarkGame();
		}
		if (S->bSampled) { Track(); }
		return S->bSampled && O->ArriveG > 0.0 && S->GEl() > O->ArriveG + 0.5;
	}, [S]()
	{
		S->Expect(S->OutsOf(8215).Num() == 0, FString::Printf(TEXT("прежний порт: 8215 не уходит (штук %d) — дефект воспроизведён"), S->OutsOf(8215).Num()));
		S->SetKey(TEXT("asda.Skill.HitConfirm8215"), 1);
	});

	// ── 5. контроль asda.Skill.GrenadeArc 0 ──
	Add(TEXT("5 контроль asda.Skill.GrenadeArc 0"), 7.f, [S, O, SpawnTrio]()
	{
		S->Mark(); O->Reset();
		S->SetKey(TEXT("asda.Skill.GrenadeArc"), 0);
		SpawnTrio(kR3, FIntPoint(0, 6));
	}, [S, O, Track]()
	{
		if (!S->bSampled && S->Mob(kR3[0]) && S->Mob(kR3[1]) && S->Mob(kR3[2]) && S->El() > 1.0)
		{
			S->Mark(); S->bSampled = true;
			S->Feed(5257, Body5257N(S->Self, 604, kR3, 500, 0x01, 0)); S->MarkGame();
		}
		if (S->bSampled) { Track(); }
		return S->bSampled && O->ArriveG > 0.0 && S->GEl() > O->ArriveG + 0.5;
	}, [S, O]()
	{
		// Прежний порт пускал носитель на COMBO0 (233 мс) и летел 2.5 юн/мс — прилёт ≈ 0.3–0.4 с от ответа.
		// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: момент прилёта берётся на опросе после него; при кадре 100 мс замер запаздывал на
		//   кадр (0,60 с вместо ≈ 0,5) и контроль краснел. Из замера вычитается шаг опроса сверх кадра 1/60 с; порог 0,6 с прежний.
		S->Expect(O->ArriveG > 0.0 && O->ArriveG - O->ArriveLagS < 0.6,
			FString::Printf(TEXT("прежний порт: прямой носитель, прилёт через %.2f с (запаздывание опроса %.2f с; дефект воспроизведён)"), O->ArriveG, O->ArriveLagS));
		S->SetKey(TEXT("asda.Skill.GrenadeArc"), 1);
	});

	// ── 7. дуэль: граната по сопернику путём игрока — в заявке 5256 нет СЕБЯ, есть соперник (баг «граната бьёт по себе в дуэли») ──
	//   + контроль asda.Skill.HostileAreaCharTarget 0: прежний порт клал себя (ветка групповой площади).
	constexpr int32 kRival = 7841;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		Add(Pass == 0 ? FString(TEXT("7 дуэль: граната по сопернику — себя в заявке нет")) : FString(TEXT("7б контроль asda.Skill.HostileAreaCharTarget 0")), 6.f,
			[S, Pass]()
		{
			S->Mark();
			S->SetKey(TEXT("asda.Skill.HostileAreaCharTarget"), Pass == 0 ? 1 : 0);
			if (AAsdaPlayerCharacter* C = S->Ch())
			{
				if (!S->PeerCh(kRival)) { S->SpawnPeer(kRival, TEXT("Соперник"), AsdaCoord::GameToTile(C->GetGamePosXZ()) + FIntPoint(4, 0)); }
			}
			TArray<uint8> L; L.SetNumZeroed(34); AsdaBytes::WrI16(&L[0], 604); L[2] = 5; L[3] = 1; S->Feed(5251, L);
			if (UAsdaNetSubsystem* Nn = S->N())
			{
				Nn->SetVitalsMp(S->Self, 99999, 99999, S->Cmd, true);
				Nn->ClearMoveRequestInFlight(TEXT("самотест Grenade: шаг 7"));
				S->bWasStandSkill = Nn->bStandCaptureSkillUse; Nn->bStandCaptureSkillUse = true;
			}
			S->Duel(kRival, true);
			S->CalmSkill(604);
		}, [S]()
		{
			AAsdaPlayerCharacter* V = S->PeerCh(kRival);
			if (V && !S->bPressed && S->El() > 0.6)
			{
				S->bPressed = true;
				if (AAsdaPlayerController* PC = S->PC()) { PC->SetTarget(V); }
				if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(604, -1); }
			}
			return S->OutsOf(5256).Num() > 0 || S->El() > 5.0;
		}, [S, Pass]()
		{
			const TArray<FOut> Q = S->OutsOf(5256);
			bool bSelfIn = false, bRivalIn = false; int32 N = 0;
			if (Q.Num() > 0)
			{
				const TArray<uint8>& B = Q[0].Payload;
				for (int32 Off = 31; Off + 29 <= B.Num(); Off += 29)
				{
					++N;
					const int32 Kind = B[Off]; const int32 Idx = AsdaBytes::RdI16(&B[Off + 1]);
					if (Kind == 1 && Idx == S->Self) { bSelfIn = true; }
					if (Kind == 1 && Idx == kRival) { bRivalIn = true; }
				}
			}
			if (Pass == 0)
			{
				S->Expect(Q.Num() == 1 && !bSelfIn && bRivalIn,
					FString::Printf(TEXT("заявка 5256 гранаты в дуэли: записей %d, соперник %s, СЕБЯ %s (эталон skillCastAreaTargetsAndSend)"),
						N, bRivalIn ? TEXT("есть") : TEXT("НЕТ"), bSelfIn ? TEXT("ЕСТЬ") : TEXT("нет")));
			}
			else
			{
				S->Expect(Q.Num() <= 1 && (bSelfIn || !bRivalIn), FString::Printf(TEXT("прежний порт: себя в заявке %s, соперник %s — цели не те (дефект воспроизведён), записей %d"),
					bSelfIn ? TEXT("есть") : TEXT("нет"), bRivalIn ? TEXT("есть") : TEXT("НЕТ"), N));
				S->SetKey(TEXT("asda.Skill.HostileAreaCharTarget"), 1);
			}
			S->Duel(kRival, false);
			S->CalmSkill(604);
			if (UAsdaNetSubsystem* Nn = S->N()) { Nn->bStandCaptureSkillUse = S->bWasStandSkill; Nn->ClearMoveRequestInFlight(TEXT("самотест Grenade: после шага 7")); }
			if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(0, -1); }
		});
	}

	// ── 8. состояние 285 «Отпор»: на мобе (свой удар 183 с битом 0x02 — состояние своего удара) и на себе (5272) — 8215 вид 1, навык 285 ──
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		const int32 Rid = 30990 + Pass;
		Add(Pass == 0 ? FString(TEXT("8 «Отпор» на мобе — 8215 вид 1")) : (Pass == 1 ? FString(TEXT("8б «Отпор» на себе — 8215 вид 1 по цели"))
			: FString(TEXT("8в контроль asda.Skill.RepulseState285 0"))), 6.f, [S, Pass, Rid]()
		{
			S->Mark();
			S->SetKey(TEXT("asda.Skill.RepulseState285"), Pass == 2 ? 0 : 1);
			S->SetKey(TEXT("asda.Skill.OwnHitStates"), 1);
			if (AAsdaPlayerCharacter* C = S->Ch()) { S->SpawnMob(Rid, AsdaCoord::GameToTile(C->GetGamePosXZ()) + FIntPoint(3, 0)); }
		}, [S, Pass, Rid]()
		{
			if (!S->bSampled && S->Mob(Rid) && S->El() > 1.0)
			{
				S->Mark(); S->bSampled = true;
				if (Pass == 1)
				{
					if (AAsdaPlayerCharacter* C = S->Ch()) { C->AttackFaceTarget = S->Mob(Rid); }
					TArray<uint8> St; St.SetNumZeroed(14);   // 5272: userIdx@0, состояние@2, стек@9, вид@10, остаток@11 (с)
					AsdaBytes::WrI16(&St[0], S->Self); AsdaBytes::WrI16(&St[2], 285); St[9] = 1; St[10] = 0; AsdaBytes::WrI16(&St[11], 5);
					S->Feed(5272, St);
				}
				else
				{
					// Сервер накладывает «Отпор» мобу записью 4102 (17 б: вид@0, цель@1, навык@3, урон@5, HP@9, наложить@13, кастер@14, стек@16) —
					//   навык 183 даёт состояние 285 (BuffFx «176/1;183/1»), кастер — я (эталон `mob + 4669`).
					TArray<uint8> B; B.SetNumZeroed(17);
					B[0] = 0; AsdaBytes::WrI16(&B[1], Rid); AsdaBytes::WrI16(&B[3], 183); AsdaBytes::WrI32(&B[9], 800);
					B[13] = 1; AsdaBytes::WrI16(&B[14], S->Self); B[16] = 1;
					S->Feed(4102, B);
				}
				S->StepStartS = FPlatformTime::Seconds();
			}
			return S->bSampled && S->El() > 3.0;
		}, [S, Pass, Rid]()
		{
			int32 N1 = 0;
			for (const FOut& D : S->OutsOf(8215))
			{
				if (D.Payload.Num() == 17 && D.Payload[0] == 1 && AsdaBytes::RdI16(&D.Payload[9]) == 285 && AsdaBytes::RdI16(&D.Payload[7]) == Rid) { ++N1; }
			}
			if (Pass < 2)
			{
				S->Expect(N1 == 1, FString::Printf(TEXT("«Отпор» на %s: 8215 вид 1 по мобу rid=%d с навыком 285 — штук %d (эталон: обработчик состояния 285)"),
					Pass == 0 ? TEXT("мобе") : TEXT("себе"), Rid, N1));
			}
			else
			{
				S->Expect(N1 == 0, FString::Printf(TEXT("прежний порт: 8215 вида 1 нет (штук %d) — дефект воспроизведён"), N1));
				S->SetKey(TEXT("asda.Skill.RepulseState285"), 1);
			}
			S->CalmSkill(183);
			TArray<uint8> B4; B4.SetNumZeroed(2); AsdaBytes::WrI16(&B4[0], Rid); S->Feed(4011, B4);
		});
	}

	// ── 9. ОТЛЁТ моба (состояние 14) от удара навыка — `charSkillCast_SetCastTargetDir@0xA296C0`: behaviorType 0 — сильный
	//   (клип слота 14, 450 юн за 600 мс от кастера, снап в конец), 1/2 — слабый (клип 15 на месте); только ОСНОВНАЯ цель записи;
	//   серверный ход во время отлёта ОТКЛАДЫВАЕТСЯ и сливается по концу клипа; ключ 0 — прежний порт. 9г — «Отпор» (285) своего
	//   кастера по мобу: точка = цель + 450·forward кастера (дизассемблер `cameraComputeCollisionTarget` 0xF8809B..0xF8811D). ──
	{
		struct FKnockObs
		{
			FVector2D Start = FVector2D::ZeroVector, Away = FVector2D::ZeroVector, Last = FVector2D::ZeroVector;
			double T0 = -1.0, EndG = -1.0, AttackG = -1.0;
			float DistMid = -1.f, DistEnd = -1.f, DotEnd = 0.f, MaxStep = 0.f, DistFinal = -1.f;
			bool bAttackDuringKnock = false, bFed4015 = false, bFedMove = false, bPendingSeen = false, bSecond14 = false; int32 State14 = 0;
			float MaxStepAfter = 0.f; FVector2D EndPos = FVector2D::ZeroVector; bool bEndPos = false;
			double PrevG = -1.0;   // игровое время прошлого опроса — длина кадра для «шага за кадр» после отлёта
			double T0LagS = 0.0;   // ★ R5-fix6: на сколько опрос, увидевший начало отлёта, отстоит от прошлого сверх кадра 1/60 с
			double TkMid = -1.0;   // ★ R5-fix6: фактическое время от начала отлёта, когда снят замер «середины»
			bool bWas14 = false;   // ★ R5-fix7: на прошлом опросе моб был в отлёте (состояние 14)
		};
		auto PickSkill = [](UWorld* Wd, bool bStrong) -> int32
		{
			const UGameInstance* G = Wd ? Wd->GetGameInstance() : nullptr;
			const UAsdaSkillDatabase* Db = G ? G->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
			if (!Db) { return -1; }
			static const int32 kPref[] = { 502, 506, 515, 1062, 503, 512 };   // skill_info +0x8B: 0 — 502/506/515/544/547…, 1 — 1062/503…, 2 — 512…
			auto Ok = [Db, bStrong](int32 Id)
			{
				const FAsdaSkillDef* D = Db->Get(Id);
				return D && Id != 537 && Id != 597 && (bStrong ? D->BehaviorType == 0 : (D->BehaviorType == 1 || D->BehaviorType == 2));
			};
			for (const int32 Id : kPref) { if (Ok(Id)) { return Id; } }
			for (int32 Id = 500; Id < 1500; ++Id) { if (Ok(Id)) { return Id; } }
			return -1;
		};
		for (int32 Pass = 0; Pass < 5; ++Pass)
		{
			const int32 Rid = 30995 + 2 * Pass, Rid2 = Rid + 1;
			const bool bRep = (Pass >= 3);   // 9г/9д — «Отпор»
			TSharedRef<FKnockObs> K = MakeShared<FKnockObs>();
			TSharedRef<int32> Sk = MakeShared<int32>(-1);
			static const TCHAR* const Names[] = { TEXT("9а отлёт СИЛЬНЫЙ (behaviorType 0): 450 юн за 600 мс, ход сервера отложен"),
				TEXT("9б отлёт СЛАБЫЙ (behaviorType 1/2): клип 15 на месте"), TEXT("9в контроль asda.Skill.KnockBack 0"),
				TEXT("9г «Отпор» своего кастера по мобу: отлёт по курсу кастера"), TEXT("9д контроль asda.Skill.Repulse285Cell 0 (эталонный доводчик)") };
			static const TCHAR* const Pfx[] = { TEXT("9a"), TEXT("9b"), TEXT("9v"), TEXT("9g"), TEXT("9d") };
			Add(Names[Pass], 9.f, [S, Pass, Rid, Rid2, SpawnTrio, Sk, PickSkill]()
			{
				S->Mark();
				S->SetKey(TEXT("asda.Skill.KnockBack"), Pass == 2 ? 0 : 1);
				S->SetKey(TEXT("asda.Skill.RepulseState285"), 1); S->SetKey(TEXT("asda.Skill.OwnHitStates"), 1);
				S->SetKey(TEXT("asda.Skill.Repulse285Cell"), Pass == 4 ? 0 : 1);
				*Sk = (Pass >= 3) ? 285 : PickSkill(S->World.Get(), Pass != 1);
				SpawnTrio({ Rid, Rid2, Rid2 + 100 }, FIntPoint(3, 0));
			}, [S, Pass, bRep, Rid, Rid2, K, Sk]()
			{
				AAsdaMobActor* M = S->Mob(Rid);
				AAsdaPlayerCharacter* C = S->Ch();
				if (!M || !C || *Sk < 0) { return S->El() > 3.0; }
				const FVector2D P = AsdaCoord::UeToGame2D(M->GetActorLocation());
				if (!S->bSampled)
				{
					if (S->El() <= 1.0) { return false; }
					S->Mark(); S->MarkGame(); S->bSampled = true;   // Mark сбрасывает bSampled — порядок важен
					K->Start = P; K->Last = P;
					const FVector2D CG = AsdaCoord::UeToGame2D(C->GetActorLocation());
					if (bRep)
					{
						// Курс кастера — под 60° к линии «я → моб»: по прежней формуле «от кастера» точка ушла бы в другую сторону.
						C->FaceGameDir((P - CG).GetSafeNormal().GetRotated(60.f));
						K->Away = C->GetFacingDirGame().GetSafeNormal();
						TArray<uint8> B; B.SetNumZeroed(17);
						B[0] = 0; AsdaBytes::WrI16(&B[1], Rid); AsdaBytes::WrI16(&B[3], 183); AsdaBytes::WrI32(&B[9], 800);
						B[13] = 1; AsdaBytes::WrI16(&B[14], S->Self); B[16] = 1;
						S->Feed(4102, B);
					}
					else
					{
						K->Away = (P - CG).GetSafeNormal();
						S->Feed(5257, Body5257N(S->Self, *Sk, { Rid, Rid2 }, 50, 0x01, 500));   // Rid — основная цель, Rid2 — вторая запись
					}
					return false;
				}
				const double G = S->GEl();
				if (M->GetEtalonActionState() == 14)
				{
					++K->State14;
					if (K->T0 < 0.0) { K->T0 = G; K->T0LagS = (K->PrevG >= 0.0) ? FMath::Max(0.0, (G - K->PrevG) - 1.0 / 60.0) : 0.0; }
					if (M->IsAttackAnimActive()) { K->bAttackDuringKnock = true; }
				}
				else if (K->T0 >= 0.0 && K->EndG < 0.0)
				{
					// ★ 11.11.2026 (D4b): первый опрос ПОСЛЕ состояния 14 приходит уже с первым шагом отложенного хода (моб тикает до
					//   опроса) — точка «конца отлёта» выходила короче на этот шаг: 409/422/382 юн при эталоне 450 ± 20, тем короче, чем
					//   длиннее кадр. Концом отлёта считается дальняя по оси отлёта из двух точек: последний кадр в состоянии 14 или этот.
					K->EndG = G;
					const double EndNow = FVector2D::DotProduct(P - K->Start, K->Away), EndPrev = FVector2D::DotProduct(K->Last - K->Start, K->Away);
					K->EndPos = (EndNow >= EndPrev) ? P : K->Last;
					K->bEndPos = true;
				}
				if (K->EndG < 0.0 && M->HasPendingNetAction()) { K->bPendingSeen = true; }
				// ★ 11.11.2026 (D4b): «шаг за кадр» после отлёта — в пересчёте на кадр 1/60 с. Порог 60 юн писался под 60 кадров/с; при
				//   длинном кадре обычный ход моба давал 76 юн и ложный «рывок». Рывок к серверной клетке (≥ 3 клеток) виден и так.
				if (K->bEndPos)
				{
					const double FrameS = (K->PrevG >= 0.0) ? G - K->PrevG : 0.0;
					// ★ ревизия R5-fix7: не делением шага на длину кадра (при кадре 100 мс рывок на 3 клетки давал 50 юн и не был виден), а
					//   вычитанием наибольшего законного пути моба за излишек кадра сверх 1/60 с: добег не быстрее 3 × скорости сервера
					//   (`kSegCatchUpMul`, AsdaMobActor.cpp), скорость пакета погони теста — 100/kSpdDenom юн/мс. Рывок сверх этого виден
					//   при любом кадре; при кадре 1/60 с поправка равна нулю.
					const float AllowWalk = (float)(kWalkMaxUuS * FMath::Max(0.0, FrameS - 1.0 / 60.0));
					K->MaxStepAfter = FMath::Max(K->MaxStepAfter, FMath::Max(0.f, (float)FVector2D::Distance(P, K->Last) - AllowWalk));
				}
				if (const AAsdaMobActor* M2 = S->Mob(Rid2); M2 && M2->GetEtalonActionState() == 14) { K->bSecond14 = true; }
				// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: «шаг за кадр» — за вычетом пути отлёта (450 юн за 0,6 с = 750 юн/с) за время
				//   кадра сверх 1/60 с. Порог 120 юн писался под 60 кадров/с; при кадре 160 мс сам плавный отлёт даёт 120 юн за кадр.
				//   Рывок (≥ 3 клеток сверх пути) виден по-прежнему. При кадре 1/60 с поправка равна нулю.
				{
					const double FrameS = (K->PrevG >= 0.0) ? G - K->PrevG : 0.0;
					// ★ Ревизия: поправка — только пока моб в отлёте (состояние 14); вне отлёта шаг считается как прежде.
					// ★ R5-fix7: и на опросе сразу после отлёта — кадр, в котором состояние 14 кончилось, тоже пройден в отлёте.
					const bool bFly = M->GetEtalonActionState() == 14 || K->bWas14;
					K->bWas14 = M->GetEtalonActionState() == 14;
					// R5-fix7: вне отлёта — тоже вычитанием: наибольший законный путь хода моба (добег 3 × скорости сервера) за излишек
					//   кадра сверх 1/60 с. Отложенный ход при кадре 100 мс даёт 120 юн за кадр — ходьба, а не рывок; рывок на 3 клетки
					//   (300 юн) остаётся виден при любом кадре (ревизия: деление шага на длину кадра его прятало).
					const float Allow = (float)((bFly ? 750.0 : kWalkMaxUuS) * FMath::Max(0.0, FrameS - 1.0 / 60.0));
					K->MaxStep = FMath::Max(K->MaxStep, FMath::Max(0.f, (float)FVector2D::Distance(P, K->Last) - Allow));
				}
				K->Last = P; K->PrevG = G;
				const float Dist = (float)FVector2D::DotProduct(P - K->Start, K->Away);
				if (K->T0 >= 0.0)
				{
					const double Tk = G - K->T0;
					if (K->DistMid < 0.f && Tk >= 0.30) { K->DistMid = Dist; K->TkMid = Tk; S->Shot(1, *FString::Printf(TEXT("%s_mid"), Pfx[Pass])); }
					if (K->DistEnd < 0.f && Tk >= 0.70)
					{
						K->DistEnd = Dist; K->DotEnd = (float)FVector2D::DotProduct((P - K->Start).GetSafeNormal(), K->Away);
						S->Shot(2, *FString::Printf(TEXT("%s_end"), Pfx[Pass]));
					}
					// Серверные пакеты во время отлёта — со СЕРВЕРНОЙ клетки (сервер о клиентском отлёте не знает):
					//   0.20 с — удар 4015 kind=3 по мне: эталон `Recv_CombatHit` case 3 (game.c:547635) при состоянии 13/14 — результат СРАЗУ, без замаха;
					//   0.30 с — погоня 4015 kind=5 (боевой канал; мирный kind=2 эталон не откладывает): откладывается в буфер (+1753) и сливается по концу клипа (`charAction_tickWarp23`).
					if (Pass == 0 && !K->bFed4015 && Tk >= 0.20)
					{
						K->bFed4015 = true;
						S->Feed(4015, Body4015Hit(S->Self, Rid, AsdaCoord::GameToTile(K->Start), 40));
					}
					if (Pass == 0 && !K->bFedMove && Tk >= 0.30)
					{
						K->bFedMove = true;
						const FIntPoint T0 = AsdaCoord::GameToTile(K->Start);
						const FVector2D Side = K->Away.GetRotated(90.f) * 200.f;
						TArray<uint8> B; B.SetNumZeroed(37);   // 4015 погоня (kind 5; мирный ход kind 2 эталон не откладывает): @0 −1, @2 тип, @4 rid, @6 = 2, клетки @7/@9 → @11/@13, скорость @15, темп @17/@19
						const FIntPoint Dst = AsdaCoord::GameToTile(K->Start + Side);
						AsdaBytes::WrI16(&B[0], S->Self); AsdaBytes::WrI16(&B[2], kMobType); AsdaBytes::WrI16(&B[4], Rid); B[6] = 5;   // погоня: @0 — цель (я)
						AsdaBytes::WrI16(&B[7], T0.X); AsdaBytes::WrI16(&B[9], T0.Y); AsdaBytes::WrI16(&B[11], Dst.X); AsdaBytes::WrI16(&B[13], Dst.Y);
						AsdaBytes::WrI16(&B[15], kSpdDenom); AsdaBytes::WrI16(&B[17], 10000); AsdaBytes::WrI16(&B[19], 10000); AsdaBytes::WrI32(&B[25], 100);
						S->Feed(4015, B);
					}
				}
				if (K->EndG > 0.0 && K->AttackG < 0.0 && M->IsAttackAnimActive()) { K->AttackG = G; }
				if (K->EndG > 0.0 && K->AttackG < 0.0 && M->IsMoving()) { K->AttackG = G; }   // AttackG у 9а — момент, когда пошёл отложенный ход
				const bool bDone = (Pass == 1 || Pass == 2) ? G > 2.5 : ((K->EndG > 0.0 && G > K->EndG + 1.2) || G > 6.0);
				if (bDone) { K->DistFinal = Dist; S->Shot(4, *FString::Printf(TEXT("%s_after"), Pfx[Pass])); }
				return bDone;
			}, [S, Pass, bRep, Rid, Rid2, K, Sk]()
			{
				S->Expect(*Sk >= 0, FString::Printf(TEXT("навык прохода: %d"), *Sk));
				if (Pass == 0 || bRep)
				{
					S->Expect(K->State14 > 0, FString::Printf(TEXT("моб в состоянии 14 (отлёт), кадров %d"), K->State14));
					// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: замер «середины» снимается первым опросом после 0,3 с, а начало отлёта
					//   видно первым опросом после него. При кадре 100 мс замер приходился на 0,4 с отлёта (324 юн при окне до 320).
					//   Окно прежней ширины (150…320 вокруг 225) сдвигается на путь эталона за фактическое время замера сверх 0,3 с;
					//   верх — ещё на путь за запаздывание начала сверх кадра 1/60 с. При 60 кадрах/с сдвиг ≤ 12 юн.
					{
						const float Shift = (float)(450.0 * (FMath::Min(K->TkMid, 0.6) - 0.30) / 0.6);
						const float LagShift = (float)(450.0 * FMath::Min(K->T0LagS, 0.3) / 0.6);
						S->Expect(K->TkMid >= 0.30 && K->DistMid > 150.f + Shift && K->DistMid < 320.f + Shift + LagShift,
							FString::Printf(TEXT("плавно: через %.2f с отлёта сдвиг %.0f юн (эталон 450·t/0.6 = %.0f; окно %.0f…%.0f)"),
								K->TkMid, K->DistMid, 225.f + Shift, 150.f + Shift, 320.f + Shift + LagShift));
					}
					S->Expect(FMath::Abs(K->DistEnd - 450.f) < 20.f && K->DotEnd > 0.97f,
						FString::Printf(TEXT("к 0.7 с сдвиг %.0f юн (эталон 450), направление · ожидаемое = %.3f (%s)"), K->DistEnd, K->DotEnd,
							bRep ? TEXT("по курсу кастера") : TEXT("от кастера")));
					if (Pass != 4) S->Expect(K->MaxStep < 120.f, FString::Printf(TEXT("без рывков: наибольший шаг за кадр %.0f юн"), K->MaxStep));
					// 9г: буфера нет, цели преследования нет ⇒ доводчик эталона (`charCastDirectionalEffectIfFar`) ставит моба на клетку, запомненную
					//   при входе в 14 (`ent[282] = ent[144]`), — до неё ≥3 тайлов. Это поведение ОРИГИНАЛА (фиксируем, не правим).
					if (Pass == 3) { S->Expect(FMath::Abs(K->DistFinal - 450.f) < 25.f, FString::Printf(TEXT("[ИСПРАВЛЕНИЕ] после клипа моб остался в клетке отброса (её держит сервер): %.0f юн от старта"), K->DistFinal)); }
					if (Pass == 4) { S->Expect(FMath::Abs(K->DistFinal) < 60.f, FString::Printf(TEXT("эталон: доводчик вернул моба на клетку до отлёта: %.0f юн от старта — дефект воспроизведён"), K->DistFinal)); S->SetKey(TEXT("asda.Skill.Repulse285Cell"), 1); }
					if (Pass != 4) S->Expect(K->bEndPos && FMath::Abs((float)FVector2D::DotProduct(K->EndPos - K->Start, K->Away) - 450.f) < 20.f,
						FString::Printf(TEXT("на конце клипа моб в точке отлёта: %.0f юн"), K->bEndPos ? (float)FVector2D::DotProduct(K->EndPos - K->Start, K->Away) : -1.f));
				}
				if (Pass == 0)
				{
					S->Expect(!K->bAttackDuringKnock, FString::Printf(TEXT("удар 4015 kind=3 во время отлёта — без замаха, результат сразу (эталон case 3 при 13/14): замах %s"),
						K->bAttackDuringKnock ? TEXT("ДА") : TEXT("нет")));
					S->Expect(K->bPendingSeen && K->AttackG > 0.0 && K->AttackG - K->EndG < 0.3,
						FString::Printf(TEXT("погоня 4015 kind=5 во время отлёта ОТЛОЖЕНА (буфер %s) и пошёл по концу клипа через %.2f с"),
							K->bPendingSeen ? TEXT("был") : TEXT("НЕ был"), K->AttackG > 0.0 ? K->AttackG - K->EndG : -1.0));
					S->Expect(K->MaxStepAfter < 60.f, FString::Printf(TEXT("после отлёта моба не дёрнуло к серверной клетке: наибольший шаг за кадр %.0f юн"), K->MaxStepAfter));
				}
				if (Pass == 1)
				{
					S->Expect(K->State14 > 0, FString::Printf(TEXT("слабый: состояние 14, кадров %d"), K->State14));
					S->Expect(FMath::Abs(K->DistFinal) < 10.f, FString::Printf(TEXT("слабый: без сдвига (%.0f юн)"), K->DistFinal));
				}
				if (Pass == 2)
				{
					S->Expect(K->State14 == 0 && FMath::Abs(K->DistFinal) < 10.f,
						FString::Printf(TEXT("прежний порт: отлёта нет (кадров 14: %d, сдвиг %.0f) — дефект воспроизведён"), K->State14, K->DistFinal));
					S->SetKey(TEXT("asda.Skill.KnockBack"), 1);
				}
				if (Pass < 2) { S->Expect(!K->bSecond14, TEXT("вторая запись каста (не основная цель) не отлетает")); }
				if (bRep)
				{
					// Клетка 8215 вид 1 «Отпора» — та же точка «цель + 450·forward кастера».
					const FIntPoint Want = AsdaCoord::GameToTile(K->Start + K->Away * 450.f);
					int32 N = 0; FIntPoint Got(-1, -1);
					for (const FOut& D : S->OutsOf(8215))
					{
						if (D.Payload.Num() == 17 && D.Payload[0] == 1 && AsdaBytes::RdI16(&D.Payload[9]) == 285)
						{
							++N; Got = FIntPoint(AsdaBytes::RdI16(&D.Payload[13]), AsdaBytes::RdI16(&D.Payload[15]));
						}
					}
					S->Expect(N == 1 && FMath::Abs(Got.X - Want.X) <= 1 && FMath::Abs(Got.Y - Want.Y) <= 1,
						FString::Printf(TEXT("8215 «Отпора» (%d шт.): клетка (%d,%d), ожидалась (%d,%d) — цель + 450·forward кастера"), N, Got.X, Got.Y, Want.X, Want.Y));
				}
				S->CalmSkill(*Sk == 285 ? 183 : FMath::Max(*Sk, 0));
				for (const int32 R : { Rid, Rid2, Rid2 + 100 }) { TArray<uint8> B4; B4.SetNumZeroed(2); AsdaBytes::WrI16(&B4[0], R); S->Feed(4011, B4); }
			});
		}
	}

	// ── 10. ФЛИНЧ (состояние 13) запоминает текущую клетку — `CCharacter_SetActionState` case 0xD: `if (prev != 5) ent[282] = ent[144]`.
	//   Запомненная серверная клетка заранее отнесена на 5 клеток вбок (устаревший серверный снимок); удар навыком 504 (behaviorType 3 —
	//   отлёта нет, только флинч). Ключ 1 — по концу клипа доводчик моба НЕ двигает (клетка = где флинч); 0 — прежний порт: ставит на старую. ──
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		struct FFlObs { FVector2D Start = FVector2D::ZeroVector, Stale = FVector2D::ZeroVector; bool bHit = false; double EndG = -1.0; float Final = -1.f, ToStale = -1.f; };
		TSharedRef<FFlObs> F = MakeShared<FFlObs>();
		const int32 Rid = 31010 + Pass;
		Add(Pass == 0 ? FString(TEXT("10 флинч запоминает текущую клетку (доводчик не дёргает)")) : FString(TEXT("10б контроль asda.Mob.FlinchRemembersCell 0")), 8.f,
			[S, Pass, Rid, SpawnTrio]()
		{
			S->Mark();
			S->SetKey(TEXT("asda.Mob.FlinchRemembersCell"), Pass == 0 ? 1 : 0);
			SpawnTrio({ Rid, Rid + 100, Rid + 200 }, FIntPoint(3, 0));
		}, [S, Pass, Rid, F]()
		{
			AAsdaMobActor* M = S->Mob(Rid);
			AAsdaPlayerCharacter* C = S->Ch();
			if (!M || !C) { return S->El() > 3.0; }
			const FVector2D P = AsdaCoord::UeToGame2D(M->GetActorLocation());
			if (!S->bSampled)
			{
				if (S->El() <= 1.0) { return false; }
				S->Mark(); S->MarkGame(); S->bSampled = true;
				F->Start = P;
				const FVector2D Away = (P - AsdaCoord::UeToGame2D(C->GetActorLocation())).GetSafeNormal();
				F->Stale = P + Away.GetRotated(90.f) * 500.f;
				M->RememberServerCell(F->Stale);
				S->Feed(5257, Body5257N(S->Self, 504, { Rid }, 50, 0x01, 500));
				return false;
			}
			const double G = S->GEl();
			if (M->GetEtalonActionState() == 13) { if (!F->bHit) { S->Shot(1, Pass == 0 ? TEXT("10_flinch") : TEXT("10b_flinch")); } F->bHit = true; }
			else if (F->bHit && F->EndG < 0.0) { F->EndG = G; }
			const bool bDone = (F->EndG > 0.0 && G > F->EndG + 0.8) || G > 6.0;
			if (bDone)
			{
				F->Final = (float)FVector2D::Distance(P, F->Start); F->ToStale = (float)FVector2D::Distance(P, F->Stale);
				S->Shot(2, Pass == 0 ? TEXT("10_after") : TEXT("10b_after"));
			}
			return bDone;
		}, [S, Pass, Rid, F]()
		{
			S->Expect(F->bHit, TEXT("моб во флинче (состояние 13)"));
			if (Pass == 0)
			{
				S->Expect(F->Final >= 0.f && F->Final < 30.f,
					FString::Printf(TEXT("после флинча моб на месте (сдвиг %.0f юн): клетка запомнена при входе в 13, доводчик не ставит на старую (до неё %.0f)"), F->Final, F->ToStale));
			}
			else
			{
				S->Expect(F->ToStale >= 0.f && F->ToStale < 60.f,
					FString::Printf(TEXT("прежний порт: доводчик поставил моба на старую клетку (до неё %.0f юн, сдвиг %.0f) — расхождение воспроизведено"), F->ToStale, F->Final));
				S->SetKey(TEXT("asda.Mob.FlinchRemembersCell"), 1);
			}
			S->CalmSkill(504);
			for (const int32 R : { Rid, Rid + 100, Rid + 200 }) { TArray<uint8> B4; B4.SetNumZeroed(2); AsdaBytes::WrI16(&B4[0], R); S->Feed(4011, B4); }
		});
	}

	// ── 11. САМ ПРИМЕНИВШИЙ НЕ В ЗАЯВКЕ (06.11, баг «граната бьёт по бросившему» + «похожие навыки»): режимы 1 дуэль / 2 свободный / 4 командный
	//   («Битва армий»), цели — соперник, моб, я сам; все боевые навыки с областью и отложенным взрывом + одиночные вида 2, что били по себе до 26.10.
	//   В записи 5256 своего userIdx быть не должно, кроме само-кастов (их урон по себе снимает сервер, P109). Эталон: `skillCastAreaTargetsAndSend`
	//   + `charManager_collectTargetsInRect` (себя не берёт), цель — я при слоте 6 — заявки нет (`combatHandleSkillUse`, `skillUseOnTargetDispatch`). ──
	{
		static const int32 kArea[] = { 604, 607, 507, 514, 516, 632, 633, 705, 714, 820, 823, 824, 841, 904, 907, 911, 912, 915, 916, 917, 1044, 1062, 1065, 1066,
			539, 556, 564, 595, 924, 961, 991, 501, 503, 505, 508, 905, 908, 906, 1064, 756, 1234, 1235, 1236, 1243, 1244, 1245, 1249, 1250, 1251, 1252, 1253, 1254 };
		static const int32 kSelfCast[] = { 611, 612, 616, 1046, 547 };   // castType 0, res 0/1 или тогл: запись «я» — путь эталона `skillCastSelfSendPacket`
		constexpr int32 kRival2 = 7842, kRidNS = 31020;
		struct FNs { int32 I = 0, T = 0; double NextS = 0.0; bool bDone = false; };
		for (const int32 Mode : { 1, 2, 4 })
		{
			TSharedRef<FNs> Q = MakeShared<FNs>();
			Add(FString::Printf(TEXT("11 режим PvP %d: сам применивший не в заявке"), Mode), 90.f, [S, Mode]()
			{
				S->Mark();
				S->SetKey(TEXT("asda.Skill.HostileAreaCharTarget"), 1);
				if (AAsdaPlayerCharacter* C = S->Ch())
				{
					const FIntPoint T0 = AsdaCoord::GameToTile(C->GetGamePosXZ());
					if (!S->PeerCh(kRival2)) { S->SpawnPeer(kRival2, TEXT("Соперник2"), T0 + FIntPoint(1, 0)); }
					if (!S->Mob(kRidNS)) { S->SpawnMob(kRidNS, T0 + FIntPoint(0, 1)); }
				}
				TArray<int32> All; for (const int32 C : kArea) { All.Add(C); } for (const int32 C : kSelfCast) { All.Add(C); }
				TArray<uint8> L; L.SetNumZeroed(34 * All.Num());
				for (int32 i = 0; i < All.Num(); ++i) { uint8* P = L.GetData() + 34 * i; AsdaBytes::WrI16(P, All[i]); P[2] = 1; P[3] = 1; }
				S->Feed(5251, L);
				if (UAsdaNetSubsystem* Nn = S->N())
				{
					Nn->SetVitalsMp(S->Self, 99999, 99999, S->Cmd, true);
					S->bWasStandSkill = Nn->bStandCaptureSkillUse; Nn->bStandCaptureSkillUse = true;
				}
				if (UAsdaSkillUseSubsystem* U = S->SU()) { U->bDebugSkipWeaponGate = true; }
				// 4206: userIdx@0, [88]@6, режим@7, соперник/команда@8, код@12. Команды — разные (иначе соперник отсеется как «своя»).
				auto Mode4206 = [S](int32 Uidx, uint8 M, int32 Arg) { TArray<uint8> B; B.SetNumZeroed(13); AsdaBytes::WrI16(&B[0], Uidx); B[6] = M; B[7] = M; AsdaBytes::WrI32(&B[8], Arg); B[12] = 1; S->Feed(4206, B); };
				Mode4206(S->Self, (uint8)Mode, Mode == 1 ? kRival2 : (Mode == 4 ? 1 : -1));
				Mode4206(kRival2, (uint8)Mode, Mode == 1 ? S->Self : (Mode == 4 ? 2 : -1));
			}, [S, Q]()
			{
				if (S->El() < 1.0) { return false; }
				const double Now = FPlatformTime::Seconds();
				if (Now < Q->NextS) { return false; }
				Q->NextS = Now + 0.06;
				const int32 NArea = UE_ARRAY_COUNT(kArea), NAll = NArea + UE_ARRAY_COUNT(kSelfCast);
				if (Q->I >= NAll) { return true; }
				const int32 Code = Q->I < NArea ? kArea[Q->I] : kSelfCast[Q->I - NArea];
				UAsdaSkillUseSubsystem* U = S->SU(); AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
				if (U && PC && C)
				{
					S->CalmSkill(Code); U->DebugSetCastState(0, -1);
					if (UAsdaNetSubsystem* Nn = S->N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест Grenade: шаг 11")); }
					AActor* Tg = Q->T == 0 ? (AActor*)S->PeerCh(kRival2) : (Q->T == 1 ? (AActor*)S->Mob(kRidNS) : (AActor*)C);
					if (Tg) { PC->SetTarget(Tg); }
					U->DispatchByKind(Code, -1);
				}
				if (++Q->T >= 3) { Q->T = 0; ++Q->I; }
				return false;
			}, [S, Mode]()
			{
				int32 NReq = 0, NSelfBad = 0, NSelfOk = 0, NRival = 0; FString Bad; TSet<int32> Codes, NoRival;
				TArray<FOut> AllOut = S->OutsOf(5256); AllOut.Append(S->OutsOf(6158));
				for (const FOut& O : AllOut)
				{
					const TArray<uint8>& B = O.Payload;
					if (B.Num() < 31) { continue; }
					const int32 Code = AsdaBytes::RdI16(&B[24]);
					++NReq; Codes.Add(Code);
					bool bSelfRec = false, bRivalRec = false;
					for (int32 Off = 31; Off + 29 <= B.Num(); Off += 29)
					{
						if (B[Off] != 1) { continue; }
						const int32 Idx = AsdaBytes::RdI16(&B[Off + 1]);
						if (Idx == S->Self) { bSelfRec = true; }
						if (Idx == kRival2) { bRivalRec = true; }
					}
					if (bRivalRec) { ++NRival; }
					bool bSelfCast = false; for (const int32 C : kSelfCast) { bSelfCast |= (C == Code); }
					if (bSelfRec && bSelfCast) { ++NSelfOk; }
					else if (bSelfRec) { ++NSelfBad; Bad += FString::Printf(TEXT(" %d"), Code); }
				}
				FString Sent; { TArray<int32> Arr = Codes.Array(); Arr.Sort(); for (const int32 C : Arr) { Sent += FString::Printf(TEXT(" %d"), C); } }
				S->Expect(NSelfBad == 0, FString::Printf(TEXT("режим %d: заявок %d, с соперником %d, свой userIdx в боевой заявке — %d раз(а)%s; само-каст с записью «я» — %d (урон снимает P109)"),
					Mode, NReq, NRival, NSelfBad, *Bad, NSelfOk));
				FString Miss; for (const int32 C : kArea) { if (!Codes.Contains(C)) { Miss += FString::Printf(TEXT(" %d"), C); } } for (const int32 C : kSelfCast) { if (!Codes.Contains(C)) { Miss += FString::Printf(TEXT(" %d"), C); } }
				S->Expect(Codes.Contains(604) && Codes.Contains(607) && NRival > 0, FString::Printf(TEXT("режим %d: ушли навыки:%s; НЕ ушли:%s"), Mode, *Sent, *Miss));
				if (UAsdaNetSubsystem* Nn = S->N()) { Nn->bStandCaptureSkillUse = S->bWasStandSkill; Nn->ClearMoveRequestInFlight(TEXT("самотест Grenade: после шага 11")); }
				if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(0, -1); U->bDebugSkipWeaponGate = false; }
				auto Off4206 = [S](int32 Uidx) { TArray<uint8> B; B.SetNumZeroed(13); AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[8], -1); B[12] = 1; S->Feed(4206, B); };
				Off4206(S->Self); Off4206(kRival2);
			});
		}
	}

	// ── 6. другие площадные и снарядные навыки: ровно один 8215 вид 0 на каждую цель записи, без дублей; рывок 537 — ни одного вида 0 ──
	{
		static const int32 kSkills[] = { 504, 707, 820, 906, 912, 1062, 537 };
		int32 Base = 30970;
		for (const int32 Sk : kSkills)
		{
			const TArray<int32> Rr = { Base, Base + 1, Base + 2 };
			Base += 3;
			Add(FString::Printf(TEXT("6 навык %d по трём мобам — 8215"), Sk), 8.f, [S, SpawnTrio, Rr]()
			{
				S->Mark();
				SpawnTrio(Rr, FIntPoint(5, 0));
			}, [S, Rr, Sk]()
			{
				if (!S->bSampled && S->Mob(Rr[0]) && S->Mob(Rr[1]) && S->Mob(Rr[2]) && S->El() > 1.0)
				{
					S->Mark(); S->bSampled = true;
					S->Feed(5257, Body5257N(S->Self, Sk, Rr, 50, 0x01, 500));   // не смертельно: мобы остаются (HP после 500)
					S->StepStartS = FPlatformTime::Seconds();
				}
				return S->bSampled && S->El() > 4.0;
			}, [S, Rr, Sk]()
			{
				TMap<int32, int32> Per; int32 Bad = 0;
				for (const FOut& D : S->OutsOf(8215))
				{
					if (D.Payload.Num() != 17) { ++Bad; continue; }
					if (D.Payload[0] != 0) { continue; }   // вид 1 — рывок (свой отправитель)
					if (AsdaBytes::RdI16(&D.Payload[9]) != Sk) { ++Bad; continue; }
					Per.FindOrAdd(AsdaBytes::RdI16(&D.Payload[7]))++;
				}
				if (Sk == 537)
				{
					S->Expect(Per.Num() == 0 && Bad == 0, FString::Printf(TEXT("рывок 537: 8215 вида 0 нет (свой 8215 вид 1 — Char_UpdateDash), вида 0 целей %d"), Per.Num()));
				}
				else
				{
					// По одному 8215 на каждый ПОКАЗАННЫЙ удар (эталон шлёт его в каждой фазе многоударного тика): 1…3 на цель (707 — 3 фазы).
					bool bOk = Per.Num() == 3 && Bad == 0;
					FString Cnt;
					for (const int32 Rid : Rr) { const int32 N = Per.FindRef(Rid); bOk = bOk && N >= 1 && N <= 3; Cnt += FString::Printf(TEXT(" %d"), N); }
					S->Expect(bOk, FString::Printf(TEXT("навык %d: 8215 вид 0 по каждой из 3 целей, по одному на показанный удар (по целям:%s; лишних %d)"), Sk, *Cnt, Bad));
				}
				S->CalmSkill(Sk);
				for (const int32 Rid : Rr) { TArray<uint8> B4; B4.SetNumZeroed(2); AsdaBytes::WrI16(&B4[0], Rid); S->Feed(4011, B4); }
			});
		}
	}

	Add(TEXT("итог"), 2.f, [S, Net]() { Cleanup(S, Net); }, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaCombat3Test, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestGrenade(
	TEXT("asda.Skill.SelfTestGrenade"),
	TEXT("Самотест «Граната» 604 (05.11): бросок дугой ~1.5 с, взрыв у каждой из трёх целей, 8215 вид 0 по каждой, смерть, трупы не встают от "
	     "запоздавшего удара; контроли asda.Skill.HitConfirm8215 0 и asda.Skill.GrenadeArc 0. Только офлайн. `shot=<папка>` `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestGrenade"));
		FAsdaCombat3SelfTest::RunGrenade(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
