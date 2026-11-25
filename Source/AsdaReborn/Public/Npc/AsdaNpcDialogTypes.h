// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNpcDialogTypes.h — ДАННЫЕ ОКНА ДИАЛОГА NPC (JUI `NPC_LINE NPC` 570), импортированные
//   из `./Data/NPC/Episode/NPC/Nsr.bsz` (ZIP-контейнер CPackFile, 187 файлов `*.NSR`).
//
// ═════════════════════════ ОТКУДА ЭТО ВЗЯТО (эталон, VA при ASLR off) ═══════════════════════════
// Загрузчик `CNpcEpisodeData_LoadNSR_Bank0@0xD9A990` (npc_mob_pet.c:2841) читает файл ПОДРЯД:
//     u32                       заголовочное слово (в данных = 101 у всех 187 файлов)
//     byte[0x3810 = 14352]      БЛОК NSR                       → self[4322…]
//     entryCount = u32 блок+0x28
//     entryCount × 31452        ростер talk-записей (страниц)  → self[7910]
//     entryCount × u32          длины строк страниц
//     строки страниц            указатель кладётся в запись[i] + 31448
//
// РАСКЛАДКА БЛОКА NSR (14352 = 48 + 64×144 + 6×848 — сходится байт-в-байт):
//     +0x00  u32       ★ npcId (в отчёте реверса поле называлось «режим/состояние»; по данным
//                        это РОВНО npcId: 187 файлов дают 187 различных значений из диапазона
//                        npc_list.BIN 1…204). Эталон `npcDialogRenderPage` сравнивает его с
//                        46 / 50 / 53 и в этих трёх случаях приписывает к тексту страницы
//                        нотис 111929 — то есть спец-приписка привязана к КОНКРЕТНЫМ NPC.
//     +0x08  char[32]  ЗАГОЛОВОК окна (идёт в `NPC_TNAME` 573) — НЕ имя из npc_list.BIN
//     +0x28  u32       entryCount — число talk-записей (страниц)
//     +0x2C  u16       menuCount — сколько кнопок меню показывать (эталон требует < 6)
//     +0x30  64×144    записи-команды сценария (opcode/pageId/метка/условия) → DT_NpcQuestScript
//     +0x2430 6×848    КНОПКИ МЕНЮ (подпись + до 5 под-записей с кодами команд) → DT_NpcNsrButton
//
// КНОПКА МЕНЮ (848 байт), доказано hex-ом `ian.nsr` и кодом `uiCraftSlotClickHandler@0xD83D90`:
//     +0x000 u16       индекс кнопки
//     +0x002 char[66]  ПОДПИСЬ (CP949; у 187 файлов это ASCII-английский, кроме 5 записей)
//     +0x044 5×156     под-записи; смещения читает эталон (`gap_4F04[848*btn + 6392 + 156*j]` …):
//                        rel+4   i16 talkId ветки
//                        rel+6   u16 флаг «показать страницу вместо команды»
//                        rel+8   НАЧАЛО блока условия (см. `FAsdaNsrCond` ниже)
//                        rel+12  u16 КОД КОМАНДЫ 30001…30088 / 31000  (= условие+4)
//                        rel+44  i32 параметр команды                 (= условие+36)
//                        rel+148 u16 состояние: 1 = конец списка, 2 = требуется проверка условия
//
// TALK-ЗАПИСЬ = СТРАНИЦА ДИАЛОГА (31452 байта):
//     +2      i16       talkId (ключ поиска; внутри файла уникален — проверено на всех 2607 страницах)
//     +14     i16       stepCount (в данных 0…2; эталон отказывается рисовать шаги при > 3)
//     +16     6×5228    блоки шагов; блок = 68 байт заголовка + 5×1032 под-записи
//        блок+2   char[66]  ПОДПИСЬ ШАГА (она же подпись кнопки `NPC_QA_BUT0i`)
//        блок+1032*m+72   i32 состояние шага (2/3/4 → кнопки «Назад»/«Далее»; 5 → облачко квестов)
//        блок+1032*m+76   НАЧАЛО блока условия (читает `netSerializeCharStatPacketB@0xD88F00`)
//        блок+1032*m+80   u16 talkId ветки
//        блок+1032*m+224/226/228  u16 talkId результата (224 — «есть ветка», 228 — обычный переход)
//     +31448  char*     ТЕКСТ СТРАНИЦЫ (аллоцируется загрузчиком из хвоста файла)
//
// Импортёр: `Tools/asda_import_nsr_dialog.py` (заливка JSON — вложенные структуры CSV не тянет).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaNpcDialogTypes.generated.h"

