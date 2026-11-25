// Copyright (c) 2026 @sitxovski (Discord)
// AsdaQuestTypes.h — ДАННЫЕ КВЕСТОВЫХ МЕТОК NPC («!» над головой, точки на карте).
//
// ══════════════════════════ ЭТАЛОН (адреса runtime VA, ASLR off) ═════════════════════════════════
// ОТКУДА КЛИЕНТ ЗНАЕТ, ЧТО ПОКАЗАТЬ НАД NPC. Всё решает КЛИЕНТ, локально, по трём источникам:
//   (1) ./Data/NPC/Episode/NPC/Nsr.bsz → «<NsrName>.NSR» — СЦЕНАРИЙ КОНКРЕТНОГО NPC
//       (CNpcData_LoadNSR@0xD9C1E0: 4 б заголовка файла + 14352 б в буфер; записи по 144 б
//        начинаются со СМЕЩЕНИЯ 48 внутри буфера, сканируется РОВНО 64 штуки);
//   (2) ./Data/NPC/npc_list.BIN — таблица NPC (заголовок 20 б, запись 136 б): id@+0, имя@+4,
//       имя NSR@+0x26, модель@+0x46, mapId@+0x6C, мир X (float)@+0x70, мир Y (float)@+0x74,
//       поворот@+0x78, ТИП@+0x7C;
//   (3) СОСТОЯНИЕ КВЕСТОВ ИГРОКА — приходит с сервера пакетом 5047 (0x13B7)
//       CQuest_OnRecv_StateSync@0xC608E0: memcpy(CQuest+0x264, body+9, 0x2B0) = 688 байт.
//       Сервер: sub_45C400@0x45C400 «sub_4C28F0(thread, 5047, 688, charIndex, CUser+21802)» —
//       шлётся, когда DB-сервер отдал DBM_QUESTWINDOW_INFO_RES (то есть ПРИ ВХОДЕ В МИР) и после
//       каждого изменения. 688 = 12 слотов × 44 б + 160 б битовой карты «квест завершён».
//
// ВЫБОР МЕТКИ — questFindMatchingActionMode1@0xD8AB90 (над головой) и ...Mode2@0xD8AE60 (карта);
//   тела ИДЕНТИЧНЫ, различается только номер набора флагов (1 или 2 — просто разные скретч-слоты):
//     for (i = 0; i < 64; ++i) {
//       rec = base + 144*i;
//       if (rec[0]==122 || rec[0]==123) {            // sub_D87180 — «спец-запись»
//           if (rec[0] != 123) continue;
//           n = questNpc_CollectDeliverableItems(player, mode);   // сдать ПРЕДМЕТЫ квеста
//           if (n < 2) continue;
//           return (n == 99) ? 2 : 1;
//       }
//       if (rec[0] == 121) continue;                 // 121 — «крафт», меток не даёт
//       cnt = itemTooltip_BuildStatFlagsTable1(rec, mode);   // сколько КАТЕГОРИЙ условий задано
//       if (charEquip_VerifyStatChecksum(player, rec, cnt, mode)) return (int8)rec[+6];  // ← МЕТКА
//     }
//     return 0;
//   Проверка «все заданные категории выполнены»: Verify возвращает cnt==matched && cnt>0.
//
// КАТЕГОРИИ УСЛОВИЙ (itemTooltip_BuildStatFlagsTable1@0xD84810 + charEquip_VerifyStatChecksum@0xD850D0):
//   0 уровень      u16@+8 (min) / u16@+10 (max)     → playerClassRangeMatch(max, min): min<=lvl<=max
//   1 класс        i32@+12 / i32@+16                → playerClassMatchesRequirement
//   2 КВЕСТЫ       i32@+20/+24/+28/+32 (+ цели)     → questCheckAllPrereqDone@0xD86770
//   3 предметы     i32@+84/+92/+100/+108/+116 (+кол-ва u16@+88/+96/+104/+112/+120)
//   4 предмет      i32@+124                          → invCountItemsById
//   5 вкладка      i32@+128 / i32@+132               → invIsActiveBagTab
//   6 слот сумки   u16@+136                          → inventoryBagHasSlot
//   7 прочее       i32@+140                          → sub_9282DA
//   ★ ФАКТ ПО ДАННЫМ КЛИЕНТА: среди 925 записей, РЕАЛЬНО дающих метку, встречаются ТОЛЬКО
//     категории 0, 1 и 2 (867× «0+2», 56× «0+1+2», 1× «2», 1× «0»). Предметных условий (3..7)
//     у меток НЕТ ни одной — поэтому маркеры считаются без инвентаря.
//
// questCheckAllPrereqDone (категория 2) — 1:1:
//   qDone   = i32@+24 : «квест ЗАВЕРШЁН» (нет в активных слотах И бит в карте завершения);
//   qFree   = i32@+20 и i32@+32 : «квест НЕ взят И НЕ завершён»;
//   qActive = i32@+28 : «квест В РАБОТЕ», плюс параметр стадии i32@+36 и до 5 целей.
//   Успех = ВСЕ заданные подусловия выполнены (required == matched && required > 0).
//
// ★★ КАКОЙ ЗНАК ЧЕМУ СООТВЕТСТВУЕТ (доказано, а не со слов):
//   метка 1 → модель eff_0_npc_99_exclam-a_00 = ЖЁЛТЫЙ «!»  (текстура-градиент жёлтая),
//             иконка карты miniQuest.TEX  = жёлтый «!», легенда окна карты Quest_Birth,
//             подпись легенды (JUI MaxMap_Regend1) = «퀘스트 발생» — «квест появился»;
//             условия в данных: qDone(+24) && qFree(+20)  ⇒ «КВЕСТ МОЖНО ВЗЯТЬ»;
//   метка 2 → модель eff_0_npc_99_exclam-a_01 = КРАСНЫЙ «!»,
//             иконка карты miniQuest2.TEX = красный «!», легенда Quest_Done,
//             подпись (MaxMap_Regend2) = «퀘스트 완료» — «квест выполнен»;
//             условия: qActive(+28) && стадия(+36)        ⇒ «КВЕСТ МОЖНО СДАТЬ»;
//   метка 3 → модель eff_0_npc_99_exclam-a_02 = ПУРПУРНЫЙ «?» (геометрия — знак вопроса).
//             На карту НЕ идёт вовсе (partyListPushEntry зовётся только при mode 1 или 2).
//   ⇒ «?» — это НЕ «можно взять». Оба состояния «взять»/«сдать» — это «!», РАЗНОГО ЦВЕТА.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaQuestTypes.generated.h"

