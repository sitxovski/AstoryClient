// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НАВЫК НА СЕБЯ НЕ БЬЁТ КАСТЕРА» (05.11.2026, баг игрока Player5 «получаю урон, когда применяю King's Rage»).
//   Команда `asda.Skill.SelfTestSelfHit`. [ПОРТ] Только офлайн (`-AsdaNoNet`): входящие — штатный приём `DebugFeedPacket`
//   (= `HandlePacket` сокета), исходящие — перехват `AsdaSelfTestKit::ArmCapture`. Свой персонаж — `Asda.World.FakeSelfSpawn`.
//   Разбор — `AsdaNetSubsystem.cpp` (`AsdaSanitizeSelfHitRecs`, ключ `asda.Skill.SelfHitNoDamage`), серверная пара — P109.
//   Ответы сервера — в порядке и раскладке живого журнала Player5 03.11 20:51:00 (611 Parry): 4058 (сервер уже снял урон с
//   кастера), затем 5257 с записью «персонаж = я», урон 1156, флаги 0x03 (у 20:51:24 — 697, 0x0B).
//   Шаги (каждый навык — путь игрока: изученный навык 5251 → `DispatchByKind` → 5256 ушёл с ОДНОЙ записью «тип 1, свой userNum»):
//     1) 611 Parry / 612 King's Rage / 616 Violent Storm, ответ НЕИСПРАВЛЕННОГО сервера (урон в записи): HP = значению 4058
//        (локально второй раз не вычтено), строки «delivered … to <себе>» нет, цифры над собой нет;
//     2) ответ сервера с P109 (урон 0, флаги 0x03): HP = 4058, строк и цифр нет;
//     3) контроль `asda.Skill.SelfHitNoDamage 0` (612): прежний порт — HP 4058 − урон, строка «delivered» есть (воспроизведение бага);
//     4) чужой урон не тронут: навык соседа по мне (5257 вид 1, кастер — сосед) — HP −урон и строка 20241, как раньше.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaFloatingTextSubsystem.h"
#include "World/AsdaCoord.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSkillSelfHitTest, Log, All);

