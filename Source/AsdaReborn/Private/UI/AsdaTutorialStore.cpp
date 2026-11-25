// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTutorialStore.cpp — ОБУЧЕНИЕ: ЛОКАЛЬНЫЙ ПРОГРЕСС НА ПЕРСОНАЖА. [ИСПРАВЛЕНИЕ] (решение владельца 10.11.2026)
//   В оригинале гид 1950 хранения не имеет (каждый вход на уровне 1 — с шага 1), а «Закрыть» книги ставит бит только
//   в памяти до перезахода (TUTORIAL.md §5). Порт хранит шаг гида, «гид пройден/закрыт» и отметки разделов книги
//   в файле персонажа. Маска сервера (6185) не трогается: раздел пройден = бит сервера ИЛИ локальный бит; «КОНЕЦ»
//   по-прежнему шлёт 6184 (опыт за раздел — только за полное прочтение, как в оригинале).
//
// ГДЕ ФАЙЛ: `FPaths::ProjectSavedDir()/AsdaTutorial/<имя>.atut`. У выпуска это `%LOCALAPPDATA%\AsdaReborn\Saved`
//   (`Launcher/src/Config.h` kGameSavedSubdir) — вне каталога установки, патч лаунчера его не пишет и не чистит;
//   выгрузка журналов лаунчера берёт только `Saved\Logs`/падения. Имя = 20 hex-символов HMAC от привязки — по имени
//   не узнать charId и не подложить файл другого персонажа.
//
// ПРИВЯЗКА: «учётка (без крайних пробелов, нижний регистр) | индекс сервера (1012) | charId (i32)». Хост логин-сервера
//   в привязку НЕ входит (смена домена сбросила бы прогресс всем); канал — тоже: каналы одного сервера делят одну БД
//   персонажей (charId тот же). userNum (i16 сессии) не используется — он меняется каждый вход (закон charid-vs-usernum).
//
// ФОРМАТ (LE): "ATU1"(4) | nonce(16) | шифротекст(48) | метка HMAC-SHA1(20) = 88 байт.
//   Открытый текст 48 б: u32 версия=1 | i32 charId | u8 шаг гида | u8 флаги (бит0 — гид закрыт) | u16 0 | маска 32 б | u32 случайное заполнение.
//   Ключи: K = HMAC-SHA1(секрет, "AsdaTutorial/v1|" + привязка); Kenc = HMAC(K, "enc"), Kmac = HMAC(K, "mac").
//   Шифр — поток HMAC-SHA1(Kenc, nonce ‖ u32 блок) (режим счётчика), метка — HMAC(Kmac, "ATU1" ‖ nonce ‖ шифротекст)
//   (шифр-затем-подпись). Проверка: длина, сигнатура, метка (сравнение без раннего выхода), версия, charId внутри.
//   Подделка/чужой файл (длина, сигнатура, метка, charId) — состояние с нуля, предупреждение, файл перезапишется при
//   следующем изменении. Файл НЕ ЧИТАЕТСЯ (занят антивирусом/облаком) или он от БОЛЕЕ НОВОГО клиента (версия выше) —
//   тоже с нуля, но запись до следующего входа запрещена (законный файл не затирается). Падений нет.
//   Запись: `.tmp` → `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` (атомарная замена на NTFS); основного файла нет, а
//   `.tmp` есть (обрыв между записью и переносом) — читается подписанный `.tmp`.
//   ★ Честная граница: секрет живёт в клиенте — защита от правки/копирования файла игроком, не от разбора exe.
//     Подделка прогресса даёт только «не показывать подсказку»: опыт за раздел выдаёт сервер по 6184, не этот файл.
#include "UI/AsdaTutorialSubsystem.h"

#include "AsdaDevConsole.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTutorialStore, Log, All);

// 1 — прогресс обучения хранится локально на персонажа [ИСПРАВЛЕНИЕ]; 0 — как оригинал (гид каждый вход с шага 1,
//   «Закрыть» книги — до перезахода). Ключ разработчика, не пользовательская настройка.
static ASDA_DEV_CVAR(int32, CVarTutorialLocalProgress, TEXT("asda.Tutorial.LocalProgress"), 1,
	TEXT("1 — шаг гида, «гид закрыт» и отметки разделов книги обучения хранятся локально на персонажа (Saved/AsdaTutorial, ")
	TEXT("шифр + подпись, привязка учётка/сервер/charId) [ИСПРАВЛЕНИЕ]; 0 — как оригинал."),
	ECVF_Default);