// ─────────────────── Строка DT_NpcQuestScript: ОДНА запись .NSR (144 байта) ──────────────────────
// Импортёр: Tools/asda_import_npc_quest_data.py (источник Nsr.bsz). Пустые записи (opcode 0 и ни
//   одного условия) в таблицу НЕ кладутся — они никогда не срабатывают и не прерывают цикл.
USTRUCT(BlueprintType)
struct FAsdaNpcQuestScriptRow : public FTableRowBase
{
	GENERATED_BODY()

	// Имя NSR-сценария (npc_list.BIN +0x26), нижний регистр — ключ группировки.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString NsrName;
	// Индекс записи внутри файла (0..63) — задаёт ПОРЯДОК просмотра, он важен (первое совпадение выигрывает).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 RecIndex = 0;
	// rec+0: 121 = крафт (пропуск), 122/123 = спец-записи (см. шапку), прочее = обычная запись.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Opcode = 0;
	// rec+4 (i16): id страницы диалога (npcDialogRenderPage) — меткам не нужен, хранится для полноты.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 PageId = 0;
	// rec+6 (i8): ★ САМА МЕТКА — 1 жёлтый «!», 2 красный «!», 3 пурпурный «?», 0 = нет.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Mark = 0;

	// Категория 0 — уровень персонажа (u16 rec+8 / rec+10). 0/0 = условие не задано.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 LevelMin = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 LevelMax = 0;

	// Категория 1 — класс (i32 rec+12 = classId, rec+16 = режим сравнения). −1 = не задано.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ClassId = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ClassMode = -1;

