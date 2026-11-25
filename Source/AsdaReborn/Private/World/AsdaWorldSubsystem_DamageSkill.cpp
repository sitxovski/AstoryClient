// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_DamageSkill.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Damage.cpp, дословный перенос): урон навыков: OnSkillHit, ApplySkillHit, поворот к цели, DoT, признаки отряда.
//   Общее для частей — в AsdaWorldSubsystem_Damage_Private.h; шапка и реверс — в AsdaWorldSubsystem_Damage.cpp.
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCombatChat.h"   // ★ R5: AsdaCombatChat::Line — строка боя в системный чат (одна копия)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: ключ отката ритма (темп предметов), кадры ударов своего замаха
#include "UI/AsdaChatSubsystem.h"            // ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки
// ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "World/AsdaFloatingTextSubsystem.h" // цифры урона (порт CFloatingText)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "World/AsdaBlendAnimInstance.h"     // ★ 30.10.2026: вздрагивание жертвы навыка (AsdaAnimSmooth::StartHitFlinch)
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "UI/AsdaPartySubsystem.h"           // ★ bowfx: настоящий отряд для отбора целей навыков (IsPartyMember / IsInParty)
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
#include "Text/AsdaTextSubsystem.h"          // loc-нотисы подбора (1011/1012/1030)
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystem_Damage_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Damage*.cpp

FColor AsdaChatColHitOnMe()
{
	return GAsdaChatHitColorFix != 0 ? FColor(0xFF, 0x7D, 0x13, 0xFF) : FColor(0xFF, 0x7B, 0x93, 0xFF);
}

// Цвета строк урона навыка эталона (D3DCOLOR → FColor): −9053441 «мой урон навыком», −55021 «урон навыком по мне».
static const FColor kAsdaSkillChatColMine(0x75, 0xDA, 0xFF, 0xFF);
static const FColor kAsdaSkillChatColOnMe(0xFF, 0x29, 0x13, 0xFF);

// Одна строка урона навыка в чат: `CObject_SetSlotString`×N + `CClass_SetSlotPair` → `UI_FormatLocalizedNotice(id)` →
//   `CChat_AddTokenizedLines(…, цвет, 10000, 0)` (дорожка A, TTL 10 000 мс). Пустой текст нотиса — строки нет (как эталон).
static void AsdaSkillDamageChat(UGameInstance* GI, int32 NoticeId, const TArray<FString>& Strs, const TArray<int32>& Nums, const FColor& Col)
{
	// ★ R5 (12.11.2026): формат и отправка — общая `AsdaCombatChat::Line`; нет подсистем — выход без строки журнала, как было.
	FString Line;
	if (AsdaCombatChat::Line(GI, NoticeId, Strs, Nums, Col, &Line) == EAsdaCombatChat::NoSubsystems) { return; }
	ASDA_CTRACE(EAsdaCT::DmgText, TEXT("строка урона навыка в чат: нотис %d «%s»"), NoticeId, *Line);
}

// ══ ★★★ 4102 — УРОН ОТ НАВЫКА (эталон `netApplySkillHitResultPacket@0xF4F8A0`, network.c:45118) ══
//   Разбор пакета и доказательства «урона навыка нет ни в 4029, ни в 4015» — в шапке делегата
//   `UAsdaNetSubsystem::OnSkillHit`.
//
// ★★★ СТЫК «УРОН ↔ ВИЗУАЛ». Сервер считает попадание сразу и шлёт 4102 задолго до кадра удара
//   (COMBO0 у протестированных навыков 400…2133 мс). Эталон в этот момент НЕ рисует цифру: он
//   раскладывает результат по полям персонажа, а показывает его ТИК — на пороге фазы
//   (`CSkillCast_UpdateMeleeHit@0xA64BB0`, `SkillEffect_UpdateScaledStrikeHit@0xA72610`) либо по
//   прилёту носителя (`CEffect_UpdateSkillCast@0xA5A060`). Поэтому запись сначала предлагается
//   визуальному слою: если у навыка идёт показ — она паркуется там и вернётся делегатом
//   `OnSkillHitMoment` ровно в момент удара. Если показа нет (чужой каст, бафф без клипа, строки в
//   `DT_SkillClips` нет) — применяем немедленно, как раньше.
void UAsdaWorldSubsystem::OnSkillHit(const UAsdaNetSubsystem::FAsdaSkillHitRec& R)
{
	if (UAsdaSkillFxSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr)
	{
		if (Fx->DeferSkillHit(R))
		{
			UE_LOG(LogAsdaWorld, Verbose,
				TEXT("[навык] 4102 отложен до момента удара: навык %d, цель kind=%d idx=%d, урон=%d"),
				R.SkillId, R.TargetKind, R.TargetIdx, R.Damage);
			ASDA_CTRACE_IF(AsdaCtWorld::SkillOurs(R.CasterIdx, (int32)R.TargetKind, R.TargetIdx), EAsdaCT::Skill, TEXT("4102 навык %d кастера uidx=%d → %s %d: урон %d, HP после %d — ОТЛОЖЕН до момента удара (показ навыка идёт)"),
				R.SkillId, R.CasterIdx, (R.TargetKind == 0) ? TEXT("моб rid") : TEXT("игрок uidx"), R.TargetIdx, R.Damage, R.HpAfter);
			// ★ 17.10.2026 (Ф1): запись 4102 по мобу ждёт момента удара — она же запись ленты попаданий (HP и смерть ждут её).
			if (R.TargetKind == 0) { ArmSkillImpact(R.CasterIdx, R.TargetIdx, /*bFrom4102=*/true, R.Damage, R.HpAfter); }
			return;
		}
	}
	// ★ R5-fix8 (14.11.2026) [ОРИГИНАЛ]: показа ещё нет, но 5257/5270 этого кастера принят тем же проходом сети — запись ждёт его
	//   на полке (ключ `asda.Skill.Hit4102WaitsQueuedCast`, разбор — у `UAsdaSkillFxSubsystem::ShelveHitForQueuedCast`).
	if (UAsdaSkillFxSubsystem* FxQ = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr)
	{
		if (FxQ->ShelveHitForQueuedCast(R))
		{
			if (R.TargetKind == 0) { ArmSkillImpact(R.CasterIdx, R.TargetIdx, /*bFrom4102=*/true, R.Damage, R.HpAfter); }
			return;
		}
	}
	ASDA_CTRACE_IF(AsdaCtWorld::SkillOurs(R.CasterIdx, (int32)R.TargetKind, R.TargetIdx), EAsdaCT::Skill, TEXT("4102 навык %d кастера uidx=%d → %s %d: урон %d, HP после %d — показа навыка нет, применяется СРАЗУ"),
		R.SkillId, R.CasterIdx, (R.TargetKind == 0) ? TEXT("моб rid") : TEXT("игрок uidx"), R.TargetIdx, R.Damage, R.HpAfter);
	ApplySkillHit(R, /*bShowMissText=*/false, /*bLedgerArmed=*/false);
}

// Момент удара наступил — визуальный слой вернул запись (порог фазы либо прилёт носителя).
void UAsdaWorldSubsystem::OnSkillHitMoment(const UAsdaNetSubsystem::FAsdaSkillHitRec& R, bool bShowMissText)
{
	ASDA_CTRACE_IF(AsdaCtWorld::SkillOurs(R.CasterIdx, (int32)R.TargetKind, R.TargetIdx), EAsdaCT::HitFrame, TEXT("МОМЕНТ УДАРА навыка %d (%s) кастера uidx=%d → %s %d: урон %d, флаги 0x%02X, HP после %d%s"),
		R.SkillId, R.bFrom4102 ? TEXT("4102") : TEXT("5257/5270"), R.CasterIdx, (R.TargetKind == 0) ? TEXT("моб rid") : TEXT("игрок uidx"), R.TargetIdx,
		R.Damage, (int32)R.HitFlags, R.HpAfter, bShowMissText ? TEXT(", фаза ведёт удар (MISS покажется)") : TEXT(""));
	ApplySkillHit(R, bShowMissText);
}

