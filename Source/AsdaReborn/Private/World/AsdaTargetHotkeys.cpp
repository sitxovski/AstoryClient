// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTargetHotkeys.cpp — клавиши «атаковать цель» (cmd 59) и «выбрать ближайшего врага» (cmd 60). Разбор — у объявлений
//   (`World/AsdaTargetHotkeys.h`) и в `Reports/keybinds/AUDIT.md`. ★ 25.10.2026.
#include "World/AsdaTargetHotkeys.h"
#include "AsdaDevConsole.h"                 // ASDA_DEV_CVAR — ключ отката
#include "AsdaNetSubsystem.h"
#include "AsdaEffectActor.h"                // kAsdaGmHiddenTagName — метка GM-скрытого персонажа
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCoord.h"                // игровые координаты (UE ↔ игра)
#include "Npc/AsdaMobActor.h"
#include "Items/AsdaItemTableSubsystem.h"   // удочка: тип 10 / подтип 1
#include "UI/AsdaSkillUseSubsystem.h"       // авто-бур (`playerHasEquippedItemType10Sub2`)
#include "UI/AsdaChatSubsystem.h"           // цвета нотисов эталона (−1 белый, −11514 золотой)
#include "UI/AsdaPremiumShopSubsystem.h"    // `helper_getField2075(g_pShopWnd)` — элитный магазин открыт
#include "UI/AsdaCraftWindowSubsystem.h"    // `tileInteract_isControlVisible(g_pItemMainWnd)` — окно 76000
#include "UI/AsdaCraftMakeWindowSubsystem.h" // `isBoundControlVisible(g_pItemQtyMakeDlg)` — окно 77500
#include "UI/AsdaWarehouseSubsystem.h"      // `CObject_GetField5581(g_pWarehouseWnd)` — свиток возврата склада
#include "UI/AsdaPlayerMenuSubsystem.h"     // `followTargetCancelReset` — следование за игроком
#include "UI/AsdaCastBarSubsystem.h"        // эталонные состояния каста 13/14/24/27
#include "UI/AsdaEffectsSubsystem.h"        // список состояний персонажа (547, 112/186)
#include "World/AsdaSkillCastLock.h"         // ★ 05.11.2026: фаза своего навыка для состояния действия
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"                    // TActorIterator — перебор мобов/персонажей мира

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTargetKeys, Log, All);

// ★ Ключ отката веток 56/59/60 (`ExecuteCommand`, вкладка «Функции» окна эмоций 2/4). 0 — прежнее: команды уходили в
//   `OnGameCommand` без подписчиков (клавиши «атака», «ближайший враг», «подбор» молчали).
static ASDA_DEV_CVAR(int32, CVarAsdaKeysTargetActions,
	TEXT("asda.Keys.TargetActions"), 1,
	TEXT("[ОРИГИНАЛ] 1 — клавиши раскладки 56 «подобрать», 59 «атаковать цель», 60 «ближайший враг» исполняются по эталону ")
	TEXT("(itemPickupGroundItem / Input_HandleTargetInteractClick / targetPick_updateSelection); 0 — прежнее (команды ничего не делали)."),
	ECVF_Default);

// ★ 25.10.2026 Ключ отката клавиш отряда/цели 48–50, 61–64 (и функций эмоций 1/0…1/3, 2/5, 2/6). 0 — прежнее.
static ASDA_DEV_CVAR(int32, CVarAsdaKeysPartyActions,
	TEXT("asda.Keys.PartyActions"), 1,
	TEXT("[ОРИГИНАЛ] 1 — клавиши 48–50 (отряд), 61 (осмотр цели), 62 (Shift+Tab), 63/64 (общая цель) и функции эмоций 1/0…1/3, 2/5, 2/6 ")
	TEXT("исполняются по эталону (UI_DispatchTargetContextMenu / input_HandleTargetSelectClick / case 64); 0 — прежнее (ничего не делали)."),
	ECVF_Default);

