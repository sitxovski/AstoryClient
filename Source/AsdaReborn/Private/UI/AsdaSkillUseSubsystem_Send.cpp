// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillUseSubsystem_Send.cpp — часть `AsdaSkillUseSubsystem.cpp`: области и отправка: SendUseAreaMixed/SelfOrArea, SendUse (46 КБ).
//   ★ R2 рефакторинга (10.11.2026): определения перенесены из `AsdaSkillUseSubsystem.cpp` ДОСЛОВНО (сверка `Tools/asda_split_check.py`);
//   общие помощники и объявления — `AsdaSkillUseSubsystemInternal.h`. Шапка системы и ссылки на эталон — в `AsdaSkillUseSubsystem.cpp` и заголовке класса.

#include "UI/AsdaSkillUseSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaNoticeColors.h"   // ★ 08.11.2026: золото плашки отказа навыка (−11514)
#include "World/AsdaPossessGate.h"   // ★ гейт действий под GM-«скрыться» (AsdaGmHide)

#include "UI/AsdaCastBarSubsystem.h"
#include "AsdaBytes.h"                  // ★ 31.10.2026 (баг 7): разбор шапки 5232
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSoulmateSkillSubsystem.h"   // ★ 31.10.2026 (баг 7): вид навыка соулмейта rec[50] для гейта 5231
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайлы
#include "World/AsdaPlayerController.h"
// ★ Сетка проходимости карты — наш аналог `GroundTarget_PickOnMesh_thunk` для точки отброса.
#include "World/AsdaWalkGrid.h"
#include "World/AsdaPathfinder.h"          // ★ 08.11.2026: рывок 537/597 — земля на пути до цели (13219)
#include "Npc/AsdaMobActor.h"
#include "World/AsdaWorldSubsystem.h"
// ★ Источник максимума полосы применения (COMBO0) — таблица клипов навыков DT_SkillClips.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "Skill/AsdaSkillClipRow.h"
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — нажатие навыка, гейты, подход, заявка, ответ, отказ

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaPvpRelation.h"          // ★ 26.10.2026 (bowfx): отношение к цели-персонажу (PvP, дуэль, команда, свои)
#include "AsdaSkillUseSubsystemInternal.h"   // ★ R2: общие помощники и объявления частей AsdaSkillUseSubsystem*.cpp (после включений пролога)

#if ASDA_COMBAT_TRACE
// ★ Журнал боя (волна 2, 18.10.2026): общее с World/AsdaPlayerController.cpp (определения — там).
namespace AsdaCtPc
{
	extern double LastOwnSkillActS;                         // последняя активность навыком — страховке цикла в PlayerTick
}

// ★ Журнал боя (волна 2): заявка навыка ушла (или начат подход ради неё). Бой «только навыками» тоже открывает цикл: по мобу MobRid (≥ 0) — если
//   цикла нет или он по другой цели; по игроку PvpUidx (≥ 0, атакующий навык под PvP) — PvP-цикл. MobRid/PvpUidx −1 —
//   цели для цикла нет (само-каст, бафф, площадь вокруг себя): только отметка времени для страховки цикла.
static void AsdaCtSkillCycle(UGameInstance* GI, int32 SkillCode, int32 MobRid, int32 PvpUidx)
{
	AsdaCtPc::LastOwnSkillActS = FPlatformTime::Seconds();
	const UAsdaWorldSubsystem* W = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (MobRid >= 0)
	{
		if (AsdaCombatTrace::CurrentCycle() == 0 || AsdaCombatTrace::CurrentCycleTarget() != MobRid)
		{
			const AAsdaMobActor* M = W ? W->FindNetMobByRid(MobRid) : nullptr;
			const FString Why = FString::Printf(TEXT("навык %d по мобу"), SkillCode);
			ASDA_CTRACE_BEGIN(MobRid, M ? *M->MobName : TEXT("?"), *Why);
		}
	}
	else if (PvpUidx >= 0)
	{
		if (AsdaCombatTrace::CurrentCycle() == 0 || AsdaCombatTrace::CurrentCyclePlayer() != PvpUidx)
		{
			const AAsdaPlayerCharacter* P = W ? Cast<AAsdaPlayerCharacter>(W->ActorByTarget(EAsdaTargetKind::Character, PvpUidx)) : nullptr;
			const FString Why = FString::Printf(TEXT("навык %d по игроку (PvP)"), SkillCode);
			ASDA_CTRACE_BEGIN_PVP(PvpUidx, *AsdaCtPc::PlayerNick(P), *Why);
		}
		AsdaCombatTrace::NoteEngagedUser(PvpUidx);   // игрок, по которому кастуем, — «наш» ещё 30 с
	}
}
#endif
namespace
{
	// ★ Нотис площадного каста «в радиусе нет ни одной цели» — эталон
	//   `Skill_SendAreaSkillTargetsCast@0xA01140` (skill_buff.c:3188-3194): при `targetCount == 0`
	//   печатает его, переводит персонажа в состояние 0 и **заявку НЕ отправляет**.
	constexpr int32 kLocNoAreaTarget = 111502;

	// ★ Дальность ОТБРОСА цели у навыков с `.sif +0x8B == 0` — литерал 450.0 в
	//   `cameraComputeSpringTarget@0xF87F30` (render_core.c:6725 `Vector3_MulScalar(…, 450.0, dir)`).
	//   Точка «цель − 450·normalize(кастер − цель)» и есть наземные тайлы `rec[+15]/[+17]` заявки.
	constexpr float kKnockbackDist = 450.f;
}

// ══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

// ★ 26.10.2026 (bowfx, цели навыков п.9) [ОРИГИНАЛ]: флаг PvP `[160]` — по сети (`UAsdaNetSubsystem::IsSelfPvpOn`, 4206), а не по
//   своему зеркалу `bSelfPvpMode`: зеркало переживало смену карты, а эталон на ней `[160]` сбрасывает (game_main.c:3929) — как и
//   сеть (`PvpStates.Reset()`). Ключ `asda.Skill.TargetRelation 0` — прежнее зеркало.
static bool AsdaSkillPvpOn(const UGameInstance* GI, bool bLegacyMirror)
{
	return AsdaPvpRel::IsOn() ? AsdaPvpRel::GetSelf(GI).bOn : bLegacyMirror;
}

// ★★★ 26.10.2026 (bowfx, баг владельца «вражеский навык ложится на мирного») [ОРИГИНАЛ] ГЕЙТ ЦЕЛИ-ПЕРСОНАЖА ЗАЯВКИ НАВЫКА.
//   Эталон шлёт заявку по персонажу ТОЛЬКО в этих случаях (всё прочее — молча, нотиса нет; таблица — skill_targets/RULES.md):
//   · вид 2 (`combatHandleSkillUse`, combat_pvp.c:7263): `kind != 2 || [160] == 1` — вне PvP по персонажу НИЧЕГО;
//       цель — я (combat_pvp.c:7268-7273): только targetSlotType 0..3 или 5; чужой — не мёртв (6/29, :7330);
//   · вид 1 (`skillUseOnTargetDispatch`): цель — я (skill_buff.c:31826-31870): slot 0/1/4/5/2/3, иначе молча (slot 6/7);
//       чужой (:31922): `slot != 3 && (slot != 6 || [160] == 1)`;
//   · отправитель атаки по персонажу `skillSendUseSkillPacket@0xA02AB0` (skill_buff.c:3693-3709): `[160] != 1` / цель — я /
//       `[92] == 1 && цель != [80]` (дуэль, не соперник) — выход без заявки.
//   Остальное (свободный бой по своим, команда, «на игрока» по сопернику) клиент шлёт, решает сервер `sub_556DB0` (код 0 → 1017).
//   OutWhy — причина для журнала. Возврат false — заявки нет.
static bool AsdaCharTargetAllowed(const UGameInstance* GI, const FAsdaSkillDef& D, int32 TIdx, bool bSelf, bool bDead, FString& OutWhy)
{
	if (!AsdaPvpRel::IsOn()) { return true; }
	const AsdaPvpRel::FSelf Me = AsdaPvpRel::GetSelf(GI);
	const int32 Slot = D.TargetSlotType;
	const bool bAttackSender = (Slot == 6) && (D.ResourceType == 0 || D.ResourceType == 1);   // одна запись → skillSendUseSkillPacket
	if (D.Kind == 2)
	{
		if (!Me.bOn) { OutWhy = TEXT("вид 2 по персонажу вне PvP (`[160]` = 0) — эталон молчит (combat_pvp.c:7263)"); return false; }
		if (bSelf)
		{
			if (!(Slot >= 0 && (Slot <= 3 || Slot == 5))) { OutWhy = FString::Printf(TEXT("вид 2, цель — я, slot %d (не 0..3/5) — молча"), Slot); return false; }
			return true;
		}
		if (bDead) { OutWhy = TEXT("вид 2, цель-персонаж мертва (6/29) — молча"); return false; }
	}
	else
	{
		if (bSelf)
		{
			if (!(Slot == 0 || Slot == 1 || Slot == 2 || Slot == 3 || Slot == 4 || Slot == 5)) { OutWhy = FString::Printf(TEXT("вид 1, цель — я, slot %d — молча (skill_buff.c:31871)"), Slot); return false; }
			return true;
		}
		if (Slot == 3) { OutWhy = TEXT("вид 1, slot 3 по чужому — молча (skill_buff.c:31922)"); return false; }
		if (Slot == 6 && !Me.bOn) { OutWhy = TEXT("враждебный навык (slot 6) по игроку вне PvP — молча (skill_buff.c:31922)"); return false; }
	}
	if (bAttackSender && Me.Mode == 1 && TIdx != Me.Rival)
	{
		OutWhy = FString::Printf(TEXT("дуэль: цель uidx=%d не соперник (%d) — молча (skill_buff.c:3705)"), TIdx, Me.Rival);
		return false;
	}
	return true;
}

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] БАГ 7 (игрок Player55, alpha-5.0.4): «после очень активного использования навыки в какой-то
//   момент перестают работать вообще, помогает только перезапуск». Живые журналы 31.10 15:52 и 16:30: эффекты навыков
//   пропадают, автоатака идёт ещё 2–3 минуты, ни сторож «ответа нет», ни сторож ритма «навык занят дольше 6 с» (оба Warning
//   и пишутся за пределом 4 МБ) не сработали ⇒ залипло блокирующее состояние при `ActiveSkill = −1`. Единственные пути туда:
//   отказ 6159 Хранителя (`UAsdaSoulGuardSubsystem::OnSkillCast6159` → `ClearActiveSkill` снимал активный навык, а
//   состояние 25 заявки оставлял; сервер шлёт этот отказ с кастером и кодом — `RecvChar_SoulGuarderSkillUse`,
//   GameServer.c:57274, коды 5/6/13/16/17/20/21/24/27/32/33), отказ 6161 (подсистема навыков его не слушала) и навык
//   соулмейта 5231 (25 без активного навыка, ответ 5232 диспетчер не разбирал). У эталона это поле одно на персонажа, его
//   перезаписывают ход и атака, а 5232 при отказе ставит 0 (network.c:45393) — вечной защёлки нет.
//   Ключ: 1 — отказы 6159/6161 и ответ 5232 снимают состояние, плюс предохранитель `TickLatchGuard`; 0 — прежнее поведение.
// ★★★ 05.11.2026 (баг «под оглушением персонаж атакует», сессия stun-attack; док. AstoryServer/Docs/GameServer/cant-act-gate-p107.md)
//   [ОРИГИНАЛ] ГЕЙТ «ПОД КОНТРОЛЕМ» У ПУТИ «ПО ЦЕЛИ» (Kind 1, `skillUseOnTargetDispatch@0xE4E9F0`, AsdaStory_game.c:464076):
//       if ( skillId == 639 || skillId == 615 || !charHasAnyEquippedItemType(…) || skillId == 736 || skillId == 984 )
//       { if (skillId == 537 || 597) { if (listHasAnyOfSlotTypes) { нотис 1155; return; } } … }   // иначе — молча ничего
//   Исключения — навыки, снимающие контроль: 639 «Оживление», 615 «Железная воля» (лёд/сон/страх/окаменение), 736 «Сосредоточение»,
//   984 «Ледяной щит» (оглушение/страх). Сервер (P107) пропускает 736/984 и под оглушением. Боевой путь (Kind 2,
//   `combatHandleSkillUse`) — по-прежнему гейт 1155 без исключений. Порт гейтил оба пути одинаково (нотис 1155, исключений нет):
//   снять оглушение «Сосредоточением» было нельзя. 1 = как эталон (исключения, без нотиса; 537/597 — нотис 1155 и при ловушке);
//   0 = прежний порт.
// ★ 05.11.2026 [ОРИГИНАЛ] ВРАЖДЕБНЫЙ площадной навык (слот 6, resourceType 2/3) по ЦЕЛИ-ПЕРСОНАЖУ (баг «граната бьёт по себе в
//   дуэли», журнал Player52 01.11: 5270 гранаты с записью по самому кастеру). Эталон `skillUseOnTargetDispatch` (skill_buff.c, ветка
//   `case 6: switch (resourceType) case 2/3`): PvP (`[160]`) → `skillCastAreaTargetsAndSend@0xA12800` (у нас `SendUseAreaMixed`, центр —
//   цель, отбор по отношению: в дуэли только соперник), иначе `Skill_SendAreaSkillTargetsCast(свой центр, …, −1)`. Порт у навыков
//   ВИДА 1 уходил в ветку ДРУЖЕСТВЕННОЙ групповой площади (`PickPartyUserIdxsInGameBox` вокруг себя) — в заявку попадали сам кастер и
//   его группа, сервер (`RecvBattle_SkillUse` верит заявке) бил по ним. ★ 06.11 УТОЧНЕНО: ветка требует castType 3 и вид != 2 — из активных навыков
//   это только 604 и 607 (539 556 564 595 924 961 991 — castType 0, уходят в `SendUseSelfOrArea`; 77 119 156 — вид 0, не классовые).
//   1 — как эталон; 0 — прежний порт.
static ASDA_DEV_CVAR(int32, CVarAsdaSkillHostileAreaCharTarget, TEXT("asda.Skill.HostileAreaCharTarget"), 1,
	TEXT("[ОРИГИНАЛ] Враждебный площадной навык вида 1 (слот 6) по персонажу: 1 — сбор целей вокруг цели по отношению (эталон ")
	TEXT("skillCastAreaTargetsAndSend), 0 — прежний порт (групповая площадь вокруг себя: в заявку попадали сам и группа)."),
	ECVF_Default);

