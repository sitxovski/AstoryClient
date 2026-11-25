// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Damage_Private.h — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Damage.cpp): ЗАКРЫТЫЙ заголовок частей AsdaWorldSubsystem_Damage*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaWorldSubsystem_Damage.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "World/AsdaPlayerCharacter.h"
// ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "Npc/AsdaMobActor.h"
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)

// GAsdaChatHitColorFix: определение — в домашней части (R2).
extern int32 GAsdaChatHitColorFix;

// GAsdaSkillDamageChatLines: определение — в домашней части (R2).
extern int32 GAsdaSkillDamageChatLines;

static constexpr float  kImpactWindowMaxS = 4.0f;   // потолок одного взвода окна (с): замах + полёт снаряда всегда короче

static constexpr double kImpactHoldMaxS   = 4.5;    // потолок удержания HP/смерти от первого придержанного пакета (с)

// Ключ CVarAsdaDeathOnHitFrame: определение — в домашней части (R2, без static).
//   ★ R3-c чистка: в Shipping ключ — константа-умолчание, своя копия в каждой части (как до разреза: значение
//   сворачивается компилятором, внешнего объекта нет). Умолчание — одно число на обе ветки.
static constexpr int32 kAsdaDeathOnHitFrameDefault = 1;
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaDeathOnHitFrame;
#else
static TAsdaConstCVar<int32> CVarAsdaDeathOnHitFrame(kAsdaDeathOnHitFrameDefault);
#endif

// ══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ Д6 — ПОРТ `Skill_Buff_RefreshStackDepth@0xF84090` (game.c:579309-579361). Дословно:
//     for (i = 0..27) { removeList[i] = -1; self.word[9310+i] = ids[i]; }   // зеркало списка
//     для КАЖДОЙ записи списка состояний сущности:
//         found = 0;
//         for (i = 0..27) if (rec.i16[+18] == ids[i]) { rec.u8[+22] = levels[i]; found = 1; }
//         if (!found) removeList[removeCount++] = rec.i16[+18];
//     for (i = 0..27) if (removeList[i] != -1) buffStopEffectsBySkillId(self, removeList[i]);
//     uiControl_MarkTargetSelected(self);                                   // перерисовать панель цели
//   Обход идёт ПО СПИСКУ СУЩНОСТИ, а не по массиву пакета ⇒ НОВЫХ записей функция не заводит
//   (их заводит `skillBuffAdd@0xF836E0` из 4102/4013). Ровно это и воспроизводим.
// ★ Зеркало `self.word[9310+i]` (байтовый +18620) не переносим: писателей у него один — эта функция,
//   а ЧИТАТЕЛЕЙ в срезе декомпила нет ни одного (проверено поиском по `AsdaStory_game.c`).
//   Формулировка осторожная — «ссылок в срезе нет», а не «мёртвый код».
// ⚠ Стек пишется и в `Stack`, и в `TargetStack`: эталонное `rec+22` — это `TargetStack`, но
//   `AddOrUpdateMobSkill` (порт `skillBuffAdd`, где `rec+22 = a3`) держит оба поля равными, и
//   расхождение сломало бы отрисовку числа стека, которая читает `Stack`.
// ══════════════════════════════════════════════════════════════════════════════════════════════════
// ★ 06.11.2026: определение — AsdaEffectsSubsystem.cpp (разбор и ключи там же).
int32 AsdaMobAddListedBuffs(UAsdaEffectsSubsystem* Fx, int32 Rid, const TArray<int32>& Ids, const TArray<int32>& Levels, bool bSpawnRecord);

