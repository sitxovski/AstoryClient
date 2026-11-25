// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СОСТОЯНИЯ ИСТЕКАЮТ» `asda.Buff.SelfTestExpiry` (04.11.2026, баг владельца: оглушение на StampBerserker — эффект и значок
//   под полосой HP остались навсегда). [ПОРТ] Только офлайн (`-AsdaNoNet`): входящие — штатный приём `DebugFeedPacket`, свой персонаж —
//   `Asda.World.FakeSelfSpawn`, сосед — `DebugSpawnFakePeer`, мобы — запись 4013. Разбор — `Reports/status_effect_expiry/REPORT.md`.
//   Корень: сервер снимает состояние моба ТОЛЬКО пакетом 4017 и шлёт его лишь при ИЗМЕНЕНИИ (NpcServer `sub_434E10` → mob+30120 →
//   8003 → GameServer `sub_425D40` → 4017), а свой удар навыком кладёт состояние на КАДРЕ удара (`Skill_ApplyEffectResult`) — позже
//   ответа. Снятие, пришедшее между ответом и кадром, «воскрешалось» последней фазой и больше не снималось.
//   Сценарии (тела — по живому журналу sitxovski 02.11 23:17, навык 707 «Repeated Shot», состояние 3, моб rid 248):
//     1) гонка: 5270 (707, бит 0x02) + 4017{3}; после первой фазы — 4017{} (сервер снял); остальные фазы НЕ возвращают состояние;
//     2) контроль `asda.Buff.MobStaleHitGate 0` — состояние возвращается и висит (дефект воспроизведён);
//     3) новый каст ПОСЛЕ снятия — состояние ложится (гейт не глушит свежий каст), следующее 4017{} снимает;
//     4) 4102 (чужая ловушка/навык) → состояние; 4017{} снимает (канал снятия как у эталона);
//     5) смерть моба (4017 flags=0) при висящем состоянии — список и эффекты гаснут сразу;
//     6) смена карты при висящем состоянии — список мобов и серверные списки сброшены;
//     7) СВОЁ состояние (5272, остаток 2 с) истекает по таймеру (`charBuffUpdateExpired`);
//     8) состояние СОСЕДА (5272) снимает 5268 сервера;
//     9–11) ★ подсказки иконок состояний (баг владельца 04.11 «у цели не видно описаний»): цель-моб, цель-игрок (сосед) — наведение
//        на иконку под полосой HP цели → общая плашка владельца TargetState с именем/описанием stateinfotip; уход — гаснет;
//        регресс: своя иконка мини-статуса по-прежнему даёт подсказку (владелец MiniState).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaSessionReset.h"
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"     // общая плашка подсказки: показана ли и чья
#include "UI/AsdaMiniStateSubsystem.h"       // свои иконки состояний (регресс подсказки)
#include "UI/AsdaTargetFrameSubsystem.h"     // иконки состояний цели 6703…6730
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Framework/Application/SlateApplication.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStatusExpiryTest, Log, All);

namespace AsdaStatusExpiryLocal
{
	const TCHAR* const kCmd = TEXT("asda.Buff.SelfTestExpiry");   // имя команды
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kSkill = 707;            // «Repeated Shot»: три фазы, buff0 = состояние 3 (живой журнал 02.11)
	constexpr int32 kState = 3;              // оглушение (набор обездвиживания 0,1,3,4,8,14)
	constexpr int32 kRidA = 30901, kRidB = 30902, kRidC = 30903, kRidD = 30904, kRidE = 30905, kRidF = 30906;   // мобы сценариев
	constexpr int32 kPeer = 7831;            // сосед
	constexpr int32 kTrapSkill = 185;        // «Ледяная ловушка» (4102 → состояние 0)
	constexpr int32 kRidT = 30907;           // моб сценария подсказок

	struct FStep { FString Name; float TimeoutSec = 10.f; TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check; };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;
		FTimerHandle Tick; bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false;
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;
		TArray<int32> Mobs;
		bool bRemovedFed = false;      // сценарий гонки: снятие сервером подано
		double RemovedAtS = 0.0;       // когда подано снятие
		int32 ReappearCount = 0;       // сколько раз состояние вернулось после снятия
		bool bHover = false; double HoverAtS = 0.0;   // подсказки: курсор поставлен на иконку и когда

