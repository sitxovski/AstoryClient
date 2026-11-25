// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ запечатанных журналов: `asda.SelfTest.LogSeal dir=<папка> [quit]` (16.10.2026).
//   Офлайн, без сети. Пишет в <папка> образцы всех видов файлов и проверяет то, что видно БЕЗ закрытого ключа: заголовок
//   сегмента, keyId вшитого ключа, ClientHello журнала, цепочку кадров (seq 1..n, длины), отсутствие открытого текста,
//   разные эфемерные ключи у сегментов, перенос старых файлов (исходники удалены только после записи пакета), пакет падения
//   по синтетическому исключению. Расшифровку (закрытым ключом) и сверку содержимого делает
//   `python Tools/asda_logseal.py verify-selftest <папка>` — независимая реализация на Python (cryptography).
#include "CoreMinimal.h"

#if PLATFORM_WINDOWS && !UE_BUILD_SHIPPING

#include "AsdaLogSeal.h"
#include "LogSeal/AsdaLogSealPrivate.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Engine/Engine.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <winioctl.h>
#include "Windows/HideWindowsPlatformTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLogSealTest, Log, All);

namespace AsdaLogSealTest
{
	using namespace AsdaLogSeal;

	// Счётчик проверок.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaLogSealTest, Log, TEXT("ЖУРНАЛ-ПЕЧАТЬ ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaLogSealTest, Error, TEXT("ЖУРНАЛ-ПЕЧАТЬ FAIL %s"), *What); }
		}
	};

	// Разбор кадров сегмента без ключа: число кадров, seq по порядку, длины в пределах. Pos — начало сегмента.
	static bool WalkFrames(const TArray<uint8>& B, int32 Pos, int32& OutFrames, int32& OutNextSeg)
	{
		OutFrames = 0;
		int32 P = Pos + kSegmentHeaderLen;
		uint64 Expect = 1;
		while (P < B.Num())
		{
			if (B.Num() - P >= 4 && FMemory::Memcmp(B.GetData() + P, "ASLG", 4) == 0) { break; }
			if (B.Num() - P < 12) { return false; }
			uint32 Clen; uint64 Seq;
			FMemory::Memcpy(&Clen, B.GetData() + P, 4);
			FMemory::Memcpy(&Seq, B.GetData() + P + 4, 8);
			if (Clen < 16 || Clen > 64 * 1024 + 16 || Seq != Expect || B.Num() - P < 12 + (int32)Clen) { return false; }
			++Expect;
			++OutFrames;
			P += 12 + Clen;
		}
		OutNextSeg = P;
		return true;
	}

	// ★ 19.10.2026 (ревизия В4): джанкшен Link → Target (точка подключения, без прав администратора). false — не вышло.
	static bool MakeJunction(const FString& Link, const FString& Target)
	{
		if (!::CreateDirectoryW(*Link, nullptr)) { return false; }
		HANDLE H = ::CreateFileW(*Link, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (H == INVALID_HANDLE_VALUE) { return false; }
		FString Sub = TEXT("\\??\\") + FPaths::ConvertRelativePathToFull(Target).Replace(TEXT("/"), TEXT("\\"));
		const int32 SubBytes = Sub.Len() * sizeof(TCHAR);
		// REPARSE_DATA_BUFFER (MountPoint): тег, длина данных, резерв; смещения/длины имён; SubstituteName\0 PrintName(пусто)\0.
		TArray<uint8> Buf;
		Buf.SetNumZeroed(16 + SubBytes + 2 + 2);
		uint8* P = Buf.GetData();
		const uint32 Tag = IO_REPARSE_TAG_MOUNT_POINT;
		const uint16 DataLen = static_cast<uint16>(Buf.Num() - 8), SubOff = 0, SubLen = static_cast<uint16>(SubBytes);
		const uint16 PrnOff = static_cast<uint16>(SubBytes + 2), PrnLen = 0;
		FMemory::Memcpy(P + 0, &Tag, 4);
		FMemory::Memcpy(P + 4, &DataLen, 2);
		FMemory::Memcpy(P + 8, &SubOff, 2);
		FMemory::Memcpy(P + 10, &SubLen, 2);
		FMemory::Memcpy(P + 12, &PrnOff, 2);
		FMemory::Memcpy(P + 14, &PrnLen, 2);
		FMemory::Memcpy(P + 16, *Sub, SubBytes);
		DWORD R = 0;
		const bool bOk = ::DeviceIoControl(H, FSCTL_SET_REPARSE_POINT, Buf.GetData(), Buf.Num(), nullptr, 0, &R, nullptr) != 0;
		::CloseHandle(H);
		return bOk;
	}

	// Есть ли подстрока байт в массиве.
	static bool Contains(const TArray<uint8>& B, const char* Needle)
	{
		const int32 N = FCStringAnsi::Strlen(Needle);
		for (int32 I = 0; I + N <= B.Num(); ++I)
		{
			if (FMemory::Memcmp(B.GetData() + I, Needle, N) == 0) { return true; }
		}
		return false;
	}

	// Записать сегмент целиком в файл (перезапись).
	static bool WriteSegment(FSegment& Seg, const FString& Path, bool bAppend)
	{
		IPlatformFile& PF = IPlatformFile::GetPlatformPhysical();
		TUniquePtr<IFileHandle> H(PF.OpenWrite(*Path, bAppend, false));
		const bool bOk = H && H->Write(Seg.PendingData(), Seg.PendingNum());
		Seg.ClearPending();
		return bOk;
	}
}