// ══ ★★★ СТИЛЬ ЦИФРЫ УРОНА НАВЫКА — 1:1 ЭТАЛОН ════════════════════════════════════════════════
//   Цель-СУЩНОСТЬ: `CharFx_ShowSkillDamageText@0xA7FC00` (effects_particles.c:11399).
//   Цель-ПЕРСОНАЖ: `Combat_ShowDamageFeedback@0xA80390` (combat_pvp.c:3517).
//   Обе функции ветвятся по флагам ОДИНАКОВО (`hitFlags` = байт `+7` 29-байтной записи):
//       урон != 0 : &4 → стиль 4 (чат-нотис 20247)   ← КРИТ
//                   &8 → стиль 8 (20248)
//                   &0x20 → стиль 5 «IMMUNE» (20010)
//                   иначе → стиль 3 (20249)          ← обычный урон навыка
//       урон == 0 : стиль 5 «MISS» (20010)
//   Различие ровно одно и только у цели-ПЕРСОНАЖА: если жертва — ЛОКАЛЬНЫЙ игрок
//   (`pTarget[47565]`), крит рисуется стилем 0xC, а обычный урон — 0xB (красная гамма «по нам»).
//   ⚠ Прежде порт рисовал урон навыка стилем 1 (наш обычный удар) — цвет и трек не совпадали ни с
//     одной эталонной веткой, а крит/IMMUNE/блок не различались вовсе.
//
// ★★★ П21 + П26 (T3, 2026-08-11) — ТРИ ветки, а не две, и ПОРЯДОК проверок значим.
//   `Combat_ShowDamageFeedback@0xA80390` (combat_pvp.c:3580-3795) в КАЖДОМ из пяти случаев ветвится
//   в одном и том же порядке (адреса — по обычному урону, остальные четыре устроены так же):
//     1) `if ( *(_DWORD *)(pAttacker + 190260) )`  — АТАКУЮЩИЙ Я           (`:3712`) → стиль 3
//     2) `else if ( *((_DWORD *)pTarget + 47565) )`— ЖЕРТВА Я              (`:3730`) → стиль 0xB
//     3) `else if ( CClass_GetField10112(&g_InventoryBag) == 1 )` — НАБЛЮДАТЕЛЬ (`:3748`) → стиль 0xB
//   `pAttacker + 190260` и `pTarget + 47565` — ОДНО И ТО ЖЕ поле (47565·4 = 190260), флаг «этот
//   объект — локальный игрок»; поэтому «бью я» ПРОВЕРЯЕТСЯ ПЕРВЫМ и **само-удар** (атакующий = жертва
//   = я) уходит в ветку атакующего, а не жертвы (П26).
//   Полная таблица (`урон != 0`):
//       флаг      | бью Я | бьют МЕНЯ | наблюдатель
//       &4  КРИТ  |   4   |    0xC    |     4        (`:3584/:3606/:3622`)
//       &8        |   8   |    8      |     8        (`:3627/:3648/:3666`)
//       &0x20     |   5 «IMMUNE» во всех трёх                (`:3672/:3689/:3705`)
//       иначе     |   3   |    0xB    |    0xB       (`:3712/:3730/:3748`)
//   `урон == 0` → 5 «MISS» во всех трёх (`:3755/:3772/:3788`).
//   ⇒ ПРЕЖНИЙ ДЕФЕКТ порта: `return bVictimIsLocalPlayer ? 0xB : 0x3;` отдавал наблюдателю 3
//   («мой урон»), а само-удар рисовал стилем жертвы. Обе ошибки закрыты третьим признаком.
//   ★ Цель-СУЩНОСТЬ (`CharFx_ShowSkillDamageText@0xA7FC00`, effects_particles.c:11440-11566) имеет
//   ровно ДВЕ ветки — «бью я» и «наблюдатель» — и стили в них СОВПАДАЮТ (4/4, 8/8, 5/5, 3/3, 5/5),
//   потому что моб локальным игроком быть не может. Отсюда вызов с `bVictimIsLocalPlayer = false`
//   даёт для мобов эталон при любом значении `bAttackerIsLocalPlayer` — регресса у мобов нет.
int32 UAsdaWorldSubsystem::SkillDamageTextStyle(int32 HitFlags, int32 Damage,
	bool bAttackerIsLocalPlayer, bool bVictimIsLocalPlayer)
{
	if (Damage == 0) { return 0x5; }                       // «MISS» — один стиль во всех трёх ветках
	if ((HitFlags & 0x04) != 0)                            // КРИТ
	{
		if (bAttackerIsLocalPlayer) { return 0x4; }        // ветка 1 — бью я
		return bVictimIsLocalPlayer ? 0xC : 0x4;           // ветка 2 — бьют меня / ветка 3 — наблюдатель
	}
	if ((HitFlags & 0x08) != 0) { return 0x8; }            // все три ветки дают 8
	if ((HitFlags & 0x20) != 0) { return 0x5; }            // «IMMUNE» — все три ветки дают 5
	if (bAttackerIsLocalPlayer) { return 0x3; }            // ветка 1 — обычный урон, бью я
	return 0xB;                                            // ветки 2 и 3 — бьют меня / наблюдатель
}

// ══ ★★★ СТИЛЬ ЦИФРЫ КАНАЛА 4102 — ДРУГОЙ ПУТЬ ЭТАЛОНА (T1, остаток П4) ════════════════════════
//   ЗАПИСЬ 4102 (17 б) поля флагов НЕ несёт, и по `hitFlags` эталон здесь не ветвится вовсе.
//   Цепочка: `netApplySkillHitResultPacket@0xF4F8A0` (network.c:45239)
//     → `CCharacter_TakeDamage2(victim, урон, a4 = 3, userIdx)` (entity_char.c:25891)
//     → `a4 == 3` → `combatFloatingDamageText(victim, userIdx, урон, damageType = 0)`
//       (combat_pvp.c:1594-1636).
//   Тело ветки `case 0` ДОСЛОВНО:
//     ```
//     if ( targetChar->effectSlots[112].field_40                      // ЖЕРТВА — локальный игрок
//       || Target_GetSelectedUserIdx(localPlayer) == thisChar[59126] )// последний бивший == МОЙ userNum
//     {
//       textStyle = 7;  noticeId = 20013; chatColor = -1;             // ← бью Я
//       if ( thisChar[47565] ) { textStyle = 15; noticeId = 20241; chatColor = -33517; }  // ← бьют МЕНЯ
//       FloatingText_Spawn(поз жертвы, textStyle, "%d");
//       …CObject_SetSlotString(0, имя жертвы); CClass_SetSlotPair(0, урон);
//       CChat_AddTokenizedLines(UI_FormatLocalizedNotice(noticeId), chatColor, 10000, 0);
//     }
//     else if ( CClass_GetField10112(&g_InventoryBag) == 1 )          // настройка «чужой урон»
//     {
//       FloatingText_Spawn(поз цели, 9u, "%d");                       // ← НАБЛЮДАТЕЛЬ, без чат-строки
//     }
//     ```
//   ★ `Target_GetSelectedUserIdx` — МИСНЕЙМ: это геттер СОБСТВЕННОГО userNum (`Char_GetUserNum`),
//     а `thisChar[59126]` (байт 236504) сервер-обработчик 4102 заполняет двумя строками выше
//     (`pByUserIdx[59126] = rec[+14]` = userIdx АТАКУЮЩЕГО, network.c:45225). Значит сравнение
//     читается как «атакующий — Я».
//   ⇒ Итог: **7** бью я · **15** бьют меня · **9** наблюдатель. Стиль 15 (`0x0F`) в
//     `AsdaFloatingTextSubsystem::StyleParams` есть и возвращает `true` — там править было нечего;
//     дефект был ровно в том, что записи 4102 шли в общий `SkillDamageTextStyle` с `HitFlags == 1`
//     и получали 3 либо 0xB.
int32 UAsdaWorldSubsystem::Damage4102TextStyle(bool bAttackerIsLocalPlayer, bool bVictimIsLocalPlayer)
{
	if (bVictimIsLocalPlayer)  { return 0x0F; }            // 15 — красная гамма «по нам», нотис 20241
	if (bAttackerIsLocalPlayer){ return 0x07; }            // 7  — мой урон, нотис 20013
	return 0x09;                                           // 9  — наблюдатель, чат-строки нет
}

