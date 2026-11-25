// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Damage.cpp — ★ 23.10.2026 часть UAsdaWorldSubsystem, вынесенная из AsdaWorldSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaWorldSubsystem.plan.json).
//   Лента попаданий (impact ledger), прогноз и «пол» HP моба, смертельная отметка, OnCombatHitAux, навыки по цели, PvP-серии.
//   #include — те же, что у AsdaWorldSubsystem.cpp, в том же порядке; общее — AsdaWorldSubsystemInternal.h.
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
// ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
// ★ 22.10.2026: окна при своём 4055/4056 — список эталона вместо CloseAll   ★ R3-c: комментарий снятого включения "UI/AsdaEntryWindows.h" (части им не пользуются)
// ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»   ★ R3-c: комментарий снятого включения "Text/AsdaLang.h" (части им не пользуются)
// ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой   ★ R3-c: комментарий снятого включения "UI/AsdaNoticeColors.h" (части им не пользуются)
// ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)   ★ R3-c: комментарий снятого включения "Resources/AsdaAssetPaths.h" (части им не пользуются)
// ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (путь DA_WalkGrid)   ★ R3-c: комментарий снятого включения "World/AsdaMapRegistry.h" (части им не пользуются)
// кости/attach-матрицы оружия «в руке ↔ за спиной»   ★ R3-c: комментарий снятого включения "Characters/AsdaCharAssembly.h" (части им не пользуются)
// HERO_NOTICE_WIN — только чужой редкий дроп (6131)   ★ R3-c: комментарий снятого включения "UI/AsdaHudSubsystem.h" (части им не пользуются)
// ★ сырая запись skill_info (.sif +0x48 = buff0 состояния отражения)   ★ R3-c: комментарий снятого включения "Items/AsdaItemDbRows.h" (части им не пользуются)
// ★ единые читалки сырой записи (DataHex)   ★ R3-c: комментарий снятого включения "AsdaBytes.h" (части им не пользуются)
// подошва цели: низ ВИДИМОЙ геометрии (TargetSoleZ)   ★ R3-c: комментарий снятого включения "Components/MeshComponent.h" (части им не пользуются)
// ★ плашка имени — тоже UMeshComponent, но не тело   ★ R3-c: комментарий снятого включения "Components/WidgetComponent.h" (части им не пользуются)
// ★ F7 (дедуп №2, В3): общие признаки «логин» / «загрузка»   ★ R3-c: комментарий снятого включения "World/AsdaWorldKind.h" (части им не пользуются)
// FAsdaHeldTables — удержание таблиц спауна (15.09.2026, dt_gc_hold)   ★ R3-c: комментарий снятого включения "AsdaHeldTables.h" (части им не пользуются)
// наземный лут (порт DropItemMgr, loot_system_re.md)   ★ R3-c: комментарий снятого включения "World/AsdaLootActor.h" (части им не пользуются)
// питомцы (6120, pet_system_re.md)   ★ R3-c: комментарий снятого включения "World/AsdaPetActor.h" (части им не пользуются)
// ★ компаньон-хранитель «ангел» (6156, soul_guardian_companion_re.md)   ★ R3-c: комментарий снятого включения "World/AsdaSoulGuardianActor.h" (части им не пользуются)
// ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP   ★ R3-c: комментарий снятого включения "AsdaFxNames.h" (части им не пользуются)
// ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты   ★ R3-c: комментарий снятого включения "Resources/AsdaMaterialNames.h" (части им не пользуются)
// RT эмблемы клана — это UTexture-наследник   ★ R3-c: комментарий снятого включения "Engine/TextureRenderTarget2D.h" (части им не пользуются)
// FAsdaClanFlagData::FromWire (флаг клана из 6542)   ★ R3-c: комментарий снятого включения "World/AsdaClanFlag.h" (части им не пользуются)
// DecodeCp1251 (имена монстров в cp1251)   ★ R3-c: комментарий снятого включения "Login/AsdaLoginData.h" (части им не пользуются)
#include "AsdaWorldSubsystem_Damage_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Damage*.cpp

// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] цвет −33517 (объявление и разбор — AsdaWorldSubsystemInternal.h).
int32 GAsdaChatHitColorFix = 1;
static FAutoConsoleVariableRef CVarAsdaChatHitColorFix(TEXT("asda.Chat.HitColorFix"), GAsdaChatHitColorFix,
	TEXT("1 — строка чата «по тебе урон» цветом эталона −33517 = 0xFFFF7D13 (оранжевый); 0 — прежний порт 0xFF7B93."), ECVF_Default);

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] СТРОКИ УРОНА НАВЫКА В СИСТЕМНЫЙ ЧАТ (баг игроков «ударил навыком — в чате ничего»).
//   Записи 5257/5270 эталон показывает двумя функциями, и КАЖДАЯ, кроме цифры, пишет строку в чат (`CChat_AddTokenizedLines(…, цвет,
//   10000, 0)`, гейт — запись навыка есть, `CSkillMgr_GetSkillRecord`):
//     · цель-СУЩНОСТЬ `CharFx_ShowSkillDamageText@0xA7FC00` (effects_particles.c:11440-11566) — ТОЛЬКО «бью я»:
//         крит &4 → 20247 {имя цели, имя навыка}{урон} цвет −9053441 (0xFF75DAFF); &8 → 20248 {…}{урон} белый;
//         &0x20 → 20010 {имя навыка} белый («IMMUNE»); иначе → 20249 {имя цели, имя навыка}{урон} белый; урон 0 → 20010 белый («MISS»);
//     · цель-ПЕРСОНАЖ `Combat_ShowDamageFeedback@0xA80390` (combat_pvp.c:3580-3795) — «бью я» и «бьют меня»:
//         бью я: 20247 0xFF75DAFF / 20248 белый / 20010 белый / 20249 0xFF75DAFF / промах 20010 белый (строка 0 — имя ЖЕРТВЫ);
//         бьют меня: крит 20240 {имя бьющего}{урон} −55021 (0xFFFF2913) БЕЗ гейта записи; &8 20248 {бьющий, навык}{урон} −33517
//         (0xFFFF7D13); &0x20 20010 {навык} −33517; иначе 20241 {бьющий}{урон} −55021; промах 20010 {навык} −33517.
//   Наблюдатель строк не получает ни в одной из функций. Порт рисовал только цифры, строк не было вовсе.
//   Ключ `asda.Skill.DamageChatLines`: 1 — как эталон; 0 — прежний порт (строк нет).
int32 GAsdaSkillDamageChatLines = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaSkillDamageChatLines,
	TEXT("asda.Skill.DamageChatLines"),
	GAsdaSkillDamageChatLines,
	TEXT("1 — строки урона навыка (5257/5270) в системный чат, как эталон (20247/20248/20249/20010; по мне 20240/20241); 0 — прежний порт без строк."),
	ECVF_Default);

// ★★★ 17.10.2026 (ревизия Ф2, ВАЖНО 4): запись СНАРЯДА закрывается по ФАКТУ показа (`NoteImpactShown` снял её известный урон), а не
//   по оценке момента попадания: план соседа с очередью отрезков + полёт подходили к 3.8–4 с, и `FlushLedger`/закрытие по оценке
//   снимали запись, пока стрела ещё летела, — полоса уходила вниз без цифры, потом цифра вычитала урон ещё раз. Запас после оценки
//   — `kProjectileShownGraceS`; потолок взвода снаряда — на `kProjectileWindowExtraS` длиннее.
static constexpr double kProjectileShownGraceS = 0.5;   // сколько ждать показа после оценки попадания снаряда (с)
static constexpr float  kProjectileWindowExtraS = 1.0f; // надбавка к потолку взвода записи снаряда (с)

static constexpr double kCorpseHitGraceS = 0.1;   // ★ 18.10.2026: удар по трупу позже смертельного больше чем на столько (с) — гасится
static constexpr double kLethalCeilExtraS = 1.0;  // ★ ревизия 18.10 (п.1): потолок ожидания показа смертельного удара сверх плана (с)
static constexpr float  kLethalBackupGraceS = 0.15f; // ★ ревизия 18.10 (п.1): запасной срок смерти — позже плана смертельного удара на столько (с)
// ★★★ 18.10.2026 (живой бой 20:50, журнал `AsdaCombat_20261018-205020`, rid=161/164: полоса прыгала ВВЕРХ) «ПОЛ» ПОЛОСЫ HP МОБА.
//   Корень: сервер списывает очередь ударов ПОСТЕПЕННО (≈2 значения за обмен), а свой замах показывает все значения пакета
//   (`StepCount` за ответ) — полоса уходила НИЖЕ истинного HP, и любой следующий снимок (4017 flags 3, попадание ответа «полоса в 0
//   при живом сервере») её поднимал: 17 → 24, 0 → 24. Истина — 4029 @4 = HP − накопитель NpcServer (`mob-hp-two-packets-mismatch`).
//   Правило (обе роли одинаково — 4029 сервер шлёт всему сектору):
//   • пол полосы = последнее HP из 4029 по мобу (любой атакующий); 4027 (сырое, без вычета накопителя), 4015 @25, 4017, 4013/4019 —
//     полом НЕ являются, разве что они ниже пола (урон других источников) — тогда пол опускается до них; до первого 4029 пол 0;
//   • на кадре удара полоса = max(полоса − урон, пол) — цифра та же (из очереди), полоса упирается в пол;
//   • пол ниже полосы, а непоказанных ударов не хватает — полоса сразу к «пол + непоказанный урон»;
//   • в бою (есть непоказанные удары или удар/ответ по мобу моложе 3 с) полоса только опускается: подъём из любого канала откладывается
//     до выхода моба из боя и применяется значением последнего серверного снимка (плавно — сглаживание полосы); оживление и
//     респаун трупа — сразу, как прежде.
//   0 — как до правки (полоса за очередью ниже сервера, подъёмы снимками видны).
static ASDA_DEV_CVAR(int32, CVarAsdaHpFloorFromServer,
	TEXT("asda.Combat.HpFloorFromServer"), 1,
	TEXT("1 (по умолчанию) = пол полосы HP моба — последнее HP из 4029 (HP − накопитель): удары очереди опускают полосу не ниже ")
	TEXT("пола; в бою полоса только опускается, подъём (реген/снимки) — после 3 с тишины значением последнего снимка; оживление и ")
	TEXT("респаун — сразу. 0 = как до 18.10 20:50 (полоса уходит ниже сервера, следующий снимок поднимает её)."),
	ECVF_Default);
