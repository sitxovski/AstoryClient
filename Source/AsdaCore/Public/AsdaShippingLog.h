// Copyright (c) 2026 @sitxovski (Discord)
// AsdaShippingLog.h — ★ ЖУРНАЛ НАШИХ МОДУЛЕЙ В SHIPPING (решение владельца 19.10.2026, Reports/release_logs_2026-10-19).
//
// ЗАЧЕМ. Движок установлен бинарной сборкой (D:\Games\UE_5.8, Engine/Build/InstalledBuild.txt): его Shipping-библиотеки
//   собраны с NO_LOGGING=1 — тело FMsg::Logf_Internal/LogV в Core пустое, UE_LOG вырезан на уровне движка. Включить
//   bUseLoggingInShipping нельзя: это свойство [RequiresUniqueBuildEnvironment], а уникальное окружение на установленном
//   движке UBT запрещает («Targets with a unique build environment cannot be built with an installed engine»); обход
//   bOverrideBuildEnvironment дал бы USE_LOGGING_IN_SHIPPING=1 только нашим модулям, но вызовы ушли бы в тот же пустой Core.
//
// КАК. Этот заголовок подключается ПРИНУДИТЕЛЬНО (ForceIncludeFiles в *.Build.cs игровых модулей, только Shipping) после
//   PCH и переопределяет UE_LOG / UE_CLOG / UE_LOG_REF: строка форматируется здесь и уходит в GLog->Serialize. В GLog
//   Shipping-процесса из наших устройств вывода есть только запечатанный журнал сессии (AsdaLogSeal, AsdaNet), открытые
//   устройства движка либо не подключены (консоль, OutputDebugString — движок с NO_LOGGING их не ставит), либо пустые
//   (FOutputDeviceFile при NO_LOGGING ничего не пишет) и сняты политикой AsdaLogSeal::InstallRetailPolicy.
//   Уровень: Fatal → как у движка (LowLevelFatalError, плюс строка в журнал); Error/Warning/Display/Log — в журнал;
//   Verbose/VeryVerbose вырезаются КОМПИЛЯЦИЕЙ (if constexpr — кода и строк формата в exe нет).
//   Категория — имя токена (TEXT(#CategoryName)): при NO_LOGGING объект категории пуст (FNoLoggingCategory), имя берётся
//   из исходника. Все наши категории — «Log, All», поэтому умолчание категории совпадает с Development.
//   Вне Shipping (и если движок когда-нибудь соберут с логами) заголовок ничего не меняет.
#pragma once

#include "CoreTypes.h"
#include "Logging/LogMacros.h"
#include "Logging/LogVerbosity.h"
#include "Misc/AssertionMacros.h"

#if UE_BUILD_SHIPPING && NO_LOGGING

// Самый подробный уровень, который попадает в журнал Shipping (Verbose/VeryVerbose вырезаются компиляцией).
#define ASDA_SHIPPING_LOG_MAX_VERBOSITY ::ELogVerbosity::Log

struct FLogCategoryBase;

namespace AsdaShipLog
{
	// Строка журнала: Category — имя категории (литерал), Fmt — printf-формат UE_LOG. Реализация — AsdaCore/Private/AsdaShippingLog.cpp.
	ASDACORE_API void VARARGS Logf(const TCHAR* Category, ELogVerbosity::Type Verbosity, const TCHAR* Fmt, ...);
	// То же для UE_LOG_REF: имя берётся из объекта категории.
	ASDACORE_API void VARARGS LogfRef(const FLogCategoryBase& Category, ELogVerbosity::Type Verbosity, const TCHAR* Fmt, ...);
}

// Общее тело: Fatal — строка в журнал и фатальная ошибка движка; до ASDA_SHIPPING_LOG_MAX_VERBOSITY — строка в журнал.
#define ASDA_SHIPLOG_PRIVATE_BODY(Sink, CategoryArg, Verbosity, Format, ...) \
	if constexpr ((::ELogVerbosity::Verbosity & ::ELogVerbosity::VerbosityMask) == ::ELogVerbosity::Fatal) \
	{ \
		::AsdaShipLog::Sink(CategoryArg, ::ELogVerbosity::Verbosity, Format, ##__VA_ARGS__); \
		LowLevelFatalError(Format, ##__VA_ARGS__); \
		CA_ASSUME(false); \
	} \
	else if constexpr ((::ELogVerbosity::Verbosity & ::ELogVerbosity::VerbosityMask) <= ASDA_SHIPPING_LOG_MAX_VERBOSITY) \
	{ \
		UE_VALIDATE_FORMAT_STRING(Format, ##__VA_ARGS__); \
		::AsdaShipLog::Sink(CategoryArg, ::ELogVerbosity::Verbosity, Format, ##__VA_ARGS__); \
	}

#undef UE_LOG
#undef UE_CLOG
#undef UE_LOG_REF

// UE_LOG Shipping: см. шапку файла.
#define UE_LOG(CategoryName, Verbosity, Format, ...) \
	{ \
		ASDA_SHIPLOG_PRIVATE_BODY(Logf, TEXT(#CategoryName), Verbosity, Format, ##__VA_ARGS__) \
	}

// UE_CLOG Shipping: условие вычисляется только для уровней, которые пишутся.
#define UE_CLOG(Condition, CategoryName, Verbosity, Format, ...) \
	{ \
		if constexpr ((::ELogVerbosity::Verbosity & ::ELogVerbosity::VerbosityMask) == ::ELogVerbosity::Fatal \
			|| (::ELogVerbosity::Verbosity & ::ELogVerbosity::VerbosityMask) <= ASDA_SHIPPING_LOG_MAX_VERBOSITY) \
		{ \
			if (Condition) \
			{ \
				ASDA_SHIPLOG_PRIVATE_BODY(Logf, TEXT(#CategoryName), Verbosity, Format, ##__VA_ARGS__) \
			} \
		} \
	}

// UE_LOG_REF Shipping: категория — ссылка на FLogCategoryBase.
#define UE_LOG_REF(CategoryRef, Verbosity, Format, ...) \
	{ \
		ASDA_SHIPLOG_PRIVATE_BODY(LogfRef, CategoryRef, Verbosity, Format, ##__VA_ARGS__) \
	}

#endif // UE_BUILD_SHIPPING && NO_LOGGING