//   Здесь — ровно две ветки эталона.
void UAsdaWorldSubsystem::ApplySkillHit(const UAsdaNetSubsystem::FAsdaSkillHitRec& R, bool bShowMissText, bool bLedgerArmed)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// ★ «Атакующий — Я» = эталонный `pAttacker[190260]` / `pCaster[47565]` (одно и то же поле,
	//   47565·4 = 190260 — флаг «этот объект локальный игрок»). Это ПЕРВЫЙ признак ветвления стиля.
	const bool bOurCast = AsdaSelf::IsSelfUserIdx(Net, R.CasterIdx);   // E2
	// ★★★ П23 (T4). Отсев кодов 181/182 принадлежит ТОЛЬКО обработчику 4102
	//   (`netApplySkillHitResultPacket@0xF4F8A0`, network.c:45230 и :45257 — по одному в каждой из двух
	//   веток). В `Recv_SkillCast@0xF4E370` (5257) и `Recv_SkillCastIdle@0xF4D9C0` (5270) этих кодов
	//   НЕТ ВОВСЕ — там записи проходят как есть. Порт применял отсев ко всем источникам и молча
	//   выбрасывал урон навыков 181/182, пришедший 29-байтными записями.
	const bool bSkillIdFiltered = R.bFrom4102 && (R.SkillId == 181 || R.SkillId == 182);

	// ── ВЕТКА kind == 0: цель — СУЩНОСТЬ (моб), ищется по renderId (`Entity_FindByRenderId_thunk`).
	if (R.TargetKind == 0)
	{
		TWeakObjectPtr<AAsdaMobActor>* Ex = NetMobs.Find(R.TargetIdx);
		AAsdaMobActor* M = Ex ? Ex->Get() : nullptr;
		if (!M)
		{
			UE_LOG(LogAsdaWorld, Verbose, TEXT("[навык] 4102: цели-сущности rid=%d в мире нет (навык %d) — запись пропущена (эталон так же)"),
				R.TargetIdx, R.SkillId);
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::Skill, TEXT("урон навыка %d по rid=%d (урон %d) НЕ применён — цели-моба нет в мире"), R.SkillId, R.TargetIdx, R.Damage);
			// ★ 31.10.2026 (ревизия А5): запись ленты этой цели закрываем и здесь — иначе она висела до потолка окна (4 с).
			if (bLedgerArmed && ConsumeSkillImpact(R.CasterIdx, R.TargetIdx, R.bFrom4102, R.Damage)) { ProcessLedger(R.TargetIdx, TEXT("запись навыка: цели нет")); }
			return;
		}
		// 1) Пометка трупа. Эталон: `hpAfter <= 0 && hpAfter != -1 && !(ent[121] & 0x20)`.
		//    −1 в этом поле означает «сервер HP не сообщил», трупом по нему помечать нельзя.
		// ★ 05.11.2026 (bug-mob-hp-zero, ключ `asda.Combat.HealEpoch`): запись посчитана сервером ДО лечения 8013 — её урон уже внутри
		//   значения лечения (живой журнал Кайи 01.11: HpAfter 11160 после лечения до 28224). Цифра показывается, полоса/пол/смерть — нет.
		const bool bPreHeal = R.ApplyFlag == 1 && IsPreHealRecord(M, R.HpAfter, R.Damage);
		if (bPreHeal)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[навык] навык %d по rid=%d '%s': запись до лечения (HP после %d + урон %d ниже полосы %d) — цифра без вычета, HP записи не применяется"),
				R.SkillId, R.TargetIdx, *M->MobName, R.HpAfter, R.Damage, M->CurHp);
		}
		const bool bCorpse = (R.HpAfter <= 0) && (R.HpAfter != -1) && !bPreHeal;
		// ★ 17.10.2026 (Ф1): запись навыка выпущена — она закрывает свою запись ленты (последняя — попадание наступило сейчас).
		const bool bLedgerRec = bLedgerArmed && ConsumeSkillImpact(R.CasterIdx, R.TargetIdx, R.bFrom4102, R.Damage);
		// 2) Применение результата. Эталон: `applyFlag == 1`; отсев 181/182 — ТОЛЬКО у 4102 (П23).
		// ★★★ П23 (T4): условие показа у эталона — `if (damageAmount)`, то есть УРОН ≠ 0, а не > 0.
		//   `CharFx_ShowSkillDamageText@0xA7FC00` печатает величину через `sprintf(Buffer, "%d", …)`,
		//   поэтому отрицательное значение (лечащая запись боевого процесса) у оригинала ВИДНО, а порт
		//   его молча глушил вместе со всей веткой применения.
		if (R.ApplyFlag == 1 && !bSkillIdFiltered && R.Damage != 0)
		{
			// `CClass_SubtractStatClamped(target, урон, 0)` (entity_char.c:57922) — `HP -= урон`, и
			//   клампится ТОЛЬКО нижняя граница (`if (HP < 0) HP = 0`); верхнего клампа у эталона нет,
			//   поэтому отрицательный урон поднимает HP как есть.
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::Damage, TEXT("УРОН НАВЫКА %d (%s) кастера uidx=%d%s по %s: урон %d%s, флаги 0x%02X, HP после по серверу %d"),
				R.SkillId, R.bFrom4102 ? TEXT("4102") : TEXT("5257/5270"), R.CasterIdx, bOurCast ? TEXT(" (я)") : TEXT(" (сосед)"),
				*AsdaCtWorld::Mob(R.TargetIdx, M), R.Damage, (R.HitFlags & 0x04) ? TEXT(" КРИТ") : ((R.HitFlags & 0x20) ? TEXT(" IMMUNE") : TEXT("")),
				R.HitFlags, R.HpAfter);
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::HpPlate, TEXT("%s: HP полосы %d → %d / %d (локальный вычет урона навыка %d на моменте удара)"),
				*AsdaCtWorld::Mob(R.TargetIdx, M), M->CurHp, FMath::Max(0, M->CurHp - R.Damage), M->MaxHp, R.SkillId);
			if (!bPreHeal)   // ★ 05.11.2026: урон записи до лечения уже внутри значения лечения
			{
				M->CurHp = FMath::Max(0, M->CurHp - R.Damage);
				NoteMobHpShown(M->NetRenderId, R.Damage);   // ★ 05.11.2026 (asda.Combat.LiftLargeGap): учёт показанного урона
			}
			UpdateMobHpBar(M);
			// ★★★ ЦИФРА УРОНА НАД ЦЕЛЬЮ. Стиль зависит от ИСТОЧНИКА записи (T1):
			//   · 5257/5270 → `CharFx_ShowSkillDamageText@0xA7FC00` — по флагам (крит/блок/IMMUNE/обычный);
			//   · 4102      → `skillApplyChainedSubEffectsAtPos@0xA33900` (skill_buff.c:6703 и :6742,
			//     подтипы 3 и 5 — единственные ветки, печатающие число) — стиль **1** в обеих ветках
			//     («бью я» и «наблюдатель»), по флагам не ветвится вовсе.
			//   ⚠ ЧЕСТНО: сам switch подтипов `buff0SubType` (3 = прямой урон, 4/6 = DOT-функции,
			//     5 = процент от атаки) в порт не перенесён — записей 4102 с `kind == 0` и ненулевым
			//     уроном в логах нет ни одной, а строить ветвление «на глаз» запрещено. Перенесён
			//     ровно стиль, который доказан для числовых веток.
			const bool bImmune = ((R.HitFlags & 0x04) == 0) && ((R.HitFlags & 0x08) == 0)
				&& ((R.HitFlags & 0x20) != 0);
			const int32 Style = R.bFrom4102
				? 0x1
				: SkillDamageTextStyle(R.HitFlags, R.Damage, bOurCast, /*bVictimIsLocalPlayer=*/false);
			// ★ 17.10.2026 (Ф5) [ОРИГИНАЛ]: навык соседа по мобу — цифра при режиме показа 1 (entity_char.c:42251/58042), свой — всегда.
			if (UAsdaFloatingTextSubsystem* FT = GetGameInstance()->GetSubsystem<UAsdaFloatingTextSubsystem>(); FT && ShouldShowDamageText(bOurCast))
			{
				FT->SpawnOverActor(Style, bImmune ? TEXT("IMMUNE") : FString::FromInt(R.Damage), M);
				ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::DmgText, TEXT("цифра «%s» стиль %d над %s (навык %d)"), bImmune ? TEXT("IMMUNE") : *FString::FromInt(R.Damage), Style,
					*AsdaCtWorld::Mob(R.TargetIdx, M), R.SkillId);
			}
			else
			{
				ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::DmgText, TEXT("цифра урона навыка %d над %s СКРЫТА (режим показа цифр или нет подсистемы)"), R.SkillId, *AsdaCtWorld::Mob(R.TargetIdx, M));
			}
			// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] строка в чат — `CharFx_ShowSkillDamageText`, ветка «бью я» (разбор у `asda.Skill.DamageChatLines`).
			//   Режим показа цифр строк не касается (эталон: гейт `Field10112` только у ветки наблюдателя).
			if (GAsdaSkillDamageChatLines != 0 && !R.bFrom4102 && bOurCast)
			{
				const UAsdaSkillDatabase* SkDb = GetGameInstance()->GetSubsystem<UAsdaSkillDatabase>();
				if (const FAsdaSkillDef* Sk = SkDb ? SkDb->Get(R.SkillId) : nullptr)
				{
					if (R.HitFlags & 0x04)      { AsdaSkillDamageChat(GetGameInstance(), 20247, { M->MobName, Sk->Name }, { R.Damage }, kAsdaSkillChatColMine); }
					else if (R.HitFlags & 0x08) { AsdaSkillDamageChat(GetGameInstance(), 20248, { M->MobName, Sk->Name }, { R.Damage }, UAsdaChatSubsystem::ColWhite()); }
					else if (bImmune)           { AsdaSkillDamageChat(GetGameInstance(), 20010, { Sk->Name }, {}, UAsdaChatSubsystem::ColWhite()); }
					else                        { AsdaSkillDamageChat(GetGameInstance(), 20249, { M->MobName, Sk->Name }, { R.Damage }, UAsdaChatSubsystem::ColWhite()); }
				}
			}
			// Реакция цели на удар (разворот к бьющему + флинч + звук) — та же точка, что у обычной
			//   атаки: эталонный `CChar_SetState(target, 13)`. Импакт-эффект самого НАВЫКА (JEB из
			//   подслота `2·фаза+4`, колонка JebSlots таблицы DT_SkillClips) спавнит слой визуала
			//   навыков (`UAsdaSkillFxSubsystem`) — здесь его не подменяем чужим (закон «не выдумывать»).
			FVector2D AtkGame = FVector2D::ZeroVector;
			if (bOurCast)
			{
				if (const AActor* Me = LocalPlayerActor) { AtkGame = AsdaCoord::UeToGame2D(Me->GetActorLocation()); }   // ★ Аудит №3 (B1)
			}
			else if (const FAsdaNetPlayerEntry* PE = NetPlayers.Find(R.CasterIdx))
			{
				if (const AActor* C = PE->Actor.Get()) { AtkGame = AsdaCoord::UeToGame2D(C->GetActorLocation()); }   // ★ Аудит №3 (B1)
			}
			M->NetHitFlinch(AtkGame);
			UE_LOG(LogAsdaWorld, Log,
				TEXT("[навык] урон: навык %d, кастер uidx=%d%s → моб rid=%d '%s' урон=%d, HP %d ")
				TEXT("(сервер: %d), флаги=0x%02X%s, стиль цифры %d"),
				R.SkillId, R.CasterIdx, bOurCast ? TEXT(" (мой)") : TEXT(" (сосед)"),
				R.TargetIdx, *M->MobName, R.Damage, M->CurHp, R.HpAfter, R.HitFlags,
				(R.HitFlags & 0x04) ? TEXT(" КРИТ") : TEXT(""), Style);
		}
		// ★★★ ПРОМАХ. Эталон `CharFx_ShowSkillDamageText@0xA7FC00` (цель-сущность): `if (урон)
		//   {цифра стилем 4/8/5/3} else {FloatingText «MISS» стилем 5 + нотис 20010}`. Эффект удара
		//   и звук к этому моменту уже отыграли — их играет визуальный слой ДО выпуска записи
		//   (`CSkillCast_UpdateMeleeHit@0xA64BB0`, skill_buff.c:14225-14257: в ветке промаха тоже
		//   спавнятся `+0x68`/`+0x168` и звук `+0x598`, и только потом текст).
		//   `bShowMissText` приходит от визуального слоя: он один знает, ВЕДЁТ ли эта фаза удар.
		else if (bShowMissText)
		{
			// ★ Стиль «MISS» у эталона ОДИН И ТОТ ЖЕ (5) и в ветке «бью я», и в ветке наблюдателя —
			//   отдельного «чужого» 0xD в `CharFx_ShowSkillDamageText` НЕТ (0xD принадлежит обычной
			//   атаке, `CCharacter_AttackAnimTick@0x9A2200`). Прежний тернарник был домыслом.
			if (UAsdaFloatingTextSubsystem* FT = GetGameInstance()->GetSubsystem<UAsdaFloatingTextSubsystem>(); FT && ShouldShowDamageText(bOurCast))   // ★ Ф5
			{
				FT->SpawnOverActor(0x5, TEXT("MISS"), M);
			}
			// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] промах: строка 20010 {имя навыка} белым — только «бью я» (effects_particles.c:11545-11553).
			if (GAsdaSkillDamageChatLines != 0 && !R.bFrom4102 && bOurCast)
			{
				const UAsdaSkillDatabase* SkDb = GetGameInstance()->GetSubsystem<UAsdaSkillDatabase>();
				if (const FAsdaSkillDef* Sk = SkDb ? SkDb->Get(R.SkillId) : nullptr)
				{
					AsdaSkillDamageChat(GetGameInstance(), 20010, { Sk->Name }, {}, UAsdaChatSubsystem::ColWhite());
				}
			}
			UE_LOG(LogAsdaWorld, Log, TEXT("[навык] ПРОМАХ: навык %d, кастер uidx=%d%s → моб rid=%d '%s' (флаги=0x%02X)"),
				R.SkillId, R.CasterIdx, bOurCast ? TEXT(" (мой)") : TEXT(" (сосед)"), R.TargetIdx,
				*M->MobName, R.HitFlags);
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::Damage, TEXT("навык %d кастера uidx=%d%s по %s: ПРОМАХ (MISS стиль 5, флаги 0x%02X), HP полосы %d/%d без изменений"),
				R.SkillId, R.CasterIdx, bOurCast ? TEXT(" (я)") : TEXT(" (сосед)"), *AsdaCtWorld::Mob(R.TargetIdx, M), R.HitFlags, M->CurHp, M->MaxHp);
		}
		else
		{
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::Skill, TEXT("навык %d по %s: урон не применяется (%s), HP — только серверное ниже"), R.SkillId, *AsdaCtWorld::Mob(R.TargetIdx, M),
				(R.ApplyFlag != 1) ? TEXT("флаг применения ≠ 1") : (bSkillIdFiltered ? TEXT("навык 181/182 отсеян (4102)") : TEXT("урон 0, фаза не ведёт удар")));
		}
		// 2б) ★ 26.10.2026 (bowfx) [ОРИГИНАЛ] СОСТОЯНИЯ СВОЕГО УДАРА: `Skill_ApplyEffectResult@0xA2D780` на кадре удара — только у
		//   локального кастера (`CClass_GetField47565(pCaster)`) и только при бите 0x02 записи. Отсюда значок яда/крови под полосой
		//   HP цели и эффект состояния на мобе у самого лучника — до прихода 6079 (тот разносит то же состояние всему сектору).
		if (!R.bFrom4102 && bOurCast) { ApplyOwnHitSkillStates(M, R); }
		// 3) Серверный HP — авторитет (цифры уже показаны выше, повторно их не рисуем).
		//   ★ Ф1: при записях ленты — сверка «только вниз» (чужие ожидающие попадания этого моба не вычитаются заранее).
		if (R.HpAfter >= 0 && !bPreHeal) { NoteMobHpFloor(M, R.HpAfter, /*bFrom4029=*/false, R.bFrom4102 ? TEXT("4102") : TEXT("5257 +11")); NoteMobCombat(M->NetRenderId); }   // ★ 18.10.2026 (пол HP); ★ 05.11: не до лечения
		if (R.HpAfter >= 0 && !bPreHeal && !LedgerHoldServerHp(M, R.HpAfter, R.bFrom4102 ? TEXT("4102") : TEXT("5257 +11"), /*bStaleChecked=*/true, /*bAllowNumber=*/false))
		{
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::HpPlate, TEXT("%s: серверное HP после навыка %d → полоса (была %d) через ApplyMobHp"), *AsdaCtWorld::Mob(R.TargetIdx, M), R.HpAfter, M->CurHp);
			ApplyMobHp(M, R.HpAfter, /*bAllowNumber=*/false);
		}
		if (bCorpse && !M->IsDead())
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[навык] 4102: моб rid=%d '%s' убит навыком %d (HP после = %d)"),
				R.TargetIdx, *M->MobName, R.SkillId, R.HpAfter);
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::Skill, TEXT("%s УБИТ навыком %d кастера uidx=%d (HP после %d) — смерть на моменте удара навыка"),
				*AsdaCtWorld::Mob(R.TargetIdx, M), R.SkillId, R.CasterIdx, R.HpAfter);
			// ★ Ф1: смертельная запись навыка — смерть на ЕЁ попадании; лента снимается, удержанные добыча и опыт выходят.
			ApplyLedgerDeath(M, TEXT("смертельная запись навыка"));
		}
		else if (bLedgerRec) { ProcessLedger(R.TargetIdx, TEXT("запись навыка выпущена")); }
		return;
	}

	// ★★★ 30.10.2026 [ОРИГИНАЛ] ВЗДРАГИВАНИЕ ЖЕРТВЫ-ПЕРСОНАЖА от навыка (`gfxBias_LazyInitHalfTexel@0xA2C7B0`; 33 вызова бинаря, разбор —
	//   заметка flinch_sites_pvp_skills.md). Только 5257/5270: у 4102 (`combatFloatingDamageText`) вызовов нет.
	//   Т2a — правило `Combat_ShowDamageFeedback@0xA80390` (combat_pvp.c:3603/3619/3728/3745): урон ≠ 0, бью я или жертва я, крит (&4) или
	//        не блок/не IMMUNE (!(&0x28)), запись навыка есть (кроме «крит по мне»);
	//   Т2b — внешние вызовы, НЕЗАВИСИМО от урона и флагов 4/8/0x20: обработчики результата (`netHandleSkillHitResult@0xA5D7A0` и ещё 4,
	//        гранаты 604/607, 907, 911 — кастер я, флаг 1, все цели); тики лучника 705/710/713 (кастер я, флаг 1, основная цель),
	//        706 «Танцующая стрела» и 709 (любой кастер, флаг 1, основная цель);
	//   Т2c — 714 «Обстрел» (`CharSkill_UpdateBlastFx2@0xACB820`, skill_buff.c:25928/25946): основная цель-персонаж, флагов и гейта
	//        применения нет, любой кастер. Повторный старт при идущем вздрагивании эталон пропускает (бит 0x200).
	auto SkillFlinch = [this, &R, bOurCast](const TCHAR* Why)
	{
		if (!AsdaAnimSmooth::HitFlinchOn()) { return; }
		AAsdaPlayerCharacter* V = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(R.TargetIdx));
		if (!V || V->IsIncapacitated() || !V->BaseSkel || !V->BaseSkel->GetSkeletalMeshAsset()) { return; }
		const bool bStarted = AsdaAnimSmooth::StartHitFlinch(V->BaseSkel);
		ASDA_CTRACE_IF(AsdaCtWorld::Users(R.CasterIdx, R.TargetIdx), EAsdaCT::Skill, TEXT("навык %d кастера uidx=%d%s → игрок uidx=%d: вздрагивание %s (%s, флаги 0x%02X)"),
			R.SkillId, R.CasterIdx, bOurCast ? TEXT(" (я)") : TEXT(""), R.TargetIdx, bStarted ? TEXT("ПУСК") : TEXT("уже идёт"), Why, R.HitFlags);
	};
	// Основная цель кастера = первая запись пакета (`targetRenderId`/`+52134`); у порта её держит доворот к цели навыка (`OnSkillCastFaceTarget`).
	auto IsMainTarget = [this, &R]()
	{
		const AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(R.CasterIdx));
		return C && C->AttackFaceTarget.IsValid() && C->AttackFaceTarget.Get() == ActorByUserIdx(R.TargetIdx);
	};
	if (R.TargetKind == 1 && !R.bFrom4102)
	{
		if (R.SkillId == 714 && IsMainTarget()) { SkillFlinch(TEXT("714 Обстрел — без флагов")); }
		else if (R.HitFlags & 0x01)
		{
			const int32 Sk = R.SkillId;
			const bool bResultHandler = bOurCast && (Sk == 604 || Sk == 607 || Sk == 907 || Sk == 911);
			const bool bBowOwn = bOurCast && (Sk == 705 || Sk == 710 || Sk == 713) && IsMainTarget();
			const bool bBowAny = (Sk == 706 || Sk == 709) && IsMainTarget();
			if (bResultHandler || bBowOwn || bBowAny)
			{
				SkillFlinch(bResultHandler ? TEXT("обработчик результата 604/607/907/911") : TEXT("тик навыка лучника"));
			}
		}
	}

	// ── ВЕТКА kind == 1: цель — ПЕРСОНАЖ (`CharManager_FindByUserIdx`), урон через
	//    `CCharacter_TakeDamage2(char, урон, 3, attackerIdx)`: HP −= урон + всплывающая цифра
	//    (`combatFloatingDamageText`). Сам бафф уже разослан сетевым слоем (OnEffectMsg).
	// ★ 31.10.2026 (ревизия А3): промах (`bShowMissText`) приходит с битом 0 = 0, т.е. `ApplyFlag == 0`, — ветку пускаем и для него,
	//   иначе «MISS» и строка 20010 по персонажу были недостижимы (у мобов ветка промаха так и устроена).
	if (R.TargetKind == 1 && (R.ApplyFlag == 1 || bShowMissText) && !bSkillIdFiltered)
	{
		AActor* Victim = ActorByUserIdx(R.TargetIdx);
		if (!Victim)
		{
			UE_LOG(LogAsdaWorld, Verbose, TEXT("[навык] 4102: цели-игрока userIdx=%d в мире нет (навык %d)"),
				R.TargetIdx, R.SkillId);
			ASDA_CTRACE_IF(AsdaCtWorld::Users(R.CasterIdx, R.TargetIdx), EAsdaCT::Skill, TEXT("урон навыка %d по игроку uidx=%d (урон %d) НЕ применён — игрока нет в мире"), R.SkillId, R.TargetIdx, R.Damage);
			return;
		}
		UAsdaFloatingTextSubsystem* FT = GetGameInstance()->GetSubsystem<UAsdaFloatingTextSubsystem>();
		// «Жертва — это я»: у эталона гейт `pTarget[47565]` (флаг локального игрока на цели).
		const bool bVictimIsMe = AsdaSelf::IsSelfUserIdx(Net, R.TargetIdx);   // E2
		// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] строки чата `Combat_ShowDamageFeedback@0xA80390` (5257/5270; разбор у `asda.Skill.DamageChatLines`).
		//   Порядок веток эталона: «бью я» (`pAttacker+190260`) проверяется ПЕРВЫМ, затем «бьют меня»; наблюдателю строк нет.
		//   Имена — `thunk_getFieldAt1056` объекта: в ветке «бью я» — ЖЕРТВЫ, в ветке «бьют меня» — БЬЮЩЕГО.
		auto SkillPvpChat = [this, &R, Net, bOurCast, bVictimIsMe](bool bImmuneHit)
		{
			if (GAsdaSkillDamageChatLines == 0 || R.bFrom4102 || !(bOurCast || bVictimIsMe)) { return; }
			auto NameOf = [this, Net](int32 Uidx)
			{
				if (AsdaSelf::IsSelfUserIdx(Net, Uidx)) { return GetSelfCharName(); }
				const FAsdaNetPlayerEntry* PE = NetPlayers.Find(Uidx);
				return PE ? PE->Name : FString();
			};
			const UAsdaSkillDatabase* SkDb = GetGameInstance()->GetSubsystem<UAsdaSkillDatabase>();
			const FAsdaSkillDef* Sk = SkDb ? SkDb->Get(R.SkillId) : nullptr;
			const FString SkName = Sk ? Sk->Name : FString();
			UGameInstance* GI = GetGameInstance();
			const FColor White = UAsdaChatSubsystem::ColWhite();
			if (bOurCast)
			{
				if (!Sk) { return; }
				const FString Victim = NameOf(R.TargetIdx);
				if (R.Damage == 0)               { AsdaSkillDamageChat(GI, 20010, { SkName }, {}, White); }
				else if (R.HitFlags & 0x04)      { AsdaSkillDamageChat(GI, 20247, { Victim, SkName }, { R.Damage }, kAsdaSkillChatColMine); }
				else if (R.HitFlags & 0x08)      { AsdaSkillDamageChat(GI, 20248, { Victim, SkName }, { R.Damage }, White); }
				else if (bImmuneHit)             { AsdaSkillDamageChat(GI, 20010, { SkName }, {}, White); }
				else                             { AsdaSkillDamageChat(GI, 20249, { Victim, SkName }, { R.Damage }, kAsdaSkillChatColMine); }
				return;
			}
			const FString Attacker = NameOf(R.CasterIdx);
			if (R.Damage != 0 && (R.HitFlags & 0x04)) { AsdaSkillDamageChat(GI, 20240, { Attacker }, { R.Damage }, kAsdaSkillChatColOnMe); return; }   // :3606 — без гейта записи
			if (!Sk) { return; }
			if (R.Damage == 0)               { AsdaSkillDamageChat(GI, 20010, { SkName }, {}, AsdaChatColHitOnMe()); }
			else if (R.HitFlags & 0x08)      { AsdaSkillDamageChat(GI, 20248, { Attacker, SkName }, { R.Damage }, AsdaChatColHitOnMe()); }
			else if (bImmuneHit)             { AsdaSkillDamageChat(GI, 20010, { SkName }, {}, AsdaChatColHitOnMe()); }
			else                             { AsdaSkillDamageChat(GI, 20241, { Attacker }, { R.Damage }, kAsdaSkillChatColOnMe); }
		};
		if (R.ApplyFlag == 1 && R.Damage != 0)
		{
			// ★★★ T1: стиль зависит от ИСТОЧНИКА записи (см. `Damage4102TextStyle` / `SkillDamageTextStyle`).
			//   · 4102      → `combatFloatingDamageText(damageType = 0)`: **7** бью я · **15** бьют меня
			//                 · **9** наблюдатель. По флагам не ветвится (поля флагов в записи нет).
			//   · 5257/5270 → `Combat_ShowDamageFeedback@0xA80390`: крит 4/0xC, ветка бита3 — 8,
			//                 IMMUNE — 5 со словом, обычный урон 3 (бью я) / 0xB (жертва я и наблюдатель).
			const int32 Style = R.bFrom4102
				? Damage4102TextStyle(bOurCast, bVictimIsMe)
				: SkillDamageTextStyle(R.HitFlags, R.Damage, bOurCast, bVictimIsMe);
			// Слово «IMMUNE» вместо числа — свойство ветки `&0x20`, которая есть только у 5257/5270.
			const bool bImmune = !R.bFrom4102 && ((R.HitFlags & 0x04) == 0) && ((R.HitFlags & 0x08) == 0)
				&& ((R.HitFlags & 0x20) != 0);
			// ★ 17.10.2026 (Ф5) [ОРИГИНАЛ]: по персонажу — «бью я / бьют меня / (прок) бьёт моя выбранная цель» или режим 1
			//   (combat_pvp.c:1603 `combatFloatingDamageText`, :3461 `Combat_ShowDamageFeedback`).
			const bool bConcernSk = bOurCast || bVictimIsMe || GetSelectedTargetUserIdx() == R.CasterIdx;
			if (FT && ShouldShowDamageText(bConcernSk)) { FT->SpawnOverActor(Style, bImmune ? TEXT("IMMUNE") : FString::FromInt(R.Damage), Victim); }
			ASDA_CTRACE_IF(AsdaCtWorld::Users(R.CasterIdx, R.TargetIdx), EAsdaCT::Damage, TEXT("УРОН НАВЫКА %d (%s) кастера uidx=%d%s по игроку uidx=%d%s: урон %d%s, флаги 0x%02X, HP после по серверу %d — в HP через ApplyVitalsDamage"),
				R.SkillId, R.bFrom4102 ? TEXT("4102") : TEXT("5257/5270"), R.CasterIdx, bOurCast ? TEXT(" (я)") : TEXT(""), R.TargetIdx, bVictimIsMe ? TEXT(" (я)") : TEXT(""),
				R.Damage, bImmune ? TEXT(" IMMUNE") : ((!R.bFrom4102 && (R.HitFlags & 0x04)) ? TEXT(" КРИТ") : TEXT("")), R.HitFlags, R.HpAfter);
			ASDA_CTRACE_IF(AsdaCtWorld::Users(R.CasterIdx, R.TargetIdx), EAsdaCT::DmgText, TEXT("цифра «%s» стиль %d над игроком uidx=%d (навык %d) — %s"), bImmune ? TEXT("IMMUNE") : *FString::FromInt(R.Damage), Style,
				R.TargetIdx, R.SkillId, (FT && ShouldShowDamageText(bConcernSk)) ? TEXT("показана") : TEXT("скрыта режимом показа"));
			// ★★★ ЧАТ-СТРОКА КАНАЛА 4102 (`combat_pvp.c:1616-1624`). Её печатает ТОЛЬКО ветка «бью я /
			//   бьют меня»; у наблюдателя (стиль 9) эталон чат не трогает вовсе.
			//     · бью я    → нотис 20013, цвет −1        (белый);
			//     · бьют меня→ нотис 20241, цвет −33517    (D3DCOLOR 0xFFFF7D13, оранжевый — `AsdaChatColHitOnMe`).
			//   Слоты у обоих одни: строка 0 = имя ЖЕРТВЫ (`thunk_getFieldAt1056(thisChar)`, а `thisChar`
			//   в этой цепочке — именно объект-жертва), число 0 = величина урона.
			if (R.bFrom4102 && (bOurCast || bVictimIsMe))
			{
				const int32 NoticeId = bVictimIsMe ? 20241 : 20013;
				const FColor NoticeCol = bVictimIsMe ? AsdaChatColHitOnMe() : UAsdaChatSubsystem::ColWhite();
				FString VictimName = GetNameSafe(Victim);
				if (const FAsdaNetPlayerEntry* VPE = NetPlayers.Find(R.TargetIdx)) { VictimName = VPE->Name; }
				else if (bVictimIsMe) { VictimName = GetSelfCharName(); }
				// `CChat_AddTokenizedLines(…, color, 10000, 0)` — дорожка A, TTL 10 000 мс.
				AsdaCombatChat::Line(GetGameInstance(), NoticeId, { VictimName }, { R.Damage }, NoticeCol);   // ★ R5: общая строка боя
			}
			SkillPvpChat(bImmune);   // ★ 31.10.2026 [ИСПРАВЛЕНИЕ] строка чата 5257/5270 по персонажу
			// ★ 30.10.2026 Т2a — правило `Combat_ShowDamageFeedback` (см. блок вздрагивания выше).
			if (!R.bFrom4102 && (bOurCast || bVictimIsMe) && ((R.HitFlags & 0x04) || !(R.HitFlags & 0x28)))
			{
				const UAsdaSkillDatabase* SkDb = GetGameInstance()->GetSubsystem<UAsdaSkillDatabase>();
				const bool bCritOnMe = (R.HitFlags & 0x04) && !bOurCast && bVictimIsMe;   // :3619 — записи навыка не требует
				if (bCritOnMe || (SkDb && SkDb->Get(R.SkillId))) { SkillFlinch(TEXT("урон навыка (Combat_ShowDamageFeedback)")); }
			}
			// ★★★ HP — только через ЕДИНЫЙ ИСТОЧНИК ИСТИНЫ (закон: плашку напрямую не трогаем).
			Net->ApplyVitalsDamage(R.TargetIdx, R.Damage, TEXT("урон навыка"));
			UE_LOG(LogAsdaWorld, Log,
				TEXT("[навык] урон по игроку userIdx=%d: навык %d, кастер uidx=%d, урон=%d ")
				TEXT("(HP после по серверу %d), флаги=0x%02X, источник=%s, стиль %d"),
				R.TargetIdx, R.SkillId, R.CasterIdx, R.Damage, R.HpAfter, R.HitFlags,
				R.bFrom4102 ? TEXT("4102") : TEXT("5257/5270"), Style);
		}
		// ★ Промах по ПЕРСОНАЖУ. ★ 31.10.2026 (ревизия А3) — у эталона два источника, различаются битом 0 записи:
		//   · бит 0 = 0 — ветка промаха ТИКА (`CSkillCast_UpdateMeleeHit` skill_buff.c:14225…, `Skill_TickSingleTargetHit` :15490):
		//     «MISS» видит бьющий-я (+ строка 20010 белым), остальные — только при режиме «показывать всё»; жертве строки НЕТ;
		//   · бит 0 = 1 и урон 0 (нулевая доля многоударного) — `Combat_ShowDamageFeedback@0xA80390` ветка «урон 0»
		//     (combat_pvp.c:3755-3795): бью я — «MISS» + 20010 белым; бьют меня — «MISS» + 20010 цветом −33517.
		else if (bShowMissText)
		{
			const bool bHitBit = (R.HitFlags & 0x01) != 0;
			if (FT && ShouldShowDamageText(bOurCast || (bVictimIsMe && bHitBit))) { FT->SpawnOverActor(0x5, TEXT("MISS"), Victim); }   // ★ Ф5
			if (bOurCast || bHitBit) { SkillPvpChat(false); }   // ★ 31.10.2026 [ИСПРАВЛЕНИЕ] жертве при промахе тика строки нет
			ASDA_CTRACE_IF(AsdaCtWorld::Users(R.CasterIdx, R.TargetIdx), EAsdaCT::Damage, TEXT("навык %d кастера uidx=%d по игроку uidx=%d%s: ПРОМАХ (MISS стиль 5, флаги 0x%02X)"),
				R.SkillId, R.CasterIdx, R.TargetIdx, bVictimIsMe ? TEXT(" (я)") : TEXT(""), R.HitFlags);
			UE_LOG(LogAsdaWorld, Log, TEXT("[навык] ПРОМАХ по игроку userIdx=%d: навык %d, кастер uidx=%d (флаги=0x%02X)"),
				R.TargetIdx, R.SkillId, R.CasterIdx, R.HitFlags);
		}
	}
}