namespace AsdaTutorialStore
{
	constexpr uint8 kMagic[4] = { 'A', 'T', 'U', '1' };   // сигнатура файла
	constexpr int32 kNonce = 16;                           // случайность записи
	constexpr int32 kPlain = 48;                           // открытый текст
	constexpr int32 kTag = 20;                             // HMAC-SHA1
	constexpr int32 kFile = 4 + kNonce + kPlain + kTag;    // 88 байт
	constexpr uint32 kVersion = 1;                         // версия открытого текста
	// Секрет хранилища (32 б). Меняется только вместе с kMagic — иначе все файлы игроков тихо сбросятся.
	static const uint8 kSecret[32] = {
		0x5b, 0x1e, 0xc7, 0x92, 0x3a, 0xf4, 0x08, 0x6d, 0xe1, 0x77, 0x2c, 0xb9, 0x40, 0x95, 0xd3, 0x1f,
		0x86, 0x0a, 0x6e, 0xf1, 0x29, 0xbc, 0x54, 0x73, 0xc8, 0x3d, 0x9f, 0x12, 0xa6, 0x4b, 0xe0, 0x37 };

	// HMAC-SHA1 строки UTF-8.
	static void Hmac(const uint8* Key, int32 KeyLen, const FString& Text, uint8 Out[20])
	{
		const FTCHARToUTF8 U(*Text);
		FSHA1::HMACBuffer(Key, KeyLen, U.Get(), U.Length(), Out);
	}

	// Ключи шифра и подписи для привязки.
	static void DeriveKeys(const FString& Binding, uint8 Enc[20], uint8 Mac[20])
	{
		uint8 K[20];
		Hmac(kSecret, sizeof(kSecret), TEXT("AsdaTutorial/v1|") + Binding, K);
		Hmac(K, 20, TEXT("enc"), Enc);
		Hmac(K, 20, TEXT("mac"), Mac);
		FMemory::Memzero(K, sizeof(K));
	}

	// Поток шифра (режим счётчика): XOR in-place.
	static void Crypt(const uint8 Enc[20], const uint8* Nonce, uint8* Data, int32 Len)
	{
		uint8 Block[kNonce + 4];
		FMemory::Memcpy(Block, Nonce, kNonce);
		for (int32 Off = 0, Ctr = 0; Off < Len; Off += 20, ++Ctr)
		{
			const uint32 C = (uint32)Ctr;
			FMemory::Memcpy(Block + kNonce, &C, 4);
			uint8 Ks[20];
			FSHA1::HMACBuffer(Enc, 20, Block, sizeof(Block), Ks);
			for (int32 I = 0; I < 20 && Off + I < Len; ++I) { Data[Off + I] ^= Ks[I]; }
		}
	}

	// Метка файла: HMAC(Kmac, сигнатура ‖ nonce ‖ шифротекст).
	static void Tag(const uint8 Mac[20], const uint8* File, uint8 Out[20])
	{
		FSHA1::HMACBuffer(Mac, 20, File, 4 + kNonce + kPlain, Out);
	}

	// Сравнение без раннего выхода.
	static bool SameTag(const uint8* A, const uint8* B)
	{
		uint8 D = 0;
		for (int32 I = 0; I < kTag; ++I) { D |= (uint8)(A[I] ^ B[I]); }
		return D == 0;
	}
}

FString UAsdaTutorialSubsystem::MakeStoreBinding() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return FString(); }
	const int32 CharId = AsdaSelf::GetCharId(Net);
	if (CharId == 0) { return FString(); }   // персонаж не выбран — хранить не к чему
	return FString::Printf(TEXT("%s|%d|%d"), *Net->PendingSessionAccount.TrimStartAndEnd().ToLower(), Net->SelectedServerIndex, CharId);
}

FString UAsdaTutorialSubsystem::StorePathFor(const FString& Binding) const
{
	uint8 H[20];
	AsdaTutorialStore::Hmac(AsdaTutorialStore::kSecret, sizeof(AsdaTutorialStore::kSecret), TEXT("name|") + Binding, H);
	const FString Dir = StoreDirOverride.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("AsdaTutorial") : StoreDirOverride;
	return Dir / (BytesToHex(H, 10).ToLower() + TEXT(".atut"));
}