static constexpr double kMobCombatQuietS = 3.0;   // ★ 18.10.2026 (пол HP): моб вне боя — столько тишины после удара/ответа (с)
// ★★★ 05.11.2026 (bug-mob-hp-zero, жалоба игроков «босс Кайя: полоса HP 0, а она жива, ходит и бьёт»). Доказано журналом игрока LogDrop
//   20261101-233556 (23:08:54–23:09:20, Кайя тип 286 rid=0): навык Кайи 3 «Mon_SelfHeal» → 8013 HP 28224 (полное лечение); запись
//   навыка 5257, посчитанная сервером ДО лечения (HpAfter 11160), легла на полосу после него; прогноз HP (`MobHpForecast`) был
//   скользящим МИНИМУМОМ, который каждый новый ответ 4029/5257 продлевал, — 11160 держался 25 с, все снимки сервера 28224…12245
//   «ОТБРОШЕН»; правило «в бою полоса только опускается» ждало 3 с тишины, которых в бою с боссом нет; локальный вычет довёл
//   полосу до 0 при серверных 12278. Лечат все 70 боссов (Monster.ai +141 бит 4 BATTLE_HEAL: HP < 50 % → +20 % max, NpcServer
//   sub_43C340/sub_446A50) и навыки-лечения мобов (шахматные, Кайя); GameServer sub_484A90 рассылает 8013, свою копию HP не трогая.
//   Три ключа (все 1 по умолчанию, 0 — как до 05.11):
static ASDA_DEV_CVAR(int32, CVarAsdaForecastNoExtend,
	TEXT("asda.Combat.ForecastNoExtend"), 1,
	TEXT("1 (по умолчанию) = прогноз HP моба продлевается только БОЛЕЕ НИЗКИМ значением (минимум живёт удержание от своего прихода); ")
	TEXT("0 = как до 05.11: любой ответ продлевал старый минимум — после лечения босса полоса не поднималась весь бой."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaHealEpoch,
	TEXT("asda.Combat.HealEpoch"), 1,
	TEXT("1 (по умолчанию) = лечение моба 8013 сбрасывает прогноз/пол/отложенный подъём, а запись навыка, посчитанная сервером до ")
	TEXT("лечения, показывает цифру, но полосу не опускает; 0 = как до 05.11."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaLiftLargeGap,
	TEXT("asda.Combat.LiftLargeGap"), 1,
	TEXT("1 (по умолчанию) = в бою серверный снимок выше полосы больше, чем показанный за 3 с урон (+1 % max), поднимает полосу сразу ")
	TEXT("(полоса заведомо неверна); 0 = как до 05.11: подъём только после 3 с тишины."),
	ECVF_Default);
static constexpr double kRecentShownS = 3.0;   // ★ 05.11.2026: окно учёта показанного урона (с) — на столько сервер может отставать от полосы

// ★★★ «HP И СМЕРТЬ ЭТОГО МОБА ЖДУТ КАДРА УДАРА». Единая точка для ВСЕХ каналов обнуления HP
//   (4015 Recv_CombatHit, 4017 flags=0 Recv_CombatHitAux, боевой HP цели): true, если по цели уже
//   принята, но ещё не выпущена смертельная запись каста (5257/5270/6159/6160, `HpAfter <= 0`).
//   ⚠ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕНО — см. шапку `asda.Skill.HoldMobDeath` наверху файла и отчёт §28:
//   гейт питается ПОКАЗОМ навыка, а показ в порту есть только у локального игрока, поэтому у
//   кастера и у наблюдателя моб падал в РАЗНЫЕ моменты (до ~1.4 с). Эталон смерть от показа не
//   зависит вовсе: `Recv_SkillCast@0xF4E370` по записи с `HpAfter <= 0` ставит цели только МЕТКУ
//   «обречён» (`sub_92C3AD(ent, 32)` = бит 0x20), а валит цель обнуление HP — из пакета сервера
//   либо локальным вычитанием урона на кадре удара (`CClass_SubtractStatClamped@0xF8A4F0` с
//   клампом в 0), после чего покадровый `entityListUpdateFade@0xF78660` видит `i[284] <= 0`.
//   Метку 0x20 порт теперь ставит ровно там же — `MarkDoomedByCastRecs` ниже.
bool UAsdaWorldSubsystem::IsMobDeathHeldByPendingHit(int32 Rid) const
{
	if (!GAsdaHoldMobDeath) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaSkillFxSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
	return Fx && Fx->HasPendingLethalHit(/*TargetKind=*/0, Rid);
}

// ══ ★★★ МЕТКА «ОБРЕЧЁН» ИЗ ЗАПИСЕЙ КАСТА — 1:1 `Recv_SkillCast@0xF4E370` ═════════════════════
//   Эталон (network.c:44847-44869), ветка `header[0] == 1`, для КАЖДОЙ записи цели-сущности:
//   ```
//   if ( *(int *)((char *)&targetWords[5] + 29 * i) <= 0 )        // это поле `+11` = HpAfter
//   {
//     targetRenderId = *(__int16 *)((char *)targetWords + 29 * i);// поле `+1` = rid цели
//     targetEnt = Entity_FindByRenderId_thunk(g_pCharManager, targetRenderId);
//     if ( targetEnt )
//     {
//       if ( targetRenderId != -1 )
//         *(_DWORD *)(targetEnt + 6996) = casterUserIdx;          // КТО убил (атрибуция)
//       sub_92C3AD(targetEnt, 32);                                // бит 0x20 = «обречён»
//       *(_DWORD *)(targetEnt + 600)  = 1176256512;               // = 10000.0f, сторожевой таймер
//       *(_WORD  *)(targetEnt + 7372) = 0;                        // взвести защёлку нотиса смерти
//     }
//   }
//   ```
//   Что даёт бит 0x20 у эталона: цель перестаёт атаковаться боевым циклом
//   (`Player_UpdateAutoActionState@0xA249A0`, entity_char.c:24041 — `(v397[121] & 0x20) == 0`;
//   тот же гейт в skill_buff.c:4508 и 4556) и снимается с подсветки. СМЕРТЬ бит 0x20 НЕ ставит.
//   Снимается бит в `Recv_CombatHitAux@0xF33B60` при `flags == 0` (network.c:38476).
//
//   ⇒ ЭТО И ЕСТЬ ОТВЕТ ОРИГИНАЛА на «сервер уже засчитал кил, но труп ещё не лёг»: не задержка
//   смерти, а мгновенный запрет бить обречённую цель. Порт метку уже имел (`bServerDead`), но
//   ставил её ТОЛЬКО из ответа на обычный замах (4029 `Recv_CastState`, `TargetHp == 0`) — из
//   записей НАВЫКА она не ставилась вовсе, хотя у эталона это одна и та же метка.
//   Данные броадкастятся всему сектору (`SendPacket_ClippingSectorInAllUser@0x41E4F0`), поэтому
//   метка встаёт ОДИНАКОВО у кастера и у наблюдателя — рассинхрона она не создаёт.
void UAsdaWorldSubsystem::MarkDoomedByCastRecs(int32 CasterUserIdx, int32 /*SkillCode*/,
	const TArray<UAsdaNetSubsystem::FAsdaSkillCastRec>& Recs)
{
	for (const UAsdaNetSubsystem::FAsdaSkillCastRec& R : Recs)
	{
		if (R.TargetKind == 1) { continue; }                 // 1 = игрок; эталонная ветка — сущности
		if (R.TargetIdx < 0) { continue; }                   // сентинел −1 — записи нет
		// ★★★ П30 (T4, 2026-08-11): условие эталона — РОВНО `<= 0`, БЕЗ исключения −1.
		//   `network.c:44857`: `if ( *(int *)((char *)&targetWords[5] + 29 * i) <= 0 )` — поле `+11`
		//   читается ЗНАКОВЫМ int, и 0xFFFFFFFF (−1) в это условие ПОПАДАЕТ.
		//   ⚠ Прежний отсев `== -1` был перенесён сюда из ДРУГОГО обработчика: исключение «−1 = HP не
		//     сообщён» живёт только в 4102 (`netApplySkillHitResultPacket@0xF4F8A0`, network.c:45252:
		//     `hpAfter <= 0 && hpAfter != -1 && !(ent[121] & 0x20)`). В 5257/5270 его нет.
		//   Метка безопасна и обратима: она НЕ убивает цель (только запрещает новые замахи и снимает
		//   подсветку) и снимается приходом `Recv_CombatHitAux` с `flags == 0` (network.c:38476).
		if (R.HpAfter > 0) { continue; }
		TWeakObjectPtr<AAsdaMobActor>* Ex = NetMobs.Find(R.TargetIdx);
		AAsdaMobActor* M = Ex ? Ex->Get() : nullptr;
		if (!M)
		{
			// ⚠ ЧЕСТНАЯ ДЫРА (П30, вторая половина). Эталон ищет цель `Entity_FindByRenderId_thunk`
			//   по ОБЩЕМУ списку сущностей, куда входят и NPC (`Entity_FindByRenderId@0xF772D0`,
			//   entity_char.c:54055 — один связный список, тип не различается). У порта реестры
			//   разведены: `NetMobs` и `NetNpcs`. Метку НЕ ставим NPC не по недосмотру: у
			//   `AAsdaNpcActor` нет ни `bServerDead`, ни HP, ни боевого цикла — заводить поле, которого
			//   никто не читает, запрещено (`no-simplifications-stubs`). Факт фиксируем в лог.
			if (const TWeakObjectPtr<AAsdaNpcActor>* Np = NetNpcs.Find(R.TargetIdx))
			{
				UE_LOG(LogAsdaWorld, Verbose,
					TEXT("[навык] rid=%d — это NPC ('%s'), эталон пометил бы его обречённым; у порта ")
					TEXT("у NPC нет боевого состояния — метка не ставится (честная дыра П30)"),
					R.TargetIdx, *GetNameSafe(Np->Get()));
			}
			continue;
		}
		if (M->IsDead() || M->bServerDead) { continue; }
		M->bServerDead = true;              // sub_92C3AD(targetEnt, 32)
		// ⚠ ЧЕСТНАЯ ДЫРА: `*(_DWORD *)(targetEnt + 6996) = casterUserIdx` НЕ портируем. Поле `+6996`
		//   (`pSelf[1749]`) — это «на кого сущность смотрит/за кем идёт»: его читает покадровый
		//   `charUpdateActionStateTick` (entity_char.c:56913 и 56977) в состояниях 3/13/14/15/16/17 и
		//   доворачивает/ведёт моба к этому персонажу. В порту у `AAsdaMobActor` такого поля и такой
		//   ветки поворота НЕТ вовсе (ссылок в срезе нет) — это отдельная система «моб смотрит на
		//   цель», не часть смерти. Заводить пустое поле ради строчки нельзя (no-simplifications-stubs).
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[навык] моб rid=%d '%s' ПОМЕЧЕН ОБРЕЧЁННЫМ записью каста (HP после = %d, кастер uidx=%d) ")
			TEXT("— эталон Recv_SkillCast@0xF4E370: sub_92C3AD(ent,32). Новых замахов по нему не начинаем; ")
			TEXT("смерть придёт обнулением HP (пакет сервера либо кадр удара)"),
			R.TargetIdx, *M->MobName, R.HpAfter, CasterUserIdx);
		ASDA_CTRACE_IF(AsdaCtWorld::Ours(R.TargetIdx, CasterUserIdx), EAsdaCT::Skill, TEXT("%s: метка «обречён» (bServerDead) по записи каста кастера uidx=%d (HP после %d) — новых замахов по цели нет, смерть придёт обнулением HP"),
			*AsdaCtWorld::Mob(R.TargetIdx, M), CasterUserIdx, R.HpAfter);
		// Подсветку/цель гасит покадровый `TargetHighlightTick` (он уже читает `bServerDead`).
	}
}

// ══ ★★★ ПРОГНОЗ HP ИЗ ЗАПИСИ КАСТА (5257/5270/6159/6160) ═════════════════════════════════════
//   Полное обоснование — в шапке объявления (.h) и в отчёте §27. Коротко: поле `+11` записи это
//   `HP(mon+30853) − накопитель(mon+34376) − этот урон`, посчитанное NpcServer НАПЕРЁД; само поле
//   HP моба сервер обнулит позже (вшитые 2000 мс окна накопителя в `CMonster::ProcessState`).
//   Все снимки HP, что придут в этом окне (4015 @+25, 4017 @+92), заведомо СТАРШЕ прогноза.
void UAsdaWorldSubsystem::NoteServerHpForecast(int32 /*CasterUserIdx*/, int32 /*SkillCode*/,
	const TArray<UAsdaNetSubsystem::FAsdaSkillCastRec>& Recs)
{
	if (GAsdaLegacyMobDeath) { return; }                   // дословный эталон — прогноза нет
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	const double Now = W->GetTimeSeconds();
	for (const UAsdaNetSubsystem::FAsdaSkillCastRec& R : Recs)
	{
		if (R.TargetKind == 1) { continue; }               // 1 = игрок; прогноз ведём только по мобам
		if (R.TargetIdx < 0 || R.HpAfter < 0) { continue; } // −1 = «сервер HP не сообщил»
		// ★ 05.11.2026 (ключ `asda.Combat.HealEpoch`): запись, посчитанная сервером до лечения 8013, прогнозом не служит.
		if (IsPreHealRecord(FindNetMobByRid(R.TargetIdx), R.HpAfter, R.Damage)) { continue; }
		FAsdaMobHpForecast& F = MobHpForecast.FindOrAdd(R.TargetIdx);
		// Несколько записей по одной цели за каст (многоударные, площадные) — берём САМЫЙ НИЗКИЙ.
		const bool bAlive = F.UntilS > Now && F.Hp >= 0;   // прежний прогноз ещё держится
		// ★ 05.11.2026 (ключ `asda.Combat.ForecastNoExtend`): значение не ниже живого прогноза его НЕ продлевает.
		if (bAlive && R.HpAfter >= F.Hp && CVarAsdaForecastNoExtend.GetValueOnGameThread() != 0) { continue; }
		F.Hp     = bAlive ? FMath::Min(F.Hp, R.HpAfter) : R.HpAfter;
		F.UntilS = Now + MobHpForecastHoldS();
	}
	// Подметание протухших прогнозов (карта не растёт бесконечно).
	for (auto It = MobHpForecast.CreateIterator(); It; ++It)
	{
		if (It->Value.UntilS <= Now) { It.RemoveCurrent(); }
	}
}

// true — снимок HP от сервера УСТАРЕЛ: он выше собственного свежего прогноза сервера по этой цели.
// ★ См. объявление. Окно живёт ровно до запланированного момента попадания и ни секундой
//   дольше: если ответ сервера почему-то не придёт, здоровье снова начнёт применяться сразу.
// ★ Ключ отката правки «смерть монстра ждёт кадра удара» (см. OnTargetHp). 0 = смерть и снятие
//   выделения применяются сразу по приходу пакета, как было до 2026-08-25.
#if ASDA_DEV_CONSOLE   // ★ R3-c чистка: в Shipping этот ключ — константа из AsdaWorldSubsystem_Damage_Private.h
ASDA_DEV_CVAR(int32, CVarAsdaDeathOnHitFrame,
	TEXT("asda.Combat.DeathOnHitFrame"), kAsdaDeathOnHitFrameDefault,
	TEXT("1 = смерть монстра от НАШЕГО замаха показывается на кадре удара; 0 = сразу по приходу пакета."),
	ECVF_Default);
#endif // ASDA_DEV_CONSOLE

// ★★★ СБРОС СВОЕЙ ОЧЕРЕДИ ПРИ ЗАЯВКЕ НА УДАР — 1:1 эталон.
//   `netSabzil_SendActionRaw` (`network.c:5219-5223`) сразу после отправки 4026 обнуляет счётчики
//   очереди у ЛОКАЛЬНОГО игрока: `field118 = 1; field11C = 0`. Именно поэтому у оригинала очередь
//   не «протухает»: каждый вход в бой начинает её заново.
//   Порт не чистил её никогда — хвост прошлого боя уезжал в следующий и сдвигал всю серию.
void UAsdaWorldSubsystem::ClearOwnDamageQueue(const TCHAR* Why)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->IsSelfUserNumKnown()) { return; }
	const int32 Self = (int32)Net->SelfUserNum;
	TArray<int32>* Buf = DmgBuffers.Find(Self);
	if (!Buf || Buf->Num() == 0)
	{
		ASDA_CTRACE_V(EAsdaCT::Queue, TEXT("своя очередь ударов (uidx=%d) пуста — сбрасывать нечего (%s)"), Self, Why ? Why : TEXT("заявка на удар"));
		return;
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[бой] очередь ударов сброшена (%s): было %d значени(й)"),
		Why ? Why : TEXT("заявка на удар"), Buf->Num());
	ASDA_CTRACE(EAsdaCT::Queue, TEXT("своя очередь ударов (uidx=%d) СБРОШЕНА (%s): было %s"), Self, Why ? Why : TEXT("заявка на удар"), *AsdaCtWorld::Queue(*Buf));
	Buf->Reset();
}