		UAsdaTargetFrameSubsystem* TF() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaTargetFrameSubsystem>() : nullptr; }
		UAsdaMiniStateSubsystem* Mini() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr; }
		// Общая плашка показана и принадлежит Owner.
		bool TipShownBy(EAsdaTipOwner Owner) const
		{
			UGameInstance* G = GI(); UAsdaItemTooltipSubsystem* T = G ? G->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
			UAsdaTipPanel* Pn = T ? T->GetMainPanel() : nullptr;
			return Pn && Pn->IsShown() && Pn->GetOwner() == Owner;
		}
		// Поставить НАСТОЯЩИЙ курсор Slate в центр виджета (путь игрока: опрос геометрии под курсором).
		bool HoverOver(const UWidget* Wd)
		{
			FVector2D Mn, Mx;
			if (!Wd || !AsdaUi::IsShown(Wd) || !AsdaUi::WidgetAbsRect(Wd, Mn, Mx) || (Mx - Mn).X < 2.0) { return false; }
			FSlateApplication::Get().SetCursorPos((Mn + Mx) * 0.5f);
			bHover = true; HoverAtS = FPlatformTime::Seconds();
			return true;
		}
		void CursorAway() { FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 2.0)); }
		// Строка подсказки, которую показывает плашка (тот же источник — stateinfotip на языке сессии).
		FString StateTipText(int32 Id) const
		{
			FString Ti, At, Bo; int32 Ce = 0;
			const UAsdaEffectsSubsystem* F = Fx();
			return (F && F->GetStateInfo(Id, Ti, At, Ce, Bo)) ? FString::Printf(TEXT("«%s» / «%s»"), *Ti, *Bo.Left(80)) : FString();
		}

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* Fx() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob(int32 Rid) const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(Rid) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaStatusExpiryTest, Log, TEXT("САМОТЕСТ-EXPIRY %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		// Пакет с полем заголовка кадра (5306 берёт userIdx из HdrA, тело пустое).
		void FeedHdr(int32 Op, const TArray<uint8>& B, int32 HdrA) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B, (uint16)(int16)HdrA); } }
		bool CharHasType(int32 Uidx, int32 Type, int32 Id) const
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return false; }
			for (const FAsdaEffectEntry& E : F->GetList(Uidx)) { if (E.Type == Type && E.Id == Id) { return true; } }
			return false;
		}
		int32 SoulSlot = -1;   // слот соулмейт-состояния с записью soulstateinfotip (ищется в шаге 12)
		// Видимый текст общей плашки подсказки (заголовок + тело).
		FString TipText() const
		{
			UGameInstance* G = GI(); UAsdaItemTooltipSubsystem* T = G ? G->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
			UAsdaTipPanel* Pn = T ? T->GetMainPanel() : nullptr;
			UUserWidget* Wd = Pn ? Pn->GetWidget() : nullptr;
			FString Out;
			if (!Wd || !Wd->WidgetTree) { return Out; }
			TArray<UWidget*> All; Wd->WidgetTree->GetAllWidgets(All);
			for (UWidget* X : All)
			{
				if (!X || !AsdaUi::IsShown(X)) { continue; }
				if (const UTextBlock* Tb = Cast<UTextBlock>(X)) { Out += Tb->GetText().ToString() + TEXT(" | "); }
				else if (const URichTextBlock* Rb = Cast<URichTextBlock>(X)) { Out += Rb->GetText().ToString() + TEXT(" | "); }
			}
			return Out;
		}
		bool MobHas(int32 Rid, int32 State) const
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return false; }
			for (const FAsdaEffectEntry& E : F->GetMobList(Rid)) { if (E.Type == 0 && E.Id == State) { return true; } }
			return false;
		}
		// Эффект-акторы состояния ещё живы (визуал «свечения»).
		bool MobFxAlive(int32 Rid, int32 State) const
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return false; }
			for (const FAsdaEffectEntry& E : F->GetMobList(Rid)) { if (E.Type == 0 && E.Id == State && (E.Fx0.IsValid() || E.Fx1.IsValid())) { return true; } }
			return false;
		}
		bool CharHas(int32 Uidx, int32 State) const
		{
			const UAsdaEffectsSubsystem* F = Fx(); if (!F) { return false; }
			for (const FAsdaEffectEntry& E : F->GetList(Uidx)) { if (E.Type == 0 && E.Id == State) { return true; } }
			return false;
		}
		void SpawnMob(int32 Rid, const FIntPoint& Tile) { Feed(4013, AsdaTestPackets::MobSpawn4013(Rid, kMobType, Tile, 1, kSpdDenom)); Mobs.AddUnique(Rid); }
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }   // исходное значение вернёт `Finish`
		void Target(int32 Rid) { if (AAsdaPlayerController* P = PC()) { P->SetTarget(Mob(Rid)); } }
	};

	// 4017 (100 б): тип@0, rid@2, флаги@4, 28 состояний@8, глубины@64, HP@92, клетка@96/98. flags 3 = живой в бою, 0 = смерть.
	static TArray<uint8> Body4017(int32 Rid, uint32 Flags, const TArray<int32>& States, int32 Hp)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], kMobType); AsdaBytes::WrI16(&B[2], Rid); AsdaBytes::WrI32(&B[4], (int32)Flags);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], States.IsValidIndex(i) ? States[i] : -1); B[64 + i] = States.IsValidIndex(i) ? 1 : 0; }
		AsdaBytes::WrI32(&B[92], Hp);
		AsdaBytes::WrI16(&B[96], -1); AsdaBytes::WrI16(&B[98], -1);
		return B;
	}
	// 5270 (шапка 7 б + 29): кастер@0, навык@2, уровень@4, записей@5; запись: вид@0, цель@1, урон@3, флаги@7, стеки@8..10, HP@11.
	static TArray<uint8> Body5270(int32 Caster, int32 Skill, int32 Rid, int32 Dmg, uint8 Flags, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(7 + 29);
		AsdaBytes::WrI16(&B[0], Caster); AsdaBytes::WrI16(&B[2], Skill); B[4] = 5; AsdaBytes::WrI16(&B[5], 1);
		uint8* R = B.GetData() + 7;
		R[0] = 0; AsdaBytes::WrI16(R + 1, Rid); AsdaBytes::WrI32(R + 3, Dmg); R[7] = Flags; R[8] = 1; AsdaBytes::WrI32(R + 11, HpAfter);
		AsdaBytes::WrI16(R + 19, -1); AsdaBytes::WrI16(R + 27, -1);
		return B;
	}
	// 4102 (17 б): вид@0, цель@1, навык@3, урон@5, HP@9, наложить@13, кастер@14, стек@16.
	static TArray<uint8> Body4102(int32 Rid, int32 Skill, int32 Caster)
	{
		TArray<uint8> B; B.SetNumZeroed(17);
		B[0] = 0; AsdaBytes::WrI16(&B[1], Rid); AsdaBytes::WrI16(&B[3], Skill); AsdaBytes::WrI32(&B[9], 100);
		B[13] = 1; AsdaBytes::WrI16(&B[14], Caster);
		return B;
	}
	// 5272: userIdx@0, состояние@2, raw: флаг@6, стек@9, вид@10, остаток@11 (с), байт9@13.
	static TArray<uint8> Body5272(int32 Uidx, int32 State, int32 RemainSec)
	{
		TArray<uint8> B; B.SetNumZeroed(14);
		AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI16(&B[2], State); B[9] = 1; B[10] = 0; AsdaBytes::WrI16(&B[11], RemainSec);
		return B;
	}
	// 5268: userIdx@0, состояние@2.
	static TArray<uint8> Body5268(int32 Uidx, int32 State)
	{
		TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI16(&B[2], State); return B;
	}
	// 5251 — изученные навыки (34 б на запись).
	using AsdaTestPackets::Body5251;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.
}

struct FAsdaStatusExpirySelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaStatusExpirySelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaStatusExpiryLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256, 8226 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	auto T = [S](int32 Dx, int32 Dy) { return S->HomeTile + FIntPoint(Dx, Dy); };

	// ── 0. подготовка ──
	Add(TEXT("подготовка"), 5.f, [S, Net, T]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS() || !S->Fx()) { S->Expect(false, TEXT("нет своего персонажа/мира/эффектов после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->Feed(5251, Body5251({ kSkill }));
		S->SetKey(TEXT("asda.Skill.OwnHitStates"), 1); S->SetKey(TEXT("asda.Skill.MobStateChain"), 1);
		S->SetKey(TEXT("asda.Buff.MobStaleHitGate"), 1);
		S->SpawnMob(kRidA, T(2, 0)); S->SpawnMob(kRidB, T(-2, 0)); S->SpawnMob(kRidC, T(0, 3));
		S->SpawnMob(kRidD, T(0, -3)); S->SpawnMob(kRidE, T(3, 3)); S->SpawnMob(kRidF, T(-3, -3));
		S->SpawnMob(kRidT, T(-3, 3));
		if (UAsdaWorldSubsystem* Ws = S->WS()) { Ws->DebugSpawnFakePeer(kPeer, TEXT("СоседЭ"), AsdaCoord::TileCenterGame(T(1, 1))); }
	}, [S]() { return S->El() > 1.0 && S->Mob(kRidA) && S->Mob(kRidF); }, [S]()
	{
		S->Expect(S->Mob(kRidA) && S->Mob(kRidF), TEXT("мобы заспавнены 4013"));
	});

	// Общий сценарий гонки на мобе Rid: 5270 + 4017{3}; как только состояние легло — 4017{} (сервер снял); дальше считаем возвраты.
	auto RaceStart = [S](int32 Rid)
	{
		S->bRemovedFed = false; S->RemovedAtS = 0.0; S->ReappearCount = 0;
		S->Target(Rid);
		S->Feed(5270, Body5270(S->Self, kSkill, Rid, 1857, 0x03, 900));
		S->Feed(4017, Body4017(Rid, 3, { kState }, 900));   // сервер в том же тике шлёт список с состоянием
	};
	auto RacePoll = [S](int32 Rid)
	{
		const bool bHas = S->MobHas(Rid, kState);
		if (!S->bRemovedFed && bHas)
		{
			S->Feed(4017, Body4017(Rid, 3, {}, 900));   // сервер снял состояние (длительность вышла) — ДО последних фаз удара
			S->bRemovedFed = true; S->RemovedAtS = FPlatformTime::Seconds();
		}
		else if (S->bRemovedFed && bHas) { ++S->ReappearCount; }
		return S->bRemovedFed && FPlatformTime::Seconds() - S->RemovedAtS > 3.0;   // все фазы показа заведомо прошли
	};

	// ── 1. гонка (исправление) ──
	Add(TEXT("1 гонка: 4017 снял до последней фазы"), 9.f, [S, RaceStart]() { RaceStart(kRidA); }, [RacePoll]() { return RacePoll(kRidA); }, [S]()
	{
		S->Expect(S->bRemovedFed, TEXT("первая фаза удара наложила состояние 3, сервер его снял (4017 без него)"));
		S->Expect(!S->MobHas(kRidA, kState) && !S->MobFxAlive(kRidA, kState),
			FString::Printf(TEXT("через 3 с после снятия: состояния 3 на мобе нет, эффект погашен (возвратов в кадрах %d)"), S->ReappearCount));
		S->Expect(S->ReappearCount == 0, TEXT("поздние фазы удара не вернули снятое сервером состояние"));
	});

	// ── 2. контроль: гейт выключен — дефект воспроизводится ──
	Add(TEXT("2 контроль asda.Buff.MobStaleHitGate 0"), 9.f, [S, RaceStart]()
	{
		S->SetKey(TEXT("asda.Buff.MobStaleHitGate"), 0);
		RaceStart(kRidB);
	}, [RacePoll]() { return RacePoll(kRidB); }, [S]()
	{
		S->Expect(S->MobHas(kRidB, kState), TEXT("прежнее поведение: поздняя фаза вернула состояние, и оно висит — дефект владельца воспроизведён"));
		S->SetKey(TEXT("asda.Buff.MobStaleHitGate"), 1);
		S->Feed(4017, Body4017(kRidB, 3, { kState }, 800)); S->Feed(4017, Body4017(kRidB, 3, {}, 800));   // уборка
		S->bRemovedFed = false;   // флаг сценария гонки — следующим шагам (3б ждёт его сброшенным)
	});

	// ── 3. новый каст после снятия ──
	Add(TEXT("3 новый каст после снятия"), 8.f, [S]()
	{
		S->Feed(4017, Body4017(kRidC, 3, { kState }, 900)); S->Feed(4017, Body4017(kRidC, 3, {}, 900));   // прошлое наложение кончилось
		S->Target(kRidC);
		S->Feed(5270, Body5270(S->Self, kSkill, kRidC, 1857, 0x03, 700));   // свежий каст — ПОСЛЕ снятия
	}, [S]() { return S->MobHas(kRidC, kState) || S->El() > 6.0; }, [S]()
	{
		S->Expect(S->MobHas(kRidC, kState), FString::Printf(TEXT("свежий каст после снятия наложил состояние (гейт не глушит новое), через %.2f с"), S->El()));
	});
	Add(TEXT("3б снятие свежего"), 6.f, [S]()
	{
		S->Feed(4017, Body4017(kRidC, 3, { kState }, 700));
	}, [S]() { if (S->El() > 2.2 && !S->bRemovedFed) { S->Feed(4017, Body4017(kRidC, 3, {}, 700)); S->bRemovedFed = true; S->RemovedAtS = FPlatformTime::Seconds(); }
		return S->bRemovedFed && FPlatformTime::Seconds() - S->RemovedAtS > 2.5; }, [S]()
	{
		S->Expect(!S->MobHas(kRidC, kState) && !S->MobFxAlive(kRidC, kState), TEXT("4017 без состояния снял его, и оно не вернулось"));
		S->bRemovedFed = false;
	});

	// ── 4. 4102 (ловушка) → 4017 снимает ──
	Add(TEXT("4 ловушка 4102 → 4017 снимает"), 4.f, [S]()
	{
		S->Feed(4102, Body4102(kRidD, kTrapSkill, S->Self));
		S->Feed(4017, Body4017(kRidD, 3, { 0 }, 100));
	}, [S]() { if (S->El() > 1.0 && !S->bRemovedFed) { S->Expect(S->MobHas(kRidD, 0), TEXT("4102 наложил состояние 0 «Оледенение»")); S->Feed(4017, Body4017(kRidD, 3, {}, 100)); S->bRemovedFed = true; }
		return S->bRemovedFed && S->El() > 2.0; }, [S]()
	{
		S->Expect(!S->MobHas(kRidD, 0), TEXT("4017 без состояния снял оледенение"));
		S->bRemovedFed = false;
	});

	// ── 5. смерть при висящем состоянии ──
	Add(TEXT("5 смерть моба"), 4.f, [S]()
	{
		S->Feed(4102, Body4102(kRidE, kTrapSkill, S->Self));
		S->Feed(4017, Body4017(kRidE, 3, { 0 }, 100));
	}, [S]() { if (S->El() > 0.8 && !S->bRemovedFed) { S->Expect(S->MobHas(kRidE, 0), TEXT("состояние висит до смерти")); S->Feed(4017, Body4017(kRidE, 0, { 0 }, 0)); S->bRemovedFed = true; S->RemovedAtS = FPlatformTime::Seconds(); }
		return S->bRemovedFed && FPlatformTime::Seconds() - S->RemovedAtS > 1.5; }, [S]()
	{
		const AAsdaMobActor* M = S->Mob(kRidE);
		S->Expect(M && M->IsDead(), TEXT("моб мёртв (4017 flags=0), труп ещё в мире"));
		S->Expect(S->Fx()->GetMobList(kRidE).Num() == 0, TEXT("на трупе состояний нет — эффекты погашены в момент смерти"));
		S->bRemovedFed = false;
	});

	// ── 6. смена карты при висящем состоянии ──
	Add(TEXT("6 смена карты"), 3.f, [S]()
	{
		S->Feed(4102, Body4102(kRidF, kTrapSkill, S->Self));
		S->Feed(4017, Body4017(kRidF, 3, { 0 }, 100));
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		S->Expect(S->MobHas(kRidF, 0), TEXT("состояние висит до смены карты"));
		S->Fx()->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		S->Expect(S->Fx()->GetMobList(kRidF).Num() == 0, TEXT("после смены карты списка состояний моба нет"));
		S->Expect(!S->Fx()->IsMobStateEndedSince(kRidF, 0, 0.0), TEXT("серверные списки 4017 прошлой карты забыты"));
	});

	// ── 7. своё состояние истекает по таймеру ──
	Add(TEXT("7 своё состояние 5272 истекает"), 6.f, [S]()
	{
		S->Feed(5272, Body5272(S->Self, kState, 2));
	}, [S]() { return S->El() > 0.5 && !S->CharHas(S->Self, kState); }, [S]()
	{
		S->Expect(!S->CharHas(S->Self, kState) && S->El() < 5.0, FString::Printf(TEXT("своё состояние (остаток 2 с) снято таймером через %.1f с"), S->El()));
	});

	// ── 8. состояние соседа снимает 5268 ──
	Add(TEXT("8 сосед: 5272 → 5268"), 3.f, [S]()
	{
		S->Feed(5272, Body5272(kPeer, kState, 2));
	}, [S]() { if (S->El() > 0.6 && !S->bRemovedFed) { S->Expect(S->CharHas(kPeer, kState), TEXT("состояние у соседа наложено 5272")); S->Feed(5268, Body5268(kPeer, kState)); S->bRemovedFed = true; }
		return S->bRemovedFed && S->El() > 1.2; }, [S]()
	{
		S->Expect(!S->CharHas(kPeer, kState), TEXT("5268 сервера снял состояние соседа"));
		S->bRemovedFed = false;
	});

	// ── 9–11. Подсказки иконок состояний цели и своих ──
	//   Общая схема: состояние наложено → цель выбрана → иконка в слоте 0 панели цели → курсор на неё → плашка TargetState
	//   с текстом stateinfotip → курсор прочь → плашка гаснет.
	auto TipStep = [S, Add](const FString& Name, TFunction<void()> Apply, TFunction<AActor*()> TargetOf, int32 StateId)
	{
		Add(Name, 6.f, [S, Apply, TargetOf]()
		{
			S->bHover = false; S->CursorAway();
			Apply();
			if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(TargetOf()); }
		}, [S, StateId]()
		{
			UAsdaTargetFrameSubsystem* F = S->TF();
			if (!S->bHover && F && F->DebugEnemyStateId(0) == StateId && F->DebugEnemyStateIconShown(0)) { S->HoverOver(F->DebugEnemyStateIcon(0)); }
			return S->bHover && FPlatformTime::Seconds() - S->HoverAtS > 0.6;
		}, [S, StateId]()
		{
			UAsdaTargetFrameSubsystem* F = S->TF();
			S->Expect(F && F->DebugEnemyStateId(0) == StateId, FString::Printf(TEXT("иконка состояния %d в слоте 0 панели цели"), StateId));
			S->Expect(S->TipShownBy(EAsdaTipOwner::TargetState),
				FString::Printf(TEXT("наведение на иконку цели — подсказка показана (владелец TargetState): %s"), *S->StateTipText(StateId)));
			S->Expect(!S->StateTipText(StateId).IsEmpty(), TEXT("у состояния есть имя/описание stateinfotip на языке сессии"));
			S->CursorAway();
		});
		Add(Name + TEXT(" — уход курсора"), 2.f, nullptr, [S]() { return S->El() > 0.4; }, [S]()
		{
			S->Expect(!S->TipShownBy(EAsdaTipOwner::TargetState), TEXT("курсор ушёл с иконки — подсказка цели погашена"));
		});
	};
	TipStep(TEXT("9 подсказка: цель-моб"), [S]()
	{
		S->Feed(4102, Body4102(kRidT, kTrapSkill, S->Self));
		S->Feed(4017, Body4017(kRidT, 3, { 0 }, 100));
	}, [S]() -> AActor* { return S->Mob(kRidT); }, 0);
	TipStep(TEXT("10 подсказка: цель-игрок (сосед)"), [S]()
	{
		S->Feed(5272, Body5272(kPeer, kState, 30));
	}, [S]() -> AActor* { const UAsdaWorldSubsystem* Ws = S->WS(); return Ws ? Ws->ActorByTarget(EAsdaTargetKind::Character, kPeer) : nullptr; }, kState);
	Add(TEXT("11 регресс: своя иконка мини-статуса"), 6.f, [S]()
	{
		S->bHover = false; S->CursorAway();
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
		S->Feed(5272, Body5272(S->Self, kState, 30));
	}, [S]()
	{
		UAsdaMiniStateSubsystem* M = S->Mini();
		for (int32 i = 0; !S->bHover && M && i < 28; ++i) { if (M->DebugBuffSlotId(i) == kState) { S->HoverOver(M->DebugBuffSlotIcon(i)); } }
		return S->bHover && FPlatformTime::Seconds() - S->HoverAtS > 0.6;
	}, [S]()
	{
		S->Expect(S->TipShownBy(EAsdaTipOwner::MiniState), FString::Printf(TEXT("своя иконка состояния — подсказка мини-статуса показана: %s"), *S->StateTipText(kState)));
		S->CursorAway();
		S->Feed(5268, Body5268(S->Self, kState));
	});

	// ── 12. Соул-состояние (type 1) у игрока-цели: иконка из soulstateinfotip, подсказка `uiHelpEntryLayoutAndShowAlt` ──
	Add(TEXT("12 подсказка: соул-состояние цели-игрока"), 6.f, [S]()
	{
		S->bHover = false; S->CursorAway();
		S->FeedHdr(5306, {}, kPeer);   // чистый список соседа — соул-состояние встанет в слот 0
		for (int32 i = 0; i < 30 && S->SoulSlot < 0; ++i) { FString Ti, At, Bo; int32 Ce = 0; if (S->Fx()->GetSoulStateInfo(i, Ti, At, Ce, Bo)) { S->SoulSlot = i; } }
		TArray<uint8> B; B.SetNumZeroed(6);
		B[0] = (uint8)(kPeer & 0xFF); B[1] = (uint8)((kPeer >> 8) & 0xFF);
		const int32 Sl = FMath::Max(0, S->SoulSlot); B[2] = (uint8)(Sl & 0xFF); B[3] = (uint8)((Sl >> 8) & 0xFF);
		B[4] = 30; B[5] = 0;
		S->Feed(5229, B);
		if (AAsdaPlayerController* P = S->PC()) { const UAsdaWorldSubsystem* Ws = S->WS(); P->SetTarget(Ws ? Ws->ActorByTarget(EAsdaTargetKind::Character, kPeer) : nullptr); }
	}, [S]()
	{
		UAsdaTargetFrameSubsystem* F = S->TF();
		if (!S->bHover && F && F->DebugEnemyStateType(0) == 1 && F->DebugEnemyStateIconShown(0)) { S->HoverOver(F->DebugEnemyStateIcon(0)); }
		return S->bHover && FPlatformTime::Seconds() - S->HoverAtS > 0.6;
	}, [S]()
	{
		UAsdaTargetFrameSubsystem* F = S->TF();
		FString Ti, At, Bo; int32 Ce = 0;
		const bool bInfo = S->SoulSlot >= 0 && S->Fx()->GetSoulStateInfo(S->SoulSlot, Ti, At, Ce, Bo);
		S->Expect(bInfo && S->CharHasType(kPeer, 1, S->SoulSlot), FString::Printf(TEXT("5229: у соседа соул-состояние слота %d «%s»"), S->SoulSlot, *Ti));
		S->Expect(F && F->DebugEnemyStateType(0) == 1 && F->DebugEnemyStateId(0) == S->SoulSlot, TEXT("иконка слота 0 панели цели — соул-состояние (type 1)"));
		// Иконка — из атласа soulstateinfotip (эталон `partyFindMemberSlotByIndex`), а не stateinfotip того же номера.
		const UImage* Ic = F ? F->DebugEnemyStateIcon(0) : nullptr;
		const UObject* Res = Ic ? Ic->GetBrush().GetResourceObject() : nullptr;
		const FString ResName = Res ? Res->GetName() : FString();
		const FString AtlasBase = FPaths::GetBaseFilename(At);
		S->Expect(!AtlasBase.IsEmpty() && ResName.Contains(AtlasBase), FString::Printf(TEXT("иконка из атласа соул-состояний «%s» (на иконке «%s»)"), *AtlasBase, *ResName));
		const FString On = S->TipText();
		S->Expect(S->TipShownBy(EAsdaTipOwner::TargetState) && !Ti.IsEmpty() && On.Contains(Ti),
			FString::Printf(TEXT("наведение — подсказка soulstateinfotip «%s» (на плашке: %s)"), *Ti, *On.Left(120)));
		S->CursorAway();
	});
	Add(TEXT("12 — уход курсора"), 2.f, nullptr, [S]() { return S->El() > 0.4; }, [S]()
	{
		S->Expect(!S->TipShownBy(EAsdaTipOwner::TargetState), TEXT("курсор ушёл — подсказка соул-состояния погашена"));
	});

	// ── 13. Смерть ИГРОКА: сервер шлёт 5306 (GameServer `sub_47A830` → 5306), клиент снимает всё, кроме предметных (type 2) ──
	Add(TEXT("13 смерть соседа и своя: 5306"), 3.f, [S]()
	{
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
		S->Feed(5272, Body5272(kPeer, kState, 30));
		S->Feed(5272, Body5272(S->Self, kState, 30));
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		S->Expect(S->CharHas(kPeer, kState) && S->CharHas(S->Self, kState), TEXT("состояния у соседа и у себя наложены 5272"));
		S->FeedHdr(5306, {}, kPeer);
		S->Expect(!S->CharHas(kPeer, kState) && !S->CharHasType(kPeer, 1, S->SoulSlot), TEXT("5306 соседа (смерть): его состояния и соул-состояние сняты"));
		S->Expect(S->CharHas(S->Self, kState), TEXT("5306 соседа не трогает свои состояния"));
		S->FeedHdr(5306, {}, S->Self);
		S->Expect(!S->CharHas(S->Self, kState), TEXT("5306 своя (смерть): свои состояния сняты"));
	});

	// ── 14. Смена карты для ИГРОКОВ: соседи прошлой карты уходят, своё состояние переносится ──
	Add(TEXT("14 смена карты: игроки"), 3.f, [S]()
	{
		S->Feed(5272, Body5272(kPeer, kState, 30));
		S->Feed(5272, Body5272(S->Self, kState, 30));
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		S->Expect(S->CharHas(kPeer, kState) && S->CharHas(S->Self, kState), TEXT("состояния у соседа и у себя наложены"));
		S->Fx()->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		S->Expect(S->Fx()->GetList(kPeer).Num() == 0, TEXT("после смены карты списка соседа прошлой карты нет"));
		S->Expect(S->CharHas(S->Self, kState), TEXT("своё состояние пережило смену карты (сервер его не переприсылает)"));
	});

	// ── 15. Перезаход (полный сброс сессии): не остаётся ничего ни у кого ──
	Add(TEXT("15 перезаход: полный сброс"), 3.f, [S]()
	{
		S->Feed(5272, Body5272(kPeer, kState, 30));
		S->Feed(4102, Body4102(kRidT, kTrapSkill, S->Self));
		S->Feed(4017, Body4017(kRidT, 3, { 0 }, 100));
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		S->Expect(S->CharHas(S->Self, kState) && S->CharHas(kPeer, kState) && S->MobHas(kRidT, 0), TEXT("состояния у себя, соседа и моба висят до перезахода"));
		const int32 SelfIdx = S->Self;
		S->Fx()->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		S->Expect(S->Fx()->GetList(SelfIdx).Num() == 0 && S->Fx()->GetList(kPeer).Num() == 0 && S->Fx()->GetMobList(kRidT).Num() == 0,
			TEXT("выход в выбор персонажа: списки своего, соседа и моба пусты"));
		S->Expect(!S->Fx()->IsMobStateEndedSince(kRidT, 0, 0.0), TEXT("серверные списки 4017 забыты"));
	});

	// ── итог ──
	Add(TEXT("итог"), 3.f, [S]()
	{
		for (const int32 Rid : S->Mobs) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Rid); S->Feed(4011, B); }
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kPeer); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
	}, [S]() { return S->El() > 0.5; }, [S, Net]()
	{
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaStatusExpiryTest, TEXT("САМОТЕСТ-EXPIRY"), S->Ok, S->Fail);
	});

	// ── спавн и исполнитель (как у `asda.Skill.SelfTestBowFx`) ──
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
			UE_LOG(LogAsdaStatusExpiryTest, Log, TEXT("САМОТЕСТ-EXPIRY шаг %s"), *Stp.Name);
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestStatusExpiry(
	TEXT("asda.Buff.SelfTestExpiry"),
	TEXT("Самотест истечения состояний (04.11): гонка «4017 снял до кадра удара» на мобе (+контроль asda.Buff.MobStaleHitGate 0), "
	     "новый каст после снятия, 4102→4017, смерть и смена карты при висящем состоянии, своё 5272 по таймеру, сосед 5268. "
	     "Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Buff.SelfTestExpiry"));
		FAsdaStatusExpirySelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
