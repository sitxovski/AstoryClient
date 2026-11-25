// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLogSealRetail.cpp — ритейл-политика журналов процесса (AsdaLogSeal::InstallRetailPolicy, решение владельца 16.10.2026).
//
// ЧТО ДЕЛАЕТ (только при AsdaLogSeal::IsActive(): Shipping всегда, Development — с ключом -AsdaLogSeal):
//   1. Журнал сессии (★ 19.10.2026, Reports/release_logs_2026-10-19, часть A). Всё, что попадает в GLog — с 19.10 это и
//      UE_LOG наших модулей в Shipping (Source/AsdaCore/Public/AsdaShippingLog.h), — пишется ОДНИМ запечатанным файлом на
//      запуск игры: Saved/Logs/Sessions/<ГГГГММДД-ЧЧММСС>_<pid>.alog (контракт с лаунчером — 00_SPEC.md: файл не
//      перезаписывается, закрывается при выходе; хранятся последние kSessionKeep файлов). Строки копятся пачкой и
//      запечатываются одной записью Text (порог kBatchChars / kBatchSeconds; Warning и выше — сразу), без FlushFileBuffers на
//      каждую пачку (данные в кэше ОС переживают падение процесса). Предел файла — под приёмник сервера (≤ 4 МиБ).
//      ★ 31.10.2026 (ротация, asda.LogSeal.Rotate): у предела сессия продолжается частью <ГГГГММДД-ЧЧММСС>_<pid>_part<N>.alog —
//      каждая часть свой сегмент (расшифровывается сама по себе; в сведениях session_id и part для склейки — имя файла на
//      сервер не передаётся), хранятся первая и последние части (всего kPartKeep); пачки сжимаются zlib (запись TextZ,
//      asda.LogSeal.Compress); хвост журнала (~64 К символов) уходит в пакет падения файлом session_tail.txt.
//      Склейка частей — `python Tools/asda_logseal.py join <папка>`.
//      После входа учётки рядом пишется открытый <файл>.alog.meta {"account","session"} (SetSessionAccount; ★ 31.10 — у каждой части).
//      Shipping: открытые устройства движка (файл, консоль) снимаются; Development + -AsdaLogSeal: запечатанное устройство
//      ДОБАВЛЯЕТСЯ к обычному (самотесты читают открытый лог).
//   2. Падения. Движок пишет Saved/Crashes/<GUID>/{CrashContext.runtime-xml, UEMinidump.dmp} открытым текстом (в xml — имя
//      пользователя, машина, командная строка; в дампе — память процесса). Здесь: FPlatformMisc::SetCrashHandlingType(Disabled)
//      (все __except движка пропускают исключение дальше), свой SetUnhandledExceptionFilter и обработчик SIGABRT. Пакет падения
//      пишет заранее созданная нить: сводка (код, адрес, модуль+RVA, стек нити) + minidump, снятый MiniDumpWriteDump В ПАМЯТЬ
//      (ввод-вывод через обратный вызов, без файла), — всё запечатано в Saved/Crashes/AsdaCrash_<время>_<pid>.acrash.
//      Открытый дамп на диск не попадает ни на каком шаге; не удалось запечатать — не пишется ничего.
//   3. Зависания. Детектор зависаний движка (FThreadHeartBeat, 25 с) в Shipping пишет открытый дамп напрямую
//      (ReportHang → ReportCrashUsingCrashReportClient, флаг Disabled его не касается) — он выключается (HangDuration=0).
//      Поведение игры не меняется: зависание в Shipping и раньше не было фатальным (HangsAreFatal=false), терялся только
//      открытый отчёт.
//   4. Перенос старого (только Shipping): открытые файлы прошлых сборок и падения до установки обработчика (статическая
//      инициализация, ранний PreInit) — Saved/Crashes/<папка>, Saved/Logs/*.log|*.dmp|*.txt, %LOCALAPPDATA%\AsdaReborn\
//      AsdaEac_raw.log — запечатываются (вид Legacy) и только после успешной записи удаляются. Отдельной нитью.
//      ★ 19.10: только «свои» обычные файлы в «своих» каталогах (без джанкшенов, симлинков и жёстких ссылок), удаление по дескриптору — см. SweepLegacyPlainFiles.
#include "AsdaLogSeal.h"
#include "LogSeal/AsdaLogSealPrivate.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/ThreadHeartBeat.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Logging/LogVerbosity.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Containers/Ticker.h"
#include "Misc/OutputDeviceConsole.h"
#include "CoreGlobals.h"
#include "Misc/Compression.h"
#include "AsdaDevConsole.h"   // ★ 31.10: ASDA_DEV_CVAR — ключи ротации и сжатия журнала сессии
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <DbgHelp.h>
#include <signal.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if UE_BUILD_SHIPPING && PLATFORM_WINDOWS
// ★ 16.10.2026: открытый канал -stdout. В Shipping движок с NO_LOGGING не вешает на GLog консоль (-log) и OutputDebugString,
//   но ключ -stdout подключает FOutputDeviceStdOutput БЕЗУСЛОВНО (LaunchEngineLoop.cpp ~1892, InitializeStdOutDevice) —
//   фатальные ошибки со стеком ушли бы в stdout открытым текстом (`AsdaReborn.exe -stdout > x.txt`). Указателя на это
//   устройство снаружи нет, поэтому ключ вычищается из буфера GetCommandLineW() (его же читает LaunchWindowsStartup) в
//   статической инициализации монолитного exe — до WinMain. Токен заменяется пробелами; путь exe (нулевой токен) не трогается.
namespace AsdaLogSealStdOutGuard
{
	void SanitizeProcessCommandLine()
	{
		static const TCHAR* const Banned[] = { TEXT("stdout"), TEXT("fullstdoutlogoutput") };
		TCHAR* P = ::GetCommandLineW();
		if (!P) { return; }
		bool bFirst = true;
		while (*P)
		{
			while (*P == TEXT(' ') || *P == TEXT('\t')) { ++P; }
			if (!*P) { break; }
			TCHAR* const Start = P;
			bool bQuoted = false;
			while (*P && (bQuoted || (*P != TEXT(' ') && *P != TEXT('\t'))))
			{
				if (*P == TEXT('"')) { bQuoted = !bQuoted; }
				++P;
			}
			if (bFirst) { bFirst = false; continue; }
			const TCHAR* Name = Start;
			if (*Name == TEXT('"')) { ++Name; }
			if (*Name != TEXT('-') && *Name != TEXT('/')) { continue; }
			++Name;
			int32 NameLen = 0;
			while (Name + NameLen < P && Name[NameLen] != TEXT('=') && Name[NameLen] != TEXT('"')) { ++NameLen; }
			for (const TCHAR* B : Banned)
			{
				if (FCString::Strlen(B) == NameLen && FCString::Strnicmp(Name, B, NameLen) == 0)
				{
					for (TCHAR* Q = Start; Q < P; ++Q) { *Q = TEXT(' '); }
					break;
				}
			}
		}
	}

	// Статическая инициализация идёт до WinMain → ключ вычищен до разбора командной строки движком.
	struct FAtStartup { FAtStartup() { SanitizeProcessCommandLine(); } };
	FAtStartup GAtStartup;
}
#endif

// Категория строк самой политики журналов (учётка сессии и т. п.).
DEFINE_LOG_CATEGORY_STATIC(LogAsdaLogSeal, Log, All);

// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] ротация журнала сессии (решение владельца «Да, ротация»; баг 7 Player55 — момент отказа навыков не
//   попал в журнал). Было: после 3,5 МиБ файла сессии писались только Warning и выше, на ~3,94 МиБ запись останавливалась —
//   у игрока это первые 6–7 минут игры. Стало: у предела части (≤ 4 МиБ — предел приёмника LogDrop на файл) сессия продолжается
//   следующей частью <сессия>_partN.alog, каждая часть — самостоятельный запечатанный сегмент; строки уровня Log не теряются.
//   0 = прежнее поведение (мягкий и жёсткий предел одного файла).
static ASDA_DEV_CVAR(int32, CVarAsdaLogSealRotate,
	TEXT("asda.LogSeal.Rotate"), 1,
	TEXT("Журнал сессии: у предела части (≤ 4 МиБ) продолжать следующей частью <сессия>_partN.alog (хранятся первая и последние, всего 6), в пакет падения — хвост журнала. 0 = прежнее: после 3,5 МиБ только Warning, на ~3,94 МиБ запись останавливается."),
	ECVF_Default);

// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] сжатие пачек журнала сессии (zlib, запись TextZ) — текст журнала сжимается в ~7 раз, в часть
//   влезает ~40 минут игры вместо ~6, и выгрузка лаунчера укладывается в лимиты приёмника. 0 = прежнее: открытый UTF-8 (Text).
static ASDA_DEV_CVAR(int32, CVarAsdaLogSealCompress,
	TEXT("asda.LogSeal.Compress"), 1,
	TEXT("Журнал сессии: пачки строк сжимаются zlib перед запечатыванием (запись TextZ). 0 = прежнее: открытый UTF-8 (запись Text)."),
	ECVF_Default);

namespace AsdaLogSealRetailPriv
{
	using namespace AsdaLogSeal;