// ══ ★★★ РАЗВОРОТ КАСТЕРА К ЦЕЛИ НАВЫКА (5257 / 5270) ═══════════════════════════════════════════
//   ЭТАЛОН, построчно. `Recv_SkillCast@0xF4E370` (network.c:44847-44930) при `result == 1`:
//     · перебирает 29-байтные записи и собирает `modeMask`: запись с `kind == 1` даёт бит **2**
//       («цель — ПЕРСОНАЖ»), запись с `kind == 0` — бит **1** («цель — СУЩНОСТЬ»);
//     · пишет `*(WORD*)&pChar[1].gap_0[2] = modeMask` (это `char + 104193` в словах) и
//       `LOWORD(pChar->targetRenderId) = targetWords[0]` — индекс ПЕРВОЙ записи;
//     · в LABEL_45 ставит `CChar_SetState(pChar, 24)`.
//   Дальше разворот ведёт ПОКАДРОВЫЙ `objAI_FaceTargetByUserIdx@0xA23350` (entity_char.c:23317),
//   общий с обычной атакой: гейт `state ∈ {5,24,14,27,13} && targetRenderId != -1`, ветка по
//   modeMask (`&8` → `CharManager_FindByUserIdx`, `&1` → `Entity_FindByRenderId`),
//   `dir = normalize(posЦели − posСвоя)` → `charComputeFacingAngleFromDir@0xA4B880`.
//   ⚠ САМО-КАСТ НЕ КРУТИТ ПЕРСОНАЖА: запись само-каста указывает на самого кастера, вектор нулевой
//     и эталон выходит по `if (dirLen == 0.0) return;`. Поэтому цель разворота берётся ИЗ ЗАПИСЕЙ
//     ПАКЕТА, а не из выбранной цели UI (иначе «Третий глаз» разворачивал бы игрока на моба).
//   Порт: цель кладётся в `AttackFaceTarget` (то же поле, что у боя — четырнадцатый кластер дублей
//     не нужен), а «состояние 24/14» живёт временем `SkillFaceUntilS`, которое снимает конец показа
//     (`UAsdaSkillFxSubsystem`) либо сторож 2 с — та же величина, которой §13.6 моделирует
//     эталонный локальный отсчёт состояния 14 (`charUpdateActionStateTick` case 0xE).
//
// ══ ★★★ П33 (T4, 2026-08-11): РЕЖИМ-МАСКА И «НЕ ПЕРЕБИВАТЬ УЖЕ ВЫБРАННУЮ ЦЕЛЬ» ═════════════════
//   Прежде порт БЕЗУСЛОВНО разворачивал кастера на `Recs[0]`. Эталон так делает только в одном из
//   четырёх случаев. Полный `switch (modeMask)` (network.c:44877-44932), где `modeMask` — накопитель
//   по записям (`kind == 1` → бит 2, иначе бит 1), а `pChar[1].gap_0[2]` — СОХРАНЁННАЯ маска:
//     case 1 (только СУЩНОСТИ): маска := 1; цель := targetWords[0] **безусловно**;
//     case 2 (только ПЕРСОНАЖИ): маска := 8; у ЛОКАЛЬНОГО игрока (`effectSlots[112].field_40`)
//            цель ставится ТОЛЬКО если прежняя пуста (`targetRenderId < 0`); у чужого — безусловно;
//     case 3 (СМЕШАННЫЙ пакет):
//            · если сохранённая маска НЕ нулевая — её ТИП сохраняется (8 → 8, иначе 1), а цель
//              ставится только при пустой прежней (у ветки `&8` дополнительно гейт локального игрока);
//            · если маска была 0 и `targetWords[0] >= 0` — маска берётся по kind ПЕРВОЙ записи,
//              цель ставится безусловно;
//     case 0 (записей нет): цель и маска не трогаются вовсе.
//   ⇒ ПРАКТИЧЕСКОЕ СЛЕДСТВИЕ: групповой бафф (все записи `kind == 1`) больше НЕ разворачивает нас
//   на согруппника, пока у нас уже выбрана цель, — оригинал в этот момент продолжает смотреть на
//   моба. Ветка «только мобы» разворот сохраняет полностью (регресса у атакующих навыков нет).
bool AsdaSkillFaceTargetEtalonOn();   // ключ asda.Skill.FaceTargetEtalon (AsdaSkillUseSubsystem.cpp)