void UAsdaWorldSubsystem::OnSkillCastFaceTarget(int32 CasterUserIdx, int32 SkillCode,
	const TArray<UAsdaNetSubsystem::FAsdaSkillCastRec>& Recs)
{
	// ★ 17.10.2026 (Ф1, проект §3.5): каст навыка снимает цикл замаха кастера (эталон: состояние 24) — его записи ленты по
	//   обычным ударам сокращаются до назначенного ответом попадания или снимаются. Свой замах уже прерван заявкой навыка
	//   (`UAsdaAttackRhythmSubsystem`), здесь — любой кастер, тем же правилом.
	ASDA_CTRACE_UID(CasterUserIdx, EAsdaCT::Skill, TEXT("каст навыка %d кастером uidx=%d: записей целей %d%s"), SkillCode, CasterUserIdx, Recs.Num(),
		Recs.IsEmpty() ? TEXT("") : TEXT(" — цикл обычного замаха кастера прерывается (записи ленты сокращаются)"));
	if (!Recs.IsEmpty()) { CutImpactEntries(CasterUserIdx, TEXT("навык")); }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!Net || !W || Recs.Num() == 0) { return; }

	// Кастер: свой павн либо чужой игрок из реестра (тот же выбор, что у замаха в OnCombatAnim).
	const bool bOurs = AsdaSelf::IsSelfUserIdx(Net, CasterUserIdx);   // E2
	AAsdaPlayerCharacter* Caster = nullptr;
	if (bOurs) { Caster = Cast<AAsdaPlayerCharacter>(LocalPlayerActor); }
	else if (FAsdaNetPlayerEntry* PE = NetPlayers.Find(CasterUserIdx)) { Caster = PE->Actor.Get(); }
	if (!Caster) { return; }

	// ── П33, шаг 1: накопитель `modeMask` по ВСЕМ записям пакета (эталон network.c:44848-44852).
	uint16 Acc = 0;
	for (const UAsdaNetSubsystem::FAsdaSkillCastRec& R : Recs)
	{
		if (R.TargetIdx < 0) { continue; }                  // сентинел −1 — записи нет
		Acc |= (R.TargetKind == 1) ? 2 : 1;
	}
	if (Acc == 0)
	{
		// ★ `case 0` пакета 5270 (`Recv_SkillCastIdle@0xF4D9C0`, network.c:44714-44720): маска := 0 и
		//   ВСЕ 20 слотов целей (`effectSlots[100].field_14 + i`) сбрасываются в −1. У порта список
		//   целей — одно поле `AttackFaceTarget`, его и гасим; `SkillFaceUntilS` не трогаем (в эталоне
		//   состояние снимает свой отсчёт). ⚠ В 5257 ветка `default` маску НЕ сбрасывает — расхождение
		//   недостижимо: там `modeMask == 0` возможен только при ПУСТОМ списке записей, а с пустым
		//   списком мы вышли выше.
		Caster->TargetModeMask = 0;
		Caster->AttackFaceTarget = nullptr;
		UE_LOG(LogAsdaWorld, Verbose,
			TEXT("[навык] навык %d: в пакете нет записей с целью — маска и цель сброшены ")
			TEXT("(Recv_SkillCastIdle case 0)"), SkillCode);
		return;
	}

	// Цель = ПЕРВАЯ запись (эталон: `targetWords[0]`, остальные записи только копятся в массивы).
	const UAsdaNetSubsystem::FAsdaSkillCastRec& R0 = Recs[0];
	if (R0.TargetIdx < 0) { return; }
	// ── П33, шаг 2: `switch (modeMask)` — новая сохранённая маска и «можно ли перебить цель».
	const uint16 PrevMask = Caster->TargetModeMask;
	// «Прежняя цель уже выбрана» = эталонное `SLOWORD(pChar->targetRenderId) >= 0`.
	const bool bHasPrevTarget = Caster->AttackFaceTarget.IsValid();
	uint16 NewMask = PrevMask;
	bool bMayRetarget = true;
	switch (Acc)
	{
	case 1:                                                 // только сущности
		NewMask = 1;
		bMayRetarget = true;
		break;
	case 2:                                                 // только персонажи
		NewMask = 8;
		// `if (pChar->effectSlots[112].field_40)` — гейт «этот персонаж локальный игрок».
		bMayRetarget = bOurs ? !bHasPrevTarget : true;
		break;
	case 3:                                                 // смешанный пакет
		if (PrevMask != 0)
		{
			NewMask = (PrevMask & 8) ? 8 : 1;
			bMayRetarget = (NewMask == 8)
				? (bOurs ? !bHasPrevTarget : true)          // ветка `&8`: тот же гейт локального игрока
				: !bHasPrevTarget;                          // ветка `&1`: только при пустой цели
		}
		else
		{
			NewMask = (R0.TargetKind == 1) ? 8 : 1;
			bMayRetarget = true;
		}
		break;
	default:                                                // 0 — записей с целью нет
		return;
	}
	Caster->TargetModeMask = NewMask;
	if (!bMayRetarget && AsdaSkillFaceTargetEtalonOn())
	{
		// ★ 31.10.2026 [ОРИГИНАЛ]: цель не перебита — но состояние 24/14 всё равно ставится (`CChar_SetState(pChar, 24)` / 14), и
		//   покадровый доворот `objAI_FaceTargetByUserIdx` ведёт к УЖЕ заданной цели (у своего — цель навыка с заявки). Порт здесь
		//   выходил без доворота: персонаж выпускал навык, стоя как стоял.
		if (AActor* Keep = Caster->AttackFaceTarget.Get(); Keep && Keep != Cast<AActor>(Caster))
		{
			Caster->SkillFaceUntilS = W->GetTimeSeconds() + 2.0;   // сторож состояния 24/14, точный срок поставит показ навыка
			Caster->FaceGameDir(AsdaCoord::UeToGame2D(Keep->GetActorLocation()) - Caster->GetGamePosXZ());
			ASDA_CTRACE_UID(CasterUserIdx, EAsdaCT::Skill, TEXT("навык %d: цель кастера uidx=%d не перебита (маска %u) — доворот к прежней цели %s"),
				SkillCode, CasterUserIdx, NewMask, *GetNameSafe(Keep));
		}
	}
	if (!bMayRetarget)
	{
		UE_LOG(LogAsdaWorld, Verbose,
			TEXT("[навык] разворот: навык %d, маска %u — цель УЖЕ выбрана, эталон её не перебивает ")
			TEXT("(Recv_SkillCast case %u)"), SkillCode, NewMask, Acc);
		return;
	}

	// ★ Аудит №3 (B6): развилка «(вид, индекс) → актор» — через ЕДИНУЮ точку `ActorByTarget`.
	//   Здесь `R0.TargetKind` — СЫРОЙ байт `rec[0]` 29-байтной записи 5257/5270 (`Recv_SkillCast`),
	//   поэтому перевод в вид идёт через `AsdaTargetKindFromHitRec`: 1 = ПЕРСОНАЖ (ветка `modeMask & 8`,
	//   индекс = userNum), иначе СУЩНОСТЬ (ветка `modeMask & 1`, индекс = renderId). Доказательство —
	//   `skillParseQuickSlotEntry@0xA869F0` + `CSkillCast_ApplyAoEHitEffects@0xA75EF0` (см. заголовок);
	//   та же пара значений уже используется строкой выше при выборе `NewMask` (8 против 1).
	AActor* Tgt = ActorByTarget(AsdaTargetKindFromHitRec(R0.TargetKind), R0.TargetIdx);
	if (!Tgt)
	{
		UE_LOG(LogAsdaWorld, Verbose,
			TEXT("[навык] разворот: цель записи (kind=%d idx=%d) в мире не найдена — курс не трогаем ")
			TEXT("(эталон сохраняет прежний dir)"), R0.TargetKind, R0.TargetIdx);
		return;
	}
	if (Tgt == Cast<AActor>(Caster))
	{
		// Само-каст: у эталона вектор нулевой ⇒ `return` без разворота. Ровно это и делаем.
		UE_LOG(LogAsdaWorld, Verbose,
			TEXT("[навык] разворот: навык %d — само-каст, курс не меняется (эталон: dirLen == 0)"), SkillCode);
		return;
	}

	AAsdaPlayerCharacter* CasterCh = Caster;
	CasterCh->AttackFaceTarget = Tgt;
	CasterCh->SkillFaceUntilS = W->GetTimeSeconds() + 2.0;   // сторож состояния 24/14 (§13.6)
	// Первый доворот — прямо в кадре пакета (дальше держит Tick павна), как у замаха соседа.
	const FVector TL = Tgt->GetActorLocation();
	CasterCh->FaceGameDir(AsdaCoord::UeToGame2D(TL) - CasterCh->GetGamePosXZ());   // ★ Аудит №3 (B1)
	UE_LOG(LogAsdaWorld, Log,
		TEXT("[навык] разворот кастера uidx=%d%s к цели «%s» (навык %d, запись kind=%d idx=%d)"),
		CasterUserIdx, bOurs ? TEXT(" (мой)") : TEXT(" (сосед)"), *GetNameSafe(Tgt),
		SkillCode, R0.TargetKind, R0.TargetIdx);
}