	// Сколько СЕССИЙ журнала хранится в Saved/Logs/Sessions (вместе с текущей; ★ 31.10 — сессия со всеми частями считается
	//   одной); старшие удаляются при запуске.
	constexpr int32 kSessionKeep = 30;
	// ★ 31.10: частей одной сессии хранится не больше: первая (начало: сборка, настройки, вход) и последние kPartKeep − 1;
	//   средние удаляются при открытии новой части. 6 × ≤ 4 МиБ = ≤ 24 МиБ на сессию (≈ 4 ч игры при сжатии) — столько
	//   лаунчер выгружает за 1–2 запуска в пределах лимитов приёмника (★ с 31.10: 25 МиБ/ч на IP и установку, 64 МиБ/сутки на учётку;
	//   до того 16/32 — пятая часть за час получала 429 и ждала следующего запуска).
	constexpr int32 kPartKeep = 6;
	// ★ 31.10: предел всей папки Sessions (байт): сверх него старейшие сессии удаляются целиком раньше, чем по kSessionKeep.
	constexpr int64 kSessionsDirCapBytes = int64(256) * 1024 * 1024;
	// Мягкий предел файла сессии (байт): дальше пишутся только Warning/Error/Fatal (приёмник сервера берёт файл ≤ 4 МиБ).
	//   ★ 31.10: только при asda.LogSeal.Rotate 0.
	constexpr int64 kSessionSoftCapBytes = int64(3) * 1024 * 1024 + 512 * 1024;
	// Жёсткий предел файла сессии (байт): дальше не пишется ничего, кроме одной пометки об обрезке.
	//   ★ 31.10: при ротации — предел ЧАСТИ (следующая пачка не влезает → новая часть); 64 КиБ запаса до 4 МиБ приёмника.
	constexpr int64 kSessionHardCapBytes = int64(4) * 1024 * 1024 - 64 * 1024;
	// Порог пачки строк (символов TCHAR): накопленное запечатывается одной записью Text.
	constexpr int32 kBatchChars = 16 * 1024;
	// Сколько строка может ждать в пачке (с): проверяется при следующей строке и тиком FTSTicker.
	constexpr double kBatchSeconds = 1.0;
	// ★ 31.10: пачку короче (байт UTF-8) не сжимаем — zlib на десятках байт не выигрывает.
	constexpr int32 kCompressMinBytes = 512;
	// ★ 31.10: после сбоя записи новая часть пробуется сразу, затем не чаще раза в столько секунд (нет места — не дёргать диск).
	constexpr double kRecoverSeconds = 1.0;
	// ★ 31.10: кусок UTF-8 одной записи TextZ (сжатый с заголовком ≤ 48 КиБ + 0,1 % + 15 б < kMaxRecordData — один кадр).
	constexpr int32 kCompressChunkBytes = 48 * 1024;
	// ★ 31.10: хвост журнала в памяти для пакета падения (символов; держится до 2× и подрезается).
	constexpr int32 kCrashTailChars = 64 * 1024;
	// ★ 31.10: предел одной строки при ротации (символов): строка длиннее режется — пачка всегда влезает в новую часть.
	constexpr int32 kMaxLineChars = 256 * 1024;
	// ★ 31.10: накладные кадра на запись (u32 clen + u64 seq + байт типа + тег GCM).
	constexpr int64 kFrameOverhead = 12 + 1 + 16;

	// ★ 31.10: ротация частей включена (asda.LogSeal.Rotate).
	static bool RotateOn() { return CVarAsdaLogSealRotate.GetValueOnAnyThread() != 0; }
	// ★ 31.10: сжатие пачек включено (asda.LogSeal.Compress).
	static bool CompressOn() { return CVarAsdaLogSealCompress.GetValueOnAnyThread() != 0; }

	// Метка времени строки журнала.
	static FString LineStamp() { return FDateTime::Now().ToString(TEXT("%Y.%m.%d %H:%M:%S.%s")); }