static void AsdaLogSeal_SelfTest(const TArray<FString>& Args)
{
	using namespace AsdaLogSealTest;
	FRun R;
	bool bQuit = false;
	FString Dir;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		else if (A.StartsWith(TEXT("dir="))) { Dir = A.Mid(4).TrimQuotes(); }
	}
	if (Dir.IsEmpty()) { Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LogSealSelfTest"))); }
	IPlatformFile& PF = IPlatformFile::GetPlatformPhysical();
	PF.CreateDirectoryTree(*Dir);
	UE_LOG(LogAsdaLogSealTest, Log, TEXT("ЖУРНАЛ-ПЕЧАТЬ папка %s, IsActive=%d"), *Dir, AsdaLogSeal::IsActive() ? 1 : 0);

	// 1. Ключ вшит и восстанавливается (keyId сверяется внутри RestorePublicKey).
	R.Expect(HasKey(), FString::Printf(TEXT("открытый ключ восстановлен из долей, keyId %s"), *KeyIdHex()));

	// 2. Сегмент журнала: заголовок, ClientHello, кадры, нет открытого текста.
	const FString Marker1 = TEXT("LOGSEAL-SELFTEST-MARKER-1 строка журнала");
	const FString P1 = Dir / TEXT("journal.alog");
	PF.DeleteFile(*P1);
	{
		FSegment S;
		R.Expect(S.Begin(EKind::SelfTest, MakeInfoText(EKind::SelfTest, TEXT("case=journal"))), TEXT("Begin сегмента"));
		R.Expect(S.SealText(Marker1 + TEXT("\r\n")) && S.SealText(TEXT("LOGSEAL-SELFTEST-MARKER-2\r\n")), TEXT("две строки запечатаны"));
		R.Expect(WriteSegment(S, P1, false), TEXT("сегмент 1 записан"));
		S.End();
		R.Expect(S.Begin(EKind::SelfTest, MakeInfoText(EKind::SelfTest, TEXT("case=journal-run2"))) && S.SealText(TEXT("LOGSEAL-SELFTEST-MARKER-3\r\n")),
			TEXT("второй сегмент (второй «запуск») в тот же файл"));
		R.Expect(WriteSegment(S, P1, true), TEXT("сегмент 2 дописан"));
	}
	TArray<uint8> B;
	FFileHelper::LoadFileToArray(B, *P1);
	R.Expect(B.Num() > 2 * kSegmentHeaderLen && FMemory::Memcmp(B.GetData(), "ASLGSEG1", 8) == 0 && B[8] == 1 && B[9] == (uint8)EKind::SelfTest,
		TEXT("заголовок: ASLGSEG1, версия 1, вид SelfTest"));
	R.Expect(B.Num() > 24 && B[20] == 0xA5 && B[21] == 1 && B[22] == kLogSubServer, TEXT("ClientHello журнала: A5 01 4C"));
	R.Expect(B.Num() > 20 && BytesToHex(B.GetData() + 12, 8).ToLower() == KeyIdHex(), TEXT("keyId в заголовке == вшитый"));
	int32 Frames1 = 0, Next = 0, Frames2 = 0, End2 = 0;
	const bool bWalk1 = WalkFrames(B, 0, Frames1, Next);
	const bool bWalk2 = bWalk1 && Next < B.Num() && WalkFrames(B, Next, Frames2, End2);
	R.Expect(bWalk1 && Frames1 == 3 && bWalk2 && Frames2 == 2 && End2 == B.Num(), FString::Printf(TEXT("кадры: сегмент 1 — %d (ждали 3: сведения + 2 строки), сегмент 2 — %d (ждали 2), seq по порядку"), Frames1, Frames2));
	R.Expect(bWalk2 && FMemory::Memcmp(B.GetData() + 24, B.GetData() + Next + 24, 64) != 0, TEXT("у сегментов разные эфемерные ключи"));
	R.Expect(!Contains(B, "LOGSEAL-SELFTEST-MARKER") && !Contains(B, "case=journal") && !Contains(B, "kind="), TEXT("открытого текста в файле нет"));

	// 3. Большая запись режется на кадры ≤ 60 КБ.
	{
		const FString P2 = Dir / TEXT("big.alog");
		TArray<uint8> Big;
		Big.SetNumUninitialized(150000);
		for (int32 I = 0; I < Big.Num(); ++I) { Big[I] = (uint8)(I * 131 + 7); }
		FSegment S;
		const bool bOk = S.Begin(EKind::SelfTest, MakeInfoText(EKind::SelfTest, TEXT("case=big"))) && S.SealFileBegin(TEXT("big.bin"), Big.Num())
			&& S.Seal(ERecord::FileData, Big.GetData(), Big.Num()) && S.SealFileEnd() && WriteSegment(S, P2, false);
		TArray<uint8> C;
		FFileHelper::LoadFileToArray(C, *P2);
		int32 F = 0, E = 0;
		R.Expect(bOk && WalkFrames(C, 0, F, E) && F == 1 + 1 + 3 + 1, FString::Printf(TEXT("150 000 б → 3 кадра данных (всего кадров %d, ждали 6)"), F));
	}

	// 4. Чужой получатель (тестовый ключ из <папка>/test.pub.hex — его закрытая половина у verify-selftest).
	{
		FString Hex;
		if (FFileHelper::LoadFileToString(Hex, *(Dir / TEXT("test.pub.hex"))))
		{
			TArray<uint8> Pub;
			Pub.SetNumZeroed(64);
			const bool bHex = HexToBytes(Hex.TrimStartAndEnd(), Pub.GetData()) == 64;
			FSegment S;
			const bool bOk = bHex && S.Begin(EKind::SelfTest, MakeInfoText(EKind::SelfTest, TEXT("case=override")), Pub.GetData())
				&& S.SealText(TEXT("LOGSEAL-SELFTEST-MARKER-4 тестовый получатель\r\n")) && WriteSegment(S, Dir / TEXT("override.alog"), false);
			R.Expect(bOk, TEXT("сегмент для тестового получателя (test.pub.hex) записан"));
		}
		else
		{
			R.Expect(true, TEXT("test.pub.hex нет — случай тестового получателя пропущен (verify-selftest создаёт его заранее)"));
		}
	}

	// 5. Перенос старых открытых файлов: исходники удаляются только после записи пакетов.
	{
		const FString Saved = Dir / TEXT("sweep/Saved");
		const FString Raw = Dir / TEXT("sweep/Local");
		// ★ 19.10 (В4): сначала снять джанкшен прошлого прогона (иначе рекурсивное удаление ушло бы по нему).
		::RemoveDirectoryW(*FPaths::ConvertRelativePathToFull(Saved / TEXT("Crashes/JUNCTION_TEST")));
		PF.DeleteDirectoryRecursively(*(Dir / TEXT("sweep")));
		FFileHelper::SaveStringToFile(TEXT("LOGSEAL-SELFTEST-LEGACY-XML"), *(Saved / TEXT("Crashes/UECC-Windows-TEST_0000/CrashContext.runtime-xml")));
		FFileHelper::SaveStringToFile(TEXT("LOGSEAL-SELFTEST-LEGACY-DMP"), *(Saved / TEXT("Crashes/UECC-Windows-TEST_0000/UEMinidump.dmp")));
		FFileHelper::SaveStringToFile(TEXT("LOGSEAL-SELFTEST-LEGACY-LOG"), *(Saved / TEXT("Logs/AsdaEac.log")));
		FFileHelper::SaveStringToFile(TEXT("LOGSEAL-SELFTEST-LEGACY-RAW"), *(Raw / TEXT("AsdaEac_raw.log")));
		FFileHelper::SaveStringToFile(TEXT("не журнал — не трогать"), *(Saved / TEXT("Logs/keep.alog")));
		// ★ 19.10 (В4): чужие файлы за джанкшеном и жёсткой ссылкой — перенос их не читает и не удаляет.
		const FString Outside = Dir / TEXT("sweep/Outside");
		FFileHelper::SaveStringToFile(TEXT("LOGSEAL-SELFTEST-JUNCTION-VICTIM"), *(Outside / TEXT("victim/victim.log")));
		FFileHelper::SaveStringToFile(TEXT("LOGSEAL-SELFTEST-HARDLINK-VICTIM"), *(Outside / TEXT("secret.txt")));
		const bool bJunction = MakeJunction(FPaths::ConvertRelativePathToFull(Saved / TEXT("Crashes/JUNCTION_TEST")),
			FPaths::ConvertRelativePathToFull(Outside / TEXT("victim")));
		const bool bHardLink = ::CreateHardLinkW(*FPaths::ConvertRelativePathToFull(Saved / TEXT("Logs/hardlink.log")),
			*FPaths::ConvertRelativePathToFull(Outside / TEXT("secret.txt")), nullptr) != 0;
		R.Expect(bJunction && bHardLink, FString::Printf(TEXT("подложены джанкшен Crashes/JUNCTION_TEST → Outside/victim (%d) и жёсткая ссылка Logs/hardlink.log (%d)"),
			bJunction ? 1 : 0, bHardLink ? 1 : 0));
		const int32 N = SweepLegacyPlainFiles(Saved, Raw);
		TArray<FString> Acrash, Alog;
		PF.FindFilesRecursively(Acrash, *(Saved / TEXT("Crashes")), TEXT(".acrash"));
		PF.FindFilesRecursively(Alog, *(Saved / TEXT("Logs")), TEXT(".alog"));
		R.Expect(N == 2 && Acrash.Num() == 1 && Alog.Num() == 2, FString::Printf(TEXT("перенос: пакетов %d (ждали 2), .acrash %d, .alog %d (ждали 2: Legacy + keep)"), N, Acrash.Num(), Alog.Num()));
		R.Expect(!PF.DirectoryExists(*(Saved / TEXT("Crashes/UECC-Windows-TEST_0000"))) && !PF.FileExists(*(Saved / TEXT("Logs/AsdaEac.log")))
			&& !PF.FileExists(*(Raw / TEXT("AsdaEac_raw.log"))) && PF.FileExists(*(Saved / TEXT("Logs/keep.alog"))),
			TEXT("открытые исходники удалены, чужой .alog не тронут"));
		bool bPlain = false;
		for (const FString& F : Acrash) { TArray<uint8> X; FFileHelper::LoadFileToArray(X, *F); bPlain |= Contains(X, "LOGSEAL-SELFTEST-LEGACY"); }
		for (const FString& F : Alog) { TArray<uint8> X; FFileHelper::LoadFileToArray(X, *F); bPlain |= Contains(X, "LOGSEAL-SELFTEST-LEGACY"); }
		R.Expect(!bPlain, TEXT("в пакетах переноса нет открытого текста"));
		R.Expect(PF.FileExists(*(Outside / TEXT("victim/victim.log"))) && PF.FileExists(*(Outside / TEXT("secret.txt")))
			&& PF.FileExists(*(Saved / TEXT("Logs/hardlink.log"))) && PF.DirectoryExists(*(Saved / TEXT("Crashes/JUNCTION_TEST"))),
			TEXT("джанкшен и жёсткая ссылка не тронуты, файлы за ними целы"));
		// Сами пакеты расшифровывает verify-selftest (метки *-VICTIM в них быть не должно); здесь — что пакетов не больше прежнего.
		R.Expect(Acrash.Num() == 1, TEXT("за джанкшеном пакет падения не создан"));
		::RemoveDirectoryW(*FPaths::ConvertRelativePathToFull(Saved / TEXT("Crashes/JUNCTION_TEST")));   // снять джанкшен (цель остаётся)
	}

	// 6. Пакет падения по синтетическому исключению (нить записи ≠ упавшая; minidump в память).
	{
		const FString P6 = Dir / TEXT("crash_test.acrash");
		PF.DeleteFile(*P6);
		const FString Got = SelfTestWriteCrashBundle(P6);
		TArray<uint8> C;
		FFileHelper::LoadFileToArray(C, *P6);
		int32 F = 0, E = 0;
		R.Expect(!Got.IsEmpty() && C.Num() > 64 * 1024, FString::Printf(TEXT("пакет падения записан: %d б"), C.Num()));
		R.Expect(C.Num() > 0 && C[9] == (uint8)EKind::CrashBundle && WalkFrames(C, 0, F, E) && E == C.Num() && F >= 5,
			FString::Printf(TEXT("пакет падения: вид 4, кадров %d, цепочка цела"), F));
		R.Expect(!Contains(C, "MDMP") && !Contains(C, "E0A5DA7E") && !Contains(C, "summary"), TEXT("в пакете падения нет открытого minidump/сводки"));
	}

	// 7. Политика: вне Shipping без -AsdaLogSeal — выключено, с ним — включено.
	R.Expect(AsdaLogSeal::IsActive() == FParse::Param(FCommandLine::Get(), TEXT("AsdaLogSeal")), TEXT("IsActive вне Shipping = ключ -AsdaLogSeal"));

	// 8. ★ 19.10.2026 (журналы релиза): формат логина учётки и <файл сессии>.alog.meta.
	{
		R.Expect(IsValidAccountLogin(TEXT("GM_sitx.v2@mail-1")) && IsValidAccountLogin(FString::ChrN(32, TEXT('a'))),
			TEXT("логин: [A-Za-z0-9_.@-] длиной 1..32 принимается"));
		R.Expect(!IsValidAccountLogin(FString()) && !IsValidAccountLogin(FString::ChrN(33, TEXT('a'))) && !IsValidAccountLogin(TEXT("..\\x"))
			&& !IsValidAccountLogin(TEXT("a b")) && !IsValidAccountLogin(TEXT("a\"b")) && !IsValidAccountLogin(TEXT("логин")),
			TEXT("логин: пустой, длиннее 32, с \\ / пробелом / кавычкой / кириллицей — отклоняется"));
		const FString Session = SessionLogPath();
		if (IsActive() && !Session.IsEmpty())
		{
			const FString Meta = Session + TEXT(".meta");
			const FString Name = FPaths::GetCleanFilename(Session);
			R.Expect(FPaths::GetCleanFilename(FPaths::GetPath(Session)) == TEXT("Sessions") && Name.EndsWith(TEXT(".alog")) && Name.Len() >= 22
				&& Name[8] == TEXT('-') && Name[15] == TEXT('_'), FString::Printf(TEXT("файл сессии Logs/Sessions/<ГГГГММДД-ЧЧММСС>_<pid>.alog: %s"), *Name));
			FString J1, J2, J3;
			const bool b1 = SetSessionAccount(TEXT("selftest_a")) && FFileHelper::LoadFileToString(J1, *Meta);
			const bool b2 = SetSessionAccount(TEXT("selftest.b@x")) && FFileHelper::LoadFileToString(J2, *Meta);
			const bool b3 = !SetSessionAccount(TEXT("bad login")) && FFileHelper::LoadFileToString(J3, *Meta);
			R.Expect(b1 && J1 == FString::Printf(TEXT("{\"account\":\"selftest_a\",\"session\":\"%s\"}"), *Name), FString::Printf(TEXT("meta после входа: %s"), *J1));
			R.Expect(b2 && J2 == FString::Printf(TEXT("{\"account\":\"selftest.b@x\",\"session\":\"%s\"}"), *Name), TEXT("meta перезаписан при входе другой учёткой"));
			R.Expect(b3 && J3 == J2, TEXT("неверный логин meta не меняет"));
			PF.DeleteFile(*Meta);   // самотест не оставляет учётку в meta сессии
		}
		else
		{
			R.Expect(!SetSessionAccount(TEXT("selftest_a")), TEXT("политика выключена — meta не пишется"));
		}
	}

	UE_LOG(LogAsdaLogSealTest, Log, TEXT("ЖУРНАЛ-ПЕЧАТЬ ИТОГ: ok=%d fail=%d — %s"), R.Ok, R.Fail, R.Fail == 0 ? TEXT("ВСЁ СОШЛОСЬ") : TEXT("ЕСТЬ ПРОВАЛЫ"));
	if (bQuit && GEngine) { GEngine->Exec(nullptr, TEXT("quit")); }
}