// ★ 4205: сервер сбросил боевое состояние → выход из боевой стойки (эталон netTradeCancelResetCharState).
void UAsdaWorldSubsystem::OnCombatStanceReset(int32 UserIdx)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	if (!Net) { return; }
	ASDA_CTRACE_UID(UserIdx, EAsdaCT::State, TEXT("4205 боевой покой для uidx=%d%s (ритм сервера %s)"), UserIdx, Net->IsSelfUserIdx(UserIdx) ? TEXT(" (я)") : TEXT(""),
		UAsdaAttackRhythmSubsystem::IsEnabled() ? TEXT("вкл — перевзвод стойки, если не занят") : TEXT("выкл — выход из стойки"));
	// ★★ 15.10.2026 (ревизия PORT-RHYTHM СРЕДН-1): эталон `netTradeCancelResetCharState@0xF4A0D0` (network.c:44065-44088)
	//   ищет персонажа по userIdx — ЛЮБОГО, не только своего — и, если тот в состоянии 0, ставит
	//   `CChar_SetState(…, 0, 0, 20)`: боевой покой с перевзводом 10-секундного таймера (`fAutoActionStartTime`).
	//   Соседу порт раньше не делал ничего.
	// ★★ 15.10.2026, РЕШЕНИЕ ВЛАДЕЛЬЦА: и для СВОЕГО — как в оригинале: вход в боевой покой (подтип 20), через 10 с без боя —
	//   мирная стойка (таймер контроллера `CombatStanceUntil`, тот же 10 с). Прежде порт трактовал 4205 у своего как ВЫХОД
	//   из стойки — это было противоположно эталону. Откат (вместе со всем ритмом): `asda.Combat.ServerRhythm 0`.
	if (!Net->IsSelfUserIdx(UserIdx))
	{
		if (!UAsdaAttackRhythmSubsystem::IsEnabled()) { return; }
		if (AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(FindPlayerActor(UserIdx)))
		{
			if (!Ch->IsAttackAnimActive() && !Ch->IsSkillAnimActive() && !Ch->IsMoving()) { Ch->RearmCombatStance(); }
		}
		return;
	}
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
	{
		if (!UAsdaAttackRhythmSubsystem::IsEnabled()) { PC->ExitCombatStance(); return; }   // прежнее поведение порта
		AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
		// Эталон: только в состоянии 0 (`!field_8`) — не в замахе, не в навыке, не в ходьбе.
		if (Ch && !Ch->IsAttackAnimActive() && !Ch->IsSkillAnimActive() && !Ch->IsMoving())
		{
			Ch->RearmCombatStance();
			PC->CombatStanceUntil = FPlatformTime::Seconds() + PC->CombatStanceHold;
		}
	}
}

