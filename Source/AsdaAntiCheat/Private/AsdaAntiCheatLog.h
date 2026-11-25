// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAntiCheatLog.h — категория журнала клиентского античита (в неё же идёт лог EOS SDK) и макрос ASDA_EAC_LOG.
#pragma once

#include "CoreMinimal.h"
#include "AsdaEacDiagLog.h"

// Журнал модуля AsdaAntiCheat и пересланный лог EOS SDK (EOS_Logging_SetCallback). Секреты (пароль, токены,
//   ClientSecret) в журнал не пишутся — только длины и коды.
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaAntiCheat, Log, All);

// ★ Живая проверка 16.10.2026: строка журнала EAC — в диагностический файл AsdaEac.log (работает и в Shipping, где UE_LOG
//   вырезан: bUseLoggingInShipping=false) И в UE-лог категории LogAsdaAntiCheat (Development/редактор — как раньше).
//   Verbosity — Log / Warning / Error / Verbose. Формат — как у UE_LOG. Секреты сюда не передавать (см. AsdaEacDiagLog.h).
#define ASDA_EAC_LOG(Verbosity, Format, ...) \
	do \
	{ \
		const FString AsdaEacLogLine_ = FString::Printf(Format, ##__VA_ARGS__); \
		FAsdaEacDiag::Write(TEXT(#Verbosity), AsdaEacLogLine_); \
		UE_LOG(LogAsdaAntiCheat, Verbosity, TEXT("%s"), *AsdaEacLogLine_); \
	} while (0)