// ────────────────────── УСЛОВИЕ (блок 148 байт; общий для кнопок и шагов) ───────────────────────
// ★ ГДЕ ОН ЛЕЖИТ (посчитано по коду, а не «на глаз»). У КНОПКИ условие читает
//   `netSerializeCharStatPacketA@0xD871C0` от адреса `848*btn + 6392 + 156*j`, у ШАГА —
//   `netSerializeCharStatPacketB@0xD88F00` от `5228*i + 16 + 1032*m + 76`. Приведя оба к началу
//   под-записи (`+68 + 156*j` и `+68 + 1032*m`), получаем ОДНО И ТО ЖЕ: блок начинается на
//   **+8 от начала под-записи**, и дальше offsets у обоих совпадают байт-в-байт
//   (уровни +8/+6, класс +12/+16, квесты +20…+80, предметы +84…+120, предмет +124,
//    вкладка +128/+132, слот +142, прочее +144).
//
// ★★★ ПРОВЕРЕНО НА БАЙТАХ (зонд `Tools/_probe_nsr_cond.py`, все 187 файлов, 13 150 под-записей
//   шага и 5 610 под-записей кнопок) — и ПОПРАВЛЕНО против первой редакции этого файла:
//     · САМ СДВИГ +8 ПОДТВЕРЖДЁН: при нём диапазон уровня осмыслен (`min <= max <= 200`) у 812
//       под-записей из 812, при сдвиге +10 — ни у одной;
//     · НО ВНУТРИ блока пара «уровень» стоит на **+6 (min) / +8 (max)**, а не на +8/+10.
//       Пример `108npc.nsr` страница 3, шаг 0, под-запись 0: `cond+6 = 140, cond+8 = 150,
//       cond+10 = 0` — «уровень 140…150», ровно как читает эталон
//       `playerClassRangeMatch(u16 cond+8 /*max*/, u16 cond+6 /*min*/)`.
//   => Никакого «дефекта D11» НЕТ. Расхождение объясняется тем, что 148-байтовый блок под-записи и
//     144-байтовая ЗАПИСЬ СЦЕНАРИЯ (`DT_NpcQuestScript`) — РАЗНЫЕ структуры: у записи сценария
//     перед уровнем стоят `u16 pageId` + `u8 mark` + `u8`, поэтому там уровень на +8/+10
//     (`charEquip_VerifyStatChecksum@0xD850D0` читает именно так — и читает верно), а у под-записи
//     на его месте `u16` резерв, и уровень начинается на два байта раньше. Хвост тоже разъехался:
//     запись сценария — слот сумки `u16 +136`, прочее `i32 +140`; под-запись — `u16 +142` и
//     `i32 +144`. Обе пары сохранены отдельными полями, потому что ОБЕ верны — каждая для своей
//     структуры (третий вариант той же семьи, `itemVerifyAttributeSum@0xD8A2A0`, читает уровень
//     вообще с +4/+6 — это записи `SNQICT.Bin`).
USTRUCT(BlueprintType)
struct FAsdaNsrCond
{
	GENERATED_BODY()

	// +0 — у ШАГА здесь продублирован код квеста (он же `State` под-записи), у КНОПКИ это
	//   служебное слово; ни один эталонный читатель условия его не проверяет.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Opcode = 0;        // i32 +0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Spare4 = 0;        // u16 +4
	// Диапазон уровня персонажа: `playerClassRangeMatch(max = u16 +8, min = u16 +6)`.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 LevelMin = 0;      // u16 +6
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 LevelMax = 0;      // u16 +8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Spare10 = 0;       // u16 +10
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ClassId = -1;      // i32 +12
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ClassMode = -1;    // i32 +16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestFreeA = -1;   // i32 +20
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestDone = -1;    // i32 +24
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestActive = -1;  // i32 +28
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestFreeB = -1;   // i32 +32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestStage = -1;   // i32 +36
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ObjIds;    // i32 +40/48/56/64/72
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ObjCounts; // u16 +44/52/60/68/76
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ObjFlagNone = 0;   // u16 +78
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ObjFlagAny = 0;    // u16 +80
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ReqItemIds;    // i32 +84…+116
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ReqItemCounts; // u16 +88…+120
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 HaveItemId = -1;   // i32 +124
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 BagTabA = 0;       // i32 +128
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 BagTabB = 0;       // i32 +132
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 BagSlot136 = 0;    // u16 +136 (раскладка записи сценария)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Misc140 = 0;       // i32 +140 (раскладка записи сценария)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 BagSlot142 = 0;    // u16 +142 (раскладка под-записи)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Misc144 = 0;       // i32 +144 (раскладка под-записи)
};