// ══ ★★★ 26.10.2026 (bowfx) СОСТОЯНИЯ СВОЕГО УДАРА И ТИКИ УРОНА ПО ВРЕМЕНИ — разбор у объявлений (AsdaWorldSubsystem.h) ══════
// ★ Ключ `asda.Skill.OwnHitStates`: 1 — эталонный `Skill_ApplyEffectResult` (состояния слотов buff0/1/2 своего удара по мобу);
//   0 — прежний порт (состояние на мобе появлялось только из 4102/4013).
static ASDA_DEV_CVAR(int32, CVarOwnHitStates, TEXT("asda.Skill.OwnHitStates"), 1,
	TEXT("Свой удар навыком по мобу: 1 = как оригинал (состояния buff0/1/2 при бите 0x02 записи — значок и эффект сразу), 0 = прежний порт"),
	ECVF_Default);
// ★ Ключ `asda.Dot.DeadClaim8226`: 1 — эталон: моб мёртв по серверу, а остаток урона по времени ещё есть — гашение эффектов и
//   заявка 8226 (MD5-конверт без секрета, `[i16 свой userNum][i16 renderId]`); 0 — заявку не слать (только гашение).
static ASDA_DEV_CVAR(int32, CVarDotDeadClaim, TEXT("asda.Dot.DeadClaim8226"), 1,
	TEXT("Урон по времени по мёртвому мобу: 1 = как оригинал (заявка 8226 «моб умер от моего урона по времени»), 0 = не слать"),
	ECVF_Default);