bool UAsdaWorldSubsystem::IsOurSwingInFlightAgainst(int32 Rid) const
{
	if (Rid < 0) { return false; }
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return false; }
	// Ключ отката `HitFrameAllChannels 0`: прежнее одиночное окно (как до 17.10).
	if (CVarAsdaHitFrameAllChannels.GetValueOnGameThread() == 0) { return Rid == PendingImpactRid && W->GetTimeSeconds() < PendingImpactUntilS; }
	const FMobImpactLedger* L = ImpactLedgers.Find(Rid);   // лента моба
	if (!L) { return false; }
	const double Now = W->GetTimeSeconds();   // мир-время опроса
	for (const FAsdaImpactEntry& E : L->Pending)
	{
		const bool bSwing = E.Kind == EAsdaImpactKind::Attack || E.Kind == EAsdaImpactKind::Projectile;   // запись замаха, не навыка
		if (bSwing && E.AtS > Now && AsdaLedgerIsSelf(GetGameInstance(), E.Attacker)) { return true; }
	}
	return false;
}

double UAsdaWorldSubsystem::MobHpForecastHoldS() const
{
	// Ключ отката `asda.Combat.HitFrameAllChannels 0` — прежний срок (до ленты попаданий удержания 4.5 с не было).
	return CVarAsdaHitFrameAllChannels.GetValueOnGameThread() != 0 ? kMobHpForecastHoldS : kMobHpForecastHoldLegacyS;
}