// ───────────────────── БЛОК ЦЕЛИ ШАГА (140 байт, 6 штук в под-записи шага) ──────────────────────
// Живёт по `под-запись + 188 + 140*j`; «цель активна» — отдельный i32 по `под-запись + 164 + 4*j`.
// Читатели в эталоне: `quest_GetEntryProgressState@0xD892D0`, `questNpc_IsTargetAvailable@0xD894C0`,
// `questStageBuildStatFlags@0xD7E950`, `uiQuestRewardPanelPopulate@0xD93F90` и отправка 5043.
USTRUCT(BlueprintType)
struct FAsdaNsrStepObj
{
	GENERATED_BODY()

	// +4 — код квеста/эпизода этой цели (идёт в название эпизода и в тело 5043 режима «сдать»).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestId = 0;
	// +24 u16 — вид цели: `questNpc_IsTargetAvailable` различает 0 и 1 (0 = «просто доступна»,
	//   1 = «нужен накопитель»: тогда смотрит пару +52/+60 и порог +136).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Kind24 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Flag90 = 0;    // u16 +90
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Need136 = 0;   // i32 +136
	// Слоты награды (`questStageBuildStatFlags`: непустых должно быть <= 6, иначе панель не строится).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A16 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A20 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A28 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A32 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A36 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A40 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A44 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A48 = 0;   // u16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A52 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A60 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A68 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A76 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 A84 = 0;
	// ★ КОЛИЧЕСТВА предметных наград (виды 8…12). `itemSlotSetupEquipSowel@0xD7F130` читает пару
	//   «id (i32) + количество (u16 по id+4)»; без них попап 540 не мог бы написать «×N».
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 C56 = 0;   // к A52
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 C64 = 0;   // к A60
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 C72 = 0;   // к A68
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 C80 = 0;   // к A76
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 C88 = 0;   // к A84
	// ★ ВТОРОЙ блок награды — ВЫБИРАЕМАЯ (`questRewardSlotParse@0xD81460`, панель `EPISODESELECT_WIN`
	//   553). Виды: S92→2, S96→3, S100→5, S104(гейт S108 u16)→6, S112/S116→10, S120/S124→11,
	//   S128/S132→12. Ровно ОДИН вид должен быть задан, иначе слот выбора отбрасывается.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S92 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S96 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S100 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S104 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S108 = 0;   // u16-гейт вида 6
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S112 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S116 = 0;   // u16 количество
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S120 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S124 = 0;   // u16 количество
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S128 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 S132 = 0;   // u16 количество
};

// ─────────────────── ПОД-ЗАПИСЬ КНОПКИ МЕНЮ (156 байт, до 5 штук на кнопку) ─────────────────────
USTRUCT(BlueprintType)
struct FAsdaNsrButtonSub
{
	GENERATED_BODY()

	// rel+148 u16: 0 = пусто, 1 = конец списка (команда выполняется без проверки),
	//              2 = сначала проверить условие `Cond`.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 State = 0;
	// rel+4 i16 — talkId ветки (эталон подставляет его В КАЧЕСТВЕ кода команды, когда Flag != 0).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 TalkId = 0;
	// rel+6 u16 — «показать страницу вместо команды». В данных этой сборки ВЕЗДЕ 0 (149/149).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Flag = 0;
	// rel+12 u16 — КОД КОМАНДЫ 30001…30088 / 31000 (диспетчер `CGameUI_HandleControlCommand@0xD87590`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 CmdCode = 0;
	// rel+44 i32 — параметр команды (у 130 из 149 под-записей это −1 «нет параметра»).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Param = -1;
	// rel+0 — блок условия (нужен только при State == 2; в этой сборке такая под-запись ОДНА).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FAsdaNsrCond Cond;
};