	// Категория 2 — предыдущие квесты цепочки.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestDone = -1;      // rec+24 «завершён»
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestFreeA = -1;     // rec+20 «не взят и не завершён»
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestFreeB = -1;     // rec+32 то же
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestActive = -1;    // rec+28 «в работе»
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestStage = -1;     // rec+36 стадия активного
	// Цели активного квеста: id (rec+40/+48/+56/+64/+72) и количества (u16 rec+44/+52/+60/+68/+76).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ObjIds;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ObjCounts;
	// Два флага режима сверки целей: u16 rec+80 (ObjFlagAny) и u16 rec+78 (ObjFlagNone).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ObjFlagAny = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ObjFlagNone = 0;

	// Категории 3..7 — предметы/сумка. У записей с меткой в данных клиента НЕ встречаются
	//   (см. шапку), но импортируются 1:1, чтобы таблица была полной, а не «под задачу».
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ReqItemIds;   // rec+84…+116
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ReqItemCounts;// rec+88…+120
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 HaveItemId = -1;      // rec+124
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 BagTabA = 0;          // rec+128
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 BagTabB = 0;          // rec+132
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 BagSlot = 0;          // u16 rec+136
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MiscCond = 0;         // rec+140
};

// ───────────────── Строка DT_NpcQuestNpc: запись npc_list.BIN (136 байт), нужное для меток ───────
USTRUCT(BlueprintType)
struct FAsdaNpcQuestNpcRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 NpcId = 0;       // +0 (1-based)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString NsrName;       // +0x26 (нижний регистр)
	// ★ +0x46 — БАЗОВОЕ ИМЯ МОДЕЛИ (`<model>.jbm`, `<model>_N.jbb`). Оно же — имя ПОРТРЕТА диалога:
	//   эталон `NPC_OpenWindow@0xD99D80` (npc_mob_pet.c:2457) строит путь
	//   `GetCurrentDir() + "\Data\NPC\Image\" + npc_list[+0x46] + ".TEX"` и грузит его
	//   `uiFont_ReloadAtlas` в текстуру портрета. В порту те же имена лежат в
	//   `/Game/AsdaAssets/Textures/NPC/Portrait/<ModelName>`.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString ModelName;     // +0x46
	// ★ +0x68 — ВЫСОТА СУЩНОСТИ (игровые единицы). Эталон worldEntityInitByType@0xF98B30:
	//   heightParam = npc_list[+0x68]; если −1 → 150. Из неё строится AABB сущности
	//   (sub_9333FB@0x9333FB → sub_B5B030@0xB5B030: полу-размер (50, H/2, 50), центр (x, y + H/2, z)),
	//   а плашка имени садится на ВЕРХ этого AABB (см. AAsdaNpcActor::ResolvePlateHeight).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 PlateHeight = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MapId = -1;      // +0x6C
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") float WorldX = 0.f;    // +0x70 (float, игровые см)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") float WorldY = 0.f;    // +0x74 (float, игровые см)
	// +0x7C — ТИП NPC. npcUi_BuildNpcActionList@0xD97650 берёт в список карты только 0, 4 и 5.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 NpcType = 0;
};

// ─────────────────── Слот активного квеста игрока (44 байта пакета 5047) ─────────────────────────
// Раскладка выведена из cquestStoreQuestSlotData@0xC61F60, questFindActiveSlotByType@0xC61EE0,
//   questHasActiveSlotByTypeAndParam@0xC61DA0 и CQuest_MatchObjective@0xC626B0.
USTRUCT(BlueprintType)
struct FAsdaQuestSlot
{
	GENERATED_BODY()