	// Разобрать имя файла сессии из папки Sessions «ГГГГММДД-ЧЧММСС_<pid>[_<n>][_part<N>].alog» — только такие файлы политика
	//   хранения трогает. OutKey — ключ сессии (имя без «_partN» и «.alog»), OutPart — номер части (1 у имени без «_partN»).
	static bool ParseSessionLogName(const FString& Name, FString* OutKey = nullptr, int32* OutPart = nullptr)
	{
		if (!Name.EndsWith(TEXT(".alog"), ESearchCase::IgnoreCase)) { return false; }
		FString Base = Name.LeftChop(5);
		int32 Part = 1;
		const int32 At = Base.Find(TEXT("_part"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (At != INDEX_NONE)
		{
			const FString Num = Base.Mid(At + 5);
			if (Num.IsEmpty() || Num.Len() > 4) { return false; }
			for (const TCHAR C : Num) { if (!FChar::IsDigit(C)) { return false; } }
			Part = FCString::Atoi(*Num);
			if (Part < 2) { return false; }
			Base.LeftInline(At);
		}
		if (Base.Len() < 17 || Base[8] != TEXT('-') || Base[15] != TEXT('_')) { return false; }
		for (int32 I = 0; I < Base.Len(); ++I)
		{
			if (I == 8 || I == 15) { continue; }
			if (!FChar::IsDigit(Base[I]) && Base[I] != TEXT('_')) { return false; }
		}
		if (OutKey) { *OutKey = Base; }
		if (OutPart) { *OutPart = Part; }
		return true;
	}

	// ── 1. журнал сессии ────────────────────────────────────────────────────────────────────────────────────────
	// Устройство GLog: строки копятся пачкой и запечатываются записью Text/TextZ; один сегмент на файл (часть) сессии.
	class FSealedLogDevice final : public FOutputDevice
	{
	public:
		// InPath — файл первой части сессии; InPub — 64 б другого получателя (только самотест вне Shipping; nullptr — вшитый ключ).
		explicit FSealedLogDevice(const FString& InPath, const uint8* InPub = nullptr) : Path(InPath)
		{
			if (InPub) { Pub.Append(InPub, 64); }
		}

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			Serialize(V, Verbosity, Category, -1.0);
		}

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, const double Time) override
		{
			(void)Time;
			const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			if (Level == ELogVerbosity::SetColor) { return; }   // служебная смена цвета консоли — не строка журнала
			FScopeLock L(&Lock);
			if (bSealing) { ++Dropped; return; }   // ★ 31.10: строка изнутри записи пачки (сжатие/ввод-вывод) — пачка занята
			if (!EnsureOpen()) { return; }   // открыть/запечатать нельзя — строка отбрасывается (открытым текстом не пишется)
			const bool bUrgent = Level <= ELogVerbosity::Warning;   // Fatal/Error/Warning — сразу на диск и даже за мягким пределом
			const bool bRotate = RotateOn();
			if (bStopped || (!bUrgent && !bRotate && Written >= kSessionSoftCapBytes))
			{
				++Dropped;
				if (!bStopped && !bSoftNoted)
				{
					bSoftNoted = true;
					AppendLine(TEXT("LogAsdaLogSeal"), ELogVerbosity::Warning, *FString::Printf(
						TEXT("журнал сессии достиг %lld байт — дальше пишутся только Warning/Error/Fatal"), (long long)Written));
					SealBatch();
				}
				return;
			}
			AppendLine(*Category.ToString(), Level, V ? V : TEXT(""), bRotate);
			if (bUrgent || Batch.Len() >= kBatchChars || FPlatformTime::Seconds() - BatchStart >= kBatchSeconds) { SealBatch(); }
		}

		virtual void Flush() override
		{
			FScopeLock L(&Lock);
			SealBatch();
		}

		virtual void TearDown() override
		{
			FScopeLock L(&Lock);
			if (Handle && !bStopped)
			{
				AppendLine(TEXT("LogAsdaLogSeal"), ELogVerbosity::Log, *FString::Printf(
					TEXT("журнал сессии закрыт: часть %d, %lld байт в части, всего записано %lld, отброшено строк: %d"),
					PartNo, (long long)Written, (long long)TotalWritten, Dropped));
			}
			SealBatch();
			if (Handle) { Handle->Flush(/*bFullFlush*/true); }
			Seg->End();
			Handle.Reset();
			bClosed = true;   // после TearDown файл не переоткрывается (иначе строки выхода создали бы второй сегмент)
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
		virtual bool CanBeUsedOnPanicThread() const override { return true; }

		// Дописать пачку строк, если она ждёт дольше kBatchSeconds (тик FTSTicker на игровом потоке).
		void FlushIfStale()
		{
			FScopeLock L(&Lock);
			if (!Batch.IsEmpty() && FPlatformTime::Seconds() - BatchStart >= kBatchSeconds) { SealBatch(); }
		}

		// Нить записи пакета падения: дописать пачку, если замок свободен (упавшая нить могла держать его — тогда не ждём).
		//   ★ 31.10: заодно снять хвост журнала (OutTail, при asda.LogSeal.Rotate) и имя текущей части (OutPart). false — замок занят.
		bool FlushFromCrashThread(FString& OutTail, FString& OutPart)
		{
			if (!Lock.TryLock()) { return false; }
			SealBatch();
			if (Handle) { Handle->Flush(/*bFullFlush*/true); }
			if (RotateOn()) { OutTail = Tail; }
			OutPart = FPaths::GetCleanFilename(Path);
			Lock.Unlock();
			return true;
		}

		// Полный путь файла текущей части сессии (★ 31.10: меняется при ротации — копия под замком).
		FString GetPath() const
		{
			FScopeLock L(&Lock);
			return Path;
		}

		// ★ 31.10: учётка сессии → <часть>.alog.meta у каждой сохранившейся части (и у будущих — при открытии). Логин проверен
		//   вызывающим. Возврат: meta текущей части записан (части ещё нет — запомнено, запишется при открытии: true).
		bool SetAccount(const FString& Login)
		{
			FScopeLock L(&Lock);
			Account = Login;
			if (!Handle) { return !bFailed && !bClosed && !bStopped; }   // остановлен (часть не открылась) — meta писать некуда
			bool bCurrent = false;
			for (const FString& P : PartPaths) { const bool b = WriteMeta(P); bCurrent = (P == Path) ? b : bCurrent; }
			return bCurrent;
		}

		// ★ 31.10 (самотест): номер текущей части и счётчик отброшенных строк.
		int32 GetPartNo() const { FScopeLock L(&Lock); return PartNo; }
		int32 GetDropped() const { FScopeLock L(&Lock); return Dropped; }
		// ★ 31.10 (самотест): запись сейчас сорвана / сколько раз срывалась.
		bool IsWriteBroken() const { FScopeLock L(&Lock); return bWriteBroken; }
		int32 GetWriteFailures() const { FScopeLock L(&Lock); return WriteFailures; }
#if !UE_BUILD_SHIPPING
		// ★ 31.10 (самотест): имитировать сбой N следующих записей (каждая пишет половину байт и сообщает об ошибке).
		void SelfTestFailWrites(int32 N) { FScopeLock L(&Lock); TestFailWrites = N; }
#endif

	private:
		// ★ 31.10: запись, подготовленная к запечатыванию (кусок RecData).
		struct FRec
		{
			ERecord Type;   // Text или TextZ
			int32 Off;      // смещение данных в RecData
			int32 Len;      // длина данных
		};

		FString Path;                     // полный путь текущей части Saved/Logs/Sessions/<имя>[_partN].alog
		FString Stem;                     // ★ 31.10: имя первой части без «.alog» (ключ сессии) — основа имён частей
		TArray<FString> PartPaths;        // ★ 31.10: сохранившиеся части сессии по порядку (первая — всегда [0], пока есть)
		TArray<uint8> Pub;                // ★ 31.10: другой получатель (самотест); пусто — вшитый ключ
		FString Account;                  // ★ 31.10: логин учётки сессии для meta частей (пусто — входа ещё не было)
		mutable FCriticalSection Lock;    // сериализация строк из разных потоков
		TUniquePtr<FSegment> Seg = MakeUnique<FSegment>();       // сегмент текущей части (открывается вместе с файлом)
		TUniquePtr<FSegment> NextSeg = MakeUnique<FSegment>();   // ★ 31.10: сегмент следующей части — начинается ДО закрытия текущей
		bool bWriteBroken = false;        // ★ 31.10: запись в текущую часть сорвалась (нет места/частичная) — нужна новая часть
		int32 WriteFailures = 0;          // ★ 31.10: сколько раз срывалась запись за сессию
		int32 RecoverTries = 0;           // ★ 31.10: неудачных попыток открыть часть после сбоя подряд
		double LastRecoverTry = 0.0;      // ★ 31.10: FPlatformTime::Seconds() последней попытки
#if !UE_BUILD_SHIPPING
		int32 TestFailWrites = 0;         // ★ 31.10 (самотест): столько следующих записей «срываются» (пишется половина)
#endif
		TUniquePtr<IFileHandle> Handle;   // файл текущей части
		FString Batch;                    // накопленные строки (открытый текст — только в памяти)
		FString Tail;                     // ★ 31.10: последние ~kCrashTailChars символов записанного журнала (для пакета падения)
		TArray<FRec> Recs;                // ★ 31.10: записи текущей пачки (память переиспользуется)
		TArray<uint8> RecData;            // ★ 31.10: данные записей пачки (сжатые куски или UTF-8)
		double BatchStart = 0.0;          // FPlatformTime::Seconds() первой строки пачки
		int64 Written = 0;                // байт записано в текущую часть
		int64 TotalWritten = 0;           // ★ 31.10: байт записано за сессию во все части
		int32 Dropped = 0;                // строк отброшено за пределом
		int32 PartNo = 0;                 // ★ 31.10: номер текущей части (1 — первая; 0 — файл не открывался)
		bool bFailed = false;             // открыть не удалось — дальше не пытаемся
		bool bClosed = false;             // TearDown прошёл — больше не пишем
		bool bSoftNoted = false;          // пометка о мягком пределе уже записана
		bool bStopped = false;            // жёсткий предел достигнут — запись остановлена
		bool bSealing = false;            // ★ 31.10: идёт запись пачки (строки из неё же — сжатия/ввода-вывода — отбрасываются)

		// Строка в пачку: «время [категория][уровень] текст». ★ 31.10: bCapLine — строка длиннее kMaxLineChars режется.
		void AppendLine(const TCHAR* Category, ELogVerbosity::Type Level, const TCHAR* Text, bool bCapLine = false)
		{
			if (Batch.IsEmpty()) { BatchStart = FPlatformTime::Seconds(); }
			Batch += LineStamp();
			Batch += TEXT(" [");
			Batch += Category;
			Batch += TEXT("][");
			Batch += ::ToString(Level);
			Batch += TEXT("] ");
			const int32 Len = bCapLine ? FCString::Strlen(Text) : 0;
			if (bCapLine && Len > kMaxLineChars)
			{
				Batch.AppendChars(Text, kMaxLineChars);
				Batch += FString::Printf(TEXT(" …[строка обрезана: %d символов]"), Len);
			}
			else
			{
				Batch += Text;
			}
			Batch += TEXT("\r\n");
		}

		// ★ 31.10: разложить UTF-8 пачки на записи (сжатие zlib уровня «скорость» — COMPRESS_BiasSpeed: ~0,1 мс на пачку под замком,
		//   в 1,2 раза хуже уровня по умолчанию; порядок строк сохраняется, т. к. всё под одним замком): со сжатием — куски ≤ kCompressChunkBytes по границе символа, каждый TextZ
		//   (u32 длина + zlib), если сжатие выиграло, иначе Text; без сжатия — одна запись Text (Seal сам режет на кадры).
		//   Возврат: байт, которые займут кадры этих записей в файле.
		int64 BuildRecords(const uint8* U, int32 N)
		{
			Recs.Reset();
			RecData.Reset();
			if (!CompressOn() || N < kCompressMinBytes)
			{
				RecData.Append(U, N);
				Recs.Add({ ERecord::Text, 0, N });
				const int64 Frames = FMath::Max<int64>(1, (N + kMaxRecordData - 1) / kMaxRecordData);
				return Frames * kFrameOverhead + N;
			}
			int64 Need = 0;
			for (int32 Off = 0; Off < N;)
			{
				int32 Take = FMath::Min(N - Off, kCompressChunkBytes);
				while (Off + Take < N && Take > 1 && (U[Off + Take] & 0xC0) == 0x80) { --Take; }   // не резать символ UTF-8
				int64 Bound = 0;
				const int32 At = RecData.Num();
				const bool bBound = FCompression::CompressMemoryBound(NAME_Zlib, Bound, int64(Take)) && Bound > 0 && Bound <= kMaxRecordData - 4;
				RecData.AddUninitialized(4 + (bBound ? int32(Bound) : 0));
				int64 Size = Bound;
				const bool bZ = bBound && FCompression::CompressMemory(NAME_Zlib, RecData.GetData() + At + 4, Size, U + Off, int64(Take), COMPRESS_BiasSpeed)
					&& Size > 0 && Size + 4 < Take;
				if (bZ)
				{
					const uint32 Raw = static_cast<uint32>(Take);
					FMemory::Memcpy(RecData.GetData() + At, &Raw, 4);
					RecData.SetNum(At + 4 + int32(Size), EAllowShrinking::No);
					Recs.Add({ ERecord::TextZ, At, 4 + int32(Size) });
				}
				else
				{
					RecData.SetNum(At, EAllowShrinking::No);
					RecData.Append(U + Off, Take);
					Recs.Add({ ERecord::Text, At, Take });
				}
				Need += kFrameOverhead + Recs.Last().Len;
				Off += Take;
			}
			return Need;
		}

		// ★ 31.10: запечатать подготовленные записи в текущую часть. false — отказ шифрования.
		bool SealRecords()
		{
			for (const FRec& R : Recs)
			{
				if (!Seg->Seal(R.Type, RecData.GetData() + R.Off, R.Len)) { return false; }
			}
			return true;
		}

		// Запечатать пачку и записать. За жёстким пределом: при ротации — следующая часть, иначе одна пометка и остановка.
		//   ★ 31.10: после сбоя записи (нет места, частичная запись) — сначала новая часть (оборванная читается до обрыва);
		//   не открылась — пачка отбрасывается (счётчик Dropped), повтор сразу и затем раз в kRecoverSeconds.
		void SealBatch()
		{
			if (Batch.IsEmpty() || !Handle) { return; }
			if (bStopped)
			{
				Batch.Reset();
				return;
			}
			TGuardValue<bool> Guard(bSealing, true);
			if (bWriteBroken)
			{
				const double Now = FPlatformTime::Seconds();
				const bool bTry = RecoverTries == 0 || Now - LastRecoverTry >= kRecoverSeconds;
				if (bTry) { LastRecoverTry = Now; }
				if (!bTry || !OpenNextPart(/*bAfterWriteFailure*/true))
				{
					RecoverTries += bTry ? 1 : 0;
					Dropped += CountLines(Batch);
					Batch.Reset(kBatchChars + 1024);
					return;
				}
				RecoverTries = 0;
			}
			int64 Need = 0;
			{
				const FTCHARToUTF8 U(*Batch);
				Need = BuildRecords(reinterpret_cast<const uint8*>(U.Get()), U.Length());
			}
			if (Written + Need > kSessionHardCapBytes)
			{
				const bool bRotate = RotateOn();
				if (!bRotate || !OpenNextPart(/*bAfterWriteFailure*/false))
				{
					// Ключ 0 — прежняя остановка; при ротации — новая часть не открылась (файл/ключ): пометка в текущую часть.
					bStopped = true;
					Dropped += CountLines(Batch);
					Batch = LineStamp() + (bRotate
						? FString::Printf(TEXT(" [LogAsdaLogSeal][Error] журнал сессии достиг предела части %lld байт, следующая часть не открылась — запись остановлена\r\n"), (long long)kSessionHardCapBytes)
						: FString::Printf(TEXT(" [LogAsdaLogSeal][Error] журнал сессии достиг предела %lld байт — запись остановлена\r\n"), (long long)kSessionHardCapBytes));
					const FTCHARToUTF8 U(*Batch);
					BuildRecords(reinterpret_cast<const uint8*>(U.Get()), U.Length());
				}
			}
			if (SealRecords()) { WritePending(); }
			else { Seg->ClearPending(); }
			if (RotateOn())
			{
				Tail += Batch;
				if (Tail.Len() > 2 * kCrashTailChars) { Tail.RightInline(kCrashTailChars, EAllowShrinking::No); }
			}
			Batch.Reset(kBatchChars + 1024);   // память пачки остаётся (без перераспределений на каждую строку)
		}

		// ★ 31.10: число строк в пачке (для счётчика отброшенных).
		static int32 CountLines(const FString& Text)
		{
			int32 N = 0;
			for (const TCHAR C : Text) { N += C == TEXT('\n') ? 1 : 0; }
			return N;
		}

		// ★ 31.10: начать сегмент части PartPath (номер Part): заголовок + сведения (имя части, ключ сессии, номер части).
		bool BeginPartSegment(FSegment& S, const FString& PartPath, int32 Part)
		{
			const FString Extra = FString::Printf(TEXT("session=%s\nsession_id=%s\npart=%d\n"), *FPaths::GetCleanFilename(PartPath), *Stem, Part);
			return S.Begin(EKind::EngineLog, MakeInfoText(EKind::EngineLog, Extra), Pub.Num() == 64 ? Pub.GetData() : nullptr);
		}

		// ★ 31.10: записать байты в файл (самотест вне Shipping может имитировать сбой: половина байт и ошибка).
		bool WriteBytes(IFileHandle& H, const uint8* Data, int64 N)
		{
#if !UE_BUILD_SHIPPING
			if (TestFailWrites > 0)
			{
				--TestFailWrites;
				H.Write(Data, N / 2);
				return false;
			}
#endif
			return H.Write(Data, N);
		}

		// ★ 31.10: открыть следующую часть <Stem>_part<N>.alog. Порядок — чтобы ничего не терять: файл открыт, сегмент НОВОЙ части
		//   начат и его заголовок записан — только потом текущая часть получает пометку «закрыта — продолжение» и закрывается.
		//   bAfterWriteFailure — текущая часть оборвана сбоем записи (пометку в неё не писать). false — новая часть не открылась
		//   (её файл удалён), текущая часть и её сегмент не тронуты.
		bool OpenNextPart(bool bAfterWriteFailure)
		{
			IPlatformFile& PF = IPlatformFile::GetPlatformPhysical();
			const FString Dir = FPaths::GetPath(Path);
			int32 N = PartNo + 1;
			FString Next = Dir / FString::Printf(TEXT("%s_part%d.alog"), *Stem, N);
			for (; PF.FileExists(*Next) && N < PartNo + 100; ++N) { Next = Dir / FString::Printf(TEXT("%s_part%d.alog"), *Stem, N + 1); }
			if (PF.FileExists(*Next)) { return false; }
			TUniquePtr<IFileHandle> NextHandle(PF.OpenWrite(*Next, /*bAppend*/false, /*bAllowRead*/true));
			if (!NextHandle.IsValid()) { return false; }
			const bool bBegun = BeginPartSegment(*NextSeg, Next, N);
			const int64 HeadBytes = NextSeg->PendingNum();
			if (!bBegun || !WriteBytes(*NextHandle, NextSeg->PendingData(), HeadBytes))
			{
				NextSeg->ClearPending();
				NextSeg->End();
				NextHandle.Reset();
				PF.DeleteFile(*Next);
				return false;
			}
			NextSeg->ClearPending();
			const FString Prev = FPaths::GetCleanFilename(Path);
			const int64 PrevBytes = Written;
			if (!bAfterWriteFailure && Seg->SealText(LineStamp() + FString::Printf(TEXT(" [LogAsdaLogSeal][Log] часть %d закрыта на %lld байт — продолжение: %s\r\n"),
				PartNo, (long long)Written, *FPaths::GetCleanFilename(Next)))) { WritePending(); }
			Seg->End();
			Seg->ClearPending();
			Swap(Seg, NextSeg);
			Handle = MoveTemp(NextHandle);
			Path = Next;
			PartNo = N;
			Written = HeadBytes;
			TotalWritten += HeadBytes;
			bWriteBroken = false;
			const FString Cont = bAfterWriteFailure
				? FString::Printf(TEXT(" [LogAsdaLogSeal][Warning] продолжение сессии %s: часть %d после СБОЯ ЗАПИСИ части %s (записано %lld байт, сбоев %d), отброшено строк: %d\r\n"),
					*Stem, PartNo, *Prev, (long long)PrevBytes, WriteFailures, Dropped)
				: FString::Printf(TEXT(" [LogAsdaLogSeal][Log] продолжение сессии %s: часть %d (предыдущая %s — %lld байт, сбоев записи %d), отброшено строк: %d\r\n"),
					*Stem, PartNo, *Prev, (long long)PrevBytes, WriteFailures, Dropped);
			if (Seg->SealText(LineStamp() + Cont)) { WritePending(); }
			PartPaths.Add(Path);
			if (!Account.IsEmpty()) { WriteMeta(Path); }
			PruneParts(PF);
			return true;
		}

		// ★ 31.10: частей сессии не больше kPartKeep: первая и последние; средние — с их meta — удаляются.
		void PruneParts(IPlatformFile& PF)
		{
			while (PartPaths.Num() > kPartKeep)
			{
				const int32 Victim = PartPaths[0].EndsWith(FString::Printf(TEXT("%s.alog"), *Stem)) ? 1 : 0;
				PF.DeleteFile(*PartPaths[Victim]);
				PF.DeleteFile(*(PartPaths[Victim] + TEXT(".meta")));
				PartPaths.RemoveAt(Victim);
			}
		}

		// ★ 31.10: <часть>.alog.meta {"account","session"} (логин проверен по [A-Za-z0-9_.@-]{1,32}, имя — наше: без экранирования).
		bool WriteMeta(const FString& PartPath) const
		{
			const FString Json = FString::Printf(TEXT("{\"account\":\"%s\",\"session\":\"%s\"}"), *Account, *FPaths::GetCleanFilename(PartPath));
			const FTCHARToUTF8 U(*Json);
			IPlatformFile& PF = IPlatformFile::GetPlatformPhysical();
			TUniquePtr<IFileHandle> H(PF.OpenWrite(*(PartPath + TEXT(".meta")), /*bAppend*/false, /*bAllowRead*/true));
			return H.IsValid() && H->Write(reinterpret_cast<const uint8*>(U.Get()), U.Length());
		}

		bool EnsureOpen()
		{
			if (Handle) { return true; }
			if (bFailed || bClosed || bStopped) { return false; }
			IPlatformFile& PF = IPlatformFile::GetPlatformPhysical();
			PF.CreateDirectoryTree(*FPaths::GetPath(Path));
			// ★ Старые сессии не перезаписываются: имя уже занято — суффикс _2, _3, …
			const FString Dir = FPaths::GetPath(Path);
			const FString BaseStem = FPaths::GetBaseFilename(Path);
			for (int32 N = 2; PF.FileExists(*Path) && N < 100; ++N)
			{
				Path = Dir / FString::Printf(TEXT("%s_%d.alog"), *BaseStem, N);
			}
			if (PF.FileExists(*Path)) { bFailed = true; return false; }
			Stem = FPaths::GetBaseFilename(Path);
			PartNo = 1;
			Handle.Reset(PF.OpenWrite(*Path, /*bAppend*/false, /*bAllowRead*/true));
			bFailed = !Handle.IsValid() || !BeginPartSegment(*Seg, Path, PartNo);
			if (bFailed) { Handle.Reset(); Seg->ClearPending(); return false; }
			WritePending();
			PartPaths.Add(Path);
			if (!Account.IsEmpty()) { WriteMeta(Path); }
			Batch.Reserve(kBatchChars + 1024);
			PruneOldSessions(PF);
			return true;
		}

		// Хранить последние kSessionKeep сессий (по имени = по времени; ★ 31.10 — все части сессии вместе, и не больше
		//   kSessionsDirCapBytes на папку); удаляются и их .alog.meta, и осиротевшие meta.
		void PruneOldSessions(IPlatformFile& PF)
		{
			const FString Dir = FPaths::GetPath(Path);
			TMap<FString, TArray<FString>> Sessions;   // ключ сессии → её файлы
			TArray<FString> Metas;
			PF.IterateDirectory(*Dir, [&Sessions, &Metas](const TCHAR* Name, bool bIsDir)
			{
				if (bIsDir) { return true; }
				const FString Clean = FPaths::GetCleanFilename(Name);
				FString Key;
				if (ParseSessionLogName(Clean, &Key)) { Sessions.FindOrAdd(Key).Add(Clean); }
				else if (Clean.EndsWith(TEXT(".alog.meta"), ESearchCase::IgnoreCase)) { Metas.Add(Clean); }
				return true;
			});
			TArray<FString> Keys;
			Sessions.GetKeys(Keys);
			Keys.Sort();
			int64 Total = 0;
			for (const TPair<FString, TArray<FString>>& S : Sessions)
			{
				for (const FString& F : S.Value) { Total += FMath::Max<int64>(0, PF.FileSize(*(Dir / F))); }
			}
			// ★ 31.10: прошлые сессии с частями сверх kPartKeep (например, после смены предела) — первая и последние, средние удалить.
			for (TPair<FString, TArray<FString>>& S : Sessions)
			{
				if (S.Key == Stem || S.Value.Num() <= kPartKeep) { continue; }
				TArray<TPair<int32, FString>> Parts;
				for (const FString& F : S.Value) { int32 Part = 0; ParseSessionLogName(F, nullptr, &Part); Parts.Add({ Part, F }); }
				Parts.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key < B.Key; });
				const int32 First = Parts[0].Key == 1 ? 1 : 0;   // первая часть остаётся, если есть
				for (int32 I = First; I < Parts.Num() - (kPartKeep - First); ++I)
				{
					Total -= FMath::Max<int64>(0, PF.FileSize(*(Dir / Parts[I].Value)));
					PF.DeleteFile(*(Dir / Parts[I].Value));
					PF.DeleteFile(*(Dir / Parts[I].Value + TEXT(".meta")));
					S.Value.Remove(Parts[I].Value);
				}
			}
			int32 Count = Keys.Num();
			for (int32 I = 0; I < Keys.Num() && (Count > kSessionKeep || Total > kSessionsDirCapBytes); ++I)
			{
				if (Keys[I] == Stem) { continue; }
				for (const FString& F : Sessions[Keys[I]])
				{
					Total -= FMath::Max<int64>(0, PF.FileSize(*(Dir / F)));
					PF.DeleteFile(*(Dir / F));
					PF.DeleteFile(*(Dir / F + TEXT(".meta")));
				}
				--Count;
			}
			for (const FString& M : Metas)
			{
				if (!PF.FileExists(*(Dir / M.LeftChop(5)))) { PF.DeleteFile(*(Dir / M)); }
			}
		}