bool AsdaTargetHotkeys::IsPartyActionsEnabled()
{
	return CVarAsdaKeysPartyActions.GetValueOnGameThread() != 0;
}

namespace
{
	// Пороги эталона (константы .rdata сборки IDB, сняты дизасмом 25.10.2026):
	//   0x13E30E4 = 2 300 000.0 — `charFindNearestEntityInRange@0xF78F00` (моб, Math_DistanceSquared3D);
	//   0x13A9278 = 2 000 000.0 — `charManagerFindNearestTargetVariant@0x9DBAC0` (персонаж, dist2DSquared).
	constexpr double kMobNearestD2 = 2300000.0;
	constexpr double kCharNearestD2 = 2000000.0;

	// Эталонное `+190204` своего персонажа в объёме, который нужен ветке моба `Input_HandleTargetInteractClick`.
	//   Тот же состав, что у `AAsdaPlayerController::KbEtalonActionState` (там метод закрытый): 6 смерть, 7/8/9 сидение,
	//   5 замах, 13/14/24/27 каст, 24 клип навыка, занятость (рыбалка/лопата/эмоция/68), 1 ход, 0 покой.
	int32 SelfActionState(const AAsdaPlayerController* PC, const AAsdaPlayerCharacter* Ch)
	{
		if (PC->IsSelfDead()) { return 6; }
		if (!Ch) { return 0; }
		if (const int32 Sit = Ch->GetEtalonSitState()) { return Sit; }
		// ★ 05.11.2026 [ОРИГИНАЛ] (`World/AsdaSkillCastLock`): своя фаза навыка 25/14/13/27/24 — та же, что у `KbEtalonActionState`.
		if (const int32 SkPh = AsdaSkillCastLock::PhaseForActionState(PC->GetGameInstance())) { return SkPh; }
		if (PC->bAutoAttack && (PC->bInCombatStance || Ch->IsAttackAnimActive())) { return 5; }
		if (const UAsdaCastBarSubsystem* Cb = PC->GetGameInstance() ? PC->GetGameInstance()->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr)
		{
			const int32 Cs = Cb->GetState();
			if (Cb->IsActive() && (Cs == 13 || Cs == 14 || Cs == 24 || Cs == 27)) { return Cs; }
		}
		if (Ch->IsSkillAnimActive()) { return 24; }
		if (const int32 Busy = Ch->GetBusyActionCode()) { return Busy; }
		return Ch->IsMoving() ? 1 : 0;
	}

	// Удочка в руке (`playerCheckEquippedItemType10`) — тот же признак, что у PvP-клика и навыков: оружие тип 10 / подтип 1.
	bool IsRodEquipped(UGameInstance* GI, const UAsdaNetSubsystem* Net)
	{
		const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const FAsdaItemRecord* R = (IT && Net && Net->SelfWeaponItemId > 0) ? IT->GetItem(Net->SelfWeaponItemId) : nullptr;
		return R && R->ItemType == 10 && R->ItemSubType == 1;
	}

	// `g_bWorldTickActive` (миснейм: «клавиатура НЕ ведёт ход») → `followTargetCancelReset(&g_bMapTransferActive)`.
	//   У порта точка этого сброса — следование за игроком окна 2000 (так же сделано у клика по миру, input.c:190/304/389/572).
	//   [ПОРТ] `g_nMiniMapViewMode = 2` рядом в эталоне — режим вида мини-карты; у порта такого поля нет (реестр отложенного).
	void CancelFollowIfKeyboardIdle(AAsdaPlayerController* PC, const TCHAR* Why)
	{
		if (PC->IsKeyboardEtalonMoveActive()) { return; }
		UAsdaPlayerMenuSubsystem* PMenu = PC->GetGameInstance() ? PC->GetGameInstance()->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
		if (PMenu && PMenu->IsFollowing()) { PMenu->CancelFollow(Why); }
	}