// ─────────────────────── DT_NpcNsrButton: ОДНА кнопка меню NPC (848 байт) ────────────────────────
// Ключ строки: `<nsrName>_<ButtonIndex>` (нижний регистр имени NSR).
USTRUCT(BlueprintType)
struct FAsdaNsrButtonRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString NsrName;
	// Порядковый номер слота 0…5. Кнопки JUI: 0…3 → `NPC_SHOP_BUT00…03` (581…584),
	//   4 → `NPC_SHOP_BUT04` (585). Слот 5 в JUI кнопки не имеет — эталон его не показывает.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ButtonIndex = 0;
	// +0 u16 — собственный индекс кнопки из данных (совпадает с ButtonIndex у всех 139 записей).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 SlotIndex = 0;
	// +2 — подпись, которую видит игрок. Ставит её КОД в рантайме (в WBP лежит плейсхолдер).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString Caption;
	// Ровно 5 под-записей (пустые не отбрасываются — эталон идёт по ним циклом j = 0…4).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<FAsdaNsrButtonSub> Subs;
};

// ───────────────────── ПОД-ЗАПИСЬ ШАГА СТРАНИЦЫ (1032 байта, до 5 на шаг) ───────────────────────
USTRUCT(BlueprintType)
struct FAsdaNsrStepSub
{
	GENERATED_BODY()

	// +72 i32 — состояние шага. Эталон `listPanel_refreshSlotStates@0xDA2810`:
	//   3 → показать `NPC_PRE_BUT` (576), 2 и 4 → `NPC_NEXT_BUT` (577);
	//   `craftSlotFindFirstStateIndex@0xD94AB0`: 2→1, 3→2, 4→3, 5→4 (5 = облачко выбора квеста).
	//   Всё остальное (в данных 818 различных значений — это КОДЫ КВЕСТОВ) кнопок не включает,
	//   и подпись шага уходит в `NPC_QA_BUT0i`.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 State = 0;
	// +80 u16 — talkId ветки (используется, когда +224 != 0).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 BranchTalkId = 0;
	// +224 / +226 / +228 u16 — talkId результата. 228 — обычный переход «дальше по диалогу»
	//   (0 = закрыть окно: `npcDialogRenderPage(0)` уходит в `UI_resetAllWindowsAndLeave_5045`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Res0 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Res1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Res2 = 0;
	// +76 — блок условия шага.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FAsdaNsrCond Cond;
	// ── КВЕСТОВАЯ ЧАСТЬ (доимпортирована на этапе Э4; раскладка закрывает 1032 байта байт-в-байт) ──
	// +1028 — «этот шаг ВЫДАЁТ квест с таким кодом» (> 0 => `quest_GetEntryProgressState` вернёт 1).
	//   В данных сборки таких под-записей 372.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 AcceptQuestId = 0;
	// +164 + 4*j (6 штук) — «цель j активна». `quest_GetEntryProgressState` сравнивает с 0,
	//   `gridScanAnyActiveCell@0xD7E2E0` — с -1. ВНИМАНИЕ: в данных ЭТОЙ сборки значение -1 не
	//   встречается ни разу (0 в 78 418 слотах из 78 900), поэтому сравнение с -1 всегда истинно.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ObjActive;
	// +188 + 140*j — шесть блоков цели. Пусто, если у под-записи их нет (446 под-записей из 13 150).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<FAsdaNsrStepObj> Objs;
};

// ────────────────────────── ШАГ СТРАНИЦЫ (заголовок 68 б + 5 под-записей) ───────────────────────
USTRUCT(BlueprintType)
struct FAsdaNsrStep
{
	GENERATED_BODY()

	// блок+2 — подпись шага (она же подпись кнопки `NPC_QA_BUT0i`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString Caption;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<FAsdaNsrStepSub> Subs;
};

// ──────────────────── DT_NpcNsrPage: СТРАНИЦА ДИАЛОГА (talk-запись 31452 байта) ─────────────────
// Ключ строки: `<nsrName>_<TalkId>` — именно по talkId ищет страницу `npcDialogRenderPage@0xD9C7B0`.
USTRUCT(BlueprintType)
struct FAsdaNsrPageRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString NsrName;
	// Порядковый номер записи в ростере (эталон хранит его в `self[14]` как «текущая страница»).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 PageIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 TalkId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 StepCount = 0;
	// Текст реплики (из хвоста файла, по указателю запись+31448). Переводы строк — как в данных.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString Text;
	// Шаги: ровно StepCount элементов (пустые хвостовые блоки не пишутся).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<FAsdaNsrStep> Steps;
};