	// +0  id эпизода/квеста. «Слот занят» = QuestId >= 2001 (эталонная константа во всех проверках).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") int32 QuestId = -1;
	// +4  (u8) категория-байт записи (HIBYTE(questId) в эталоне используется как та же категория).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") int32 Category = 0;
	// ★ +5 (i16) НОМЕР ЯЧЕЙКИ ЖУРНАЛА, в которой лежит запись. Именно он уходит в тело C2S 5006
	//   («отказаться от задания») и 5000 («перенести в другую ячейку»), и по нему же пакет 5052
	//   адресует обновление. В корректном дампе 5047 совпадает с индексом в массиве, но эталон
	//   везде берёт ИМЕННО ПОЛЕ, а не индекс, — повторяем 1:1.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") int32 SlotIndex = -1;
	// ★ +7 (u8) ВИД ЗАДАНИЯ — по нему ветвится вся отрисовка окна 700
	//   (`CQuest_RefreshEpisodeObjectiveText@0xC5E710` / `questPanel_RefreshSlot@0xC5EE50`):
	//     0 — текстовое задание (описание в списке-свитке, лоток целей пуст),
	//     1 — «собрать предметы» (иконки целей + счётчики),
	//     2 — «убить/состояние» (иконки целей + проверка условия завершения по QCT).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") int32 ObjType = 0;
	// +8  (i16) НОМЕР квеста — им оперируют условия NSR и битовая карта завершения.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") int32 QuestNo = -1;
	// +10 (i16) стадия/параметр квеста (сравнивается с QuestStage записи NSR).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") int32 Stage = -1;
	// +14+6j (i32 id, i16 счётчик) — 5 целей квеста.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ObjIds;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ObjCounts;
};

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЭКРАННЫЙ ТРЕКЕР ЗАДАНИЙ (окно 1800 `Quest_State_Win`) — запись отслеживаемого задания.
//  Полный разбор — `Reports/quest_tracker.md`.
//  Эталон: 156-байтовая запись (`questTrackerEntry_ctor@0xC57E80`), массив из 6 штук в
//  `CQuestManager+104`; наполняет `questTracker_UpdateEntry@0xC58820`, печатает
//  `CQuestTracker_BuildWaypointStrings@0xC58A10`.
// ════════════════════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct FAsdaQuestTrackEntry
{
	GENERATED_BODY()

	// +0 questId — КЛЮЧ ПОИСКА записи. Это НОМЕР КВЕСТА (`слот +8`, i16): именно им эталон
	//   удаляет запись (`questTracker_RemoveEntry(…, *(i16*)&слот[a2].questNo)`), и им же кладёт
	//   её крафт-ветка. Обычная ветка кладёт поле блока книги слота, у этих данных равное тому же
	//   номеру, — иначе снятие галки никогда не находило бы свою запись.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") int32 QuestId = -1;
	// Номер квеста для поиска ЦЕЛИ (стадия слота + строки `DT_QuestGuide`). Совпадает с `QuestId`;
	//   держим отдельно, чтобы «чем ищем запись» и «чем ищем цель» не смешивались в одном поле.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") int32 QuestNo = -1;
	// +4 char[64] — заголовок (название эпизода либо название крафт-записи `Raid +12`).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") FString Title;
	// +68/+96/+124 — три строки «предмет : есть/надо». Ровно 3 элемента.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") TArray<FString> Cond;
	// +152 — сколько строк условий показывать (эталон: `lineIdx + 1` последнего записанного).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Quest") int32 LineCount = 0;

	FAsdaQuestTrackEntry() { Cond.SetNum(3); }
};

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЖУРНАЛ ЗАДАНИЙ (окно 700 `QUEST_WIN`) — три таблицы, добавленные 2026-08-22.
//  Импортёр: `Tools/asda_import_quest_tables.py`. Разбор форматов и доказательства смещений —
//  `Reports/quest_window_re.md` §3.3/§3.4/§7a и `Reports/quest_window_impl.md` §1.
// ════════════════════════════════════════════════════════════════════════════════════════════════

// ─────────────── `Data\QDT.bin` (тег «QDT», 766 × 36 б) — ЦЕЛИ-ПУТЕВОДИТЕЛИ ─────────────────────
// Загрузчик `questGuideLoadQdtBin@0xC68B60`. Читатели:
//   · `QuestGuide_BuildObjectiveText@0xC68E00` — до 4 строк «=> Цель — Карта, x:…, y:…» на кнопки
//     705..708 окна 700 (цикл идёт, пока `ContinueFlag != 0`, максимум 4 строки);
//   · `CQuestTracker_BuildWaypointStrings@0xC58A10` — строка экранного трекера 1800 и стрелка.
// Ключ строки — сквозной `Index`: пара (QuestNo, Stage) НЕ уникальна (у 56 квестов до 4 записей).
USTRUCT(BlueprintType)
struct FAsdaQuestGuideRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Index = 0;        // порядок в файле
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestNo = -1;     // i32 +0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Stage = 0;        // u8  +4
	// u8 +5: 0 = цель-NPC (имя через таблицу NPC), 1 = цель-моб (имя через таблицу мобов).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 TargetKind = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 TargetId = 0;     // i32 +8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MapId = -1;       // i32 +12
	// u8 +16: 0 ⇒ строка последняя, перебор записей этого ключа прекращается.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ContinueFlag = 0;
	// i32 +20 / +24 — КЛЕТКА карты (не мировые см). −1 = координат нет, строка идёт без «x:…, y:…».
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 X = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Y = -1;
};

