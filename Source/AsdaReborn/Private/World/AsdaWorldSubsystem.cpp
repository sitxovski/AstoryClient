// Copyright (c) 2026 @sitxovski (Discord)
// ★ 23.10.2026 РАЗНОС ФАЙЛА (слово владельца «огромные файлы разделить … с гарантией 100 %»): методы UAsdaWorldSubsystem
//   разнесены по группам в AsdaWorldSubsystem_{Hit,Damage,Entities,Visuals,Peers}.cpp; общее (LogAsdaWorld, журнал боя
//   AsdaCtWorld/ASDA_CTRACE_END_IF_TARGET, чистые константы и хелперы) — в AsdaWorldSubsystemInternal.h. Строки перенесены
//   БАЙТ В БАЙТ: `python Tools/asda_split_cpp.py verify --plan Reports/split_2026-10-23/AsdaWorldSubsystem.plan.json`.
//   Здесь осталось ядро: Initialize/Deinitialize, сброс сессии, свой спаун, война за города, движение, ProcessEnvBatches.
// Реализация UAsdaWorldSubsystem — см. AsdaWorldSubsystem.h (ФАЗЫ B4/B6 входа в мир).
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер

#include "AsdaNetSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: ключ отката ритма (темп предметов), кадры ударов своего замаха
#include "Characters/AsdaWornParts.h"   // кости/attach-матрицы оружия «в руке ↔ за спиной»   ★ R3: FAsdaWornParts — узкий заголовок вместо AsdaCharAssembly.h
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaHudSubsystem.h"             // HERO_NOTICE_WIN — только чужой редкий дроп (6131)
#include "UI/AsdaChatSubsystem.h"            // ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки
#include "UI/AsdaMessageSubsystem.h"         // ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "World/AsdaFloatingTextSubsystem.h" // цифры урона (порт CFloatingText)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Components/SkeletalMeshComponent.h"
#include "ProceduralMeshComponent.h"               // ★ 09.11: площадка города по рельефу
#include "StaticMeshResources.h"                   // ★ 09.11: вершины/UV квада площадки
#include "Engine/StaticMesh.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
// ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»   ★ R3-c: комментарий снятого включения "Text/AsdaLang.h" (части им не пользуются)
// ★ снаряды дальнобойного оружия (projectile_re.md)   ★ R3-c: комментарий снятого включения "World/AsdaProjectileActor.h" (части им не пользуются)
// ★ сырая запись skill_info (.sif +0x48 = buff0 состояния отражения)   ★ R3-c: комментарий снятого включения "Items/AsdaItemDbRows.h" (части им не пользуются)
// FAsdaHeldTables — удержание таблиц спауна (15.09.2026, dt_gc_hold)   ★ R3-c: комментарий снятого включения "AsdaHeldTables.h" (части им не пользуются)
// ★ компаньон-хранитель «ангел» (6156, soul_guardian_companion_re.md)   ★ R3-c: комментарий снятого включения "World/AsdaSoulGuardianActor.h" (части им не пользуются)
#include "AsdaWorldSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem*.cpp

int32 GAsdaMobClipBaseModel = 1;
static FAutoConsoleVariableRef CVarAsdaMobClipBaseModel(TEXT("asda.Mob.ClipBaseModel"), GAsdaMobClipBaseModel,
	TEXT("1 — моб с базой клипов Mob list +0x24 ≠ меша +0x04 берёт скелетный вариант <меш>__<база> (анимация как у оригинала, ")
	TEXT("в т.ч. у 28 мешей без своих клипов); 0 — прежний порт (меш +0x04 со своими клипами или статика). Действует на новые спавны."),
	ECVF_Default);

// ★★★ 17.10.2026 Окно «наш замах в полёте» для всех каналов HP и смерти — полный разбор у `ArmOurImpactWindow`.
// ★ Ф2 (ревизия Ф1 п.5/п.6) — ЧТО ИМЕННО ВОЗВРАЩАЕТ КАЖДЫЙ КЛЮЧ (честно; состояния «ровно до Ф1» не возвращает НИ ОДИН ключ):
//   • `asda.Combat.HitFrameAllChannels 0` — лента попаданий выключена ЦЕЛИКОМ для всех атакующих: прежнее одиночное окно
//     «наш замах в полёте» в редакции ДО ПРАВКИ 17.10 (держит только HP из 4015 и только при своём ответе с HP > 0), прогноз HP
//     2.5 с, без удержания добычи/опыта, фильтр снимка — прежний. Это состояние до утренней правки 17.10, а не «после правки,
//     до Ф1»: окна правки 17.10 (смертельный ответ, взвод по отправке, все каналы) этот ключ тоже снимает.
//   • `asda.Combat.ObserverSync 0` — только НАБЛЮДАТЕЛЬ (чужой атакующий) как до 17.10: очередь соседа без сброса, красные цифры
//     соседа, чужая дельта — «наша» цифра, без поворота моба, замах соседа по времени, а не от пакетов (Ф2), записи ленты для
//     соседа не взводятся. СВОЙ персонаж при этом остаётся на ленте по правилам Ф1/Ф2.
//   • `asda.Combat.SwingPlanFromPackets 0` (Ф2, объявлен в `AsdaPlayerCharacter.cpp`) — замах соседа по времени (как после Ф1) и
//     прежний расчёт момента цифр (кадр от начала клипа) для всех ролей; лента и правила Ф0/Ф1 остаются.
ASDA_DEV_CVAR(int32, CVarAsdaHitFrameAllChannels,   // ★ 23.10.2026 разнос: без static — читают несколько частей (extern в AsdaWorldSubsystemInternal.h)
	TEXT("asda.Combat.HitFrameAllChannels"), 1,
	TEXT("1 (по умолчанию) = пока наш замах по цели в полёте, HP из 4015/4017/4013/4019 и серверная смерть (4015 HP=0, ")
	TEXT("4017 flags=0, 4013/4019 HP=0) ждут попадания; окно открывается и при отправке удара, и при смертельном ответе; с Ф1 — лента ")
	TEXT("попаданий моба для любого атакующего. 0 = лента выключена целиком: окно как до утренней правки 17.10.2026 (только HP из ")
	TEXT("4015 и только при своём ответе с HP>0), не «состояние до Ф1»."),
	ECVF_Default);

