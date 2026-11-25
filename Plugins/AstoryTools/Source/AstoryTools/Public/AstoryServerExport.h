// Copyright (c) 2026 @sitxovski (Discord)
// FAstoryServerExport — ★ ОБЩАЯ ТОЧКА ВЫГРУЗКИ ФАЙЛОВ ДЛЯ СЕРВЕРА.
//
// Задача. Редактор правит клиентскую таблицу, а у сервера есть свой текстовый близнец
// (звания → `FameInfo.txt`, магазины → `item_table.txt` и т. д.). Живой сервер трогать НЕЛЬЗЯ,
// поэтому после сохранения инструмент раскладывает готовые файлы во ВРЕМЕННУЮ папку
// зеркалом серверных путей, кладёт рядом README и открывает её в проводнике — подмену
// делает владелец руками.
//
// Раскладка выгрузки:
//   %TEMP%\AstoryExport_<ГГГГММДД>_<ЧЧММ SS>\<тип сервера>\<путь как на сервере>\<файл>
//   например ...\AstoryExport_20260829_214530\GameServer\ConfigSet\FameInfo.txt
//
// ★★ ГЛАВНОЕ ПРАВИЛО: тип сервера и путь определяются ФАКТОМ, а не догадкой.
//   `DiscoverPlacements()` сканирует корни серверов и возвращает ВСЕ места, где файл с таким
//   именем реально лежит. Один и тот же файл часто нужен НЕСКОЛЬКИМ серверам: `FameInfo.txt`
//   лежит и в `GameServer\ConfigSet`, и в `Indun_GameServer\ConfigSet` (побайтово одинаковые),
//   и в `NpcServer\FameInfo` (там ДРУГОЕ поколение файла). Если выгрузить только в одну ветку,
//   в подземельях останутся старые данные — на этом уже обжигались с ценами бустеров.
//   Поэтому найденные места делятся на два класса:
//     * `bSameContent == true`  — текущее содержимое совпадает с тем, из которого мы читали
//                                 (та же база) ⇒ выгружаем туда файл;
//     * `bSameContent == false` — там ДРУГОЕ поколение файла ⇒ файл НЕ подсовываем,
//                                 но место перечисляем в README как предупреждение.
//
// ⚠ Ничего на живой сервер не пишется: код только читает шару и пишет в %TEMP%.
#pragma once

#include "CoreMinimal.h"

// Одно найденное место файла на сервере.
struct ASTORYTOOLS_API FAstoryServerPlacement
{
	// Тип сервера = имя папки верхнего уровня в корне сервера («GameServer», «NpcServer», …).
	FString ServerKind;
	// Путь внутри сервера БЕЗ имени файла («ConfigSet», «ItemTable_Info», «FameInfo»).
	FString RelativeDir;
	// Полный путь исходного файла (для чтения/сравнения). На живую шару НЕ пишем.
	FString SourceFullPath;
	// Размер и хеш текущего содержимого — ими и определяется «то же поколение».
	int64 SourceSize = 0;
	FString SourceHash;
	// Совпадает ли содержимое с базовым файлом, который редактор читал при загрузке.
	bool bSameContent = false;
	// Пояснение для README, если не совпадает («147 записей вместо 251 — файл 2009 г.»).
	FString Note;
};

// Один файл, который нужно выгрузить.
struct ASTORYTOOLS_API FAstoryServerExportFile
{
	// Имя файла как на сервере («FameInfo.txt»).
	FString FileName;
	// Необязательная подпапка внутри выгрузки: нужна, когда одного и того же файла готовится
	//   НЕСКОЛЬКО вариантов (например «до патчей сервера» и «после»). Пусто — класть в корень.
	FString SubFolder;
	// Пояснение к этому варианту файла — попадёт в README рядом с путями.
	FString Note;
	// Готовое содержимое (уже в серверной кодировке/переводах строк — формирует вызывающий).
	TArray<uint8> Content;
	// Куда класть. Обычно заполняется из DiscoverPlacements() отбором bSameContent == true.
	TArray<FAstoryServerPlacement> Placements;
	// Все найденные места (включая несовпадающие) — попадут в README как предупреждение.
	TArray<FAstoryServerPlacement> AllPlacements;
};

// Запрос на выгрузку.
struct ASTORYTOOLS_API FAstoryServerExportRequest
{
	// Кто выгружает («Achievements Editor») — идёт в README и в имя папки.
	FString ToolName;
	// Человеческое описание правок (многострочно) — идёт в README.
	FString ChangeSummary;
	// Как откатить — идёт в README.
	FString RollbackHint;
	// Файлы.
	TArray<FAstoryServerExportFile> Files;
};

class ASTORYTOOLS_API FAstoryServerExport
{
public:
	// Корни серверов, по которым идёт поиск. Первый существующий считается «живым»,
	//   остальные — локальными копиями. Все — ТОЛЬКО НА ЧТЕНИЕ.
	static const TArray<FString>& GetServerRoots();

	// ★ Найти ФАКТОМ все места, где лежит файл с именем `FileName`.
	//   `BaselineHash` — хеш файла, из которого редактор читал данные (обычно тот, что вернул
	//   `HashFile()`); по нему проставляется `bSameContent`.
	//   Поиск идёт по корню сервера на глубину `MaxDepth` папок (по умолчанию 2): все известные
	//   места лежат на глубине 1 (`GameServer\ConfigSet\`, `NpcServer\FameInfo\`), а живой сервер —
	//   это сетевая шара, где каждый лишний листинг стоит дорого.
	static TArray<FAstoryServerPlacement> DiscoverPlacements(
		const FString& FileName, const FString& BaselineHash, int32 MaxDepth = 2);

	// MD5-хеш файла (пусто, если файла нет).
	static FString HashFile(const FString& FullPath);

	// Разложить выгрузку во временную папку, написать README.txt и ОТКРЫТЬ папку в проводнике.
	//   Возвращает путь папки; при ошибке — пустую строку и текст в OutError.
	static FString Run(const FAstoryServerExportRequest& Request, FString& OutError);

	// То же, но без открытия проводника (для автотестов).
	static FString Build(const FAstoryServerExportRequest& Request, FString& OutError);
};