// ────────── ★ [ПОРТ] 31.10.2026 `DT_QuestTargetArea` — МЕСТА МОБОВ ЦЕЛЕЙ ЗАДАНИЙ (значки целей на карте мира) ──────────
// У оригинала такой таблицы нет (значков целей на карте мира у него нет — решение владельца «добавить»). Строит
//   `Tools/asda_quest_target_areas.py` из СЕРВЕРНЫХ данных: `Monster.ai` (квест-блок +402: какой тип моба на каком шаге
//   задания даёт предмет) + спавн `<карта>.svr` (точки типа на карте), точки сгруппированы в зоны. Нужна для шагов,
//   у которых в гиде `QDT.bin` координат нет (63 шага обычных заданий и 53 задания доски). Ключ строки — сквозной номер.
USTRUCT(BlueprintType)
struct FAsdaQuestTargetAreaRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestNo = -1;   // номер задания (как `FAsdaQuestSlot::QuestNo`)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Stage = 0;      // шаг задания (`Monster.ai` +4 записи)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MobType = -1;   // серверный тип моба (= строка таблицы мобов + 1)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ItemId = -1;    // квестовый предмет, который он даёт
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MapId = -1;     // карта зоны спавна
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 TileX = -1;     // центр зоны, клетка карты
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 TileY = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 SpawnCount = 0; // точек спавна в зоне
};

// ────────── ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (баг 6) `DT_QuestTurnIn` — КТО ПРИНИМАЕТ ШАГ ЗАДАНИЯ (по данным СЕРВЕРА) ──────────
// Строит `Tools/asda_quest_turnin_table.py` из `QuestNpcTable.Bin` + `ConditionTable.Bin` + `NpcInfo/<карта>.npc`. Правило
//   сервера — NpcServer `Process_Quest@0x420C10`: ячейка «ПРИНИМАЕТ» NPC (+894 + 88·k) принимает сдачу, только если в журнале
//   есть слот (задание, стадия) == (ячейка +0, ячейка +66), и хоть одно из пяти условий ячейки проходит (ИЛИ). Клиентский
//   `SNQInfo.Bin` (+66) у 14 ячеек отстал от сервера, поэтому таблица — серверная. Ключ строки — сквозной номер.
USTRUCT(BlueprintType)
struct FAsdaQuestTurnInRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestNo = -1;   // задание (ячейка +0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Stage = -1;     // шаг, на котором ячейка принимает (+66)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 NpcId = -1;     // NPC (запись +0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> Keys;   // 5 ключей условий (`SNQICT`/`ConditionTable`), −1 = нет
	// Действие самого сильного ключа (+35 записи условия): 2 — задание завершается, 1 — стадия +1, 0 — выдача предмета.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Action = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MapId = -1;     // карта NPC — серверный спавн
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 TileX = -1;     // клетка NPC — серверный спавн
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 TileY = -1;
};