// Ветвление отправки — эталон `charUpdateActionStateTick` case 0x66 (`entity_char.c:24186-24260`),
//   `switch (rec[+0x36])` (в порту `ResourceType`) с уточнением по `+0x34` (`TargetSlotType`).
//   Все пять веток эталона собирают ОДИН И ТОТ ЖЕ 60-байтовый пакет 5256 и различаются только тем,
//   что кладут в hit-запись: цель, точку на земле или ничего.
// ★★★ ВЕТКА `castType == 0` — САМО-КАСТ И ПЛОЩАДЬ ВОКРУГ СЕБЯ. ЦЕЛЬ НЕ ЧИТАЕТСЯ.
//   Эталон (`combat_pvp.c:7534-7605`, хвост `combatHandleSkillUse`; тот же блок в
//   `skillUseOnTargetDispatch`, skill_buff.c:32095-32227) ветвится по `resourceType` (`.sif +0x36`):
//     case 0/1: если у персонажа висит эффект 132 → площадные отправители;
//               ИНАЧЕ (обычный случай) → `skillCastSelfSendPacket@0x9FE210`, одна запись
//               `rec[0] = 1`, `rec[1..2] = СВОЙ userNum` (skill_buff.c:2221-2222), тайлы СВОИ;
//     case 2/3: всегда площадь — `skillCastAreaTargetsAndSend@0xA12800` (если стоит флаг
//               `char[1].buffFxSlots[160]`) либо `Skill_SendAreaSkillTargetsCast@0xA01140`.
//   ⚠ Эффект 132 и флаг `buffFxSlots[160]` в порту не заведены — берём ОБЫЧНЫЕ ветки (без 132,
//   без 160), то есть `skillCastSelfSendPacket` и `Skill_SendAreaSkillTargetsCast`. Это записано
//   честно, а не подменено «как удобнее».
//   `Skill_SendAreaSkillTargetsCast` (skill_buff.c:3157-3200) собирает цели так:
//     bbox = свой тайл ± `skillRecord->maxSkillLevel[0]` (это **`.sif +0x37`**, в порту `AoeRadius`;
//     имя поля в структуре эталона — мисней), углы переводятся в мир и расширяются на ±50;
//     `objectsPickInBoundingBox` отдаёт до 20 renderId СУЩНОСТЕЙ, свой renderId исключается;
//     **если целей 0 — нотис 111502 и ВЫХОД БЕЗ ОТПРАВКИ**;
//     иначе N записей `rec[0] = 0`, `rec[1..2] = renderId` (skill_buff.c:3312-3331), тайлы СВОИ.
// ★★★ БОКС ПЛОЩАДИ — ОБЩИЙ У ВСЕХ ПЯТИ ПЛОЩАДНЫХ ОТПРАВИТЕЛЕЙ ЭТАЛОНА, дословно:
//     centerTileX = helper_scaleIfNonzero(map, centerX);      // мир → тайл (пол)
//     centerTileZ = ratioIfFieldSet(map, centerZ);
//     rectMin = centerTile − rec->maxSkillLevel[0];  rectMax = centerTile + rec->maxSkillLevel[0];
//     tileMin  = CMap_GetTileAt(rectMinX, rectMinZ); tileMax = CMap_GetTileAt(rectMaxX, rectMaxZ);
//     бокс = [tileMin − 50 … tileMax + 50] по X и по Z
//   (skill_buff.c:2903-2930, 3163-3200, 3459-3497, 4276-4310 — четыре независимых копии одного кода).
//   ⚠ Центр бокса — ЦЕНТР ТАЙЛА, а не его начало и не точная позиция персонажа: сначала floor до
//   индекса тайла, потом обратно в мир ЧЕРЕЗ `CMap_GetTileAt`, а тот отдаёт ЦЕНТР ячейки.
//   Доказательство (2026-08-11, перепроверка): `CMap_GetTileAt@0x9662F0` → `getObjectPosition@0x966340`
//   → `CObj_GetPosition@0x966370` (дизасм `add eax,0xC`) читает `tile+0x50` = float dword[20];
//   туда `gridBuildCellNeighbors@0x1132600` (`world_map.c:12567`) пишет `(v9 + v11) / 2.0` —
//   полусумму границ ячейки, то есть `index·100 + 50`. Полуразмер ячейки `+17 = (v11 − v9)/2 = 50`.
//   ⇒ эталонный бокс = `[tileMin − 50 … tileMax + 50]` = `[(cx−R)·100 … (cx+R)·100 + 100]`,
//   его центр = `cx·100 + 50`, полуразмер = `R·100 + 50` (`skill_buff.c:2916-2930`).
//   Прежний порт центрировал на НАЧАЛЕ тайла ⇒ весь бокс был сдвинут на −50 по обеим осям:
//   цель в тайле `cx+R` со смещением > 50 терялась, цель в тайле `cx−R−1` попадала ложно.
void UAsdaSkillUseSubsystem::AreaBoxFromTile(const FVector2D& CenterGamePos, int32 RadiusTiles,
	FVector2D& OutCenter, float& OutHalf) const
{
	const FIntPoint Tile = AsdaCoord::GameToTile(CenterGamePos);   // ★ Аудит №3 (B2)
	// ЦЕНТР тайла (`index·100 + 50`) — ровно то, что возвращает `CMap_GetTileAt`.
	OutCenter = AsdaCoord::TileCenterGame(Tile);   // ★ Аудит №3 (B2)
	// ⚠ ПОЛУРАЗМЕР бокса — НЕ центр тайла: та же арифметика `R·100 + 50`, но смысл другой
	//   (эталон skill_buff.c:2916-2930: границы бокса шире радиуса на полутайл с каждой стороны).
	OutHalf = (float)RadiusTiles * AsdaCoord::kTileUnits + AsdaCoord::kTileUnits * 0.5f;
}

// ★★★ ПОДМЕНА РАДИУСА ЗАПИСЬЮ НАВЫКА 132 ЖИВЁТ РОВНО В ОДНОМ ОТПРАВИТЕЛЕ (проверено 2026-08-11).
//   Полный греп `0x84u` по всем 30 файлам `RE/etalon/*.c` даёт ТРИ вхождения, из них к навыкам
//   относится ОДНО — `skill_buff.c:2897`, внутри `skillCastAreaSendPacket@0xA00570` (площадь
//   вокруг ЦЕЛИ-МОБА). Остальные два — `image_codecs.c:10304` (qmemcpy 132 б) и
//   `world_map.c:11141` (fread рыбных зон), к навыкам отношения не имеют.
//   Три других площадных отправителя берут радиус ПРЯМО из записи своего навыка, без развилки:
//     · `skillCastAreaTargetsAndSend@0xA12800`  — `skill_buff.c:4279-4284`;
//     · `Skill_SendAreaSkillTargetsCast@0xA01140` — `skill_buff.c:3158-3172`;
//     · `Skill_SendAreaSkillCast@0xA0xxxx`      — `skill_buff.c:3452-3461`.
//   ⇒ параметр `bEffect132Substitution` включён ТОЛЬКО в ветке A (площадь вокруг цели-моба).
//   Прежний порт подставлял `rad(132) = 3` во всех трёх остальных путях и занижал радиус.
int32 UAsdaSkillUseSubsystem::AreaRadiusTiles(int32 SkillCode, const FAsdaSkillDef& D,
	bool bEffect132Substitution) const
{
	// `skillCastAreaSendPacket@0xA00570` (skill_buff.c:2893-2900):
	//   if (CList_FindByTypeSub(char, 132, 0) && skillCode <= 1200) rec = GetSkillRecord(0x84);
	//   rect = originTile ± rec->maxSkillLevel[0];
	if (bEffect132Substitution && SkillCode <= 1200 && HasAreaEffect132())
	{
		if (const FAsdaSkillDef* E = GetDef(132))
		{
			return E->AoeRadius;
		}
	}
	return D.AoeRadius;
}

bool UAsdaSkillUseSubsystem::HasAreaEffect132() const
{
	const UAsdaEffectsSubsystem* Eff = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Eff) { return false; }
	for (const FAsdaEffectEntry& E : Eff->GetSelfList())
	{
		if (E.Id == 132 && E.Type == 0) { return true; }
	}
	return false;
}