// ★ 07.11.2026 [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] Ключ `asda.Dot.OwnChatLineOnly`: строка 20013 «Вы нанесли» у тика по мобу — только своего кастера.
//   Эталон (`Skill_Buff_TickAll`, skill_buff.c:39110) выводит цифру и строку одной веткой `свой == кастер || режим показа == 1`, то есть
//   при режиме 1 называет чужой тик своим уроном. У оригинала режим по умолчанию 0, в порту — 1 (запрос владельца 17.10), поэтому
//   игрок в городе видел в чате чужой урон по далёкому мобу. Цифра по-прежнему следует режиму показа. 0 — как эталон.
static ASDA_DEV_CVAR(int32, CVarDotOwnChatLineOnly, TEXT("asda.Dot.OwnChatLineOnly"), 1,
	TEXT("Тик урона по времени по мобу: 1 = строка в чат только у своего кастера (чужой тик — лишь цифра по режиму показа), 0 = как оригинал (при режиме 1 строка и у чужого)"),
	ECVF_Default);

void UAsdaWorldSubsystem::ApplyOwnHitSkillStates(AAsdaMobActor* M, const UAsdaNetSubsystem::FAsdaSkillHitRec& R)
{
	if (!M || CVarOwnHitStates.GetValueOnGameThread() == 0) { return; }
	if ((R.HitFlags & 0x02) == 0) { return; }   // `(pCaster_2[effectSlotIndex + 104194] & 2) != 0`
	// Шапка эталона: цель мертва (`pTarget[284] <= 0 || state == 10`) — эффекты гаснут, состояний не будет.
	if (M->IsDead() || M->bServerDead) { return; }
	UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx) { return; }
	const uint8 Stacks[3] = { (uint8)(R.BuffStacks & 0xFF), (uint8)((R.BuffStacks >> 8) & 0xFF), (uint8)((R.BuffStacks >> 16) & 0xFF) };
	// ★ 04.11.2026 (status_effect_expiry): the cast-result moment — a state the server ended after it is not re-added (see the gate).
	const int32 N = Fx->ApplyMobSkillChain(M->NetRenderId, R.SkillId, R.CasterIdx, Stacks, /*bRequireDuration=*/true, /*bOnlySlot0=*/false,
		/*CastRecvAtS=*/(R.RecvAtS > 0.0) ? R.RecvAtS : -1.0);
	ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, R.CasterIdx), EAsdaCT::Fx, TEXT("свой удар навыком %d по %s: состояния слотов (бит 0x02) — наложено %d, стеки %d/%d/%d"),
		R.SkillId, *AsdaCtWorld::Mob(M->NetRenderId, M), N, Stacks[0], Stacks[1], Stacks[2]);
	// ⚠ [ПОРТ] подтипы урона состояний в этой функции эталона (3 — прямой урон из изученного навыка, 6 — локальная заготовка
	//   слота урона по времени, 7 — процент от HP) не переносятся: подтип 6 сразу же перекрывает 6079 (`skillInitDotSlot`
	//   переписывает слот целиком, а первый тик наступает не раньше шага 3000 мс — 6079 в живых журналах приходит через ~2 с
	//   после заявки), подтип 3 есть только у навыка 1032 («Урон жнеца», решение владельца 25.09 — без команды не браться),
	//   подтип 7 — только у 913. Разбор — `Reports/skill_targets/RULES.md` §«Состояния».
}

