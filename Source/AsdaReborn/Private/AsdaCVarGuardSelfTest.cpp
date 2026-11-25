// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ сторожа ключей самотестов (11.10.2026, ревизия wave0_REVIEW.md С1 + МЕЛК-2): `asda.SelfTest.CVarGuard [quit]`.
//   С1: ключ подменён (`AsdaCVarSave::SetForTest`) и взведён сторож мира (`ArmWorldGuard`), затем мир УХОДИТ настоящей
//       сменой карты (`open <та же карта>`, без итога самотеста) → в новом мире ключ обязан иметь исходные значение и
//       приоритет, реестр — пуст, в логе строка «CVAR_RESTORE … мир ушёл до итога».
//   МЕЛК-2: после возврата ключ ставится ниже консоли (ECVF_SetByGameSetting) и этот уровень снимается `Unset` → значение и
//       приоритет обязаны вернуться к исходным (без снятия записи Console из истории приоритетов UE 5.8 вернулось бы
//       значение самотеста с приоритетом Console). Контрольная пара: тот же Set/Unset до подмены — даёт исходное.
//   Ключ — `asda.UI.OpenOnTop` (окна поверх; на время самотеста меняется 1→0). Ничего не отправляет, сеть не нужна.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCVarGuardTest, Log, All);

namespace AsdaCVarGuardTest
{
	static const TCHAR* GKey = TEXT("asda.UI.OpenOnTop");          // подменяемый ключ
	static const TCHAR* GWho = TEXT("asda.SelfTest.CVarGuard");    // имя самотеста (для сторожа)

	// Состояние прогона (живёт в замыкании тикера ядра — переживает смену мира).
	struct FRun
	{
		const UWorld* OldWorld = nullptr;   // мир, который уходит (только сравнение)
		TWeakObjectPtr<UWorld> OldWeak;     // он же слабой ссылкой: новый мир может занять тот же адрес
		FString OrigValue;                  // исходное значение ключа
		EConsoleVariableFlags OrigSetBy = ECVF_SetByConstructor;   // исходный приоритет
		bool bQuit = false;                 // выйти после итога
		double T0 = 0.0;                    // старт ожидания смены мира
		int32 Ok = 0, Fail = 0;             // счётчики
		void Expect(bool b, const FString& What)   // проверка с записью в лог
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCVarGuardTest, Log, TEXT("СТОРОЖ-КЛЮЧЕЙ ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaCVarGuardTest, Error, TEXT("СТОРОЖ-КЛЮЧЕЙ FAIL %s"), *What); }
		}
	};

	// Приоритет ключа (биты SetBy).
	static EConsoleVariableFlags SetByOf(const IConsoleVariable* C)
	{
		return (EConsoleVariableFlags)((uint32)C->GetFlags() & (uint32)ECVF_SetByMask);
	}

	// Игровой мир процесса.
	static UWorld* GameWorld()
	{
		if (!GEngine) { return nullptr; }
		for (const FWorldContext& C : GEngine->GetWorldContexts())
		{
			if ((C.WorldType == EWorldType::Game || C.WorldType == EWorldType::PIE) && C.World()) { return C.World(); }
		}
		return nullptr;
	}

