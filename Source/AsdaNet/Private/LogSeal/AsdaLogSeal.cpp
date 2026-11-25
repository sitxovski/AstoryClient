// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLogSeal.cpp — сегмент запечатанного журнала поверх AsdaSecure (описание схемы и правила — AsdaLogSeal.h,
//   байтовый формат и расшифровка — Tools/asda_logseal.py). Ритейл-политика процесса — AsdaLogSealRetail.cpp.
#include "AsdaLogSeal.h"
#include "LogSeal/AsdaLogSealPrivate.h"   // ★ 19.10: FOwnedFile / IsPlainDirectory (ревизия В4)
#include "Secure/AsdaSecure.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include <vector>
#include <cstring>
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if __has_include("LogSeal/AsdaLogSealKey.generated.h")
#include "LogSeal/AsdaLogSealKey.generated.h"
#endif

#ifndef ASDA_LOGSEAL_PRESENT
// Ключа нет (Tools/asda_logseal.py keygen не запускался): Development собирается и пишет журналы как раньше.
#define ASDA_LOGSEAL_PRESENT 0
#endif

// ★ Ритейл без ключа запрещён: иначе журналы Shipping было бы нечем запечатать (а открытым текстом их писать нельзя).
#if UE_BUILD_SHIPPING && !ASDA_LOGSEAL_PRESENT
#error "AsdaLogSeal: в Shipping нужен открытый ключ журналов — python Tools/asda_logseal.py keygen (создаёт AsdaLogSealKey.generated.h)"
#endif

namespace AsdaLogSealPriv
{
	// Метка сегмента (8 байт).
	static const uint8 kSegMagic[8] = { 'A', 'S', 'L', 'G', 'S', 'E', 'G', '1' };
	// Версия формата файла (байт 8 заголовка).
	constexpr uint8 kFormatVersion = 1;
	// Резерв буфера записи: два полных кадра + заголовок — запись куска в обработчике падения не перераспределяет память.
	constexpr size_t kPendingReserve = 2 * (AsdaSecure::kMaxFrame) + AsdaLogSeal::kSegmentHeaderLen;

	// Поток маски долей (формула 1:1 с Tools/asda_logseal.py::_xorshift64_mask и ключом pak).
	static void MaskStream(uint64 Seed, uint8* Out, int32 N)
	{
		uint64 X = Seed;
		for (int32 I = 0; I < N; ++I)
		{
			X ^= X << 13;
			X ^= X >> 7;
			X ^= X << 17;
			Out[I] = static_cast<uint8>(X >> ((I % 8) * 8));
		}
	}

	// SHA-256 (CNG, псевдо-дескриптор алгоритма — без открытия провайдера). false — отказ.
	static bool Sha256(const uint8* Data, size_t Len, uint8 Out[32])
	{
#if PLATFORM_WINDOWS
		return BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, (PUCHAR)Data, (ULONG)Len, Out, 32));
#else
		return false;
#endif
	}

	// Восстановить открытый ключ из долей и сверить его keyId с вшитым. false — ключа нет или доли испорчены.
	static bool RestorePublicKey(uint8 OutPub[64], uint8 OutKeyId[8])
	{
		FMemory::Memzero(OutPub, 64);
#if ASDA_LOGSEAL_PRESENT
		for (int32 Share = 0; Share < ASDA_LOGSEAL_SHARES; ++Share)
		{
			uint8 Mask[64];
			MaskStream(ASDA_LOGSEAL_SEED[Share], Mask, 64);
			for (int32 I = 0; I < 64; ++I)
			{
				OutPub[ASDA_LOGSEAL_PERM[Share][I]] ^= static_cast<uint8>(ASDA_LOGSEAL_PART[Share][I] ^ Mask[I]);
			}
		}
		uint8 H[32];
		if (!Sha256(OutPub, 64, H) || FMemory::Memcmp(H, ASDA_LOGSEAL_KEYID, 8) != 0)
		{
			FMemory::Memzero(OutPub, 64);
			return false;
		}
		FMemory::Memcpy(OutKeyId, ASDA_LOGSEAL_KEYID, 8);
		return true;
#else
		(void)OutKeyId;
		return false;
#endif
	}
}