void UAsdaWorldSubsystem::OnDotTick(FAsdaDotTickEvent& E)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { E.bStopSlot = true; return; }
	UAsdaFloatingTextSubsystem* FT = GI->GetSubsystem<UAsdaFloatingTextSubsystem>();
	const bool bCasterIsMe = E.CasterUserIdx >= 0 && Net->IsSelfUserIdx(E.CasterUserIdx);
	auto ChatLine = [GI](int32 LocId, const FString& Name, int32 Value, const FColor& Col)
	{
		AsdaCombatChat::Line(GI, LocId, { Name }, { Value }, Col);   // ★ R5: общая строка боя; `CChat_AddTokenizedLines(…, 10000, 0)`
	};
	if (E.bMob)
	{
		// ── Моб: `Skill_Buff_TickAll@0xF83A00` (skill_buff.c:39097-39150).
		AAsdaMobActor* M = FindNetMobByRid(E.Key);
		if (!M) { E.bStopSlot = true; return; }   // сущности нет — слот гибнет вместе с ней
		// Шапка: `ent[284] <= 0 || state == 10` — мёртвый моб не тикает (эффекты гаснут); слот гасим.
		if (M->IsDead()) { E.bStopSlot = true; return; }
		const int32 Was = M->CurHp;
		M->CurHp = FMath::Max(0, M->CurHp - E.Damage);   // `CClass_SubtractStatClamped(ent, тик, 1)` — и под льдом (a3 = 1)
		NoteMobHpShown(E.Key, E.Damage);                 // ★ 05.11.2026 (asda.Combat.LiftLargeGap): учёт показанного урона
		UpdateMobHpBar(M);
		// Цифра и строка — «кастер я» (`Target_GetSelectedUserIdx(LocalPlayer)` — МИСНЕЙМ: свой userNum) или режим показа 1.
		if (bCasterIsMe || GetDamageTextMode() == 1)
		{
			if (FT && ShouldShowDamageText(bCasterIsMe)) { FT->SpawnOverActor(0x7, FString::FromInt(E.Damage), M); }   // стиль 7
			if (bCasterIsMe || CVarDotOwnChatLineOnly.GetValueOnGameThread() == 0)   // ★ 07.11: чужой тик в чат не идёт — разбор у ключа
			{
				ChatLine(20013, M->MobName, E.Damage, UAsdaChatSubsystem::ColWhite());                                 // нотис 20013, цвет −1
			}
		}
		ASDA_CTRACE_IF(AsdaCtWorld::Ours(E.Key, E.CasterUserIdx), EAsdaCT::Damage, TEXT("ТИК урона по времени по %s: состояние %d (навык %d), урон %d, HP полосы %d → %d / %d, остаток %d, кастер uidx=%d%s"),
			*AsdaCtWorld::Mob(E.Key, M), E.StateId, E.SkillId, E.Damage, Was, M->CurHp, M->MaxHp, E.Remaining, E.CasterUserIdx, bCasterIsMe ? TEXT(" (я)") : TEXT(""));
		UE_LOG(LogAsdaWorld, Log, TEXT("[урон по времени] моб rid=%d '%s': состояние %d, тик %d, HP %d → %d, остаток %d (кастер uidx=%d)"),
			E.Key, *M->MobName, E.StateId, E.Damage, Was, M->CurHp, E.Remaining, E.CasterUserIdx);
		// Хвост эталона: остаток > 0, а моб по серверу мёртв (`ent[284] <= 0`) или помечен трупом (0x20) — погасить все эффекты
		//   состояний, послать 8226, остаток в 0. ⚠ [ПОРТ] `ent[284]` (HP из пакета) у порта отдельным полем не хранится: берём
		//   флаг трупа `bServerDead` (тот же 0x20) и HP полосы ≤ 0 после вычета — сервер проверяет сам (`Process_ClientMonsterDead`:
		//   `HP <= 0`), ложная заявка ничего не меняет.
		if (E.Remaining > 0 && (M->bServerDead || M->CurHp <= 0))
		{
			if (UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>()) { Fx->ClearMobEffects(E.Key); }   // `charStopAllBuffAndSlotEffects`
			if (CVarDotDeadClaim.GetValueOnGameThread() != 0 && Net->IsSelfUserNumKnown())
			{
				const bool bSent = Net->SendDotDeadClaim8226(E.Key);   // `CNetwork_Send(…, 20, 8226, …)`: MD5(16) + 4 б
				UE_LOG(LogAsdaWorld, Log, TEXT("[урон по времени] моб rid=%d мёртв по серверу при остатке %d — 8226 %s"), E.Key, E.Remaining, bSent ? TEXT("отправлен") : TEXT("НЕ ушёл"));
				ASDA_CTRACE_IF(AsdaCtWorld::Ours(E.Key, E.CasterUserIdx), EAsdaCT::Send, TEXT("ЗАЯВКА 8226 «моб rid=%d умер от урона по времени» (остаток %d) — %s"), E.Key, E.Remaining, bSent ? TEXT("ушла") : TEXT("не ушла"));
			}
			E.bStopSlot = true;
		}
		return;
	}
	// ── Игрок: `charDot_TickAll@0xA2BFD0` → `CCharacter_TakeDamage(this, тик, 2)` (entity_char.c:25847).
	AActor* Victim = ActorByUserIdx(E.Key);
	if (!Victim) { E.bStopSlot = true; return; }
	UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>();
	// Под оледенением/окаменением (состояния 0/1, type 0) урона нет вовсе — ни HP, ни цифры.
	if (Fx && (Fx->FindByTypeSub(E.Key, 0, 0) || Fx->FindByTypeSub(E.Key, 1, 0))) { return; }
	Net->ApplyVitalsDamage(E.Key, E.Damage, TEXT("тик урона по времени"));   // `HP -= урон`, кламп 0, HUD у своего
	// `combatFloatingDamageText(жертва, её userNum, тик, вид 0)` (combat_pvp.c:1603-1632): жертва — я → 15 + 20241;
	//   «наложил я» (`жертва[59126]` = кастер 6079) → 7 + 20013; иначе при режиме показа 1 → 9 без строки.
	const bool bVictimIsMe = Net->IsSelfUserIdx(E.Key);
	FString VictimName = GetNameSafe(Victim);
	if (const FAsdaNetPlayerEntry* VPE = NetPlayers.Find(E.Key)) { VictimName = VPE->Name; }
	else if (bVictimIsMe) { VictimName = GetSelfCharName(); }
	if (bVictimIsMe || bCasterIsMe)
	{
		if (FT && ShouldShowDamageText(true)) { FT->SpawnOverActor(bVictimIsMe ? 0x0F : 0x07, FString::FromInt(E.Damage), Victim); }
		ChatLine(bVictimIsMe ? 20241 : 20013, VictimName, E.Damage, bVictimIsMe ? AsdaChatColHitOnMe() : UAsdaChatSubsystem::ColWhite());
	}
	else if (GetDamageTextMode() == 1)
	{
		if (FT && ShouldShowDamageText(false)) { FT->SpawnOverActor(0x09, FString::FromInt(E.Damage), Victim); }
	}
	ASDA_CTRACE_IF(AsdaCtWorld::Users(E.CasterUserIdx, E.Key), EAsdaCT::Damage, TEXT("ТИК урона по времени по игроку uidx=%d%s: состояние %d (навык %d), урон %d, остаток %d, кастер uidx=%d"),
		E.Key, bVictimIsMe ? TEXT(" (я)") : TEXT(""), E.StateId, E.SkillId, E.Damage, E.Remaining, E.CasterUserIdx);
}

// ★ Отряд — разбор у объявления. Дев-ростер (`asda.Party.Set`) остаётся подменой для проверок без сервера.
bool UAsdaWorldSubsystem::IsPartyMember(int32 UserIdx) const
{
	if (PartyUserIdxs.Contains(UserIdx)) { return true; }
	const UAsdaPartySubsystem* P = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	return P && P->IsMemberUserNum(UserIdx);
}

bool UAsdaWorldSubsystem::IsInParty() const
{
	if (PartyUserIdxs.Num() > 0) { return true; }
	const UAsdaPartySubsystem* P = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	return P && P->IsInParty();
}

int32 UAsdaWorldSubsystem::GetPeerCampId(int32 UserIdx) const
{
	const FAsdaNetPlayerEntry* PE = NetPlayers.Find(UserIdx);
	if (!PE || PE->CharId == 0) { return -1; }
	const FIntPoint* F = FactionByCharId.Find(PE->CharId);
	return F ? F->X : -1;
}