	// Проверки после смены мира + МЕЛК-2; итог и (с quit) выход.
	static void Finish(TSharedRef<FRun> R, UWorld* W)
	{
		IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(GKey);
		R->Expect(C != nullptr, TEXT("ключ существует"));
		if (C)
		{
			R->Expect(C->GetString() == R->OrigValue, FString::Printf(TEXT("С1: после ухода мира значение исходное (%s, ждали %s)"), *C->GetString(), *R->OrigValue));
			R->Expect(SetByOf(C) == R->OrigSetBy, FString::Printf(TEXT("С1: приоритет исходный (%s, ждали %s)"),
				GetConsoleVariableSetByName(SetByOf(C)), GetConsoleVariableSetByName(R->OrigSetBy)));
			R->Expect(AsdaCVarSave::Registry().Num() == 0, FString::Printf(TEXT("С1: реестр подмен пуст (%d)"), AsdaCVarSave::Registry().Num()));
			// МЕЛК-2: снятие нижнего уровня не возвращает значение самотеста.
			C->Set(TEXT("7"), ECVF_SetByGameSetting);
			C->Unset(ECVF_SetByGameSetting);
			R->Expect(C->GetString() == R->OrigValue, FString::Printf(TEXT("МЕЛК-2: Set+Unset уровня GameSetting после возврата → значение %s (ждали %s)"), *C->GetString(), *R->OrigValue));
			R->Expect(SetByOf(C) == R->OrigSetBy, FString::Printf(TEXT("МЕЛК-2: приоритет %s (ждали %s; Console = застрявшая запись истории)"), GetConsoleVariableSetByName(SetByOf(C)), GetConsoleVariableSetByName(R->OrigSetBy)));
			// Контрольная пара: ПРЕЖНИЙ возврат (Set текущим приоритетом + SetFlags, без Unset) — проверка обязана увидеть Console.
			const FString TestV = R->OrigValue == TEXT("0") ? TEXT("1") : TEXT("0");
			C->Set(*TestV, ECVF_SetByConsole);                         // подмена, как SetForTest
			C->Set(*R->OrigValue, ECVF_SetByConsole);                  // прежний Restore: значение текущим приоритетом…
			C->SetFlags((EConsoleVariableFlags)(((uint32)C->GetFlags() & ~(uint32)ECVF_SetByMask) | (uint32)R->OrigSetBy));   // …и SetFlags
			C->Set(TEXT("7"), ECVF_SetByGameSetting);
			C->Unset(ECVF_SetByGameSetting);
			R->Expect(SetByOf(C) == ECVF_SetByConsole, FString::Printf(TEXT("контроль МЕЛК-2: прежний возврат → после Unset приоритет %s (ждали Console — дефект воспроизведён)"),
				GetConsoleVariableSetByName(SetByOf(C))));
			C->Unset(ECVF_SetByConsole);                               // уборка контроля: снять запись Console
			C->Set(*R->OrigValue, SetByOf(C));
			C->SetFlags((EConsoleVariableFlags)(((uint32)C->GetFlags() & ~(uint32)ECVF_SetByMask) | (uint32)R->OrigSetBy));
			R->Expect(C->GetString() == R->OrigValue && SetByOf(C) == R->OrigSetBy, FString::Printf(TEXT("уборка контроля: %s/%s"),
				*C->GetString(), GetConsoleVariableSetByName(SetByOf(C))));
		}
		UE_LOG(LogAsdaCVarGuardTest, Log, TEXT("СТОРОЖ-КЛЮЧЕЙ ИТОГ: ok=%d fail=%d"), R->Ok, R->Fail);
		if (R->bQuit && GEngine) { GEngine->Exec(W, TEXT("quit")); }
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCVarGuard(
	TEXT("asda.SelfTest.CVarGuard"),
	TEXT("asda.SelfTest.CVarGuard [quit] — самотест сторожа ключей (AsdaCVarSave): подмена ключа, уход мира сменой карты без итога → ")
	TEXT("ключ вернулся (значение и приоритет); Set+Unset нижнего уровня после возврата не поднимает значение самотеста. Офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaCVarGuardTest;
		TSharedRef<FRun> R = MakeShared<FRun>();
		for (const FString& Arg : A) { if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { R->bQuit = true; } }
		IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(GKey);
		if (!W || !C)
		{
			UE_LOG(LogAsdaCVarGuardTest, Error, TEXT("СТОРОЖ-КЛЮЧЕЙ ИТОГ: ОТКАЗ — нет мира или ключа %s"), GKey);
			if (R->bQuit && GEngine) { GEngine->Exec(W, TEXT("quit")); }
			return;
		}
		R->OrigValue = C->GetString();
		R->OrigSetBy = SetByOf(C);
		// Контрольная пара МЕЛК-2: до подмены Set+Unset нижнего уровня возвращает исходное.
		C->Set(TEXT("7"), ECVF_SetByGameSetting);
		C->Unset(ECVF_SetByGameSetting);
		R->Expect(C->GetString() == R->OrigValue && SetByOf(C) == R->OrigSetBy, FString::Printf(TEXT("контроль: Set+Unset GameSetting до подмены → %s/%s"),
			*C->GetString(), GetConsoleVariableSetByName(SetByOf(C))));
		// Подмена и сторож, затем уход мира БЕЗ итога (RestoreAll не зовётся самотестом).
		AsdaCVarSave::ArmWorldGuard(W, GWho);
		AsdaCVarSave::SetForTest(GKey, R->OrigValue == TEXT("0") ? 1 : 0);
		R->Expect(C->GetString() != R->OrigValue && SetByOf(C) == ECVF_SetByConsole, FString::Printf(TEXT("подмена стоит: %s/%s"),
			*C->GetString(), GetConsoleVariableSetByName(SetByOf(C))));
		R->OldWorld = W;
		R->OldWeak = W;
		R->T0 = FPlatformTime::Seconds();
		// Та же карта С ТЕМИ ЖЕ параметрами URL (?game=… режима мира): без них мир поднимается с режимом по умолчанию, и
		//   следующие самотесты пакета шли бы в другом окружении (замечено пакетом: Craft.SelfTest 65/2 после голого `open`).
		FString Map = UWorld::RemovePIEPrefix(W->GetOutermost()->GetName());
		for (const FString& Op : W->URL.Op) { Map += TEXT("?") + Op; }
		UE_LOG(LogAsdaCVarGuardTest, Log, TEXT("СТОРОЖ-КЛЮЧЕЙ: уход мира — open %s"), *Map);
		GEngine->Exec(W, *FString::Printf(TEXT("open %s"), *Map));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float) -> bool
		{
			UWorld* NW = GameWorld();
			const bool bNew = NW && (NW != R->OldWorld || !R->OldWeak.IsValid());   // другой мир (адрес или поколение объекта)
			if (bNew && NW->HasBegunPlay()) { Finish(R, NW); return false; }
			if (FPlatformTime::Seconds() - R->T0 > 120.0)
			{
				R->Expect(false, TEXT("новый мир не поднялся за 120 с"));
				AsdaCVarSave::RestoreAll(GWho);
				Finish(R, NW);
				return false;
			}
			return true;
		}), 0.2f);
	}));

#endif // !UE_BUILD_SHIPPING