// ★★★ `skillCastAreaTargetsAndSend@0xA12800` (skill_buff.c:4147-4470) — ДОСЛОВНО.
//   1. Центр области:
//      · `flags & 1` (цель-СУЩНОСТЬ) → позиция сущности; она же занимает запись №0 массива мобов;
//      · `flags & 8` и `TargetIdx >= 0` (цель-ПЕРСОНАЖ) → если это Я САМ и `slot == 6`, центр = я
//        и запись №0 НЕ занимается; иначе центр = цель, она занимает запись №0 массива персонажей;
//      · иначе → центр = я.
//   2. Бокс = центрТайл ± `.sif +0x37`, ±50 (см. `AreaBoxFromTile`).
//   3. Сущности: `entityQueryInRectExcl` (запись №0 уже занята основной целью, она исключается).
//      Персонажи: `charManager_collectTargetsInRect` — ТОЛЬКО при `buffFxSlots[160] == 1` (PvP-бой).
//   4. Ни одной цели ⇒ нотис 111502 и заявка НЕ уходит.
//   5. Порядок записей: `flags & 1` ⇒ сначала МОБЫ (`rec[0]=0`), потом ПЕРСОНАЖИ (`rec[0]=1`);
//      иначе ⇒ сначала ПЕРСОНАЖИ, потом МОБЫ.
bool UAsdaSkillUseSubsystem::SendUseAreaMixed(int32 SkillCode, const FAsdaSkillDef& D,
	int32 TargetFlags, int32 TargetIdx, int32 TargetKindSlot)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* W = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = GetLocalChar();
	if (!Net || !W || !Ch)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d (смешанная площадь): заявка НЕ ушла — нет сети/мира/персонажа"), SkillCode);
		ActiveSkill = -1; ActiveSkillSentS = 0.0; return false;
	}

	FVector2D CenterPos = Ch->GetGamePosXZ();
	int32 PrimaryMobId = -1, PrimaryCharIdx = -1;
	// ★ Аудит №3 (B6): обе ветки ищут цель ЕДИНОЙ точкой `ActorByTarget`. ⚠ ЗДЕСЬ ТРЕТЬЯ КОНВЕНЦИЯ —
	//   не байт `kind`, а БИТОВАЯ МАСКА `targetFlags` слота: эталон `skillCastAreaTargetsAndSend@0xA12800`
	//   на `targetFlags & 1` зовёт `Entity_FindByRenderId` (skill_buff.c:4238 — СУЩНОСТЬ, renderId), а на
	//   `targetFlags & 8` — `CharManager_FindByUserIdx` (skill_buff.c:4250 — ПЕРСОНАЖ, userNum). Перевод
	//   в вид делается прямо в ветке разбора этой маски, поэтому перепутать значения негде.
	if ((TargetFlags & 1) != 0 && TargetIdx >= 0)
	{
		if (const AActor* M = W->ActorByTarget(EAsdaTargetKind::Entity, TargetIdx))
		{
			CenterPos = AsdaCoord::UeToGame2D(M->GetActorLocation());   // ★ Аудит №3 (B1)
			PrimaryMobId = TargetIdx;
		}
		else
		{
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d (смешанная площадь): цель-сущность rid=%d не найдена — заявка не ушла"), SkillCode, TargetIdx);
			ActiveSkill = -1; ActiveSkillSentS = 0.0; return false;
		}   // эталон: `if (!targetEntity) return;`
	}
	else if ((TargetFlags & 8) != 0 && TargetIdx >= 0)
	{
		if (TargetIdx == SelfUserIdx() && TargetKindSlot == 6)
		{
			CenterPos = Ch->GetGamePosXZ();                                // центр — я, запись №0 свободна
		}
		else if (const AActor* P = W->ActorByTarget(EAsdaTargetKind::Character, TargetIdx))
		{
			CenterPos = AsdaCoord::UeToGame2D(P->GetActorLocation());   // ★ Аудит №3 (B1)
			PrimaryCharIdx = TargetIdx;
		}
		else
		{
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d (смешанная площадь): цель-персонаж userIdx=%d не найден — заявка не ушла"), SkillCode, TargetIdx);
			ActiveSkill = -1; ActiveSkillSentS = 0.0; return false;
		}
	}

	FVector2D BoxCenter; float Half = 0.f;
	AreaBoxFromTile(CenterPos, AreaRadiusTiles(SkillCode, D), BoxCenter, Half);

	TArray<int32> Mobs, Chars;
	if (PrimaryMobId >= 0) { Mobs.Add(PrimaryMobId); }
	{
		TArray<int32> Extra;
		W->PickEntityRenderIdsInGameBox(BoxCenter, Half, Extra, 20 - Mobs.Num(), PrimaryMobId);
		Mobs.Append(Extra);
	}
	if (AsdaSkillPvpOn(GetGameInstance(), bSelfPvpMode))
	{
		if (PrimaryCharIdx >= 0) { Chars.Add(PrimaryCharIdx); }
		TArray<int32> Extra;
		W->PickPlayerUserIdxsInGameBox(BoxCenter, Half, Extra, 20 - Chars.Num(), PrimaryCharIdx);
		Chars.Append(Extra);
	}

	if (Mobs.Num() == 0 && Chars.Num() == 0)
	{
		PostSkillNotice(kLocNoAreaTarget);
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: смешанная площадь (радиус %d тайлов) — ")
			TEXT("в боксе нет ни одной цели, эталон заявку не шлёт (нотис %d)"),
			SkillCode, *D.Name, AreaRadiusTiles(SkillCode, D), kLocNoAreaTarget);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: смешанная площадь (радиус %d тайлов) — в боксе НЕТ целей, заявка не ушла (нотис %d)"),
			SkillCode, AreaRadiusTiles(SkillCode, D), kLocNoAreaTarget);
		ActiveSkill = -1; ActiveSkillSentS = 0.0;
		return false;
	}

	TArray<UAsdaNetSubsystem::FAsdaSkillUseRec> Recs;
	auto AddMobs = [&Recs, &Mobs]() { for (int32 R : Mobs) { UAsdaNetSubsystem::FAsdaSkillUseRec X; X.Type = 0; X.Idx = R; Recs.Add(X); } };
	auto AddChars = [&Recs, &Chars]() { for (int32 U : Chars) { UAsdaNetSubsystem::FAsdaSkillUseRec X; X.Type = 1; X.Idx = U; Recs.Add(X); } };
	if ((TargetFlags & 1) != 0) { AddMobs(); AddChars(); } else { AddChars(); AddMobs(); }

	const FIntPoint CenterTile = AsdaCoord::GameToTile(CenterPos);   // ★ Аудит №3 (B2)
	const int32 TileX = CenterTile.X, TileZ = CenterTile.Y;
	StopBeforeCast(SkillCode);
	const bool bOk = Net->SendSkillUseRequest(SkillCode, TileX, TileZ, Recs, 0, 0);
	if (!bOk)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: заявка (смешанная площадь) НЕ отправлена — сеть вернула отказ"), SkillCode);
		ActiveSkill = -1; ActiveSkillSentS = 0.0; return false;
	}
#if ASDA_COMBAT_TRACE
	// Журнал боя: основная цель площади — моб (цикл по нему) или чужой игрок под PvP (PvP-цикл).
	AsdaCtSkillCycle(GetGameInstance(), SkillCode, PrimaryMobId,
		(AsdaSkillPvpOn(GetGameInstance(), bSelfPvpMode) && PrimaryCharIdx >= 0 && PrimaryCharIdx != SelfUserIdx()) ? PrimaryCharIdx : -1);
#endif
	++Sent5256;
	// ★★★ П29 (2026-08-11): эталон после КАЖДОЙ заявки ставит состояние **25**
	//   (`CharFx_AutoSabzil_State(char, id, 25, 0, 0)` — skill_buff.c:2237/2582/2783/
	//   3061/3348/3647/3874/4467, все восемь отправителей). 25 входит в `StateBlocksCast`,
	//   то есть именно оно запрещает вторую заявку до ответа. Снимается ответом 5257/5270
	//   (24/14), отказом (0) и сторожем «ответа нет» kNoAnswerGuardS.
	CharState = 25;
	LastSentSkill = SkillCode;
	ASDA_CTRACE(EAsdaCT::Send, TEXT("%s ЗАЯВКА навыка %d «%s»: СМЕШАННАЯ ПЛОЩАДЬ, флаги 0x%X, тайл центра (%d,%d), радиус %d: мобов %d, персонажей %d (PvP=%s), записей %d; состояние → 25"),
		SkillCode < 1201 ? TEXT("5256") : TEXT("6158"), SkillCode, *D.Name, TargetFlags, TileX, TileZ, AreaRadiusTiles(SkillCode, D),
		Mobs.Num(), Chars.Num(), AsdaSkillPvpOn(GetGameInstance(), bSelfPvpMode) ? TEXT("да") : TEXT("нет"), Recs.Num());
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %s ушёл: навык %d «%s» — СМЕШАННАЯ ПЛОЩАДЬ ")
		TEXT("(skillCastAreaTargetsAndSend, флаги=0x%X, вид «%s»), тайл центра (%d,%d), радиус %d тайлов: ")
		TEXT("мобов %d, персонажей %d (PvP=%s), всего записей %d"),
		SkillCode < 1201 ? TEXT("5256") : TEXT("6158"), SkillCode, *D.Name, TargetFlags,
		TargetSlotTypeName(D.TargetSlotType), TileX, TileZ, AreaRadiusTiles(SkillCode, D),
		Mobs.Num(), Chars.Num(), AsdaSkillPvpOn(GetGameInstance(), bSelfPvpMode) ? TEXT("да") : TEXT("нет"), Recs.Num());
	return true;
}

