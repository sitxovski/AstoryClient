// Copyright (c) 2026 @sitxovski (Discord)
// FAstoryDataTableIO — ★ ОБЩИЙ шаг «CSV → DataTable» редакторских инструментов на C++
//   (волна 3 п.15 плана дублей, 15.09.2026; аудит `Reports/dedup_audit/04_cross_layer.md` S4/X12).
//
// Пара на Python — `Tools/asda_dt_common.py` (`fill_dt`/`build_table`): там тот же шаг для импортёров,
//   запускаемых во встроенном Python редактора. Эти две реализации живут в разных языках и объединены
//   быть не могут; правило одно: сначала резервная копия файла, затем переимпорт из ТОЙ ЖЕ строки CSV,
//   что записана на диск, замечания импортёра — в отчёт, сохранение пакета — напрямую с проверкой факта.
//
// Сейчас потребитель один — редактор званий (`FAstoryTitleData::Save`); будущие редакторы таблиц
//   (магазины, дроп, цены) зовут эти функции, а не пишут свой переимпорт.
#pragma once

#include "CoreMinimal.h"

struct ASTORYTOOLS_API FAstoryDataTableIO
{
	// Резервная копия файла в Dir: «<имя>.<Stamp><.расширение>» (закон backup-before-edit).
	//   Возвращает путь копии; пустая строка — исходного файла нет (копировать нечего).
	static FString BackupFile(const FString& Src, const FString& Dir, const FString& Stamp);

	// Переимпорт DataTable из строки CSV и (по флагу) прямое сохранение пакета.
	//   DataTablePath — полный путь объекта («/Game/.../DT_x.DT_x»); DisplayName — имя в отчёте.
	//   В OutLog дописываются строки отчёта: замечания импортёра (до 8) и итог
	//   «<DisplayName> переимпортирован[ и СОХРАНЁН: … | — ★ ПАКЕТ НЕ СОХРАНЁН: … | (ассет НЕ сохранялся — режим проверки)] (N строк)».
	//   Возвращает false, если таблица не загрузилась (строку об этом пишет вызывающий — он знает, что уже записал).
	static bool ReimportFromCsv(const TCHAR* DataTablePath, const FString& DisplayName, const FString& Csv,
		bool bSavePackage, FString& OutLog);
};
