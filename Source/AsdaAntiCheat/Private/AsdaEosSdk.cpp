// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEosSdk.cpp — загрузка EOS SDK полным путём и EOS_Initialize (описание — в заголовке).
#include "AsdaEosSdk.h"
#include "AsdaAntiCheatLog.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"

#if WITH_ASDA_EOSSDK
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <delayimp.h>   // ★ ред. 3 (E1-17): хук отложенной загрузки — импорт EOS привязывается к НАШЕЙ копии DLL
#include "Windows/HideWindowsPlatformTypes.h"
#include "eos_sdk.h"
#include "eos_logging.h"
#include "eos_version.h"
#endif

FAsdaEosSdk& FAsdaEosSdk::Get()
{
	static FAsdaEosSdk Instance;   // один на процесс
	return Instance;
}

#if WITH_ASDA_EOSSDK
// Лог EOS SDK → журнал UE (категория LogAsdaAntiCheat) и ★ диагностический AsdaEac.log (16.10: и в Shipping).
//   Строки SDK — UTF-8, живут только на время вызова. Текст SDK мы не контролируем — в файл он идёт через WriteForeign
//   (длинные токеноподобные последовательности маскируются: PUID, JWT, секреты).
static void EOS_CALL AsdaEosLogCallback(const EOS_LogMessage* Msg)
{
	if (!Msg) { return; }
	const FString Cat = UTF8_TO_TCHAR(Msg->Category ? Msg->Category : "");
	const FString Text = UTF8_TO_TCHAR(Msg->Message ? Msg->Message : "");
	const FString Line = FString::Printf(TEXT("[EOS %s] %s"), *Cat, *Text);
	switch (Msg->Level)
	{
	case EOS_ELogLevel::EOS_LOG_Fatal:
	case EOS_ELogLevel::EOS_LOG_Error:
		FAsdaEacDiag::WriteForeign(TEXT("Error"), Line);
		UE_LOG(LogAsdaAntiCheat, Error, TEXT("%s"), *Line);
		break;
	case EOS_ELogLevel::EOS_LOG_Warning:
		FAsdaEacDiag::WriteForeign(TEXT("Warning"), Line);
		UE_LOG(LogAsdaAntiCheat, Warning, TEXT("%s"), *Line);
		break;
	case EOS_ELogLevel::EOS_LOG_Info:
		FAsdaEacDiag::WriteForeign(TEXT("Log"), Line);
		UE_LOG(LogAsdaAntiCheat, Log, TEXT("%s"), *Line);
		break;
	default:
		FAsdaEacDiag::WriteForeign(TEXT("Verbose"), Line);
		UE_LOG(LogAsdaAntiCheat, Verbose, TEXT("%s"), *Line);
		break;
	}
}

// Якорь адреса внутри бинарника этого модуля (папка бинарника = $(BinaryOutputDir), куда UBT кладёт DLL SDK).
static void AsdaEosSdkAnchor() {}

// Папка бинарника, в который слинкован этот код (редактор: Binaries/Win64 проекта; монолит: папка exe).
static FString AsdaEosOwnBinaryDir()
{
	HMODULE Self = nullptr;
	if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&AsdaEosSdkAnchor), &Self) || !Self)
	{
		return FString();
	}
	WCHAR Buf[MAX_PATH * 4] = {};
	const DWORD N = ::GetModuleFileNameW(Self, Buf, UE_ARRAY_COUNT(Buf));
	if (N == 0 || N >= UE_ARRAY_COUNT(Buf)) { return FString(); }
	return FPaths::GetPath(FString(Buf));
}

// ★ ред. 3 (E1-17): полный путь DLL SDK проекта — подпапка ASDA_EOSSDK_DLL_SUBDIR рядом с бинарником этого модуля.
static FString AsdaEosOwnDllPath()
{
	const FString Dir = AsdaEosOwnBinaryDir();
	if (Dir.IsEmpty()) { return FString(); }
	FString P = FPaths::Combine(Dir, TEXT(ASDA_EOSSDK_DLL_SUBDIR), TEXT(ASDA_EOSSDK_DLL_NAME));
	FPaths::NormalizeFilename(P);
	return P;
}