bool UAsdaWorldSubsystem::IsImpactLedgerOn(bool bSelfAttacker) const
{
	if (CVarAsdaHitFrameAllChannels.GetValueOnGameThread() == 0) { return false; }
	return bSelfAttacker || CVarAsdaObserverSync.GetValueOnGameThread() != 0;
}

bool UAsdaWorldSubsystem::HasPendingImpacts(int32 Rid) const
{
	const FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	return L && L->Pending.Num() > 0;
}

bool UAsdaWorldSubsystem::IsLedgerDeathPending(int32 Rid) const
{
	const FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	return L && L->bDeathPending;
}

void UAsdaWorldSubsystem::ArmImpact(int32 Rid, int32 Attacker, EAsdaImpactKind Kind, float DelayS, bool bCommitted, bool bLethal,
	int32 KnownDamage, bool bOutcomeKnown, const TCHAR* Why)
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W || Rid < 0) { return; }
	const double Now = W->GetTimeSeconds();                                                // мир-время взвода
	// ★ Ревизия Ф2 (ВАЖНО 4): у снаряда потолок взвода длиннее (план соседа с очередью отрезков + полёт).
	const FMobImpactLedger* LW = ImpactLedgers.Find(Rid);
	const float WindowMax = kImpactWindowMaxS + ((Kind == EAsdaImpactKind::Projectile) ? kProjectileWindowExtraS : 0.f)
		+ ((Kind == EAsdaImpactKind::Skill && LW) ? (float)LW->HoldExtraS : 0.f);   // ★ 10.11.2026: надбавка отложенной записи
	const double Until = Now + (double)FMath::Clamp(DelayS, 0.05f, WindowMax);            // момент попадания этого взвода
	FMobImpactLedger& L = ImpactLedgers.FindOrAdd(Rid);
	FAsdaImpactEntry* E = L.Pending.FindByPredicate([Attacker, Kind](const FAsdaImpactEntry& X) { return X.Attacker == Attacker && X.Kind == Kind; });
	if (!E)
	{
		E = &L.Pending.AddDefaulted_GetRef();
		E->Attacker = Attacker; E->Kind = Kind; E->ArmedS = Now; E->AtS = Until;
	}
	else if (bCommitted && bLethal && IsDeathAtLastPendingHit())
	{
		// ★★★ 18.10.2026 (ключ `asda.Combat.DeathAtLastPendingHit`): смертельный ответ назначает ТОЧНЫЙ срок (метка смертельного удара
		//   `ArmLethalDeath` — потолок запасного таймера). «Только вперёд» здесь уводило смертельную запись на конец прохода клипа
		//   (оценка по отправке 4028 / прежняя формула), и серверная смерть 4017 ждала её «до попадания +3.50 с» (живой журнал 18.10).
		//   ★ Ревизия 18.10 (п.2): запись уже смертельная — повторный смертельный ответ срок не отодвигает (меньший из двух).
		const double WasAtS = E->AtS;       // журнал: было
		const bool bWasLethal = E->bLethal; // журнал: запись уже была смертельной
		E->AtS = bWasLethal ? FMath::Min(E->AtS, Until) : Until;
		ASDA_CTRACE_IF(AsdaCtWorld::Ours(Rid, Attacker), EAsdaCT::Death, TEXT("лента rid=%d: смертельная запись атакующего uidx=%d — срок попадания было +%.3f → стало +%.3f с (%s)"),
			Rid, Attacker, WasAtS - Now, E->AtS - Now, bWasLethal ? TEXT("повторный смертельный ответ: меньший из двух") : TEXT("точный срок смертельного удара, не «только вперёд»"));
	}
	else
	{
		E->AtS = FMath::Max(E->AtS, Until);   // только вперёд: попадание не раньше прежней оценки
	}
	if (bCommitted)
	{
		E->CommittedAtS = FMath::Max(E->CommittedAtS, Until);
		E->bCommittedOutcomeUnknown = !bOutcomeKnown;
		E->bOutcomeUnknown = !bOutcomeKnown;
		E->PendingDamage += FMath::Max(0, KnownDamage);
		E->bLethal |= bLethal;
	}
	else
	{
		E->bOutcomeUnknown = true;   // заявка ушла, ответа нет: урон и смертельность неизвестны
	}
	L.LastAttackS.Add(Attacker, Now);
	UE_LOG(LogAsdaWorld, Verbose, TEXT("[лента] rid=%d: запись атакующего %d (вид %d) до +%.2f с (%s%s%s, урон %d)"), Rid, Attacker, (int32)Kind,
		E->AtS - Now, Why ? Why : TEXT("?"), bCommitted ? TEXT(", ответ") : TEXT(", заявка"), E->bLethal ? TEXT(", СМЕРТЕЛЬНАЯ") : TEXT(""), E->PendingDamage);
	ASDA_CTRACE_IF(AsdaCtWorld::Ours(Rid, Attacker), EAsdaCT::HitFrame, TEXT("лента %s: запись атакующего uidx=%d (%s) — попадание через %.2f с (%s, %s%s, непоказанный урон %d%s); записей у моба %d"),
		*AsdaCtWorld::Mob(Rid, FindNetMobByRid(Rid)), Attacker,
		(Kind == EAsdaImpactKind::Attack) ? TEXT("удар") : (Kind == EAsdaImpactKind::Projectile) ? TEXT("снаряд") : (Kind == EAsdaImpactKind::Skill) ? TEXT("навык") : TEXT("4102"),
		E->AtS - Now, Why ? Why : TEXT("?"), bCommitted ? TEXT("ответ") : TEXT("заявка без ответа"), E->bLethal ? TEXT(", СМЕРТЕЛЬНАЯ") : TEXT(""),
		E->PendingDamage, E->bOutcomeUnknown ? TEXT(", исход неизвестен") : TEXT(""), L.Pending.Num());
	LedgerReconcileHp(Rid, Why);
	RearmLedgerTimer(Rid);
}

void UAsdaWorldSubsystem::NoteLedgerSnapshot(int32 Rid, int32 Hp, bool bAllowNumber)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	if (!L || Hp < 0 || L->Pending.Num() == 0 || L->bDeathPending) { return; }
	ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("лента %s: серверный снимок ответа HP=%d ЗАПОМНЕН (было %d) — на полосу ляжет сверкой «только вниз» на попаданиях (%d запис.)"),
		*AsdaCtWorld::Mob(Rid, FindNetMobByRid(Rid)), Hp, L->ServerHp, L->Pending.Num());
	L->ServerHp = Hp;
	L->bServerHpNumber = bAllowNumber;
	LedgerReconcileHp(Rid, TEXT("снимок ответа"));
	RearmLedgerTimer(Rid);
}

void UAsdaWorldSubsystem::NoteImpactShown(int32 Rid, int32 Attacker, int32 Damage)
{
	FMobImpactLedger* L = ImpactLedgers.Find(Rid);
	if (!L || Attacker < 0 || Damage <= 0) { return; }
	for (FAsdaImpactEntry& E : L->Pending)
	{
		if (E.Attacker != Attacker || (E.Kind != EAsdaImpactKind::Attack && E.Kind != EAsdaImpactKind::Projectile)) { continue; }
		E.PendingDamage = FMath::Max(0, E.PendingDamage - Damage);   // показанный урон больше не покрывает разницу со снимком
		ASDA_CTRACE_V(EAsdaCT::HitFrame, TEXT("лента rid=%d: попадание атакующего uidx=%d показано, урон %d вычтен из записи (непоказано осталось %d)"),
			Rid, Attacker, Damage, E.PendingDamage);
		break;
	}
	if (const AAsdaMobActor* M = FindNetMobByRid(Rid)) { L->ShownHp = M->CurHp; }
	LedgerUpdateHold(Rid);   // ★ Ф2 (ревизия Ф1 п.1а): показанный урон меняет «объяснимое записями» — удержание пересчитать
	RearmLedgerTimer(Rid);   // ★ ревизия Ф2 (ВАЖНО 4): показ снаряда закрывает его запись (`LedgerEntryCloseS`)
}

