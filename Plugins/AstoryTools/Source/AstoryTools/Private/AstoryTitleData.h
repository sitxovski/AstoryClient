// Copyright (c) 2026 @sitxovski (Discord)
// AstoryTitleData — МОДЕЛЬ ДАННЫХ редактора достижений (званий, Fame).
//
// Источники (порядок = порядок загрузки):
//   1. RawAssets/Data/ItemDB/title_list.csv    — то, ИЗ ЧЕГО импортируется DT_title_list
//                                                 (именно его читает порт). 280 записей, id 0…279.
//   2. RawAssets/Data/Character/title_list.bin — исходный бинарь клиента (header 20 б +
//                                                 280 × 256, где 256 = MD5 16 + запись 240).
//                                                 Нужен для обратной сборки и как эталон.
//   3. RawAssets/Text/GameText.csv             — строки локализации (error list.bin):
//                                                 грейд 1450+v, категория 1460+v, позиция 1466/1467.
//   4. <сервер>/GameServer/ConfigSet/FameInfo.txt — серверный близнец таблицы, 251 запись,
//                                                 46 колонок. ★ ТОЛЬКО ЧТЕНИЕ.
//
// ★★ Раскладка записи снята по коду окна 18700 (см. `Reports/title_system.md`).
//   Прежние имена `EquipFlag` / `TypeLocIndex` / `Param96` — МИСНЕЙМЫ, здесь не используются.
//   Смещения, которые дешифровка трогает, но потребитель которых не найден, честно называются
//   `UnkNN` и помечены `bIdentified = false` — выдумывать им имена нельзя.
#pragma once

#include "CoreMinimal.h"

// ─────────────────────────────────────────────────────────────────────────────
// Описание поля записи
// ─────────────────────────────────────────────────────────────────────────────
enum class EAstoryFieldType : uint8
{
	Int32,
	String       // строка фикс. длины в CP1251 внутри записи
};

// Смысловая группа поля — по ней поля раскладываются в карточке записи.
enum class EAstoryFieldGroup : uint8
{
	Identity,     // идентификация
	Display,      // отображение
	Conditions,   // условия получения
	Bonuses,      // бонусы (тип берётся из title_option)
	Unidentified  // не опознано
};

// Тип перечисления значений: подставляет читаемую подпись вместо голого числа.
enum class EAstoryFieldEnum : uint8
{
	None,
	Grade,        // ColorIndex  → loc 1450+v (Обычный…Мифический)
	Group,        // Group       → loc 2405/2406/2407 (вкладка окна)
	Position,     // Position    → loc 1466/1467 (приставка/окончание)
	Category,     // CategoryLoc → loc 1460+v (Монстр/Уровень/…)
	ClassReq,     // 0 любой · 1 class≤4 · 2 class 12…15 · 3 class 23…26
	Enabled       // 0 не показывать · 1/2 показывать
};

struct FAstoryTitleFieldDef
{
	int32 Offset = 0;                       // смещение в записи (240 б)
	EAstoryFieldType Type = EAstoryFieldType::Int32;
	int32 ByteLen = 4;                      // длина поля в байтах
	const TCHAR* CsvName = nullptr;         // имя колонки в CSV; nullptr = колонки нет (только DataHex)
	const TCHAR* Label = nullptr;           // подпись в карточке
	const TCHAR* Tip = nullptr;             // подсказка: что поле значит
	EAstoryFieldGroup Group = EAstoryFieldGroup::Identity;
	EAstoryFieldEnum Enum = EAstoryFieldEnum::None;
	bool bIdentified = true;                // false = смысл не установлен
	bool bInList = false;                   // показывать колонкой в таблице по умолчанию
};

// Полная раскладка записи title_list (240 б). Определена в .cpp.
ASTORYTOOLS_API const TArray<FAstoryTitleFieldDef>& AstoryTitleFields();

// Смещения, которые важны коду напрямую.
namespace AstoryTitleOffsets
{
	static constexpr int32 Id = 0;
	static constexpr int32 SortNum = 4;
	static constexpr int32 ClassReq = 12;
	static constexpr int32 Name = 16;
	static constexpr int32 ColorIndex = 80;
	static constexpr int32 Group = 84;
	static constexpr int32 Position = 88;
	static constexpr int32 CategoryLocIndex = 92;
	static constexpr int32 Point = 96;
	static constexpr int32 Desc = 100;
	static constexpr int32 OptionTypeId0 = 176;
	static constexpr int32 Enabled = 200;
	static constexpr int32 IsPercent0 = 204;
}