		// Записать накопленное сегментом. ★ 31.10: сбой (нет места, частичная запись) — кадры за ним уже не связать цепочкой seq
		//   (лаунчер и расшифровщик остановятся на разрыве), поэтому при ротации часть помечается оборванной (bWriteBroken) и в
		//   неё больше не пишется: следующая пачка открывает новую часть. Ключ 0 — прежнее: кадры отбрасываются, запись дальше.
		void WritePending()
		{
			if (Handle && Seg->PendingNum() > 0 && !bWriteBroken)
			{
				if (WriteBytes(*Handle, Seg->PendingData(), Seg->PendingNum()))
				{
					Written += Seg->PendingNum();
					TotalWritten += Seg->PendingNum();
				}
				else if (RotateOn())
				{
					bWriteBroken = true;
					++WriteFailures;
				}
			}
			Seg->ClearPending();
		}
	};

	// Установленное устройство (живёт до конца процесса: GLog может звать его и после выгрузки модуля).
	static FSealedLogDevice* GEngineDevice = nullptr;
	// Тик дописи пачки журнала сессии (раз в kBatchSeconds, игровой поток).
	static FTSTicker::FDelegateHandle GFlushTicker;

	// ── 4. перенос старых открытых файлов ──────────────────────────────────────────────────────────────────────
	// Признак остановки и результат нити переноса.
	static TFuture<void> GSweepFuture;

