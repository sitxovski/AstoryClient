// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ КЛЮЧИ КОНСОЛИ В САМОТЕСТАХ: ЗАПОМНИТЬ ИСХОДНОЕ → ПОМЕНЯТЬ → ВЕРНУТЬ ЗНАЧЕНИЕ И ПРИОРИТЕТ (11.10.2026). [ПОРТ]
//   Волна В0 аудита дублей №2, находка N1(д)/U8: `Reports/dedup_audit2/02_cpp_world_net_data.md` §2.1, `01_cpp_ui.md` U8.
//
//   ЗАЧЕМ. Самотесты меняли ключи `Set(…, ECVF_SetByConsole)` и «возвращали» их КОНСТАНТОЙ (самотест заточки ставил
//   `asda.Upgrade.ReplyTimeoutSec = 10` при умолчании 20) или прежним значением, но с приоритетом «из консоли». В обоих
//   случаях после самотеста ключ испорчен до перезапуска: значение не то, а запись из ini/кода с меньшим приоритетом
//   молча отвергается движком. Образец правильного возврата — `AsdaPacketStand.cpp` (`FSavedCVar`); здесь он общий.
//
//   КАК. `SetForTest(имя, значение)` при ПЕРВОМ касании ключа в процессе запоминает исходные значение и приоритет
//   (SetBy) и ставит значение с приоритетом «из консоли» (как раньше — поведение самого самотеста не меняется).
//   `RestoreAll(кто)` в конце самотеста возвращает каждому запомненному ключу исходное значение (запись с текущим
//   приоритетом проходит всегда) и затем исходный приоритет (`SetFlags`), печатает строку `CVAR_RESTORE` и очищает
//   реестр. `Remember(имя)` — только запомнить (если ключ меняется чужим кодом, например самой подсистемой).
//
//   Доступен во всех сборках (зависимостей от тестового кода нет; самотесты части подсистем компилируются и в Shipping).
//   Реестр общий на процесс: самотесты одного процесса идут последовательно.
//
//   ★ 11.10.2026 (ревизия wave0_REVIEW.md, С1). УХОД МИРА ПОСРЕДИ САМОТЕСТА. Шаги самотестов идут на таймерах мира; если мир
//   ушёл раньше итога, `RestoreAll` не вызывался и ключи оставались подменёнными. `ArmWorldGuard(мир, кто)` — одна общая
//   точка: при очистке ИМЕННО этого мира (`FWorldDelegates::OnWorldCleanup`, сравнение указателя) — `RestoreAll(кто)`.
//   Зовётся из команды самотеста рядом с первым `SetForTest`; `RestoreAll(кто)` снимает сторожа этого самотеста.
//   ★ 11.10.2026 (МЕЛК-2). История приоритетов UE 5.8: каждый `Set` пишет запись в историю (`TrackHistory`), и после
//   возврата в ней оставалась запись уровня Console. `Restore` снимает её `Unset(ECVF_SetByConsole)`, если исходный
//   приоритет ниже консоли, — иначе позднее снятие нижнего уровня (`Unset`) вернуло бы значение и приоритет самотеста.
#pragma once
#include "CoreMinimal.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace AsdaCVarSave
{
	// Исходное состояние ключа: значение (строкой; вещественное — ещё и числом без потери точности) и приоритет.
	struct FSaved
	{
		FString Name;                                          // имя ключа
		bool bFound = false;                                   // ключ существует
		bool bFloat = false;                                   // ключ вещественный (возврат числом)
		float FloatValue = 0.f;                                // исходное вещественное значение
		FString Value;                                         // исходное значение строкой
		EConsoleVariableFlags SetBy = ECVF_SetByConstructor;   // исходный приоритет
	};

	// Реестр «первого касания» ключей за процесс (имя → исходное состояние).
	inline TMap<FString, FSaved>& Registry()
	{
		static TMap<FString, FSaved> GRegistry;
		return GRegistry;
	}

	// Снять текущее состояние ключа.
	inline FSaved Save(const TCHAR* Name)
	{
		FSaved S;
		S.Name = Name;
		if (const IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			S.bFound = true;
			S.bFloat = C->IsVariableFloat();
			S.FloatValue = C->GetFloat();
			S.Value = C->GetString();
			S.SetBy = (EConsoleVariableFlags)((uint32)C->GetFlags() & (uint32)ECVF_SetByMask);
		}
		return S;
	}

	// Вернуть значение и приоритет; строка `CVAR_RESTORE` в лог. true — и значение, и приоритет совпали с исходными.
	inline bool Restore(const FSaved& S, const TCHAR* Who)
	{
		if (!S.bFound) { return true; }   // ключа не было — возвращать нечего
		IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(*S.Name);
		if (!C) { return false; }
		const EConsoleVariableFlags Cur = (EConsoleVariableFlags)((uint32)C->GetFlags() & (uint32)ECVF_SetByMask);
		if (S.bFloat) { C->Set(S.FloatValue, Cur); } else { C->Set(*S.Value, Cur); }   // текущий приоритет — запись не отвергается
		// ★ МЕЛК-2: снять запись уровня Console из истории приоритетов (движок сам пересчитает значение и приоритет по истории).
		if ((uint32)S.SetBy < (uint32)ECVF_SetByConsole && (uint32)Cur >= (uint32)ECVF_SetByConsole)
		{
			C->Unset(ECVF_SetByConsole);
			const bool bValOk = S.bFloat ? (C->GetFloat() == S.FloatValue) : (C->GetString() == S.Value);   // значение после пересчёта
			if (!bValOk)   // страховка: история дала другое значение — вернуть исходное текущим приоритетом
			{
				const EConsoleVariableFlags Cur2 = (EConsoleVariableFlags)((uint32)C->GetFlags() & (uint32)ECVF_SetByMask);
				if (S.bFloat) { C->Set(S.FloatValue, Cur2); } else { C->Set(*S.Value, Cur2); }
			}
		}
		C->SetFlags((EConsoleVariableFlags)(((uint32)C->GetFlags() & ~(uint32)ECVF_SetByMask) | (uint32)S.SetBy));
		const EConsoleVariableFlags Now = (EConsoleVariableFlags)((uint32)C->GetFlags() & (uint32)ECVF_SetByMask);
		const bool bSame = (S.bFloat ? (C->GetFloat() == S.FloatValue) : (C->GetString() == S.Value)) && (Now == S.SetBy);
		UE_LOG(LogTemp, Display, TEXT("CVAR_RESTORE [%s] %s: значение=%s SetBy=%s (исходно %s, %s) — %s"), Who, *S.Name, *C->GetString(),
			GetConsoleVariableSetByName(Now), *S.Value, GetConsoleVariableSetByName(S.SetBy), bSame ? TEXT("восстановлен") : TEXT("НЕ ВОССТАНОВЛЕН"));
		return bSame;
	}

	// Запомнить исходное состояние ключа (только при первом касании за процесс).
	inline void Remember(const TCHAR* Name)
	{
		if (!Registry().Contains(Name)) { Registry().Add(Name, Save(Name)); }
	}

	// Поставить значение на время самотеста (приоритет «из консоли», как прежние `Set(…, ECVF_SetByConsole)`), запомнив исходное.
	inline void SetForTest(const TCHAR* Name, const TCHAR* Value)
	{
		Remember(Name);
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name)) { C->Set(Value, ECVF_SetByConsole); }
	}
	// То же для целого.
	inline void SetForTest(const TCHAR* Name, int32 Value)
	{
		Remember(Name);
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name)) { C->Set(Value, ECVF_SetByConsole); }
	}
	// То же для вещественного.
	inline void SetForTest(const TCHAR* Name, float Value)
	{
		Remember(Name);
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name)) { C->Set(Value, ECVF_SetByConsole); }
	}

	// ★ С1: сторож мира самотеста — мир (указатель для сравнения при очистке) и имя самотеста.
	struct FWorldGuard
	{
		const UWorld* World = nullptr;   // мир прогона (только сравнение, не разыменовывается)
		FString Who;                     // имя самотеста (для CVAR_RESTORE)
	};

	// Взведённые сторожа (на процесс).
	inline TArray<FWorldGuard>& WorldGuards()
	{
		static TArray<FWorldGuard> GGuards;
		return GGuards;
	}

	// Снять сторожей самотеста `Who` (вызывает `RestoreAll` — ключи уже возвращены штатно).
	inline void DisarmWorldGuard(const TCHAR* Who)
	{
		WorldGuards().RemoveAll([Who](const FWorldGuard& G) { return G.Who == Who; });
	}

	// Вернуть все запомненные ключи (значение + приоритет) и очистить реестр; число НЕ восстановленных.
	inline int32 RestoreAll(const TCHAR* Who)
	{
		DisarmWorldGuard(Who);   // ★ С1: итог дошёл — сторож мира этого самотеста больше не нужен
		int32 Bad = 0;
		TArray<FString> Names;
		Registry().GetKeys(Names);
		Names.Sort();
		for (const FString& N : Names) { if (!Restore(Registry()[N], Who)) { ++Bad; } }
		Registry().Reset();
		return Bad;
	}

	// ★ С1: взвести сторожа — при очистке мира `W` вернуть ключи (`RestoreAll(Who)`), если самотест не дошёл до итога.
	//   Подписка на `OnWorldCleanup` одна на процесс; повторный вызов с тем же миром и именем ничего не добавляет.
	inline void ArmWorldGuard(const UWorld* W, const TCHAR* Who)
	{
		if (!W || !Who) { return; }
		for (const FWorldGuard& G : WorldGuards()) { if (G.World == W && G.Who == Who) { return; } }
		WorldGuards().Add(FWorldGuard{ W, FString(Who) });
		static FDelegateHandle GHandle;   // подписка на очистку миров (одна на процесс)
		if (!GHandle.IsValid())
		{
			GHandle = FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
			{
				TArray<FString> Whos;   // самотесты, чей мир уходит
				for (const FWorldGuard& G : WorldGuards()) { if (G.World == World) { Whos.AddUnique(G.Who); } }
				WorldGuards().RemoveAll([World](const FWorldGuard& G) { return G.World == World; });
				for (const FString& N : Whos)
				{
					if (Registry().Num() == 0) { break; }   // возвращать нечего
					UE_LOG(LogTemp, Warning, TEXT("CVAR_RESTORE [%s] мир ушёл до итога самотеста — возврат ключей (%d)"), *N, Registry().Num());
					RestoreAll(*N);
				}
			});
		}
	}
}