// ───────────────────── DT_NpcNsrHeader: шапка файла `.NSR` (блок 48 байт) ───────────────────────
// Ключ строки: `<nsrName>`.
USTRUCT(BlueprintType)
struct FAsdaNsrHeaderRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString NsrName;
	// блок+0 — npcId владельца сценария (см. шапку файла).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 NpcId = 0;
	// блок+8 — заголовок окна (`NPC_TNAME` 573).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString Title;
	// блок+0x28 — число страниц; блок+0x2C — число кнопок меню (эталон рисует только при < 6).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 EntryCount = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 MenuCount = 0;
};

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ТАБЛИЦЫ, ДОБАВЛЕННЫЕ НА ЭТАПАХ Э3…Э5 (импортёр `Tools/asda_import_npc_tables.py`)
//  Раскладка каждой и адрес эталонного читателя — в шапке импортёра.
// ════════════════════════════════════════════════════════════════════════════════════════════════

// ═══ АССОРТИМЕНТ МАГАЗИНА NPC — `Data\NPC\Shop\js<npcId>.spd` (импортёр `asda_import_npc_shop.py`) ═══
//   Формат файла (загрузчик `NPCShop_LoadBuyList@0xE28700`, npc_mob_pet.c:6641):
//     `[u32 =0][u32 count][count × u32 itemCode]`, размер файла = 8 + 4·count.
//   ★ ИМЯ ФАЙЛА = npcId: `sprintf(path, "./data/npc/Shop/js%d.spd", окно[+476])`, а в это поле
//     `NPCShop_OpenWindow@0xE25C00` кладёт свой третий аргумент — `pThis[436]` окна разговора,
//     то есть «с каким NPC говорим». Отдельного «идентификатора магазина» в данных НЕТ.
//   ★ Количество/потолок стопки в файле НЕ хранится: загрузчик берёт его из справочника
//     (`ItemRecord->flagWord176`, при ≤ 1 — единица; у самоцветов — ноль). Поэтому в таблице
//     ТОЛЬКО коды, а потолок считает рантайм по тому же `DT_item_list`.
//   Ключ строки — npcId десятичной строкой (`AsdaDt::RowByInt`).
USTRUCT(BlueprintType)
struct FAsdaNpcShopRow : public FTableRowBase
{
	GENERATED_BODY()
	// Идентификатор NPC (он же номер в имени `js<N>.spd`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 NpcId = 0;
	// Коды товаров В ПОРЯДКЕ ФАЙЛА — порядок задаёт раскладку витрины 6×6 постранично.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ItemCodes;
};

// `Data\NPC\WarpListTable.BIN` (тег "WLT", 16 × 16 б). Ключ строки — ListIndex.
// Команда 30031: в пакет 5477 уходит МЛАДШИЙ БАЙТ поля `MapId` записи `listIndex`
//   (`netSendReqWord_op5477(this, *(u8*)(база + 16*listIndex + 4))`).
USTRUCT(BlueprintType)
struct FAsdaWarpListRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ListIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 WarpIndex = 0;   // +0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 MapId = 0;       // +4
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 CellX = 0;       // +8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 CellY = 0;       // +12
};

// Под-запись `SNQInfo.Bin` (88 б): квест + пять ключей в `SNQICT.Bin`.
USTRUCT(BlueprintType)
struct FAsdaSnqEntry
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestId = 0;     // i16 +0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> Keys;    // +68 + 4*j
};

// `Data\NPC\SNQInfo.Bin` (тег "NPC", 194 × 1796 б). Ключ строки — NpcId.
// Источник обоих списков облачка `MULTYQUEST_WIN` (`questNpc_CollectDeliverableItems@0xD89550`).
USTRUCT(BlueprintType)
struct FAsdaNpcSnqRow : public FTableRowBase
{
	GENERATED_BODY()
	// Порядковый номер записи в файле — важен: у ЧЕТЫРЁХ npcId (38, 39, 200, 201) записей ДВЕ,
	//   а эталон `findCopyRecord1796ById16@0xDA1140` берёт ПЕРВУЮ; вторые в таблицу не попадают.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 RecIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 NpcId = 0;       // i16 +0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString NpcName;       // +2 (34 б)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<FAsdaSnqEntry> Give;  // +36
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<FAsdaSnqEntry> Take;  // +916
};