// ★ 31.10.2026 САМОТЕСТ ротации журнала сессии: `asda.SelfTest.LogSealRotate dir=<папка> [quit]`. Офлайн. Тот же класс устройства,
//   что у GLog, пишет три сессии в <папка>/rot_{on,plain,off}/Sessions: со сжатием и ротацией (~60 МиБ строк — 8+ частей,
//   хранятся 6), с ротацией без сжатия (~10 МиБ), прежнее поведение (ключ 0, ~8 МиБ). Здесь — что видно без ключа (число и
//   размер частей ≤ 4 МиБ, цепочки кадров, meta, нет открытого текста, пакет падения); расшифровку, непрерывность строк и
//   склейку проверяет `python Tools/asda_logseal.py verify-rotation <папка>` (итоги прогона — <папка>/rot_result.txt).
static void AsdaLogSeal_SelfTestRotate(const TArray<FString>& Args)
{
	using namespace AsdaLogSealTest;
	FRun R;
	bool bQuit = false;
	FString Dir;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		else if (A.StartsWith(TEXT("dir="))) { Dir = A.Mid(4).TrimQuotes(); }
	}
	if (Dir.IsEmpty()) { Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LogSealRotateSelfTest"))); }
	IPlatformFile& PF = IPlatformFile::GetPlatformPhysical();
	PF.CreateDirectoryTree(*Dir);
	// Тестовый получатель (закрытая половина — у verify-rotation); нет файла — вшитый ключ.
	TArray<uint8> Pub;
	FString Hex;
	if (FFileHelper::LoadFileToString(Hex, *(Dir / TEXT("test.pub.hex"))))
	{
		Pub.SetNumZeroed(64);
		if (HexToBytes(Hex.TrimStartAndEnd(), Pub.GetData()) != 64) { Pub.Reset(); }
	}
	UE_LOG(LogAsdaLogSealTest, Log, TEXT("ЖУРНАЛ-РОТАЦИЯ папка %s, получатель %s"), *Dir, Pub.Num() == 64 ? TEXT("тестовый") : TEXT("вшитый"));
	constexpr int64 kMiB = 1024 * 1024;
	constexpr int64 kReceiverMax = 4 * kMiB;   // предел приёмника LogDrop на файл
	FString Result;

	// Проверки одного прогона: файлы, размеры, кадры, открытый текст, meta.
	auto CheckFiles = [&R, &PF](const TCHAR* Case, const AsdaLogSeal::FSelfTestRotationResult& Res, const FString& Account)
	{
		bool bSize = Res.Files.Num() > 0, bFrames = true, bPlain = false, bMeta = true;
		FString Sizes;
		for (const FString& F : Res.Files)
		{
			TArray<uint8> B;
			FFileHelper::LoadFileToArray(B, *F);
			int32 Frames = 0, Next = 0;
			bSize &= B.Num() > 0 && B.Num() <= kReceiverMax;
			bFrames &= B.Num() > kSegmentHeaderLen && FMemory::Memcmp(B.GetData(), "ASLGSEG1", 8) == 0 && B[9] == (uint8)EKind::EngineLog
				&& WalkFrames(B, 0, Frames, Next) && Next == B.Num() && Frames >= 2;
			bPlain |= Contains(B, "ROT-LINE") || Contains(B, "session_id");
			FString J;
			if (!Account.IsEmpty())
			{
				bMeta &= FFileHelper::LoadFileToString(J, *(F + TEXT(".meta")))
					&& J == FString::Printf(TEXT("{\"account\":\"%s\",\"session\":\"%s\"}"), *Account, *FPaths::GetCleanFilename(F));
			}
			Sizes += FString::Printf(TEXT(" %s=%d"), *FPaths::GetCleanFilename(F), B.Num());
		}
		R.Expect(bSize, FString::Printf(TEXT("%s: каждый файл ≤ 4 МиБ (предел приёмника):%s"), Case, *Sizes));
		R.Expect(bFrames, FString::Printf(TEXT("%s: каждый файл — один сегмент журнала движка (вид 3), цепочка кадров цела до конца"), Case));
		R.Expect(!bPlain, FString::Printf(TEXT("%s: открытого текста в файлах нет"), Case));
		if (!Account.IsEmpty()) { R.Expect(bMeta, FString::Printf(TEXT("%s: у каждой части свой .alog.meta с учёткой"), Case)); }
	};
	// Строка итогов прогона для verify-rotation.
	auto Note = [&Result](const TCHAR* Case, const AsdaLogSeal::FSelfTestRotationResult& Res)
	{
		Result += FString::Printf(TEXT("%s.lines=%lld\n%s.parts=%d\n%s.dropped=%d\n%s.files="), Case, (long long)Res.LinesLogged, Case, Res.Parts, Case, Res.Dropped, Case);
		for (int32 I = 0; I < Res.Files.Num(); ++I) { Result += (I ? TEXT(",") : TEXT("")) + FPaths::GetCleanFilename(Res.Files[I]); }
		Result += TEXT("\n");
		if (!Res.CrashBundle.IsEmpty()) { Result += FString::Printf(TEXT("%s.crash=%s\n"), Case, *FPaths::GetCleanFilename(Res.CrashBundle)); }
	};
	const uint8* PubPtr = Pub.Num() == 64 ? Pub.GetData() : nullptr;
	const FString Stem = TEXT("20261031-120000_4242");

	// 1. Ротация + сжатие: ~60 МиБ строк Log → 8+ частей, хранятся первая и 5 последних; пакет падения с хвостом.
	{
		const FString D = Dir / TEXT("rot_on/Sessions");
		PF.DeleteDirectoryRecursively(*(Dir / TEXT("rot_on")));
		PF.CreateDirectoryTree(*D);
		// Прошлая сессия с 9 частями (как после смены предела) — при открытии новой подрезается до первой и 5 последних.
		const FString Old = TEXT("20261030-100000_1111");
		for (int32 I = 1; I <= 9; ++I)
		{
			FFileHelper::SaveStringToFile(TEXT("old"), *(D / (I == 1 ? Old + TEXT(".alog") : FString::Printf(TEXT("%s_part%d.alog"), *Old, I))));
		}
		const double T0 = FPlatformTime::Seconds();
		const FSelfTestRotationResult Res = SelfTestSessionRotation(D / (Stem + TEXT(".alog")), PubPtr, 60 * kMiB, 1, 1, TEXT("rot_tester"),
			Dir / TEXT("rot_on/crash_rot.acrash"));
		UE_LOG(LogAsdaLogSealTest, Log, TEXT("ЖУРНАЛ-РОТАЦИЯ rot_on: строк %lld, частей открыто %d, файлов %d, %.1f с"), (long long)Res.LinesLogged, Res.Parts,
			Res.Files.Num(), FPlatformTime::Seconds() - T0);
		R.Expect(Res.Parts >= 8, FString::Printf(TEXT("rot_on: открыто частей %d (ждали ≥ 8 — больше, чем хранится, средние удалены)"), Res.Parts));
		bool bNames = Res.Files.Num() == 6 && FPaths::GetCleanFilename(Res.Files[0]) == Stem + TEXT(".alog");
		for (int32 I = 1; bNames && I < Res.Files.Num(); ++I)
		{
			bNames = FPaths::GetCleanFilename(Res.Files[I]) == FString::Printf(TEXT("%s_part%d.alog"), *Stem, Res.Parts - (Res.Files.Num() - 1) + I);
		}
		R.Expect(bNames, FString::Printf(TEXT("rot_on: хранится 6 файлов — первая часть и части %d..%d (файлов %d)"), Res.Parts - 4, Res.Parts, Res.Files.Num()));
		R.Expect(Res.Dropped == 0, FString::Printf(TEXT("rot_on: строк Log не отброшено (%d)"), Res.Dropped));
		CheckFiles(TEXT("rot_on"), Res, TEXT("rot_tester"));
		TArray<uint8> C;
		FFileHelper::LoadFileToArray(C, *Res.CrashBundle);
		int32 F = 0, E = 0;
		R.Expect(!Res.CrashBundle.IsEmpty() && C.Num() > 0 && C[9] == (uint8)EKind::CrashBundle && WalkFrames(C, 0, F, E) && E == C.Num()
			&& !Contains(C, "ROT-L"), FString::Printf(TEXT("rot_on: пакет падения с хвостом записан (%d б), открытого текста нет"), C.Num()));
		Note(TEXT("rot_on"), Res);
		bool bOld = PF.FileExists(*(D / (Old + TEXT(".alog"))));
		for (int32 I = 2; I <= 9; ++I) { bOld &= PF.FileExists(*(D / FString::Printf(TEXT("%s_part%d.alog"), *Old, I))) == (I >= 5); }
		R.Expect(bOld, TEXT("rot_on: прошлая сессия из 9 частей подрезана при запуске до части 1 и 5..9"));
	}
	// 1б. Сбой записи («нет места»): на строке 20000 две записи подряд срываются (половина байт + ошибка) — оборванная часть
	//   читается до обрыва, первая попытка новой части тоже срывается (файл удаляется), через ≤ 1 с открывается новая часть,
	//   и всё записанное после восстановления выгружается и читается (цепочка seq не рвётся).
	{
		const FString D = Dir / TEXT("rot_fail/Sessions");
		PF.DeleteDirectoryRecursively(*(Dir / TEXT("rot_fail")));
		PF.CreateDirectoryTree(*D);
		const FSelfTestRotationResult Res = SelfTestSessionRotation(D / (Stem + TEXT(".alog")), PubPtr, 6 * kMiB, 1, 1, FString(), FString(), 20000, 2);
		R.Expect(Res.WriteFailures == 1 && Res.Parts >= 2 && Res.Files.Num() == Res.Parts && Res.Dropped > 0,
			FString::Printf(TEXT("rot_fail: сбой записи учтён (%d), после него новая часть (частей %d, файлов %d), отброшено за время сбоя %d"),
				Res.WriteFailures, Res.Parts, Res.Files.Num(), Res.Dropped));
		bool bSize = Res.Files.Num() > 0, bTail = true;
		for (int32 I = 0; I < Res.Files.Num(); ++I)
		{
			TArray<uint8> B;
			FFileHelper::LoadFileToArray(B, *Res.Files[I]);
			int32 Frames = 0, Next = 0;
			bSize &= B.Num() > 0 && B.Num() <= kReceiverMax;
			if (I > 0) { bTail &= WalkFrames(B, 0, Frames, Next) && Next == B.Num(); }   // после восстановления цепочки целы
		}
		R.Expect(bSize && bTail, TEXT("rot_fail: файлы ≤ 4 МиБ, у частей после сбоя цепочка кадров цела до конца"));
		Note(TEXT("rot_fail"), Res);
	}
	// 2. Ротация без сжатия: ~10 МиБ → ≥ 3 части, все хранятся.
	{
		const FString D = Dir / TEXT("rot_plain/Sessions");
		PF.DeleteDirectoryRecursively(*(Dir / TEXT("rot_plain")));
		PF.CreateDirectoryTree(*D);
		const FSelfTestRotationResult Res = SelfTestSessionRotation(D / (Stem + TEXT(".alog")), PubPtr, 10 * kMiB, 1, 0, FString(), FString());
		R.Expect(Res.Parts >= 3 && Res.Files.Num() == Res.Parts && Res.Dropped == 0,
			FString::Printf(TEXT("rot_plain: частей %d (ждали ≥ 3), файлов %d, отброшено %d"), Res.Parts, Res.Files.Num(), Res.Dropped));
		CheckFiles(TEXT("rot_plain"), Res, FString());
		Note(TEXT("rot_plain"), Res);
	}
	// 3. Ключ 0 — прежнее поведение: один файл, после 3,5 МиБ только Warning, на ~3,94 МиБ остановка.
	{
		const FString D = Dir / TEXT("rot_off/Sessions");
		PF.DeleteDirectoryRecursively(*(Dir / TEXT("rot_off")));
		PF.CreateDirectoryTree(*D);
		const FSelfTestRotationResult Res = SelfTestSessionRotation(D / (Stem + TEXT(".alog")), PubPtr, 8 * kMiB, 0, 0, FString(), FString());
		const int64 Size = Res.Files.Num() == 1 ? PF.FileSize(*Res.Files[0]) : -1;
		R.Expect(Res.Files.Num() == 1 && Res.Parts == 1 && Res.Dropped > 0 && Size > 3 * kMiB + 512 * 1024 && Size <= 4 * kMiB - 64 * 1024 + 4096,
			FString::Printf(TEXT("rot_off (ключ 0): один файл %lld б, частей %d, отброшено строк %d — прежняя обрезка"), (long long)Size, Res.Parts, Res.Dropped));
		CheckFiles(TEXT("rot_off"), Res, FString());
		Note(TEXT("rot_off"), Res);
	}
	R.Expect(FFileHelper::SaveStringToFile(Result, *(Dir / TEXT("rot_result.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
		TEXT("итоги прогона записаны в rot_result.txt"));
	UE_LOG(LogAsdaLogSealTest, Log, TEXT("ЖУРНАЛ-РОТАЦИЯ ИТОГ: ok=%d fail=%d — %s"), R.Ok, R.Fail, R.Fail == 0 ? TEXT("ВСЁ СОШЛОСЬ") : TEXT("ЕСТЬ ПРОВАЛЫ"));
	if (bQuit && GEngine) { GEngine->Exec(nullptr, TEXT("quit")); }
}

static FAutoConsoleCommand GAsdaSelfTestLogSealRotate(
	TEXT("asda.SelfTest.LogSealRotate"),
	TEXT("asda.SelfTest.LogSealRotate dir=<папка> [quit] — ротация журнала сессии: части ≤ 4 МиБ, хранение 6, сжатие, хвост в пакете падения, ключ 0 = прежняя обрезка. Расшифровка — python Tools/asda_logseal.py verify-rotation <папка>."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaLogSeal_SelfTestRotate));

static FAutoConsoleCommand GAsdaSelfTestLogSeal(
	TEXT("asda.SelfTest.LogSeal"),
	TEXT("asda.SelfTest.LogSeal dir=<папка> [quit] — запечатанные журналы: сегменты, перенос старых файлов, пакет падения. Расшифровка — python Tools/asda_logseal.py verify-selftest <папка>."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaLogSeal_SelfTest));

#endif // PLATFORM_WINDOWS && !UE_BUILD_SHIPPING