bool UAsdaTutorialSubsystem::EnsureProgressLoaded()
{
	using namespace AsdaTutorialStore;
	if (CVarTutorialLocalProgress.GetValueOnGameThread() == 0) { return false; }
	const FString Binding = MakeStoreBinding();
	if (Binding.IsEmpty()) { return false; }
	if (Binding == StoreBinding) { return true; }
	// Новая привязка (первый вход персонажа в этой сессии): состояние гида и отметки — с нуля, затем файл. Шаг гида
	//   прежнего персонажа (или прогресс без привязки, charId 0) не наследуется.
	GuideStep = 1;
	GuideShownStep = 0;
	FMemory::Memzero(GuideFlags, sizeof(GuideFlags));
	StoreBinding = Binding;
	FMemory::Memzero(LocalMask, sizeof(LocalMask));
	bGuideDismissed = false;
	bSavedSnapshotValid = false;
	bStoreReadOnly = false;
	FString Path = StorePathFor(Binding);
	const int32 CharId = AsdaSelf::GetCharId(GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>());
	TArray<uint8> Raw;
	if (!IFileManager::Get().FileExists(*Path))
	{
		// Обрыв между записью `.tmp` и переносом: основного нет — берём подписанный `.tmp` (проверки те же).
		if (IFileManager::Get().FileExists(*(Path + TEXT(".tmp")))) { Path += TEXT(".tmp"); }
		else
		{
			UE_LOG(LogAsdaTutorialStore, Log, TEXT("[обучение] локального прогресса персонажа charId=%d нет — с нуля"), CharId);
			return true;
		}
	}
	const TCHAR* Why = nullptr;
	uint8 Plain[kPlain];
	const int64 Size = IFileManager::Get().FileSize(*Path);
	if (Size >= 0 && Size != kFile) { Why = TEXT("длина"); }   // до чтения: файл чужого размера целиком не грузим
	else if (!FFileHelper::LoadFileToArray(Raw, *Path)) { Why = TEXT("не читается"); bStoreReadOnly = true; }
	else if (Raw.Num() != kFile) { Why = TEXT("длина"); }
	else if (FMemory::Memcmp(Raw.GetData(), kMagic, 4) != 0) { Why = TEXT("сигнатура"); }
	else
	{
		uint8 Enc[20], Mac[20], T[20];
		DeriveKeys(Binding, Enc, Mac);
		Tag(Mac, Raw.GetData(), T);
		if (!SameTag(T, Raw.GetData() + 4 + kNonce + kPlain)) { Why = TEXT("подпись (правка файла или чужой персонаж/учётка)"); }
		else
		{
			FMemory::Memcpy(Plain, Raw.GetData() + 4 + kNonce, kPlain);
			Crypt(Enc, Raw.GetData() + 4, Plain, kPlain);
			uint32 Ver; int32 FileChar;
			FMemory::Memcpy(&Ver, Plain + 0, 4);
			FMemory::Memcpy(&FileChar, Plain + 4, 4);
			if (Ver > kVersion) { Why = TEXT("версия новее клиента"); bStoreReadOnly = true; }
			else if (Ver != kVersion) { Why = TEXT("версия"); }
			else if (FileChar != CharId) { Why = TEXT("charId"); }
		}
		FMemory::Memzero(Enc, sizeof(Enc)); FMemory::Memzero(Mac, sizeof(Mac));
	}
	if (Why)
	{
		// Тихий сброс: игрок видит обучение с начала; файл перезапишется при первом изменении (кроме «только чтение»).
		UE_LOG(LogAsdaTutorialStore, Warning, TEXT("[обучение] локальный прогресс charId=%d отвергнут (%s) — с нуля%s"), CharId, Why,
			bStoreReadOnly ? TEXT(", запись до следующего входа запрещена") : TEXT(""));
		return true;
	}
	const int32 Step = Plain[8];
	if (Step >= 1 && Step <= 23) { GuideStep = Step; GuideShownStep = 0; }
	bGuideDismissed = (Plain[9] & 1) != 0;
	FMemory::Memcpy(LocalMask, Plain + 12, 32);
	SavedSnapshot[0] = Plain[8]; SavedSnapshot[1] = Plain[9] & 1;
	FMemory::Memcpy(SavedSnapshot + 2, LocalMask, 32);
	bSavedSnapshotValid = true;
	int32 Marks = 0;
	for (int32 I = 0; I < 256; ++I) { Marks += (LocalMask[I >> 3] >> (I & 7)) & 1; }
	UE_LOG(LogAsdaTutorialStore, Log, TEXT("[обучение] локальный прогресс charId=%d: шаг гида %d%s, отмечено разделов %d"),
		CharId, GuideStep, bGuideDismissed ? TEXT(" (гид закрыт)") : TEXT(""), Marks);
	return true;
}