double UAsdaWorldSubsystem::LedgerEntryCloseS(const FAsdaImpactEntry& E)
{
	// Удар/снаряд с ещё не показанным известным уроном ждёт показа (запас после оценки); прочие — по оценке момента попадания.
	//   ★ Удар (не только снаряд): свой замах под серверными часами переставляет таймеры цифр (`RetimeOwnHits`) — фактический
	//   показ может разойтись с оценкой на десятки мс, а закрытая раньше показа запись — это тот же двойной вычет (ВАЖНО 4).
	const bool bWaitsShow = (E.Kind == EAsdaImpactKind::Projectile || E.Kind == EAsdaImpactKind::Attack) && E.PendingDamage > 0;
	return bWaitsShow ? E.AtS + kProjectileShownGraceS : E.AtS;
}

int32 UAsdaWorldSubsystem::ArmOwnHitTimer(AAsdaPlayerCharacter* Ch, float DelayS, TFunction<void()> Fn, int32 Rid)
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W || !Ch) { return 0; }
	FTimerManager& TM = W->GetTimerManager();
	OwnHitTimers.RemoveAll([&TM](const FAsdaOwnHitTimer& E) { return !TM.IsTimerActive(E.H); });   // отработавшие
	FAsdaOwnHitTimer& E = OwnHitTimers.AddDefaulted_GetRef();
	E.Fn = Fn;
	E.SwingGen = Ch->GetAttackSwingGen();   // ★ 18.10.2026 (ВАЖНО 3): таймер принадлежит этому замаху
	E.Id = NextOwnHitTimerId++;             // ★ 18.10.2026: номер для учёта поставленных ударов
	E.Rid = Rid;                            // ★ ревизия 18.10 (п.11): цель удара
	if (!Ch->ClipTargetFromDelay(DelayS, E.Serial, E.ClipPos)) { E.Serial = MIN_int32; }
#if ASDA_COMBAT_TRACE
	// ★ Журнал боя: кадр удара СВОЕГО замаха — плановое время постановки и фактическое срабатывание (значение и цель пишет сам Fn).
	{
		const double CtArmS = W->GetTimeSeconds();                                // мир-время постановки
		const double CtPlanS = CtArmS + (double)FMath::Max(0.001f, DelayS);      // плановый момент кадра удара
		const int32 CtGen = E.SwingGen;                                           // поколение замаха постановки
		const int32 CtSerial = E.Serial;                                          // номер прохода клипа (MIN_int32 — без привязки к клипу)
		const float CtClip = E.ClipPos;                                           // позиция кадра удара в клипе
		ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("свой кадр удара ПОСТАВЛЕН: через %.3f с (замах #%d, %s проход %d позиция клипа %.3f)"),
			FMath::Max(0.001f, DelayS), CtGen, (CtSerial == MIN_int32) ? TEXT("без привязки к клипу,") : TEXT("привязка к клипу:"),
			(CtSerial == MIN_int32) ? -1 : CtSerial, CtClip);
		TWeakObjectPtr<UWorld> CtW = W;                                           // мир — для фактического времени
		TWeakObjectPtr<AAsdaPlayerCharacter> CtCh = Ch;                           // свой персонаж — поколение замаха на срабатывании
		TM.SetTimer(E.H, FTimerDelegate::CreateLambda([Fn, CtPlanS, CtArmS, CtGen, CtW, CtCh]()
		{
			const UWorld* Wf = CtW.Get();
			const AAsdaPlayerCharacter* Cf = CtCh.Get();
			const double NowF = Wf ? Wf->GetTimeSeconds() : CtPlanS;
			ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("свой кадр удара СРАБОТАЛ: план +%.3f, факт +%.3f от постановки (расхождение %+.3f с), замах #%d%s"),
				CtPlanS - CtArmS, NowF - CtArmS, NowF - CtPlanS, CtGen,
				(Cf && Cf->GetAttackSwingGen() != CtGen) ? *FString::Printf(TEXT(" — таймер ПРОШЛОГО замаха (сейчас #%d)"), Cf->GetAttackSwingGen()) : TEXT(""));
			Fn();
		}), FMath::Max(0.001f, DelayS), false);
	}
#else
	TM.SetTimer(E.H, FTimerDelegate::CreateLambda([Fn]() { Fn(); }), FMath::Max(0.001f, DelayS), false);
#endif
	if (OwnRetimeChar.Get() != Ch)
	{
		if (AAsdaPlayerCharacter* Old = OwnRetimeChar.Get()) { Old->OnSegmentRateChanged.Remove(OwnRetimeHandle); }
		OwnRetimeHandle = Ch->OnSegmentRateChanged.AddUObject(this, &UAsdaWorldSubsystem::RetimeOwnHits);
		OwnRetimeChar = Ch;
	}
	return OwnHitTimers.Last().Id;   // ★ 18.10.2026: номер таймера (сам таймер `RetimeOwnHits` может завести заново)
}

// ★ R5-fix8 (14.11.2026): ключ правки «перестановка своих кадров удара не заводит заново уже наступивший таймер» (разбор и
//   причина — в `RetimeOwnHits`).
static ASDA_DEV_CVAR(int32, CVarAsdaOwnHitRetimeKeepDue,
	TEXT("asda.Combat.OwnHitRetimeKeepDue"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = смена скорости отрезка замаха не заводит заново таймер своего кадра удара, который уже ")
	TEXT("наступил (сработает в этом же тике): цифра встаёт в кадр удара клипа. 0 = как было: цифра на кадр позже."),
	ECVF_Default);

void UAsdaWorldSubsystem::RetimeOwnHits()
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	AAsdaPlayerCharacter* Ch = OwnRetimeChar.Get();
	if (!W || !Ch) { return; }
	FTimerManager& TM = W->GetTimerManager();
	for (int32 i = OwnHitTimers.Num() - 1; i >= 0; --i)
	{
		FAsdaOwnHitTimer& E = OwnHitTimers[i];
		if (!TM.IsTimerActive(E.H)) { OwnHitTimers.RemoveAt(i); continue; }
		if (E.Serial == MIN_int32) { continue; }
		// ★ 18.10.2026 (ревизия Ф4, ВАЖНО 3): таймер прошлого замаха (взвод нового после навыка/смены цели/убийства) не
		//   переставляется — срабатывает в свой исходный момент (ключ `asda.Combat.OwnHitSwingGuard`, разбор — у ключа).
		if (AAsdaPlayerCharacter::IsOwnHitSwingGuardOn() && E.SwingGen != Ch->GetAttackSwingGen()) { continue; }
		const float D = Ch->DelayToClipTarget(E.Serial, E.ClipPos);
		if (D < 0.f) { continue; }
		// ★★★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ]: кадр удара УЖЕ НАСТУПИЛ — таймер не трогаем (ключ `asda.Combat.OwnHitRetimeKeepDue`).
		//   Второй кадр удара отрезка стоит на его конце — там же, где точка отправки следующего удара (`OnAttackSendPoint`), и оба
		//   таймера приходятся на один тик. Если первым срабатывал таймер точки отправки, смена скорости отрезка звала эту
		//   перестановку, и таймер удара, который сработал бы следом В ЭТОМ ЖЕ тике, заводился заново на 0,001 с — а таймеры,
		//   заведённые внутри тика таймеров, срабатывают только на следующем: цифра вставала на кадр позже кадра удара клипа (в
		//   журнале — «СРАБОТАЛ (после перестановки): план +0.001, факт +0.010…0.015»), а когда точка отправки сама отставала от
		//   клипа на кадр — на два (`Combat.SelfTestObserverSync`, Ф2 «своя цифра на кадре удара клипа», 0,027 с при кадре 13,5 мс).
		//   Клип уже в точке удара (остаток 0) и таймер уже просрочен — он сработает в этом же тике таймеров сам.
		if (D <= KINDA_SMALL_NUMBER && TM.GetTimerRemaining(E.H) <= KINDA_SMALL_NUMBER)
		{
			if (CVarAsdaOwnHitRetimeKeepDue.GetValueOnGameThread() != 0) { ++DebugOwnRetimeKeptDue; continue; }
			++DebugOwnRetimeRearmedDue;
		}
		TFunction<void()> Fn = E.Fn;
#if ASDA_COMBAT_TRACE
		// ★ Журнал боя: смена скорости отрезка переставила свой кадр удара — было/стало, и фактическое срабатывание.
		{
			const double CtArmS = W->GetTimeSeconds();                            // мир-время перестановки
			const double CtPlanS = CtArmS + (double)FMath::Max(0.001f, D);       // новый плановый момент
			const int32 CtGen = E.SwingGen;                                       // поколение замаха таймера
			ASDA_CTRACE_V(EAsdaCT::HitFrame, TEXT("свой кадр удара ПЕРЕСТАВЛЕН (смена скорости отрезка): было через %.3f с, стало через %.3f с (замах #%d, проход %d позиция %.3f)"),
				TM.GetTimerRemaining(E.H), FMath::Max(0.001f, D), CtGen, E.Serial, E.ClipPos);
			TWeakObjectPtr<UWorld> CtW = W;                                       // мир — для фактического времени
			TM.SetTimer(E.H, FTimerDelegate::CreateLambda([Fn, CtPlanS, CtArmS, CtGen, CtW]()
			{
				const UWorld* Wf = CtW.Get();
				const double NowF = Wf ? Wf->GetTimeSeconds() : CtPlanS;
				ASDA_CTRACE(EAsdaCT::HitFrame, TEXT("свой кадр удара СРАБОТАЛ (после перестановки): план +%.3f, факт +%.3f (расхождение %+.3f с), замах #%d"),
					CtPlanS - CtArmS, NowF - CtArmS, NowF - CtPlanS, CtGen);
				Fn();
			}), FMath::Max(0.001f, D), false);
		}
#else
		TM.SetTimer(E.H, FTimerDelegate::CreateLambda([Fn]() { Fn(); }), FMath::Max(0.001f, D), false);
#endif
	}
}