	// Ветка `g_nInputControlState == 1` (цель — моб) `Input_HandleTargetInteractClick` (input.c:1336-1543).
	bool AttackMob(AAsdaPlayerController* PC, AAsdaMobActor* M, const TCHAR* Why)
	{
		UGameInstance* GI = PC->GetGameInstance();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
		if (!GI || !Net || !WS || !Ch) { return false; }
		// 1) Свиток возврата склада в пути — молча (`CObject_GetField5581(g_pWarehouseWnd)`, input.c:1340).
		if (const UAsdaWarehouseSubsystem* Wh = GI->GetSubsystem<UAsdaWarehouseSubsystem>(); Wh && Wh->IsRecallActive())
		{
			UE_LOG(LogAsdaTargetKeys, Log, TEXT("[атака цели] %s: идёт свиток возврата склада — молча (input.c:1340)"), Why);
			return false;
		}
		// 2) Авто-бур — нотис 111204 белым; 3) удочка — 110707 белым (input.c:1342-1349).
		const UAsdaSkillUseSubsystem* SU = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
		if (SU && SU->IsAutoShovelEquipped()) { WS->PostPvpNotice(111204, UAsdaChatSubsystem::ColWhite(), TEXT("клавиша атаки: авто-бур")); return false; }
		if (IsRodEquipped(GI, Net)) { WS->PostPvpNotice(110707, UAsdaChatSubsystem::ColWhite(), TEXT("клавиша атаки: удочка")); return false; }
		// 4) Клавиатура не ведёт ход — снять следование (input.c:1350-1354).
		CancelFollowIfKeyboardIdle(PC, TEXT("клавиша атаки цели"));
		// 5) Цель — сетевой моб реестра (`Entity_FindByRenderId_thunk(g_nTargetUserIdx)`, input.c:1357); иначе молча.
		if (M->NetRenderId < 0 || WS->FindNetMobByRid(M->NetRenderId) != M) { return false; }
		// 6) Уже бьём ЭТУ цель (состояние 5/15/1 с той же целью, input.c:1360/1471) — молча, серия идёт.
		if (PC->bAutoAttack && !PC->bPvpAttack && PC->TargetActor.Get() == M)
		{
			UE_LOG(LogAsdaTargetKeys, Log, TEXT("[атака цели] %s: бой по rid=%d уже идёт — без новой заявки (эталон)"), Why, M->NetRenderId);
			return true;
		}
		// 7) Цель мертва (`Entity_GetActionState == 10`, input.c:1364) — молча.
		if (M->IsDead()) { return false; }
		// 8) Занятые состояния (input.c:1369-1388): 22/15/5/24/25/14/27/13/7/8/9 или состояние 547 — новой атаки нет; при 5/24
		//   эталон ставит цель «следующей» (`sub_9356E7`). [ПОРТ] очереди «следующей цели» у порта нет — при 5/24 бой
		//   переключается на новую цель сразу тем же движком (`EngageMobFromClick` сам бросает прежнюю серию).
		const int32 St = SelfActionState(PC, Ch);
		const UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>();
		const bool bState547 = Fx && Fx->FindByTypeSub(Fx->SelfUserIdx(), 547, 0) != nullptr;
		static const int32 kBusy[] = { 22, 15, 5, 24, 25, 14, 27, 13, 7, 8, 9 };
		bool bBusy = bState547;
		for (const int32 B : kBusy) { bBusy |= (St == B); }
		if (bBusy && St != 5 && St != 24)
		{
			UE_LOG(LogAsdaTargetKeys, Log, TEXT("[атака цели] %s: своё состояние %d%s — атака не начата (эталон input.c:1369)"),
				Why, St, bState547 ? TEXT(" / 547") : TEXT(""));
			return false;
		}
		if (St == 6) { return false; }   // [ПОРТ] смерть: у эталона ветки нет (тик боя всё равно снял бы автоатаку)
		// 9) Нет оружия — 1004 золотым (input.c:1395 `localPlayer_2[52091] == -1`).
		if (Net->SelfWeaponItemId <= 0) { WS->PostPvpNotice(1004, UAsdaChatSubsystem::ColGold(), TEXT("клавиша атаки: нет оружия")); return false; }
		// 10) Элитный магазин — 6811 белым (input.c:1401).
		if (const UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>(); Ps && Ps->IsOpen())
		{
			WS->PostPvpNotice(6811, UAsdaChatSubsystem::ColWhite(), TEXT("клавиша атаки: элитный магазин"));
			return false;
		}
		// 11) Окна ремесла 76000/77500 — молча (input.c:1417).
		const UAsdaCraftWindowSubsystem* Cw = GI->GetSubsystem<UAsdaCraftWindowSubsystem>();
		const UAsdaCraftMakeWindowSubsystem* Cm = GI->GetSubsystem<UAsdaCraftMakeWindowSubsystem>();
		if ((Cw && Cw->IsOpen()) || (Cm && Cm->IsOpen())) { return false; }
		// [ПОРТ] Гейт 111205 (мобы 845…848 на карте командной битвы, input.c:1419-1427) не перенесён и у клика мышью —
		//   нет поля `g_TargetClickContext` (реестр отложенного «Командная битва»).
		// 12) Атака. Эталон шлёт 4026 сразу (`netSabzil_SendActionRaw`, + 4008 на ходу, троттл 200 мс по цели).
		//   [ПОРТ] бой порта ведёт один движок (`AttackTick`): далеко — подход 4006 с целью, в дальности — 4008 + 4026; тот же,
		//   что у второго клика мышью. 4026 издалека сервер принимает и бьёт, пока персонаж бежит (рассинхрон) — поэтому не он.
		PC->EngageMobFromClick(M);
		UE_LOG(LogAsdaTargetKeys, Log, TEXT("[атака цели] %s: бой по мобу rid=%d «%s» начат (автоатака=%d)"), Why, M->NetRenderId,
			*M->MobName, PC->bAutoAttack ? 1 : 0);
		return PC->bAutoAttack;
	}
}