// ★★★ ПРЕДЕЛ ЗАГРУЗЧИКА СЕРВЕРА — СНЯТ ПАТЧЕМ P21 (30.08.2026).
//   Было: `sub_4A18F0 @ 0x4A18F0` (из `LoadFameInfo @ 0x49F410`) проверял
//       if ( count > 0 && count <= 256 ) ... иначе «FameInfo count over» и
//   таблица не грузилась ВООБЩЕ — сервер валился в exit(1).
//   Стало: иммедиат по адресу 0x4A19AA поднят 0x100 -> 0x200, массив записей
//   выделяется с запасом (0x4A2824). Патч установлен и проверен на живом сервере.
//   ⇒ Инструмент работает с ПОЛНОЙ таблицей и на предел 256 больше не оглядывается.
static constexpr int32 AstoryServerFameMaxRows = 512;

// ★★ Маска «звание получено» расширена до 512 бит (опкоды 6066: 42 -> 74 б,
//    6086: 38 -> 70 б; формат самоописателен по длине тела). Звания id 256…511
//    достижимы: проверено на живом сервере — id 279 «Зомби» выдан и отображается.
static constexpr int32 AstoryFameMaskBits = 512;

// ─────────────────────────────────────────────────────────────────────────────
// Запись
// ─────────────────────────────────────────────────────────────────────────────
struct FAstoryTitleRecord
{
	int32 RowIndex = 0;                     // номер строки в CSV (колонка `_row`)
	TMap<int32, int32> Ints;                // смещение → значение (целые поля)
	TMap<int32, FString> Strs;              // смещение → значение (строковые поля)
	TArray<uint8> Payload;                  // 240 б ДЕШИФРОВАННОЙ записи (источник для .bin и DataHex)

	int32 GetInt(int32 Offset) const { const int32* P = Ints.Find(Offset); return P ? *P : 0; }
	FString GetStr(int32 Offset) const { const FString* P = Strs.Find(Offset); return P ? *P : FString(); }

	// Присвоение ОДНОВРЕМЕННО правит Payload — иначе CSV и .bin разойдутся.
	void SetInt(int32 Offset, int32 Value);
	void SetStr(int32 Offset, int32 ByteLen, const FString& Value);

	int32 GetId() const { return GetInt(AstoryTitleOffsets::Id); }
	FString GetName() const { return GetStr(AstoryTitleOffsets::Name); }
	bool IsUnreachable() const { return GetId() >= AstoryFameMaskBits; }
	bool IsDisabled() const { return GetInt(AstoryTitleOffsets::Enabled) == 0; }

	// ★ ДЕФЕКТ ДАННЫХ: описание обрезано в самом title_list.bin (не UI его режет).
	//   Два независимых признака:
	//     1) текст занял ВСЁ поле (64 байта CP1251) — конец фразы физически не поместился;
	//     2) след ошибки конвертера, резавшего строку на ПЕРВОЙ букве «я»: в шифре «я» — это
	//        байт 0x00, и наивный читатель принимает его за конец строки (закон
	//        `item-string-cipher-terminator`). Такая строка не оканчивается знаком препинания
	//        И не содержит ни одной «я».
	//   Оба признака — ПОДОЗРЕНИЕ, а не доказательство: восстановить исходную фразу можно только
	//   по внешнему эталону, поэтому редактор помечает запись, а не «чинит» её сам.
	bool IsDescLikelyTruncated() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Серверная строка FameInfo.txt
// ─────────────────────────────────────────────────────────────────────────────
struct FAstoryFameServerRow
{
	int32 Id = 0;
	TArray<int32> Cols;                     // все колонки строки как есть (обычно 46)
};

// Соответствие «клиентское поле ↔ серверная колонка». Расширяется по мере реверса.
struct FAstoryServerColMap
{
	int32 ClientOffset;
	int32 ServerCol;
	const TCHAR* Label;
};
ASTORYTOOLS_API const TArray<FAstoryServerColMap>& AstoryServerColMap();

// ─────────────────────────────────────────────────────────────────────────────
// Проблемы валидации
// ─────────────────────────────────────────────────────────────────────────────
enum class EAstoryIssueKind : uint8
{
	DuplicateId,      // два звания с одним id
	EmptyName,        // пустое имя
	BadCategory,      // CategoryLocIndex вне 0…5 ⇒ loc 1460+v вне 1460…1465
	MissingLoc,       // ссылка на несуществующую строку локализации
	UnreachableId,    // id ≥ 256 — недостижимо (маска 256 бит)
	TruncatedDesc,    // описание обрезано в самих данных
	IdOutOfRange,     // id < 0
	ServerMissing,    // серверной строки с таким id нет
	ServerMismatch    // значение расходится с сервером
};

struct FAstoryIssue
{
	int32 RowIndex = INDEX_NONE;
	int32 Id = 0;
	EAstoryIssueKind Kind = EAstoryIssueKind::DuplicateId;
	FString Text;
	// ★ Дефект БЫЛ в данных ещё до этой сессии. Такие не мешают сохранять: иначе один
	//   унаследованный дефект (например, 15 записей с категорией вне 1460…1465) навсегда
	//   запретил бы любую несвязанную правку.
	bool bPreExisting = false;