int32 UAsdaWorldSubsystem::OwnPendingHitFrames(const AAsdaPlayerCharacter* Ch, int32 Rid) const
{
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W || !Ch) { return 0; }
	const FTimerManager& TM = W->GetTimerManager();
	int32 N = 0;   // кадров удара текущего замаха, ещё занятых непоказанными ударами по этой цели
	for (const FAsdaOwnHitTimer& E : OwnHitTimers)
	{
		// ★ Ревизия 18.10 (п.11): удары по прежней цели (смена цели без нового «взвода») кадры новой цели не занимают.
		if (E.SwingGen == Ch->GetAttackSwingGen() && (Rid < 0 || E.Rid == Rid) && TM.IsTimerActive(E.H)) { ++N; }
	}
	return N;
}

const FTimerHandle* UAsdaWorldSubsystem::PlannedHitTimer(const FAsdaPlannedHit& X) const
{
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	const FTimerManager& TM = W->GetTimerManager();
	if (X.OwnTimerId > 0)
	{
		const FAsdaOwnHitTimer* E = OwnHitTimers.FindByPredicate([&X](const FAsdaOwnHitTimer& O) { return O.Id == X.OwnTimerId; });
		return (E && TM.IsTimerActive(E->H)) ? &E->H : nullptr;
	}
	return TM.IsTimerActive(X.H) ? &X.H : nullptr;
}

bool UAsdaWorldSubsystem::IsPlannedHitPending(const FAsdaPlannedHit& X, float& OutS) const
{
	OutS = -1.f;
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return false; }
	// ★ Ревизия 18.10 (п.3): прерванная серия соседа — удар не будет показан (его таймер погасит сверка поколения).
	if (X.bGenGuard && PeerSeriesGen.FindRef(X.Attacker) != X.PeerGen) { return false; }
	if (const FTimerHandle* Th = PlannedHitTimer(X)) { OutS = W->GetTimerManager().GetTimerRemaining(*Th) + X.FlightS; return true; }
	const double Now = W->GetTimeSeconds();
	if (X.FlightS > 0.f && X.AtS > Now) { OutS = (float)(X.AtS - Now); return true; }   // снаряд летит; ближний с отработавшим таймером — показан
	return false;
}

void UAsdaWorldSubsystem::NotePlannedHit(int32 Rid, int32 Attacker, int32 Damage, FTimerHandle H, int32 OwnTimerId, float FlightS, float DelayS,
	uint32 Seq, bool bGenGuard, uint32 PeerGen)
{
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W || Rid < 0) { return; }
	const double Now = W->GetTimeSeconds();   // мир-время постановки
	TArray<FAsdaPlannedHit>& L = PlannedHits.FindOrAdd(Rid);
	// Показанные (таймер отработал, снаряд долетел по оценке) и прерванные удары больше не нужны.
	L.RemoveAll([this](const FAsdaPlannedHit& X) { float S = 0.f; return !IsPlannedHitPending(X, S); });
	FAsdaPlannedHit& E = L.AddDefaulted_GetRef();
	E.Attacker = Attacker; E.Damage = FMath::Max(0, Damage); E.H = H; E.OwnTimerId = OwnTimerId; E.FlightS = FMath::Max(0.f, FlightS);
	E.AtS = Now + (double)FMath::Max(0.f, DelayS);
	E.Seq = Seq; E.bGenGuard = bGenGuard; E.PeerGen = PeerGen;
}

bool UAsdaWorldSubsystem::LethalPlannedHitDelayS(int32 Rid, int32 Attacker, int32 BarHp, float& OutDelayS, int32& OutHits, uint32& OutSeq)
{
	OutDelayS = 0.f; OutHits = 0; OutSeq = 0;
	if (BarHp <= 0) { return true; }   // полоса уже 0 — смерть сразу
	TArray<FAsdaPlannedHit>* L = PlannedHits.Find(Rid);
	if (!L) { return false; }
	// До показа каждого непоказанного удара: таймер ещё ждёт — его остаток (перестановку `RetimeOwnHits` он уже включает) + полёт;
	//   снаряд уже вылетел — оценка прилёта. Удары прерванной серии соседа (фантомы) выбрасываются — ревизия 18.10, п.3.
	struct FDue { float S; int32 Dmg; bool bMine; uint32 Seq; };   // до показа (с), урон, удар этого атакующего, его номер
	TArray<FDue> Due;
	for (int32 i = L->Num() - 1; i >= 0; --i)
	{
		const FAsdaPlannedHit& X = (*L)[i];
		float S = -1.f;   // до показа (с)
		if (!IsPlannedHitPending(X, S)) { L->RemoveAt(i); continue; }
		Due.Add({ S, X.Damage, X.Attacker == Attacker, X.Seq });
	}
	// Порядок показа: по времени; при равенстве — по номеру постановки (FIFO).
	Due.Sort([](const FDue& A, const FDue& B) { return (A.S != B.S) ? A.S < B.S : A.Seq < B.Seq; });
	int64 Acc = 0;          // накопленный урон поставленных ударов всех ролей по порядку показа
	float LastMine = -1.f;  // последний удар этого атакующего (не хватило урона)
	uint32 LastSeq = 0;     // его номер
	for (const FDue& D : Due)
	{
		Acc += D.Dmg;
		if (!D.bMine) { continue; }
		++OutHits;
		LastMine = D.S; LastSeq = D.Seq;
		if (Acc >= (int64)BarHp) { OutDelayS = D.S; OutSeq = D.Seq; return true; }   // этот удар доводит полосу до 0
	}
	if (LastMine < 0.f) { return false; }   // поставленных ударов этого атакующего нет
	OutDelayS = LastMine; OutSeq = LastSeq;
	return true;
}

float UAsdaWorldSubsystem::ArmLethalDeath(AAsdaMobActor* M, int32 Attacker, uint32 Seq, float DeathDelayS, const TCHAR* Why)
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W || !M) { return DeathDelayS; }
	const int32 Rid = M->NetRenderId;   // моб смерти
	const double Now = W->GetTimeSeconds();
	const double PlanS = Now + (double)FMath::Max(0.f, DeathDelayS - 0.05f);   // плановый момент смертельного удара
	if (FAsdaLethalMark* Old = LethalMarks.Find(Rid); Old && !M->IsDead() && Old->PlanS <= PlanS + 1e-3)
	{
		// ★ Ревизия 18.10 (п.2): повторный смертельный ответ при живом мобе срок не отодвигает — меньший из двух.
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("%s: повторный смертельный ответ (%s) — срок прежний: удар №%u атакующего uidx=%d через %.3f с (новый был бы через %.3f с)"),
			*AsdaCtWorld::Mob(Rid, M), Why ? Why : TEXT("?"), Old->Seq, Old->Attacker, Old->PlanS - Now, PlanS - Now);
		return (float)FMath::Max(0.05, (Old->Seq > 0 ? Old->CeilS : Old->PlanS + 0.05) - Now);
	}
	if (FAsdaLethalMark* Old = LethalMarks.Find(Rid)) { W->GetTimerManager().ClearTimer(Old->Backup); }
	FAsdaLethalMark& K = LethalMarks.FindOrAdd(Rid);
	K.Attacker = Attacker; K.Seq = Seq; K.PlanS = PlanS;
	// Есть смертельный удар — смерть по его показу, таймер запасной (потолок `kLethalCeilExtraS`); нет — смерть по сроку.
	K.CeilS = (Seq > 0) ? Now + (double)DeathDelayS + kLethalCeilExtraS : Now + (double)DeathDelayS;
	const float BackupS = (Seq > 0) ? DeathDelayS + kLethalBackupGraceS : DeathDelayS;
	W->GetTimerManager().SetTimer(K.Backup, FTimerDelegate::CreateUObject(this, &UAsdaWorldSubsystem::OnLethalBackup, Rid), FMath::Max(0.001f, BackupS), false);
	ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("%s: смерть %s (%s): удар №%u атакующего uidx=%d через %.3f с, запасной срок через %.3f с (потолок %.3f с)"),
		*AsdaCtWorld::Mob(Rid, M), (Seq > 0) ? TEXT("— после показа смертельного удара") : TEXT("по сроку"), Why ? Why : TEXT("?"), Seq, Attacker,
		PlanS - Now, BackupS, K.CeilS - Now);
	return (float)FMath::Max(0.05, K.CeilS - Now);
}