bool AsdaTargetHotkeys::IsEnabled()
{
	return CVarAsdaKeysTargetActions.GetValueOnGameThread() != 0;
}

bool AsdaTargetHotkeys::AttackSelectedTarget(AAsdaPlayerController* PC, const TCHAR* Why)
{
	if (!PC) { return false; }
	Why = Why ? Why : TEXT("?");
	AActor* T = PC->TargetActor.Get();
	if (AAsdaMobActor* M = Cast<AAsdaMobActor>(T)) { return AttackMob(PC, M, Why); }
	if (AAsdaPlayerCharacter* Pl = Cast<AAsdaPlayerCharacter>(T))
	{
		// Ветка `g_nInputControlState == 0` (input.c:1545-1680): снять следование, затем ровно тело второго клика по игроку
		//   (`EngagePlayerFromClick` — порт этой же ветки: PvP-режим `[160]`, авто-бур, удочка, обездвижен 1161, окна, оружие 1004, 4201).
		CancelFollowIfKeyboardIdle(PC, TEXT("клавиша атаки цели"));
		if (Pl == PC->GetPawn()) { return false; }   // `localPlayer != targetChar`
		PC->EngagePlayerFromClick(Pl);
		UE_LOG(LogAsdaTargetKeys, Log, TEXT("[атака цели] %s: цель — персонаж, PvP-атака %s"), Why, PC->bPvpAttack ? TEXT("начата") : TEXT("не начата (гейт)"));
		return PC->bPvpAttack;
	}
	// Цели нет или это NPC/лут (`g_nInputControlState` 3 и прочие) — ветки у эталона нет.
	UE_LOG(LogAsdaTargetKeys, Log, TEXT("[атака цели] %s: цели-моба/персонажа нет — ничего (эталон)"), Why);
	return false;
}

