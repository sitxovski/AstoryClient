// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// СТРОКИ ТАБЛИЦ ПИТОМЦЕВ — 1:1 с СЕРВЕРНЫМИ файлами `GameServer\PetData_Info\*.txt`.
//
// ★★★ ПОЧЕМУ СЕРВЕР, А НЕ КЛИЕНТ (закон `client-server-table-desync`: авторитет — сервер).
//   У клиента есть свои `PetTable.BIN` (32 записи) и `PetExpTable.BIN` (11 записей) — они УЖЕ
//   импортированы в порт как `DT_pettable` / `DT_petexptable` и остаются источником ВИДА питомца
//   (имя для UI, модель, иконка, грейд). Но ПРАВИЛА (предел уровня, скорость опыта, бонусы,
//   корм, распад сытости, рецепты эволюции, пороги броска) считает сервер и только по своим
//   файлам. Клиентских таблиц с этими числами нет вовсе либо они расходятся:
//     • породы: клиент 32 записи (id 0..31), сервер 31 (id 0..30). Порода 31 у сервера
//       НЕДОСТИЖИМА — `PetIndex < PetInfoCount` (GameServer.c:34628). В UI её показывать нельзя;
//     • уровни: клиент 11 записей (0..10), сервер 12 (0..11). Сервер читает `PetExpTable[Level+1]`
//       при капе `Level < 10` (GameServer.c:35298-35304), строка 11 — запирающая (999999999).
//   Поэтому здесь заведены ОТДЕЛЬНЫЕ таблицы серверной стороны; клиентские не трогаются.
//
// ★ РАСКЛАДКА ПОЛЕЙ СНЯТА С ЗАГРУЗЧИКА СЕРВЕРА, а не с вида файла:
//   `CParamManager::LoadPetTables` — `RE/ida/server_work/GameServer.c:69613-69833`. У каждого
//   поля ниже стоит смещение в серверной записи и номер колонки в .txt.
//
// ★ ИМПОРТ: `AstoryClient/Tools/asda_import_pet_server_tables.py` (JSON-копия исходника —
//   `RawAssets/Data/Pet/`, закон «сырым файлам в Content не место»). Руками эти таблицы не
//   правятся: перегенерация сотрёт (закон `itemdb-header-generated-not-manual`).
// ═══════════════════════════════════════════════════════════════════════════════════════════════
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaPetTableRows.generated.h"