// ────────── `Data\NPC\QCT.BIN` (тег «QCC», 471 × 140 б) — УСЛОВИЕ «КВЕСТ ВЫПОЛНЕН» ──────────────
// Поиск `charStateFindAndBuildRequest@0xC65080` (линейно, `rec+0 == questNo`), вычисление
//   `netBuildCharStateRequest@0xC65140`.
// ★★★ ГЛАВНАЯ ЭКОНОМИЯ: тело вычислителя ПОБАЙТНО совпадает с `charEquip_VerifyStatChecksum@0xD850D0`,
//   уже портированным как `UAsdaQuestMarkerSubsystem::EvalNsrCond`. Запись QCT = запись `.NSR`
//   БЕЗ первых 4 байт, поэтому импортёр раскладывает поля ПО ИМЕНАМ `FAsdaNsrCond`, а сдвиг −4
//   исчезает ещё на импорте. Раскладка последней пары («слот сумки» u16 +132, «прочее» i32 +136)
//   соответствует `EAsdaCondLayout::Script`, режим счёта категорий — `EAsdaCondOwner::MenuButton`
//   (класс включается по `>= 0`, как и в `netBuildCharStateRequest`).
//   ⇒ ВТОРОГО вычислителя условий в порту не появляется.
USTRUCT(BlueprintType)
struct FAsdaQuestCompleteRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Index = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestNo = -1;      // i32 +0 (ключ)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 LevelMin = 0;      // u16 +4
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 LevelMax = 0;      // u16 +6
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ClassId = -1;      // i32 +8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ClassMode = -1;    // i32 +12
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestFreeA = -1;   // i32 +16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestDone = -1;    // i32 +20
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestActive = -1;  // i32 +24
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestFreeB = -1;   // i32 +28
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestStage = -1;   // i32 +32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ObjIds;    // i32 +36+8k
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ObjCounts; // u16 +40+8k
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ObjFlagNone = 0;   // u16 +74
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ObjFlagAny = 0;    // u16 +76
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ReqItemIds;    // i32 +80+8k
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ReqItemCounts; // u16 +84+8k
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 HaveItemId = -1;   // i32 +120
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 BagTabA = 0;       // i32 +124
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 BagTabB = 0;       // i32 +128
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 BagSlot = 0;       // u16 +132
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Misc = 0;          // i32 +136
};

// ────────── `Data\NPC\Episode\BOOK\*.QST` (442 файла) — ТЕКСТЫ КВЕСТА ───────────────────────────
// Загрузчик `questBook_LoadQstFile@0xC60C00`; ключ строки — имя файла без расширения
//   (`FAsdaEpisodeRow::QstFile`). Куда что идёт (`uiBuildItemSkillTooltipText@0xC5FB70`):
//   `Title` → контрол 703, `Desc` → список 734 (правая страница-свиток),
//   `нотис(3580 + Kind)` + `\n\n` + `Steps[стадия]` + `\n` → контрол описания 714.
USTRUCT(BlueprintType)
struct FAsdaQuestBookRow : public FTableRowBase
{
	GENERATED_BODY()

	// Имя файла без расширения — оно же ключ строки и значение `Episode_Table +204`.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString FileName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Version = 0;   // i32 +0 (301 у всех)
	// i32 +8 — номер книги (совпадает с числовым префиксом имени файла).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 BookNo = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString Title;       // ASCIIZ +12
	// i32 +76 — вид квеста: нотис заголовка описания = 3580 + Kind (0 «Обычное», 1 «Элитное»).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Kind = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString Desc;
	// Ровно 10 элементов (пустые — там, где длина блока была 0): индекс = стадия квеста.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<FString> Steps;
	// ★★★ ПОД-ЗАПИСИ ШАГОВ (добавлено 2026-08-22 вместе с экранным трекером 1800).
	//   В фиксированном блоке `.QST` лежит массив под-записей шага: БАЗА 796 (0x31C) от начала
	//   файла, ШАГ 68, ровно 10 штук (796 + 68×10 = 1476, конец фикс-блока 1488). Поле `+12`
	//   каждой под-записи — это `recId` в `Data\NPC\SMInfo.BIN`, где лежат КОДЫ и КОЛИЧЕСТВА
	//   требуемых предметов. Именно этой связки не хватало трекеру, чтобы напечатать «есть/надо».
	//   Читатель эталона: `QuestReward_UpdateProgressList@0xC65AA0` →
	//   `findRecordById_C10(g_pNpcWnd, книга.подзапись[стадия] + 12)`.
	//   База 796 проверена перебором по всем 442 файлам: при ней ВСЕ 250 ненулевых значений
	//   попадают в диапазон 0…268 (= число записей SMInfo), при базах 788/792 — ни одно.
	//   Ровно 10 элементов; −1 = у шага нет предметных требований.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> StepSmInfoIds;
	// Файл-заготовка без текстов (в данных ровно один — `373_Qst_124`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") bool bTruncated = false;
};

