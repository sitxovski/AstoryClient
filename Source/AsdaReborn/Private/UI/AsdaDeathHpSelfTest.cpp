// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СМЕРТЬ = ПОЛОСА HP В НУЛЕ» (08.11.2026, пачка 19 баг 3: «умер, а полоса 1282/15277») — `asda.SelfTest.DeathHp`.
//   [ПОРТ] Только офлайн (`-AsdaNoNet`): входящие — штатный приём `DebugFeedPacket` (= `HandlePacket` сокета), свой персонаж —
//   `Asda.World.FakeSelfSpawn`, соседи — `DebugSpawnFakePeer`, мобы — запись 4013. Разбор и ключи — `AsdaNetSubsystem_Combat.cpp`
//   (`asda.Death.ZeroVitals`, `asda.Death.ZeroPeerHp`), эталон: entity_char.c:16913 (состояние 6: `UI_UpdateHPBar(0)`, HP = 0),
//   ui_core.c:75482 (покадрово при состоянии 6 HP/MP = 0), network.c:37680 (удар пишет HP только при остатке > 0).
//   Шаги (HUD — текст MINISTATE_HPTEXT настоящего мини-статуса, хранилище — `GetVitals`):
//     1) удары моба 4015 kind=3: 3282 → 2282 → 1282, убивающий удар 2000 (> остатка) — HP 1282 (эталон не пишет), затем 4053 —
//        HP 0, MP 0, текст «0/15277»; поздний 4058 со старым HP 1282 — остаётся 0; воскрешение 4056 — HP/MP пакета;
//     2) контроль `asda.Death.ZeroVitals 0` — после 4053 полоса 1282/15277 (баг владельца воспроизведён);
//     3) урон и смерть в ОДНОМ кадре (4015 убивающий + 4053 одной пачкой) — HP 0;
//     4) многоударный навык соседа 513 (5270, 4 записи по мне, по 1000) при HP 2500, 4053 посередине фаз (1.25 с) — HP 0 сразу и до
//        конца клипа (поздние фазы HP не поднимают и не уводят в минус); журнал HP по времени;
//     5) смерть противника (4053 соседа): его HP в хранилище 0 [ИСПРАВЛЕНИЕ]; контроль `asda.Death.ZeroPeerHp 0` — остаток; 4056 соседа — HP;
//     6) моб: 4015 с HP 0 — у моба HP 0.
//   `shot` — кадры в Reports/batch19/death_hp/ (<шаг>_<ru|en>.png). Итог: «САМОТЕСТ-СМЕРТЬ-HP ИТОГ».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaMiniStateSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCombatTrace.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDeathHpTest, Log, All);