// ───────────────────────────────────────────────────────────────────────────────────────────────
// `PetInfo_Table.txt` — ПОРОДЫ. Запись 60 байт, 31 строка (id 0..30), 17 колонок.
//   Загрузчик: GameServer.c:69630-69677. Имя строки DataTable = `PetIndex`.
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaPetInfoRow : public FTableRowBase
{
	GENERATED_BODY()

	/** +0 u16, колонка 1 — индекс породы. Он же ключ строки и значение `PetSlot+4`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 PetIndex = -1;

	/** +2 char[16], колонка 2 — ДЕФОЛТНОЕ имя породы в кодировке cp949 (корейская).
	 *  ★ Это НЕ имя для UI. Оно нужно ровно одному правилу: сервер разрешает переименование
	 *    только пока текущее имя питомца совпадает с этим (GameServer.c:36262-36278). Для показа
	 *    берётся имя из клиентской `DT_pettable` (см. шапку). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") FString DefaultName;

	/** +2 char[16] в виде hex — ТОЧНЫЕ байты имени. Сравнение имён (гейт переименования) идёт
	 *  по байтам, а не по перекодированной строке: cp949 в UTF-8 туда-обратно не круговая. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") FString DefaultNameHex;

	/** +18 u8, колонка 3 — назначение НЕ установлено (закон `dead-code-markings-suspect`:
	 *  потребителя в разобранном срезе `GameServer.c` не найдено). Значения в данных: 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 Unk18 = 0;

	/** +19 u8, колонка 4 — назначение НЕ установлено, потребителя в срезе нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 Unk19 = 0;

	/** +20 u8, колонка 5 — БАЗОВЫЙ предел уровня породы. Читается двумя местами:
	 *  запись в `PetRecord.MaxLevel` при выдаче питомца и ОТКАТ при крупном провале лимит-брейка
	 *  без премиума: `MaxLevel = BaseMaxLevel`, `Level = BaseMaxLevel − 1` (GameServer.c:36782-36786). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 BaseMaxLevel = 0;

	/** +21 u8, колонка 6 — множитель опыта ×10. Порог уровня = `PetExp[Level+1].Exp · это / 10`
	 *  (GameServer.c:35304, 36723-36724, 36032). 10 = ×1.0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 ExpRateX10 = 0;

	/** +22 i32, колонка 7 — назначение НЕ установлено, потребителя в срезе нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 Unk22 = 0;

	/** +26 i32, колонка 8 — назначение НЕ установлено, потребителя в срезе нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 Unk26 = 0;

	/** +30 i32, колонка 9 — назначение НЕ установлено, потребителя в срезе нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 Unk30 = 0;

	/** +34/+40/+46 u16, колонки 10/12/14 — ТИП бонуса (0 = бонуса нет).
	 *  Массив ВСЕГДА 3 элемента: пишется `Init(0, 3)`, `Reset()` запрещён
	 *  (закон `tarray-reset-breaks-fixed-length`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") TArray<int32> AbilType;

	/** +36/+42/+48 i32, колонки 11/13/15 — БАЗОВОЕ значение бонуса. Итог на сервере:
	 *  `Value = PetExp[Level].AbilRatePercent · это / 100`, затем штраф по сытости
	 *  (≤39 → 0, ≤59 → ×80 %, ≤79 → ×90 %) — `sub_459AD0`, GameServer.c:36357. Массив всегда 3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") TArray<int32> AbilValue;

	/** +52 i32, колонка 16 — БИТОВАЯ МАСКА разрешённого корма. Отказ кормления (код 5), если
	 *  `(маска & (1 << subIndex предмета)) == 0` (GameServer.c:35579-35582). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 FeedTypeMask = 0;

	/** +56 i32, колонка 17 — период тика сытости, мс: раз в столько `Fullness--`
	 *  (`sub_456840`, GameServer.c:35148-35236). В данных 56571…88714 мс. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetInfo") int32 FullnessDecayMs = 0;

	FAsdaPetInfoRow()
	{
		AbilType.Init(0, 3);
		AbilValue.Init(0, 3);
	}
};

// ───────────────────────────────────────────────────────────────────────────────────────────────
// `PetExp_Table.txt` — УРОВНИ. Запись 13 байт, 12 строк (0..11), 4 колонки.
//   Загрузчик: GameServer.c:69686-69712. Имя строки = `Level`.
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaPetExpRow : public FTableRowBase
{
	GENERATED_BODY()

	/** +0 u8, колонка 1 — уровень (0..11). Индекс записи == уровень. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetExp") int32 Level = 0;

	/** +1 i32, колонка 2 — БАЗОВЫЙ порог опыта. Реальный порог перехода на `Level+1` =
	 *  `Exp · PetInfo[порода].ExpRateX10 / 10` (GameServer.c:35304). Строка 11 — запирающая
	 *  (999999999): кап всё равно раньше, `Level < 10 && Level < MaxLevel`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetExp") int32 Exp = 0;

	/** +5 i32, колонка 3 — процент, которым множится базовый бонус породы на этом уровне
	 *  (`Value = AbilValue · это / 100`, GameServer.c:36357). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetExp") int32 AbilRatePercent = 0;

	/** +9 i32, колонка 4 — назначение НЕ установлено: потребителя в разобранном срезе
	 *  `GameServer.c` не найдено. Данные растут 1000…999977999. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetExp") int32 Unk9 = 0;
};

// ───────────────────────────────────────────────────────────────────────────────────────────────
// `PetIncubateRate_Table.txt` — ШАНС ВЫВЕДЕНИЯ. Запись 5 байт, 10 строк, 2 колонки.
//   Загрузчик: GameServer.c:69715-69742. Бросок — GameServer.c:34663-34668, целиком серверный.
//   Имя строки = `Grade` (грейд яйца из item-БД).
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaPetIncubateRateRow : public FTableRowBase
{
	GENERATED_BODY()

	/** +0 u8, колонка 1 — грейд яйца (байт +148 записи cash-предмета, шаг 172). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetIncubate") int32 Grade = 0;

	/** +1 i32, колонка 2 — шанс из 100000. Грейд 8 = 100000 (гарантия).
	 *  ★★★ ТАБЛИЦА ПРИМЕНЯЕТСЯ ТОЛЬКО К CASH-ЯЙЦАМ (найдено ревизией 2026-09-21, перепроверено
	 *  здесь построчно). Ветка `sub_532A90(itemCode)` = «это cash-предмет» берёт грейд из +148 и
	 *  читает эту таблицу (GameServer.c:34619-34621); ветка обычного предмета (`sub_532B10`)
	 *  ставит `v15 = 100000` БЕЗУСЛОВНО (GameServer.c:34639). ⇒ обычное яйцо вылупляется
	 *  ГАРАНТИРОВАННО, и окно 42000 не должно обещать игроку иное. В отчёте этого нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetIncubate") int32 Rate = 0;
};

// ───────────────────────────────────────────────────────────────────────────────────────────────
// `Petdetailevotable.txt` — РЕЦЕПТЫ ЭВОЛЮЦИИ (юниона). Запись 18 байт, 9 строк, 9 колонок.
//   Загрузчик: GameServer.c:69745-69795. Потребитель: `Recv_PetUnion_Req`, GameServer.c:36013-36032.
//
// ★★★ НАПРАВЛЕНИЕ ТАБЛИЦЫ. В `Reports/pet_system_re.md` §1.4-1.5 оно ПЕРЕВЁРНУТО
//   («строка 0 = результат, строки 1..6 = ингредиенты»). Верное направление доказано ДВАЖДЫ:
//     1) загрузчик кладёт колонки строго по порядку c1→+2, c2→+4, c3/c4/c5→+6/+10/+14,
//        c6/c7/c8→+8/+12/+16 (GameServer.c:69777-69794);
//     2) обработчик юниона читает `+2` как индекс в `PetInfo_Table` (ResultPetIndex),
//        `+4` как индекс в `PetExpTable` с проверкой `≤ 10` (ResultPetLevel),
//        `+6+4j` сравнивает с `PetSlot.PetIndex`, `+8+4j` — с `PetSlot.Level` через `>=`
//        (GameServer.c:36000-36012, 36021-36024).
//   Строка 0 `0 21 4 13 1 10 4 3 3` читается так: РЕЗУЛЬТАТ — порода 21 уровня 4;
//   НУЖНЫ породы 13 (ур. 4), 1 (ур. 3), 10 (ур. 3).
//     3) ТРЕТИЙ, независимый от декомпила метод — прямой дамп клиентского `Data/PetStuff.bin`
//        (тег PDE, заголовок 20 б, 9 записей по 9 i32, файл 344 б, md5 85b26c4208295eb83c8a0aff721759af):
//        все девять записей ЗНАЧЕНИЕ В ЗНАЧЕНИЕ равны строкам `Petdetailevotable.txt`
//        (проверено 2026-09-21). Клиент и сервер читают одни и те же числа в одном порядке,
//        то есть раскладка не может быть «перевёрнутой» ни у одного из них.
//
// ★ Имя строки = `UnionIndex`. Индексы ОБЯЗАНЫ идти подряд: при разрыве оригинал бьёт
//   `MessageBoxA("Petdetailevotable.txt Sync Error")` и отказывается стартовать (GameServer.c:69773).
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaPetUnionRecipeRow : public FTableRowBase
{
	GENERATED_BODY()

	/** +0 i16, колонка 1 — номер рецепта. Именно он едет в теле 6112 (`unionTableIndex`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetUnion") int32 UnionIndex = -1;

	/** +2 i16, колонка 2 — порода РЕЗУЛЬТАТА (индекс `PetInfo_Table`). Отказ 7, если вне
	 *  `[0, PetInfoCount)` (GameServer.c:36039-36040). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetUnion") int32 ResultPetIndex = -1;

	/** +4 i16, колонка 3 — уровень РЕЗУЛЬТАТА (индекс `PetExp_Table`). Отказ 7, если `> 10`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetUnion") int32 ResultPetLevel = 0;

	/** +6/+10/+14 i16, колонки 4/5/6 — ТРЕБУЕМАЯ порода материала j. Массив всегда 3 элемента. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetUnion") TArray<int32> ReqPetIndex;

	/** +8/+12/+16 i16, колонки 7/8/9 — ТРЕБУЕМЫЙ минимальный уровень материала j (сравнение `>=`).
	 *  Массив всегда 3 элемента. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetUnion") TArray<int32> ReqLevel;

	FAsdaPetUnionRecipeRow()
	{
		ReqPetIndex.Init(-1, 3);
		ReqLevel.Init(0, 3);
	}
};

// ───────────────────────────────────────────────────────────────────────────────────────────────
// `Petlimitbreaktable.txt` — ПОРОГИ БРОСКА ЛИМИТ-БРЕЙКА. Запись 16 байт, 10 строк.
//   Загрузчик: GameServer.c:69797-69829. Потребитель: GameServer.c:36724-36775 —
//   `rand = sub_5329A0(0, 100000)`, индекс строки = ТЕКУЩИЙ `MaxLevel` питомца, читаются +0/+4/+8:
//     rand < [+0] → исход 1 (MaxLevel+1, опыт = середина следующего уровня)
//     rand < [+4] → исход 2 (MaxLevel+1, опыт = порог)
//     rand < [+8] → исход 3 (провал; без премиума Level−1)
//     иначе       → исход 4 (крупный провал; без премиума откат к BaseMaxLevel)
//
// ★★★ ДЕФЕКТ ОРИГИНАЛА — РАССИНХРОН ЗАГРУЗЧИКА И ФАЙЛА (найден 2026-09-21, в прежних
//   материалах его нет). В файле у строки ПЯТЬ колонок (`Level Thr1 Thr2 Thr3 100000`), а
//   загрузчик читает `fscanf("%s")` РОВНО ЧЕТЫРЕ на строку (GameServer.c:69814-69828). Поток
//   токенов сдвигается на один с каждой строкой: в память строки 0 попадает `1 25000 50000 75000`,
//   в строку 1 — `100000 2 25000 50000`, в строку 2 — `75000 100000 3 25000` и так далее.
//   Контроль (второй метод): у ВСЕХ ОСТАЛЬНЫХ пет-таблиц число `fscanf` на строку совпадает с
//   числом колонок файла — PetInfo 17/17 (:69635-69675), PetExp 4/4 (:69701-69712),
//   PetIncubateRate 2/2 (:69735-69742), Petdetailevotable 9/9 (:69766-69794). Расходится
//   ТОЛЬКО эта, 4/5. Значит это не ошибка чтения декомпила, а настоящий дефект оригинала.
//   Поэтому здесь ДВА набора полей:
//     • `Level`/`Thr1..Thr4` — колонки ФАЙЛА, как их задумывал издатель;
//     • `ServerD0..ServerD3` — четыре dword'а ТАК, КАК ИХ РЕАЛЬНО КЛАДЁТ ЗАГРУЗЧИК СЕРВЕРА
//       (воспроизведён поток токенов). Именно эти числа определяют исход броска на ЖИВОМ сервере.
//   ФАКТИЧЕСКИЕ пороги в памяти сервера по `MaxLevel` (сошлись у двух независимых проверок):
//       2 → 75000 / 100000 / 3      3 → 50000 / 75000 / 100000   4 → 20000 / 45000 / 70000
//       5 → 5 / 20000 / 45000       6 → 100000 / 6 / 15000       7 → 65000 / 100000 / 7
//       8 → 35000 / 60000 / 100000  9 → 7000 / 20000 / 45000
//   Последствия видны глазом: на `MaxLevel == 6` бросок ВСЕГДА даёт исход 1 (порог 100000),
//   а на `MaxLevel == 5` почти всегда проваливается (порог 5 из 100000).
//   ★ ПО УМОЛЧАНИЮ ПОРТ ВОСПРОИЗВОДИТ СЕРВЕР 1:1 — `ServerD*` (закон `never-invent-only-original`).
//     Переключатель на «как задумано» — ключ `asda.Pet.LimitBreakFixTable 1`; лечение и его
//     уместность решает ВЛАДЕЛЕЦ (закон `fix-original-defects`: исправления помечать).
//     Клиент броска не делает — он показывает исход; значение имеет только предсказание в UI.
//   ⚠ НЕ ПРОВЕРЕНО на живом образе (`\\@@ASTORY_SERVER_HOSTNAME@@\h\ASDA\Working_ASDA\v2` из этой сессии не читается):
//     там файл может оказаться уже четырёхколоночным, и тогда сдвига нет.
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaPetLimitBreakRow : public FTableRowBase
{
	GENERATED_BODY()

	/** ★ ИНДЕКС ЗАПИСИ В МАССИВЕ (0..9). Он же имя строки DataTable. ИМЕННО ИМ сервер адресует
	 *  таблицу: `base + 16 * MaxLevel` (GameServer.c:36725/36727/36729) — то есть ключ поиска
	 *  это `MaxLevel` питомца, сопоставленный с ПОРЯДКОВЫМ НОМЕРОМ записи.
	 *  ⚠ Ключевать по колонке `Level` файла НЕЛЬЗЯ: она идёт 1..10 и отличается от индекса на
	 *    единицу даже без дефекта ПД4 — получился бы сдвиг ещё на одну строку поверх сдвига ПД4. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 RowIndex = 0;

	/** Колонка 1 файла — «уровень», как его подписал издатель (1..10). Справочное поле:
	 *  сервер по нему ничего не ищет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 Level = 0;

	/** Колонка 2 файла — порог исхода 1 (полный успех) из 100000. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 Thr1 = 0;

	/** Колонка 3 файла — порог исхода 2 (успех) из 100000. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 Thr2 = 0;

	/** Колонка 4 файла — порог исхода 3 (провал) из 100000. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 Thr3 = 0;

	/** Колонка 5 файла — всегда 100000 (граница исхода 4). Загрузчик сервера её НЕ читает. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 Thr4 = 0;

	/** Байты +0 записи сервера так, как их кладёт загрузчик со сдвигом (см. шапку). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 ServerD0 = 0;

	/** Байты +4 записи сервера (порог исхода 1 в ЖИВОЙ памяти сервера). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 ServerD1 = 0;

	/** Байты +8 записи сервера (порог исхода 2 в ЖИВОЙ памяти сервера). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 ServerD2 = 0;

	/** Байты +12 записи сервера. Обработчик броска их не читает — поле дано для полноты 1:1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetLimitBreak") int32 ServerD3 = 0;
};

// ───────────────────────────────────────────────────────────────────────────────────────────────
// КАРТА РАЗВИТИЯ — `Data/PetEvolutionTable.bin` КЛИЕНТА (тег PEV, 88 б × 3, файл 284 б).
//   Загрузчик оригинала — `CPetMgr_LoadPetEvolutionTable@0xEDEED0` (npc_mob_pet.c:7524).
//   Это источник ЛЕВОЙ ПАНЕЛИ окна 43500: семь стадий 43510..43516 с подписями и глифами.
//
// ★ СЕРВЕР ЭТУ ТАБЛИЦУ НЕ ЧИТАЕТ: парсера в диапазоне загрузчика (GameServer.c:69613-69833) нет,
//   сервер открывает пять файлов из семи. Серверный `Petevotable.txt` — это РОВНО колонка
//   `StuffIdx` из PEV (сверено побайтово), то есть треть данных. Поэтому источником взят
//   КЛИЕНТСКИЙ .bin: в нём есть ещё `PetType[7]` и `Level[7]`, без которых панель не собрать.
//   Расхождения клиент/сервер тут нет — есть неполнота серверной копии.
//
// ★ ФОРМАТ СНЯТ С ФАЙЛА (дамп 2026-09-21, не догадка): заголовок 20 б = `i32 0`, `i32 count`,
//   `i32 dwordsPerRecord`, тег `"PEV\0"`, `i32 0`; далее `count` записей по `dwordsPerRecord`
//   двойных слов. Здесь count = 3, dwordsPerRecord = 22 ⇒ запись 88 байт:
//     +0 i32 EvoIndex │ +4 i32 PetType[7] │ +32 i32 Level[7] │ +60 i32 StuffIdx[7].
//   Строка 0: PetType = (13, 21, 22, 23, −1, −1, −1),
//   Level = (1, 4, 4, 4, −1, −1, −1), StuffIdx = (−1, 0, 1, 2, −1, −1, −1) — то есть стадия 0
//   (порода 13 уровня 1) БАЗОВАЯ и рецепта не имеет, а стадии 1..3 — результаты рецептов 0/1/2.
//
// ★ ГАСИТЬ СТАДИИ ПО `PetType == -1`, А НЕ ПО ЧИСЛУ КОНТРОЛОВ: в данных стадии 4..6 пустые во
//   всех трёх строках, а окно биндит все семь (ui_core.c:89827-89843). Если считать «сколько
//   пришло», панель окажется на три ячейки короче, чем размечено в ассете.
// ★ Имя строки = `EvoIndex` (0..2).
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaPetEvoStageRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Номер карты развития (0..2) — индекс записи в PEV. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetEvo") int32 EvoIndex = -1;

	/** +4, 7 × i32 — ПОРОДА на каждой из семи стадий. −1 = стадии нет (ячейку гасить). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetEvo") TArray<int32> PetType;

	/** +32, 7 × i32 — УРОВЕНЬ породы на стадии. −1 = стадии нет. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetEvo") TArray<int32> Level;

	/** +60, 7 × i32 — номер рецепта `Petdetailevotable` для стадии; −1 = базовая стадия
	 *  (рецепта нет, питомец получается не эволюцией). Побайтово совпадает с серверным
	 *  `Petevotable.txt`. Массивы ВСЕГДА по 7 элементов (закон `tarray-reset-breaks-fixed-length`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PetEvo") TArray<int32> StuffIdx;

	FAsdaPetEvoStageRow()
	{
		PetType.Init(-1, 7);
		Level.Init(-1, 7);
		StuffIdx.Init(-1, 7);
	}
};
