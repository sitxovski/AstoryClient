// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ равенства двух разборов printf-шаблона с одним целым (R5-fix Г15, 13.11.2026): `asda.SelfTest.SprintfOneInt [quit]`.
//   До R5-fix шаблон опции предмета и «скорость атаки» инфо-панели разбирала своя `FormatOneInt` (AsdaItemTooltipSubsystemInternal.h),
//   дословно повторявшая `UAsdaMiniStateSubsystem::LegacySprintfOneInt`. Теперь `FormatOneInt` — вызов общей функции.
//   Самотест держит ПРЕЖНЕЕ тело `FormatOneInt` как образец (`Reference`, текст перенесён без изменений) и сверяет с ним вывод
//   общей функции:
//     A — граничные шаблоны с ожидаемым текстом (и образец, и общая функция обязаны дать ровно его);
//     B — ВСЕ шаблоны `FmtString` таблицы `itemoptiontable` (то, что реально получает `FormatOptionValue`) × набор значений;
//     C — строка 111405 «скорость атаки» инфо-панели × набор значений.
//   Ничего не отправляет; мир нужен только ради подсистем таблиц и текста.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSprintfOneIntTest, Log, All);

namespace AsdaSprintfOneIntTest
{
	// Образец: прежнее тело `FormatOneInt` (до R5-fix), перенесено без изменений.
	static FString Reference(const FString& Fmt, int32 Arg)
	{
		FString Out;
		const int32 N = Fmt.Len();
		bool bUsed = false;
		for (int32 i = 0; i < N; ++i)
		{
			const TCHAR C = Fmt[i];
			if (C != TEXT('%')) { Out.AppendChar(C); continue; }
			int32 j = i + 1;
			if (j < N && Fmt[j] == TEXT('%')) { Out.AppendChar(TEXT('%')); i = j; continue; }
			while (j < N && (Fmt[j] == TEXT('-') || Fmt[j] == TEXT('+') || Fmt[j] == TEXT(' ')
			              || Fmt[j] == TEXT('#') || Fmt[j] == TEXT('0') || Fmt[j] == TEXT('.')
			              || FChar::IsDigit(Fmt[j]))) { ++j; }
			if (j >= N) { break; }
			const TCHAR Conv = Fmt[j];
			if (Conv == TEXT('d') || Conv == TEXT('i') || Conv == TEXT('u'))
			{
				Out += bUsed ? TEXT("0") : *FString::FromInt(Arg);
				bUsed = true;
			}
			else { Out.AppendChar(Conv); }
			i = j;
		}
		return Out;
	}