namespace AsdaLogSeal
{
	bool IsActive()
	{
#if UE_BUILD_SHIPPING
		return true;
#else
		// Вне Shipping — только по явному ключу запуска (проверка ритейл-поведения в Development). Решение кэшируется.
		static const bool bParam = FParse::Param(FCommandLine::Get(), TEXT("AsdaLogSeal"));
		return bParam;
#endif
	}

	bool HasKey()
	{
		uint8 Pub[64], Kid[8];
		const bool bOk = AsdaLogSealPriv::RestorePublicKey(Pub, Kid);
		FMemory::Memzero(Pub, sizeof(Pub));
		return bOk;
	}

	FString KeyIdHex()
	{
		uint8 Pub[64], Kid[8];
		if (!AsdaLogSealPriv::RestorePublicKey(Pub, Kid)) { return TEXT("нет"); }
		FMemory::Memzero(Pub, sizeof(Pub));
		return BytesToHex(Kid, 8).ToLower();
	}

	FString MakeInfoText(EKind Kind, const FString& Extra)
	{
		FString S = FString::Printf(TEXT("kind=%u\ntime=%s\ntime_utc=%s\npid=%u\nconfig=%s\nproject=%s\nbuild=%s\nexe=%s\n"),
			(uint32)Kind, *FDateTime::Now().ToString(TEXT("%Y.%m.%d %H:%M:%S")), *FDateTime::UtcNow().ToIso8601(),
			FPlatformProcess::GetCurrentProcessId(), LexToString(FApp::GetBuildConfiguration()), FApp::GetProjectName(),
			FApp::GetBuildVersion(), FPlatformProcess::ExecutablePath());
		if (!Extra.IsEmpty())
		{
			S += Extra;
			if (!S.EndsWith(TEXT("\n"))) { S += TEXT("\n"); }
		}
		return S;
	}

	// ── FSegment ────────────────────────────────────────────────────────────────────────────────────────────────
	struct FSegment::FImpl
	{
		AsdaSecure::Session Sess;          // рукопожатие против ключа получателя + кадры Seal
		std::vector<uint8_t> Pending;      // байты для записи на диск (заголовок + кадры)
		std::vector<uint8_t> Plain;        // открытый текст одной записи (тип + кусок данных)
		bool bOpen = false;                // сегмент открыт

		FImpl()
		{
			Pending.reserve(AsdaLogSealPriv::kPendingReserve);
			Plain.reserve(kMaxRecordData + 1);
		}
	};

	FSegment::FSegment() : Impl(new FImpl()) {}

	FSegment::~FSegment()
	{
		End();
		delete Impl;
	}