// `Data\NPC\SNQICT.Bin` (тег "CON", 774 × 156 б). Ключ строки — Key.
// ТРЕТИЙ вариант блока условия: категории включаются ОТДЕЛЬНЫМИ БАЙТАМИ-флагами
//   (`itemTooltip_BuildStatFlagsTable2@0xD89E80`), а данные читает `itemVerifyAttributeSum@0xD8A2A0`.
USTRUCT(BlueprintType)
struct FAsdaSnqIctRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Key = 0;         // i32 +0
	// +8 — talkId страницы, на которую уходит диалог при выборе строки в облачке.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 TalkId = 0;      // u16 +8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 HasLevel = 0;    // u8 +37
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 LevelMin = 0;    // u8 +38
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 LevelMax = 0;    // u8 +39
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 HasClass = 0;    // u8 +40
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ClassId = -1;    // i32 +44
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ClassMode = -1;  // i32 +48
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 HasQuest = 0;    // u8 +52
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestDone = -1;  // i32 +60
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestFreeA = -1; // i32 +56
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestActive = -1;// i32 +64
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestFreeB = -1; // i32 +68
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestStage = 0;  // u8  +72
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ObjFlagNone = 0; // u8  +74
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 ObjFlagAny = 0;  // u8  +75
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ObjIds;      // i32 +76
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ObjCounts;   // u16 +96
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 HasReqItems = 0; // u8 +106
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ReqItemIds;    // i32 +108
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ReqItemCounts; // u16 +128
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 HasEquipped = 0; // u8  +138
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 HaveItemId = -1; // i32 +140
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 HasBagTab = 0;   // u8  +144
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 BagTabA = 0;     // u16 +146
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 BagTabB = 0;     // u16 +148
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 BagSlot = 0;     // u16 +150
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Misc = 0;        // i32 +152
};

// `Data\NPC\Episode\Episode_Table.BIN` (тег "JET", 466 × 288 б). Ключ строки — EpisodeId.
// `CQuest_Episode_FindByWord236@0xC60A30` ищет по `QuestKey` (u16 +236) и отдаёт СТРОКУ с +0;
// `questEpisode_FindWordKeyById@0xC648F0` ищет по `EpisodeId` (i32 +200) и отдаёт `QuestKey`.
USTRUCT(BlueprintType)
struct FAsdaEpisodeRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 EpisodeId = 0;   // i32 +200
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 QuestKey = 0;    // u16 +236
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString Title;         // +0
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Index = 0;
	// ★ Добавлено 2026-08-22 для журнала заданий (окно 700) — три поля, которые уже были в файле,
	//   но в порт не импортировались. Разбор — `Reports/quest_window_impl.md` §1.2.
	// `+204` — имя файла текстов: `./Data/NPC/Episode/BOOK/<QstFile>.QST` (`questBook_LoadQstFile@0xC60C00`).
	//   Оно же — ключ строки `DT_QuestBook`.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString QstFile;
	// `+252` — имя текстуры-иконки слота журнала (`CQuestLog_RefreshSlotIcons@0xC5D500`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") FString IconName;
	// `+284` (u16) — категория эпизода; `CQuest_LoadEpisodeTable` отбирает по `== 2` отдельный список.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Category = 0;
};

// ⚠ Структуры для `Data\Mob\CallMobTable.BIN` здесь НЕТ: она уже есть в порту —
//   `FAsdaCallMobRow` (`Public/Data/AsdaEffectMobTableRows.h`, ассет `/Game/AsdaAssets/Data/DT_CallMob`).
//   Команда 30050 пользуется ИМЕННО ЕЙ; смысл полей уточнён комментарием в том файле.