// ★ ред. 3 (E1-17): ХУК ОТЛОЖЕННОЙ ЗАГРУЗКИ. Отложенный импорт разрешает DLL по ИМЕНИ (LoadLibrary без пути), а в процессе
//   редактора уже может сидеть одноимённая движковая EOSSDK-Win64-Shipping.dll 1.19.1.2 (её грузит EOSShared) — загрузчик
//   ОС вернул бы её. Хук отдаёт загрузчику модуль нашей копии (уже загруженной FAsdaEosSdk полным путём из подпапки, иначе
//   загружает её сам). Хук один на образ (наш модуль в редакторе / exe в монолите); других хуков в движке нет (grep 16.10).
static FARPROC WINAPI AsdaEosDelayLoadHook(unsigned DliNotify, PDelayLoadInfo Pdli)
{
	if (DliNotify != dliNotePreLoadLibrary || !Pdli || !Pdli->szDll) { return nullptr; }
	if (_stricmp(Pdli->szDll, ASDA_EOSSDK_DLL_NAME) != 0) { return nullptr; }   // чужие DLL — штатная загрузка
	HMODULE H = static_cast<HMODULE>(FAsdaEosSdk::Get().GetDllHandleRaw());
	if (!H)
	{
		const FString Path = AsdaEosOwnDllPath();
		if (!Path.IsEmpty()) { H = ::LoadLibraryW(*Path); }
	}
	return reinterpret_cast<FARPROC>(H);   // nullptr — загрузчик попробует по имени (до EnsureInitialized вызовов EOS нет)
}
extern "C" const PfnDliHook __pfnDliNotifyHook2 = AsdaEosDelayLoadHook;

// Полный путь уже загруженного модуля с именем Name (пусто — не загружен).
static FString AsdaEosLoadedModulePath(const TCHAR* Name)
{
	HMODULE H = ::GetModuleHandleW(Name);
	if (!H) { return FString(); }
	WCHAR Buf[MAX_PATH * 4] = {};
	const DWORD N = ::GetModuleFileNameW(H, Buf, UE_ARRAY_COUNT(Buf));
	return (N > 0 && N < UE_ARRAY_COUNT(Buf)) ? FString(Buf) : FString(TEXT("?"));
}
#endif // WITH_ASDA_EOSSDK