namespace AsdaDeathHpTestLocal
{
	constexpr int32 kSelfFallback = 4322;   // свой userNum, если не известен
	constexpr int32 kP = 7843;              // сосед-противник
	constexpr int32 kRid = 4101;            // моб, бьющий меня
	constexpr int32 kRid2 = 4102;           // моб шага 6
	constexpr int32 kMobType = 1;           // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;        // знаменатель скорости 4013/4015
	constexpr int32 kHpMax = 15277;         // максимум со скриншота владельца
	constexpr int32 kMpMax = 255;

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
		FTimerHandle Tick; bool bQuit = false; bool bShot = false;
		FAsdaSelfTestRunGate Gate{ TEXT(""), false };
		int32 Ok = 0, Fail = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		int32 Self = -1;
		FIntPoint Home = FIntPoint(256, 256);
		// Поэтапные метки шага.
		int32 Phase = 0; double PhaseS = 0.0;
		int32 HpBeforeDeath = -1, HpAtDeath = -1, MinHpAfterDeath = INT32_MAX, MaxHpAfterDeath = -1;
		double DeathS = -1.0;
		int32 LastLoggedHp = INT32_MIN;
		FString TextAtDeath;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 Rid) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(Rid) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		double PEl() const { return FPlatformTime::Seconds() - PhaseS; }
		void NextPhase() { ++Phase; PhaseS = FPlatformTime::Seconds(); }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaDeathHpTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }   // исходное значение вернёт `Finish` (RestoreAll)
		int32 Hp(int32 U) const { const UAsdaNetSubsystem* Nn = N(); const UAsdaNetSubsystem::FAsdaVitals* V = Nn ? Nn->GetVitals(U) : nullptr; return V ? V->HpCur : -1; }
		int32 Mp(int32 U) const { const UAsdaNetSubsystem* Nn = N(); const UAsdaNetSubsystem::FAsdaVitals* V = Nn ? Nn->GetVitals(U) : nullptr; return V ? V->MpCur : -1; }
		// Текст HP настоящего мини-статуса (MINISTATE_HPTEXT, контрол 32).
		FString HudHpText() const
		{
			UGameInstance* G = GI();
			const UAsdaMiniStateSubsystem* M = G ? G->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
			UUserWidget* Wd = M ? M->GetMiniWidget() : nullptr;
			const UTextBlock* T = (Wd && Wd->WidgetTree) ? Cast<UTextBlock>(Wd->WidgetTree->FindWidget(FName(TEXT("MINISTATE_HPTEXT")))) : nullptr;
			return T ? T->GetText().ToString() : FString(TEXT("<нет HPTEXT>"));
		}
		// Журнал HP по времени (каждое изменение).
		void TraceHp(const TCHAR* What)
		{
			const int32 H = Hp(Self);
			if (H == LastLoggedHp) { return; }
			LastLoggedHp = H;
			UE_LOG(LogAsdaDeathHpTest, Log, TEXT("%s [HP %s] t=%.3f с: HP=%d текст HUD «%s» мёртв=%s"), Tag, What, El(), H, *HudHpText(),
				(N() && N()->bSelfDead) ? TEXT("да") : TEXT("нет"));
			ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("САМОТЕСТ DeathHp [%s] t=%.3f: HP=%d HUD «%s»"), What, El(), H, *HudHpText());
		}
		void Shot(const TCHAR* Name)
		{
			if (!bShot) { return; }
			const FString Path = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), TEXT("../Reports/batch19/death_hp"),
				FString::Printf(TEXT("%s_%s.png"), Name, FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en")));
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/true, /*bAddUniqueSuffix=*/false);
			UE_LOG(LogAsdaDeathHpTest, Log, TEXT("%s кадр %s"), Tag, *Path);
		}
		// Живой персонаж с HP/MP пакетом 4058/4059-эквивалентом хранилища.
		void Alive(int32 HpV, int32 MpV)
		{
			if (UAsdaNetSubsystem* Nn = N())
			{
				Nn->bSelfDead = false;
				Nn->SetVitalsHp(Self, HpV, kHpMax, TEXT("самотест DeathHp"), true);
				Nn->SetVitalsMp(Self, MpV, kMpMax, TEXT("самотест DeathHp"), true);
			}
			HpBeforeDeath = HpAtDeath = MaxHpAfterDeath = -1; MinHpAfterDeath = INT32_MAX; DeathS = -1.0; TextAtDeath.Reset();
			Phase = 0; PhaseS = FPlatformTime::Seconds(); LastLoggedHp = INT32_MIN;
		}
		void NoteAfterDeath()
		{
			if (DeathS < 0.0) { return; }
			const int32 H = Hp(Self);
			MinHpAfterDeath = FMath::Min(MinHpAfterDeath, H);
			MaxHpAfterDeath = FMath::Max(MaxHpAfterDeath, H);
		}
	};

	// 4015 kind=3 (37 б) — удар моба по игроку: @0 жертва, @2 тип, @4 rid, @6 = 3, клетки, @21 результат удара, @25 HP моба.
	static TArray<uint8> Body4015Hit(int32 Victim, int32 Rid, const FIntPoint& Cur, int32 Dmg)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], Victim); AsdaBytes::WrI16(&B[2], kMobType); AsdaBytes::WrI16(&B[4], Rid); B[6] = 3;
		AsdaBytes::WrI16(&B[7], Cur.X); AsdaBytes::WrI16(&B[9], Cur.Y); AsdaBytes::WrI16(&B[11], Cur.X); AsdaBytes::WrI16(&B[13], Cur.Y);
		AsdaBytes::WrI16(&B[15], kSpdDenom); AsdaBytes::WrI16(&B[17], 10000); AsdaBytes::WrI16(&B[19], 10000);
		AsdaBytes::WrI32(&B[21], Dmg); AsdaBytes::WrI32(&B[25], 100); B[29] = 0; B[30] = 0; AsdaBytes::WrI16(&B[31], -1);
		return B;
	}
	// 4015 kind=1 (стоп на клетке) с HP моба (раскладка `AsdaCombatHitFrameSelfTest`).
	static TArray<uint8> Body4015Hp(int32 Rid, const FIntPoint& Tile, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(37);
		AsdaBytes::WrI16(&B[0], -1); AsdaBytes::WrI16(&B[2], (int16)kMobType); AsdaBytes::WrI16(&B[4], (int16)Rid); B[6] = 1;
		AsdaBytes::WrI16(&B[7], (int16)Tile.X); AsdaBytes::WrI16(&B[9], (int16)Tile.Y); AsdaBytes::WrI16(&B[11], (int16)Tile.X); AsdaBytes::WrI16(&B[13], (int16)Tile.Y);
		AsdaBytes::WrI32(&B[21], -1); AsdaBytes::WrI32(&B[25], Hp); B[29] = 0xFF; AsdaBytes::WrI16(&B[31], -1);
		return B;
	}
	// 4053 (4 б): userIdx, убийца.
	using AsdaTestPackets::Body4053;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
	// 4056 (22 б) — воскрешение на месте: uidx, тайл X/Z, HP i32, MP i16, exp i64, @20 состояние 0.
	static TArray<uint8> Body4056(int32 Uidx, const FIntPoint& T, int32 Hp, int32 Mp)
	{
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(&B[0], (int16)Uidx); AsdaBytes::WrI16(&B[2], (int16)T.X); AsdaBytes::WrI16(&B[4], (int16)T.Y);
		AsdaBytes::WrI32(&B[6], Hp); AsdaBytes::WrI16(&B[10], (int16)Mp);
		return B;
	}
	// 4058 — HP игрока (живой журнал: uidx@0, макс@2, тек@6, тип@10, навык@11 −1, «второй»@13 = тек; тело 32 б).
	static TArray<uint8> Body4058(int32 Uidx, int32 Cur, int32 Max)
	{
		TArray<uint8> B; B.SetNumZeroed(32);
		AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[2], Max); AsdaBytes::WrI32(&B[6], Cur);
		AsdaBytes::WrI16(&B[11], -1); AsdaBytes::WrI32(&B[13], Cur);
		return B;
	}
	// Запись 5270 (29 б, байт в байт живая 508 — `AsdaSkillDmgTextSelfTest` Rec29): вид@0, цель@1, урон@3, флаги@7, HP после@11.
	static TArray<uint8> Rec29(uint8 Kind, int32 Idx, int32 Dmg, int32 HpAfter)
	{
		static const uint8 kLive[29] = { 0x00, 0xF0, 0x00, 0x6F, 0x0F, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xDC, 0x2E, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		TArray<uint8> R; R.Append(kLive, 29);
		R[0] = Kind; AsdaBytes::WrI16(&R[1], Idx); AsdaBytes::WrI32(&R[3], Dmg); AsdaBytes::WrI32(&R[11], HpAfter);
		return R;
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
				UE_LOG(LogAsdaDeathHpTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
				ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("САМОТЕСТ DeathHp: шаг «%s»"), *Stp.Name);
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

struct FAsdaDeathHpSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaDeathHpSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaDeathHpTestLocal;
	const TCHAR* const kCmd = TEXT("asda.SelfTest.DeathHp");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-СМЕРТЬ-HP");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	for (const FString& A : Args) { if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { S->bShot = true; } }
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// Воскрешение после шага (4056 на месте) — HP/MP пакета, смерть снята.
	auto Revive = [S](const TCHAR* Why)
	{
		S->Feed(4056, Body4056(S->Self, S->Home, 7638, 127));
		S->Expect(S->N() && !S->N()->bSelfDead && S->Hp(S->Self) == 7638 && S->Mp(S->Self) == 127,
			FString::Printf(TEXT("%s: воскрешение 4056 — HP %d (ждём 7638), MP %d (ждём 127), текст «%s»"), Why, S->Hp(S->Self), S->Mp(S->Self), *S->HudHpText()));
	};

	Add(TEXT("подготовка"), 6.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		S->Self = (int32)Net->SelfUserNum;
		S->Home = AsdaCoord::GameToTile(C->GetGamePosXZ());
		S->SetKey(TEXT("asda.Death.ZeroVitals"), 1);
		S->SetKey(TEXT("asda.Death.ZeroPeerHp"), 1);
		S->Alive(3282, 185);
		S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->Home + FIntPoint(1, 0), 1, kSpdDenom));
		if (UAsdaWorldSubsystem* Ws = S->WS()) { Ws->DebugSpawnFakePeer(kP, TEXT("СоседН"), AsdaCoord::TileCenterGame(S->Home + FIntPoint(0, 3))); }
	}, [S]() { return S->El() > 1.5 && S->Mob(kRid) != nullptr; }, [S]()
	{
		S->Expect(S->Mob(kRid) != nullptr, TEXT("моб на месте"));
		S->Expect(S->Hp(S->Self) == 3282 && S->HudHpText() == FString::Printf(TEXT("%d/%d"), 3282, kHpMax),
			FString::Printf(TEXT("HP до боя %d, текст «%s»"), S->Hp(S->Self), *S->HudHpText()));
	});

	// ── 1 и 2: удары моба, убивающий больше остатка, затем 4053 (ключ 1 — HP 0; ключ 0 — 1282, баг владельца) ──
	for (const bool bKey : { true, false })
	{
		const FString Name = bKey
			? TEXT("1 удары моба 1000/1000, убивающий 2000 при остатке 1282, затем 4053 — полоса 0")
			: TEXT("2 контроль asda.Death.ZeroVitals 0 — после 4053 полоса 1282/15277 (баг владельца)");
		Add(Name, 9.f, [S, bKey]()
		{
			S->SetKey(TEXT("asda.Death.ZeroVitals"), bKey ? 1 : 0);
			S->Alive(3282, 185);
			S->Feed(4015, Body4015Hit(S->Self, kRid, S->Home + FIntPoint(1, 0), 1000));
		}, [S, bKey]()
		{
			S->TraceHp(TEXT("удары моба"));
			S->NoteAfterDeath();
			if (S->Phase == 0 && S->PEl() > 1.0) { S->Feed(4015, Body4015Hit(S->Self, kRid, S->Home + FIntPoint(1, 0), 1000)); S->NextPhase(); }
			else if (S->Phase == 1 && S->PEl() > 1.0) { S->Feed(4015, Body4015Hit(S->Self, kRid, S->Home + FIntPoint(1, 0), 2000)); S->NextPhase(); }
			else if (S->Phase == 2 && S->PEl() > 1.2)
			{
				S->HpBeforeDeath = S->Hp(S->Self);
				S->Feed(4053, Body4053(S->Self, -1));
				S->HpAtDeath = S->Hp(S->Self); S->TextAtDeath = S->HudHpText(); S->DeathS = FPlatformTime::Seconds();
				S->NextPhase();
			}
			else if (S->Phase == 3 && S->PEl() > 0.8)
			{
				S->Shot(bKey ? TEXT("1_death_zero") : TEXT("2_control_old"));
				S->Feed(4058, Body4058(S->Self, 1282, kHpMax));   // поздний снимок с HP до смерти
				S->NextPhase();
			}
			return S->Phase == 4 && S->PEl() > 0.4;
		}, [S, bKey, Revive]()
		{
			S->Expect(S->HpBeforeDeath == 1282, FString::Printf(TEXT("до 4053 HP %d (ждём 1282: убивающий удар 2000 эталон не пишет)"), S->HpBeforeDeath));
			if (bKey)
			{
				S->Expect(S->HpAtDeath == 0 && S->Mp(S->Self) == 0, FString::Printf(TEXT("по 4053 HP %d, MP %d (ждём 0/0)"), S->HpAtDeath, S->Mp(S->Self)));
				S->Expect(S->TextAtDeath == FString::Printf(TEXT("0/%d"), kHpMax), FString::Printf(TEXT("текст HUD в кадре смерти «%s»"), *S->TextAtDeath));
				S->Expect(S->MaxHpAfterDeath == 0, FString::Printf(TEXT("после смерти HP не поднимался (макс. %d, поздний 4058 1282 пришёл)"), S->MaxHpAfterDeath));
				S->Expect(S->HudHpText() == FString::Printf(TEXT("0/%d"), kHpMax), FString::Printf(TEXT("текст HUD в конце «%s»"), *S->HudHpText()));
			}
			else
			{
				S->Expect(S->HpAtDeath == 1282 && S->TextAtDeath == FString::Printf(TEXT("1282/%d"), kHpMax),
					FString::Printf(TEXT("прежний порт: после 4053 HP %d, текст «%s» (баг воспроизведён)"), S->HpAtDeath, *S->TextAtDeath));
			}
			Revive(bKey ? TEXT("1") : TEXT("2"));
			S->SetKey(TEXT("asda.Death.ZeroVitals"), 1);
		});
	}

	// ── 3: урон и смерть в одном кадре ──
	Add(TEXT("3 убивающий удар и 4053 в одном кадре — полоса 0"), 6.f, [S]()
	{
		S->Alive(1282, 185);
		S->Feed(4015, Body4015Hit(S->Self, kRid, S->Home + FIntPoint(1, 0), 2000));
		S->Feed(4053, Body4053(S->Self, -1));
		S->HpAtDeath = S->Hp(S->Self); S->TextAtDeath = S->HudHpText(); S->DeathS = FPlatformTime::Seconds();
	}, [S]()
	{
		S->TraceHp(TEXT("один кадр"));
		S->NoteAfterDeath();
		if (S->Phase == 0 && S->El() > 1.2) { S->Shot(TEXT("3_same_frame")); S->NextPhase(); }
		return S->El() > 2.0;
	}, [S, Revive]()
	{
		S->Expect(S->HpAtDeath == 0 && S->TextAtDeath == FString::Printf(TEXT("0/%d"), kHpMax),
			FString::Printf(TEXT("HP %d, текст «%s» сразу после пачки"), S->HpAtDeath, *S->TextAtDeath));
		S->Expect(S->MaxHpAfterDeath == 0 && S->MinHpAfterDeath == 0, FString::Printf(TEXT("HP после смерти %d…%d (ждём 0)"), S->MinHpAfterDeath, S->MaxHpAfterDeath));
		Revive(TEXT("3"));
	});

	// ── 4: многоударный навык соседа (513, 4 записи по мне), смерть посередине фаз ──
	Add(TEXT("4 навык соседа 513 (5270, 4 записи по 1000) при HP 2500, 4053 на 1.25 с — полоса 0 до конца клипа"), 8.f, [S]()
	{
		S->Alive(2500, 185);
		TArray<uint8> B; B.SetNumZeroed(7);   // 5270: кастер u16, навык u16, уровень u16, [6] = 1, записи с +7
		AsdaBytes::WrI16(&B[0], kP); AsdaBytes::WrI16(&B[2], 513); AsdaBytes::WrI16(&B[4], 5); B[6] = 1;
		const int32 HpAfter[4] = { 1500, 500, 0, 0 };
		for (int32 k = 0; k < 4; ++k) { B.Append(Rec29(1, S->Self, 1000, HpAfter[k])); }
		S->Feed(5270, B);
	}, [S]()
	{
		S->TraceHp(TEXT("513 соседа"));
		S->NoteAfterDeath();
		if (S->Phase == 0 && S->El() > 1.25)
		{
			S->HpBeforeDeath = S->Hp(S->Self);
			S->Feed(4053, Body4053(S->Self, kP));
			S->HpAtDeath = S->Hp(S->Self); S->TextAtDeath = S->HudHpText(); S->DeathS = FPlatformTime::Seconds();
			S->NextPhase();
		}
		else if (S->Phase == 1 && S->El() > 2.0) { S->Shot(TEXT("4_multihit_mid")); S->NextPhase(); }
		return S->El() > 3.6;   // клип 513 — 3.0 с, четвёртая фаза 2.5 с
	}, [S, Revive]()
	{
		UE_LOG(LogAsdaDeathHpTest, Log, TEXT("%s [513] HP перед 4053 = %d (записи по 1000 от 2500; фазы 667/1100/1533/2500 мс)"), S->Tag, S->HpBeforeDeath);
		S->Expect(S->HpBeforeDeath > 0, FString::Printf(TEXT("до смерти HP > 0 (%d) — показанные фазы вычтены, ноль от ударов не пишется"), S->HpBeforeDeath));
		S->Expect(S->HpAtDeath == 0 && S->TextAtDeath == FString::Printf(TEXT("0/%d"), kHpMax),
			FString::Printf(TEXT("по 4053 посередине фаз HP %d, текст «%s»"), S->HpAtDeath, *S->TextAtDeath));
		S->Expect(S->MaxHpAfterDeath == 0 && S->MinHpAfterDeath == 0,
			FString::Printf(TEXT("поздние фазы после смерти: HP %d…%d (ждём 0, не выше и не в минус)"), S->MinHpAfterDeath, S->MaxHpAfterDeath));
		Revive(TEXT("4"));
	});

	// ── 5: смерть противника ──
	Add(TEXT("5 смерть противника 4053 — его HP 0 [ИСПРАВЛЕНИЕ]; контроль ZeroPeerHp 0; 4056 соседа"), 4.f, [S]()
	{
		UAsdaNetSubsystem* Nn = S->N();
		Nn->SetVitalsHp(kP, 1500, 15000, TEXT("самотест DeathHp"), true);
		S->Feed(4053, Body4053(kP, S->Self));
		S->Expect(S->Hp(kP) == 0, FString::Printf(TEXT("HP противника после 4053 = %d (ждём 0)"), S->Hp(kP)));
		S->Expect(!Nn->bSelfDead && S->Hp(S->Self) == 7638, FString::Printf(TEXT("своё HP не тронуто (%d), я жив"), S->Hp(S->Self)));
		S->SetKey(TEXT("asda.Death.ZeroPeerHp"), 0);
		Nn->SetVitalsHp(kP, 1500, 15000, TEXT("самотест DeathHp"), true);
		S->Feed(4053, Body4053(kP, S->Self));
		S->Expect(S->Hp(kP) == 1500, FString::Printf(TEXT("контроль ZeroPeerHp 0: HP противника %d (прежний остаток 1500)"), S->Hp(kP)));
		S->SetKey(TEXT("asda.Death.ZeroPeerHp"), 1);
		S->Feed(4056, Body4056(kP, S->Home + FIntPoint(0, 3), 7500, 100));
	}, [S]() { return S->El() > 1.0; }, [S]()
	{
		S->Expect(S->Hp(kP) == 7500, FString::Printf(TEXT("воскрешение соседа 4056: HP %d (ждём 7500)"), S->Hp(kP)));
	});

	// ── 6: моб — HP 0 по пакету ──
	Add(TEXT("6 моб: 4015 с HP 0 — у моба HP 0"), 5.f, [S]()
	{
		S->Phase = 0; S->PhaseS = FPlatformTime::Seconds();   // счётчик фаз — свой у шага (шаг 4 оставляет 2)
		S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid2, kMobType, S->Home + FIntPoint(-2, 0), 1, kSpdDenom));
	}, [S]()
	{
		if (S->Phase == 0 && S->El() > 0.8 && S->Mob(kRid2)) { S->Feed(4015, Body4015Hp(kRid2, S->Home + FIntPoint(-2, 0), 0)); S->NextPhase(); }
		return S->Phase == 1 && S->PEl() > 1.5;
	}, [S]()
	{
		const AAsdaMobActor* M = S->Mob(kRid2);
		S->Expect(!M || M->CurHp == 0, FString::Printf(TEXT("моб rid %d: HP %d, мёртв=%s (ждём HP 0)"), kRid2, M ? M->CurHp : -1, (M && M->IsDead()) ? TEXT("да") : TEXT("нет/удалён")));
	});

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		for (const int32 R : { kRid, kRid2 }) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], R); S->Feed(4011, B); }   // уборка, как `AsdaCombat3SelfTest`
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kP); S->Feed(4011, B); }
		Net->bSelfDead = false;
		Net->SetVitalsHp(S->Self, kHpMax, kHpMax, TEXT("самотест DeathHp"), true);
		Net->SetVitalsMp(S->Self, kMpMax, kMpMax, TEXT("самотест DeathHp"), true);
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaDeathHpTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestDeathHp(
	TEXT("asda.SelfTest.DeathHp"),
	TEXT("Самотест «смерть = полоса HP в нуле» (08.11, пачка 19 баг 3): удары моба с убивающим больше остатка + 4053, контроль "
	     "asda.Death.ZeroVitals 0, урон и смерть в одном кадре, многоударный 513 соседа со смертью посередине фаз, смерть противника "
	     "(asda.Death.ZeroPeerHp), моб с HP 0. Только офлайн. `shot` — кадры в Reports/batch19/death_hp/. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.SelfTest.DeathHp"));
		FAsdaDeathHpSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
