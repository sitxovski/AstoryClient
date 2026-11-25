// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ЦИФРЫ И СТРОКИ ЧАТА УРОНА НАВЫКА» (31.10.2026, баг игроков «ударил навыком — над целью нет цифр, в чате нет строк»).
//   Команда `asda.Skill.SelfTestDamageText`. [ПОРТ] Только офлайн (`-AsdaNoNet`): входящие — штатный приём `DebugFeedPacket`
//   (= `HandlePacket` сокета), исходящие — перехват `AsdaSelfTestKit::ArmCapture`. Свой персонаж — `Asda.World.FakeSelfSpawn`,
//   сосед — `DebugSpawnFakePeer`, мобы — запись 4013. Разбор — `AsdaWorldSubsystem_Damage.cpp` (`ApplySkillHit`, ключ
//   `asda.Skill.DamageChatLines`), эталон `CharFx_ShowSkillDamageText@0xA7FC00` и `Combat_ShowDamageFeedback@0xA80390`.
//   Шаги:
//     1) путь игрока: выбор моба `SetTarget` + навык `DispatchByKind` (705) → 5256 ушёл → ответ 5257 по мобу; на кадре удара —
//        цифра над мобом и строка в чат: обычный (0x01) — 20249 белым, крит (0x05) — 20247 цветом 0xFF75DAFF, блок (0x09) — 20248,
//        IMMUNE (0x21) — 20010; промах навыком 1252 (урон 0, 0x00) — «MISS» и 20010;
//     2) контроль `asda.Skill.DamageChatLines 0`: цифра есть, строки нет (прежний порт);
//     3) мой навык по соседу (5257 вид 1): 20249 цветом 0xFF75DAFF; навык соседа по мне: 20241 и крит 20240 цветом 0xFFFF2913;
//     4) наблюдатель: навык соседа по мобу — строк нет;
//     5) виды навыков — цифра на КАДРЕ УДАРА (эталон), а не при приёме пакета: без эффекта удара в строке клипа (908, 512), площадь
//        (912 по двум мобам), многоударный (707, три фазы), дальний со снарядом (903), смертельный удар; контроль
//        `asda.Skill.HitFrameAlways 0` — цифра сразу. Кадры — `Reports/bug_skill_dmgtext/frames/*.png`.
//     6) ★ 05.11: многоударные с ДВУМЯ записями по одной цели (508/504/509/513 — 5270, 903 — 5257; тело записи — живой 5270 навыка 508):
//        цифра каждого удара на своей фазе COMBO, по строке 20249 на удар; контроль `asda.Skill.HitRecordPerPhase 0` — обе разом.
//     6н) ★ 08.11 (пачка 17): 513 «Starlight Slasher» — 4 записи по одной цели, удары 667/1100/1533/2500 мс по одному (журнал времени
//        каждого удара, HP после каждого, фазы 0..3, кадры `Reports/batch17/starlight/`); контроль `asda.Skill.HexaFourHits 0`.
//     7) ★ 07.11 чужой урон без дела: чужой тик 6079 и чужая дельта 4017 не пишут «Вы нанесли»; контроли ключами; свой тик пишет.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"
#include "Resources/AsdaGameUserSettings.h"
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
#include "Npc/AsdaMobActor.h"
#include "Skill/AsdaSkillFxSubsystem.h"         // ★ 08.11: шаг 6н — сработавшие фазы показа (PhasesFiredSeen)
#include "Text/AsdaLang.h"                      // ★ 08.11: язык в имени кадра
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"                       // FScreenshotRequest — кадры PNG

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSkillDmgTextTest, Log, All);