// ═══ ★★★ 18.10.2026 ПЛАВНЫЙ БОЙ: СМЕРТЬ, ОЧЕРЕДЬ, ПОДЪЁМ ПОЛОСЫ (`Reports/combat_trace_2026-10-18/bugs_death_and_ledger.md`) ═══
//   Живой журнал `AsdaCombat_20261018-184820` (меч, моб на 35 HP): после «HP 0» персонаж бил ещё ~2.8 с, полоса подскочила 35 → 54,
//   на двух кадрах удара — по две цифры. Три корня и три ключа (умолчание — новое поведение):
//   • `asda.Combat.DeathAtLastPendingHit` — смерть по ответу 4029 с HP 0 ложилась на max(кадр удара ОТ НУЛЯ клипа, КОНЕЦ ПРОХОДА
//     клипа) — у anim_21 проход 6 с, отсюда 3.65 с; тот же срок уходил в ленту («только вперёд») и откладывал 4017. [ОРИГИНАЛ]
//     4029 по HP 0 ставит лишь метку 0x20 (network.c:41909–41917), замах она не останавливает; HP цели уходит в 0 на кадре удара
//     (entity_char.c:829/900), и в первом же тике атакующий встаёт в стойку 0/0/20 (entity_char.c:23132–23137), непоказанные удары
//     пропадают (цифры гейтятся `target+480 != 10`, entity_char.c:848/861/904). [ПОРТ] смерть — на ударе, который довёл полосу до 0
//     (накопленный урон поставленных ударов ≥ полосы; не хватило — на последнем ударе этого атакующего; ударов нет — ближайший кадр
//     удара + полёт, полоса 0/замаха нет — сразу), тот же срок — в ленту; удары по трупу позже смертельного гасятся; атакующие трупа
//     сразу в стойку. Обе роли одинаково: удары соседа ставятся тем же учётом. 0 — прежняя формула.
//   • `asda.Combat.OwnHitsFifo` — удары нового ответа своего замаха ложились на h-й ближайший кадр «от сейчас», не видя кадров,
//     уже занятых прошлым ответом: двойной старт серии (ответ на 4026 и на первый 4028 подряд) давал по две цифры на кадр (8 показано /
//     6 применено). [ОРИГИНАЛ] одно значение очереди на кадр удара строго по очереди (entity_char.c:829 `playerArrayPopFront`), 4029
//     только пополняет очередь (network.c:8317). [ПОРТ] план нового ответа — с кадра после уже поставленных. 0 — прежний план.
//   • `asda.Combat.LedgerRaiseBareOnly` — при записях ленты снимок поднимал полосу до «снимок + непоказанный урон»; у 4015 снимок —
//     HP ДО этих ударов (живое поле без вычета накопителя), отсюда 35 + 19 = 54. [ОРИГИНАЛ] 4015 kind 1/2 пишет только серверное HP
//     `ent[284]` и полосу не трогает (network.c:37551); показанное HP поднимают только спаун/реген. [ПОРТ] при записях ленты 4015,
//     5257/4102 полосу не поднимают (сверка «только вниз» остаётся), реген 4013/4019/4017 — голым значением. 0 — прежний подъём.
//   Четвёртый ключ — `asda.Combat.MobMaxHpFixed` (AsdaMobActor.cpp): MaxHp моба пакетами не поднимается.
static ASDA_DEV_CVAR(int32, CVarAsdaDeathAtLastPendingHit,
	TEXT("asda.Combat.DeathAtLastPendingHit"), 1,
	TEXT("1 (по умолчанию) = смерть моба по ответу с HP 0 — на кадре удара, который довёл полосу до 0 (последний из поставленных ")
	TEXT("ударов атакующего; ударов нет — ближайший кадр удара + полёт или сразу), тот же срок в ленте (4017 не ждёт конца клипа), ")
	TEXT("удары по трупу позже смертельного не показываются, атакующие трупа сразу в боевой стойке. 0 = прежняя формула ")
	TEXT("max(кадр удара от нуля клипа, конец прохода клипа)."),
	ECVF_Default);

bool UAsdaWorldSubsystem::IsDeathAtLastPendingHit()
{
	return CVarAsdaDeathAtLastPendingHit.GetValueOnGameThread() != 0;
}

// ★★★ 17.10.2026 СИНХРОНИЗАЦИЯ БОЯ, ФАЗА Ф0 — НАБЛЮДАТЕЛЬ (чужой игрок бьёт моба) как у оригинала.
//   Проект `Reports/combat_sync_2026-10-17/03_DESIGN.md` (ред. 2), ревизия `04_REVIEW.md`. Ключ включает:
//   • [ОРИГИНАЛ] очередь ударов СОСЕДА сбрасывается на 4027 case 1 (network.c:40916–40919), на 4029, когда сосед не в замахе
//     (network.c:42063–42071; выход в покой `CChar_SetState(…, 0)` сбрасывает её же, entity_char.c:19301–19305), и при смене
//     цели соседа; по мёртвой/отсутствующей цели удары не копятся (network.c:41904 — `state == 10 → return`);
//   • [ПОРТ] цифра «по дельте HP» (`ApplyMobHpUnfiltered`) — только если источник наш замах по этой цели; иначе только полоса
//     (раньше любая дельта рисовалась НАШЕЙ: белая цифра, «вы нанесли» в чат, наше оружие, поворот к нам); время последней
//     цифры очереди — по мобу, а не на весь мир;
//   • [ОРИГИНАЛ] стили цифр соседа по мобу 1/2/5/6 (entity_char.c:848–1056, `Player_ProcessAttackTick`), строки в чат нет;
//     красные 9/0xA/0xD/0xE — только урон по персонажу (`CCharacter_AttackAnimTick`, entity_char.c:1775);
//   • [ОРИГИНАЛ] конец серии соседа по отказу 4029 (коды 1/3/4/5/6/7, код 2 при HP цели ≤ 0; network.c:42084–42281);
//   • [ПОРТ] флинч моба от удара соседа разворачивает моба к соседу (Р8).
//   0 = наблюдатель как до 17.10.2026 (дефекты видны: хвост прошлого моба на новом, чужая дельта — «наша» белая цифра, красные
//   цифры соседа, замах соседа доигрывает после отказа, моб не поворачивается к соседу).
ASDA_DEV_CVAR(int32, CVarAsdaObserverSync,   // ★ 23.10.2026 разнос: без static — читают несколько частей (extern в AsdaWorldSubsystemInternal.h)
	TEXT("asda.Combat.ObserverSync"), 1,
	TEXT("1 (по умолчанию) = бой соседа у наблюдателя по эталону: сброс очереди соседа (4027, вне замаха, смена цели, мёртвая цель), ")
	TEXT("цифра по дельте HP только от нашего замаха, стили соседа по мобу 1/2/5/6, конец серии соседа по отказу 4029, поворот моба ")
	TEXT("к соседу, лента попаданий для соседа, замах соседа от пакетов (Ф2). 0 = только наблюдатель как до 17.10.2026 (свой ")
	TEXT("персонаж остаётся на ленте попаданий)."),
	ECVF_Default);