// `Data\Character\Bin.bbz` → `event_item_list.bin` (тег "IMT", 66 × 148 б). Ключ строки — Index.
// Это `g_pNpcExchangeTable`: `npcExchangeBuyRequest@0xDA3F60` перед 6098 требует все пять пар.
USTRUCT(BlueprintType)
struct FAsdaEventItemRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") int32 Index = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ItemIds;    // +68 + 4*j
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|NPC") TArray<int32> ItemCounts; // +88 + 4*j
};

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  `Data\NPC\RewardTable.BIN` — НАГРАДЫ ЗА КВЕСТЫ (импортёр `Tools/asda_import_quest_reward_table.py`)
//
//  Загрузчик `questLoadRewardTableBin@0xC66190` (quest.c:4007): `fopen("./Data/NPC/RewardTable.BIN")`,
//    шапка 0x14, тег «RTE», `count` из шапки `+4` = 636, запись `0x30` = 48 б.
//    Контроль размером: 20 + 48×636 = 30 548 = размер файла.
//    Массив кладётся в `CQuestMgr+0x544C`, число записей — в `CQuestMgr+0x559C`.
//
//  Потребитель `questBuildRewardSet@0xC663A0` (quest.c:4059) ищет ВСЕ записи с ПАРОЙ
//    (`QuestId` == questId, `Stage` == stage) и раскладывает их по ВОСЬМИ слотам окна наград.
//    ★ Роль поля определяется НЕ типом данных, а НОМЕРОМ СЛОТА, в который оно уходит: массив типов
//      слотов лежит по `CQuestMgr+0x557C` (8×i32), и рисовалка `CQuest_RefreshRewardItemGrid@0xC677B0`
//      (то же в `CQuest_PopulateRewardWindow@0xC66A20`) читает по типу слота РАЗНОЕ поле.
//    Слот копируется целиком: `qmemcpy(0x545C + 36*slot, запись+12, 0x24)` — то есть 36 байт
//      `+12…+47` записи ложатся в слот как есть, а дальше берётся «своё» поле слота:
//        тип 0 (слот 0) → `[0x545C + 36*i]` = запись+12 → рисуется «%d <имя предмета 20551>»
//        тип 1 (слот 1) → `[0x5460 + 36*i]` = запись+16 → «%d <нотис 6010>», иконка "exp2"
//        тип 2, i == 2  → `[0x5464 + 36*i]` / `[0x5468 + 36*i]` = запись+20 / +24
//        тип 2, i != 2  → `[0x546C + 36*i]` / `[0x5470 + 36*i]` = запись+28 / +32
//        тип 2, i = 4…7 → `[0x5478 + 36*i]` / `[0x547C + 36*i]` = запись+40 / +44
//      (смещения сняты дизассемблером ИЗ БИНАРЯ, не из декомпила: типизация `aRewardSlotItems`
//       в эталоне противоречива и по ней раскладка не сходится.)
//    Ветка по questId: диапазон 20000…30000 в ЭТУ таблицу не идёт, там `mapObjFindEntryById`
//      по окну ремесла (`g_pQuestCraftWnd`).
//
//  Ключ строки — `RecordIndex`: у 55 пар (QuestId, Stage) записей БОЛЬШЕ ОДНОЙ (эталон не делает
//    break и складывает их в слоты 4…7 как список «выбери одну награду»), поэтому пара ключом быть
//    не может. RecordIndex в файле сплошной 1…636.
//
//  ⚠ Таблица НЕ подключена ни к попапу наград 540, ни к окну разговора: её потребитель в эталоне —
//    окно журнала квестов и приём пакета 5044, а этой системы в порту пока нет.
USTRUCT(BlueprintType)
struct FAsdaQuestRewardTableRow : public FTableRowBase
{
	GENERATED_BODY()

	// +0 — порядковый номер записи, 1…636 сплошным рядом (ключ строки; проверено: 636 значений,
	//      все различны). Служебного смысла в эталоне не имеет, нужен только для уникальности ключа.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 RecordIndex = 0;

	// +4 — id квеста. Первый ключ поиска в `questBuildRewardSet` (`запись+4 == a2`).
	//      В данных −1…467, 461 различное значение; −1 у четырёх записей 533…536 (мёртвые).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 QuestId = -1;

	// +8 — шаг (stage) квеста. Второй ключ поиска (`запись+8 == a3`). В данных 0…3.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Stage = 0;

	// +12 — ДЕНЬГИ. Уходит в слот типа 0, а слот типа 0 рисуется как «%d <имя предмета 20551>»,
	//       где 20551 = «Золото» (item_list.csv, строка 0). Данные подтверждают: 61 различное
	//       значение, все круглые — 0, 1, 10, 50, 100, 200 … 92000, 120000.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Money = 0;

	// +16 — ОПЫТ. Уходит в слот типа 1: «%d <нотис 6010>» с иконкой "exp2" (жёстко в коде обеих
	//       рисовалок); нотис 6010 = «Опыт» (RawAssets/Text/GameText.csv, ключ "6010").
	//       Данные: 73 различных круглых значения 0, 10, 20 … 80000, 150000.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 Exp = 0;