	bool FSegment::Begin(EKind Kind, const FString& InfoText, const uint8* OverridePub)
	{
		using namespace AsdaSecure;
		End();
		if (!AsdaSecure::Init()) { return false; }
		uint8 Pub[64], Kid[8];
#if !UE_BUILD_SHIPPING
		if (OverridePub)
		{
			uint8 H[32];
			FMemory::Memcpy(Pub, OverridePub, 64);
			if (!AsdaLogSealPriv::Sha256(Pub, 64, H)) { return false; }
			FMemory::Memcpy(Kid, H, 8);
		}
		else
#endif
		if (!AsdaLogSealPriv::RestorePublicKey(Pub, Kid))
		{
			return false;
		}
		PublicKey Recipient;
		FMemory::Memcpy(Recipient.xy, Pub, kPubLen);
		// Шаг 1 рукопожатия AsdaSecure: эфемерная пара + ClientHello (под-сервер журнала 'L').
		uint8 Ch[kClientHelloLen];
		if (!Impl->Sess.ClientHello(static_cast<SubServer>(kLogSubServer), Recipient, Ch))
		{
			Impl->Sess.Reset();
			return false;
		}
		// «ServerHello» получателя: его ДОЛГОВРЕМЕННЫЙ открытый ключ + случайный nonce. Подпись не нужна и не проверяется
		//   (ClientFinish её не смотрит; аутентичность получателя даёт вшитый ключ).
		uint8 Sh[kServerHelloLen] = { kServerMagic, kVersion, kLogSubServer, 0 };
		FMemory::Memcpy(Sh + 4, Pub, kPubLen);
		if (!RandomBytes(Sh + 4 + kPubLen, kNonceLen) || Impl->Sess.ClientFinish(Sh, sizeof(Sh)) != Err::Ok)
		{
			Impl->Sess.Reset();
			return false;
		}
		// Заголовок сегмента 128 б.
		uint8 Hdr[kSegmentHeaderLen] = {};
		FMemory::Memcpy(Hdr, AsdaLogSealPriv::kSegMagic, 8);
		Hdr[8] = AsdaLogSealPriv::kFormatVersion;
		Hdr[9] = static_cast<uint8>(Kind);
		FMemory::Memcpy(Hdr + 12, Kid, 8);
		FMemory::Memcpy(Hdr + 20, Ch, kClientHelloLen);
		FMemory::Memcpy(Hdr + 20 + kClientHelloLen, Sh + 4 + kPubLen, kNonceLen);
		Impl->Pending.insert(Impl->Pending.end(), Hdr, Hdr + kSegmentHeaderLen);
		Impl->bOpen = true;
		const FTCHARToUTF8 U(*InfoText);
		if (!Seal(ERecord::Info, reinterpret_cast<const uint8*>(U.Get()), U.Length()))
		{
			End();
			Impl->Pending.clear();
			return false;
		}
		return true;
	}

	bool FSegment::Seal(ERecord Type, const uint8* Data, int64 Len)
	{
		if (!Impl->bOpen || Len < 0 || (Len > 0 && !Data)) { return false; }
		int64 Off = 0;
		do
		{
			const int64 Take = FMath::Min<int64>(Len - Off, kMaxRecordData);
			Impl->Plain.clear();
			Impl->Plain.push_back(static_cast<uint8_t>(Type));
			if (Take > 0) { Impl->Plain.insert(Impl->Plain.end(), Data + Off, Data + Off + Take); }
			if (Impl->Sess.Seal(Impl->Plain.data(), Impl->Plain.size(), Impl->Pending) != AsdaSecure::Err::Ok)
			{
				AsdaSecure::Wipe(Impl->Plain.data(), Impl->Plain.size());
				return false;
			}
			Off += Take;
		} while (Off < Len);
		AsdaSecure::Wipe(Impl->Plain.data(), Impl->Plain.size());
		return true;
	}

	bool FSegment::SealText(const FString& Text)
	{
		const FTCHARToUTF8 U(*Text);
		return Seal(ERecord::Text, reinterpret_cast<const uint8*>(U.Get()), U.Length());
	}

	bool FSegment::SealFileBegin(const FString& Name, uint64 Size)
	{
		const FTCHARToUTF8 U(*Name);
		const uint16 NameLen = static_cast<uint16>(FMath::Min<int32>(U.Length(), 1024));
		uint8 Buf[2 + 1024 + 8];
		FMemory::Memcpy(Buf, &NameLen, 2);
		FMemory::Memcpy(Buf + 2, U.Get(), NameLen);
		FMemory::Memcpy(Buf + 2 + NameLen, &Size, 8);
		return Seal(ERecord::FileBegin, Buf, 2 + NameLen + 8);
	}

	bool FSegment::SealFileEnd()
	{
		return Seal(ERecord::FileEnd, nullptr, 0);
	}

	bool FSegment::IsOpen() const { return Impl->bOpen; }

	void FSegment::End()
	{
		if (Impl->bOpen || Impl->Sess.IsReady()) { Impl->Sess.Reset(); }   // ключи затираются (Session::Reset → Wipe)
		Impl->bOpen = false;
	}

	const uint8* FSegment::PendingData() const { return Impl->Pending.data(); }
	int64 FSegment::PendingNum() const { return static_cast<int64>(Impl->Pending.size()); }
	void FSegment::ClearPending() { Impl->Pending.clear(); }

