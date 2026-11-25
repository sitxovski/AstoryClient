// Copyright (c) 2026 @sitxovski (Discord)
// Типы потока активных эффектов (5272/5268/5229/5453/6154/4102 …) и «часа удачи» (4301): одно сообщение на все опкоды потока.
//   ★ R1 рефакторинга (10.11.2026): дословно вынесены из `AsdaNetSubsystem.h` (он включает этот файл). Потребитель только
//   этих типов (`UAsdaEffectsSubsystem`) больше не тянет весь класс сети. Отражение UE не менялось.
#pragma once
#include "CoreMinimal.h"
#include "AsdaNetEffectTypes.generated.h"

// ══════════════════════ ПОТОК АКТИВНЫХ ЭФФЕКТОВ (ЭТАП 2 MINISTATE_WIN) ══════════════════════════
// Что за что отвечает — Reports/ministate_win_re_B.md §1.3. Один тип сообщения на ВСЕ опкоды
//   потока; разбор тел — в AsdaNetSubsystem.cpp, применение — в UAsdaEffectsSubsystem.
UENUM()
enum class EAsdaEffectMsgKind : uint8
{
	SkillApply   = 0,   // 5272 / запись 4029 / секция 1 снапшота 6154 → buffList_AddOrUpdate (type 0)
	SkillRemove  = 1,   // 5268 (снять), 5273 (снять полностью)
	SoulApply    = 2,   // 5229 / секция 2 снапшота 6154 → charStatusAddType1 (type 1)
	SoulRemove   = 3,   // 5230
	ItemApply    = 4,   // 5453 / секция 3 снапшота 6154 → cCharStatusAddTypeMount → charStatusAddType2 (type 2)
	ItemRemove   = 5,   // 5454
	ClearAll     = 6,   // 5306 (кроме type 2 — предметные переживают очистку)
	ClearByMode  = 7,   // 5307 (mode == 10 либо по buff0DamageType)
	// ★★★ СОСТОЯНИЯ СУЩНОСТЕЙ (мобов). Эталон держит у них ТОТ ЖЕ список (моб + 0x4890) и свою
	//   таблицу обработчиков визуала (`CCharEffect_InitSkillEffectTable@0xF601D0`). Канал —
	//   `Reports/skill_fx_anim.md` §16/§17. UserIdx сообщения = renderId сущности.
	//   Снятие отдельного состояния сущности сервер не шлёт: эталон гасит их по истечении остатка
	//   (`Skill_Buff_TickAll@0xF83A00`) и целиком при смерти/уходе сущности (уже есть `OnEntityDespawn`).
	MobSkillApply  = 8, // 4102 запись с TargetKind == 0 → skillBuffAdd@0xF836E0
};

USTRUCT()
struct FAsdaEffectMsg
{
	GENERATED_BODY()
	UPROPERTY() int32 UserIdx = -1;                                     // владелец эффекта
	UPROPERTY() EAsdaEffectMsgKind Kind = EAsdaEffectMsgKind::SkillApply;
	UPROPERTY() int32 Id = -1;            // skillId (type 0) / соулмейт-слот (type 1) / id записи = subType (type 2)
	UPROPERTY() int32 ItemCode = 0;       // код предмета (только type 2; ключ иконки и тултипа)
	UPROPERTY() int32 DurationSec = 0;    // «сырая» запись +7 (остаток в секундах; 0 у type 2 = НЕ переписывать)
	UPROPERTY() int32 Stack = 0;          // «сырая» запись +5 (стек/уровень, показывается при 2..30)
	UPROPERTY() int32 KindByte = 0;       // «сырая» запись +6 — ★ 4 = ПОСТОЯННЫЙ (тик не уменьшает)
	UPROPERTY() int32 IdFlag = 0;         // «сырая» запись +2 — при >= 1201 таймер НЕ рисуется (бафф хранителя)
	UPROPERTY() int32 Byte9 = 0;          // «сырая» запись +9 (у type1/type2 жёстко 99)
	UPROPERTY() int32 Mode = 0;           // 5307: байт +2
	// ★ Кто наложил состояние (userIdx кастера, запись 4102 +14). Нужен записям программы показа с
	//   носителем `caster` — `Effect_SpawnCounterOnChar@0xF695B0`, `Effect_SpawnDridg5OnChar@0xF6F280`.
	UPROPERTY() int32 CasterIdx = -1;
	UPROPERTY() int32 Opcode = 0;         // источник — для дампа/лога
};

// ★★★ 4301: тело 16 байт «часа удачи». ПОЛНАЯ раскладка — с СЕРВЕРА (авторитет, закон
//   gameserver-crossref): `AlarmGameServerEventInfo@0x5585D0` (GameServer.c:132233) собирает
//   ровно этот буфер из `GetLocalTime()` и строки таблицы `.\Event\GameServerEvent.txt`
//   (6 колонок: год, месяц, день, часНачала, часКонца, бонус — КОЛОНКИ «ВИД БОНУСА» В НЕЙ НЕТ).
//   4301 — это ЗАПРОС клиента (C→S, 40 б анти-чит) с ОТВЕТОМ тем же опкодом (S→C, 16 б).
USTRUCT()
struct FAsdaEventBonusPkt
{
	GENERATED_BODY()
	// +0  Вид бонуса. Клиент умеет 1 = опыт, 2 = золото (монеты), 3 = оба.
	//     ★ СЕРВЕР ЭТОЙ СБОРКИ ПИШЕТ СЮДА ВСЕГДА 1: и инициализатор `sub_5587C0` (`*this = 1`),
	//       и ветка совпадения (`LOBYTE(v6[0]) = 1`). Значения 2/3 сервер не шлёт никогда.
	UPROPERTY() int32 EventType = 0;
	UPROPERTY() int32 Month = 0;        // +1  ТЕКУЩИЙ месяц сервера (GetLocalTime) → часы клиента
	UPROPERTY() int32 Day = 0;          // +2  текущий день
	UPROPERTY() int32 Hour = 0;         // +3  текущий час
	UPROPERTY() int32 Minute = 0;       // +4  текущая минута
	UPROPERTY() int32 Second = 0;       // +5  текущая секунда — КЛИЕНТ ЕЁ НЕ ЧИТАЕТ (только дамп)
	UPROPERTY() int32 SchedYear = 0;    // +6  i16 год     строки расписания — клиент не читает
	UPROPERTY() int32 SchedMonth = 0;   // +8  месяц       строки расписания — клиент не читает
	UPROPERTY() int32 SchedDay = 0;     // +9  день        строки расписания — клиент не читает
	UPROPERTY() int32 SchedStartHour = 0; // +10 часНачала строки расписания — клиент не читает
	UPROPERTY() int32 EndHour = 0;      // +11 часКонца строки → остаток минут
	UPROPERTY() float BonusRate = 0.f;  // +12 f32 бонус из строки расписания; множитель = BonusRate + 1.0
};
