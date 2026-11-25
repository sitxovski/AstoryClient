// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPvpRelation.cpp — разбор в заголовке (`World/AsdaPvpRelation.h`).
#include "World/AsdaPvpRelation.h"
#include "AsdaDevConsole.h"                 // ASDA_DEV_CVAR — дев-ключ вне Shipping
#include "AsdaNetSubsystem.h"               // своё/чужое состояние PvP (4206)
#include "World/AsdaWorldSubsystem.h"       // клан, Друг, отряд, лагерь соседа, GM-скрытие
#include "World/AsdaPlayerCharacter.h"      // IsIncapacitated — состояния 6/29
#include "World/AsdaLoadManagerSubsystem.h" // CurrentMapId — карта осады 19/23
#include "AsdaEffectActor.h"                // kAsdaGmHiddenTagName — метка GM-скрытого персонажа
#include "Engine/GameInstance.h"

// ★ Ключ `asda.Skill.TargetRelation`: 1 — эталонные гейты отношения к цели-персонажу (заявка навыка, площадной сбор, карты осады);
//   0 — прежний порт (навык по игроку шёл без проверок клиента, площадь брала всех персонажей бокса).
static ASDA_DEV_CVAR(int32, CVarTargetRelation, TEXT("asda.Skill.TargetRelation"), 1,
	TEXT("Цели навыков среди игроков: 1 = как оригинал (PvP-флаг, дуэль, команда, свои в свободном бою; площадь не берёт своих и мирных ")
	TEXT("вне PvP), 0 = прежний порт (без проверок клиента)"), ECVF_Default);

namespace AsdaPvpRel
{
	bool IsOn() { return CVarTargetRelation.GetValueOnGameThread() != 0; }

	FSelf GetSelf(const UGameInstance* GI)
	{
		FSelf Me;
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net || !Net->bSelfUserNumKnown) { return Me; }
		Me.SelfIdx = (int32)Net->SelfUserNum;
		if (const UAsdaNetSubsystem::FAsdaPvpState* S = Net->GetPvpState(Me.SelfIdx))
		{
			Me.bOn = S->bOn; Me.Mode = (int32)S->Mode; Me.Rival = S->Rival; Me.Team = S->Team;
		}
		// Карты осады 19/23: `[92] = 4`, `[116]` = свой лагерь (`game_main.c:3910-3916`) — флаг `[160]` эталон здесь не трогает.
		const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
		const int32 MapId = LM ? LM->CurrentMapId : -1;
		if (IsOn() && (MapId == 19 || MapId == 23))
		{
			Me.bCampWarMap = true;
			Me.Mode = 4;
			if (const UAsdaWorldSubsystem* W = GI->GetSubsystem<UAsdaWorldSubsystem>())
			{
				const int32 Camp = W->GetSelfCampId();
				Me.Team = (Camp >= 0) ? Camp : -1;
			}
		}
		return Me;
	}

	int32 PeerTeam(const UGameInstance* GI, int32 UserIdx, const FSelf& Me)
	{
		if (Me.bCampWarMap)
		{
			const UAsdaWorldSubsystem* W = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			return W ? W->GetPeerCampId(UserIdx) : -1;
		}
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const UAsdaNetSubsystem::FAsdaPvpState* S = Net ? Net->GetPvpState(UserIdx) : nullptr;
		return S ? S->Team : -1;   // пакета 4206 о нём не было — «команды нет»
	}

	bool IsFriendlyInFreePvp(const UGameInstance* GI, const FSelf& Me, int32 UserIdx)
	{
		const UAsdaWorldSubsystem* W = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!W) { return false; }
		FString Nm; int32 ClassId = 0, Lvl = 0, HisClan = -1;
		W->GetNetPlayerInfo(UserIdx, Nm, ClassId, Lvl, HisClan);
		const int32 MyClan = W->GetSelfClanId();
		if (MyClan > 0 && HisClan == MyClan) { return true; }                       // свой клан (`+0x968`, свой > 0)
		const int32 Mate = W->GetSoulPartnerUserIdx(Me.SelfIdx);
		if (Mate >= 0 && Mate == UserIdx) { return true; }                           // свой Друг (`+0x8A7`)
		if (W->IsInParty() && W->IsPartyMember(UserIdx)) { return true; }            // согруппник
		return false;
	}

	EArea AreaCandidate(const UGameInstance* GI, const FSelf& Me, int32 UserIdx, int32 PrimaryUserIdx, FString* OutWhy)
	{
		auto Skip = [OutWhy](const TCHAR* W) { if (OutWhy) { *OutWhy = W; } return EArea::Skip; };
		if (UserIdx < 0 || UserIdx == Me.SelfIdx) { return Skip(TEXT("свой персонаж")); }
		if (UserIdx == PrimaryUserIdx) { return Skip(TEXT("основная цель (запись №0)")); }
		if (Me.Mode == 0) { return Skip(TEXT("режим [92] = 0 — персонажей не берёт")); }
		const UAsdaWorldSubsystem* W = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!W) { return Skip(TEXT("нет мира")); }
		if (const AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(W->ActorByUserIdx(UserIdx)))
		{
			if (P->IsIncapacitated()) { return Skip(TEXT("мёртв (6/29)")); }
			if (P->ActorHasTag(FName(kAsdaGmHiddenTagName))) { return Skip(TEXT("GM-скрыт")); }   // `sub_93B218 & 1` — метка `SetCharHiddenByGm`
		}
		if (Me.Mode == 1)
		{
			// Дуэль: только соперник, и на нём сбор кончается (`return v17 + 1`).
			return (Me.Rival == UserIdx) ? EArea::AddAndStop : Skip(TEXT("дуэль: не соперник"));
		}
		if (Me.Mode == 4 && Me.Team == PeerTeam(GI, UserIdx, Me)) { return Skip(TEXT("своя команда")); }
		if (Me.Mode == 2 && IsFriendlyInFreePvp(GI, Me, UserIdx)) { return Skip(TEXT("свой (клан/Друг/отряд) в свободном бою")); }
		return EArea::Add;
	}
}