bool UAsdaSkillUseSubsystem::SendUseSelfOrArea(int32 SkillCode, const FAsdaSkillDef& D, const FVector2D& SelfGamePos)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d (само-каст/площадь): заявка НЕ ушла — нет сети"), SkillCode);
		ActiveSkill = -1; ActiveSkillSentS = 0.0; return false;
	}

	// Тайлы шапки у обеих веток — СВОИ (skill_buff.c:2060-2061 и 3141-3143).
	const FIntPoint SelfTile = AsdaCoord::GameToTile(SelfGamePos);   // ★ Аудит №3 (B2)
	const int32 TileX = SelfTile.X, TileZ = SelfTile.Y;

	// ★★★ ПРАВИЛА СЕРВЕРА ПО ВИДУ ЗАПИСИ (`RecvChar_SkillUse`, GameServer.c:46167-46250) — ими
	//   проверяется КАЖДАЯ ветка ниже:
	//     case 1 «Self»   : `if (!rec[0]) отказ 16` и `rec[1] == свой userNum`
	//     case 2/3 «Party»: `if (!rec[0]) отказ 16`, `rec[1] != −1`, одна группа
	//     case 5 «User»   : `if (!rec[0]) отказ 16`, `rec[1] != −1`
	//     case 6 «Monster»: принимает ЛЮБОЙ rec[0] при `rec[1] != −1`
	//     case 7 «Floor»  : `if (rec[0] == 1) отказ 20` ⇒ нужна именно запись сущности
	// ★★★ Флаг `char[1].buffFxSlots[160]` = «идёт PvP-бой», писатель — опкод 4206 type==1
	//   (network.c:44129); порт держит его в `bSelfPvpMode`.
	// ★★ Эффект 132 (`CList_FindByTypeSub(&char->gap_96A[128], 132, 0)`) — второй переключатель
	//   площадной ветки; в порту тот же список ведёт `UAsdaEffectsSubsystem`.
	// ★★★★★ ПЕРЕПИСАНО 2026-08-11 ПО ДВУМ ДИСПЕТЧЕРАМ ЭТАЛОНА. Прежняя версия ветвилась ТОЛЬКО по
	//   `targetSlotType` и не различала вид диспетчера — из-за этого 29 групповых баффов уходили
	//   само-кастом, а не сбором согруппников. Полные таблицы (сняты дословно):
	//
	//   ВИД 1 — `skillUseOnTargetDispatch@0xE4E9F0`, `skill_buff.c:32164-32227`:
	//     res 0/1 → `skillCastSelfSendPacket@0x9FE210`                     (1 запись: 1, свой userNum)
	//     res 2/3 → slot 0/1/4/5 → `Skill_SendAreaSkillCast(…, −1)`        (персонажи-СОГРУППНИКИ в боксе)
	//               slot 2/3     → Party_IsInParty ? `Skill_SendAreaSkillCast(…, 2)` : нотис 1223
	//               slot 6/7     → PvP ? `skillCastAreaTargetsAndSend(id, флаги, свой userNum, slot)`
	//                                  : `Skill_SendAreaSkillTargetsCast(…, −1)`   (СУЩНОСТИ вокруг себя)
	//
	//   ВИД 2 — `combatHandleSkillUse@0xE4B960`, `combat_pvp.c:7565-7605` (развилки по slot ЗДЕСЬ НЕТ!):
	//     res 0/1 → эффект132 ? (PvP ? смешанная(свой userNum) : сущности вокруг себя) : САМО-КАСТ
	//     res 2/3 → PvP ? смешанная(−1) : сущности вокруг себя
	const bool bHasEffect132 = HasAreaEffect132();
	if (bHasEffect132 && (D.ResourceType == 0 || D.ResourceType == 1))
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: висит эффект 132 — само-каст превращён в ")
			TEXT("площадь (эталон combat_pvp.c:7569)"), SkillCode, *D.Name);
	}
	// ★ ТОГЛ (`subType == 5`) уходит в САМО-КАСТ ещё ДО развилки по `resourceType`: у обоих
	//   диспетчеров это отдельная ветка `else if (skillSubType == 5) { …skillCastSelfSendPacket… }`
	//   (skill_buff.c:32045, combat_pvp.c:7486). На данных это 547 «Ловушка» (`res 2`) —
	//   без гейта он попал бы в площадную ветку.
	const bool bResArea = (D.SubType != 5)
		&& ((D.ResourceType == 2 || D.ResourceType == 3)
		 || ((D.ResourceType == 0 || D.ResourceType == 1) && bHasEffect132 && D.Kind == 2));
	// Что именно собираем: 0 = само-каст, 1 = сущности вокруг себя, 2 = согруппники, 3 = смешанная.
	enum { kSelf = 0, kEntities = 1, kParty = 2, kMixed = 3 };
	int32 Mode = kSelf;
	if (bResArea)
	{
		if (D.Kind == 2)
		{
			Mode = AsdaSkillPvpOn(GetGameInstance(), bSelfPvpMode) ? kMixed : kEntities;         // combat_pvp.c:7591-7602
		}
		else if (D.TargetSlotType == 2 || D.TargetSlotType == 3)
		{
			Mode = kParty;                                    // skill_buff.c:32192-32202
			// ★ 26.10.2026 (bowfx, цели навыков п.7-8) [ОРИГИНАЛ]: `Party_IsInParty ? Skill_SendAreaSkillCast(…, 2) : нотис 1223` —
			//   вне отряда групповой бафф не уходит вовсе. Прежде гейт был выключен «до появления подсистемы отряда»; отряд есть
			//   (`UAsdaPartySubsystem`, ростер — `UAsdaWorldSubsystem::IsInParty`). Ключ `asda.Skill.TargetRelation 0` — без гейта.
			UAsdaWorldSubsystem* WP = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			if (AsdaPvpRel::IsOn() && WP && !WP->IsInParty())
			{
				PostSkillNotice(1223);
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d «%s»: групповой (slot %d) вне отряда — нотис 1223, заявки нет"), SkillCode, *D.Name, D.TargetSlotType);
				ActiveSkill = -1; ActiveSkillSentS = 0.0;
				return false;
			}
		}
		else if (D.TargetSlotType == 6 || D.TargetSlotType == 7)
		{
			Mode = AsdaSkillPvpOn(GetGameInstance(), bSelfPvpMode) ? kMixed : kEntities;         // skill_buff.c:32203-32220
		}
		else
		{
			// slot 0/1/4/5 при `castType == 0` — `Skill_SendAreaSkillCast(…, −1)`. На данных ветка
			//   НЕДОСТИЖИМА (единственная запись с `res 2` и `slot 1` — 547, а у неё `subType 5`,
			//   т.е. ТОГЛ, и он уходит выше по `skillCastSelfSendPacket`). Ветку держим, потому что
			//   она есть в КОДЕ клиента (закон dead-code-markings-suspect). Собирает согруппников
			//   в боксе — ровно `charCollectRenderIdsInRect` + фильтр записи `Party_FindMemberByUserIdx`
			//   (skill_buff.c:3609-3618). ⚠ У эталона в этой ветке off-by-one: сборщик пишет с
			//   индекса 1, а запись №0 берётся из ещё не заполненного `targetIdList[0] == −1`.
			//   Дефект ОРИГИНАЛА, не воспроизводим (закон fix-original-defects).
			Mode = kParty;
		}
	}

	if (Mode == kMixed)
	{
		// Флаги = `char[1].gap_0[2]`; ровно перед вызовом эталон делает `*(_WORD*)v209 |= 8u`
		//   (skill_buff.c:32160, combat_pvp.c:7561), т.е. бит3 «цель-ПЕРСОНАЖ» гарантированно взведён.
		//   `targetIdx`: вид 1 → свой userNum, вид 2 → −1 (combat_pvp.c:7594).
		const int32 MixIdx = (D.Kind == 2 && (D.ResourceType == 2 || D.ResourceType == 3))
			? -1 : SelfUserIdx();
		return SendUseAreaMixed(SkillCode, D, 8, MixIdx, D.TargetSlotType);
	}

	TArray<UAsdaNetSubsystem::FAsdaSkillUseRec> Recs;
	if (Mode == kSelf)
	{
		UAsdaNetSubsystem::FAsdaSkillUseRec R;
		R.Type = 1;                       // цель-ПЕРСОНАЖ
		R.Idx  = SelfUserIdx();           // СВОЙ сессионный userNum (i16, не charId)
		Recs.Add(R);
	}
	else if (UAsdaWorldSubsystem* W = GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
	{
		FVector2D BoxCenter; float Half = 0.f;
		const int32 RadTiles = AreaRadiusTiles(SkillCode, D);
		AreaBoxFromTile(SelfGamePos, RadTiles, BoxCenter, Half);
		if (Mode == kParty)
		{
			// `charCollectPartyMembersInBox@0x9DB8E0`: кастер идёт ПЕРВОЙ записью («и на себя»),
			//   дальше — согруппники в боксе. Сервер `case 2` требует записей ПЕРСОНАЖЕЙ.
			// ⚠ ГЕЙТ `Party_IsInParty` (иначе нотис 1223) НЕ применяем, пока в порту нет подсистемы
			//   отряда: он выключил бы 29 рабочих групповых баффов целиком. Включить вместе с отрядом.
			TArray<int32> Uidxs;
			W->PickPartyUserIdxsInGameBox(BoxCenter, Half, Uidxs, 20, SelfUserIdx());
			for (int32 U : Uidxs)
			{
				UAsdaNetSubsystem::FAsdaSkillUseRec R;
				R.Type = 1;                               // цель-ПЕРСОНАЖ
				R.Idx  = U;                               // ★ i16 userNum (закон charid-vs-usernum)
				Recs.Add(R);
			}
		}
		else
		{
			// `Skill_SendAreaSkillTargetsCast@0xA01140` → `objectsPickInBoundingBox`: СУЩНОСТИ вокруг СЕБЯ.
			TArray<int32> Rids;
			W->PickEntityRenderIdsInGameBox(BoxCenter, Half, Rids, 20);
			for (int32 Rid : Rids)
			{
				UAsdaNetSubsystem::FAsdaSkillUseRec R;
				R.Type = 0;                               // цель-СУЩНОСТЬ
				R.Idx  = Rid;                             // ★ i16 renderId (закон charid-vs-usernum)
				Recs.Add(R);
			}
		}
		if (Recs.Num() == 0)
		{
			// Эталон: нотис 111502, состояние 0, заявка НЕ уходит.
			PostSkillNotice(kLocNoAreaTarget);
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: площадь (resourceType=%d, радиус %d тайлов) — ")
				TEXT("в боксе нет ни одной цели, эталон заявку не шлёт (нотис %d)"),
				SkillCode, *D.Name, D.ResourceType, RadTiles, kLocNoAreaTarget);
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: площадь %s (res=%d, радиус %d тайлов) — в боксе НЕТ целей, заявка не ушла (нотис %d)"),
				SkillCode, Mode == kParty ? TEXT("согруппников") : TEXT("сущностей вокруг себя"), D.ResourceType, RadTiles, kLocNoAreaTarget);
			ActiveSkill = -1; ActiveSkillSentS = 0.0;
			return false;
		}
	}
	const bool bArea = (Mode != kSelf);
	const bool bAreaChars = (Mode == kParty);

	// ★★★ СТОП ПЕРЕД ЗАЯВКОЙ (эталон 4008 → 5256): и само-каст тоже отбивается кодом 24, если
	//   серверный флаг движения взведён — он ОБЩИЙ для всех навыков, кроме 736 и 984.
	StopBeforeCast(SkillCode);
	const bool bOk = Net->SendSkillUseRequest(SkillCode, TileX, TileZ, Recs, 0, 0);
	if (!bOk)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: заявка (castType 0) НЕ отправлена — сеть вернула отказ"), SkillCode);
		ActiveSkill = -1; ActiveSkillSentS = 0.0; return false;
	}
#if ASDA_COMBAT_TRACE
	// Журнал боя: castType 0 цель не читает (само-каст/площадь вокруг себя) — цикл не открываем, только отметка для страховки.
	AsdaCtSkillCycle(GetGameInstance(), SkillCode, -1, -1);
#endif
	++Sent5256;
	// ★★★ П29 (2026-08-11): эталон после КАЖДОЙ заявки ставит состояние **25**
	//   (`CharFx_AutoSabzil_State(char, id, 25, 0, 0)` — skill_buff.c:2237/2582/2783/
	//   3061/3348/3647/3874/4467, все восемь отправителей). 25 входит в `StateBlocksCast`,
	//   то есть именно оно запрещает вторую заявку до ответа. Снимается ответом 5257/5270
	//   (24/14), отказом (0) и сторожем «ответа нет» kNoAnswerGuardS.
	CharState = 25;
	LastSentSkill = SkillCode;
	ASDA_CTRACE(EAsdaCT::Send, TEXT("%s ЗАЯВКА навыка %d «%s»: castType 0 (%s), свой тайл (%d,%d), записей %d (первая: тип %d idx %d); состояние → 25"),
		SkillCode < 1201 ? TEXT("5256") : TEXT("6158"), SkillCode, *D.Name,
		Mode == kSelf ? TEXT("само-каст") : (Mode == kParty ? TEXT("площадь: согруппники") : TEXT("площадь: сущности вокруг себя")),
		TileX, TileZ, Recs.Num(), Recs.Num() > 0 ? Recs[0].Type : -1, Recs.Num() > 0 ? Recs[0].Idx : -1);
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %s ушёл: навык %d «%s» — castType=0 (цель НЕ читается), ")
		TEXT("вид «%s» (targetSlotType=%d, resourceType=%d), тайл (%d,%d) [свой тайл], %s"),
		SkillCode < 1201 ? TEXT("5256") : TEXT("6158"), SkillCode, *D.Name,
		TargetSlotTypeName(D.TargetSlotType), D.TargetSlotType, D.ResourceType, TileX, TileZ,
		bArea ? *FString::Printf(TEXT("площадь: %d записей (%s), радиус %d тайлов"), Recs.Num(),
		            bAreaChars ? TEXT("тип 1 = согруппники, кастер первым")
		                       : TEXT("тип 0 = сущности вокруг себя"), AreaRadiusTiles(SkillCode, D))
		      : *FString::Printf(TEXT("само-каст: 1 запись (тип 1 = персонаж, idx %d)"), Recs[0].Idx));
	return true;
}

// ★ 31.10.2026 (баг владельца 5.0.3 «при атаке персонаж смотрит не в сторону противника», PvP, «Тройной выстрел» 701) [ОРИГИНАЛ]:
//   своя заявка навыка по цели ставит СВОЕМУ персонажу цель навыка ДО отправки — `combatHandleSkillUse@0xE4B960`:
//   сущность — `targetRenderId = CControl_GetField236(цель)` (combat_pvp.c:7144), персонаж — `= Target_GetSelectedUserIdx(цель)`
//   (:7283/:7361), вместе с маской вида (`gap_0[2]`: 1 сущность / 8 персонаж). Ответ 5257/5270 у своего с целью-персонажем цель НЕ
//   перебивает (`if targetRenderId < 0`, network.c:44730), и покадровый доворот `objAI_FaceTargetByUserIdx@0xA23350` (гейт состояний
//   5/24/14/27/13) ведёт к ней. Порт цель на заявке не ставил: в `AttackFaceTarget` оставалась прежняя цель (атака по другому игроку
//   или мобу), и навык выпускался в её сторону. 0 — прежний порт (вместе с веткой «цель не перебита» в `OnSkillCastFaceTarget`).
static ASDA_DEV_CVAR(int32, CVarAsdaSkillFaceTargetEtalon, TEXT("asda.Skill.FaceTargetEtalon"), 1,
	TEXT("1 = как оригинал: своя заявка навыка ставит персонажу цель навыка (доворот к ней), ответ без смены цели доворачивает к уже заданной; 0 = прежний порт"),
	ECVF_Default);
bool AsdaSkillFaceTargetEtalonOn() { return CVarAsdaSkillFaceTargetEtalon.GetValueOnGameThread() != 0; }