// ────────── `Data\NPC\SMInfo.BIN` (тег «QUE», 269 × 72) — «СКОЛЬКО ПРЕДМЕТОВ НАДО» ──────────────
// Загрузчик `questData_loadSMInfo@0xDA0820`: шапка 20 б, затем `i32 count`, затем `count × 72`.
//   Поиск — `findRecordById_C10@0xDA0C10`, линейно по `rec+0 == id`.
//   Читатель ровно один: `QuestReward_UpdateProgressList@0xC65AA0` (строки экранного трекера 1800):
//     `reqCode = *(i32*)(rec + 32 + 4*i)`, `maxCount = *(u16*)(rec + 52 + 2*i)`, i = 0…4.
//   Ключ строки таблицы = `RecId` (он же порядковый номер записи в файле — проверено: у всех
//   269 записей `rec+0` равен индексу).
USTRUCT(BlueprintType)
struct FAsdaSmInfoRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 RecId = -1;         // i32 +0
	// i32 +32 + 4·i (i = 0…4): коды требуемых предметов, −1 = пусто. Ровно 5 элементов.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ReqItemIds;
	// u16 +52 + 2·i: сколько штук надо. Ровно 5 элементов, соответствие поэлементное с `ReqItemIds`.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ReqItemCounts;
};

// ────────── `Data\NPC\Raid_quest_data.BIN` (тег «RQD», 57 × 388) — КРАФТ/РЕЙД-ЗАДАНИЯ ────────────
// Загрузчик `questLoadQuestTable@0xEEA960` (count лежит В ШАПКЕ, `+4`), поиск
//   `mapObjFindEntryById@0xEECC90` (линейно по `rec+0 == questNo`).
// Ветка «B» трекера (`20000 ≤ questNo ≤ 30000`) в `QuestReward_UpdateProgressList@0xC65AA0`
//   читает отсюда: `+12` название записи, `+88` режим, `+92+4k` коды, `+112+4k` количества.
// ★ Текстовая таблица `Text\Quest\DT_Raid_quest_data` несёт только `id`+`name` — числовых полей
//   в ней нет, поэтому нужна эта, числовая. Дубля не образуется: имена берём отсюда же.
USTRUCT(BlueprintType)
struct FAsdaRaidQuestRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestId = -1;       // i32 +0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString Name;             // ASCIIZ +12 (48 б)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 LevelMin = 0;       // i32 +80
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 LevelMax = 0;       // i32 +84
	// i32 +88: 1 ⇒ «сколько есть» считается ПО СУМКЕ (`CInventory_CountItemById`), а не по
	//   счётчикам слота журнала; при этом эталон такую запись показывать в трекере отказывается.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Mode = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ReqItemIds;    // i32 +92 + 4k
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ReqItemCounts; // i32 +112 + 4k
	// ═══ ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ (окно 75000, `Docs/Port/systems/BOARD_QUEST_FLOW.md` §4.2): остальные поля записи
	//   388 б. Генератор — `Tools/asda_import_board_quests.py` (источник чисел — клиентская таблица Interlude, решение владельца
	//   28.10). Порядок записи в файле = индекс k, которым доска адресует задание (`questTable[k]`, §2.4).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 RecIndex = -1;      // порядковый номер записи в файле (k)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 PeriodType = 0;     // i32 +4: 0 «Повтор», 1 «В день», 2/3 лимит навсегда
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MaxCount = 0;       // i32 +8: лимит раз (для типа ≠ 0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Zone = -1;          // i32 +76: зона (вкладка доски, `CMap_InitZoneList`)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ChoiceItemIds;    // i32 +332 + 4i (4): награда на выбор, −1 = нет
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> ChoiceItemCounts; // i32 +348 + 4i (4): её количество
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Exp = 0;            // i32 +364: опыт
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Gold = 0;           // i32 +368: золото
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> RewardItemIds;    // i32 +372 + 4i (2): обязательная награда-предмет, −1 = нет
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> RewardItemCounts; // i32 +380 + 4i (2): её количество
	// [ПОРТ] Скрыть задание с досок (в эталоне такого поля нет). Одна строка данных: ведомость `Tools/data/board_quest_hidden.json`
	//   (генератор кладёт сюда) или правка строки DT в редакторе. Скрытое задание не попадает во вкладки и метку миникарты;
	//   журнал/сдача уже взятого — как обычно. По умолчанию не скрыто ничего (решение по 20025/20048/20055 — за владельцем).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") bool bHidden = false;
};