DEFINE_LOG_CATEGORY(LogAsdaWorld);   // ★ 23.10.2026 разнос: было DEFINE_LOG_CATEGORY_STATIC; объявление — AsdaWorldSubsystemInternal.h (один экземпляр на все части)

// ★★★ ОТКАТ ФИЛЬТРА УСТАРЕВШЕГО СНИМКА HP (см. `NoteServerHpForecast` / `IsServerHpSnapshotStale`).
//   1 = поведение эталона дословно: любой снимок HP из 4015/4017 применяется как есть, даже если он
//   ВЫШЕ собственного прогноза сервера из записи каста (тогда полоса «оживает» до полной сразу после
//   показанной цифры урона — дефект оригинала, живой замер 2026-08-11 09:20:46.715).
//   ⚠ Фильтр — ЛОКАЛЬНЫЙ показ (полоса HP рисуется у каждого клиента своя) и питается ИСКЛЮЧИТЕЛЬНО
//   секторным броадкастом 5257/5270/6159/6160, поэтому у всех клиентов он даёт ОДИНАКОВЫЙ результат
//   и рассинхрона между игроками не создаёт (доказательство — отчёт §28.3).
bool GAsdaLegacyMobDeath = false;
static ASDA_DEV_CVAR_REF(CVarAsdaLegacyMobDeath,
	TEXT("asda.Skill.LegacyDeath"),
	GAsdaLegacyMobDeath,
	TEXT("Мобы: 1 = снимок HP из 4015/4017 применяется всегда (дословный эталон, полоса «оживает»); ")
	TEXT("0 = снимок ВЫШЕ прогноза записи каста отбрасывается (исправление дефекта оригинала)"));

// ★★★ ПРИДЕРЖИВАЮЩИЙ ГЕЙТ СМЕРТИ МОБА — ВЫКЛЮЧЕН ПО УМОЛЧАНИЮ (было включено в §26.4/§27.7).
//   1 = обнуление HP и смерть моба ждут выпуска припаркованной смертельной записи каста, то есть
//       НАШЕГО кадра удара (`IsMobDeathHeldByPendingHit`).
//   0 (ДЕФОЛТ) = эталон: смерть идёт от ДАННЫХ (4015/4017/локальное вычитание урона на кадре удара),
//       как в `entityListUpdateFade@0xF78660` (`i[284] <= 0` → `SetActionState(i,10)`).
//   ★ ПОПРАВКА §29 (2026-08-11): показ ЧУЖОГО каста в порт добавлен (`StartSkillVisual` берёт кастера
//   из пакета), поэтому кадр удара есть и у наблюдателя. Гейт всё равно оставлен ВЫКЛЮЧЕННЫМ: он
//   зависит от того, ЗАПУСТИЛСЯ ли показ, а тот может не запуститься по портовым причинам (соседа
//   ещё нет в мире, у навыка нет строки в `DT_SkillClips`, клип не импортирован) — тогда рассинхрон
//   вернулся бы точечно. Смерть по данным одинакова у всех БЕЗУСЛОВНО. Ниже — исходное обоснование.
//   ★ ПОЧЕМУ ВЫКЛЮЧЕН. Гейт привязан к ПОКАЗУ НАВЫКА, а показ в порту идёт ТОЛЬКО у локального
//   игрока (`UAsdaSkillUseSubsystem::StartSkillVisual` → `GetLocalChar()`). У наблюдателя чужого
//   каста показа нет, его полка разбирается сторожем `kCastHitsGraceS = 0.35 с` — значит труп у
//   кастера ложится на кадре удара (у навыка 1243 это 1767 мс), а у соседа — через 0.35 с.
//   Разница до ~1.4 с по ОДНОМУ И ТОМУ ЖЕ мобу = рассинхрон между клиентами, которого у эталона
//   НЕТ: там и записи урона (`SendPacket_ClippingSectorInAllUser` — сектор целиком), и показ
//   (`Recv_SkillCast@0xF4E370` ищет кастера через `CharManager_FindByUserIdx`, тик удара
//   `CSkillCast_UpdateMeleeHit@0xA64BB0` крутится у ЛЮБОГО персонажа) работают у всех одинаково.
//   Закон `fix-original-defects` разрешает оставлять исправление, только если оно не создаёт
//   рассинхрона между клиентами — здесь создаёт. Полный разбор — отчёт §28.
bool GAsdaHoldMobDeath = false;
static ASDA_DEV_CVAR_REF(CVarAsdaHoldMobDeath,
	TEXT("asda.Skill.HoldMobDeath"),
	GAsdaHoldMobDeath,
	TEXT("Мобы: 1 = придержать HP=0 и смерть до кадра удара навыка (красивее, НО рассинхрон с ")
	TEXT("другими игроками — показ навыка есть только у локального кастера); 0 = эталон (дефолт)"));

void UAsdaWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	// ★★★ СТЫК «УРОН ↔ ВИЗУАЛ»: визуальный слой возвращает записи 4102 РОВНО В МОМЕНТ УДАРА
	//   (порог фазы COMBO[фаза] либо прилёт носителя). Зависимость объявлена явно, чтобы делегат
	//   существовал к моменту подписки.
	Collection.InitializeDependency(UAsdaSkillFxSubsystem::StaticClass());
	if (UAsdaSkillFxSubsystem* Fx = GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>())
	{
		Fx->OnSkillHitMoment.AddUObject(this, &UAsdaWorldSubsystem::OnSkillHitMoment);
	}
	// ★ 26.10.2026 (bowfx) [ОРИГИНАЛ]: тики урона по времени (6079 → слоты `UAsdaEffectsSubsystem`) применяет мир — HP, цифра, чат,
	//   заявка 8226. Зависимость явная (Effects зависит только от сети, цикла нет).
	Collection.InitializeDependency(UAsdaEffectsSubsystem::StaticClass());
	if (UAsdaEffectsSubsystem* Ef = GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>())
	{
		Ef->OnDotTick.AddUObject(this, &UAsdaWorldSubsystem::OnDotTick);
	}
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		Net->OnSelfSpawn.AddUObject(this, &UAsdaWorldSubsystem::OnSelfSpawn);
		Net->OnWorldReady.AddUObject(this, &UAsdaWorldSubsystem::OnWorldReady);
		Net->OnMapEntryFx.AddUObject(this, &UAsdaWorldSubsystem::OnMapEntryFx);   // ★ эффект появления/воскрешения (4056)
		Net->OnEnvBatch.AddUObject(this, &UAsdaWorldSubsystem::ProcessEnvBatches);
		Net->OnEntityDespawn.AddUObject(this, &UAsdaWorldSubsystem::OnEntityDespawn);
		Net->OnCombatHit.AddUObject(this, &UAsdaWorldSubsystem::OnCombatHit);
		Net->OnCombatHitAux.AddUObject(this, &UAsdaWorldSubsystem::OnCombatHitAux);
		Net->OnCombatAnim.AddUObject(this, &UAsdaWorldSubsystem::OnCombatAnim);
		Net->OnCombatAnimTargetCell.AddUObject(this, &UAsdaWorldSubsystem::OnCombatAnimTargetCell);   // ★ mobpos_swing: живая клетка цели 4027
		// ★ 4025 — действия/эмоции персонажей сектора (эталон `Recv_CharSpawnFx@0xF3AF00`).
		//   Сюда же сведён 4103 (оператор вошёл в зону уже скрытым) — состояния 300/301 общие.
		Net->OnCharAction.AddUObject(this, &UAsdaWorldSubsystem::OnCharAction);
		// ★ 25.10.2026 [ОРИГИНАЛ] 4025 действие 111 — смена показа шлема персонажа (`Recv_CharSpawnFx`, network.c:39837).
		Net->OnHelmetShowSync.AddUObject(this, &UAsdaWorldSubsystem::ApplyRemoteHelmetShow);
		// ★ 10.10.2026 (копание): 5430 — итог копки (нотис 9003/9004 своему, эффект и звук итога всем).
		Net->OnMiningResult.AddUObject(this, &UAsdaWorldSubsystem::OnMiningResult);
		// ★★★ 6006 — ответ на НАШУ заявку 6005 «скрыться»: своя невидимость приходит только им.
		Net->OnGmHideResult.AddUObject(this, &UAsdaWorldSubsystem::OnGmHideResult);
		Net->OnSkillHit.AddUObject(this, &UAsdaWorldSubsystem::OnSkillHit);   // ★ 4102: применение эффекта/HP
		// ★★★ РАЗВОРОТ КАСТЕРА К ЦЕЛИ НАВЫКА (5257/5270). Эталон `Recv_SkillCast@0xF4E370` пишет
		//   `pChar->targetRenderId` из ПЕРВОЙ 29-байтной записи и ставит состояние 24 — дальше
		//   покадровый `objAI_FaceTargetByUserIdx@0xA23350` доворачивает. Урон из этих же записей
		//   ведёт визуальный слой (`UAsdaSkillFxSubsystem::OnNetSkillCastRecs`), здесь — только цель.
		Net->OnSkillCastRecs.AddUObject(this, &UAsdaWorldSubsystem::OnSkillCastFaceTarget);
			// ★★★ ТО ЖЕ ДЛЯ СКИЛЛОВ ХРАНИТЕЛЯ ДУШИ (6159) — эталон разбирает 6159 и 5257 ОДНОЙ
			//   функцией `Recv_SkillCast@0xF4E370`, значит и `targetRenderId`, и состояние 24 у них
			//   общие. Компаньон своего курса не имеет (`charPreviewUpdateRotation@0xFAA520`:
			//   `Translate(0, +30, 0) × ownerWorldMatrix`), поэтому «не соблюдает направление атаки»
			//   = не развернулся ВЛАДЕЛЕЦ. Здесь берём ТОЛЬКО цель разворота; урон из тех же записей
			//   с 2026-08-11 паркует слой показа (он подписан на этот же делегат) — см. шапку
			//   OnSoulSkillCastRecs: прежнее «числа ведёт цепочка 4028/4029» опровергнуто (0 пакетов).
		Net->OnSoulSkillCastRecs.AddUObject(this, &UAsdaWorldSubsystem::OnSkillCastFaceTarget);
		// ★★★ ПРОГНОЗ HP ИЗ ТЕХ ЖЕ ЗАПИСЕЙ (см. шапку `NoteServerHpForecast` в .h): поле `+11`
		//   записи — это `HP − накопитель − урон` НА БУДУЩЕЕ, а 4015/4017 несут ЖИВОЕ поле HP моба,
		//   которое NpcServer не трогает до 2000 мс. Без прогноза полоса откатывалась к полной.
		Net->OnSkillCastRecs.AddUObject(this, &UAsdaWorldSubsystem::NoteServerHpForecast);
		Net->OnSoulSkillCastRecs.AddUObject(this, &UAsdaWorldSubsystem::NoteServerHpForecast);
		// ★★★ МЕТКА «ОБРЕЧЁН» ИЗ ТЕХ ЖЕ ЗАПИСЕЙ — эталонная ветка `Recv_SkillCast@0xF4E370`
		//   (`HpAfter <= 0` → `sub_92C3AD(ent, 32)`). Именно ЭТИМ, а не задержкой смерти, оригинал
		//   реагирует на «сервер уже засчитал кил». См. `MarkDoomedByCastRecs` и отчёт §28.
		Net->OnSkillCastRecs.AddUObject(this, &UAsdaWorldSubsystem::MarkDoomedByCastRecs);
		Net->OnSoulSkillCastRecs.AddUObject(this, &UAsdaWorldSubsystem::MarkDoomedByCastRecs);
		// HP цели теперь приходит вместе с очередью ударов (OnCombatAnim) — отдельная подписка не нужна.
		// ★ СМЕРТЬ/ВОСКРЕШЕНИЕ ПЕРСОНАЖА (4053 → 4055/4056). Презентация (клип гибели, поза трупа,
		//   обесцвечивание сцены, блок ввода) живёт здесь — меню выбора ведёт UAsdaAfterDeathSubsystem.
		Net->OnCharDeath.AddUObject(this, &UAsdaWorldSubsystem::OnPlayerCharDeath);
		Net->OnCharRevived.AddUObject(this, &UAsdaWorldSubsystem::OnPlayerCharRevived);
		Net->OnSelfWarp.AddUObject(this, &UAsdaWorldSubsystem::OnSelfWarp);   // ★ позиция воскрешения/телепорта от сервера
		Net->OnSelfVitals.AddUObject(this, &UAsdaWorldSubsystem::OnSelfVitals); // ★ HP/MP из пакета воскрешения (50 % max)
		// ★★★ ЕДИНЫЙ ИСТОЧНИК ИСТИНЫ ПО HP: любое изменение витальных значений ЛЮБОГО игрока
		//   (4058 регенерация/лечение/яд, 4055/4056 воскрешение, 4053 смерть, урон 4015/4029) →
		//   плашка. Плашка больше НИЧЕГО не считает сама.
		Net->OnVitalsChanged.AddUObject(this, &UAsdaWorldSubsystem::OnVitalsChanged);
		Net->OnVitalsFx.AddUObject(this, &UAsdaWorldSubsystem::OnVitalsFx);
		Net->OnCharStateUpdate.AddUObject(this, &UAsdaWorldSubsystem::OnCharStateUpdate);   // 6539: HP/MP цели по charId
		Net->OnCombatRefused.AddUObject(this, &UAsdaWorldSubsystem::OnCombatRefused);
		Net->OnPvpAttackStart.AddUObject(this, &UAsdaWorldSubsystem::OnPvpAttackStart);   // ★ 17.10.2026 (Ф4): 4202
		Net->OnPvpHit.AddUObject(this, &UAsdaWorldSubsystem::OnPvpHit);                   // ★ 17.10.2026 (Ф4): 4204
		Net->OnCombatStanceReset.AddUObject(this, &UAsdaWorldSubsystem::OnCombatStanceReset);
		Net->OnEntityMove.AddUObject(this, &UAsdaWorldSubsystem::OnNetEntityMove);
		Net->OnEntityPosSync.AddUObject(this, &UAsdaWorldSubsystem::OnNetEntityPosSync);   // ★ 4062
		Net->OnEntitySpeed.AddUObject(this, &UAsdaWorldSubsystem::OnNetEntitySpeed);       // ★ 6071
		// ★ 4036 у СОСЕДА: эталон пишет новый уровень в `CCharacter+1082`; у нас соответствующее
		//   поле — `NetPlayers[uid].Level`, и его читает панель цели (спрайтовые цифры уровня).
		Net->OnRemoteLevel.AddUObject(this, &UAsdaWorldSubsystem::OnRemoteLevel);
		Net->OnCharPartUpdate.AddUObject(this, &UAsdaWorldSubsystem::OnCharPartUpdate);
		Net->OnCharTitle.AddUObject(this, &UAsdaWorldSubsystem::OnCharTitle);
		Net->OnCharRank.AddUObject(this, &UAsdaWorldSubsystem::OnCharRank);
		Net->OnClanInfo.AddUObject(this, &UAsdaWorldSubsystem::OnClanInfo);
		Net->OnClanReset.AddUObject(this, &UAsdaWorldSubsystem::OnClanReset);
		Net->OnClanFlag.AddUObject(this, &UAsdaWorldSubsystem::OnClanFlag);
		Net->OnClanSnapshot.AddUObject(this, &UAsdaWorldSubsystem::OnClanSnapshot);
		// ★ Мёртвое game-соединение (send-fail полоса): нотис + гашение боя (иначе «дёргается» в пустоту).
		//   Эталон (network.c:767/1316): loc 1001 КРАСНЫМ в WARMESSAGE_WIN (solo), 10 с.
		//   ★ РЕШЕНИЕ ЮЗЕРА: категория Connection → СИСТЕМНЫЙ ЧАТ (221 остаётся за военными).
		Net->OnGameDisconnected.AddLambda([this]()
		{
			if (UAsdaMessageSubsystem* Msg = GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>())
			{
				FAsdaMsgParams P;
				P.Category = EAsdaMsgCategory::Connection;
				P.LocId = 1001;
				// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки → «[#1001]».
				Msg->Post(P);
			}
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
			{
				PC->ClearTarget();        // SetTarget(nullptr): гасит автоатаку/стойку/замах
				PC->ClearPendingLoot();
			}
		});
		// ★ 6713: СВОЯ фракция → полоса лагеря + эмблема звания на своей плашке (альтернатор с кланом).
		Net->OnSelfFaction.AddLambda([this](int32 Fid, int32 Pts, int32 Gr)
		{
			SelfCampId = Fid; SelfFactionGrade = Gr; SelfFactionPoints = Pts;
			if (AActor* Me = LocalPlayerActor.Get())
			{
				if (UAsdaNameplateComponent* NP = Me->FindComponentByClass<UAsdaNameplateComponent>())
				{
					NP->SetCamp(Fid, Gr);
				}
			}
		});
		// ★ Крылья Plus Package (6585) + кольца soulmate (5220) — wings_soulmate_re.md.
		// ★ Заточка своего оружия/щита (4048-бурст и 4021-смена) → пересобрать своё свечение.
		Net->OnSelfEquipGrade.AddUObject(this, &UAsdaWorldSubsystem::RefreshSelfWeaponGlow);
		// ★ Смена СВОЕГО оружия на лету → другой набор анимаций покоя/бега (эталон SetupModelFromItem).
		Net->OnSelfEquipGrade.AddUObject(this, &UAsdaWorldSubsystem::RefreshSelfWeaponMotion);
		Net->OnWingState.AddUObject(this, &UAsdaWorldSubsystem::OnWingState);
		Net->OnSoulmateState.AddUObject(this, &UAsdaWorldSubsystem::OnSoulmateState);
		Net->OnMountState.AddUObject(this, &UAsdaWorldSubsystem::OnMountState);
		Net->OnPeerPet.AddUObject(this, &UAsdaWorldSubsystem::OnPeerPet);
		Net->OnPeerFaction.AddUObject(this, &UAsdaWorldSubsystem::OnPeerFaction);
		Net->OnSoulmatePeer.AddUObject(this, &UAsdaWorldSubsystem::OnSoulmatePeer);
		// ★★★ Одержимость (SoulState=2) ЛЮБОГО игрока: 6152 по charId — основной канал (AoI-бандл),
		//   5220 по userIdx — резервный (enter-map/событийный). Разбор — в сетевом слое.
		Net->OnPeerSoulStateByCharId.AddUObject(this, &UAsdaWorldSubsystem::OnPeerSoulStateByCharId);
		Net->OnPeerSoulStateByUserIdx.AddUObject(this, &UAsdaWorldSubsystem::OnPeerSoulStateByUserIdx);
		Net->OnSoulmateBond.AddUObject(this, &UAsdaWorldSubsystem::OnSoulmateBond);
		Net->OnSoulmateBreak.AddUObject(this, &UAsdaWorldSubsystem::OnSoulmateBreak);
		// ★ 6156: ВИЗУАЛЬНЫЙ КОМПАНЬОН-ХРАНИТЕЛЬ («ангел») — свой И чужие
		//   (soul_guardian_companion_re.md §2). Подписка ВТОРАЯ к той же рассылке: UI-подсистема
		//   хранителя (окно/плашка) слушает её же и о 3D-компаньоне ничего не знает.
		Net->OnSoulGuardIdx.AddUObject(this, &UAsdaWorldSubsystem::OnSoulGuardIndex);
		// ★ Лут (loot_system_re.md): 5005 спавн / 5015 удаление / 5013 результат / 5017 hint.
		Net->OnLootSpawn.AddUObject(this, &UAsdaWorldSubsystem::OnLootSpawn);
		Net->OnLootRemove.AddUObject(this, &UAsdaWorldSubsystem::OnLootRemove);
		Net->OnLootPickupResult.AddUObject(this, &UAsdaWorldSubsystem::OnLootPickupResult);
		Net->OnLootPickHint.AddUObject(this, &UAsdaWorldSubsystem::OnLootPickHint);
	}
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UAsdaWorldSubsystem::OnPostLoadMap);
	// ★ ХРАНИТЕЛЬ: эталон применяет карту 6156 КАЖДЫЙ КАДР (charVisibilityStateRefresh из главного
	//   цикла, game_main.c:1504) — воспроизводим тикером ядра, а не таймером: реже нельзя, иначе
	//   компаньон соседа появляется с задержкой относительно самого соседа.
	// ★ 05.11.2026 (stun-attack): тот же покадровый тикер ведёт гейт «не может действовать» для серий атаки (разбор — у ключа
	//   `asda.Combat.ControlStopsAttack`, AsdaNetSubsystem_Combat.cpp): заголовок не трогаем — логика здесь же, в теле лямбды.
	SoulGuardTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float)
		{
			TickSoulGuardians();
			// ── [ОРИГИНАЛ] `Player_ProcessAttackTick` / `CCharacter_AttackAnimTick`: персонаж под 0/1/3/14/8/4 (`charHasAnyEquippedItemType`)
			//   уходит в покой (`CharFx_AutoSabzil_State(…, 0, 0, 20)`, очередь сброшена). [ИСПРАВЛЕНИЕ] сразу в кадре наложения, а не на
			//   конце текущего шага.
			if (!AsdaControlStopsAttackOn()) { return true; }
			UGameInstance* GIt = GetGameInstance();
			UAsdaEffectsSubsystem* FxT = GIt ? GIt->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
			UAsdaNetSubsystem* NetT = GIt ? GIt->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!FxT || !NetT) { return true; }
			// Свой: серия идёт (автоатака / замах) — тот же выход, что у отказа 4028 при стане (`SendAttackNext4028Impl`).
			if (NetT->IsSelfUserNumKnown() && FxT->HasControlState(FxT->SelfUserIdx()))
			{
				AAsdaPlayerController* PCt = Cast<AAsdaPlayerController>(GIt->GetFirstLocalPlayerController());
				const AAsdaPlayerCharacter* ChT = Cast<AAsdaPlayerCharacter>(LocalPlayerActor);
				const bool bSwing = ChT && (ChT->IsAttackAnimActive() || ChT->IsAttackLoopAlive());
				if (PCt && (PCt->bAutoAttack || bSwing))
				{
					UE_LOG(LogAsdaWorld, Log, TEXT("[стан] свой персонаж под «не может действовать»: серия атаки оборвана (замах %d, автоатака %d) [ИСПРАВЛЕНИЕ 05.11]"),
						bSwing ? 1 : 0, PCt->bAutoAttack ? 1 : 0);
					ASDA_CTRACE(EAsdaCT::Exit, TEXT("свой персонаж обездвижен (0/1/3/4/8/14): серия атаки оборвана — замах %s, автоатака %s"),
						bSwing ? TEXT("шёл") : TEXT("не шёл"), PCt->bAutoAttack ? TEXT("была") : TEXT("не было"));
					// Серия брошена: следующий удар — новый 4026, её неприменённые значения сервер не спишет — их цифры не показываются
					//   (тот же механизм, что у хода/смены цели: `asda.Combat.OwnHitsServerCommit`; эталон — покой сбрасывает очередь).
					if (UAsdaAttackRhythmSubsystem* RhT = GIt->GetSubsystem<UAsdaAttackRhythmSubsystem>()) { RhT->NoteSeriesAbandoned(TEXT("свой персонаж обездвижен")); }
					NetT->OnCombatRefused.Broadcast(NetT->SelfUserNum, /*Sub=*/0, /*Opcode=*/4028);
				}
				const int32 SelfIdx = (int32)NetT->SelfUserNum;
				if (PvpSeries.Contains(SelfIdx)) { ResetPvpSeries(SelfIdx, TEXT("свой персонаж обездвижен")); }
			}
			// Соседи: серия по мобу — конец серии (покой, очередь сброшена, поставленные цифры прохода гасятся); PvP-серия — сброс.
			TArray<int32> Stop;
			for (const TPair<int32, FAsdaNetPlayerEntry>& KV : NetPlayers)
			{
				const AAsdaPlayerCharacter* ChP = KV.Value.Actor.Get();
				if (!ChP || !FxT->HasControlState(KV.Key)) { continue; }
				if (ChP->IsAttackAnimActive() || ChP->IsAttackLoopAlive() || PvpSeries.Contains(KV.Key)) { Stop.Add(KV.Key); }
			}
			for (const int32 Uidx : Stop)
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[стан] сосед %d под «не может действовать»: серия атаки оборвана [ИСПРАВЛЕНИЕ 05.11]"), Uidx);
				ASDA_CTRACE_UID(Uidx, EAsdaCT::Peer, TEXT("сосед uidx=%d обездвижен (0/1/3/4/8/14): серия атаки оборвана"), Uidx);
				StopPeerAttackSeries(Uidx, /*Code=*/1);
				if (PvpSeries.Contains(Uidx)) { ResetPvpSeries(Uidx, TEXT("сосед обездвижен")); }
			}
			return true;
		}), 0.f);
	// ★ 17.10.2026 (Ф4): PvP — значения очереди снимаются на кадрах удара клипа атакующего каждый кадр (эталон: тик анимации).
	PvpTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAsdaWorldSubsystem::PvpHitFrameTick), 0.f);
	// Смена владельца города перекрашивает флаги в мире (эталон: та же переменная состояния,
	//   которую читают `uniconWarApplyTex0/1` при каждой отрисовке).
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		HCityWarInfo = Net->OnCityOwnerInfo.AddWeakLambda(this, [this]() { ApplyCityWarTextures(); });
	}

}

void UAsdaWorldSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	if (SoulGuardTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SoulGuardTickHandle);
		SoulGuardTickHandle.Reset();
	}
	if (PvpTickHandle.IsValid())   // ★ Ф4
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PvpTickHandle);
		PvpTickHandle.Reset();
	}
	Super::Deinitialize();
}

void UAsdaWorldSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	ASDA_CTRACE(EAsdaCT::Note, TEXT("сброс боевого состояния мира (причина %d, %s): прогнозы HP, окно замаха, ленты%s"), (int32)Reason,
		AsdaIsFullSessionReset(Reason) ? TEXT("полный сброс сессии") : TEXT("смена карты"),
		AsdaIsFullSessionReset(Reason) ? TEXT(", очереди ударов, серии соседей") : TEXT(""));
	if (!AsdaIsFullSessionReset(Reason))
	{
		// СМЕНА КАРТЫ: реестры очистит 4003 нового входа (он приходит сразу за 5058). Здесь
		// снимаем только то, что 4003 не трогает и что привязано к УМЕРШЕМУ миру.
		MobHpForecast.Reset();
		LastServerHpSnapshot.Reset();
		PendingImpactRid = -1;
		PendingImpactUntilS = 0.0;
		ResetImpactLedgers();   // ★ 17.10.2026 (Ф1): ленты попаданий — данные умершего мира
		CityWarAppliedState = -2;
		CityWarAppliedSlot = -1;
		CityWarSlots.Reset();
		bCityWarSlotsScanned = false;
		bCityWarClanApplied = false;
		// ★ 06.10.2026 (повторная ревизия портрета, R2-СРЕДН-1): павн УМЕРШЕГО мира уничтожен травелом — ссылку
		//   обнуляем, как требует комментарий ниже («при MapChanged ссылку обнулять обязательно»); раньше ветка
		//   выходила раньше этой строки. Иначе 4135 своего, пришедший до 4003 (CharMake :124111 → :124121), ложился
		//   на старый актор. Зеркало клана своего (`SelfClan`) при смене карты НЕ трогаем — оно живёт всю сессию.
		LocalPlayerActor = nullptr;
		// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Mount.PortalReapply`): кэш транспорта ключуется номерами сессии УМЕРШЕЙ карты. Новая карта
		//   присылает всё заново (соседи — 4005 + 6154, свой — повтор из своего списка на 4003), а старая запись под номером, который
		//   сервер отдаст другому игроку, посадила бы его на чужой транспорт (вход через 4065 транспорта не несёт).
		if (AsdaMountPortalReapplyOn()) { MountByUserIdx.Reset(); }
		return;
	}

	UE_LOG(LogAsdaWorld, Log,
		TEXT("[мир] сброс сессии (%s): игроков %d, мобов %d, NPC %d, лута %d, свой персонаж «%s»"),
		AsdaSessionResetReasonName(Reason), NetPlayers.Num(), NetMobs.Num(), NetNpcs.Num(),
		NetLoot.Num(), *SelfCharName);

	// Реестры мира и всё, что к ним привязано.
	NetMobs.Empty(); NetNpcs.Empty(); NetPlayers.Empty(); NetLoot.Empty(); NetLootSerial.Empty();   // ★ 09.10.2026: и номера появления лута
	DmgBuffers.Empty(); PetActors.Empty();
	PeerSeriesTargetRid.Reset(); PeerSeriesFlags.Reset(); PeerSeriesGen.Reset();   // ★ 17.10.2026 (Ф0/Ф1): серии соседей — данные сессии
	PvpSeries.Reset();   // ★ 17.10.2026 (Ф4): PvP-серии — данные сессии
	MobHpForecast.Reset();
	LastServerHpSnapshot.Reset();
	PartyUserIdxs.Reset();
	// ★★★ GM-невидимость ключуется сессионным userIdx — на новой сессии он значит другого игрока.
	//   Актор при этом уже уничтожен вместе с реестром, снимать `bHidden` не с кого.
	GmHiddenUserIdx.Reset();
	PendingClanByCharId.Reset();
	SelfClan = FAsdaPendingClan(); bSelfClanKnown = false;   // ★ 06.10.2026: зеркало клана своего — данные сессии
	PendingTitleByUserIdx.Reset();
	WingByCharId.Reset();
	// ★ ВОЛНА 6: кэш «последний 6120» и очередь его слива — сессионные. Прежде `PetByCharId`
	//   сброса НЕ имел (соседние `WingByCharId`/`SoulByUserIdx`/`FactionByCharId` имели), то есть
	//   переживал переподключение; правило «каждый снимок пакета сбрасывается на сессии» ловит это.
	PetByCharId.Reset();
	PeerPetQueue.Reset();
	// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Mount.PortalReapply`): и кэш транспорта — данные сессии (номера сессии сервер переиспользует).
	//   Раньше он переживал перезаход: свой «кэш == код» отсекал посадку нового павна.
	if (AsdaMountPortalReapplyOn()) { MountByUserIdx.Reset(); }
	SoulByUserIdx.Reset();
	// ★ 29.10.2026 [ИСПРАВЛЕНИЕ] (ключ asda.Soulmate.RingCacheSync): и кэш колец 6152 по charId — данные сессии (у оригинала поле
	//   +2215 умирает вместе с объектом персонажа). Раньше он переживал выход в выбор персонажа и разрыв связи: разорванная,
	//   пока игрок был вне игры, пара снова зажигала СВОЁ кольцо при следующем входе без перезапуска клиента.
	if (AsdaSoulRingCacheSyncOn()) { SoulIdxByCharId.Reset(); }
	SoulGuardIdxByCharId.Reset();
	PendingImpactRid = -1;
	PendingImpactUntilS = 0.0;
	ResetImpactLedgers();   // ★ 17.10.2026 (Ф1): ленты попаданий — данные сессии
	LastQueueHitS = 0.0;
	LastSelfEntryFxS = -1000.0;

	// Зеркала СВОЕГО персонажа.
	// ★ СМЯГЧЕНИЕ (решение владельца 2026-09-02): при РАЗРЫВЕ СВЯЗИ уровень не выгружался —
	//   актор своего персонажа жив и стоит на карте. Обнуление ссылки гасило HUD, полосы и
	//   миникарту прямо под ним (`UAsdaHudSubsystem::Tick` выходит по `!IsValid(LocalPlayerActor)`).
	//   Оригинал этой развилки не знает: он при разрыве просто закрывает клиент
	//   (`PostQuitMessage(0)`, см. шапку `AsdaSessionReset.h`), так что сверять не с чем —
	//   это наше решение поверх нашего же «вход можно повторить».
	//   При травеле (`LeftWorld` / `EnteringWorld` / `MapChanged`) актор УНИЧТОЖЕН движком,
	//   и ссылку обнулять обязательно.
	if (!AsdaSessionResetKeepsWorldAlive(Reason))
	{
		LocalPlayerActor = nullptr;
		SelfParts = FAsdaWornParts();
	}
	else if (AActor* Me = LocalPlayerActor.Get())
	{
		// ★ Актор жив (разрыв связи): сетевой слой уже сбросил свой `SelfSoulState` в 0 — снимаем и
		//   показ (полупрозрачность «души» / метку одержимости), иначе он переживёт сессию.
		ApplySoulStateToActor(Me, 0);
		// ★ 09.10.2026 (автолопата, ревизия six_fixes МЕЛК-1): лопата 40/43 и таймер конца её клипа — данные сессии: без снятия
		//   конец клипа отправил бы 5428 уже в новую сессию. Таймер цикла автолопаты снимает `UAsdaInventorySubsystem`.
		if (AAsdaPlayerCharacter* MeCh = Cast<AAsdaPlayerCharacter>(Me))
		{
			MeCh->AbortEtalonShovelState(AsdaSessionResetReasonName(Reason));
		}
	}
	bPendingSelfSpawn = false;
	SelfCharName.Empty();
	SelfClanId = -1;
	SelfGold = -1;
	SelfCampId = -2;
	SelfFactionGrade = -1;
	SelfFactionPoints = 0;
	// ★ `SelfParts` сброшен ВЫШЕ, внутри развилки «мир умер»: пока актор жив (разрыв связи),
	//   его надетые части остаются на месте вместе с ним — иначе персонаж разденется на глазах.
	SelfSex = 1;
	EquipTryOn.Reset();
	bLocalWingsHidden = false;

	// Состояние войны за города считано для прошлой сессии.
	CityWarAppliedState = -2;
	CityWarAppliedSlot = -1;
	CityWarSlots.Reset();
	bCityWarSlotsScanned = false;
	bCityWarClanApplied = false;

	// Авто-оценка AoI — счёт «за сессию карты».
	AoiAuto.EffTiles = 30;
	AoiAuto.CurSec   = -1;
	AoiAuto.LastObsS = 0.0;
	FMemory::Memzero(AoiAuto.Buckets, sizeof(AoiAuto.Buckets));
}