void UAsdaTutorialSubsystem::SaveProgress(const TCHAR* Why)
{
	using namespace AsdaTutorialStore;
	if (CVarTutorialLocalProgress.GetValueOnGameThread() == 0) { return; }
	if (!EnsureProgressLoaded()) { return; }   // персонаж не выбран — некуда
	if (bStoreReadOnly) { return; }            // файл не прочитан по сбою/от нового клиента — не затирать
	uint8 Snap[34];
	Snap[0] = (uint8)FMath::Clamp(GuideStep, 1, 23);
	Snap[1] = bGuideDismissed ? 1 : 0;
	FMemory::Memcpy(Snap + 2, LocalMask, 32);
	if (bSavedSnapshotValid && FMemory::Memcmp(Snap, SavedSnapshot, sizeof(Snap)) == 0) { return; }
	const int32 CharId = AsdaSelf::GetCharId(GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>());
	uint8 File[kFile];
	FMemory::Memcpy(File, kMagic, 4);
	// nonce: GUID + время — уникален на запись (повтор nonce при том же ключе раскрыл бы XOR двух записей).
	const FGuid G = FGuid::NewGuid();
	const uint32 Gw[4] = { G.A, G.B, G.C, G.D ^ (uint32)FPlatformTime::Cycles() };
	FMemory::Memcpy(File + 4, Gw, kNonce);
	uint8* Plain = File + 4 + kNonce;
	FMemory::Memzero(Plain, kPlain);
	const uint32 Ver = kVersion;
	FMemory::Memcpy(Plain + 0, &Ver, 4);
	FMemory::Memcpy(Plain + 4, &CharId, 4);
	Plain[8] = Snap[0];
	Plain[9] = Snap[1];
	FMemory::Memcpy(Plain + 12, LocalMask, 32);
	const uint32 Fill = (uint32)FMath::Rand() ^ G.B;   // случайное заполнение хвоста (не счётчик, при чтении не проверяется)
	FMemory::Memcpy(Plain + 44, &Fill, 4);
	uint8 Enc[20], Mac[20];
	DeriveKeys(StoreBinding, Enc, Mac);
	Crypt(Enc, File + 4, Plain, kPlain);
	Tag(Mac, File, File + 4 + kNonce + kPlain);
	FMemory::Memzero(Enc, sizeof(Enc)); FMemory::Memzero(Mac, sizeof(Mac));
	// Запись через временный файл и атомарную замену: оборванная запись не оставит ни полфайла, ни пустоты
	//   (`IFileManager::Move` сначала удаляет цель — между удалением и переносом файла не было бы вовсе).
	const FString Path = StorePathFor(StoreBinding);
	const FString Tmp = Path + TEXT(".tmp");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
	TArray<uint8> Out(File, kFile);
	bool bOk = FFileHelper::SaveArrayToFile(Out, *Tmp);
#if PLATFORM_WINDOWS
	bOk = bOk && ::MoveFileExW(*FPaths::ConvertRelativePathToFull(Tmp), *FPaths::ConvertRelativePathToFull(Path),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	bOk = bOk && IFileManager::Get().Move(*Path, *Tmp, /*Replace=*/true);
#endif
	if (bOk) { FMemory::Memcpy(SavedSnapshot, Snap, sizeof(Snap)); bSavedSnapshotValid = true; }
	UE_LOG(LogAsdaTutorialStore, Log, TEXT("[обучение] локальный прогресс charId=%d записан (%s): шаг %d%s -> %s"),
		CharId, Why, (int32)Snap[0], bGuideDismissed ? TEXT(", гид закрыт") : TEXT(""), bOk ? TEXT("OK") : TEXT("ОШИБКА"));
}

void UAsdaTutorialSubsystem::GuideDismiss(const TCHAR* Why)
{
	CloseGuide(Why);
	if (CVarTutorialLocalProgress.GetValueOnGameThread() == 0) { return; }
	bGuideDismissed = true;   // [ИСПРАВЛЕНИЕ] у оригинала гид снова открывается на следующей карте/входе на уровне 1
	SaveProgress(Why);
}