	// +20/+24 — код и количество ПЕРВОГО предмета-награды (слот 2, тип 2). −1 = слота нет.
	//       Код ищется по цепочке `GetItemRecord` → `GetSowelRecord` → `GetTable2Record`;
	//       сверено: все 111 кодов таблицы находятся в item_list / sowell_list / special_itemlist,
	//       не найденных нет. Количество в данных −1…20.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ItemCode1 = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ItemCount1 = 0;

	// +28/+32 — код и количество ВТОРОГО предмета-награды (слот 3, тип 2). −1 = слота нет.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ItemCode2 = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ItemCount2 = 0;

	// +36 — ФЛАГ «награда на выбор». При != 0 эталон дополнительно копирует блок `+12…+47`
	//       в очередной слот 4…7 (счётчик `v7` растёт на КАЖДОЙ подходящей записи) и ставит ему
	//       тип 2 — так из нескольких записей одной пары (QuestId, Stage) собирается список
	//       «выбери одну». Он же переключает пороги проверок слотов 0…3 с `>= 0` на `> 0`.
	//       В данных только 0/1; флаг = 1 у 76 записей.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ChoiceFlag = 0;

	// +40/+44 — код и количество награды НА ВЫБОР (то, что читают слоты 4…7 по `0x5478`/`0x547C`).
	//       Данные: у ВСЕХ 76 записей с ChoiceFlag = 1 поле +40 заполнено, а ItemCode1/ItemCode2
	//       у них ровно −1 — то есть слоты 4…7 могут показать ТОЛЬКО это поле. Пример: пара
	//       (QuestId 27, Stage 1) — две записи 134/135 с кодами 7732 и 7733, это и есть выбор.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ChoiceItemCode = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Quest") int32 ChoiceItemCount = 0;
};

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОДНА ЯЧЕЙКА ОКНА НАГРАД — результат `questBuildRewardSet@0xC663A0`
//
//  ★ ОБЩАЯ ТОЧКА. Ровно один разбор `RewardTable.BIN` на весь порт: его делает
//    `UAsdaQuestLogSubsystem::BuildRewardLines`, а пользуются им ОБА потребителя эталона —
//      · панель наград журнала заданий (окно 700, строки 786…789 / иконки 1690…1693,
//        эталон `CQuest_RefreshRewardItemGrid@0xC677B0`);
//      · окно приёма задания `QUEST_Confirm_Win` (750, плитки 752…759,
//        эталон `CQuest_PopulateRewardWindow@0xC66A20` — код тождественный).
//    Второй копии поиска по паре (QuestId, Stage) в порту нет.
//
//  ★ РОЛЬ ЯЧЕЙКИ ЗАДАЁТ ЕЁ НОМЕР, а не содержимое (см. таблицу у `FAsdaQuestRewardTableRow`):
//    0 — деньги, 1 — опыт, 2 — предмет 1, 3 — предмет 2, 4…7 — награды «на выбор».
//    Пустая ячейка приходит с `Kind == -1` и НЕ схлопывается: эталон рисует ячейки по фиксированным
//    местам и просто прячет пустые (`if (тип < 0) SetVisible(строка, 0);`).
//
//  ★★★ ОТСТУПЛЕНИЕ 2026-08-23 (решение владельца, `Reports/quest_reward_collapse.md`).
//    Правило выше отменено для БАЗОВЫХ ячеек 0…3: ПУСТАЯ ячейка выкидывается из ряда, и всё
//    правее сдвигается влево, чтобы дыр не оставалось вовсе («сдвигать всегда, когда там
//    пустота»). Делает это `UAsdaQuestLogSubsystem::CollapseRewardCells` — вызывается из общей
//    точки, поэтому оба окна ведут себя одинаково. Порядок наград при этом сохраняется.
//    ⇒ ПОТРЕБИТЕЛЬ ОБЯЗАН читать вид из поля `Kind`, а не выводить его из номера ячейки.
//    ⚠ Ячеек «на выбор» (4…7) это НЕ касается: их список и так плотный, а индекс строки уходит
//      на сервер — схлопывание к нему не прикасается.
//    Ключ отката `asda.Quest.CollapseRewardCells 0`.
struct FAsdaQuestRewardLine
{
	int32 Kind     = -1;   // −1 ячейка пуста, 0 деньги, 1 опыт, 2 предмет (значения массива типов)
	int32 Amount   = 0;    // деньги (вид 0) либо опыт (вид 1)
	int32 ItemCode = -1;   // код предмета/самоцвета (вид 2)
	int32 Count    = 0;    // количество предметов (вид 2)
};
