// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLogSeal.h — ★ ЗАПЕЧАТАННЫЕ ЖУРНАЛЫ РИТЕЙЛ-КЛИЕНТА (решение владельца 16.10.2026).
//
// ПРАВИЛО. Всё, что Shipping-клиент (в том числе альфа) пишет как журнал или диагностику, ложится на диск ТОЛЬКО в
//   зашифрованном виде. В клиенте — лишь ОТКРЫТЫЙ ключ P-256 получателя (доли с масками, AsdaLogSealKey.generated.h);
//   закрытый — только у нас (AstoryServer/Patches/AsdaSecure/out/keys/logseal). Development-сборки пишут как раньше, открытым текстом;
//   ключ запуска -AsdaLogSeal включает запечатывание и в Development (проверка без Shipping-сборки).
//
// КРИПТО. Никакой своей криптографии: сегмент журнала — это рукопожатие AsdaSecure (Session::ClientHello + ClientFinish)
//   против НАШЕГО долговременного открытого ключа вместо эфемерного ключа сервера (ECIES на ECDH P-256 + HKDF-SHA256),
//   кадры — Session::Seal (AES-256-GCM, seq в AAD и IV). Под-сервер рукопожатия — kLogSubServer = 0x4C ('L'): ключи журнала
//   разведены с ключами трафика (1–4). На каждый сегмент — новая эфемерная пара; после записи ключи затираются.
//
// ФОРМАТ (.alog / .acrash) и расшифровка — Tools/asda_logseal.py (там же описание байтов). Коротко: сегмент = заголовок
//   128 б («ASLGSEG1», версия, вид, keyId, ClientHello, nonce) + кадры; запись в кадре = тип (1 байт) + данные.
#pragma once

#include "CoreMinimal.h"

namespace AsdaLogSeal
{
	// Вид сегмента (байт 9 заголовка) — для расшифровщика и разбора.
	enum class EKind : uint8
	{
		EacJournal  = 1,    // журнал клиентского EAC (AsdaEac.alog)
		EacRaw      = 2,    // запасной маркер EAC прямым Win32 (AsdaEac_raw.alog)
		EngineLog   = 3,    // журнал сессии: всё из GLog (в Shipping — и UE_LOG наших модулей), Saved/Logs/Sessions/*.alog (★ 19.10)
		CrashBundle = 4,    // пакет падения: сводка + minidump (.acrash)
		Legacy      = 5,    // перенос старых открытых файлов (журналы/падения прошлых сборок) в запечатанный вид
		SelfTest    = 127,  // самотест asda.SelfTest.LogSeal
	};

	// Тип записи внутри кадра (первый байт открытого текста кадра).
	enum class ERecord : uint8
	{
		Info      = 0x01,   // сведения о сегменте: UTF-8 «ключ=значение\n»
		Text      = 0x02,   // строки журнала, UTF-8
		TextZ     = 0x03,   // ★ 31.10.2026: строки журнала, сжатые zlib: u32 длина UTF-8 (LE) + поток zlib (одна запись = один кадр)
		FileBegin = 0x10,   // начало файла пакета: u16 длина имени, имя UTF-8, u64 размер
		FileData  = 0x11,   // кусок данных файла
		FileEnd   = 0x12,   // конец файла
	};

	// Длина заголовка сегмента, байт.
	constexpr int32 kSegmentHeaderLen = 128;
	// Предел данных одной записи (кадр AsdaSecure ≤ 64 КБ открытого текста, минус байт типа и запас под заголовок файла).
	constexpr int32 kMaxRecordData = 60 * 1024;
	// Под-сервер AsdaSecure для журналов ('L') — разводит ключи журналов с ключами трафика.
	constexpr uint8 kLogSubServer = 0x4C;

	// Политика: запечатывать ли журналы в этом процессе. Shipping — всегда; иначе — только с ключом запуска -AsdaLogSeal.
	ASDANET_API bool IsActive();
	// Вшит ли открытый ключ (генератор запускался). В Shipping без ключа сборка не собирается (#error).
	ASDANET_API bool HasKey();
	// keyId вшитого ключа (16 hex-символов) — для строки «запуск» и сверки с расшифровщиком.
	ASDANET_API FString KeyIdHex();

	// Один сегмент запечатанного журнала. Байты для записи на диск копятся во внутреннем буфере (Pending) — вызывающий
	//   пишет их и зовёт ClearPending. Буфер заранее зарезервирован: в обработчике падения запись куска ≤ kMaxRecordData не
	//   перераспределяет память. Не потокобезопасен — сериализует владелец.
	class ASDANET_API FSegment
	{
	public:
		FSegment();
		~FSegment();
		FSegment(const FSegment&) = delete;
		FSegment& operator=(const FSegment&) = delete;

