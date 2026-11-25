// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТЫ: ОБЩИЕ ПОМОЩНИКИ КОНТЕКСТА ПРОГОНА `AsdaSelfTestCtx` (R5-c, волна дублей, 12.11.2026). [ПОРТ]
//   Аудит и отчёт: `Reports/refactor_2026-11/r5/c/AUDIT.md`, `REPORT.md`.
//
//   ЗАЧЕМ. Почти каждый самотест окна держит свой контекст прогона `FCtx` (мир `W` слабой ссылкой, счётчики, перехват
//   отправок) и рядом — свои копии одних и тех же помощников: «шаг через N секунд», «экземпляр игры», «подсистема»,
//   «подать пакет». Копии были дословными с точностью до имени типа контекста (33 `At`, 17 `GIOf`, 15 `Sub`, 15 + 7 + 4 `After`,
//   7 `Feed`, 5 `CountOp`, 5 `Then`). Здесь — по одной функции-шаблону на каждую; тип контекста — параметр шаблона.
//
//   КАК ПОДКЛЮЧАЕТСЯ. В самотесте на месте прежней копии стоит её комментарий и `using AsdaSelfTestCtx::At;` — вызовы
//   не меняются. Требование к контексту то же, что было у копий: поле `W` (`TWeakObjectPtr<UWorld>`), для `Then` — ещё
//   накопленное время `T` (float), для `CountOp` — `Sent` (указатель на `TArray<TPair<int32, TArray<uint8>>>`).
//
//   ★ `After` бывает трёх видов (этап указателем на функцию, этап `TFunction` с контекстом, шаг по миру) — они в разных
//   вложенных пространствах имён (`ByStage`, `ByFn`, `ByWorld`): в одном пространстве вызов с лямбдой без захвата был бы
//   неоднозначен между указателем на функцию и `TFunction`.
//   Только для самотестов; ничего не шлёт и состояния игры не трогает.
#pragma once
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "Test/AsdaSelfTestNet.h"

namespace AsdaSelfTestCtx
{
	// Шаг через Sec секунд мирового таймера (не меньше 0,01 с). Мира нет — шаг не ставится.
	//   (У копий: «Шаг через Sec секунд», «Запустить шаг через Sec секунд»; у семи копий параметр звался `Delay`.)
	template <class TCtx> void At(const TSharedRef<TCtx>& C, float Sec, TFunction<void()> Fn)
	{
		UWorld* W = C->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), FMath::Max(0.01f, Sec), false);
	}

	// Следующий шаг через Step секунд после предыдущего: время копится в `C->T`.
	template <class TCtx> void Then(const TSharedRef<TCtx>& C, float Step, TFunction<void()> Fn)
	{
		UWorld* W = C->W.Get();
		if (!W) { return; }
		C->T += Step;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), FMath::Max(0.01f, C->T), false);
	}

	// Экземпляр игры мира прогона (nullptr — мира уже нет).
	template <class TCtx> UGameInstance* GIOf(const TSharedRef<TCtx>& C) { UWorld* W = C->W.Get(); return W ? W->GetGameInstance() : nullptr; }

	// Подсистема экземпляра игры мира прогона (nullptr — мира нет).
	template <class T, class TCtx> T* Sub(const TSharedRef<TCtx>& C) { UGameInstance* GI = GIOf(C); return GI ? GI->GetSubsystem<T>() : nullptr; }

	// Подать пакет в разбор так, будто он пришёл с сервера (`UAsdaNetSubsystem::DebugFeedPacket`). Нет мира/сети — ничего.
	//   У копий было `Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(Op, B)`; здесь то же через тонкий доступ `AsdaTestNet`
	//   (полный класс сети этому заголовку не нужен): `AsdaTestNet::Get` = `GI->GetSubsystem<UAsdaNetSubsystem>()`.
	template <class TCtx> void Feed(const TSharedRef<TCtx>& C, int32 Op, const TArray<uint8>& B)
	{
		UGameInstance* GI = GIOf(C);
		if (UAsdaNetSubsystem* N = GI ? AsdaTestNet::Get(GI) : nullptr) { AsdaTestNet::DebugFeedPacket(N, Op, B); }
	}

	// Сколько перехвачено опкода Op; OutLast — payload последнего.
	template <class TCtx> int32 CountOp(const TSharedRef<TCtx>& C, int32 Op, TArray<uint8>* OutLast = nullptr)
	{
		int32 N = 0;
		if (!C->Sent) { return 0; }
		for (const TPair<int32, TArray<uint8>>& P : *C->Sent) { if (P.Key == Op) { ++N; if (OutLast) { *OutLast = P.Value; } } }
		return N;
	}

	// ★ R5-fix-b 13.11.2026 (заключительный заход, четвёртая запись): очередь шагов «по событию» (`FStepSeq`) и `Until` (R5-fix6-c) сведены на общую
	//   `Test/AsdaSelfTestSeq.h` (`At`/`When`/`AfterInput`) — один механизм на самотесты (просьба координатора, 13.11.2026).

	namespace ByStage
	{
		// Запустить этап через Sec секунд мирового таймера (кадры идут, геометрия успевает отрисоваться); этап — функция
		//   `void Stage(TSharedRef<FCtx>)`. Контекст держится копией до срабатывания таймера. Мира нет — этап не ставится.
		//   (У копий: «Следующий этап через Sec секунд»; у семи копий параметр звался `Fn`.)
		template <class TCtx> void After(TSharedRef<TCtx> C, float Sec, TIdentity_T<void (*)(TSharedRef<TCtx>)> Stage)
		{
			UWorld* W = C->W.Get();
			if (!W) { return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C, Stage]() { Stage(C); }), Sec, false);
		}
	}

	namespace ByFn
	{
		// Таймер мира прогона: через Sec секунд вызвать `Fn(C)`. Контекст держится копией до срабатывания таймера.
		//   (У одной копии из семи параметр звался `Stage`.)
		template <class TCtx> void After(TSharedRef<TCtx> C, float Sec, TIdentity_T<TFunction<void(TSharedRef<TCtx>)>> Fn)
		{
			UWorld* W = C->W.Get();
			if (!W) { return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C, Fn]() { Fn(C); }), Sec, false);
		}
	}

	namespace ByWorld
	{
		// Запуск шага через Delay секунд от «сейчас» (не меньше 0,01 с) таймером мира `W` (мир обязан быть, как у копий).
		inline void After(UWorld* W, float Delay, TFunction<void()> Fn)
		{
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), FMath::Max(0.01f, Delay), false);
		}
	}
}