bool UAsdaSkillUseSubsystem::SendUse(int32 SkillCode, const FAsdaSkillDef& D)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = GetLocalChar();
	if (!Net || !Ch)
	{
		UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] %d: нет сети/персонажа — 5256 не отправлен"), SkillCode);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: заявка НЕ ушла — нет сети/персонажа"), SkillCode);
		ActiveSkill = -1; ActiveSkillSentS = 0.0;
		return false;
	}
	const FVector2D G = Ch->GetGamePosXZ();

	int32 TType = 0, TIdx = -1; FVector2D TPos = G; float TDist = 0.f;
	const bool bHasTarget = GetSelectedTarget(TType, TIdx, TPos, TDist);

	// ═══════════════════════════════════════════════════════════════════════════════════════════
	// ★★★ ВЕРХНЯЯ РАЗВИЛКА ЭТАЛОНА ИДЁТ ПО `castType` (`.sif +0x33`), А НЕ ПО ЦЕЛИ.
	//   `skillUseOnTargetDispatch@0xE4E9F0` (skill_buff.c:31643) и `combatHandleSkillUse@0xE4B960`
	//   (combat_pvp.c:7096) построены одинаково:
	//       v189 = pSkillRecord->castType[0];
	//       if (v189) { if (v189 > 1 && v189 <= 3) { …ветки ПО ЦЕЛИ… } }        // 2 и 3
	//       else if (skillSubType == 5) { …тогл: skillCastSelfSendPacket… }      // 0
	//       else if (state != 24)       { …САМО-КАСТ / ПЛОЩАДЬ: цель НЕ читается… }
	//   ⇒ при `castType == 0` выбранная цель НЕ ЧИТАЕТСЯ ВООБЩЕ. Прежний порт всё равно лез в неё
	//   и на 9 навыках «AoE вокруг себя» (516 539 556 564 595 911 917 961 991) слал запись
	//   `(тип 0, idx −1)` при отсутствии цели — сервер отвечал отказом 20 «Выбери цель»
	//   (`RecvChar_SkillUse` case 6, GameServer.c:46224). Это и был «навык вообще не применяется».
	//   Заодно гейт дальности (он живёт ТОЛЬКО в ветке цели-моба, combat_pvp.c:7161-7175) больше
	//   не глушит само-касты при выбранной ДАЛЁКОЙ цели.
	if (D.CastType == 0)
	{
		return SendUseSelfOrArea(SkillCode, D, G);
	}
	// ★ Ветки «по цели» (castType 2 и 3) существуют ТОЛЬКО при выбранной цели: у эталона обе висят
	//   на `g_nInputControlState` (1 = моб, 0 = персонаж), и при отсутствии цели ни одна не
	//   срабатывает — заявка НЕ уходит. Порт раньше слал `(0, −1)` и получал отказ 20.
	if (!bHasTarget)
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: castType=%d (по цели), а цель не выбрана — ")
			TEXT("эталон в этом случае заявку НЕ шлёт (ни ветка моба, ни ветка персонажа не срабатывает)"),
			SkillCode, *D.Name, D.CastType);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d «%s»: castType=%d (по цели), а цель НЕ выбрана — заявка не ушла"), SkillCode, *D.Name, D.CastType);
		ActiveSkill = -1; ActiveSkillSentS = 0.0;
		return false;
	}
	// ★ `castType == 2` закрывает ветку МОБА явным условием эталона `if (castType != 2)`
	//   (skill_buff.c:31655, combat_pvp.c:7106). Это ровно 41 навык «только на игрока».
	if (D.CastType == 2 && TType == 0)
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: castType=2 — по МОБУ эталон не применяет ")
			TEXT("(условие `if (castType != 2)` закрывает ветку моба). Нужна цель-ПЕРСОНАЖ."),
			SkillCode, *D.Name);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d «%s»: castType=2 (только на игрока), а цель — МОБ rid=%d — заявка не ушла"), SkillCode, *D.Name, TIdx);
		ActiveSkill = -1; ActiveSkillSentS = 0.0;
		return false;
	}
	// ★★★ 26.10.2026 (bowfx) [ОРИГИНАЛ] ГЕЙТ ЦЕЛИ-ПЕРСОНАЖА — разбор у `AsdaCharTargetAllowed`. Стоит ДО подхода: эталон по
	//   недопустимой цели не бежит и не шлёт ничего (порт бежал к мирному игроку и получал отказ 1017).
	if (TType == 1)
	{
		const bool bSelfT = (TIdx == SelfUserIdx());
		bool bDeadT = false;
		if (UAsdaWorldSubsystem* WG = GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
		{
			if (const AAsdaPlayerCharacter* PT = Cast<AAsdaPlayerCharacter>(WG->ActorByTarget(EAsdaTargetKind::Character, TIdx))) { bDeadT = PT->IsIncapacitated(); }
		}
		FString Why;
		if (!AsdaCharTargetAllowed(GetGameInstance(), D, TIdx, bSelfT, bDeadT, Why))
		{
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s» по игроку uidx=%d: заявки нет — %s"), SkillCode, *D.Name, TIdx, *Why);
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d «%s» по игроку uidx=%d НЕ применён: %s"), SkillCode, *D.Name, TIdx, *Why);
			CancelSkillApproach(TEXT("цель-персонаж недопустима"));
			ActiveSkill = -1; ActiveSkillSentS = 0.0;
			return false;
		}
	}

	// ★★★ ТАЙЛЫ В ШАПКЕ — ПОЗИЦИЯ ЦЕЛИ, А НЕ СВОЯ (исправлено 2026-08-10 по живому логу).
	//   Эталон: `Skill_SendSkillUse@0x9FED00` (skill_buff.c:2366/2382) и
	//   `skillSendUseSkillPacket@0xA02AB0` (skill_buff.c:3723-3725) считают тайлы от позиции ЦЕЛИ
	//   (`CClass_SetField177(targetEntity)` / `CObject_GetSubField47649(targetChar)`) и кладут их в
	//   `head+3` / `head+5`. Свою позицию туда пишет ТОЛЬКО `skillCastSelfSendPacket@0x9FE210`
	//   (skill_buff.c:2060-2061) — ветка «цели нет». Сервер разбирает этот dword как
	//   `sub_55FF20(userMng, a2, tiles)` и ПЕРЕСЫЛАЕТ его боевому процессу в теле 8205
	//   (`GameServer.c:46152 Src[1] = *(_DWORD*)((char*)&a4[1]+1)`) — для него это точка применения.
	//   Тайлы цели берём, только когда заявка ВООБЩЕ адресует цель (см. выбор записи ниже);
	//   у само-каста эталон кладёт свою позицию.
	//   ★ Тайлы цели берут ВСЕ адресные отправители, включая «цель-персонаж»
	//   (`Skill_SendSkillUseOnTarget@0x9FFB10` skill_buff.c:2645-2652 и
	//    `skillSendUseSkillPacket@0xA02AB0` skill_buff.c:3718-3726 берут позицию из
	//    `CharManager_FindByUserIdx(targetUserIdx)`). Свою позицию кладёт только само-каст
	//   `skillCastSelfSendPacket@0x9FE210` (skill_buff.c:2060-2061), а навык вида «только на себя»
	//   (`targetSlotType == 1`) — всегда само-каст, чем бы ни была выбранная цель.
	const bool bTargetIsChar = bHasTarget && TType == 1;   // цель — ПЕРСОНАЖ (свой/чужой игрок)
	const bool bTargetIsMob  = bHasTarget && TType == 0;   // цель — МОБ (renderId)
	const bool bAddressesTarget = bHasTarget && D.TargetSlotType != 1
		&& (bTargetIsMob ? (D.TargetSlotType == 6 || D.TargetSlotType == 7 || D.TargetSlotType == 0)
		                 : true);
	const FVector2D HeadPos = bAddressesTarget ? TPos : G;
	const FIntPoint HeadTile = AsdaCoord::GameToTile(HeadPos);   // ★ Аудит №3 (B2)
	const int32 TileX = HeadTile.X, TileZ = HeadTile.Y;

	// ★ ДАЛЬНОСТЬ (эталон `entity_char.c:24263`): rangeSq = ((abilityBonus + rec[0x35]) · 100)².
	//   Бонус способностей (`skillCalcAbilityBonus@0xA870B0`) в порту не заведён — берём голое
	//   `RangeMeters` из записи, как это делает приём чужого каста (`network.c:44936`).
	//   ★★★ ПОПРАВКА 2026-08-10 (требование юзера + разбор эталона entity_char.c:24845-24878):
	//   вне дальности эталон НЕ «деградирует в обычную атаку насовсем» — он держит состояние 102 и
	//   шлёт ПОДХОД (`Combat_SendNormalAttack_thunk` = ход 4006 с renderId цели), а по прибытии в
	//   зону действия останавливается (4008) и отправляет ИМЕННО НАВЫК. Порт делает то же самое
	//   через `BeginSkillApproach` + `TickSkillApproach` (маршрут — общий, `StartRoutedMove`).
	// ★★★ П13 (2026-08-11): УБРАН ГЕЙТ `D.RangeMeters > 0`. У эталона его НЕТ: при `rec[0x35] == 0`
	//   и нулевом бонусе способностей `rangeSq` просто равен 0, и ЛЮБАЯ `dist² > 0` уводит в подход
	//   (`skill_buff.c:31740`, `entity_char.c:24864` — сравнение безусловное). Порт же полностью
	//   отключал проверку дальности у всех навыков с `+0x35 == 0` — под это попадал и **911
	//   Fire Storm**: заявка уходила с любого расстояния и глохла отказом сервера.
	if (bHasTarget && bOutOfRangeFallsBackToAttack)
	{
		const float RangeU = SkillRangeUnits(D);
		ASDA_CTRACE(EAsdaCT::Range, TEXT("навык %d: дальность до цели %s idx=%d — %.0f %s %.0f юн (%d м)%s"),
			SkillCode, TType == 0 ? TEXT("моб") : TEXT("игрок"), TIdx, TDist, TDist > RangeU ? TEXT(">") : TEXT("≤"), RangeU, D.RangeMeters,
			TDist > RangeU ? TEXT(" — ВНЕ дальности, подход (102)") : TEXT(" — в зоне, заявка"));
		if (TDist > RangeU)
		{
#if ASDA_COMBAT_TRACE
			// Журнал боя: подход ради атакующего навыка — уже начало боя (цикл открыт до бега, бег и заявка — внутри него).
			//   Моб: запись по нему уйдёт типом 0 (вид навыка не 1/2/3/5 — см. switch ниже). Игрок: вид 6 под PvP, не я.
			AsdaCtSkillCycle(GetGameInstance(), SkillCode,
				(TType == 0 && D.TargetSlotType != 1 && D.TargetSlotType != 2 && D.TargetSlotType != 3 && D.TargetSlotType != 5) ? TIdx : -1,
				(TType == 1 && D.TargetSlotType == 6 && AsdaSkillPvpOn(GetGameInstance(), bSelfPvpMode) && TIdx != SelfUserIdx()) ? TIdx : -1);
#endif
			BeginSkillApproach(SkillCode, RangeU, TType, TIdx);
			return false;
		}
	}

	// ★★★★★ НАЗЕМНЫЕ ТАЙЛЫ `rec[+15]/[+17]` — ЭТО ТОЧКА ПРИЗЕМЛЕНИЯ ОТБРОСА, А НЕ ЗОНА ПОРАЖЕНИЯ.
	//   Разобрано заново 2026-08-11 (§25 отчёта). Поле `.sif +0x8B` — НЕ «наземный площадной навык»,
	//   а ТИП РЕАКЦИИ ЦЕЛИ НА УДАР (отбрасывание). Доказательство — три места эталона, где по нему
	//   идёт `switch`, и во всех трёх результат применяется К ЦЕЛИ, а не к сбору целей:
	//     • `charApplySkillImpact` (skill_buff.c:4654) — case 0: `cameraComputeSpringTarget(цель)` →
	//       `GroundTarget_PickOnMesh` → `charSetMoveTarget(цель, …, 200.0f)` + состояние 14;
	//       case 1/2: то же БЕЗ пика, скорость 100.0f; case 3: флаг +18800; case 4: только разворот;
	//     • `skillSetupTargetPos@0xA29C00` (skill_buff.c:4779) — тот же switch, те же состояния;
	//     • `effects_particles.c:6214` / `skill_buff.c:11887,12381` — `behaviorType != 4` гейтит
	//       эффект удара, т.е. 4 = «цель не реагирует».
	//   ⇒ `cameraComputeSpringTarget@0xF87F30` (render_core.c:6695) считает НЕ «точку под целью»:
	//       dir = normalize(позЛокальногоИгрока − позЦели);   P = позЦели − 450·dir;
	//       terrainGetCellByPos(P)                       // зажим XZ в [100..51100] + высота из .TER
	//     то есть точку в 450 игровых единицах ЗА целью по лучу «кастер → цель» — куда цель улетит.
	//   Прежний порт клал сюда тайл САМОЙ цели — это давало `земля == тайл цели` (живой лог 08:00:17
	//   `тайл=(86,123) … земля=(86,123)`), тогда как эталон отстоит на 450 ед = 4.5 тайла.
	//   Пишем тайлы ТОЛЬКО при удачном пике (`GroundTarget_PickOnMesh_thunk`, наш аналог —
	//   `UAsdaWalkGrid::IsWorldWalkable`, см. `warmessage_win.md` §176); неудача ⇒ нули, которые уже
	//   лежат в записи после `SkillPacketBody_Init`.
	//   Навыков с `+0x8B == 0` двенадцать: 502/506/515/544/547 + 1231-1233 и 1237-1239.
	int32 GndX = 0, GndZ = 0;
	if (D.BehaviorType == 0 && bTargetIsMob)
	{
		// Луч «цель → кастер»; вырожденный случай (цель ровно под нами) — пика нет, тайлы нули.
		const FVector2D ToCaster = G - TPos;
		if (!ToCaster.IsNearlyZero())
		{
			const FVector2D Dir = ToCaster.GetSafeNormal();
			FVector2D P = TPos - Dir * kKnockbackDist;
			// `terrainGetCellByPos_A840@0x95A840`: x<0 → 100, x>51100 → 51100 (то же по Z).
			if (P.X <    0.f) { P.X =   100.f; }  if (P.X > 51100.f) { P.X = 51100.f; }
			if (P.Y <    0.f) { P.Y =   100.f; }  if (P.Y > 51100.f) { P.Y = 51100.f; }
			// Наш аналог `GroundTarget_PickOnMesh_thunk` — сетка проходимости карты (как в
			//   `AsdaPlayerController.cpp:555` для клика по земле). Сетки нет ⇒ fail-open.
			if (!Ch->WalkGrid || Ch->WalkGrid->IsWorldWalkable(P.X, P.Y))
			{
				const FIntPoint GndTile = AsdaCoord::GameToTile(P);   // ★ Аудит №3 (B2)
				GndX = GndTile.X;
				GndZ = GndTile.Y;
			}
		}
	}
	// ★★★★★ ГЛАВНАЯ ПРАВКА 2026-08-10 — ТИП ЦЕЛИ В HIT-ЗАПИСИ. Раньше сюда безусловно писалась 1,
	//   и ЭТО И БЫЛ РАЗРЫВ: байт 0 записи — «цель ИГРОК (1) / цель МОБ (0)», и сервер с боевым
	//   процессом искали ИГРОКА с сессионным индексом, равным renderId моба.
	//   Доказательства:
	//     • `Skill_SendSkillUse@0x9FED00` (ветка «цель — сущность по renderId», skill_buff.c:2431-2432):
	//         `skillPacketBody[0] = 0;  *(_WORD *)&skillPacketBody[1] = targetRenderId;`
	//     • `skillCastAreaSendPacket@0xA00570` (та же ветка «по площади», skill_buff.c:3037-3045):
	//         `v65[0] = 0;  *(_WORD*)&v65[1] = targetRenderIds[i];` — и так на каждую цель.
	//     • 1 пишут ТОЛЬКО те два отправителя, где цель — ПЕРСОНАЖ:
	//         `skillCastSelfSendPacket@0x9FE210` (само-каст, индекс = СВОЙ userIdx) и
	//         `skillSendUseSkillPacket@0xA02AB0` (цель-игрок, `CharManager_FindByUserIdx`).
	//     • Серверный `RecvChar_SkillUse@0x46F900` (GameServer.c:45917):
	//         `if (rec[0] == 1 && a2 != rec[1] && sub_5328F0(rec[1])) { PvP-проверка sub_556DB0 … }`
	//       — т.е. при rec[0]==1 индекс трактуется как СЕССИОННЫЙ ИНДЕКС ИГРОКА.
	//     • Живой лог 2026-08-09 21:02: наш `цель(тип=1, idx=3)` по мобу rid=3 «Степной волк» →
	//       ответ `5257 body=[00 FF FF FF FF 00 FF FF]` = код 0, а поля userIdx/skillId ещё −1,
	//       т.е. отказ сработал ДО `v25 = a2; v29 = a4[0]` — ровно в этой PvP-ветке. Все прочие
	//       заявки (idx=12, живого игрока с таким индексом нет) уходили в боевой процесс с
	//       «целью-игроком №12» и тонули там БЕЗ ответа — отсюда «полосы нет, урона нет».
	//   ⚠ `charid-vs-usernum`: индекс i16 в обоих случаях — renderId моба ЛИБО сессионный userIdx
	//     игрока; charId (i32) сюда не кладётся никогда.
	//   ★ ЧЕМ ВЫБИРАЕМ ЗАПИСЬ. Эталон выбирает отправителя по НЕВОСПРОИЗВЕДЁННОМУ флагу персонажа
	//     (`char[52120] & 1` — «бой с сущностью по renderId» vs «цель-персонаж»), которого в порту
	//     нет. Поэтому берём поле, которое РОВНО ЭТО И ВАЛИДИРУЕТ на сервере — `targetSlotType`
	//     (`.sif +0x34`, серверное `SkillInfo[209·id + 52]`), по нему идёт
	//     `switch` в `RecvChar_SkillUse` (GameServer.c:46167-46250):
	//       1 «Self»    : rec[0] != 0 И rec[1] == СВОЙ userIdx, иначе 16 / 20
	//       2,3 «Party» : rec[0] != 0, rec[1] != −1, та же группа, иначе 16 / 20 / 0
	//       5 «User»    : rec[0] != 0, rec[1] != −1, иначе 16 / 20
	//       6 «Monster» : rec[1] != −1 при любом rec[0], иначе 20
	//       7 «Floor»   : rec[0] == 1 → СРАЗУ код 20; нужен rec[0] != 1 и rec[1] != −1
	//       0 и прочее  : сервер не проверяет (default → LABEL_156)
	//     Это и точнее, и безопаснее слепого «есть моб ⇒ шлём как по мобу»: само-бафф с выбранным
	//     мобом ушёл бы записью «моб» и получил код 16.
	//   ★★★ ЧЕТЫРЕ ВИДА НАВЫКОВ — это ровно `targetSlotType` (`.sif +0x34`). Проверено на реальных
	//     байтах `skill_info.sif` (1255×273): у навыков классов (grade 1..9, подтип ≠ 3/4) поле
	//     принимает ТОЛЬКО значения 1 / 2 / 5 / 6 (плюс 0 — и все 12 таких записей имеют подтип 3,
	//     то есть ПАССИВЫ, которые не применяются вовсе). Значений 3, 4, 7 в данных нет вообще.
	//     ★ ЦИФРЫ ПЕРЕСЧИТАНЫ 2026-08-10 по байтам файла (фильтр: `skillGrade` +0xBD в 1..9 —
	//       ровно 422 записи; прежние 115/42 давали сумму 419 и были неверны):
	//       6 → «на моба ИЛИ игрока» (атака)          117 записей
	//       5 → «только на игрока»                     43
	//       1 → «только на себя»                      201
	//       2 → «на игрока и на себя» (группа)         49
	//       0 → пассивы                                12      (итого 422)
	//     Сервер валидирует ровно так же (`RecvChar_SkillUse` GameServer.c:46167-46250):
	//       1 «Self»    : rec[0] != 0 И rec[1] == СВОЙ userNum          иначе 16 / 20
	//       2,3 «Party» : rec[0] != 0, rec[1] != −1, ОДНА группа        иначе 16 / 20 / 0
	//                     (сам кастер группе принадлежит ⇒ «и на себя»)
	//       5 «User»    : rec[0] != 0, rec[1] != −1                     иначе 16 / 20
	//       6 «Monster» : rec[1] != −1 при ЛЮБОМ rec[0] ⇒ и моб, и игрок
	//       7 «Floor»   : rec[0] != 1 и rec[1] != −1                    иначе 20
	//       0 и прочее  : не проверяет (default → LABEL_156)
	//   ★ «Только на себя применяется на себя ДАЖЕ при выбранной цели» — это не наше допущение,
	//     а структура эталона: верхняя развилка `skillUseOnTargetDispatch@0xE4E9F0`
	//     (skill_buff.c:31643) идёт по `castType` (`.sif +0x33`), и при `castType == 0` управление
	//     уходит в ветку `skill_buff.c:32095`, которая ЦЕЛЬ НЕ ЧИТАЕТ ВООБЩЕ и зовёт
	//     `skillCastSelfSendPacket` (свой userNum).
	//   ★ СВЯЗЬ castType ↔ targetSlotType — НЕ взаимно однозначная. Прежний комментарий утверждал
	//     «castType 0 ⇔ slot 1/2, castType 2 ⇔ 5, castType 3 ⇔ 6, 0 исключений» — это ЛОЖЬ.
	//     Кросс-таб по тем же 422 записям (grade 1..9), пересчитан 2026-08-10 по байтам:
	//       (castType 0, slot 0) = 12   (0, 1) = 201  (0, 2) = 48
	//       (castType 0, slot 5) =  2   ← 1029, 1040
	//       (castType 0, slot 6) = 18   ← 516 529 530 539 556 564 571 577 579 595 745 809 911 917
	//                                      923 924 961 991 — это AoE ВОКРУГ СЕБЯ («Огненный шторм»,
	//                                      «Песчаная ловушка», «Зона скорби», «Волчья Яма»…)
	//       (castType 2, slot 5) = 41   (castType 3, slot 2) = 1  ← 925 «Жертвенный договор»
	//       (castType 3, slot 6) = 99
	//     Тот же срез с фильтром «подтип ∉ {3,4}» (307 записей): (0,1)=110, (0,2)=47, (0,5)=1,
	//     (0,6)=9, (2,5)=41, (3,6)=99 — в отчёте раньше стояли 101/40/97, они не воспроизводятся.
	//     ⚠ НА ВЫБОР ВЕТКИ НИЖЕ ЭТО НЕ ВЛИЯЕТ: развилка идёт по `targetSlotType`, потому что именно
	//     его валидирует сервер, а `castType`-развилку эталона (флаг `char[52120] & 1`) порт не
	//     воспроизводит. Для 18 «AoE вокруг себя» это означает: эталон послал бы САМО-каст, порт
	//     шлёт запись с целью — сервер при slot 6 принимает и то, и другое (`rec[1] != −1`).
	int32 SendType = 1;
	int32 SendIdx  = SelfUserIdx();          // эталон `skillCastSelfSendPacket`: свой userNum
	switch (D.TargetSlotType)
	{
	case 1:                                  // ВИД 3 «только на себя» — цель игнорируется целиком
		SendType = 1; SendIdx = SelfUserIdx();
		break;
	case 2: case 3:                          // ВИД 4 «на игрока и на себя» (группа)
	case 5:                                  // ВИД 2 «только на игрока»
		// Эталон `Skill_SendSkillUseOnTarget@0x9FFB10` (skill_buff.c:2767-2768): байт 0 = 1,
		//   индекс = userNum ЦЕЛИ-ПЕРСОНАЖА. Нет персонажа под курсором — само-каст (свой userNum),
		//   что для «Party»/«User» серверу тоже валидно.
		SendType = 1;
		SendIdx  = bTargetIsChar ? TIdx : SelfUserIdx();
		break;
	case 6:                                  // ВИД 1 «на моба ИЛИ игрока» (атака)
		if (bTargetIsChar)
		{
			// цель-ИГРОК → `skillSendUseSkillPacket@0xA02AB0` (skill_buff.c:3857-3858): байт 0 = 1.
			SendType = 1; SendIdx = TIdx;
		}
		else
		{
			// цель-МОБ → `Skill_SendSkillUse@0x9FED00` (skill_buff.c:2433-2434): байт 0 = 0, renderId.
			SendType = 0; SendIdx = bTargetIsMob ? TIdx : -1;
		}
		break;
	case 7:                                  // Floor — байт 0 обязан быть НЕ 1.
		// ★ ВЕТКА НЕДОСТИЖИМА НА ДАННЫХ, и это проверено, а не предположено: значения 7 в поле
		//   `+0x34` нет НИ В ОДНОЙ из 1255 записей ОБОИХ `skill_info.sif` (порта и эталонного
		//   клиента) — гистограмма поля по всем записям порта {0:791, 1:210, 2:54, 5:43, 6:157}.
		//   Убирать её нельзя: сравнение с 7 есть в КОДЕ клиента (`skillrecord_fields.tsv`,
		//   «сравн. ==7») и в серверном `switch` (`GameServer.c:46167-46250`), т.е. вид навыка
		//   существует в протоколе — просто ни один навык его не использует
		//   (закон dead-code-markings-suspect: «данных нет» ≠ «кода нет»).
		SendType = 0;
		SendIdx  = bTargetIsMob ? TIdx : -1;
		break;
	default:                                 // 0 и прочие — сервер запись не проверяет
		SendType = bTargetIsMob ? 0 : 1;
		SendIdx  = bHasTarget ? TIdx : SelfUserIdx();
		break;
	}

	// ═══════════════════════════════════════════════════════════════════════════════════════════
	// ★★★★★ ПЛОЩАДЬ ПО ВЫБРАННОЙ ЦЕЛИ (добавлено 2026-08-11). ЭТО И БЫЛА ГЛАВНАЯ ДЫРА: порт слал
	//   ОДНУ запись даже там, где эталон собирает до 20. Таблицы сняты дословно:
	//
	//   ЦЕЛЬ-МОБ (`g_nInputControlState == 1`):
	//     вид 1 `skill_buff.c:31756-31780`:  res 0/1 + slot 6/7 → `Skill_SendSkillUse` (ОДНА запись)
	//                                        res 2/3 + slot 6/7 → `skillCastAreaSendPacket` ★ ПЛОЩАДЬ
	//     вид 2 `combat_pvp.c:7184-7241`:    slot < 4 → заявки нет вовсе;
	//                                        res 0/1 + slot 4..7 → эффект132 ? ПЛОЩАДЬ : одна запись;
	//                                        res 2/3 + slot 4..7 → `skillCastAreaSendPacket` ★ ПЛОЩАДЬ
	//
	//   `skillCastAreaSendPacket@0xA00570` (skill_buff.c:2876-3060), дословно:
	//     · ЦЕНТР ОБЛАСТИ — ПОЗИЦИЯ ЦЕЛИ (`CClass_SetField177(targetEntity)`), не своя;
	//     · радиус = `.sif +0x37` цели-навыка; при висящем эффекте 132 и коде ≤ 1200 радиус берётся
	//       ИЗ ЗАПИСИ НАВЫКА 132 (см. `AreaRadiusTiles`);
	//     · `targetRenderIds[0] = targetRenderId` — ОСНОВНАЯ ЦЕЛЬ ИДЁТ ПЕРВОЙ ЗАПИСЬЮ;
	//     · `entityQueryInRectExcl` добирает остальные сущности бокса, исключая основную; потолок 20;
	//     · записи `rec[0] = 0`, `rec[1..2] = renderId`; НАЗЕМНЫЕ ТАЙЛЫ ветка НЕ ПИШЕТ
	//       (skill_buff.c:3036-3046 заполняет только байт 0 и индекс) — поэтому `0, 0` ниже.
	//     · тайлы шапки = тайлы ЦЕЛИ (они уже посчитаны выше как `TileX/TileZ` через bAddressesTarget).
	//
	//   ЦЕЛЬ-ПЕРСОНАЖ (`g_nInputControlState == 0`):
	//     вид 2 `combat_pvp.c:7263-7470` (гейт: `kind != 2 || PvP`):
	//        res 0/1 → эффект132 ? `skillCastAreaTargetsAndSend(id, 8, targetIdx, slot)` : одна запись
	//        res 2/3 → `skillCastAreaTargetsAndSend(id, 8, targetIdx, slot)`  ★ СМЕШАННАЯ ПЛОЩАДЬ
	//     вид 1 `skill_buff.c:31828-31870`:
	//        slot 0/1/4/5: res 0/1 → одна запись; res 2/3 → `Skill_SendAreaSkillCast(targetUserNum)`
	//        slot 2/3    : `Party_IsInParty ? Skill_SendAreaSkillCast(…, 2) : нотис 1223`
	//        ⚠ Обе площадные ветки вида 1 по ПЕРСОНАЖУ на данных НЕДОСТИЖИМЫ: среди 280 активных
	//          классовых навыков нет ни одного с `castType` 2/3, `slot` ∈ {0,1,2,3,4,5} и `res` 2/3
	//          (гистограмма res×slot: (0,1)=99 (0,5)=42 (0,6)=21 (1,1)=1 (1,6)=55 (2,1)=1 (2,2)=29
	//          (2,6)=12 (3,6)=20 — площадь всегда идёт со slot 6, кроме групповых баффов castType 0).
	//          Ветки держим (закон dead-code-markings-suspect) и уводим в тот же сбор согруппников.
	{
		const bool bEff132 = HasAreaEffect132();
		const bool bResArea2 = (D.ResourceType == 2 || D.ResourceType == 3);
		const bool bArea132 = (D.ResourceType == 0 || D.ResourceType == 1) && bEff132 && D.Kind == 2;

		// ── A. ПЛОЩАДЬ ВОКРУГ ЦЕЛИ-МОБА ────────────────────────────────────────────────────────
		const bool bSlotOkMob = (D.Kind == 2) ? (D.TargetSlotType >= 4 && D.TargetSlotType <= 7)
		                                      : (D.TargetSlotType == 6 || D.TargetSlotType == 7);
		if (bTargetIsMob && SendType == 0 && SendIdx >= 0 && bSlotOkMob && (bResArea2 || bArea132))
		{
			UAsdaWorldSubsystem* W = GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
			if (W)
			{
				// ★ Единственная ветка, где эталон подменяет радиус записью навыка 132.
				const int32 RadTiles = AreaRadiusTiles(SkillCode, D, /*bEffect132Substitution=*/true);
				FVector2D BoxCenter; float Half = 0.f;
				AreaBoxFromTile(TPos, RadTiles, BoxCenter, Half);   // ★ центр — ЦЕЛЬ

				TArray<UAsdaNetSubsystem::FAsdaSkillUseRec> Recs;
				{
					UAsdaNetSubsystem::FAsdaSkillUseRec R0;
					R0.Type = 0; R0.Idx = SendIdx;                  // основная цель — запись №0
					Recs.Add(R0);
				}
				TArray<int32> Rids;
				W->PickEntityRenderIdsInGameBox(BoxCenter, Half, Rids, 20 - Recs.Num(), SendIdx);
				for (int32 Rid : Rids)
				{
					UAsdaNetSubsystem::FAsdaSkillUseRec R;
					R.Type = 0; R.Idx = Rid;
					Recs.Add(R);
				}
				StopBeforeCast(SkillCode);
				// ★ Наземные тайлы этой ветке не положены — эталон их здесь не пишет.
				const bool bAreaOk = Net->SendSkillUseRequest(SkillCode, TileX, TileZ, Recs, 0, 0);
				if (!bAreaOk)
				{
					ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: заявка (площадь вокруг моба) НЕ отправлена — сеть вернула отказ"), SkillCode);
					ActiveSkill = -1; ActiveSkillSentS = 0.0; return false;
				}
				if (AsdaSkillApproachOverAttackOn() && ApproachSkill >= 0)   // ★ заявка ушла — отложенное действие 102 исчерпано (шапка у ключа)
				{
					CancelSkillApproach(TEXT("заявка навыка ушла — отложенное действие одно (эталон)"));
				}
#if ASDA_COMBAT_TRACE
				AsdaCtSkillCycle(GetGameInstance(), SkillCode, SendIdx, -1);   // журнал боя: цикл по основной цели-мобу
#endif
				ASDA_CTRACE(EAsdaCT::Send, TEXT("%s ЗАЯВКА навыка %d «%s»: ПЛОЩАДЬ ВОКРУГ МОБА rid=%d, тайл цели (%d,%d), радиус %d: записей %d (основная + %d в боксе)%s; состояние → 25"),
					SkillCode < 1201 ? TEXT("5256") : TEXT("6158"), SkillCode, *D.Name, SendIdx, TileX, TileZ, RadTiles, Recs.Num(), Recs.Num() - 1,
					bArea132 ? TEXT(", включена эффектом 132") : TEXT(""));
				++Sent5256;
				// ★★★ П29 (2026-08-11): эталон после КАЖДОЙ заявки ставит состояние **25**
				//   (`CharFx_AutoSabzil_State(char, id, 25, 0, 0)` — skill_buff.c:2237/2582/2783/
				//   3061/3348/3647/3874/4467, все восемь отправителей). 25 входит в `StateBlocksCast`,
				//   то есть именно оно запрещает вторую заявку до ответа. Снимается ответом 5257/5270
				//   (24/14), отказом (0) и сторожем «ответа нет» kNoAnswerGuardS.
				CharState = 25;
				LastSentSkill = SkillCode;
				UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %s ушёл: навык %d «%s» — ПЛОЩАДЬ ВОКРУГ ЦЕЛИ ")
					TEXT("(skillCastAreaSendPacket, вид %d, вид цели «%s», res=%d%s), тайл ЦЕЛИ (%d,%d), ")
					TEXT("радиус %d тайлов: записей %d (основная цель rid=%d + ещё %d в боксе)"),
					SkillCode < 1201 ? TEXT("5256") : TEXT("6158"), SkillCode, *D.Name, D.Kind,
					TargetSlotTypeName(D.TargetSlotType), D.ResourceType,
					bArea132 ? TEXT(", площадь включена эффектом 132") : TEXT(""),
					TileX, TileZ, RadTiles, Recs.Num(), SendIdx, Recs.Num() - 1);
				return true;
			}
		}

		// ── B. ПЛОЩАДЬ ПО ЦЕЛИ-ПЕРСОНАЖУ ───────────────────────────────────────────────────────
		// ★★★ ВХОД В ПЛОЩАДЬ ЧЕРЕЗ `targetSlotType` 2/3 — НЕЗАВИСИМО ОТ `resourceType` (2026-08-11).
		//   Эталон `skill_buff.c:31853-31866` (диспетчер вида 1, ветка «цель-ПЕРСОНАЖ»):
		//     switch (targetSlotType) {
		//       case 0/1/4/5: switch (resourceType) { 0/1 → одна запись; 2/3 → Skill_SendAreaSkillCast }
		//       case 2/3:     Party_IsInParty ? Skill_SendAreaSkillCast(…, 2) : нотис 1223;   ← БЕЗ switch по rt
		//     }
		//   То есть у slot 2/3 развилки по `resourceType` НЕТ ВООБЩЕ — сбор согруппников идёт всегда.
		//   Порт гейтил площадь только через `rt ∈ {2,3}` и терял навык 925 «Жертвенный договор»
		//   (`castType = 3, slot = 2, rt = 0, rad = 10, kind = 1`): вместо сбора согруппников уходила
		//   одна запись. У диспетчера ВИДА 2 (`combat_pvp.c:7297-7470`) развилка по `rt` есть — там
		//   условие остаётся прежним.
		const bool bSlotPartyArea = (D.Kind != 2) && (D.TargetSlotType == 2 || D.TargetSlotType == 3);
		if (bTargetIsChar && (bResArea2 || bArea132 || bSlotPartyArea))
		{
			if (D.Kind == 2)
			{
				return SendUseAreaMixed(SkillCode, D, 8, TIdx, D.TargetSlotType);
			}
			// ★ 05.11.2026 [ОРИГИНАЛ] враждебный (слот 6) — не групповая площадь: разбор у ключа asda.Skill.HostileAreaCharTarget.
			if (D.TargetSlotType == 6 && CVarAsdaSkillHostileAreaCharTarget.GetValueOnGameThread() != 0)
			{
				const bool bPvp = AsdaPvpRel::IsOn() ? AsdaPvpRel::GetSelf(GetGameInstance()).bOn : bSelfPvpMode;
				return bPvp ? SendUseAreaMixed(SkillCode, D, 8, TIdx, D.TargetSlotType)     // `skillCastAreaTargetsAndSend` по цели
				            : SendUseAreaMixed(SkillCode, D, 0, -1, D.TargetSlotType);      // `Skill_SendAreaSkillTargetsCast(свой центр, −1)`
			}
			if (UAsdaWorldSubsystem* W = GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
			{
				// Вид 1: `Skill_SendAreaSkillCast` — бокс вокруг СЕБЯ, записи ПЕРСОНАЖЕЙ-согруппников.
				const int32 RadTiles = AreaRadiusTiles(SkillCode, D);
				FVector2D BoxCenter; float Half = 0.f;
				AreaBoxFromTile(G, RadTiles, BoxCenter, Half);
				TArray<int32> Uidxs;
				W->PickPartyUserIdxsInGameBox(BoxCenter, Half, Uidxs, 20, SelfUserIdx());
				TArray<UAsdaNetSubsystem::FAsdaSkillUseRec> Recs;
				for (int32 U : Uidxs)
				{
					UAsdaNetSubsystem::FAsdaSkillUseRec R;
					R.Type = 1; R.Idx = U;
					Recs.Add(R);
				}
				const FIntPoint SelfTile2 = AsdaCoord::GameToTile(G);   // ★ Аудит №3 (B2)
				const int32 SelfTileX = SelfTile2.X, SelfTileZ = SelfTile2.Y;
				StopBeforeCast(SkillCode);
				const bool bAreaOk = Net->SendSkillUseRequest(SkillCode, SelfTileX, SelfTileZ, Recs, 0, 0);
				if (!bAreaOk)
				{
					ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: заявка (групповая площадь) НЕ отправлена — сеть вернула отказ"), SkillCode);
					ActiveSkill = -1; ActiveSkillSentS = 0.0; return false;
				}
				if (AsdaSkillApproachOverAttackOn() && ApproachSkill >= 0)   // ★ заявка ушла — отложенное действие 102 исчерпано (шапка у ключа)
				{
					CancelSkillApproach(TEXT("заявка навыка ушла — отложенное действие одно (эталон)"));
				}
#if ASDA_COMBAT_TRACE
				AsdaCtSkillCycle(GetGameInstance(), SkillCode, -1, -1);   // журнал боя: групповой бафф — цикл не открываем, отметка для страховки
#endif
				ASDA_CTRACE(EAsdaCT::Send, TEXT("%s ЗАЯВКА навыка %d «%s»: ГРУППОВАЯ ПЛОЩАДЬ по цели-персонажу userIdx=%d, свой тайл (%d,%d), радиус %d: записей %d; состояние → 25"),
					SkillCode < 1201 ? TEXT("5256") : TEXT("6158"), SkillCode, *D.Name, TIdx, SelfTileX, SelfTileZ, RadTiles, Recs.Num());
				++Sent5256;
				// ★★★ П29 (2026-08-11): эталон после КАЖДОЙ заявки ставит состояние **25**
				//   (`CharFx_AutoSabzil_State(char, id, 25, 0, 0)` — skill_buff.c:2237/2582/2783/
				//   3061/3348/3647/3874/4467, все восемь отправителей). 25 входит в `StateBlocksCast`,
				//   то есть именно оно запрещает вторую заявку до ответа. Снимается ответом 5257/5270
				//   (24/14), отказом (0) и сторожем «ответа нет» kNoAnswerGuardS.
				CharState = 25;
				LastSentSkill = SkillCode;
				UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %s ушёл: навык %d «%s» — ГРУППОВАЯ ПЛОЩАДЬ ")
					TEXT("по цели-персонажу (Skill_SendAreaSkillCast), тайл (%d,%d), радиус %d: записей %d"),
					SkillCode < 1201 ? TEXT("5256") : TEXT("6158"), SkillCode, *D.Name,
					SelfTileX, SelfTileZ, RadTiles, Recs.Num());
				return true;
			}
		}
	}

	// ★★★ СТОП ПЕРЕД ЗАЯВКОЙ — обязателен (эталон: `if (getFlag_47551) Net_SendActionPacket` = 4008).
	StopBeforeCast(SkillCode);
	const bool bOk = Net->SendSkillUse5256(SkillCode, TileX, TileZ, SendType, SendIdx, GndX, GndZ);
	if (!bOk)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: заявка 5256 НЕ отправлена — сеть вернула отказ"), SkillCode);
		ActiveSkill = -1; ActiveSkillSentS = 0.0; return false;
	}
	// ★★★ 01.11.2026 [ОРИГИНАЛ] (`asda.Skill.ApproachOverAttack`): заявка УШЛА — отложенный подход (любого навыка, и этого же) снят.
	//   У эталона отложенное действие одно: `skillCastActivate` переписывает 102 новым навыком, а отправка его исчерпывает. Порт держал
	//   старый подход до 20 с — его маршрут гасил бег в покой поверх клипа нового навыка (самотест AnimAudit fight: 516/526/532), а по
	//   прибытии мог выпустить навык второй раз. Снятие — ПОСЛЕ успешной отправки: отказ отправки подход не трогает.
	if (AsdaSkillApproachOverAttackOn() && ApproachSkill >= 0)   // ★ заявка ушла — отложенное действие 102 исчерпано (шапка у ключа)
	{
		CancelSkillApproach(TEXT("заявка навыка ушла — отложенное действие одно (эталон)"));
	}
	// ★ 31.10.2026 [ОРИГИНАЛ] цель навыка — своему персонажу (разбор у ключа asda.Skill.FaceTargetEtalon). Само-цель: эталон пишет свой
	//   userIdx (:7508) — вектор доворота нулевой, курс не меняется; у порта это «цель = сам» ⇒ прежняя цель снимается.
	if (AsdaSkillFaceTargetEtalonOn() && SendIdx >= 0)
	{
		if (UAsdaWorldSubsystem* WF = GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
		{
			AActor* FaceT = WF->ActorByTarget(SendType == 1 ? EAsdaTargetKind::Character : EAsdaTargetKind::Entity, SendIdx);
			Ch->AttackFaceTarget = (FaceT && FaceT != Ch) ? FaceT : nullptr;
			Ch->TargetModeMask = (SendType == 1) ? 8 : 1;
			ASDA_CTRACE(EAsdaCT::State, TEXT("навык %d: цель доворота своего персонажа := %s (%s idx=%d, маска %d) — эталон combatHandleSkillUse"),
				SkillCode, FaceT ? *GetNameSafe(FaceT) : TEXT("нет в мире"), SendType == 1 ? TEXT("персонаж") : TEXT("сущность"), SendIdx, (int32)Ch->TargetModeMask);
		}
	}
#if ASDA_COMBAT_TRACE
	// Журнал боя: запись по мобу (тип 0) — цикл по нему; по чужому игроку атакующим навыком (вид 6) под PvP — PvP-цикл.
	AsdaCtSkillCycle(GetGameInstance(), SkillCode, (SendType == 0 && SendIdx >= 0) ? SendIdx : -1,
		(SendType == 1 && bTargetIsChar && SendIdx == TIdx && SendIdx != SelfUserIdx() && D.TargetSlotType == 6 && AsdaSkillPvpOn(GetGameInstance(), bSelfPvpMode)) ? SendIdx : -1);
#endif
	ASDA_CTRACE(EAsdaCT::Send, TEXT("%s ЗАЯВКА навыка %d «%s»: вид «%s», запись (тип %d = %s, idx %d), тайл (%d,%d) [%s], до цели %.0f, земля отброса (%d,%d); состояние → 25"),
		SkillCode < 1201 ? TEXT("5256") : TEXT("6158"), SkillCode, *D.Name, TargetSlotTypeName(D.TargetSlotType),
		SendType, SendType == 1 ? TEXT("персонаж") : TEXT("моб"), SendIdx, TileX, TileZ,
		bAddressesTarget ? TEXT("тайл ЦЕЛИ") : TEXT("свой тайл"), TDist, GndX, GndZ);
	++Sent5256;
	// ★★★ П29 (2026-08-11): эталон после КАЖДОЙ заявки ставит состояние **25**
	//   (`CharFx_AutoSabzil_State(char, id, 25, 0, 0)` — skill_buff.c:2237/2582/2783/
	//   3061/3348/3647/3874/4467, все восемь отправителей). 25 входит в `StateBlocksCast`,
	//   то есть именно оно запрещает вторую заявку до ответа. Снимается ответом 5257/5270
	//   (24/14), отказом (0) и сторожем «ответа нет» kNoAnswerGuardS.
	CharState = 25;
	LastSentSkill = SkillCode;

	// ★★★ КУЛДАУН ВЗВОДИТСЯ НЕ ЗДЕСЬ. Эталон `recvOtherCharSkillUse@0xF4D7D0` (network.c:44624):
	//   `if (result == 1 && это локальный игрок) sub_9216EC(g_pSkillWnd, skillId)` →
	//   `skillStartGroupCooldown@0xE57860`. То есть кулдаун ставит ОТВЕТ 5271 с кодом 1, а не
	//   отправка заявки. Прежний предикт «на отправке» и давал ложную картину «навык применился»
	//   (юзер: «кулдаун появляется, как будто применяются»), маскируя тишину сервера.
	// ★ Почему заявка ОДИНОЧНАЯ, печатаем прямо здесь: спор «а этот навык разве не площадной?»
	//   решается полем `resourceType` (+0x36), и ТОЛЬКО им. Поле `AoeRadius` (+0x37) на одиночных
	//   путях НЕ ЧИТАЕТСЯ вовсе — у 205 из 280 активных навыков там стоит 1, включая само-баффы;
	//   порога вида «радиус > N ⇒ площадь» в эталоне НЕТ (все 20 чтений `+0x37` лежат внутри
	//   четырёх площадных отправителей, плюс тултип «зона действия» и эффект 154).
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %s ушёл: навык %d «%s» — вид «%s» (targetSlotType=%d, ")
		TEXT("castType=%d, resourceType=%d), тайл (%d,%d) [%s], запись (тип %d = %s, idx %d), земля (%d,%d); ")
		TEXT("ОДИНОЧНАЯ заявка: resourceType=%d (площадь только при 2/3 либо при эффекте 132), ")
		TEXT("AoeRadius=%d тайлов на этом пути НЕ читается; земля = %s"),
		SkillCode < 1201 ? TEXT("5256") : TEXT("6158"),
		SkillCode, *D.Name, TargetSlotTypeName(D.TargetSlotType), D.TargetSlotType,
		D.CastType, D.ResourceType, TileX, TileZ,
		bAddressesTarget ? TEXT("тайл ЦЕЛИ") : TEXT("свой тайл"),
		SendType, SendType == 1 ? TEXT("персонаж") : TEXT("моб"), SendIdx, GndX, GndZ,
		D.ResourceType, D.AoeRadius,
		// ★ Наземные тайлы — НЕ зона поражения: это точка приземления ОТБРОСА (`.sif +0x8B`).
		D.BehaviorType != 0
			? *FString::Printf(TEXT("не пишется (behaviorType=%d ≠ 0 — отброс без пика земли)"), D.BehaviorType)
			: (GndX || GndZ
				? *FString::Printf(TEXT("точка ОТБРОСА цели: 450 ед. за целью по лучу «кастер→цель» ")
					TEXT("(behaviorType=0); НЕ зона поражения"))
				: TEXT("behaviorType=0, но точка отброса непроходима — тайлы нули (как в эталоне)")));
	return true;
}