	// Блокирует ли сохранение ПРЯМО СЕЙЧАС: ошибка серьёзная И внесена в этой сессии.
	bool IsBlockingNow() const { return IsBlocking() && !bPreExisting; }

	// Серьёзность сама по себе. Расхождения с сервером и недостижимые id — это состояние
	//   ДАННЫХ, а не ошибка правки: они предупреждают, но сохранить не мешают.
	bool IsBlocking() const
	{
		return Kind == EAstoryIssueKind::DuplicateId
		    || Kind == EAstoryIssueKind::EmptyName
		    || Kind == EAstoryIssueKind::BadCategory
		    || Kind == EAstoryIssueKind::MissingLoc
		    || Kind == EAstoryIssueKind::IdOutOfRange;
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Модель
// ─────────────────────────────────────────────────────────────────────────────
class FAstoryTitleData
{
public:
	// Загрузка всех источников. Возвращает false + текст ошибки, если нет обязательных файлов.
	bool LoadAll(FString& OutError);

	// ── доступ ──
	TArray<FAstoryTitleRecord>& GetRecords() { return Records; }
	const TArray<FAstoryTitleRecord>& GetRecords() const { return Records; }
	const TMap<int32, FAstoryFameServerRow>& GetServerRows() const { return ServerRows; }
	const FString& GetServerFilePath() const { return ServerFilePath; }
	const FString& GetServerFileHash() const { return ServerFileHash; }

	// Строка локализации по id ("" если нет). `bExists` — есть ли такая строка вообще.
	FString Loc(int32 Id, bool* bExists = nullptr) const;

	// ── сверка с сервером ──
	// Расхождения записи: список «поле: клиент ≠ сервер». Пусто = совпадает.
	TArray<FString> DiffWithServer(const FAstoryTitleRecord& Rec) const;
	bool HasServerRow(int32 Id) const { return ServerRows.Contains(Id); }

	// ── валидация ──
	//   Возвращённые проблемы помечены `bPreExisting`: были ли они в данных до этой сессии.
	TArray<FAstoryIssue> Validate() const;
	// Та же проверка над произвольным набором записей (нужна, чтобы оценить исходное состояние).
	TArray<FAstoryIssue> ValidateRecords(const TArray<FAstoryTitleRecord>& InRecords) const;

	// ── сохранение ──
	// Пишет CSV + JSON (с резервной копией), переимпортирует DT_title_list и сохраняет ассет.
	//   bAlsoWriteBin — дополнительно пересобрать title_list.bin (с побайтовой самопроверкой).
	//   `bSavePackage` — сохранять ли .uasset DataTable. Обычная работа: да. Автопроверка ставит
	//   false, чтобы не писать ассет, пока владелец держит проект открытым в своём редакторе.
	bool Save(bool bAlsoWriteBin, FString& OutLog, FString& OutError, bool bSavePackage = true);

	// ── сериализация (те же байты, что уходят в файлы; вынесено ради самопроверки) ──
	FString SerializeCsv() const;
	FString SerializeJson() const;

	// ★★ ПРИМЕНЕНИЕ ПАТЧА ТЕКСТОВ (имена и описания) через сам редактор.
	//   Файл патча — CSV `Id,Field,Value,Origin`, поле `Field` = `Name` или `Desc`.
	//   Готовит его `Tools/asda_title_merge.py` из объединённой таблицы.
	//   Проходит тот же путь, что и ручная правка: SetStr -> DataHex -> валидация ->
	//   резервная копия -> CSV/JSON -> переимпорт DataTable -> .bin с побайтовой самопроверкой.
	//   ★ Строка длиннее 63 байт CP1251 ОТКЛОНЯЕТСЯ: 64-байтовое поле минус запас под дефект D8
	//   (тултип оригинала копирует поле как memcpy(..., 0x3F) и съедает последний символ).
	//   ★ `bExportServer` — собрать заодно выгрузку серверного FameInfo.txt. Делать это можно
	//   ТОЛЬКО здесь, внутри той же загрузки: режим «только мои правки» опирается на снимок
	//   `Baseline`, снятый при загрузке, а `Save()` в конце заменяет снимок сохранённым. Отдельной
	//   командой после патча выгрузка увидела бы НОЛЬ правок (закон: не резать охват).
	static void ApplyTextPatch(const FString& PatchCsvPath, bool bWriteBin, FString& OutReport,
	                           bool bExportServer = false);

	// ★★ ПРОВЕРКА ВВОДА-ВЫВОДА: доказывает числами, что правка доезжает туда, куда надо.
	//   Клиент: правим одно поле -> сохраняем -> читаем обратно ИЗ ФАЙЛА, ИЗ DataTable и ИЗ .bin ->
	//           возвращаем как было -> сверяем все три файла с исходными БАЙТ В БАЙТ.
	//   Сервер: полный цикл «прочитать -> ничего не менять -> собрать» обязан дать 0 расхождений;
	//           затем цикл с ОДНОЙ правкой обязан отличаться РОВНО в одной позиции — и именно там,
	//           где стоит правленое поле.
	//   ⚠ Живой серверный файл при этом только читается.
	static void RunIoVerification(FString& OutReport);

	// ★ Полная самопроверка без записи на диск: обратная сборка .bin побайтово + совпадение
	//   заново сериализованных CSV/JSON с тем, что лежит на диске. Доказывает, что редактор
	//   ничего не портит при сохранении «вхолостую».
	static void RunSelfCheck(FString& OutReport);

	// ── обратная сборка .bin ──
	// Самопроверка: разобрать исходный .bin и собрать обратно БЕЗ правок. Результат обязан
	//   совпасть с оригиналом побайтово — это доказательство, что формат понят верно.
	static bool SelfTestBinRoundTrip(const FString& BinPath, FString& OutReport);

	// Собрать .bin из текущих записей. Возвращает готовые байты файла.
	bool BuildBin(TArray<uint8>& OutBytes, FString& OutError) const;

	// ── экспорт ──
	// CSV произвольной выборки записей (человекочитаемый: с подставленными подписями).
	bool ExportSelectionCsv(const TArray<int32>& RowIndices, const FString& FilePath, FString& OutError) const;

	// Серверный FameInfo.txt, пересобранный по клиентской таблице (для выгрузки владельцу).
	//   ★ Меняются ТОЛЬКО сопоставленные колонки (см. AstoryServerColMap) у тех id, что есть
	//   у сервера; всё остальное содержимое строки переносится 1:1, включая форматирование
	//   (в файле встречаются значения с хвостовым пробелом — «15 »; поэтому сравнение
	//   ЧИСЛОВОЕ, а нетронутые ячейки сохраняются побайтово).
	//   Новые строки НЕ добавляются: у загрузчика жёсткий предел 256 записей и своя нумерация —
	//   это решение владельца, а не редактора.
	//
	//   ★★ `bAllMappedColumns`:
	//     false (по умолчанию) — переносятся ТОЛЬКО ПРАВКИ ЭТОЙ СЕССИИ: значения, отличающиеся
	//        от того, что было при загрузке. Это безопасное поведение: расхождения, возникшие
	//        до нас, не затрагиваются (авторитет данных — сервер);
	//     true — все сопоставленные колонки выравниваются по клиенту, включая ранее возникшие
	//        расхождения. Только по явному выбору владельца.
	//   `bAddMissingRows` — ДОСТРОИТЬ файл: добавить строки для клиентских id, которых у сервера
	//     нет вовсе (сейчас это 29 записей, id 251…279). Значения сопоставленных колонок берутся
	//     из клиента, остальные — из строки-СОСЕДА (запись с наибольшим id), потому что
	//     клиентского близнеца у них не существует. Существующие строки при этом НЕ меняются,
	//     правится только счётчик записей в первой строке.
	//   `OutAddedRows` — сколько строк добавлено.
	bool BuildServerFameInfo(TArray<uint8>& OutBytes, int32& OutChangedRows, FString& OutError,
	                         bool bAllMappedColumns = false, bool bAddMissingRows = false,
	                         int32* OutAddedRows = nullptr,
	                         TArray<int32>* OutAddedIds = nullptr,
	                         bool bIgnoreLoaderLimit = false,
	                         TArray<int32>* OutSkippedIds = nullptr) const;

	// Разбивка расхождений с сервером ПО ПОЛЯМ («Очки славы: 42, Показывать в списке: 98»).
	//   Общее число строк с расхождением меньше суммы: у одной строки их бывает несколько.
	FString DescribeServerMismatchBreakdown() const;

	// Сколько строк изменится в каждом из двух режимов — чтобы спросить владельца осмысленно.
	void CountServerChanges(int32& OutMyEdits, int32& OutAllMapped) const;

	// Полный запрос на выгрузку для сервера: содержимое файла + ФАКТИЧЕСКИ найденные места на
	//   серверах + записка. Одна точка и для кнопки в UI, и для headless-проверки.
	bool BuildServerExportRequest(struct FAstoryServerExportRequest& OutRequest,
	                              int32& OutChangedRows, FString& OutError,
	                              bool bAllMappedColumns = false,
	                              bool bAddMissingRows = false,
	                              bool bIgnoreLoaderLimit = false) const;

	// ★ Достроить серверную таблицу недостающими записями, проверить результат побайтово и
	//   разложить выгрузку в обе серверные ветки. Живой сервер только читается.
	static void BuildAndVerifyServerAddMissing(bool bDoExport, FString& OutReport,
	                                          bool bIgnoreLoaderLimit = false);

	// ★★ ФИНАЛЬНАЯ ВЫГРУЗКА: готовит СРАЗУ ДВА варианта серверной таблицы в разных подпапках —
	//   «до патчей сервера» (256 записей, кладётся хоть сейчас) и «после патчей» (280 записей,
	//   требует расширенной маски И поднятого предела count в загрузчике). Оба проверяются
	//   побайтово: существующие строки не меняются ни в одном.
	static void BuildFinalServerExport(FString& OutReport);

	// Изменена ли запись по сравнению с состоянием на момент загрузки (по сопоставленным полям).
	bool IsFieldEditedSinceLoad(int32 Id, int32 ClientOffset) const;

	// ── пути ──
	static FString ProjectRawDir();          // RawAssets
	static FString CsvPath();                // RawAssets/Data/ItemDB/title_list.csv
	static FString JsonPath();
	static FString BinPath();                // RawAssets/Data/Character/title_list.bin
	static FString GameTextPath();
	static FString BackupDir();

	// Путь к серверному FameInfo.txt: первый существующий из списка кандидатов.
	static FString ResolveServerFameInfo();

	// ── служебное ──
	// Кодек CP1251 ↔ FString (внутри записей строки в CP1251).
	static FString Cp1251ToString(const uint8* Bytes, int32 Len);
	static void StringToCp1251(const FString& S, TArray<uint8>& Out);

	// Дешифровка/зашифровка записи title_list (240 б) — зеркальные операции.
	static void DecryptTitleRecord(TArray<uint8>& Rec);
	static void EncryptTitleRecord(TArray<uint8>& Rec);

	// Флаг «данные загружены».
	bool IsLoaded() const { return bLoaded; }

	// Заголовок исходного .bin (ver/count/extra/tag) — переносится в пересобранный файл 1:1.
	uint8 BinHeader[20] = {};

private:
	bool LoadCsv(FString& OutError);
	bool LoadGameText(FString& OutError);
	bool LoadServerFameInfo();

	TArray<FAstoryTitleRecord> Records;
	// ★ Снимок записей на момент загрузки/последнего сохранения. По нему отличаются ПРАВКИ ЭТОЙ
	//   СЕССИИ от расхождений, которые в данных были и до нас: на сервер по умолчанию едут только
	//   первые (см. BuildServerFameInfo).
	TMap<int32, FAstoryTitleRecord> Baseline;
	TMap<int32, FString> LocStrings;
	TMap<int32, FAstoryFameServerRow> ServerRows;
	FString ServerFilePath;
	FString ServerFileHash;
	// Исходные строки FameInfo.txt (для пересборки серверного файла 1:1, кроме правленых колонок).
	TArray<FString> ServerRawLines;
	bool bLoaded = false;
};