	// ── пакет файлов ────────────────────────────────────────────────────────────────────────────────────────────
	// ★ 19.10.2026 (ревизия безопасности журналов, В4): в пакет попадают только «свои» обычные файлы — без джанкшенов,
	//   симлинков и жёстких ссылок на пути, с пределом размера; чтение и удаление идут по одному и тому же дескриптору.
#if PLATFORM_WINDOWS
	// Настоящий путь объекта по дескриптору без префикса «\\?\» (для UNC — «\\сервер\…»). Пусто — не вышло.
	static FString FinalPathOf(HANDLE H)
	{
		TCHAR Buf[1024];
		const DWORD N = ::GetFinalPathNameByHandleW(H, Buf, UE_ARRAY_COUNT(Buf), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		if (N == 0 || N >= UE_ARRAY_COUNT(Buf)) { return FString(); }
		FString S(Buf);
		if (S.StartsWith(TEXT("\\\\?\\UNC\\"))) { S = TEXT("\\\\") + S.Mid(8); }
		else if (S.StartsWith(TEXT("\\\\?\\"))) { S = S.Mid(4); }
		return S;
	}

	// Заданный путь в виде для сравнения с настоящим: полный, «\», без завершающего «\».
	static FString NormalizedForCompare(const FString& Path)
	{
		FString S = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(S);
		S.ReplaceInline(TEXT("/"), TEXT("\\"));
		while (S.Len() > 3 && S.EndsWith(TEXT("\\"))) { S.LeftChopInline(1); }
		return S;
	}

	// ★ Настоящий путь, которым ОБЯЗАН быть объект Path внутри корня Root: настоящий путь самого Root (его предков —
	//   перенаправление профиля, виртуализация AppData пакетных приложений, джанкшен C:\Users — не трогаем) + тот же хвост,
	//   что у Path относительно Root. Пусто — Path не внутри Root, Root не открыт или сам Root — точка повторного разбора.
	static FString ExpectedRealPath(const FString& Path, const FString& Root)
	{
		const FString P = NormalizedForCompare(Path);
		const FString R = NormalizedForCompare(Root);
		FString Tail;
		if (!P.Equals(R, ESearchCase::IgnoreCase))
		{
			if (!P.StartsWith(R + TEXT("\\"), ESearchCase::IgnoreCase)) { return FString(); }
			Tail = P.Mid(R.Len());
		}
		HANDLE H = ::CreateFileW(*R, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (H == INVALID_HANDLE_VALUE) { return FString(); }
		BY_HANDLE_FILE_INFORMATION I = {};
		const bool bOk = ::GetFileInformationByHandle(H, &I) && (I.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			&& !(I.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
		const FString RootReal = bOk ? FinalPathOf(H) : FString();
		::CloseHandle(H);
		return RootReal.IsEmpty() ? FString() : RootReal + Tail;
	}
#endif

	bool IsPlainDirectory(const FString& Dir, const FString& Root)
	{
#if PLATFORM_WINDOWS
		const FString Want = ExpectedRealPath(Dir, Root);
		if (Want.IsEmpty()) { return false; }
		HANDLE H = ::CreateFileW(*NormalizedForCompare(Dir), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (H == INVALID_HANDLE_VALUE) { return false; }
		BY_HANDLE_FILE_INFORMATION I = {};
		const bool bInfo = ::GetFileInformationByHandle(H, &I) != 0;
		const FString Real = FinalPathOf(H);
		::CloseHandle(H);
		return bInfo && (I.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(I.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
			&& Real.Equals(Want, ESearchCase::IgnoreCase);
#else
		(void)Dir; (void)Root;
		return false;   // перенос/пакеты файлов — только Windows (другие платформы игра не собирает)
#endif
	}

	bool OpenOwnedFile(const FString& Path, const FString& Root, uint64 MaxBytes, FOwnedFile& Out, FString* OutWhy)
	{
		Out = FOwnedFile();
#if PLATFORM_WINDOWS
		auto Fail = [OutWhy](const TCHAR* Why) { if (OutWhy) { *OutWhy = Why; } return false; };
		const FString Want = ExpectedRealPath(Path, Root);
		if (Want.IsEmpty()) { return Fail(TEXT("вне корня или корень — точка повторного разбора")); }
		// Чтение + DELETE; совместно — только чтение (пишущий/удаляющий владелец → отказ открытия, файл не трогаем).
		HANDLE H = ::CreateFileW(*NormalizedForCompare(Path), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (H == INVALID_HANDLE_VALUE) { return Fail(TEXT("не открыт (занят или нет доступа)")); }
		BY_HANDLE_FILE_INFORMATION I = {};
		const bool bInfo = ::GetFileInformationByHandle(H, &I) != 0;
		const uint64 Size = (uint64(I.nFileSizeHigh) << 32) | I.nFileSizeLow;
		const TCHAR* Why = nullptr;
		if (!bInfo) { Why = TEXT("нет сведений о файле"); }
		else if (I.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { Why = TEXT("точка повторного разбора (симлинк/джанкшен)"); }
		else if (I.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) { Why = TEXT("не обычный файл"); }
		else if (::GetFileType(H) != FILE_TYPE_DISK) { Why = TEXT("не дисковый файл"); }
		else if (I.nNumberOfLinks != 1) { Why = TEXT("жёсткая ссылка (ссылок > 1)"); }
		else if (Size > MaxBytes) { Why = TEXT("больше предела пакета"); }
		else if (!FinalPathOf(H).Equals(Want, ESearchCase::IgnoreCase)) { Why = TEXT("настоящий путь другой (джанкшен/симлинк на пути)"); }
		if (Why) { ::CloseHandle(H); return Fail(Why); }
		Out.Handle = H;
		Out.Path = NormalizedForCompare(Path);
		Out.Size = Size;
		return true;
#else
		(void)Path; (void)Root; (void)MaxBytes;
		if (OutWhy) { *OutWhy = TEXT("только Windows"); }
		return false;
#endif
	}

	void CloseOwnedFile(FOwnedFile& File, bool bDelete)
	{
#if PLATFORM_WINDOWS
		if (!File.Handle) { return; }
		HANDLE H = static_cast<HANDLE>(File.Handle);
		if (bDelete)
		{
			// Удаление по дескриптору: только этот, проверенный объект. Сначала FileDispositionInfoEx (Windows 10 1809+: POSIX-удаление
			//   сразу освобождает имя + снимает «только чтение»), иначе — классический FileDispositionInfo. Константы — из
			//   документации winbase.h: в заголовках SDK движка они закрыты условием версии Windows.
			constexpr int kFileDispositionInfoEx = 21;   // FILE_INFO_BY_HANDLE_CLASS::FileDispositionInfoEx
			constexpr DWORD kFlagDelete = 0x1, kFlagPosix = 0x2, kFlagIgnoreReadOnly = 0x10;   // FILE_DISPOSITION_FLAG_*
			DWORD ExFlags = kFlagDelete | kFlagPosix | kFlagIgnoreReadOnly;   // FILE_DISPOSITION_INFO_EX { DWORD Flags; }
			if (!::SetFileInformationByHandle(H, static_cast<FILE_INFO_BY_HANDLE_CLASS>(kFileDispositionInfoEx), &ExFlags, sizeof(ExFlags)))
			{
				BOOLEAN DeleteIt = 1;   // FILE_DISPOSITION_INFO { BOOLEAN DeleteFile; }
				::SetFileInformationByHandle(H, FileDispositionInfo, &DeleteIt, sizeof(DeleteIt));
			}
		}
		::CloseHandle(H);
#else
		(void)bDelete;
#endif
		File.Handle = nullptr;
	}

	bool SealOwnedFilesToBundle(EKind Kind, const FString& OutPath, const TArray<FOwnedFile>& Files, const FString& BaseDir, const FString& Note)
	{
#if PLATFORM_WINDOWS
		const FString OutFull = NormalizedForCompare(OutPath);
		// Новый файл: существующий (в т. ч. подложенный) не перезаписывается и не открывается.
		HANDLE Out = ::CreateFileW(*OutFull, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (Out == INVALID_HANDLE_VALUE) { return false; }
		FSegment Seg;
		int64 Written = 0;
		// Записать накопленное и очистить; false — сбой записи.
		auto Flush = [&Seg, Out, &Written]() -> bool
		{
			const uint8* P = Seg.PendingData();
			int64 Left = Seg.PendingNum();
			while (Left > 0)
			{
				DWORD Wr = 0;
				if (!::WriteFile(Out, P, static_cast<DWORD>(FMath::Min<int64>(Left, 1 << 20)), &Wr, nullptr) || Wr == 0) { Seg.ClearPending(); return false; }
				P += Wr;
				Left -= Wr;
				Written += Wr;
			}
			Seg.ClearPending();
			return true;
		};
		bool bOk = Seg.Begin(Kind, MakeInfoText(Kind, FString::Printf(TEXT("files=%d\nnote=%s\n"), Files.Num(), *Note))) && Flush();
		TArray<uint8> Chunk;
		Chunk.SetNumUninitialized(kMaxRecordData);
		for (int32 I = 0; bOk && I < Files.Num(); ++I)
		{
			const FOwnedFile& F = Files[I];
			HANDLE In = static_cast<HANDLE>(F.Handle);
			// Имя в пакете — путь относительно BaseDir через «/» (вне BaseDir — только имя файла).
			const FString Base = NormalizedForCompare(BaseDir) + TEXT("\\");
			FString Rel = F.Path.StartsWith(Base, ESearchCase::IgnoreCase) ? F.Path.Mid(Base.Len()) : FPaths::GetCleanFilename(F.Path);
			Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
			LARGE_INTEGER Zero = {};
			bOk = In && ::SetFilePointerEx(In, Zero, nullptr, FILE_BEGIN) && Seg.SealFileBegin(Rel, F.Size) && Flush();
			for (uint64 Off = 0; bOk && Off < F.Size; Off += kMaxRecordData)
			{
				const DWORD Take = static_cast<DWORD>(FMath::Min<uint64>(F.Size - Off, kMaxRecordData));
				DWORD Rd = 0;
				bOk = ::ReadFile(In, Chunk.GetData(), Take, &Rd, nullptr) && Rd == Take && Seg.Seal(ERecord::FileData, Chunk.GetData(), Take) && Flush();
			}
			bOk = bOk && Seg.SealFileEnd() && Flush();
		}
		FMemory::Memzero(Chunk.GetData(), Chunk.Num());
		Seg.End();
		bOk = bOk && ::FlushFileBuffers(Out);
		LARGE_INTEGER OnDisk = {};
		bOk = bOk && ::GetFileSizeEx(Out, &OnDisk) && OnDisk.QuadPart == Written && Written > kSegmentHeaderLen;
		::CloseHandle(Out);
		if (!bOk) { ::DeleteFileW(*OutFull); }   // неполный пакет не оставляем (исходники тогда тоже не удаляются); файл создан нами
		return bOk;
#else
		(void)Kind; (void)OutPath; (void)Files; (void)BaseDir; (void)Note;
		return false;
#endif
	}

	bool SealFilesToBundle(EKind Kind, const FString& OutPath, const TArray<FString>& Files, const FString& BaseDir, const FString& Note)
	{
		// ★ 19.10.2026 (В4): те же правила, что у переноса старых файлов — только «свои» обычные файлы, сумма ≤ kMaxBundleSourceBytes;
		//   прочие пропускаются (не читаются). Исходники здесь не удаляются.
		TArray<FOwnedFile> Owned;
		uint64 Total = 0;
		for (const FString& P : Files)
		{
			FOwnedFile F;
			if (OpenOwnedFile(P, BaseDir, kMaxBundleSourceBytes - Total, F)) { Total += F.Size; Owned.Add(F); }
		}
		const bool bOk = Owned.Num() > 0 && SealOwnedFilesToBundle(Kind, OutPath, Owned, BaseDir, Note);
		for (FOwnedFile& F : Owned) { CloseOwnedFile(F, /*bDelete*/false); }
		return bOk;
	}
}