void UAsdaWorldSubsystem::OnLethalBackup(int32 Rid)
{
	FAsdaLethalMark* K = LethalMarks.Find(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	AAsdaMobActor* M = FindNetMobByRid(Rid);
	if (!K || !W || !M || M->IsDead()) { return; }
	const double Now = W->GetTimeSeconds();
	if (K->Seq > 0 && Now + 0.001 < K->CeilS)
	{
		// Смертельный удар ещё впереди (перестановка таймеров своего замаха `RetimeOwnHits`, полёт) — ждём его, но не дальше потолка.
		if (const TArray<FAsdaPlannedHit>* L = PlannedHits.Find(Rid))
		{
			for (const FAsdaPlannedHit& X : *L)
			{
				float S = -1.f;   // до показа смертельного удара (с)
				if (X.Seq == K->Seq && IsPlannedHitPending(X, S))
				{
					const float Re = FMath::Min(S + kLethalBackupGraceS, (float)(K->CeilS - Now));
					ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("%s: запасной срок смерти — смертельный удар №%u ещё впереди (через %.3f с), срок перевзведён на %.3f с"),
						*AsdaCtWorld::Mob(Rid, M), K->Seq, S, Re);
					W->GetTimerManager().SetTimer(K->Backup, FTimerDelegate::CreateUObject(this, &UAsdaWorldSubsystem::OnLethalBackup, Rid), FMath::Max(0.001f, Re), false);
					return;
				}
			}
		}
	}
	ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("%s: %s — смерть (удар №%u)"), *AsdaCtWorld::Mob(Rid, M),
		(K->Seq == 0) ? TEXT("срок смерти по ответу наступил") : ((Now + 0.001 >= K->CeilS) ? TEXT("потолок запасного срока") : TEXT("смертельный удар показан не будет (прерван)")), K->Seq);
	ApplyLedgerDeath(M, TEXT("смертельный ответ (срок)"));
}

void UAsdaWorldSubsystem::OnPlannedHitShown(int32 Rid, uint32 Seq)
{
	if (Seq == 0) { return; }
	if (TArray<FAsdaPlannedHit>* L = PlannedHits.Find(Rid)) { L->RemoveAll([Seq](const FAsdaPlannedHit& X) { return X.Seq == Seq; }); }
	FAsdaLethalMark* K = LethalMarks.Find(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	AAsdaMobActor* M = FindNetMobByRid(Rid);
	if (!K || K->Seq != Seq || !W || !M || M->IsDead()) { return; }
	// ★ Ревизия 18.10 (п.1): смерть — ПОСЛЕ показа смертельного удара (+0.05 с: цифра и вычет полосы видны раньше трупа).
	ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("%s: смертельный удар №%u показан — смерть через 0.05 с"), *AsdaCtWorld::Mob(Rid, M), Seq);
	W->GetTimerManager().SetTimer(K->Backup, FTimerDelegate::CreateWeakLambda(this, [this, Rid, Seq]()
	{
		const FAsdaLethalMark* K2 = LethalMarks.Find(Rid);
		AAsdaMobActor* M2 = FindNetMobByRid(Rid);
		if (!K2 || K2->Seq != Seq || !M2 || M2->IsDead()) { return; }
		ApplyLedgerDeath(M2, TEXT("смертельный удар показан"));
	}), 0.05f, false);
}

bool UAsdaWorldSubsystem::IsHitAfterLethal(const AAsdaMobActor* M, int32 Attacker, uint32 Seq) const
{
	if (!M) { return false; }
	const FAsdaLethalMark* K = LethalMarks.Find(M->NetRenderId);
	if (!K) { return false; }
	// Тот же атакующий, удар из плана: порядок (FIFO) — номер после смертельного (метка без удара — любой удар плана).
	if (Attacker == K->Attacker && Seq > 0) { return Seq > K->Seq; }
	// Прочие (другой атакующий, удар не из плана): по времени — позже смерти больше чем на кадр.
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return M->IsDead() && W && M->GetDeathAtS() > 0.0 && W->GetTimeSeconds() > M->GetDeathAtS() + kCorpseHitGraceS;
}

void UAsdaWorldSubsystem::ForgetLethalMark(int32 Rid)
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (FAsdaLethalMark* K = LethalMarks.Find(Rid))
	{
		if (W) { W->GetTimerManager().ClearTimer(K->Backup); }
		LethalMarks.Remove(Rid);
	}
	// ★ 18.10.2026 (пол HP): новая жизнь / реген — пол и отложенный подъём прошлой жизни недействительны.
	if (FAsdaMobHpFloor* F = MobHpFloors.Find(Rid))
	{
		if (W) { W->GetTimerManager().ClearTimer(F->QuietTimer); }
		MobHpFloors.Remove(Rid);
	}
}

void UAsdaWorldSubsystem::ForgetLethalState(int32 Rid)
{
	ForgetLethalMark(Rid);
	PlannedHits.Remove(Rid);
}

// ═══ ★★★ 18.10.2026 «ПОЛ» ПОЛОСЫ HP МОБА (ключ `asda.Combat.HpFloorFromServer`, разбор у объявления ключа) ═══
bool UAsdaWorldSubsystem::IsHpFloorOn()
{
	return CVarAsdaHpFloorFromServer.GetValueOnGameThread() != 0;
}

void UAsdaWorldSubsystem::NoteMobHpFloor(AAsdaMobActor* M, int32 Hp, bool bFrom4029, const TCHAR* Chan)
{
	if (!IsHpFloorOn() || !M || Hp < 0 || M->IsDead()) { return; }
	const int32 Rid = M->NetRenderId;   // моб снимка
	FAsdaMobHpFloor& F = MobHpFloors.FindOrAdd(Rid);
	// Снимок ниже отложенного подъёма — он новее: подъём к старому значению после боя был бы ложным.
	if (F.DeferredHp >= 0 && Hp < F.DeferredHp)
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("%s: отложенный подъём до %d снят — новый снимок %d из %s ниже"), *AsdaCtWorld::Mob(Rid, M), F.DeferredHp, Hp, Chan ? Chan : TEXT("?"));
		F.DeferredHp = -1;
	}
	const int32 Was = F.Floor;   // журнал: прежний пол
	if (bFrom4029) { F.Floor = Hp; }
	else if (F.Floor >= 0 && Hp < F.Floor) { F.Floor = Hp; }   // урон других источников: пол только опускается
	if (F.Floor != Was)
	{
		ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("%s: пол HP = %d (%s; был %d), полоса %d"), *AsdaCtWorld::Mob(Rid, M), F.Floor,
			bFrom4029 ? TEXT("4029 — HP − накопитель") : (Chan ? Chan : TEXT("?")), Was, M->CurHp);
	}
}

int32 UAsdaWorldSubsystem::MobHpFloorValue(int32 Rid) const
{
	const FAsdaMobHpFloor* F = MobHpFloors.Find(Rid);
	return (F && F->Floor > 0) ? F->Floor : 0;
}

void UAsdaWorldSubsystem::NoteMobCombat(int32 Rid)
{
	if (!IsHpFloorOn() || Rid < 0) { return; }
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (W) { MobHpFloors.FindOrAdd(Rid).LastCombatS = W->GetTimeSeconds(); }
}

// ═══ ★★★ 05.11.2026 (bug-mob-hp-zero) ЛЕЧЕНИЕ МОБА 8013 И УЧЁТ ПОКАЗАННОГО УРОНА — разбор у ключа `asda.Combat.HealEpoch` ═══
void UAsdaWorldSubsystem::OnMobHealed(AAsdaMobActor* M, int32 WasHp, int32 Hp)
{
	if (!M || CVarAsdaHealEpoch.GetValueOnGameThread() == 0) { return; }
	const int32 Rid = M->NetRenderId;   // моб лечения
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	// Значение лечения — абсолютное HP NpcServer, новее любого прежнего прогноза и снимка.
	MobHpForecast.Remove(Rid);
	LastServerHpSnapshot.Remove(Rid);
	if (FMobImpactLedger* L = ImpactLedgers.Find(Rid)) { L->ServerHp = -1; }   // снимок ленты прошлой эпохи недействителен
	FAsdaMobHpFloor& F = MobHpFloors.FindOrAdd(Rid);
	if (W) { W->GetTimerManager().ClearTimer(F.QuietTimer); }
	F.DeferredHp = -1;
	F.Floor = Hp;                                     // пол = подтверждённое сервером HP после лечения
	F.RecentShown.Reset();                            // показанный до лечения урон уже внутри значения лечения
	F.HealS = W ? W->GetTimeSeconds() : -1000.0;
	F.HealGain = FMath::Max(0, Hp - WasHp);
	UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d '%s': лечение 8013 %d → %d — прогноз, пол и отложенный подъём сброшены (asda.Combat.HealEpoch)"),
		Rid, *M->MobName, WasHp, Hp);
}