namespace AsdaSkillDmgTextTestLocal
{
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kP = 7841;               // сосед
	constexpr int32 kRid = 31101;            // моб-цель навыка
	constexpr int32 kSkill = 705;            // «Многократный выстрел» (лук, маска 0x400)
	constexpr int32 kRid2 = 31102;           // второй моб (площадь)
	const FColor kColMine(0x75, 0xDA, 0xFF, 0xFF);   // −9053441
	const FColor kColOnMe(0xFF, 0x29, 0x13, 0xFF);   // −55021

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
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 WasWeaponSub = 99;
		bool bSampled = false; int32 FloatBase = 0, FloatMax = 0;
		// Вид навыка (шаг 5): момент пакета, первой надписи, число всплытий за шаг и снимок кадра.
		double FedAtS = 0.0, FirstFloatS = -1.0; int32 PrevLive = 0, Spawns = 0; FString ShotName; bool bShotDone = false;
		// ★ R4, доля a, запись 9 (15.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: момент пакета и первой надписи по ИГРОВЫМ часам мира. Окна
		//   «через X с после пакета» сверяются по ним: игра ставит фазы по игровым часам, а настенные включают остановку кадра
		//   приёма пакета (запись журнала на занятый диск — до 1,7 с), которую игровые часы обрезают до 0,4 с.
		double FedAtG = -1.0, FirstFloatG = -1.0;
		// Игровые часы мира, с.
		double GNow() const { const UWorld* W = World.Get(); return W ? W->GetTimeSeconds() : 0.0; }
		int32 LinesBase = 0;
		int32 Base707a = 0, Base707b = 0;   // счётчики строк фаз многоударного 707 на старте шага   // счётчик искомой строки чата на старте шага
		double TA = -1.0, TB = -1.0;        // шаг 6: момент (с от пакета) первой строки удара A / удара B
		// ★ R4, доля a (15.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: те же моменты по ИГРОВЫМ часам мира (`UWorld::GetTimeSeconds`). Разрыв между
		//   ударами сверяется по ним: игра ставит удары по игровым часам, а настенные часы сжимают разрыв, когда в кадре первого
		//   удара идёт синхронная загрузка ассета эффекта (30–60 мс на холодном ассете) — цифра появляется в конце длинного кадра.
		double GA = -1.0, GB = -1.0;
		int32 BaseA = 0, BaseB = 0;         // шаг 6: счётчики строк ударов A / B на старте шага
		double HexaT[4] = { -1.0, -1.0, -1.0, -1.0 };   // шаг 6н: момент (с от пакета) строки удара k навыка 513
		int32 HexaBase[4] = { 0, 0, 0, 0 };            // шаг 6н: счётчики строк ударов на старте шага
		int32 HexaHp[4] = { -1, -1, -1, -1 };          // шаг 6н: HP моба в момент строки удара k
		bool bHexaShot[4] = { false, false, false, false };   // шаг 6н: кадр удара k снят
		int32 HexaPhFrom = 0;                           // шаг 6н: с какой записи `PhasesFiredSeen` начинается шаг
		int32 SavedMode = -1;               // шаг 7: режим показа цифр до прогона (вернёт шаг «итог»)
		int32 DeltaHp = 0, DeltaVal = 0;    // шаг 7: HP снимка и величина чужой дельты
		void SetMode(int32 M) { if (UAsdaGameUserSettings* G = UAsdaGameUserSettings::Get()) { if (SavedMode < 0) { SavedMode = G->DamageTextMode; } G->DamageTextMode = M; } }   // режим показа цифр; профиль на диск не пишется

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		UAsdaFloatingTextSubsystem* FT() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaFloatingTextSubsystem>() : nullptr; }
		UAsdaChatSubsystem* Chat() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaChatSubsystem>() : nullptr; }
		UAsdaTextSubsystem* Txt() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaTextSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AActor* Peer() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->ActorByTarget(EAsdaTargetKind::Character, kP) : nullptr; }
		AAsdaMobActor* Mob() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(kRid) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaSkillDmgTextTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }   // исходное значение вернёт `Finish` (RestoreAll)
		void CalmSkill(int32 Code) { if (UAsdaSkillUseSubsystem* S = SU()) { S->SimulateCastResult5257(0, Code); } }
		void TrackFloat()
		{
			UAsdaFloatingTextSubsystem* F = FT(); if (!F) { return; }
			const int32 Live = F->DebugLiveCount();
			FloatMax = FMath::Max(FloatMax, Live - FloatBase);
			if (FedAtS > 0.0 && Live > PrevLive)
			{
				Spawns += Live - PrevLive;
				if (FirstFloatS < 0.0) { FirstFloatS = FPlatformTime::Seconds() - FedAtS; FirstFloatG = GNow() - FedAtG; }
			}
			PrevLive = Live;
			// Кадр — через 0.15 с после первой надписи (пик масштаба трека 50…80 мс уже прошёл, надпись ещё яркая).
			if (!ShotName.IsEmpty() && !bShotDone && FirstFloatS >= 0.0 && FPlatformTime::Seconds() - FedAtS >= FirstFloatS + 0.15)
			{
				bShotDone = true;
				const FString Path = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), TEXT("../Reports/bug_skill_dmgtext/frames"), ShotName + TEXT(".png"));
				FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/true, /*bAddUniqueSuffix=*/false);
				UE_LOG(LogAsdaSkillDmgTextTest, Log, TEXT("%s кадр %s"), Tag, *Path);
			}
		}
		FString SkName(int32 Code) const
		{
			const UGameInstance* G = GI(); const UAsdaSkillDatabase* D = G ? G->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
			const FAsdaSkillDef* Def = D ? D->Get(Code) : nullptr; return Def ? Def->Name : FString();
		}
		FString Line(int32 Id, const TArray<FString>& Strs, const TArray<int32>& Nums) const { const UAsdaTextSubsystem* T = Txt(); return T ? T->FormatById(Id, Strs, Nums) : FString(); }
		int32 Count(const FString& L) const { const UAsdaChatSubsystem* C = Chat(); return (C && !L.IsEmpty()) ? C->DebugCountSystemLines(L) : 0; }
		bool LastColor(const FString& L, FColor& Out) const { const UAsdaChatSubsystem* C = Chat(); return C && !L.IsEmpty() && C->DebugFindLastSystemLineColor(L, Out); }
		void Mark(const FString& L)
		{
			OutFrom = Outs.Num(); bSampled = false;
			FloatBase = FT() ? FT()->DebugLiveCount() : 0; FloatMax = 0; LinesBase = Count(L);
			FedAtS = 0.0; FirstFloatS = -1.0; PrevLive = FloatBase; Spawns = 0; ShotName.Reset(); bShotDone = false;
			FedAtG = -1.0; FirstFloatG = -1.0;
		}
		bool Sent5256(int32 Code) const
		{
			for (int32 i = OutFrom; i < Outs.Num(); ++i)
			{
				if (Outs[i].Op == 5256 && Outs[i].Payload.Num() >= 26 && AsdaBytes::RdI16(&Outs[i].Payload[24]) == Code) { return true; }
			}
			return false;
		}
	};

	// 5257 (шапка 8 б + 29·N): [0] результат, [1..2] кастер, [3..4] навык, [5] 1, [6..7] уровень; запись — вид@0 (0 = моб, 1 = персонаж),
	//   цель@1, урон@3, флаги@7, стеки buff0/1/2 @8..10, HP после @11, состояние @19 (−1).
	static TArray<uint8> Body5257(int32 Caster, int32 Skill, uint8 Kind, int32 Idx, int32 Dmg, uint8 Flags, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		uint8* R = B.GetData() + 8;
		R[0] = Kind; AsdaBytes::WrI16(R + 1, Idx); AsdaBytes::WrI32(R + 3, Dmg); R[7] = Flags;
		AsdaBytes::WrI32(R + 11, HpAfter); AsdaBytes::WrI16(R + 19, -1);
		return B;
	}

	// Исполнитель шагов: спавн своего персонажа, шаги по таймеру, итог (как у `asda.Skill.SelfTestFaceTarget`).
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
				UE_LOG(LogAsdaSkillDmgTextTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
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

struct FAsdaSkillDmgTextSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaSkillDmgTextSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaSkillDmgTextTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestDamageText");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-ЦИФРЫ-НАВЫКА");
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
	auto Idle = [S]() { const UAsdaSkillUseSubsystem* U = S->SU(); return U && U->GetActiveSkill() < 0 && U->GetSelfCharState() == 0; };
	// Прежние надписи погасли (иначе замер «живых надписей» следующего шага ложно даёт 0: старая гаснет в окне новой).
	auto LiveZero = [S]() { const UAsdaFloatingTextSubsystem* F = S->FT(); return S->El() > 0.3 && (!F || F->DebugLiveCount() == 0); };

	Add(TEXT("подготовка"), 5.f, [S, Net, Body5251]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
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
		S->WasWeaponSub = C->EquippedWeaponSubType; C->EquippedWeaponSubType = 10;   // лук: навык 705 (маска 0x400)
		S->bWasStand = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->Feed(5251, Body5251({ kSkill }));
		Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест цифр навыка"), true);
		S->SetKey(TEXT("asda.Skill.DamageChatLines"), 1);
		S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom));
		if (UAsdaWorldSubsystem* Ws = S->WS()) { Ws->DebugSpawnFakePeer(kP, TEXT("СоседП"), AsdaCoord::TileCenterGame(S->HomeTile + FIntPoint(0, 4))); }
	}, [S]() { return S->El() > 1.0 && S->Mob() && S->Peer(); }, [S]()
	{
		S->Expect(S->Mob() && S->Peer(), TEXT("моб и сосед на местах"));
		S->Expect(!S->SkName(kSkill).IsEmpty(), FString::Printf(TEXT("запись навыка %d есть: «%s»"), kSkill, *S->SkName(kSkill)));
	});

	// ── 1–2. путь игрока по мобу ──
	//   `Skill`/`Weapon`/`bDispatch`: промах берётся навыком 1252 «Зов дрожи» (посох, подтип 5) — у его фазы есть ветка удара
	//   (живой журнал игрока 30.10: «[навык] ПРОМАХ: навык 1252»); у 705 ветки удара нет, и «MISS» там не рисуется ни у
	//   эталона, ни у порта (гейт `bHasHitFx`, AsdaSkillFxSubsystem.cpp `ReleaseHits`). Ответ на него — прямой 5257 (путь сети).
	struct FCase { const TCHAR* Name; int32 Skill; int32 Weapon; bool bDispatch; int32 Dmg; uint8 Flags; int32 Notice; bool bNum; bool bKeyOn; FColor Col; const TCHAR* Word; };
	const FColor White(0xFF, 0xFF, 0xFF, 0xFF);
	const FCase kCases[] = {
		{ TEXT("1а обычный урон (0x01) — 20249 белым"),   kSkill, 10, true,  123, 0x01, 20249, true,  true,  White,    nullptr },
		{ TEXT("1б крит (0x05) — 20247 голубым"),         kSkill, 10, true,  321, 0x05, 20247, true,  true,  kColMine, nullptr },
		{ TEXT("1в ветка &8 (0x09) — 20248 белым"),       kSkill, 10, true,   45, 0x09, 20248, true,  true,  White,    nullptr },
		{ TEXT("1г IMMUNE (0x21) — 20010 белым"),         kSkill, 10, true,    7, 0x21, 20010, false, true,  White,    TEXT("IMMUNE") },
		{ TEXT("1д промах 1252 (урон 0, 0x00) — MISS и 20010"), 1252, 5, false, 0, 0x00, 20010, false, true, White,  TEXT("MISS") },
		{ TEXT("2 контроль asda.Skill.DamageChatLines 0"), kSkill, 10, true,  99, 0x01, 20249, true,  false, White,    nullptr },
	};
	for (const FCase& K : kCases)
	{
		auto LineOf = [S, K]()
		{
			const FString Sk = S->SkName(K.Skill);
			const AAsdaMobActor* M = S->Mob(); const FString Mn = M ? M->MobName : FString();
			return K.bNum ? S->Line(K.Notice, { Mn, Sk }, { K.Dmg }) : S->Line(K.Notice, { Sk }, {});
		};
		Add(K.Name, 6.f, [S, K, LineOf]()
		{
			S->Mark(LineOf());
			S->SetKey(TEXT("asda.Skill.DamageChatLines"), K.bKeyOn ? 1 : 0);
			AAsdaPlayerController* PC = S->PC();
			if (!PC) { return; }
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = K.Weapon; }
			PC->SetTarget(S->Mob());
			if (!K.bDispatch)
			{
				S->bSampled = true;
				S->Feed(5257, Body5257(S->Self, K.Skill, 0, kRid, K.Dmg, K.Flags, 900));
			}
			else if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DispatchByKind(K.Skill, -1); }
		}, [S, K, LineOf]()
		{
			if (!S->bSampled && S->Sent5256(K.Skill))
			{
				S->bSampled = true;
				S->Feed(5257, Body5257(S->Self, K.Skill, 0, kRid, K.Dmg, K.Flags, 900));
				S->StepStartS = FPlatformTime::Seconds();
			}
			if (!S->bSampled) { return false; }
			return (K.bKeyOn && S->Count(LineOf()) > S->LinesBase && S->FloatMax >= 1) || S->El() > 3.5;
		}, [S, K, LineOf]()
		{
			const FString L = LineOf();
			if (K.bDispatch) { S->Expect(S->Sent5256(K.Skill), FString::Printf(TEXT("заявка 5256 навыка %d ушла путём игрока"), K.Skill)); }
			S->Expect(S->FloatMax >= 1, FString::Printf(TEXT("над мобом всплыла надпись (%s): живых надписей до %d"), K.Word ? K.Word : TEXT("цифра"), S->FloatMax));
			const int32 Got = S->Count(L) - S->LinesBase;
			if (K.bKeyOn)
			{
				FColor C; const bool bHas = S->LastColor(L, C);
				S->Expect(Got == 1, FString::Printf(TEXT("строка %d в чате ровно одна (+%d): «%s»"), K.Notice, Got, *L));
				S->Expect(bHas && C == K.Col, FString::Printf(TEXT("цвет строки %s (ждём %s)"), bHas ? *C.ToHex() : TEXT("нет строки"), *K.Col.ToHex()));
			}
			else
			{
				S->Expect(Got == 0, FString::Printf(TEXT("прежний порт: строки %d нет (+%d)"), K.Notice, Got));
			}
			S->CalmSkill(K.Skill);
			S->SetKey(TEXT("asda.Skill.DamageChatLines"), 1);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 10; }
		});
		Add(TEXT("пауза (навык свободен)"), 3.f, nullptr, [Idle, LiveZero]() { return Idle() && LiveZero(); }, nullptr);
	}

	// ── 3. по персонажу (5257 вид 1) ──
	struct FPvp { const TCHAR* Name; bool bMine; int32 Dmg; uint8 Flags; int32 Notice; FColor Col; };
	const FPvp kPvp[] = {
		{ TEXT("3а мой навык по соседу — 20249 голубым"),     true,  210, 0x01, 20249, kColMine },
		{ TEXT("3б навык соседа по мне — 20241 красным"),      false,  77, 0x01, 20241, kColOnMe },
		{ TEXT("3в крит соседа по мне — 20240 красным"),       false, 155, 0x05, 20240, kColOnMe },
	};
	for (const FPvp& K : kPvp)
	{
		auto LineOf = [S, K]()
		{
			if (K.bMine) { return S->Line(K.Notice, { TEXT("СоседП"), S->SkName(kSkill) }, { K.Dmg }); }
			return S->Line(K.Notice, { TEXT("СоседП") }, { K.Dmg });
		};
		Add(K.Name, 6.f, [S, K, LineOf]()
		{
			S->Mark(LineOf());
			if (K.bMine) { S->Feed(5257, Body5257(S->Self, kSkill, 1, kP, K.Dmg, K.Flags, 900)); }
			else         { S->Feed(5257, Body5257(kP, kSkill, 1, S->Self, K.Dmg, K.Flags, 900)); }
		}, [S, LineOf]() { return (S->Count(LineOf()) > S->LinesBase && S->FloatMax >= 1) || S->El() > 3.5; }, [S, K, LineOf]()
		{
			const FString L = LineOf();
			FColor C; const bool bHas = S->LastColor(L, C);
			S->Expect(S->FloatMax >= 1, FString::Printf(TEXT("цифра над %s: живых надписей до %d"), K.bMine ? TEXT("соседом") : TEXT("мной"), S->FloatMax));
			S->Expect(S->Count(L) - S->LinesBase == 1, FString::Printf(TEXT("строка %d «%s»"), K.Notice, *L));
			S->Expect(bHas && C == K.Col, FString::Printf(TEXT("цвет строки %s (ждём %s)"), bHas ? *C.ToHex() : TEXT("нет строки"), *K.Col.ToHex()));
			if (K.bMine) { S->CalmSkill(kSkill); }
		});
		Add(TEXT("пауза"), 3.f, nullptr, [Idle, LiveZero]() { return Idle() && LiveZero(); }, nullptr);
	}

	// ── 5. виды навыков: момент цифры относительно пакета 5257 (эталон — кадр удара COMBO0 / прилёт) и кадры PNG ──
	//   Порог COMBO — клип-мс; темп анимации у тестового персонажа ×1. Оружие — как у живых кастеров (журналы игроков 29–31.10).
	struct FKind { const TCHAR* Name; int32 Skill; int32 Weapon; int32 NRecs; int32 Hp; float MinS; float MaxS; int32 MinSpawns; bool bKeyOn; const TCHAR* Shot; };
	const FKind kKinds[] = {
		{ TEXT("5а без эффекта удара в строке клипа: 908 «Аура тьмы» — цифра на COMBO0 1400 мс"), 908, 5, 1, 900, 1.2f, 2.6f, 1, true,  TEXT("908_aura_no_hitfx") },
		{ TEXT("5б контроль asda.Skill.HitFrameAlways 0: 908 — цифра сразу (прежний порт)"),        908, 5, 1, 900, 0.0f, 0.4f, 1, false, nullptr },
		{ TEXT("5в ударный без эффекта: 512 «Удар» — цифра на COMBO0 833 мс"),                      512, 3, 1, 900, 0.65f, 2.0f, 1, true, TEXT("512_strike") },
		{ TEXT("5г площадь: 912 «Океан огня» по двум мобам — две цифры на COMBO0 1767 мс"),          912, 5, 2, 900, 1.5f, 3.0f, 2, true,  TEXT("912_area_two_mobs") },
		{ TEXT("5д многоударный: 707 «Повторный выстрел» — три цифры по фазам 500/1000/1500 мс"),    707, 11, 1, 900, 0.4f, 2.6f, 2, true, TEXT("707_multihit") },
		{ TEXT("5е дальний (снаряд): 903 «Лезвие воздуха» — цифра по прилёте"),                      903, 5, 1, 900, 0.6f, 3.0f, 1, true,  TEXT("903_projectile") },
		// ★ Ревизия А1: тик вне белого списка (skillFx_UpdateChanneled_931 урона по порогу не показывает) — запись, как прежде, сразу.
		{ TEXT("5з вне белого списка: 931 «Вихрь жизни» — цифра сразу"),                             931, 3, 1, 900, 0.0f, 0.4f, 1, true,  nullptr },
		{ TEXT("5ж цель умирает от удара: 908, HP после 0 — цифра есть"),                             908, 5, 1, 0,   1.2f, 2.6f, 1, true,  TEXT("908_lethal") },
	};
	for (const FKind& K : kKinds)
	{
		// Многоударный 707: урон 250 делится 83/83/84 (`SplitDamageIn3`) — по строке 20249 на каждую фазу, в той же точке, что и цифра.
		//   Счётчик всплытий по приросту живых надписей занижает (первая гаснет в кадре третьей), поэтому фазы доказываются строками.
		auto Line707 = [S](int32 Dmg) { const AAsdaMobActor* M = S->Mob(); return S->Line(20249, { M ? M->MobName : FString(), S->SkName(707) }, { Dmg }); };
		Add(K.Name, 6.f, [S, K, Line707]()
		{
			S->Mark(FString());
			S->Base707a = S->Count(Line707(83)); S->Base707b = S->Count(Line707(84));
			S->SetKey(TEXT("asda.Skill.HitFrameAlways"), K.bKeyOn ? 1 : 0);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = K.Weapon; }
			if (!S->Mob()) { S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom)); }
			if (K.NRecs > 1 && !(S->WS() && S->WS()->FindNetMobByRid(kRid2)))
			{
				S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid2, kMobType, S->HomeTile + FIntPoint(4, 2), 1, kSpdDenom));
			}
			if (AAsdaPlayerController* PC = S->PC()) { PC->SetTarget(S->Mob()); }
		}, [S, K]()
		{
			const bool bReady = S->Mob() && (K.NRecs < 2 || (S->WS() && S->WS()->FindNetMobByRid(kRid2)));
			if (!S->bSampled && bReady && S->El() > 0.3)
			{
				S->bSampled = true;
				S->ShotName = K.Shot ? K.Shot : TEXT("");
				TArray<uint8> B = Body5257(S->Self, K.Skill, 0, kRid, 250, 0x01, K.Hp);
				if (K.NRecs > 1)
				{
					const TArray<uint8> B2 = Body5257(S->Self, K.Skill, 0, kRid2, 260, 0x01, K.Hp);
					B.Append(B2.GetData() + 8, 29);
				}
				S->FedAtS = FPlatformTime::Seconds(); S->FedAtG = S->GNow();
				S->Feed(5257, B);
				S->StepStartS = FPlatformTime::Seconds();
			}
			return S->bSampled && S->El() > K.MaxS + 0.4;
		}, [S, K, Line707]()
		{
			if (K.Skill == 707)
			{
				const int32 A = S->Count(Line707(83)) - S->Base707a, B = S->Count(Line707(84)) - S->Base707b;
				S->Expect(A == 2 && B == 1, FString::Printf(TEXT("три попадания фазами: строк «83» +%d (ждём 2), «84» +%d (ждём 1)"), A, B));
			}
			S->Expect(S->FirstFloatG >= K.MinS && S->FirstFloatG <= K.MaxS,
				FString::Printf(TEXT("первая цифра через %.2f с игровых часов после 5257 (ждём %.2f…%.2f; по настенным часам %.2f с)"),
					S->FirstFloatG, K.MinS, K.MaxS, S->FirstFloatS));
			S->Expect(S->Spawns >= K.MinSpawns, FString::Printf(TEXT("всплытий цифр %d (ждём ≥ %d)"), S->Spawns, K.MinSpawns));
			if (K.Hp == 0) { const AAsdaMobActor* M = S->Mob(); S->Expect(!M || M->IsDead(), TEXT("цель умерла на этом ударе")); }
			S->CalmSkill(K.Skill);
			S->SetKey(TEXT("asda.Skill.HitFrameAlways"), 1);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 10; }
		});
		Add(TEXT("пауза (навык свободен)"), 8.f, [S, K]()
		{
			// После смертельного удара — свежий моб на том же месте: 4011 убирает труп, после его ухода 4013 ставит заново.
			S->bSampled = false;
			if (K.Hp == 0) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kRid); S->Feed(4011, B); }
		}, [S, K, Idle, LiveZero]()
		{
			if (K.Hp != 0) { return Idle() && LiveZero(); }
			const AAsdaMobActor* M = S->Mob();
			if (!S->bSampled && !M)
			{
				S->bSampled = true;
				S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom));
			}
			return S->bSampled && M && !M->IsDead() && Idle() && LiveZero();
		}, nullptr);
	}

	// ── 5и. нулевые доли многоударного: 707 с уроном 2 → доли 0/0/2 (`SplitDamageIn3`), бит 0 = 1 — эталон на нулевых долях
	//   показывает «урон 0»: «MISS» + 20010 кастеру (effects_particles.c:7681-7693 → CharFx_ShowSkillDamageText ветка «урон 0»).
	Add(TEXT("5и многоударный 707, урон 2: доли 0/0/2 — дважды MISS + 20010, затем 20249 «2»"), 6.f, [S]()
	{
		const AAsdaMobActor* M = S->Mob();
		S->Mark(FString());
		S->Base707a = S->Count(S->Line(20010, { S->SkName(707) }, {}));
		S->Base707b = S->Count(S->Line(20249, { M ? M->MobName : FString(), S->SkName(707) }, { 2 }));
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 11; }
		if (!S->Mob()) { S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom)); }
	}, [S]()
	{
		if (!S->bSampled && S->Mob() && S->El() > 0.3)
		{
			S->bSampled = true; S->FedAtS = FPlatformTime::Seconds();
			S->Feed(5257, Body5257(S->Self, 707, 0, kRid, 2, 0x01, 900));
			S->StepStartS = FPlatformTime::Seconds();
		}
		return S->bSampled && S->El() > 2.8;
	}, [S]()
	{
		const AAsdaMobActor* M = S->Mob();
		const int32 Miss = S->Count(S->Line(20010, { S->SkName(707) }, {})) - S->Base707a;
		const int32 Hit = S->Count(S->Line(20249, { M ? M->MobName : FString(), S->SkName(707) }, { 2 })) - S->Base707b;
		S->Expect(Miss == 2 && Hit == 1, FString::Printf(TEXT("строк 20010 +%d (ждём 2), 20249 «2» +%d (ждём 1)"), Miss, Hit));
		S->Expect(S->Spawns >= 2, FString::Printf(TEXT("надписей (MISS/цифра) всплыло %d (ждём ≥ 2)"), S->Spawns));
		S->CalmSkill(707);
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 10; }
	});
	Add(TEXT("пауза (навык свободен)"), 4.f, nullptr, [Idle, LiveZero]() { return Idle() && LiveZero(); }, nullptr);

	// ── 6. ★ 05.11 (баг «Rising Dragon: два удара, одна цифра», `asda.Skill.HitRecordPerPhase`): сервер шлёт ДВЕ записи по одной цели
	//   (живые журналы 01–05.11: 504/508/509/513 — 5270, 903 — 5257). Тело записи — байт в байт с живого 5270 навыка 508 (01.11 12:01:30,
	//   «Imp General»): заменены только номер цели и урон/HP. Эталон: запись k — на фазе k (COMBO k), цифр две, их сумма — урон сервера.
	//   Строки 20249 — свидетель момента (они выходят в той же точке, что и цифра).
	auto Rec29 = [](int32 Idx, int32 Dmg, int32 Hp)
	{
		static const uint8 kLive[29] = { 0x00, 0xF0, 0x00, 0x6F, 0x0F, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xDC, 0x2E, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		TArray<uint8> R; R.Append(kLive, 29);
		AsdaBytes::WrI16(&R[1], Idx); AsdaBytes::WrI32(&R[3], Dmg); AsdaBytes::WrI32(&R[11], Hp);
		return R;
	};
	// ★ 05.11 (закрытие пунктов): Kind — вид цели записи (0 моб, 1 персонаж: ветка PvP — эталон берёт урон игрока `field_E4 + фаза`,
	//   effects_particles.c:25227); NRec — записей (502: сервер шлёт одну, живые журналы 83 из 83); Quiet — до какого момента (с от пакета)
	//   ждать «тишины» после последней записи: у 513/701 третья фаза без записи — ни цифры, ни «MISS»/20010 (дефект оригинала: эталон
	//   читает несброшенную ячейку №2 — `skillClearQuickSlots@0xA86B10` не чистит урон моба 172632+, а флаг №2 чистит лишь ответ без цели).
	struct FMulti { const TCHAR* Name; int32 Skill; int32 Weapon; uint16 Op; float A0; float A1; float B0; float B1; float MinGap; bool bKeyOn; const TCHAR* Shot;
		uint8 Kind; int32 NRec; float Quiet; };
	const FMulti kMulti[] = {
		{ TEXT("6а 508 «Rising Dragon» (5270, 2 записи): цифры на COMBO0 567 и COMBO1 933 мс"),       508, 3, 5270, 0.40f, 0.90f, 0.80f, 1.50f, 0.25f, true,  TEXT("508_two_hits"), 0, 2, 0.f },
		{ TEXT("6б контроль asda.Skill.HitRecordPerPhase 0: 508 — обе цифры разом (прежний порт)"),  508, 3, 5270, 0.40f, 0.90f, 0.40f, 0.90f, -1.f,  false, nullptr, 0, 2, 0.f },
		{ TEXT("6в 504 «Удар наотмашь» (5270): COMBO0 600 и COMBO1 1167 мс"),                        504, 2, 5270, 0.45f, 0.95f, 1.00f, 1.70f, 0.40f, true,  nullptr, 0, 2, 0.f },
		{ TEXT("6г 509 «Двойной удар» (5270): COMBO0 667 и COMBO1 933 мс"),                           509, 1, 5270, 0.50f, 1.00f, 0.80f, 1.50f, 0.18f, true,  nullptr, 0, 2, 0.f },
		// ★ 08.11: прежний шаг «6д 513 — 2 записи, фаза 1533 тишина» снят — разбор 05.11 по 513 был неверен (живой сервер шлёт 4 записи
		//   и у тика 4 фазы); 513 проверяют шаги 6н/6о ниже.
		{ TEXT("6е 903 «Лезвие воздуха» (5257, снаряды фаз 0 и 1): вторая цифра позже первой"),      903, 5, 5257, 0.55f, 2.50f, 1.10f, 3.50f, 0.35f, true,  nullptr, 0, 2, 0.f },
		{ TEXT("6ж 503 (5270): COMBO0 867 и COMBO1 1533 мс"),                                          503, 1, 5270, 0.65f, 1.20f, 1.30f, 2.10f, 0.45f, true,  nullptr, 0, 2, 0.f },
		{ TEXT("6з 506 (5270): COMBO0 567 и COMBO1 2867 мс"),                                          506, 2, 5270, 0.40f, 0.95f, 2.60f, 3.50f, 1.80f, true,  nullptr, 0, 2, 0.f },
		{ TEXT("6и 510 (5270): COMBO0 467 и COMBO1 967 мс"),                                           510, 1, 5270, 0.30f, 0.80f, 0.75f, 1.40f, 0.30f, true,  nullptr, 0, 2, 0.f },
		{ TEXT("6к 701 (5270, снаряды, 3 фазы, 2 записи): фазы 700/833, фаза 1000 — тишина"),        701, 9, 5270, 0.55f, 2.20f, 0.65f, 2.60f, 0.08f, true,  nullptr, 0, 2, 3.2f },
		{ TEXT("6л 502 (5270, 1 запись): одна цифра на COMBO0 533, на COMBO1 1033 — тишина"),       502, 1, 5270, 0.35f, 0.85f, 0.f,   0.f,   0.f,   true,  nullptr, 0, 1, 1.9f },
		{ TEXT("6м PvP: 508 по соседу (вид 1, 2 записи) — по цифре на COMBO0 и COMBO1"),            508, 3, 5270, 0.40f, 0.90f, 0.80f, 1.50f, 0.25f, true,  nullptr, 1, 2, 0.f },
	};
	constexpr int32 kDmgA = 3951, kDmgB = 3627;   // урон ударов — как в живом пакете 508
	for (const FMulti& K : kMulti)
	{
		// Строка-свидетель: по мобу — 20249 {моб, навык}; по соседу — та же 20249 {имя соседа, навык} (шаг 3а).
		auto LineD = [S, K](int32 Dmg)
		{
			if (K.Kind == 1) { return S->Line(20249, { TEXT("СоседП"), S->SkName(K.Skill) }, { Dmg }); }
			const AAsdaMobActor* M = S->Mob(); return S->Line(20249, { M ? M->MobName : FString(), S->SkName(K.Skill) }, { Dmg });
		};
		auto LineMiss = [S, K]() { return S->Line(20010, { S->SkName(K.Skill) }, {}); };
		Add(K.Name, K.Quiet > 0.f ? K.Quiet + 2.f : 7.f, [S, K, LineD, LineMiss]()
		{
			S->Mark(FString());
			S->BaseA = S->Count(LineD(kDmgA)); S->BaseB = S->Count(LineD(kDmgB)); S->TA = -1.0; S->TB = -1.0; S->GA = -1.0; S->GB = -1.0;
			S->LinesBase = S->Count(LineMiss());
			S->bSampled = false;   // предыдущий шаг (5и/пауза) оставляет признак взведённым
			S->SetKey(TEXT("asda.Skill.HitRecordPerPhase"), K.bKeyOn ? 1 : 0);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = K.Weapon; }
			if (!S->Mob()) { S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom)); }
			if (AAsdaPlayerController* PC = S->PC()) { PC->SetTarget(K.Kind == 1 ? S->Peer() : S->Mob()); }
		}, [S, K, LineD, Rec29]()
		{
			if (!S->bSampled && (K.Kind == 1 ? S->Peer() != nullptr : S->Mob() != nullptr) && S->El() > 0.3)
			{
				S->bSampled = true;
				S->ShotName = K.Shot ? K.Shot : TEXT("");
				TArray<uint8> B;
				if (K.Op == 5270)
				{
					B.SetNumZeroed(7);   // 5270: кастер u16, навык u16, уровень u16, [6] = 1 (живой пакет), записи с +7
					AsdaBytes::WrI16(&B[0], S->Self); AsdaBytes::WrI16(&B[2], K.Skill); AsdaBytes::WrI16(&B[4], 5); B[6] = 1;
				}
				else
				{
					B.SetNumZeroed(8);   // 5257: результат, кастер, навык, 1, уровень; записи с +8
					B[0] = 1; AsdaBytes::WrI16(&B[1], S->Self); AsdaBytes::WrI16(&B[3], K.Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 5);
				}
				const int32 Tgt = K.Kind == 1 ? kP : kRid;
				TArray<uint8> RA = Rec29(Tgt, kDmgA, 900); RA[0] = K.Kind; B.Append(RA);
				if (K.NRec > 1) { TArray<uint8> RB = Rec29(Tgt, kDmgB, 800); RB[0] = K.Kind; B.Append(RB); }
				S->FedAtS = FPlatformTime::Seconds(); S->FedAtG = S->GNow();
				S->Feed(K.Op, B);
				S->StepStartS = FPlatformTime::Seconds();
			}
			if (!S->bSampled) { return false; }
			const double T = FPlatformTime::Seconds() - S->FedAtS;
			const UWorld* GameWorld = S->World.Get();
			const double G = GameWorld ? GameWorld->GetTimeSeconds() : 0.0;   // ★ R4: игровые часы — для разрыва между ударами
			if (S->TA < 0.0 && S->Count(LineD(kDmgA)) > S->BaseA) { S->TA = T; S->GA = G; }
			if (S->TB < 0.0 && S->Count(LineD(kDmgB)) > S->BaseB) { S->TB = T; S->GB = G; }
			if (K.Quiet > 0.f) { return T > K.Quiet; }   // ждём до конца «пустой» фазы — проверка тишины
			return (S->TA >= 0.0 && S->TB >= 0.0 && T > S->TB + 0.3) || S->El() > K.B1 + 0.8;
		}, [S, K, LineD, LineMiss]()
		{
			const int32 NA = S->Count(LineD(kDmgA)) - S->BaseA, NB = S->Count(LineD(kDmgB)) - S->BaseB;
			if (K.NRec > 1)
			{
				S->Expect(NA == 1 && NB == 1, FString::Printf(TEXT("по строке на удар: «%d» +%d, «%d» +%d (ждём по 1; сумма %d = урон сервера)"),
					kDmgA, NA, kDmgB, NB, kDmgA + kDmgB));
			}
			else
			{
				S->Expect(NA == 1 && NB == 0, FString::Printf(TEXT("одна запись — одна строка: «%d» +%d (ждём 1), «%d» +%d (ждём 0)"), kDmgA, NA, kDmgB, NB));
			}
			// ★ R4, запись 9: окна ударов — по игровым часам (`GA`, `GB` от момента пакета `FedAtG`); настенное время — справкой.
			const double GAfter = S->GA >= 0.0 ? S->GA - S->FedAtG : -1.0, GBfter = S->GB >= 0.0 ? S->GB - S->FedAtG : -1.0;
			S->Expect(GAfter >= K.A0 && GAfter <= K.A1, FString::Printf(TEXT("удар 1 через %.2f с игровых часов после пакета (ждём %.2f…%.2f; по настенным %.2f с)"),
				GAfter, K.A0, K.A1, S->TA));
			if (K.NRec > 1)
			{
				S->Expect(GBfter >= K.B0 && GBfter <= K.B1, FString::Printf(TEXT("удар 2 через %.2f с игровых часов после пакета (ждём %.2f…%.2f; по настенным %.2f с)"),
					GBfter, K.B0, K.B1, S->TB));
				if (K.MinGap > 0.f)
				{
					// ★ R4, доля a (15.11.2026): разрыв — по игровым часам (разбор у полей `GA`/`GB`); настенный — в строке для справки.
					S->Expect(S->GB - S->GA >= K.MinGap, FString::Printf(TEXT("цифры на РАЗНЫХ ударах: между ними %.2f с игровых часов (ждём ≥ %.2f; по настенным часам %.2f с)"),
						S->GB - S->GA, K.MinGap, S->TB - S->TA));
				}
				else
				{
					S->Expect(FMath::Abs(S->GB - S->GA) < 0.1, FString::Printf(TEXT("прежний порт: цифры разом (между ними %.2f с игровых часов; по настенным %.2f с)"),
						S->GB - S->GA, S->TB - S->TA));
				}
			}
			if (K.Quiet > 0.f)
			{
				const int32 NM = S->Count(LineMiss()) - S->LinesBase;
				S->Expect(NM == 0, FString::Printf(TEXT("фаза без записи — тишина до %.1f с: строк «MISS» 20010 +%d (ждём 0), цифр всплыло %d (ждём ≤ %d)"),
					K.Quiet, NM, S->Spawns, K.NRec));
				S->Expect(S->Spawns <= K.NRec, FString::Printf(TEXT("лишних надписей нет: всплытий %d (ждём ≤ %d)"), S->Spawns, K.NRec));
			}
			S->CalmSkill(K.Skill);
			S->SetKey(TEXT("asda.Skill.HitRecordPerPhase"), 1);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 10; }
		});
		Add(TEXT("пауза (навык свободен)"), 6.f, [S]() { S->bSampled = false; }, [Idle, LiveZero]() { return Idle() && LiveZero(); }, nullptr);
	}

	// ── 6н. ★ 08.11 (пачка 17, баг «Starlight Slasher: 4 удара приходят одновременно», `asda.Skill.HexaFourHits`): сервер на 513 шлёт ОДИН
	//   5270 с ЧЕТЫРЬМЯ записями по одной цели (живые журналы Player5 01–05.11, 31 из 31; урон ниже — живой каст 01.11 00:45:19).
	//   Эталон `CSkillEffect_Update`: запись k — на фазе k, пороги combo time.bin 667/1100/1533/2500 мс. Журнал: момент каждой строки
	//   20249 от пакета, HP моба после каждого удара, сработавшие фазы показа; кадр на каждый удар — `Reports/batch17/starlight/`.
	//   Контроль `asda.Skill.HexaFourHits 0` — прежний порт: три фазы, записи 3 и 4 разом на третьей.
	struct FHexa { const TCHAR* Name; bool bKeyOn; };
	const FHexa kHexa[] = {
		{ TEXT("6н 513 «Starlight Slasher» (5270, 4 записи по одной цели): удары 667/1100/1533/2500 мс по одному"), true },
		{ TEXT("6о контроль asda.Skill.HexaFourHits 0: 513 — три фазы, удары 3 и 4 разом (прежний порт)"), false },
	};
	static const int32 kHexaDmg[4] = { 1567, 1145, 1600, 1091 };
	static const int32 kHexaHp[4] = { 90, 70, 50, 30 };   // HP моба из 4013 = 100; удары не смертельны
	for (const FHexa& K : kHexa)
	{
		auto LineH = [S](int32 Dmg) { const AAsdaMobActor* M = S->Mob(); return S->Line(20249, { M ? M->MobName : FString(), S->SkName(513) }, { Dmg }); };
		Add(K.Name, 12.f, [S, K, LineH]()
		{
			S->Mark(FString());
			for (int32 k = 0; k < 4; ++k) { S->HexaBase[k] = S->Count(LineH(kHexaDmg[k])); S->HexaT[k] = -1.0; S->HexaHp[k] = -1; S->bHexaShot[k] = false; }
			S->bSampled = false;
			S->SetKey(TEXT("asda.Skill.HexaFourHits"), K.bKeyOn ? 1 : 0);
			S->SetKey(TEXT("asda.Skill.HitRecordPerPhase"), 1);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 1; }   // 513 — меч (эталон: equippedWeaponType == 1)
			if (!S->Mob()) { S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom)); }
			if (AAsdaPlayerController* PC = S->PC()) { PC->SetTarget(S->Mob()); }
			if (UAsdaSkillFxSubsystem* Fx = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr) { Fx->bRecordPhases = true; S->HexaPhFrom = Fx->PhasesFiredSeen.Num(); }
		}, [S, K, LineH, Rec29]()
		{
			if (!S->bSampled && S->Mob() != nullptr && S->El() > 0.3)
			{
				S->bSampled = true;
				TArray<uint8> B; B.SetNumZeroed(7);   // 5270: кастер u16, навык u16, уровень u16, [6] = 1 (живой пакет), записи с +7
				AsdaBytes::WrI16(&B[0], S->Self); AsdaBytes::WrI16(&B[2], 513); AsdaBytes::WrI16(&B[4], 5); B[6] = 1;
				for (int32 k = 0; k < 4; ++k) { B.Append(Rec29(kRid, kHexaDmg[k], kHexaHp[k])); }
				S->FedAtS = FPlatformTime::Seconds(); S->FedAtG = S->GNow();
				S->Feed(5270, B);
				S->StepStartS = FPlatformTime::Seconds();
			}
			if (!S->bSampled) { return false; }
			const double T = S->GNow() - S->FedAtG;   // ★ R4, запись 9: моменты ударов 513 — по игровым часам от пакета
			for (int32 k = 0; k < 4; ++k)
			{
				if (S->HexaT[k] < 0.0 && S->Count(LineH(kHexaDmg[k])) > S->HexaBase[k])
				{
					S->HexaT[k] = T;
					const AAsdaMobActor* M = S->Mob(); S->HexaHp[k] = M ? M->CurHp : -1;
					UE_LOG(LogAsdaSkillDmgTextTest, Log, TEXT("%s [журнал ударов 513, ключ %d] удар %d: урон %d через %.3f с после 5270, HP моба %d (запись: %d)"),
						S->Tag, K.bKeyOn ? 1 : 0, k + 1, kHexaDmg[k], T, S->HexaHp[k], kHexaHp[k]);
				}
				// Кадр каждого удара через 0.15 с после его строки (надпись ещё яркая).
				if (K.bKeyOn && S->HexaT[k] >= 0.0 && !S->bHexaShot[k] && T >= S->HexaT[k] + 0.15)
				{
					S->bHexaShot[k] = true;
					const FString Path = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), TEXT("../Reports/batch17/starlight"),
						FString::Printf(TEXT("513_%s_hit%d.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), k + 1));
					FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/true, /*bAddUniqueSuffix=*/false);
					UE_LOG(LogAsdaSkillDmgTextTest, Log, TEXT("%s кадр %s"), S->Tag, *Path);
				}
			}
			return T > 3.4;   // клип 3.0 с; четвёртый удар — 2.5 с клипа
		}, [S, K]()
		{
			for (int32 k = 0; k < 4; ++k)
			{
				S->Expect(S->HexaT[k] >= 0.0, FString::Printf(TEXT("удар %d (урон %d): строка 20249 есть, через %.2f с"), k + 1, kHexaDmg[k], S->HexaT[k]));
			}
			if (K.bKeyOn)
			{
				// Окна от пакета: порог фазы + задержка старта показа (0.0…0.35 с, как у шагов 6а–6л).
				const double Lo[4] = { 0.55, 0.98, 1.42, 2.38 }, Hi[4] = { 1.05, 1.50, 1.95, 2.95 };
				for (int32 k = 0; k < 4; ++k)
				{
					S->Expect(S->HexaT[k] >= Lo[k] && S->HexaT[k] <= Hi[k], FString::Printf(TEXT("удар %d через %.2f с (ждём %.2f…%.2f: COMBO%d %s мс)"),
						k + 1, S->HexaT[k], Lo[k], Hi[k], k, k == 0 ? TEXT("667") : k == 1 ? TEXT("1100") : k == 2 ? TEXT("1533") : TEXT("2500")));
				}
				for (int32 k = 1; k < 4; ++k)
				{
					const double Gap = S->HexaT[k] - S->HexaT[k - 1];
					const double Want = k == 3 ? 0.80 : 0.30;
					S->Expect(Gap >= Want, FString::Printf(TEXT("удары %d и %d поочерёдно: между ними %.2f с (ждём ≥ %.2f)"), k, k + 1, Gap, Want));
				}
				// HP моба после каждого удара — только в журнал: местный HP тестового моба из 4013 стенд не ведёт (у шагов 5/6 тоже «HP 0 (сервер: N)»);
				//   «HP после» записи k выпускается тем же `ReleaseHits`, что и цифра удара k, — момент строки и есть момент HP.
				int32 Ph[4] = { 0, 0, 0, 0 };
				if (const UAsdaSkillFxSubsystem* Fx = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr)
				{
					for (int32 i = S->HexaPhFrom; i < Fx->PhasesFiredSeen.Num(); ++i)
					{
						const UAsdaSkillFxSubsystem::FAsdaPhaseFiredSeen& E = Fx->PhasesFiredSeen[i];
						if (E.SkillId == 513 && E.Phase >= 0 && E.Phase < 4) { ++Ph[E.Phase]; }
					}
				}
				S->Expect(Ph[0] == 1 && Ph[1] == 1 && Ph[2] == 1 && Ph[3] == 1,
					FString::Printf(TEXT("фазы показа сработали 0:%d 1:%d 2:%d 3:%d (ждём по одной — эталон field_3A0 0..3)"), Ph[0], Ph[1], Ph[2], Ph[3]));
				S->Expect(S->Spawns >= 4, FString::Printf(TEXT("цифр над мобом всплыло %d (ждём ≥ 4)"), S->Spawns));
			}
			else
			{
				S->Expect(S->HexaT[3] >= 0.0 && S->HexaT[2] >= 0.0 && FMath::Abs(S->HexaT[3] - S->HexaT[2]) < 0.1 && S->HexaT[3] < 2.1,
					FString::Printf(TEXT("прежний порт: удары 3 и 4 разом на третьей фазе (%.2f и %.2f с)"), S->HexaT[2], S->HexaT[3]));
			}
			if (UAsdaSkillFxSubsystem* Fx = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr) { Fx->bRecordPhases = false; }
			S->CalmSkill(513);
			S->SetKey(TEXT("asda.Skill.HexaFourHits"), 1);
			if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 10; }
		});
		Add(TEXT("пауза (навык свободен)"), 6.f, [S]() { S->bSampled = false; }, [Idle, LiveZero]() { return Idle() && LiveZero(); }, nullptr);
	}

	// ── 3г/3д. промах по персонажу (ревизия А3): бит 0 = 0 ──
	Add(TEXT("3г мой промах по соседу (1252, 0x00) — MISS и 20010 белым"), 6.f, [S]()
	{
		S->Mark(S->Line(20010, { S->SkName(1252) }, {}));
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 5; }
		S->bSampled = true; S->FedAtS = FPlatformTime::Seconds();
		S->Feed(5257, Body5257(S->Self, 1252, 1, kP, 0, 0x00, 900));
	}, [S]() { return S->El() > 3.0; }, [S]()
	{
		const FString L = S->Line(20010, { S->SkName(1252) }, {});
		FColor C; const bool bHas = S->LastColor(L, C);
		S->Expect(S->Spawns >= 1, FString::Printf(TEXT("«MISS» над соседом: всплытий %d"), S->Spawns));
		S->Expect(S->Count(L) - S->LinesBase == 1 && bHas && C == FColor(0xFF, 0xFF, 0xFF, 0xFF),
			FString::Printf(TEXT("строка 20010 +%d, цвет %s (ждём одну белую)"), S->Count(L) - S->LinesBase, bHas ? *C.ToHex() : TEXT("нет")));
		S->CalmSkill(1252);
		if (AAsdaPlayerCharacter* Ch = S->Ch()) { Ch->EquippedWeaponSubType = 10; }
	});
	Add(TEXT("пауза (навык свободен)"), 4.f, nullptr, [Idle, LiveZero]() { return Idle() && LiveZero(); }, nullptr);
	Add(TEXT("3д промах соседа по мне (0x00) — строки 20010 нет (у эталона жертве при промахе тика строки нет)"), 4.f, [S]()
	{
		S->Mark(S->Line(20010, { S->SkName(1252) }, {}));
		S->Feed(5257, Body5257(kP, 1252, 1, S->Self, 0, 0x00, 900));
	}, [S]() { return S->El() > 3.0; }, [S]()
	{
		S->Expect(S->Count(S->Line(20010, { S->SkName(1252) }, {})) - S->LinesBase == 0, TEXT("строки 20010 жертве нет"));
	});

	// ── 4. наблюдатель: навык соседа по мобу — строк нет ──
	Add(TEXT("4 наблюдатель — строк нет"), 5.f, [S]()
	{
		if (!S->Mob()) { S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom)); }
		const AAsdaMobActor* M = S->Mob(); const FString Mn = M ? M->MobName : FString();
		S->Mark(S->Line(20249, { Mn, S->SkName(kSkill) }, { 66 }));
		S->Feed(5257, Body5257(kP, kSkill, 0, kRid, 66, 0x01, 900));
	}, [S]() { return S->El() > 3.5; }, [S]()
	{
		const AAsdaMobActor* M = S->Mob(); const FString Mn = M ? M->MobName : FString();
		const FString L = S->Line(20249, { Mn, S->SkName(kSkill) }, { 66 });
		S->Expect(S->Count(L) - S->LinesBase == 0, TEXT("строки 20249 у наблюдателя нет (эталон: ветка наблюдателя чат не трогает)"));
	});

	// ── 7. ★ 07.11 (баг «чужие удары считаются моими», `asda.Dot.OwnChatLineOnly`, `asda.Combat.DeltaTargetNeedsSwing`): игрок стоит без дела,
	//   режим показа 1 (умолчание порта). 6079 — раскладка `FAsdaDotSlotPkt`; 4017 flags=3 — живой моб, HP@92. Нотисы 20012 и 20013 — один текст.
	auto Body6079 = [](int32 Caster, int32 Target, int32 Tick)
	{
		TArray<uint8> B; B.SetNumZeroed(21);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); B[3] = 0; AsdaBytes::WrI16(&B[4], Target);
		AsdaBytes::WrI16(&B[6], kSkill); AsdaBytes::WrI16(&B[8], 70); B[10] = 5;
		AsdaBytes::WrI32(&B[11], 3000); AsdaBytes::WrI16(&B[15], 1000); AsdaBytes::WrI32(&B[17], Tick);
		return B;
	};
	auto Body4017Hp = [](int32 Hp, const FIntPoint& Tile)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], kMobType); AsdaBytes::WrI16(&B[2], kRid); AsdaBytes::WrI32(&B[4], 3);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		AsdaBytes::WrI32(&B[92], Hp);
		AsdaBytes::WrI16(&B[96], (int16)Tile.X); AsdaBytes::WrI16(&B[98], (int16)Tile.Y);   // клетка моба: нули увели бы его с карты (AoI-деспаун)
		return B;
	};
	constexpr int32 kFatHp = 5000;   // к шагу 7 волк истрачен прошлыми шагами, а три тика 61 больше его 100 HP
	// Дельта HP выбранной цели без своего замаха: флаг боя взведён, цель выбрана, персонаж стоит. Ключ 0 — прежний порт, строка есть.
	struct FDelta { const TCHAR* Name; int32 Key; int32 Want; };
	const FDelta kDelta[] = {
		{ TEXT("7а чужая дельта HP выбранной цели, своего замаха нет — строки 20012 нет"), 1, 0 },
		{ TEXT("7б контроль asda.Combat.DeltaTargetNeedsSwing 0 — строка 20012 есть (воспроизведение)"), 0, 1 },
	};
	for (const FDelta& K : kDelta)
	{
		Add(TEXT("пауза (лента попаданий остыла)"), 5.f, [S]() { S->bSampled = false; }, [S]() { return S->El() > 3.0; }, nullptr);
		Add(K.Name, 5.f, [S, K]()
		{
			S->Mark(FString());
			S->SetMode(1);
			S->SetKey(TEXT("asda.Combat.DeltaTargetNeedsSwing"), K.Key);
			S->DeltaHp = 0; S->DeltaVal = 0; S->bSampled = false;
			if (!S->Mob()) { S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom)); }
		}, [S, Body4017Hp]()
		{
			AAsdaMobActor* M = S->Mob();
			if (!M) { return false; }
			const FIntPoint Tile = S->HomeTile + FIntPoint(4, 0);
			if (S->DeltaHp == 0)
			{
				M->MaxHp = kFatHp; M->CurHp = kFatHp; S->DeltaHp = kFatHp;
				if (AAsdaPlayerController* PC = S->PC()) { PC->SetTarget(M); PC->bCombatStarted = true; }
				S->FedAtS = FPlatformTime::Seconds();
				S->Feed(4017, Body4017Hp(S->DeltaHp, Tile));   // снимок: дельта 0
				return false;
			}
			const double Since = FPlatformTime::Seconds() - S->FedAtS;
			if (!S->bSampled && Since > 0.4)
			{
				S->bSampled = true;
				S->DeltaVal = 17;
				S->LinesBase = S->Count(S->Line(20012, { M->MobName }, { S->DeltaVal }));
				S->Feed(4017, Body4017Hp(S->DeltaHp - S->DeltaVal, Tile));   // чужой урон: HP упало, ленты нет
			}
			return S->bSampled && Since > 2.0;
		}, [S, K]()
		{
			const AAsdaMobActor* M = S->Mob();
			const int32 N = S->Count(S->Line(20012, { M ? M->MobName : FString() }, { S->DeltaVal })) - S->LinesBase;
			S->Expect(N == K.Want, FString::Printf(TEXT("строк 20012 «%d» +%d (ждём %d)"), S->DeltaVal, N, K.Want));
			S->Expect(M && M->CurHp == S->DeltaHp - S->DeltaVal, FString::Printf(TEXT("полоса HP по серверу: %d (ждём %d)"), M ? M->CurHp : -1, S->DeltaHp - S->DeltaVal));
			if (AAsdaPlayerController* PC = S->PC()) { PC->bCombatStarted = false; PC->SetTarget(nullptr); }
			S->SetKey(TEXT("asda.Combat.DeltaTargetNeedsSwing"), 1);
		});
	}
	// Тики: у чужого кастера строки нет, цифра по режиму показа есть; у своего — и то и другое. Значение как на снимке владельца.
	struct FDotT { const TCHAR* Name; bool bMine; int32 Key; int32 Tick; int32 WantLines; };
	const FDotT kDots[] = {
		{ TEXT("7в чужой урон по времени (6079 кастер сосед, тик 61) — строк 20013 нет, цифры есть"), false, 1, 61, 0 },
		{ TEXT("7г контроль asda.Dot.OwnChatLineOnly 0 — три строки (воспроизведение снимка владельца)"), false, 0, 62, 3 },
		{ TEXT("7д свой урон по времени (6079 кастер я) — три строки 20013"), true, 1, 63, 3 },
	};
	for (const FDotT& K : kDots)
	{
		Add(K.Name, 7.f, [S, K, Body6079]()
		{
			AAsdaMobActor* M = S->Mob();
			if (M) { M->MaxHp = kFatHp; M->CurHp = kFatHp; }
			S->Mark(S->Line(20013, { M ? M->MobName : FString() }, { K.Tick }));
			S->SetMode(1);
			S->SetKey(TEXT("asda.Dot.OwnChatLineOnly"), K.Key);
			S->FedAtS = FPlatformTime::Seconds();
			S->Feed(6079, Body6079(K.bMine ? S->Self : kP, kRid, K.Tick));
		}, [S]() { return S->El() > 3.7; }, [S, K]()
		{
			const AAsdaMobActor* M = S->Mob();
			const int32 N = S->Count(S->Line(20013, { M ? M->MobName : FString() }, { K.Tick })) - S->LinesBase;
			S->Expect(N == K.WantLines, FString::Printf(TEXT("строк 20013 «%d» +%d (ждём %d)"), K.Tick, N, K.WantLines));
			S->Expect(S->Spawns >= 1, FString::Printf(TEXT("цифры тиков над мобом: всплытий %d (ждём ≥ 1: счётчик по живым надписям, тик каждую секунду совпадает с угасанием прошлой)"), S->Spawns));
			S->SetKey(TEXT("asda.Dot.OwnChatLineOnly"), 1);
		});
		Add(TEXT("пауза"), 3.f, nullptr, [LiveZero]() { return LiveZero(); }, nullptr);
	}

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		if (S->SavedMode >= 0) { S->SetMode(S->SavedMode); }
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kRid); S->Feed(4011, B); }
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kRid2); S->Feed(4011, B); }
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kP); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); P->bAutoAttack = false; P->ExitCombatStance(); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->WasWeaponSub; }
		Net->bStandCaptureSkillUse = S->bWasStand;
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaSkillDmgTextTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillDamageText(
	TEXT("asda.Skill.SelfTestDamageText"),
	TEXT("Самотест цифр и строк чата урона навыка (31.10): путь игрока SetTarget + DispatchByKind, ответ 5257 по мобу — цифра и строка "
	     "20249/20247/20248/20010 (MISS/IMMUNE); по соседу 20249, по мне 20241/20240; наблюдатель — без строк; контроль "
	     "asda.Skill.DamageChatLines 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestDamageText"));
		FAsdaSkillDmgTextSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