void UAsdaWorldSubsystem::RevealWorld(bool bReopenTravelKept)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { LM->HideLoadingScreen(); }
	if (UAsdaWindowSubsystem* Win = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		Win->CloseAll();
		// ★ 22.10.2026: окна, которые эталон держит при смене карты (инвентарь, персонаж, журнал, навыки, сообщество …),
		//   снесены травелом UE — вернуть их владельцами (`UAsdaWindowSubsystem::ReopenTravelKeptWindows`, ключ
		//   `asda.UI.EntryWindowsEtalon`). Первый вход с выбора персонажа: список пуст (полный сброс сессии его снял).
		if (bReopenTravelKept) { Win->ReopenTravelKeptWindows(TEXT("вход на карту (RevealWorld)")); }
		// ★ БАЗОВЫЙ режим ввода мира = GameAndUI. Без этого ЛЮБОЕ открытие игрового окна (мини-карта 1702,
		//   инвентарь, …) через OpenWindow → RefreshModalInput возвращало UIOnly и убивало WASD/click-to-move.
		Win->SetBaseInputMode(/*bWorldGameAndUI*/true);
	}
	if (UWorld* W = GI->GetWorld())
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
		{
			FInputModeGameAndUI M; M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			M.SetHideCursorDuringCapture(false);
			PC->SetInputMode(M);
			PC->bShowMouseCursor = true;
			PC->FlushPressedKeys();
		}
	}
	// ★ Клавиатурный фокус — на игровой вьюпорт (иначе залипший UI-виджет глотает WASD; source ошибок
	//   'InputMode:UIOnly focus Non-Focusable'). Без этого движение по клавишам не доходит до контроллера.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("RevealWorld: загрузочный экран снят, login-UI закрыт, режим Game, фокус→вьюпорт"));
	// Флаги города: состояние либо уже принято пакетом 6743, либо ещё «не занят».
	ApplyCityWarTextures();

}