bool AsdaTargetHotkeys::SelectNearestEnemy(AAsdaPlayerController* PC, const TCHAR* Why)
{
	if (!PC) { return false; }
	Why = Why ? Why : TEXT("?");
	UWorld* W = PC->GetWorld();
	UGameInstance* GI = PC->GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
	if (!W || !Net || !WS || !Me) { return false; }
	AActor* Cur = PC->TargetActor.Get();
	// [ОРИГИНАЛ] `g_bAltTargetCastMode` (@0x3528BD8) — в образе ни одной записи (5 чтений, все в 0x95DC70/0xE3E990; сплошной
	//   байтовый скан и декомпил), поле в .bss ⇒ всегда 0. Ветки `entityFindNearestInRange` / `charManagerFindNearestTarget`
	//   (×100 к дистанции раненых, порог 23 000 000 / 20 000 000) недостижимы; работают только «обычные» поиски ниже.
	if (!Net->IsSelfPvpOn())
	{
		// ── `charFindNearestEntityInRange@0xF78F00`: список сущностей (мобы), пропуск состояния 10 и текущей цели-моба
		//   (`g_nInputControlState == 1` → её renderId, иначе −1), Math_DistanceSquared3D < 2 300 000; при равенстве — первый.
		const AAsdaMobActor* CurMob = Cast<AAsdaMobActor>(Cur);
		const int32 Exclude = CurMob ? CurMob->NetRenderId : -1;
		const FVector From = AsdaCoord::UeToGame3D(FVector(Me->GetActorLocation().X, Me->GetActorLocation().Y, UAsdaWorldSubsystem::TargetSoleZ(Me)));
		AAsdaMobActor* Best = nullptr;
		double BestD2 = -1.0;
		for (TActorIterator<AAsdaMobActor> It(W); It; ++It)
		{
			AAsdaMobActor* M = *It;
			if (!M || !M->bNetSpawned || M->NetRenderId < 0 || WS->FindNetMobByRid(M->NetRenderId) != M) { continue; }   // только живые записи реестра
			if (M->IsDead() || M->NetRenderId == Exclude) { continue; }
			const FVector P = AsdaCoord::UeToGame3D(FVector(M->GetActorLocation().X, M->GetActorLocation().Y, UAsdaWorldSubsystem::TargetSoleZ(M)));
			const double D2 = FVector::DistSquared(From, P);
			if (D2 >= kMobNearestD2) { continue; }
			if (BestD2 < 0.0 || BestD2 > D2) { Best = M; BestD2 = D2; }
		}
		if (!Best)
		{
			UE_LOG(LogAsdaTargetKeys, Log, TEXT("[ближайший враг] %s: живых мобов в радиусе нет (исключён rid=%d) — цель не меняется"), Why, Exclude);
			return false;
		}
		PC->SetTarget(Best);   // плашка/панель цели/кольцо — та же точка, что у клика (`uiSwitchToBattlePanel`, `hud_setTarget`…)
		UE_LOG(LogAsdaTargetKeys, Log, TEXT("[ближайший враг] %s: моб rid=%d «%s», дистанция %.0f (исключён rid=%d)"), Why,
			Best->NetRenderId, *Best->MobName, FMath::Sqrt(BestD2), Exclude);
		return true;
	}
	// ── PvP-режим (`buffFxSlots[160] == 1`): `charManagerFindNearestTargetVariant@0x9DBAC0` (дизасм 0x9DBAC0..0x9DBD29).
	//   Исключение: текущая цель-персонаж (`g_nInputControlState == 0` → `g_nTargetUserIdx`), иначе −1.
	const int32 SelfU = Net->bSelfUserNumKnown ? (int32)Net->SelfUserNum : -1;
	const UAsdaNetSubsystem::FAsdaPvpState* MyPvp = Net->GetPvpState(SelfU);
	const int32 Mode = MyPvp ? (int32)MyPvp->Mode : 0;   // `[92]`
	const AAsdaPlayerCharacter* CurPl = Cast<AAsdaPlayerCharacter>(Cur);
	const int32 Exclude = CurPl ? WS->FindUserIdxByActor(CurPl) : -1;
	const UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>();
	const int32 MyClan = WS->GetSelfClanId();
	const int32 MyMate = WS->GetSoulPartnerUserIdx(SelfU);
	const FVector2D From = Me->GetGamePosXZ();
	AAsdaPlayerCharacter* Best = nullptr;
	double BestD2 = -1.0;
	if (Mode != 0)   // `[92] == 0` — ни одного кандидата (0x9DBB8A)
	{
		for (TActorIterator<AAsdaPlayerCharacter> It(W); It; ++It)
		{
			AAsdaPlayerCharacter* C = *It;
			if (!C || C == Me) { continue; }                                   // `+190260` — свой персонаж
			const int32 U = WS->FindUserIdxByActor(C);
			if (U < 0 || U == SelfU || U == Exclude) { continue; }             // `+190188 != a5`
			if (C->IsIncapacitated()) { continue; }                             // состояния 6 / 29 (труп)
			if (Fx && (Fx->FindByTypeSub(U, 112, 0) || Fx->FindByTypeSub(U, 186, 0))) { continue; }   // `skillHasBuffType112or186`
			if (C->ActorHasTag(FName(kAsdaGmHiddenTagName))) { continue; }     // `sub_93B218 & 1` — GM-скрыт (метка `SetCharHiddenByGm`)
			// [ПОРТ] `(+190208 & 6) != 0` — биты занятости персонажа; у порта их нет (реестр «Биты занятости»).
			const UAsdaNetSubsystem::FAsdaPvpState* His = Net->GetPvpState(U);
			const int32 HisTeam = His ? His->Team : -1;                                         // нет пакета 4206 о нём — «команды нет»
			if (Mode == 4 && MyPvp && HisTeam == MyPvp->Team) { continue; }                     // своя команда (`+0x33078`)
			if (Mode == 1 && (!MyPvp || MyPvp->Rival != U)) { continue; }                     // дуэль: только соперник (`+0x33054`)
			if (Mode == 2)
			{
				FString Nm; int32 ClassId = 0, Lvl = 0, HisClan = -1;
				WS->GetNetPlayerInfo(U, Nm, ClassId, Lvl, HisClan);
				if (MyClan > 0 && HisClan == MyClan) { continue; }              // свой клан (`+0x968`, свой > 0)
				if (MyMate >= 0 && MyMate == U) { continue; }                   // свой Друг (`+0x8A7` ↔ charId `+0x41C`; у порта — userIdx партнёра)
				if (WS->IsInParty() && WS->IsPartyMember(U)) { continue; }      // согруппник (`Party_FindMemberByUserIdx`)
			}
			const double D2 = FVector2D::DistSquared(From, C->GetGamePosXZ());
			if (D2 >= kCharNearestD2) { continue; }
			if (BestD2 < 0.0 || BestD2 > D2) { Best = C; BestD2 = D2; }
		}
	}
	if (!Best)
	{
		UE_LOG(LogAsdaTargetKeys, Log, TEXT("[ближайший враг] %s: PvP-режим %d — противника в радиусе нет (исключён %d)"), Why, Mode, Exclude);
		return false;
	}
	PC->SetTarget(Best);
	UE_LOG(LogAsdaTargetKeys, Log, TEXT("[ближайший враг] %s: PvP-режим %d — персонаж userIdx=%d, дистанция %.0f"), Why, Mode,
		WS->FindUserIdxByActor(Best), FMath::Sqrt(BestD2));
	return true;
}