namespace AsdaSkillSelfHitTestLocal
{
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kP = 7843;               // сосед (шаг 4)
	constexpr int32 kHpMax = 106864;         // HP персонажа живого журнала Player5
	constexpr int32 kHpStart = 106864;       // HP до навыка

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
		bool bWasInGame = false; bool bWasStand = false;
		int32 Self = -1; int32 WasWeaponSub = 99;
		bool bSampled = false;       // ответ сервера уже подан
		int32 PrevLive = 0, Spawns = 0;   // всплывающие надписи за шаг
		int32 LinesBase = 0;         // счётчик искомой строки на старте шага

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		UAsdaFloatingTextSubsystem* FT() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaFloatingTextSubsystem>() : nullptr; }
		UAsdaChatSubsystem* Chat() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaChatSubsystem>() : nullptr; }
		UAsdaTextSubsystem* Txt() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaTextSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaSkillSelfHitTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }   // исходное значение вернёт `Finish` (RestoreAll)
		void CalmSkill(int32 Code) { if (UAsdaSkillUseSubsystem* S = SU()) { S->SimulateCastResult5257(0, Code); } }
		int32 Hp() const { const UAsdaNetSubsystem* Nn = N(); const UAsdaNetSubsystem::FAsdaVitals* V = Nn ? Nn->GetVitals(Self) : nullptr; return V ? V->HpCur : -1; }
		FString SkName(int32 Code) const
		{
			const UGameInstance* G = GI(); const UAsdaSkillDatabase* D = G ? G->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
			const FAsdaSkillDef* Def = D ? D->Get(Code) : nullptr; return Def ? Def->Name : FString();
		}
		FString SelfName() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->GetSelfCharName() : FString(); }
		FString Line(int32 Id, const TArray<FString>& Strs, const TArray<int32>& Nums) const { const UAsdaTextSubsystem* T = Txt(); return T ? T->FormatById(Id, Strs, Nums) : FString(); }
		int32 Count(const FString& L) const { const UAsdaChatSubsystem* C = Chat(); return (C && !L.IsEmpty()) ? C->DebugCountSystemLines(L) : 0; }
		void TrackFloat()
		{
			UAsdaFloatingTextSubsystem* F = FT(); if (!F) { return; }
			const int32 Live = F->DebugLiveCount();
			if (Live > PrevLive) { Spawns += Live - PrevLive; }
			PrevLive = Live;
		}
		void Mark(const FString& L)
		{
			OutFrom = Outs.Num(); bSampled = false;
			PrevLive = FT() ? FT()->DebugLiveCount() : 0; Spawns = 0; LinesBase = Count(L);
		}
		// Исходящий 5256 навыка Code: тело = токен/MD5 (24) + шапка 7 б (код @24) + записи 29 б с @31.
		const FOut* Sent5256(int32 Code) const
		{
			for (int32 i = OutFrom; i < Outs.Num(); ++i)
			{
				if (Outs[i].Op == 5256 && Outs[i].Payload.Num() >= 26 && AsdaBytes::RdI16(&Outs[i].Payload[24]) == Code) { return &Outs[i]; }
			}
			return nullptr;
		}
	};

	// 4058 — HP своего персонажа (живой журнал: uidx@0, макс@2, тек@6, тип@10, навык@11 −1, «второй»@13 = тек; тело 32 б).
	static TArray<uint8> Body4058(int32 Uidx, int32 Cur, int32 Max)
	{
		TArray<uint8> B; B.SetNumZeroed(32);
		AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[2], Max); AsdaBytes::WrI32(&B[6], Cur);
		AsdaBytes::WrI16(&B[11], -1); AsdaBytes::WrI32(&B[13], Cur);
		return B;
	}
	// 5257 (шапка 8 б + 29 б): [0] 1, [1..2] кастер, [3..4] навык, [5] 1, [6..7] уровень 5; запись — вид, цель, урон@3, флаги@7,
	//   стек buff0 @8 = 1, HP после @11 = 0, состояние @19 = −1 (байт в байт как живой 5257 20:51:00).
	static TArray<uint8> Body5257(int32 Caster, int32 Skill, uint8 Kind, int32 Idx, int32 Dmg, uint8 Flags)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 5);
		uint8* R = B.GetData() + 8;
		R[0] = Kind; AsdaBytes::WrI16(R + 1, Idx); AsdaBytes::WrI32(R + 3, Dmg); R[7] = Flags; R[8] = 1;
		AsdaBytes::WrI32(R + 11, 0); AsdaBytes::WrI16(R + 19, -1);
		return B;
	}

	// Исполнитель шагов (как у `asda.Skill.SelfTestDamageText`).
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
				UE_LOG(LogAsdaSkillSelfHitTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
				if (Stp.Start) { Stp.Start(); }
				return;
			}
			St->TrackFloat();
			const double El = Now - St->StepStartS;
			const bool bDone = !Stp.Poll || Stp.Poll();
			if (!bDone && El < Stp.TimeoutSec) { return; }
			if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
			if (Stp.Check) { Stp.Check(); }
			++St->StepIdx; St->bStepStarted = false;
		}), 0.01f, true, KitArgs.Delay + 2.5f);
	}
}

struct FAsdaSkillSelfHitSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaSkillSelfHitSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaSkillSelfHitTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestSelfHit");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-НАВЫК-НА-СЕБЯ");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// 5251 — изученные навыки (34 б на запись): код@0, уровень@2, 1@3.
	auto Body5251 = [](const TArray<int32>& Codes)
	{
		TArray<uint8> B; B.SetNumZeroed(34 * Codes.Num());
		for (int32 i = 0; i < Codes.Num(); ++i) { uint8* P = B.GetData() + 34 * i; AsdaBytes::WrI16(P, Codes[i]); P[2] = 5; P[3] = 1; }
		return B;
	};
	auto Idle = [S]() { const UAsdaSkillUseSubsystem* U = S->SU(); return S->El() > 0.3 && U && U->GetActiveSkill() < 0 && U->GetSelfCharState() == 0; };

	Add(TEXT("подготовка"), 5.f, [S, Net, Body5251]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
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
		S->WasWeaponSub = C->EquippedWeaponSubType; C->EquippedWeaponSubType = 1;   // меч: маска 611/612 = 0x0FFE (подтипы 1…11); 616 — 0x8, ставится в своём шаге
		S->bWasStand = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->Feed(5251, Body5251({ 611, 612, 616 }));
		Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест навыка на себя"), true);
		Net->SetVitalsHp(S->Self, kHpStart, kHpMax, TEXT("самотест навыка на себя"), true);
		S->SetKey(TEXT("asda.Skill.SelfHitNoDamage"), 1);
		S->SetKey(TEXT("asda.Skill.DamageChatLines"), 1);
		if (UAsdaWorldSubsystem* Ws = S->WS()) { Ws->DebugSpawnFakePeer(kP, TEXT("СоседН"), AsdaCoord::TileCenterGame(AsdaCoord::GameToTile(C->GetGamePosXZ()) + FIntPoint(0, 4))); }
	}, [S]() { return S->El() > 1.0; }, [S]()
	{
		for (const int32 Code : { 611, 612, 616 })
		{
			S->Expect(!S->SkName(Code).IsEmpty(), FString::Printf(TEXT("запись навыка %d есть: «%s»"), Code, *S->SkName(Code)));
		}
		S->Expect(S->Hp() == kHpStart, FString::Printf(TEXT("HP до навыков %d"), S->Hp()));
	});

	// ── 1–3. путь игрока: DispatchByKind → 5256 → ответ сервера (4058, затем 5257) ──
	struct FCase { const TCHAR* Name; int32 Skill; int32 Weapon; int32 Dmg; uint8 Flags; bool bServerHit; bool bKeyOn; };   // Weapon — подтип по маске .sif +0x83
	const FCase kCases[] = {
		{ TEXT("1а 611 Parry: сервер без P109 (урон 1156, 0x03 — живой 20:51:00)"),    611, 1, 1156, 0x03, true,  true  },
		{ TEXT("1б 611 Parry: сервер без P109 (урон 697, 0x0B — живой 20:51:24)"),     611, 1, 697,  0x0B, true,  true  },
		{ TEXT("1в 612 King's Rage: сервер без P109 (урон 1840, 0x03)"),                612, 1, 1840, 0x03, true,  true  },
		{ TEXT("1г 616 Violent Storm: сервер без P109 (урон 2210, 0x07 крит)"),          616, 3, 2210, 0x07, true,  true  },
		{ TEXT("2 612 King's Rage: сервер с P109 (урон 0, 0x03)"),                       612, 1, 0,    0x03, false, true  },
		{ TEXT("3 контроль asda.Skill.SelfHitNoDamage 0 (612, урон 1840) — прежний порт"), 612, 1, 1840, 0x03, true,  false },
	};
	for (const FCase& K : kCases)
	{
		const int32 ServerHp = kHpStart - (K.bServerHit ? K.Dmg : 0);   // HP, которое прислал сервер в 4058
		auto LineOf = [S, K]() { return S->Line(20249, { S->SelfName(), S->SkName(K.Skill) }, { K.Dmg }); };
		Add(K.Name, 8.f, [S, K, LineOf]()
		{
			S->SetKey(TEXT("asda.Skill.SelfHitNoDamage"), K.bKeyOn ? 1 : 0);
			if (UAsdaNetSubsystem* Nn = S->N()) { Nn->SetVitalsHp(S->Self, kHpStart, kHpMax, TEXT("самотест навыка на себя"), true); }
			S->Mark(LineOf());
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = K.Weapon; }   // 611/612 — маска 0x0FFE (меч 1), 616 — 0x8 (подтип 3)
			if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(K.Skill, -1); }
		}, [S, K, ServerHp]()
		{
			if (!S->bSampled && S->Sent5256(K.Skill))
			{
				S->bSampled = true;
				S->Feed(4058, Body4058(S->Self, ServerHp, kHpMax));
				S->Feed(5257, Body5257(S->Self, K.Skill, 1, S->Self, K.Dmg, K.Flags));
				S->StepStartS = FPlatformTime::Seconds();
			}
			return S->bSampled && S->El() > 3.5;   // клип 611/612 — 1.6 с; запись выпускается на конце клипа
		}, [S, K, ServerHp, LineOf]()
		{
			const FOut* O = S->Sent5256(K.Skill);
			const bool bRec = O && O->Payload.Num() >= 31 + 29 && O->Payload[31] == 1 && AsdaBytes::RdI16(&O->Payload[32]) == S->Self
				&& O->Payload.Num() == 31 + 29;
			S->Expect(bRec, FString::Printf(TEXT("5256 навыка %d ушёл путём игрока с одной записью «персонаж = я» (uidx %d)"), K.Skill, S->Self));
			const int32 Got = S->Count(LineOf()) - S->LinesBase;
			if (K.bKeyOn)
			{
				S->Expect(S->Hp() == ServerHp, FString::Printf(TEXT("HP = значению сервера 4058: %d (ждём %d; второго вычитания нет)"), S->Hp(), ServerHp));
				S->Expect(Got == 0, FString::Printf(TEXT("строки «урон себе» 20249 нет (+%d): «%s»"), Got, *LineOf()));
				S->Expect(S->Spawns == 0, FString::Printf(TEXT("цифры урона над собой нет (всплытий %d)"), S->Spawns));
			}
			else
			{
				S->Expect(S->Hp() == ServerHp - K.Dmg, FString::Printf(TEXT("прежний порт: HP %d = 4058 %d − урон записи %d (двойной счёт — баг воспроизведён)"), S->Hp(), ServerHp, K.Dmg));
				S->Expect(Got == 1, FString::Printf(TEXT("прежний порт: строка «урон себе» есть (+%d): «%s»"), Got, *LineOf()));
			}
			S->CalmSkill(K.Skill);
			S->SetKey(TEXT("asda.Skill.SelfHitNoDamage"), 1);
		});
		Add(TEXT("пауза (навык свободен)"), 4.f, nullptr, Idle, nullptr);
	}

	// ── 4. чужой урон по мне не тронут ──
	Add(TEXT("4 навык соседа по мне (612, урон 77) — HP −77 и строка 20241, как раньше"), 6.f, [S]()
	{
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->SetVitalsHp(S->Self, kHpStart, kHpMax, TEXT("самотест навыка на себя"), true); }
		S->Mark(S->Line(20241, { TEXT("СоседН") }, { 77 }));
		S->Feed(5257, Body5257(kP, 612, 1, S->Self, 77, 0x01));
	}, [S]() { return S->El() > 3.5; }, [S]()
	{
		const FString L = S->Line(20241, { TEXT("СоседН") }, { 77 });
		S->Expect(S->Hp() == kHpStart - 77, FString::Printf(TEXT("HP %d (ждём %d)"), S->Hp(), kHpStart - 77));
		S->Expect(S->Count(L) - S->LinesBase == 1, FString::Printf(TEXT("строка 20241 «%s» +%d"), *L, S->Count(L) - S->LinesBase));
	});

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kP); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); P->bAutoAttack = false; P->ExitCombatStance(); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->WasWeaponSub; }
		Net->SetVitalsHp(S->Self, kHpMax, kHpMax, TEXT("самотест навыка на себя"), true);
		Net->bStandCaptureSkillUse = S->bWasStand;
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaSkillSelfHitTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillSelfHit(
	TEXT("asda.Skill.SelfTestSelfHit"),
	TEXT("Самотест «навык на себя не бьёт кастера» (05.11, King's Rage): путь игрока DispatchByKind 611/612/616 → 5256 с записью «я», "
	     "ответ 4058 + 5257 живой раскладки — HP только от сервера, нет строки/цифры урона себе; ответ с P109; контроль "
	     "asda.Skill.SelfHitNoDamage 0; чужой урон по мне не тронут. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestSelfHit"));
		FAsdaSkillSelfHitSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
