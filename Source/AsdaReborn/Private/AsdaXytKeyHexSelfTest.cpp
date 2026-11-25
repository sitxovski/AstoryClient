// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ разбора hex-ключа приёма XyT (R5-fix D1, 13.11.2026): `asda.SelfTest.XytKeyHex [quit]`.
//   Проверяет `AsdaXytKeyHex::Parse` в режиме ключа `asda.XyT.RecvKeyStrictHex`:
//     A — правильный ключ (64 hex-символа, строчные и прописные) разбирается в те же 32 байта;
//     B — не-hex символ в старшем/младшем полубайте, пробел и перевод строки внутри ключа — ОТКАЗ с позицией пары;
//     C — строка короче 64 символов — отказ;
//     D — контроль прежнего поведения (`ParseWith(…, false)`): тот же мусор принимается и даёт нулевые полубайты.
//   При `asda.XyT.RecvKeyStrictHex 0` блок B красный (мусор принят) — это и есть дефект до правки.
//   Ничего не отправляет, сеть и мир не нужны; ключ шифра процесса не трогает (проверяется только разбор).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaXytKeyHex.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaXytKeyHexTest, Log, All);

namespace AsdaXytKeyHexTest
{
	// Счётчики и запись проверки в журнал.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // счётчики
		void Expect(bool b, const FString& What)   // проверка с записью в лог
		{
			if (b) { ++Ok; UE_LOG(LogAsdaXytKeyHexTest, Log, TEXT("САМОТЕСТ-КЛЮЧ-XYT ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaXytKeyHexTest, Error, TEXT("САМОТЕСТ-КЛЮЧ-XYT FAIL %s"), *What); }
		}
	};

	// Образцовый ключ: байты 0x00, 0x11 … 0xFF, затем 0x0F, 0x1E … 0xF0 (32 байта).
	static void MakeKey(uint8 K[32])
	{
		for (int32 i = 0; i < 16; ++i) { K[i] = (uint8)(i * 0x11); }
		for (int32 i = 0; i < 16; ++i) { K[16 + i] = (uint8)((i << 4) | (15 - i)); }
	}

	// Hex-запись ключа (строчными или прописными).
	static FString HexOf(const uint8 K[32], bool bUpper)
	{
		FString S;
		for (int32 i = 0; i < 32; ++i) { S += bUpper ? FString::Printf(TEXT("%02X"), K[i]) : FString::Printf(TEXT("%02x"), K[i]); }
		return S;
	}

	// Строка S с заменой символа в позиции Pos на C.
	static FString WithChar(const FString& S, int32 Pos, TCHAR C)
	{
		FString R = S;
		R[Pos] = C;
		return R;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestXytKeyHex(
	TEXT("asda.SelfTest.XytKeyHex"),
	TEXT("asda.SelfTest.XytKeyHex [quit] — самотест разбора hex-ключа приёма XyT (R5-fix D1): правильный ключ разбирается, ")
	TEXT("не-hex символ и короткая строка отвергаются; контроль прежнего поведения. При asda.XyT.RecvKeyStrictHex 0 — красный."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaXytKeyHexTest;
		FRun R;
		bool bQuit = false;
		for (const FString& Arg : A) { if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; } }
		UE_LOG(LogAsdaXytKeyHexTest, Log, TEXT("САМОТЕСТ-КЛЮЧ-XYT: asda.XyT.RecvKeyStrictHex = %d"), AsdaXytKeyHex::IsStrict() ? 1 : 0);

		uint8 Want[32];
		MakeKey(Want);
		const FString Lower = HexOf(Want, false), Upper = HexOf(Want, true);

		// ── A. Правильный ключ ──
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			uint8 Got[32];
			FMemory::Memset(Got, 0xAA, sizeof(Got));
			int32 Bad = -7;
			const bool bOk = AsdaXytKeyHex::Parse(Pass == 0 ? Lower : Upper, Got, Bad);
			R.Expect(bOk && Bad == -1 && FMemory::Memcmp(Got, Want, 32) == 0,
				FString::Printf(TEXT("A%d правильный ключ (%s) разобран в те же 32 байта: ok=%d, позиция %d"), Pass + 1,
					Pass == 0 ? TEXT("строчные") : TEXT("прописные"), bOk ? 1 : 0, Bad));
		}
		{
			uint8 Got[32];
			int32 Bad = -7;
			const bool bOk = AsdaXytKeyHex::Parse(Lower + TEXT("zz-хвост"), Got, Bad);
			R.Expect(bOk && FMemory::Memcmp(Got, Want, 32) == 0, TEXT("A3 хвост после 64 символов не читается (как было): ключ разобран"));
		}

		// ── B. Мусор внутри ключа — отказ с позицией пары ──
		// Случай мусора: позиция символа в ключе, сам символ и описание для журнала.
		struct FCase { int32 Pos; TCHAR C; const TCHAR* What; };
		const FCase Cases[] = {
			{ 10, TEXT('z'), TEXT("не-hex буква в старшем полубайте") },
			{ 11, TEXT('g'), TEXT("не-hex буква в младшем полубайте") },
			{ 30, TEXT(' '), TEXT("пробел внутри ключа") },
			{ 41, TEXT('\n'), TEXT("перевод строки внутри ключа") },
			{ 0,  TEXT('x'), TEXT("не-hex первым символом") },
			{ 63, TEXT('-'), TEXT("не-hex последним символом") },
		};
		for (const FCase& Cs : Cases)
		{
			uint8 Got[32];
			int32 Bad = -7;
			const bool bOk = AsdaXytKeyHex::Parse(WithChar(Lower, Cs.Pos, Cs.C), Got, Bad);
			R.Expect(!bOk && Bad == (Cs.Pos / 2) * 2,
				FString::Printf(TEXT("B %s (символ %d): отказ=%d, позиция пары %d (ждём %d)"), Cs.What, Cs.Pos, bOk ? 0 : 1, Bad, (Cs.Pos / 2) * 2));
		}

		// ── C. Короткая строка ──
		{
			uint8 Got[32];
			int32 Bad = -7;
			const bool bOk = AsdaXytKeyHex::Parse(Lower.Left(63), Got, Bad);
			R.Expect(!bOk && Bad == 63, FString::Printf(TEXT("C1 строка из 63 символов: отказ=%d, позиция %d (ждём 63)"), bOk ? 0 : 1, Bad));
			Bad = -7;
			const bool bOk2 = AsdaXytKeyHex::Parse(FString(), Got, Bad);
			R.Expect(!bOk2 && Bad == 0, FString::Printf(TEXT("C2 пустая строка: отказ=%d, позиция %d (ждём 0)"), bOk2 ? 0 : 1, Bad));
		}

		// ── D. Контроль прежнего поведения (не зависит от ключа) ──
		{
			uint8 Got[32];
			int32 Bad = -7;
			const bool bOk = AsdaXytKeyHex::ParseWith(WithChar(WithChar(Lower, 10, TEXT('z')), 11, TEXT('g')), false, Got, Bad);
			R.Expect(bOk && Got[5] == 0 && Bad == -1,
				FString::Printf(TEXT("D1 контроль прежнего разбора: мусор «zg» принят, байт 5 = 0x%02X (ждём 0x00) — дефект воспроизведён"), Got[5]));
			Bad = -7;
			const bool bOk2 = AsdaXytKeyHex::ParseWith(Upper, true, Got, Bad);
			R.Expect(bOk2 && FMemory::Memcmp(Got, Want, 32) == 0, TEXT("D2 строгий разбор правильного ключа — те же байты"));
		}

		UE_LOG(LogAsdaXytKeyHexTest, Log, TEXT("САМОТЕСТ-КЛЮЧ-XYT ИТОГ: ok=%d fail=%d"), R.Ok, R.Fail);
		if (bQuit && GEngine) { GEngine->Exec(W, TEXT("quit")); }
	}));

#endif // !UE_BUILD_SHIPPING