// ────────── `Data\NPC\Raid_npc_data.BIN` (тег «RQN», 11 × 428) — NPC-ДОСКИ ОБЪЯВЛЕНИЙ ─────────────────────────────────────
// ★ 28.10.2026. Загрузчик `dataLoadRecordArray428@0xEEA6E0` (конструктор `questStateInitSlots@0xEEA8C0` — всё −1), поиск
//   `questFindRecordById@0xEEB0C0` (по `+4 == npcId`). Читатели — фильтр окна `questUiBuildAvailableLists@0xEEACE0` и метка
//   миникарты `craftCheckRecipeMaterialsAvailable@0xEEEE70`. Ключ строки — номер записи (`+0`).
USTRUCT(BlueprintType)
struct FAsdaBoardNpcRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 RecIndex = -1;     // i32 +0: номер записи
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 NpcId = -1;        // i32 +4: npcId доски (−1 — пустая запись)
	// i32 +8 + 4j (j 0…4): зона вкладки j, −1 = вкладки нет. Ровно 5 элементов.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> Zones;
	// i32 +28 + 4k (k 0…99): ≥ 0 — задание с индексом k доступно у этой доски (значение = номер задания). Ровно 100 элементов.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") TArray<int32> QuestSlots;
};

// ═══════════════ ★ ОТКУДА ПАДАЕТ КВЕСТОВЫЙ ПРЕДМЕТ (`NpcServer\Monster\Monster.ai`) ═══════════
//  Цели вида «убей N» в этой игре устроены как «набери N квестовых предметов», и предмет не падает
//  на землю, а НАЧИСЛЯЕТСЯ: при смерти монстра сервер мобов смотрит пять квест-записей его типа
//  (`Monster.ai`, 479 б на запись, блок с байта +402) и, если моб подходит под активное задание,
//  сообщает игровому серверу, а тот начисляет предмет и шлёт игроку пакет прогресса.
//  Разбор: `Reports/quest_item_source.md`, выгрузка `Reports/quest_item_source.tsv`.
//
//  Таблица нужна клиенту ровно для одного — ответить игроку «где это добыть»: по коду предмета
//  показать, с каких монстров он падает, с каким шансом и на каких картах.
//  ★ Данные СПРАВОЧНЫЕ: сама выдача предмета целиком серверная, клиент в ней не участвует.
USTRUCT(BlueprintType)
struct FAsdaMobQuestDropRow : public FTableRowBase
{
	GENERATED_BODY()

	// Тип монстра (`Monster.ai +0`) — он же ключ строки таблицы.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 MobId = 0;

	// Имя монстра, как его видит игрок.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString MobName;

	// Задание, ради которого начисляется предмет, и его название.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestNo = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString QuestName;

	// Шаг задания, на котором это работает.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Step = 0;

	// Сам предмет и его имя.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ItemId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString ItemName;

	// Сколько всего надо по заданию.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 NeedQty = 0;

	// Шанс в процентах. ★ Значение больше 100 означает «всегда, и только тому, кто добил» —
	//   так в данных оригинала, поэтому храним как есть и подписываем отдельно.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Rate = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") bool bGuaranteed = false;

	// Карты, где этот монстр водится (через запятую) — чтобы подсказать, куда идти.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString Maps;

	// Кто выдаёт задание (для подсказки «к кому вернуться»).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString QuestNpcName;

	// ★ Строка помечена как дефект данных оригинала (условие неактивно, предмет не совпадает с
	//   заданием, задания не существует). Игроку такие не показываем — но и не выбрасываем,
	//   чтобы было видно, что в данных не так.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") bool bBroken = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") FString Note;
};