#if PLATFORM_WINDOWS
	// ── 2. обработчик падений ───────────────────────────────────────────────────────────────────────────────────
	// Резерв адресного пространства под minidump в памяти (страницы фиксируются по мере записи).
	constexpr SIZE_T kDumpReserve = SIZE_T(1024) * 1024 * 1024;
	// Шаг фиксации страниц буфера дампа.
	constexpr SIZE_T kDumpCommitStep = SIZE_T(4) * 1024 * 1024;
	// Предел кадров стека в сводке.
	constexpr int32 kMaxStackFrames = 64;
	// Код исключения, которым обработчик SIGABRT передаёт abort() в общий путь пакета падения.
	constexpr DWORD kAbortExceptionCode = 0xE0A5DA0A;

	static HANDLE GCrashRequest = nullptr;            // событие «упали — пишите пакет»
	static HANDLE GCrashDone = nullptr;               // событие «пакет записан (или не вышло)»
	static HANDLE GCrashThread = nullptr;             // заранее созданная нить записи пакета
	static volatile int32 GCrashCount = 0;             // сколько раз входили в обработчик (пишем только первый)
	static EXCEPTION_POINTERS* GCrashEp = nullptr;    // исключение для нити записи
	static DWORD GCrashTid = 0;                       // нить, на которой упали
	static FString GCrashDir;                         // Saved/Crashes (полный путь, вычислен заранее)
	static FSegment* GCrashSeg = nullptr;             // сегмент пакета (создан заранее: буферы зарезервированы)
	static uint8* GDumpBase = nullptr;                // буфер minidump (зарезервирован)
	static SIZE_T GDumpCommitted = 0;                 // зафиксировано байт буфера
	static SIZE_T GDumpSize = 0;                      // размер снятого дампа
	static bool GDumpOverflow = false;                // дамп не поместился в резерв
	static HANDLE GNulHandle = INVALID_HANDLE_VALUE;  // дескриптор NUL для MiniDumpWriteDump (ввод-вывод идёт через обратный вызов)
	static bool GCrashInstalled = false;              // обработчик установлен
	static FString GCrashTail;                        // ★ 31.10: хвост журнала сессии для пакета (снимает нить записи; пусто — нет)
	static FString GCrashTailPart;                    // ★ 31.10: имя части журнала сессии, к которой относится хвост
	static bool GCrashTailBusy = false;               // ★ 31.10: замок журнала держала упавшая нить — хвост не снят

	// Зафиксировать страницы буфера дампа до End байт.
	static bool EnsureDumpCommitted(ULONG64 End)
	{
		if (End > kDumpReserve) { GDumpOverflow = true; return false; }
		while (GDumpCommitted < End)
		{
			const SIZE_T Step = FMath::Min<SIZE_T>(kDumpCommitStep, kDumpReserve - GDumpCommitted);
			if (!::VirtualAlloc(GDumpBase + GDumpCommitted, Step, MEM_COMMIT, PAGE_READWRITE)) { GDumpOverflow = true; return false; }
			GDumpCommitted += Step;
		}
		return true;
	}

	// Обратный вызов MiniDumpWriteDump: весь ввод-вывод — в буфер памяти (IoStartCallback → S_FALSE включает этот режим).
	static BOOL CALLBACK DumpCallback(PVOID, const PMINIDUMP_CALLBACK_INPUT In, PMINIDUMP_CALLBACK_OUTPUT Out)
	{
		if (!In || !Out) { return 1; }
		switch (In->CallbackType)
		{
		case IoStartCallback:
			Out->Status = S_FALSE;
			return 1;
		case IoWriteAllCallback:
		{
			const ULONG64 Off = In->Io.Offset;
			const ULONG Bytes = In->Io.BufferBytes;
			if (!EnsureDumpCommitted(Off + Bytes)) { Out->Status = E_OUTOFMEMORY; return 1; }
			::memcpy(GDumpBase + Off, In->Io.Buffer, Bytes);
			GDumpSize = FMath::Max<SIZE_T>(GDumpSize, SIZE_T(Off + Bytes));
			Out->Status = S_OK;
			return 1;
		}
		case IoFinishCallback:
			Out->Status = S_OK;
			return 1;
		default:
			return 1;
		}
	}

	// Раскрутка стека из контекста исключения (x64: RtlLookupFunctionEntry + RtlVirtualUnwind). Только POD — внутри __try.
	static int32 WalkStack(const CONTEXT* Start, DWORD64* OutPcs, int32 Max)
	{
		int32 N = 0;
#if defined(_M_X64)
		__try
		{
			CONTEXT Ctx = *Start;
			while (N < Max && Ctx.Rip)
			{
				OutPcs[N++] = Ctx.Rip;
				DWORD64 ImageBase = 0;
				PRUNTIME_FUNCTION Fn = ::RtlLookupFunctionEntry(Ctx.Rip, &ImageBase, nullptr);
				if (Fn)
				{
					PVOID HandlerData = nullptr;
					DWORD64 EstablisherFrame = 0;
					::RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, Ctx.Rip, Fn, &Ctx, &HandlerData, &EstablisherFrame, nullptr);
				}
				else
				{
					if (!Ctx.Rsp) { break; }
					Ctx.Rip = *reinterpret_cast<DWORD64*>(Ctx.Rsp);   // листовая функция: адрес возврата на вершине стека
					Ctx.Rsp += 8;
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
#endif
		return N;
	}

	// «модуль+0xRVA» для адреса.
	static FString DescribeAddress(DWORD64 Pc)
	{
		HMODULE Mod = nullptr;
		if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(Pc), &Mod) && Mod)
		{
			wchar_t Name[MAX_PATH] = {};
			::GetModuleFileNameW(Mod, Name, MAX_PATH);
			return FString::Printf(TEXT("%s+0x%llX"), *FPaths::GetCleanFilename(FString(Name)), (unsigned long long)(Pc - reinterpret_cast<DWORD64>(Mod)));
		}
		return FString::Printf(TEXT("?+0x%llX"), (unsigned long long)Pc);
	}

	// Имя кода исключения.
	static const TCHAR* ExceptionName(DWORD Code)
	{
		switch (Code)
		{
		case EXCEPTION_ACCESS_VIOLATION:      return TEXT("EXCEPTION_ACCESS_VIOLATION");
		case EXCEPTION_STACK_OVERFLOW:        return TEXT("EXCEPTION_STACK_OVERFLOW");
		case EXCEPTION_ILLEGAL_INSTRUCTION:   return TEXT("EXCEPTION_ILLEGAL_INSTRUCTION");
		case EXCEPTION_INT_DIVIDE_BY_ZERO:    return TEXT("EXCEPTION_INT_DIVIDE_BY_ZERO");
		case EXCEPTION_PRIV_INSTRUCTION:      return TEXT("EXCEPTION_PRIV_INSTRUCTION");
		case EXCEPTION_IN_PAGE_ERROR:         return TEXT("EXCEPTION_IN_PAGE_ERROR");
		case EXCEPTION_BREAKPOINT:            return TEXT("EXCEPTION_BREAKPOINT");
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return TEXT("EXCEPTION_ARRAY_BOUNDS_EXCEEDED");
		case EXCEPTION_DATATYPE_MISALIGNMENT: return TEXT("EXCEPTION_DATATYPE_MISALIGNMENT");
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return TEXT("EXCEPTION_FLT_DIVIDE_BY_ZERO");
		case 0xC0000409:                      return TEXT("STATUS_STACK_BUFFER_OVERRUN");
		case 0xC0000374:                      return TEXT("STATUS_HEAP_CORRUPTION");
		case 0xE06D7363:                      return TEXT("исключение C++");
		case 0x4000:                          return TEXT("assert/фатальная ошибка UE (AssertExceptionCode)");
		case 0x8000:                          return TEXT("падение GPU UE (GPUCrashExceptionCode)");
		case 0x6000:                          return TEXT("нехватка памяти UE (OutOfMemoryExceptionCode)");
		case kAbortExceptionCode:             return TEXT("abort() / SIGABRT");
		default:                              return TEXT("исключение");
		}
	}

	// Сводка падения (текст) — код, адрес, модуль, стек упавшей нити, сведения о сборке.
	static FString MakeCrashSummary(const EXCEPTION_POINTERS* Ep, DWORD Tid)
	{
		FString S;
		const EXCEPTION_RECORD* Rec = Ep ? Ep->ExceptionRecord : nullptr;
		if (Rec)
		{
			S += FString::Printf(TEXT("исключение: 0x%08X %s, флаги 0x%X, адрес %s\r\n"), (uint32)Rec->ExceptionCode,
				ExceptionName(Rec->ExceptionCode), (uint32)Rec->ExceptionFlags, *DescribeAddress(reinterpret_cast<DWORD64>(Rec->ExceptionAddress)));
			if (Rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && Rec->NumberParameters >= 2)
			{
				S += FString::Printf(TEXT("доступ: %s по адресу 0x%llX\r\n"), Rec->ExceptionInformation[0] == 0 ? TEXT("чтение")
					: (Rec->ExceptionInformation[0] == 8 ? TEXT("исполнение") : TEXT("запись")), (unsigned long long)Rec->ExceptionInformation[1]);
			}
		}
		// ★ 19.10: текст фатальной ошибки движка (LowLevelFatalError/UE_LOG Fatal кладут его в GErrorHist до исключения).
		if (GErrorHist[0]) { S += TEXT("ошибка: ") + FString(GErrorHist).Left(4000) + TEXT("\r\n"); }
		S += FString::Printf(TEXT("нить: %u\r\nстек:\r\n"), (uint32)Tid);
		if (Ep && Ep->ContextRecord)
		{
			DWORD64 Pcs[kMaxStackFrames] = {};
			const int32 N = WalkStack(Ep->ContextRecord, Pcs, kMaxStackFrames);
			for (int32 I = 0; I < N; ++I) { S += FString::Printf(TEXT("  #%02d %s\r\n"), I, *DescribeAddress(Pcs[I])); }
		}
		return S;
	}

	// Записать накопленное сегментом в файл. false — сбой записи.
	static bool WriteSegPending(HANDLE F, FSegment& Seg)
	{
		const uint8* P = Seg.PendingData();
		int64 Left = Seg.PendingNum();
		while (Left > 0)
		{
			DWORD Wr = 0;
			const DWORD Want = static_cast<DWORD>(FMath::Min<int64>(Left, 1 << 20));
			if (!::WriteFile(F, P, Want, &Wr, nullptr) || Wr == 0) { Seg.ClearPending(); return false; }
			P += Wr;
			Left -= Wr;
		}
		Seg.ClearPending();
		return true;
	}

	// Написать пакет падения: сводка + minidump, всё запечатано. OutPath — куда (пусто — Saved/Crashes/AsdaCrash_<время>_<pid>.acrash).
	//   Зовётся заранее созданной нитью (или нитью самотеста). Возврат: путь записанного пакета; пусто — не вышло (файл удалён).
	static FString WriteCrashBundle(EXCEPTION_POINTERS* Ep, DWORD Tid, const FString& OutPathIn)
	{
		if (!GCrashSeg || !GDumpBase) { return FString(); }
		SYSTEMTIME T;
		::GetLocalTime(&T);
		const FString OutPath = !OutPathIn.IsEmpty() ? OutPathIn : FPaths::Combine(GCrashDir,
			FString::Printf(TEXT("AsdaCrash_%04u%02u%02u-%02u%02u%02u_%u.acrash"), T.wYear, T.wMonth, T.wDay, T.wHour, T.wMinute, T.wSecond,
				(uint32)::GetCurrentProcessId()));
		::CreateDirectoryW(*GCrashDir, nullptr);
		HANDLE F = ::CreateFileW(*OutPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (F == INVALID_HANDLE_VALUE) { return FString(); }
		FSegment& Seg = *GCrashSeg;
		const FString Summary = MakeCrashSummary(Ep, Tid);
		// ★ 31.10: имя части журнала сессии снимает нить записи под замком (GCrashTailPart); замок был занят — без имени
		//   (до 31.10 имя читалось без замка, а при ротации путь меняется на ходу).
		const bool bTail = !GCrashTail.IsEmpty();
		bool bOk = Seg.Begin(EKind::CrashBundle, MakeInfoText(EKind::CrashBundle, FString(TEXT("content=summary.txt,minidump.dmp"))
			+ (bTail ? TEXT(",session_tail.txt\n") : TEXT("\n"))
			+ (!GCrashTailPart.IsEmpty() ? FString::Printf(TEXT("session=%s\n"), *GCrashTailPart) : FString())
			+ (GCrashTailBusy ? FString(TEXT("session_tail=замок журнала занят упавшей нитью — хвост не снят\n")) : FString()))) && WriteSegPending(F, Seg);
		if (bOk)
		{
			const FTCHARToUTF8 U(*Summary);
			bOk = Seg.SealFileBegin(TEXT("summary.txt"), U.Length()) && Seg.Seal(ERecord::FileData, reinterpret_cast<const uint8*>(U.Get()), U.Length())
				&& Seg.SealFileEnd() && WriteSegPending(F, Seg);
		}
		// ★ 31.10: хвост журнала сессии (последние ~64 К символов, asda.LogSeal.Rotate) — тот же текст, что в конце последней части.
		if (bOk && bTail)
		{
			const FTCHARToUTF8 TailU(*GCrashTail);
			bOk = Seg.SealFileBegin(TEXT("session_tail.txt"), TailU.Length()) && Seg.Seal(ERecord::FileData, reinterpret_cast<const uint8*>(TailU.Get()), TailU.Length())
				&& Seg.SealFileEnd() && WriteSegPending(F, Seg);
		}
		// Minidump в память. Тип — как у отчёта движка по умолчанию + косвенно адресуемая память стеков.
		GDumpSize = 0;
		GDumpOverflow = false;
		bool bDump = false;
		if (bOk)
		{
			MINIDUMP_EXCEPTION_INFORMATION ExInfo = {};
			ExInfo.ThreadId = Tid;
			ExInfo.ExceptionPointers = Ep;
			ExInfo.ClientPointers = 0;
			MINIDUMP_CALLBACK_INFORMATION Cb = {};
			Cb.CallbackRoutine = &DumpCallback;
			const MINIDUMP_TYPE Type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules
				| MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithProcessThreadData);
			bDump = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), GNulHandle, Type, Ep ? &ExInfo : nullptr, nullptr, &Cb)
				&& GDumpSize > 0 && !GDumpOverflow;
			if (bDump)
			{
				bOk = Seg.SealFileBegin(TEXT("minidump.dmp"), GDumpSize) && WriteSegPending(F, Seg);
				for (SIZE_T Off = 0; bOk && Off < GDumpSize; Off += kMaxRecordData)
				{
					const int64 Take = FMath::Min<int64>(int64(GDumpSize - Off), kMaxRecordData);
					bOk = Seg.Seal(ERecord::FileData, GDumpBase + Off, Take) && WriteSegPending(F, Seg);
				}
				bOk = bOk && Seg.SealFileEnd() && WriteSegPending(F, Seg);
			}
			else
			{
				bOk = Seg.SealText(FString::Printf(TEXT("minidump не снят: GetLastError=%u, размер %llu, переполнение %d\r\n"),
					(uint32)::GetLastError(), (unsigned long long)GDumpSize, GDumpOverflow ? 1 : 0)) && WriteSegPending(F, Seg);
			}
			// Затереть дамп в памяти и вернуть страницы.
			if (GDumpCommitted > 0)
			{
				::SecureZeroMemory(GDumpBase, GDumpCommitted);
				::VirtualFree(GDumpBase, GDumpCommitted, MEM_DECOMMIT);
				GDumpCommitted = 0;
			}
		}
		Seg.End();
		Seg.ClearPending();
		bOk = bOk && ::FlushFileBuffers(F);
		::CloseHandle(F);
		if (!bOk) { ::DeleteFileW(*OutPath); return FString(); }
		return OutPath;
	}

	// Нить записи пакета: ждёт падения, пишет, сигналит.
	static DWORD WINAPI CrashThreadProc(LPVOID)
	{
		::WaitForSingleObject(GCrashRequest, INFINITE);
		// ★ 19.10: последние строки сессии — на диск до пакета; ★ 31.10: и хвост журнала с именем части — в пакет.
		if (GEngineDevice) { GCrashTailBusy = !GEngineDevice->FlushFromCrashThread(GCrashTail, GCrashTailPart); }
		WriteCrashBundle(GCrashEp, GCrashTid, FString());
		::SetEvent(GCrashDone);
		return 0;
	}

	// Общий вход падения: первый — будит нить записи и ждёт её (до 60 с); последующие (другие нити) — просто ждут.
	static void HandleCrash(EXCEPTION_POINTERS* Ep)
	{
		if (FPlatformAtomics::InterlockedIncrement(&GCrashCount) == 1)
		{
			GCrashEp = Ep;
			GCrashTid = ::GetCurrentThreadId();
			::SetEvent(GCrashRequest);
		}
		::WaitForSingleObject(GCrashDone, 60 * 1000);
	}

	// Верхний фильтр необработанных исключений процесса: пакет падения, затем завершение (EXCEPTION_EXECUTE_HANDLER).
	static LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* Ep)
	{
		HandleCrash(Ep);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// Фильтр для abort(): то же, что падение.
	static int AbortFilter(EXCEPTION_POINTERS* Ep)
	{
		HandleCrash(Ep);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// SIGABRT (abort, std::terminate): исключение с нашим кодом → общий путь пакета → завершение процесса.
	static void __cdecl AbortHandler(int)
	{
		__try
		{
			::RaiseException(kAbortExceptionCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
		}
		__except (AbortFilter(GetExceptionInformation()))
		{
		}
		::TerminateProcess(::GetCurrentProcess(), 3);
	}

	// Подготовить ресурсы и перехватить обработку падений.
	static void InstallCrashHandler()
	{
		if (GCrashInstalled) { return; }
		GCrashDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Crashes")));
		FPaths::MakePlatformFilename(GCrashDir);
		GCrashSeg = new FSegment();
		GDumpBase = static_cast<uint8*>(::VirtualAlloc(nullptr, kDumpReserve, MEM_RESERVE, PAGE_READWRITE));
		GNulHandle = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		GCrashRequest = ::CreateEventW(nullptr, 1, 0, nullptr);
		GCrashDone = ::CreateEventW(nullptr, 1, 0, nullptr);
		if (!GDumpBase || GNulHandle == INVALID_HANDLE_VALUE || !GCrashRequest || !GCrashDone)
		{
			// Ресурсов нет — всё равно перехватываем: без пакета, но и без открытого дампа движка.
		}
		else
		{
			GCrashThread = ::CreateThread(nullptr, 256 * 1024, &CrashThreadProc, nullptr, 0, nullptr);
		}
		// Движок: все его __except пропускают исключение дальше; верхний фильтр и SIGABRT — наши.
		FPlatformMisc::SetCrashHandlingType(ECrashHandlingType::Disabled);
		::SetUnhandledExceptionFilter(&UnhandledFilter);
		::signal(SIGABRT, &AbortHandler);
		GCrashInstalled = true;
	}
#endif // PLATFORM_WINDOWS

	// Выключить детектор зависаний движка (его отчёт — открытый дамп, см. п. 3 шапки).
	static void DisableEngineHangReports()
	{
		if (GConfig)
		{
			GConfig->SetDouble(TEXT("Core.System"), TEXT("HangDuration"), 0.0, GEngineIni);
			GConfig->SetDouble(TEXT("Core.System"), TEXT("PresentHangDuration"), 0.0, GEngineIni);
		}
		FThreadHeartBeat::Get().InitSettings();
	}
}

namespace AsdaLogSeal
{
	// Перенести открытые файлы прошлых сборок в запечатанный вид (см. п. 4 шапки). SavedDir — Saved игры, RawDir — каталог
	//   AsdaEac_raw.log. Объявление и правила — LogSeal/AsdaLogSealPrivate.h.
	// ★ 19.10.2026 (ревизия безопасности журналов, В4): раньше папка Saved/Crashes/<имя> бралась целиком рекурсивно и удалялась
	//   DeleteDirectoryRecursively — джанкшен <имя> → «Документы» запечатал бы и удалил чужие файлы. Теперь: каталоги Saved,
	//   Saved/Logs, Saved/Crashes, RawDir и каждая папка падения — только «свои» (IsPlainDirectory: не точка повторного разбора,
	//   между корнем Saved/RawDir и ней нет джанкшенов/симлинков); в папке падения — только её прямые файлы известных движку видов; каждый файл —
	//   OpenOwnedFile (обычный, одна ссылка, без джанкшена/симлинка на пути, ≤ 64 МиБ, пакет ≤ 64 МиБ); удаляется только
	//   запечатанное и только по своему дескриптору; папка падения — RemoveDirectory (только пустая).
	int32 SweepLegacyPlainFiles(const FString& SavedDir, const FString& RawDir)
	{
		IPlatformFile& PF = IPlatformFile::GetPlatformPhysical();
		int32 Bundles = 0;
		const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
		if (!IsPlainDirectory(SavedDir, SavedDir)) { return 0; }   // Saved — джанкшен/симлинк или нет: ничего не трогаем
		// Прямые файлы каталога Dir с подходящим расширением → открытые «свои» файлы (общий предел пакета).
		auto CollectOwned = [&PF, &SavedDir](const FString& Dir, TFunctionRef<bool(const FString& Ext)> Want, TArray<FOwnedFile>& Out)
		{
			TArray<FString> Names;
			PF.IterateDirectory(*Dir, [&Names](const TCHAR* Name, bool bIsDir) { if (!bIsDir) { Names.Add(Name); } return true; });
			Names.Sort();
			uint64 Total = 0;
			for (const FString& N : Names)
			{
				if (!Want(FPaths::GetExtension(N).ToLower())) { continue; }
				FOwnedFile F;
				if (OpenOwnedFile(N, SavedDir, kMaxBundleSourceBytes - Total, F)) { Total += F.Size; Out.Add(F); }
			}
		};
		// Запечатать набор; при успехе удалить исходники по дескрипторам, иначе просто закрыть.
		auto SealAndDelete = [&Bundles](TArray<FOwnedFile>& Owned, const FString& Out, const FString& Base, const TCHAR* Note)
		{
			const bool bOk = Owned.Num() > 0 && SealOwnedFilesToBundle(EKind::Legacy, Out, Owned, Base, Note);
			for (FOwnedFile& F : Owned) { CloseOwnedFile(F, bOk); }
			Owned.Reset();
			Bundles += bOk ? 1 : 0;
			return bOk;
		};
		// Падения движка: каждая «своя» папка Saved/Crashes/<имя> → Saved/Crashes/<имя>_<время>.acrash.
		const FString CrashDir = FPaths::Combine(SavedDir, TEXT("Crashes"));
		if (IsPlainDirectory(CrashDir, SavedDir))
		{
			TArray<FString> Dirs;
			PF.IterateDirectory(*CrashDir, [&Dirs](const TCHAR* Name, bool bIsDir) { if (bIsDir) { Dirs.Add(Name); } return true; });
			for (const FString& D : Dirs)
			{
				if (!IsPlainDirectory(D, SavedDir)) { continue; }   // джанкшен/симлинк внутри Crashes — не трогаем вовсе
				TArray<FOwnedFile> Owned;
				CollectOwned(D, [](const FString& Ext)
				{
					return Ext == TEXT("runtime-xml") || Ext == TEXT("dmp") || Ext == TEXT("log") || Ext == TEXT("txt") || Ext == TEXT("xml") || Ext == TEXT("json");
				}, Owned);
				if (Owned.Num() == 0) { PF.DeleteDirectory(*D); continue; }   // пустая папка — убрать (не пустая не удалится)
				FString Name = FPaths::GetCleanFilename(D);
				if (Name.IsEmpty() || Name.StartsWith(TEXT("_"))) { Name = TEXT("UE") + Name; }
				const FString Out = FPaths::Combine(CrashDir, FString::Printf(TEXT("%s_%s.acrash"), *Name, *Stamp));
				if (SealAndDelete(Owned, Out, SavedDir, TEXT("открытая папка падения движка (до установки обработчика или прошлая сборка)")))
				{
					PF.DeleteDirectory(*D);   // только пустая: чужие/неизвестные файлы остаются вместе с папкой
				}
			}
		}
		// Открытые журналы прошлых сборок: Saved/Logs/*.log|*.dmp|*.txt и AsdaEac_raw.log → Saved/Logs/Legacy_<время>.alog.
		const FString LogDir = FPaths::Combine(SavedDir, TEXT("Logs"));
		if (IsPlainDirectory(LogDir, SavedDir))
		{
			TArray<FOwnedFile> Owned;
			CollectOwned(LogDir, [](const FString& Ext) { return Ext == TEXT("log") || Ext == TEXT("dmp") || Ext == TEXT("txt"); }, Owned);
			uint64 Total = 0;
			for (const FOwnedFile& F : Owned) { Total += F.Size; }
			const FString Raw = FPaths::Combine(RawDir, TEXT("AsdaEac_raw.log"));
			FOwnedFile RawF;
			if (IsPlainDirectory(RawDir, RawDir) && PF.FileExists(*Raw) && OpenOwnedFile(Raw, RawDir, kMaxBundleSourceBytes - Total, RawF)) { Owned.Add(RawF); }
			SealAndDelete(Owned, FPaths::Combine(LogDir, FString::Printf(TEXT("Legacy_%s.alog"), *Stamp)), SavedDir, TEXT("открытые журналы прошлых сборок"));
		}
		return Bundles;
	}

#if PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
	// Самотест: пакет падения по синтетическому исключению — тем же путём (нить записи ≠ упавшая нить), без завершения процесса.
	//   Объявление — LogSeal/AsdaLogSealPrivate.h. Возврат: путь пакета (пусто — не вышло).
	FString SelfTestWriteCrashBundle(const FString& OutPath)
	{
		using namespace AsdaLogSealRetailPriv;
		if (!GCrashSeg)
		{
			GCrashDir = FPaths::GetPath(OutPath);
			GCrashSeg = new FSegment();
			GDumpBase = static_cast<uint8*>(::VirtualAlloc(nullptr, kDumpReserve, MEM_RESERVE, PAGE_READWRITE));
			GNulHandle = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		}
		struct FCtx { EXCEPTION_POINTERS* Ep; DWORD Tid; FString Out; FString Result; };
		FCtx Ctx{ nullptr, ::GetCurrentThreadId(), OutPath, FString() };
		// Фильтр синтетического исключения: пакет пишет отдельная нить (как в бою), упавшая ждёт.
		auto Filter = [](EXCEPTION_POINTERS* Ep, FCtx* C) -> int
		{
			C->Ep = Ep;
			HANDLE H = ::CreateThread(nullptr, 256 * 1024, [](LPVOID P) -> DWORD
			{
				FCtx* X = static_cast<FCtx*>(P);
				X->Result = WriteCrashBundle(X->Ep, X->Tid, X->Out);
				return 0;
			}, C, 0, nullptr);
			if (H) { ::WaitForSingleObject(H, 120 * 1000); ::CloseHandle(H); }
			return EXCEPTION_EXECUTE_HANDLER;
		};
		struct FRaise
		{
			static void Run(FCtx* C, int (*F)(EXCEPTION_POINTERS*, FCtx*))
			{
				__try { ::RaiseException(0xE0A5DA7E, 0, 0, nullptr); }
				__except (F(GetExceptionInformation(), C)) {}
			}
		};
		FRaise::Run(&Ctx, Filter);
		return Ctx.Result;
	}

	// ★ 31.10.2026 самотест ротации журнала сессии (объявление и правила — LogSeal/AsdaLogSealPrivate.h).
	FSelfTestRotationResult SelfTestSessionRotation(const FString& SessionPath, const uint8* Pub, int64 TotalBytes, int32 Rotate, int32 Compress,
		const FString& Account, const FString& CrashOut, int64 FailAtLine, int32 FailWrites)
	{
		using namespace AsdaLogSealRetailPriv;
		FSelfTestRotationResult Res;
		const int32 OldRotate = CVarAsdaLogSealRotate.GetValueOnAnyThread();
		const int32 OldCompress = CVarAsdaLogSealCompress.GetValueOnAnyThread();
		CVarAsdaLogSealRotate->Set(Rotate, ECVF_SetByCode);
		CVarAsdaLogSealCompress->Set(Compress, ECVF_SetByCode);
		{
			FSealedLogDevice Dev(SessionPath, Pub);
			const FName Category(TEXT("LogAsdaLogSealRotTest"));
			// Нагрузка строки: 160 символов из 64 (≈ 6 бит на символ) — zlib сжимает её слабо, части заполняются быстро.
			static const TCHAR Alphabet[] = TEXT("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+/");
			uint64 X = 0x9E3779B97F4A7C15ull;
			TCHAR Payload[161];
			int64 Bytes = 0;
			FString Line;
			const double Start = FPlatformTime::Seconds();
			while (Bytes < TotalBytes || (FailWrites > 0 && Dev.IsWriteBroken() && FPlatformTime::Seconds() - Start < 15.0))
			{
				if (Res.LinesLogged == FailAtLine) { Dev.SelfTestFailWrites(FailWrites); }
				for (int32 I = 0; I < 160; ++I)
				{
					X ^= X << 13;
					X ^= X >> 7;
					X ^= X << 17;
					Payload[I] = Alphabet[X & 63];
				}
				Payload[160] = 0;
				Line = FString::Printf(TEXT("ROT-LINE #%07lld %s"), (long long)Res.LinesLogged, Payload);
				Dev.Serialize(*Line, ELogVerbosity::Log, Category);
				if (Res.LinesLogged == 0 && !Account.IsEmpty()) { Dev.SetAccount(Account); }   // вход — после первой строки, как в игре
				Bytes += Line.Len() + 48;   // + метка времени, категория и уровень
				++Res.LinesLogged;
			}
			Dev.Serialize(*FString::Printf(TEXT("ROT-LAST #%07lld конец прогона"), (long long)Res.LinesLogged), ELogVerbosity::Log, Category);
			if (!CrashOut.IsEmpty())
			{
				GCrashTail.Reset();
				GCrashTailPart.Reset();
				GCrashTailBusy = !Dev.FlushFromCrashThread(GCrashTail, GCrashTailPart);
				Res.CrashBundle = SelfTestWriteCrashBundle(CrashOut);
				GCrashTail.Reset();
				GCrashTailPart.Reset();
				GCrashTailBusy = false;
			}
			Res.Dropped = Dev.GetDropped();
			Res.Parts = Dev.GetPartNo();
			Res.WriteFailures = Dev.GetWriteFailures();
			Dev.TearDown();
		}
		CVarAsdaLogSealRotate->Set(OldRotate, ECVF_SetByCode);
		CVarAsdaLogSealCompress->Set(OldCompress, ECVF_SetByCode);
		// Файлы сессии в папке — по номеру части.
		FString Key;
		ParseSessionLogName(FPaths::GetCleanFilename(SessionPath), &Key);
		TArray<TPair<int32, FString>> Found;
		IPlatformFile::GetPlatformPhysical().IterateDirectory(*FPaths::GetPath(SessionPath), [&Found, &Key](const TCHAR* Name, bool bIsDir)
		{
			FString K;
			int32 Part = 0;
			if (!bIsDir && ParseSessionLogName(FPaths::GetCleanFilename(Name), &K, &Part) && K == Key) { Found.Add({ Part, FString(Name) }); }
			return true;
		});
		Found.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B) { return A.Key < B.Key; });
		for (const TPair<int32, FString>& F : Found) { Res.Files.Add(F.Value); }
		return Res;
	}
#endif

	void InstallRetailPolicy()
	{
		using namespace AsdaLogSealRetailPriv;
		if (!IsActive()) { return; }
		// 1. Журнал сессии (★ 19.10: файл на запуск, Saved/Logs/Sessions/<ГГГГММДД-ЧЧММСС>_<pid>.alog).
		if (GLog && !GEngineDevice)
		{
			const FString Name = FString::Printf(TEXT("%s_%u.alog"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")),
				FPlatformProcess::GetCurrentProcessId());
			GEngineDevice = new FSealedLogDevice(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"),
				TEXT("Sessions"), Name)));
#if UE_BUILD_SHIPPING
			// Открытые устройства движка: файл Saved/Logs/AsdaReborn.log (при NO_LOGGING пуст, но снимаем) и консоль (-log;
			//   движок с NO_LOGGING её в GLog не ставит — снимаем на случай смены сборки движка). -stdout вычищен до WinMain.
			GLog->RemoveOutputDevice(FPlatformOutputDevices::GetLog());
			if (GLogConsole) { GLog->RemoveOutputDevice(GLogConsole); }
#endif
			GLog->SerializeBacklog(GEngineDevice);   // ★ 16.10: всё, что попало в GLog до загрузки AsdaNet, — тоже запечатанным
			GLog->AddOutputDevice(GEngineDevice);
			GFlushTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				if (GEngineDevice) { GEngineDevice->FlushIfStale(); }
				return true;
			}), static_cast<float>(kBatchSeconds));
		}