bool FAsdaEosSdk::EnsureInitialized(FString& OutWhy)
{
	if (bTried) { OutWhy = FailWhy; return bReady; }
	bTried = true;
#if WITH_ASDA_EOSSDK
	// ★ ред. 3 (E1-17): DLL — в подпапке AsdaEOS рядом с бинарником (EOSShared редактора туда не смотрит и грузит свою).
	DllPath = AsdaEosOwnDllPath();
	// ★ Живая проверка 16.10: путь строится от папки СВОЕГО бинарника (GetModuleHandleEx по адресу кода модуля), а не от
	//   ProjectDir/BaseDir: в монолитном Shipping это папка exe AsdaReborn\Binaries\Win64 → …\AsdaEOS\EOSSDK-Win64-Shipping.dll.
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS SDK: папка бинарника модуля «%s», ожидаемая DLL «%s», файл %s"),
		*AsdaEosOwnBinaryDir(), *DllPath, (!DllPath.IsEmpty() && FPaths::FileExists(DllPath)) ? TEXT("есть") : TEXT("НЕТ"));
	if (DllPath.IsEmpty() || !FPaths::FileExists(DllPath))
	{
		FailWhy = FString::Printf(TEXT("нет %s в подпапке %s рядом с бинарником (%s)"), TEXT(ASDA_EOSSDK_DLL_NAME), TEXT(ASDA_EOSSDK_DLL_SUBDIR), *DllPath);
		ASDA_EAC_LOG(Warning, TEXT("[EAC] EOS SDK: %s"), *FailWhy);
		OutWhy = FailWhy;
		return false;
	}
	// Одноимённая DLL уже в процессе (движковая 1.19.1.2 от EOSShared в редакторе) — теперь допустимо: наша копия грузится
	//   ПОЛНЫМ путём отдельным модулем, а отложенный импорт привязан к ней хуком (AsdaEosDelayLoadHook). Только строка в лог.
	{
		FString Loaded = AsdaEosLoadedModulePath(TEXT(ASDA_EOSSDK_DLL_NAME));
		FPaths::NormalizeFilename(Loaded);
		if (!Loaded.IsEmpty() && !Loaded.Equals(DllPath, ESearchCase::IgnoreCase))
		{
			ASDA_EAC_LOG(Log, TEXT("[EAC] EOS SDK: в процессе есть одноимённая %s (%s, движковая) — наша копия грузится отдельно полным путём"),
				TEXT(ASDA_EOSSDK_DLL_NAME), *Loaded);
		}
	}
	// ★ Живая проверка 16.10: LoadLibraryExW напрямую — чтобы снять GetLastError сразу после отказа (FPlatformProcess::GetDllHandle
	//   его не отдаёт). LOAD_WITH_ALTERED_SEARCH_PATH — зависимости DLL ищутся от её папки, как у GetDllHandle.
	::SetLastError(0);
	DllHandle = ::LoadLibraryExW(*DllPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	const uint32 LoadErr = DllHandle ? 0u : (uint32)::GetLastError();
	if (!DllHandle)
	{
		TCHAR ErrBuf[512] = {};
		FPlatformMisc::GetSystemErrorMessage(ErrBuf, UE_ARRAY_COUNT(ErrBuf), (int32)LoadErr);
		FailWhy = FString::Printf(TEXT("не загрузилась %s: LoadLibraryExW → GetLastError %u (%s)"), *DllPath, LoadErr, ErrBuf);
		ASDA_EAC_LOG(Warning, TEXT("[EAC] EOS SDK: %s"), *FailWhy);
		OutWhy = FailWhy;
		return false;
	}
	{
		FString Loaded = AsdaEosLoadedModulePath(TEXT(ASDA_EOSSDK_DLL_NAME));
		ASDA_EAC_LOG(Log, TEXT("[EAC] EOS SDK: LoadLibraryExW — OK (HMODULE %p); модуль по имени в процессе: «%s»"), DllHandle, *Loaded);
	}
	Version = UTF8_TO_TCHAR(EOS_GetVersion());

	EOS_InitializeOptions Init = {};
	Init.ApiVersion = EOS_INITIALIZE_API_LATEST;
	const FTCHARToUTF8 ProductName(FApp::GetProjectName());                       // имя продукта для SDK (≤ 64)
	const FTCHARToUTF8 ProductVersion(*FString(FApp::GetBuildVersion()).Left(64)); // версия сборки для SDK (≤ 64)
	Init.ProductName = ProductName.Get();
	Init.ProductVersion = ProductVersion.Get();
	const EOS_EResult R = EOS_Initialize(&Init);
	if (R != EOS_EResult::EOS_Success && R != EOS_EResult::EOS_AlreadyConfigured)
	{
		FailWhy = FString::Printf(TEXT("EOS_Initialize: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(R)));
		ASDA_EAC_LOG(Warning, TEXT("[EAC] EOS SDK %s: %s"), *Version, *FailWhy);
		OutWhy = FailWhy;
		return false;
	}
	// ★ EOS_AlreadyConfigured — SDK в процессе уже поднял другой владелец. Так бывает ТОЛЬКО в процессе редактора
	//   (в т. ч. `UnrealEditor.exe -game`): движковый плагин EOSShared (его тянут Fab и MetaHumanSDK, TargetAllowList=Editor)
	//   на PostConfigInit грузит DLL по имени и находит нашу копию в Binaries/Win64 проекта. Тогда жизненный цикл SDK
	//   (лог-колбэк, EOS_Shutdown) — его: свой лог-колбэк не ставим (он глобальный), EOS_Shutdown не зовём.
	//   В игре (Shipping/Development Game) EOSShared нет — владелец SDK мы.
	bOwnsSdk = (R == EOS_EResult::EOS_Success);
	if (bOwnsSdk)
	{
		EOS_Logging_SetCallback(&AsdaEosLogCallback);
		EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_Info);
	}
	bReady = true;
	ASDA_EAC_LOG(Log, TEXT("[EAC] EOS SDK %s загружен (%s), EOS_Initialize: %s%s"),
		*Version, *DllPath, UTF8_TO_TCHAR(EOS_EResult_ToString(R)),
		bOwnsSdk ? TEXT("") : TEXT(" — SDK уже поднят движковым EOSShared (процесс редактора): лог SDK идёт в LogEOSSDK, EOS_Shutdown — за ним"));
	return true;
#else
	FailWhy = TEXT("сборка без EOS SDK (платформа не Win64 x64)");
	OutWhy = FailWhy;
	return false;
#endif
}

void FAsdaEosSdk::Shutdown()
{
#if WITH_ASDA_EOSSDK
	if (bReady && bOwnsSdk)
	{
		EOS_Logging_SetCallback(nullptr);
		EOS_Shutdown();
	}
	bReady = false;
	// DLL не выгружаем: импорт отложенной загрузки уже привязан к её адресам, а повторный EOS_Initialize в процессе
	//   SDK всё равно не допускает. ОС выгрузит её вместе с процессом.
#endif
}