		// Начать сегмент: эфемерная пара, вывод ключа, заголовок 128 б + запись Info в Pending.
		//   OverridePub — 64 байта X||Y другого получателя (только самотест вне Shipping; в Shipping игнорируется).
		//   false — ключа нет / отказ CNG: сегмент не открыт, Pending пуст (вызывающий НЕ пишет ничего открытым текстом).
		bool Begin(EKind Kind, const FString& InfoText, const uint8* OverridePub = nullptr);
		// Запечатать запись (данные любой длины режутся на куски ≤ kMaxRecordData, каждый — свой кадр с тем же типом).
		bool Seal(ERecord Type, const uint8* Data, int64 Len);
		// Запечатать текст UTF-8 записью Text.
		bool SealText(const FString& Text);
		// Начало / конец файла пакета (FileBegin / FileEnd).
		bool SealFileBegin(const FString& Name, uint64 Size);
		bool SealFileEnd();
		// Сегмент открыт (Begin прошёл, End не звали).
		bool IsOpen() const;
		// Закрыть сегмент: затереть ключи. Pending не трогается (его ещё можно записать).
		void End();

		// Накопленные байты для записи.
		const uint8* PendingData() const;
		int64 PendingNum() const;
		void ClearPending();

	private:
		struct FImpl;
		FImpl* Impl;   // состояние AsdaSecure::Session и буфер (скрыты от включающих: AsdaSecure.h — приватный заголовок AsdaNet)
	};

	// Запечатать набор файлов одним пакетом (сегмент вида Kind, по записи FileBegin/FileData/FileEnd на файл) в OutPath.
	//   Имена в пакете — пути относительно BaseDir. Note — текст в сведения сегмента. true — файл записан целиком и
	//   сброшен на диск (только тогда вызывающему можно удалять исходники).
	//   ★ 19.10.2026 (ревизия В4): берутся только обычные файлы с одной ссылкой без джанкшена/симлинка на пути, сумма ≤ 64 МиБ (прочие пропускаются);
	//   пакет пишется только в НОВЫЙ файл (существующий не перезаписывается).
	ASDANET_API bool SealFilesToBundle(EKind Kind, const FString& OutPath, const TArray<FString>& Files, const FString& BaseDir, const FString& Note);

	// Строка сведений сегмента по умолчанию: время, PID, конфигурация, версия сборки, keyId, вид.
	ASDANET_API FString MakeInfoText(EKind Kind, const FString& Extra = FString());

	// Ритейл-политика процесса (AsdaNet::StartupModule): журнал сессии (GLog) → запечатанный файл Saved/Logs/Sessions/
	//   <ГГГГММДД-ЧЧММСС>_<pid>.alog (хранятся последние 30 сессий; ★ 31.10 — у предела части следующая <…>_partN.alog),
	//   свой обработчик падений (движок в Shipping открытых дампов не пишет), отключение отчёта о зависании, перенос старых
	//   открытых файлов. Вне IsActive() ничего не делает.
	ASDANET_API void InstallRetailPolicy();
	// Снять политику при выгрузке модуля: дописать журнал движка, остановить нить переноса.
	ASDANET_API void ShutdownRetailPolicy();

	// ★ 19.10.2026 (журналы релиза, 00_SPEC.md «Дополнение»): полный путь файла журнала текущей сессии (★ 31.10 — текущей
	//   части); пусто — политика не активна.
	ASDANET_API FString SessionLogPath();
	// Логин учётки годится для .alog.meta и пути на сервере: только [A-Za-z0-9_.@-], длина 1..32 (как us_id в БД).
	ASDANET_API bool IsValidAccountLogin(const FString& Login);
	// После успешного входа (1004, статус 1): рядом с файлом сессии — открытый <файл>.alog.meta, UTF-8 JSON
	//   {"account":"<логин>","session":"<имя файла>"} (★ 31.10 — у каждой сохранившейся части сессии свой; новая часть
	//   получает meta сразу при открытии); повторный вход другой учёткой перезаписывает. Пароль/MD5 сюда не попадают
	//   никогда (параметр — только логин). Логин не прошёл проверку или политика не активна — ничего не пишется, false.
	ASDANET_API bool SetSessionAccount(const FString& Login);
}