	// Счётчики и запись проверки в журнал.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // счётчики
		void Expect(bool b, const FString& What)   // проверка с записью в лог
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSprintfOneIntTest, Log, TEXT("САМОТЕСТ-ШАБЛОН-ЧИСЛА ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaSprintfOneIntTest, Error, TEXT("САМОТЕСТ-ШАБЛОН-ЧИСЛА FAIL %s"), *What); }
		}
	};

	// Значения аргумента для сверки шаблонов таблицы.
	static const int32 kArgs[] = { 0, 1, -1, 7, 100, 12345, MAX_int32, MIN_int32 };

	// Сверка одного шаблона по всем значениям: число расхождений; первое расхождение — в OutFirst.
	static int32 Mismatches(const FString& Fmt, FString& OutFirst)
	{
		int32 Bad = 0;
		for (const int32 V : kArgs)
		{
			const FString A = Reference(Fmt, V), B = UAsdaMiniStateSubsystem::LegacySprintfOneInt(Fmt, V);
			if (!A.Equals(B, ESearchCase::CaseSensitive))
			{
				if (Bad == 0) { OutFirst = FString::Printf(TEXT("«%s» при %d: образец «%s», общая «%s»"), *Fmt, V, *A, *B); }
				++Bad;
			}
		}
		return Bad;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSprintfOneInt(
	TEXT("asda.SelfTest.SprintfOneInt"),
	TEXT("asda.SelfTest.SprintfOneInt [quit] — самотест R5-fix Г15: вывод общей LegacySprintfOneInt равен прежней FormatOneInt ")
	TEXT("на граничных шаблонах, всех шаблонах itemoptiontable и строке 111405."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaSprintfOneIntTest;
		FRun R;
		bool bQuit = false;
		for (const FString& Arg : A) { if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; } }

		// ── A. Граничные шаблоны с ожидаемым текстом ──
		// Граничный случай: шаблон, аргумент и ожидаемый текст.
		struct FCase { const TCHAR* Fmt; int32 Arg; const TCHAR* Want; };
		const FCase Cases[] = {
			{ TEXT("Критический удар при атаке +%d%%"), 5, TEXT("Критический удар при атаке +5%") },
			{ TEXT("%d"), -7, TEXT("-7") },
			{ TEXT("%d и %d"), 3, TEXT("3 и 0") },
			{ TEXT("%5d|%-3i|%u"), 42, TEXT("42|0|0") },
			{ TEXT("100%%"), 1, TEXT("100%") },
			{ TEXT("+ 25% \n ОЗ"), 9, TEXT("+ 25\n ОЗ") },
			{ TEXT("конец %"), 1, TEXT("конец ") },
			{ TEXT("конец %5"), 1, TEXT("конец ") },
			{ TEXT("%s=%d"), 4, TEXT("s=4") },
			{ TEXT(""), 1, TEXT("") },
			{ TEXT("без спецификаторов"), 1, TEXT("без спецификаторов") },
			{ TEXT("%+d"), MAX_int32, TEXT("2147483647") },
			{ TEXT("%d"), MIN_int32, TEXT("-2147483648") },
			{ TEXT("%%%d%%"), 0, TEXT("%0%") },
			{ TEXT("%.2d"), 3, TEXT("3") },
			{ TEXT("%#x"), 255, TEXT("x") },
			{ TEXT("%"), 1, TEXT("") },
			{ TEXT("%%"), 1, TEXT("%") },
		};
		for (const FCase& C : Cases)
		{
			const FString Ref = Reference(C.Fmt, C.Arg), Got = UAsdaMiniStateSubsystem::LegacySprintfOneInt(C.Fmt, C.Arg);
			R.Expect(Ref.Equals(C.Want, ESearchCase::CaseSensitive) && Got.Equals(C.Want, ESearchCase::CaseSensitive),
				FString::Printf(TEXT("A «%s» при %d: образец «%s», общая «%s», ждём «%s»"), C.Fmt, C.Arg, *Ref, *Got, C.Want));
		}

		// ── B. Все шаблоны таблицы опций предмета ──
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const UDataTable* Dt = IT ? IT->GetDataTable(TEXT("itemoptiontable")) : nullptr;
		R.Expect(Dt != nullptr, TEXT("B0 таблица itemoptiontable загружена"));
		if (Dt)
		{
			int32 Rows = 0, WithSpec = 0, Bad = 0;
			FString First;
			for (const TPair<FName, uint8*>& P : Dt->GetRowMap())
			{
				const FAsdaItemoptiontableRow* Row = reinterpret_cast<const FAsdaItemoptiontableRow*>(P.Value);
				++Rows;
				if (Row->FmtString.Contains(TEXT("%"))) { ++WithSpec; }
				FString One;
				const int32 N = Mismatches(Row->FmtString, One);
				if (N > 0 && Bad == 0) { First = One; }
				Bad += N;
			}
			R.Expect(Rows > 0 && WithSpec > 0, FString::Printf(TEXT("B1 шаблонов в таблице %d, из них со спецификатором %d (не пусто)"), Rows, WithSpec));
			R.Expect(Bad == 0, FString::Printf(TEXT("B2 все шаблоны таблицы × %d значений: расхождений %d%s%s"), (int32)UE_ARRAY_COUNT(kArgs), Bad,
				Bad ? TEXT("; первое: ") : TEXT(""), *First));
		}

		// ── C. Строка «скорость атаки» инфо-панели ──
		{
			const FString Fmt = AsdaNotice::LocRaw(GI, 111405);
			FString First;
			const int32 Bad = Fmt.IsEmpty() ? -1 : Mismatches(Fmt, First);
			R.Expect(Bad == 0, FString::Printf(TEXT("C1 строка 111405 «%s» × %d значений: расхождений %d%s%s"), *Fmt, (int32)UE_ARRAY_COUNT(kArgs), Bad,
				Bad > 0 ? TEXT("; первое: ") : TEXT(""), *First));
		}

		UE_LOG(LogAsdaSprintfOneIntTest, Log, TEXT("САМОТЕСТ-ШАБЛОН-ЧИСЛА ИТОГ: ok=%d fail=%d"), R.Ok, R.Fail);
		if (bQuit && GEngine) { GEngine->Exec(W, TEXT("quit")); }
	}));

#endif // !UE_BUILD_SHIPPING
