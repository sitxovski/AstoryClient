// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLogSealPrivate.h — внутренние точки запечатанных журналов для политики и самотеста модуля AsdaNet (не для других модулей).
#pragma once

#include "CoreMinimal.h"
#include "AsdaLogSeal.h"

namespace AsdaLogSeal
{
	// Предел одного пакета переноса/набора файлов (байт исходников): больше — файл пропускается (★ 19.10, ревизия В4).
	constexpr uint64 kMaxBundleSourceBytes = uint64(64) * 1024 * 1024;

	// ★ 19.10.2026 (ревизия безопасности журналов, В4): файл, взятый в пакет. Дескриптор открыт на чтение + DELETE без
	//   совместной записи/удаления и держится от проверки до записи пакета и удаления — подменить файл (джанкшеном, жёсткой
	//   ссылкой, переименованием) между проверкой и удалением нельзя. Удаление — только по этому дескриптору.
	struct FOwnedFile
	{
		void* Handle = nullptr;   // HANDLE (Windows); nullptr — не открыт
		FString Path;             // заданный путь (полный)
		uint64 Size = 0;          // размер на момент открытия
	};

	// Открыть файл для пакета. Принимается ТОЛЬКО обычный файл: не точка повторного разбора (джанкшен/симлинк), не каталог и
	//   не устройство, ровно одна ссылка (жёсткие ссылки отклоняются), файл лежит внутри корня Root и его настоящий путь
	//   (GetFinalPathNameByHandle) = настоящий путь Root + тот же хвост — ни одного джанкшена/симлинка между Root и файлом
	//   (Root сам не точка повторного разбора; его предки — перенаправление профиля и т. п. — не проверяются), размер ≤ MaxBytes.
	//   false — файл не трогается; OutWhy — причина.
	bool OpenOwnedFile(const FString& Path, const FString& Root, uint64 MaxBytes, FOwnedFile& Out, FString* OutWhy = nullptr);
	// Закрыть файл; bDelete — удалить его по дескриптору (FileDispositionInfo), а не по имени.
	void CloseOwnedFile(FOwnedFile& File, bool bDelete);
	// Каталог «свой»: существует, не точка повторного разбора, лежит в корне Root (или равен ему) и между Root и ним нет
	//   джанкшенов/симлинков (правило — как у OpenOwnedFile).
	bool IsPlainDirectory(const FString& Dir, const FString& Root);
	// Запечатать открытые файлы одним пакетом (вид Kind) в НОВЫЙ файл OutPath (существующий не перезаписывается). Имена в
	//   пакете — относительно BaseDir. true — пакет записан целиком и сброшен на диск.
	bool SealOwnedFilesToBundle(EKind Kind, const FString& OutPath, const TArray<FOwnedFile>& Files, const FString& BaseDir, const FString& Note);

	// Перенести открытые файлы прошлых сборок (Saved/Crashes/<папка>/{*.runtime-xml,*.dmp,*.log,*.txt,*.xml,*.json},
	//   Saved/Logs/*.log|*.dmp|*.txt, <RawDir>/AsdaEac_raw.log) в запечатанные пакеты — только «свои» файлы (OpenOwnedFile) внутри
	//   «своих» каталогов (IsPlainDirectory); исходники удаляются по дескриптору только после успешной записи, папка падения —
	//   RemoveDirectory (только пустая, без рекурсии). Возврат: число записанных пакетов.
	int32 SweepLegacyPlainFiles(const FString& SavedDir, const FString& RawDir);

#if PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
	// Самотест: пакет падения по синтетическому исключению (нить записи ≠ упавшая), процесс не завершается. Возврат: путь пакета.
	FString SelfTestWriteCrashBundle(const FString& OutPath);

	// ★ 31.10.2026 (ротация журнала сессии): итог прогона отдельного устройства журнала сессии.
	struct FSelfTestRotationResult
	{
		TArray<FString> Files;      // файлы сессии в папке после прогона (полные пути, по номеру части)
		int64 LinesLogged = 0;      // сколько строк уровня Log отдано устройству
		int32 Dropped = 0;          // сколько строк устройство отбросило (прежняя обрезка)
		int32 Parts = 0;            // номер последней открытой части
		int32 WriteFailures = 0;    // сколько раз срывалась запись (имитация FailWrites)
		FString CrashBundle;        // путь пакета падения с хвостом последней части (пусто — не писался / не вышло)
	};
	// Самотест: устройство журнала сессии (тот же класс, что у GLog) пишет SessionPath строками уровня Log объёмом TotalBytes
	//   (строки «ROT-LINE #<номер> <полезная нагрузка>», нагрузка плохо сжимается). Pub — 64 б тестового получателя (nullptr —
	//   вшитый ключ). Rotate/Compress — значения ключей ротации и сжатия на время прогона (затем прежние). Account — логин для
	//   meta (пусто — не писать). CrashOut — непусто: после строк снять пакет падения с хвостом части туда. FailAtLine ≥ 0 — на
	//   этой строке имитировать сбой FailWrites следующих записей (половина байт + ошибка; «нет места»), прогон идёт, пока запись
	//   не восстановится (не дольше 15 с сверх объёма).
	FSelfTestRotationResult SelfTestSessionRotation(const FString& SessionPath, const uint8* Pub, int64 TotalBytes, int32 Rotate, int32 Compress,
		const FString& Account, const FString& CrashOut, int64 FailAtLine = -1, int32 FailWrites = 0);
#endif
}