bool UAsdaWorldSubsystem::IsForecastNoExtendOn()
{
	return CVarAsdaForecastNoExtend.GetValueOnGameThread() != 0;
}

bool UAsdaWorldSubsystem::IsPreHealRecord(const AAsdaMobActor* M, int32 HpAfter, int32 Damage) const
{
	if (!M || HpAfter < 0 || CVarAsdaHealEpoch.GetValueOnGameThread() == 0) { return false; }
	const FAsdaMobHpFloor* F = MobHpFloors.Find(M->NetRenderId);
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!F || !W || F->HealGain <= 0 || W->GetTimeSeconds() - F->HealS > kImpactHoldMaxS) { return false; }
	// «HP до удара» по записи = HpAfter + урон. Запись после лечения ≈ полоса; до лечения — ниже неё на прибавку лечения.
	const int64 PreHit = (int64)HpAfter + (int64)FMath::Max(0, Damage);
	return PreHit <= (int64)M->CurHp - (int64)(F->HealGain / 2);
}

void UAsdaWorldSubsystem::NoteMobHpShown(int32 Rid, int32 Dmg)
{
	if (Rid < 0 || Dmg <= 0) { return; }
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	const double Now = W->GetTimeSeconds();   // мир-время показа
	FAsdaMobHpFloor& F = MobHpFloors.FindOrAdd(Rid);
	F.RecentShown.RemoveAll([Now](const TPair<double, int32>& P) { return Now - P.Key > kRecentShownS; });
	F.RecentShown.Emplace(Now, Dmg);
}

int32 UAsdaWorldSubsystem::RecentShownDamage(int32 Rid) const
{
	const FAsdaMobHpFloor* F = MobHpFloors.Find(Rid);
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!F || !W) { return 0; }
	const double Now = W->GetTimeSeconds();
	int64 Sum = 0;   // показанный урон в окне
	for (const TPair<double, int32>& P : F->RecentShown) { if (Now - P.Key <= kRecentShownS) { Sum += P.Value; } }
	return (int32)FMath::Min<int64>(Sum, INT32_MAX);
}

bool UAsdaWorldSubsystem::IsMobInCombat(int32 Rid) const
{
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return false; }
	if (const TArray<FAsdaPlannedHit>* L = PlannedHits.Find(Rid))
	{
		for (const FAsdaPlannedHit& X : *L) { float S = 0.f; if (IsPlannedHitPending(X, S)) { return true; } }
	}
	if (HasPendingImpacts(Rid)) { return true; }
	const FAsdaMobHpFloor* F = MobHpFloors.Find(Rid);
	return F && W->GetTimeSeconds() - F->LastCombatS < kMobCombatQuietS;
}

bool UAsdaWorldSubsystem::HoldMobHpRaise(AAsdaMobActor* M, int32 NewHp, const TCHAR* Chan)
{
	if (!IsHpFloorOn() || bApplyingDeferredRaise || !M || M->IsDead() || NewHp <= M->CurHp) { return false; }
	const int32 Rid = M->NetRenderId;   // моб подъёма
	if (!IsMobInCombat(Rid)) { return false; }   // вне боя — законный подъём сразу (реген покоя)
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return false; }
	// ★ 05.11.2026 (ключ `asda.Combat.LiftLargeGap`, разбор у объявления): сервер отстаёт от показа не больше чем на показанный за
	//   `kRecentShownS` урон. Разрыв больше этого (+1 % max на округления) — полоса неверна (лечение, запись до лечения, иной сдвиг):
	//   подъём сразу (плавно — сглаживание полосы), а не после тишины, которой в бою с боссом нет.
	if (CVarAsdaLiftLargeGap.GetValueOnGameThread() != 0)
	{
		const int64 Gap = (int64)NewHp - (int64)M->CurHp;                                         // насколько сервер выше полосы
		const int64 Allow = (int64)RecentShownDamage(Rid) + FMath::Max(1, M->MaxHp / 100);       // допустимое отставание сервера
		if (Gap > Allow)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d '%s': %s HP=%d выше полосы %d на %lld > показанного за %.0f с урона + 1%% (%lld) — полоса неверна, подъём сразу (asda.Combat.LiftLargeGap)"),
				Rid, *M->MobName, Chan ? Chan : TEXT("?"), NewHp, M->CurHp, Gap, kRecentShownS, Allow);
			if (FAsdaMobHpFloor* Fl = MobHpFloors.Find(Rid)) { Fl->DeferredHp = -1; }
			return false;
		}
	}
	FAsdaMobHpFloor& F = MobHpFloors.FindOrAdd(Rid);
	F.DeferredHp = NewHp;
	const float WaitS = FMath::Max(0.1f, (float)(F.LastCombatS + kMobCombatQuietS - W->GetTimeSeconds()));   // до выхода из боя (с)
	if (!W->GetTimerManager().IsTimerActive(F.QuietTimer))
	{
		W->GetTimerManager().SetTimer(F.QuietTimer, FTimerDelegate::CreateUObject(this, &UAsdaWorldSubsystem::OnMobQuietCheck, Rid), WaitS, false);
	}
	ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("%s: подъём отложен до выхода из боя — %s HP=%d выше полосы %d (в бою полоса только опускается; проверка через %.2f с)"),
		*AsdaCtWorld::Mob(Rid, M), Chan ? Chan : TEXT("?"), NewHp, M->CurHp, WaitS);
	return true;
}

void UAsdaWorldSubsystem::OnMobQuietCheck(int32 Rid)
{
	FAsdaMobHpFloor* F = MobHpFloors.Find(Rid);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	AAsdaMobActor* M = FindNetMobByRid(Rid);
	if (!F || !W || !M || M->IsDead() || F->DeferredHp < 0) { return; }
	if (IsMobInCombat(Rid))
	{
		const float WaitS = FMath::Max(0.25f, (float)(F->LastCombatS + kMobCombatQuietS - W->GetTimeSeconds()));   // ещё в бою
		W->GetTimerManager().SetTimer(F->QuietTimer, FTimerDelegate::CreateUObject(this, &UAsdaWorldSubsystem::OnMobQuietCheck, Rid), WaitS, false);
		return;
	}
	const int32 Want = F->DeferredHp;   // последний серверный снимок, отложенный в бою
	F->DeferredHp = -1;
	if (Want <= M->CurHp) { return; }
	ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("%s: подъём применён после боя (%.1f с тишины): полоса %d → %d (плавно)"), *AsdaCtWorld::Mob(Rid, M), kMobCombatQuietS, M->CurHp, Want);
	bApplyingDeferredRaise = true;
	ApplyMobHpUnfiltered(M, Want, /*bAllowNumber=*/false);
	bApplyingDeferredRaise = false;
}

void UAsdaWorldSubsystem::ApplyHpFloorNow(AAsdaMobActor* M, const TCHAR* Why)
{
	if (!IsHpFloorOn() || !M || M->IsDead()) { return; }
	const int32 Rid = M->NetRenderId;   // моб пола
	const FAsdaMobHpFloor* F = MobHpFloors.Find(Rid);
	if (!F || F->Floor < 0 || F->Floor >= M->CurHp) { return; }
	int64 Cover = 0;   // непоказанный урон: поставленные удары всех ролей + записи навыков ленты
	if (const TArray<FAsdaPlannedHit>* L = PlannedHits.Find(Rid))
	{
		for (const FAsdaPlannedHit& X : *L) { float S = 0.f; if (IsPlannedHitPending(X, S)) { Cover += X.Damage; } }
	}
	if (const FMobImpactLedger* Lg = ImpactLedgers.Find(Rid))
	{
		for (const FAsdaImpactEntry& E : Lg->Pending) { if (E.Kind == EAsdaImpactKind::Skill || E.Kind == EAsdaImpactKind::Proc4102) { Cover += E.PendingDamage; } }
	}
	const int64 Want = (int64)F->Floor + Cover;   // ниже полоса будет опущена ударами сама
	if (Want >= (int64)M->CurHp) { return; }
	ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("%s: пол %d ниже полосы %d, непоказанных ударов %lld не хватает — полоса сразу %d → %lld (%s)"),
		*AsdaCtWorld::Mob(Rid, M), F->Floor, M->CurHp, Cover, M->CurHp, Want, Why ? Why : TEXT("?"));
	ApplyMobHpUnfiltered(M, (int32)Want, /*bAllowNumber=*/false);
}

#if !UE_BUILD_SHIPPING
void UAsdaWorldSubsystem::DebugOwnHitTimersRemaining(TArray<float>& Out) const
{
	Out.Reset();
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	const FTimerManager& TM = W->GetTimerManager();
	for (const FAsdaOwnHitTimer& E : OwnHitTimers) { Out.Add(TM.IsTimerActive(E.H) ? TM.GetTimerRemaining(E.H) : -1.f); }
}
#endif
