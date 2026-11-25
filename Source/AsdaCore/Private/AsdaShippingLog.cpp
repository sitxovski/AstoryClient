// Copyright (c) 2026 @sitxovski (Discord)
// AsdaShippingLog.cpp — приёмник UE_LOG наших модулей в Shipping (описание — AsdaCore/Public/AsdaShippingLog.h).
#include "AsdaShippingLog.h"

#if UE_BUILD_SHIPPING && NO_LOGGING
#include "CoreGlobals.h"
#include "Logging/LogCategory.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/StringBuilder.h"
#include "UObject/NameTypes.h"

namespace AsdaShipLogPriv
{
	// Отформатировать и отдать строку в GLog (там — запечатанный журнал сессии). Без GLog строка отбрасывается.
	static void LogV(const FName& Category, ELogVerbosity::Type Verbosity, const TCHAR* Fmt, va_list Args)
	{
		if (!GLog || !Fmt) { return; }
		TStringBuilder<512> Buffer;
		Buffer.AppendV(Fmt, Args);
		GLog->Serialize(*Buffer, Verbosity, Category);
	}
}

namespace AsdaShipLog
{
	void VARARGS Logf(const TCHAR* Category, ELogVerbosity::Type Verbosity, const TCHAR* Fmt, ...)
	{
		va_list Args;
		va_start(Args, Fmt);
		AsdaShipLogPriv::LogV(FName(Category ? Category : TEXT("LogAsda")), Verbosity, Fmt, Args);
		va_end(Args);
	}

	void VARARGS LogfRef(const FLogCategoryBase& Category, ELogVerbosity::Type Verbosity, const TCHAR* Fmt, ...)
	{
		va_list Args;
		va_start(Args, Fmt);
		AsdaShipLogPriv::LogV(FName(Category.GetCategoryName()), Verbosity, Fmt, Args);
		va_end(Args);
	}
}
#endif // UE_BUILD_SHIPPING && NO_LOGGING