#if PLATFORM_WINDOWS
		// 2. Падения.
		InstallCrashHandler();
#endif
		// 3. Зависания.
		DisableEngineHangReports();
#if UE_BUILD_SHIPPING
		// 4. Перенос открытых файлов прошлых сборок и ранних падений — отдельной нитью (не задерживает запуск).
		const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		const FString RawDir = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")), TEXT("AsdaReborn"));
		GSweepFuture = Async(EAsyncExecution::Thread, [Saved, RawDir]() { SweepLegacyPlainFiles(Saved, RawDir); });
#endif
	}

	void ShutdownRetailPolicy()
	{
		using namespace AsdaLogSealRetailPriv;
		if (GSweepFuture.IsValid()) { GSweepFuture.Wait(); }
		if (GFlushTicker.IsValid()) { FTSTicker::RemoveTicker(GFlushTicker); GFlushTicker.Reset(); }
		if (GEngineDevice) { GEngineDevice->Flush(); }
	}

	FString SessionLogPath()
	{
		using namespace AsdaLogSealRetailPriv;
		return GEngineDevice ? GEngineDevice->GetPath() : FString();
	}

	bool IsValidAccountLogin(const FString& Login)
	{
		if (Login.Len() < 1 || Login.Len() > 32) { return false; }
		for (const TCHAR C : Login)
		{
			const bool bOk = (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9'))
				|| C == TEXT('_') || C == TEXT('.') || C == TEXT('@') || C == TEXT('-');
			if (!bOk) { return false; }
		}
		return true;
	}

	bool SetSessionAccount(const FString& Login)
	{
		using namespace AsdaLogSealRetailPriv;
		if (!GEngineDevice || !IsValidAccountLogin(Login)) { return false; }
		// ★ 31.10: meta пишет устройство — у каждой сохранившейся части сессии и у будущих частей при их открытии.
		const bool bOk = GEngineDevice->SetAccount(Login);
		UE_LOG(LogAsdaLogSeal, Log, TEXT("учётка сессии: %s — %s.meta %s"), *Login, *FPaths::GetCleanFilename(GEngineDevice->GetPath()), bOk ? TEXT("записан") : TEXT("НЕ записан"));
		return bOk;
	}
}

#if !UE_BUILD_SHIPPING
// ★ Дев-команда: настоящее падение процесса для сквозной проверки ритейл-обработчика (только с -AsdaLogSeal).
static FAutoConsoleCommand GAsdaLogSealCrashNow(
	TEXT("asda.LogSeal.CrashNow"),
	TEXT("asda.LogSeal.CrashNow — уронить процесс (запись по нулевому адресу), чтобы проверить пакет падения .acrash. Только с -AsdaLogSeal."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		if (!AsdaLogSeal::IsActive()) { return; }
		volatile int32* P = nullptr;
		*P = 0x5EA1;
	}));
#endif
