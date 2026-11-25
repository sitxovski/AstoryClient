// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТЫ: ШАГИ ПО СОБЫТИЯМ, А НЕ ПО ЧАСАМ — `AsdaSelfTestSeq` (R5-fix-b, заключительный заход, 13.11.2026). [ПОРТ]
//   Отчёт: `Reports/refactor_2026-11/r5/fix/b/REPORT6.md`.
//
//   ЗАЧЕМ. Самотесты окон строят прогон расписанием `At(C, t, шаг)` по мировому таймеру: «в 7,4 с перетащить, в 9,0 с проверить».
//   Заказанный ввод (`UAsdaWindowSubsystem::SimulateClickAt/SimulateDrag`) идёт по КАДРАМ: перетаскивание с удержанием 30 кадров
//   при 60 к/с длится 0,5 с, при 15 к/с (три игры разом) — 2 с. Тогда проверка в 9,0 с читает состояние ДО отпускания, а следующий
//   заказ затирает недоставленный — один медленный кадр даёт каскад ложных провалов (`SelfTest.Rings`, `SelfTest.DragDelete`,
//   `SelfTest.Sowel`, `SelfTest.WareStoreCombo` в прогоне `rx_r5fix3`; одиночно все зелёные).
//
//   ЧТО ЗДЕСЬ.
//   • `At(C, Sec, Fn)` — та же запись, что `AsdaSelfTestCtx::At`, но шаги идут ОЧЕРЕДЬЮ по порядку расписания, и каждый ждёт:
//       1) пока заказанный клик/перетаскивание доставлен (`IsSimClickPending() == false`; потолок 20 с — дальше проверки покажут)
//          и после отпускания прошло 3 кадра (окна разбирают мышь в своём `Tick`, геометрия обновляется на отрисовке);
//       2) свой срок по расписанию от ЯКОРЯ — реального времени предыдущего шага по расписанию (`t_i − t_{i−1}`), а если был ввод —
//          ещё и min(промежуток, `AfterInputSec`) после его отпускания (поле очереди, по умолчанию 0,25 с; ★ R5-fix8-b 14.11.2026 (пятая запись, ревизия REVIEW8b)).
//     Якорь (ревизия REVIEW6 С-2): шаг, выполненный в срок (опоздание до 0,1 с — округление до кадра), сдвигает якорь на СРОК, а не на
//     «сейчас» — опоздания на кадр не копятся по длинному расписанию; шаг, который держали ввод, ворота или заминка дольше 0,1 с, ставит
//     якорь на «сейчас» — следующие шаги не срабатывают пачкой в одном кадре. При обычном кадре порядок и промежутки те же, что у
//     мирового таймера; под нагрузкой прогон растягивается на время ожидания событий, а не рвётся.
//     Часы — реальные (`FPlatformTime`): ожидание здесь — за кадрами ввода и отрисовки, а не за игровыми таймерами; игровое время
//     обрезает длинный кадр и отставало бы от самих кадров.
//     Вложенный `At` из шага отсчитывается от времени расписания этого шага (как таймер, поставленный из шага). `At` при пустой
//     очереди (не из шага) отсчитывается от момента вызова (REVIEW6 М-1).
//   • `When(C, Sec, Pred, Cap, Fn)` — шаг-«ворота»: держит очередь, пока `Pred()` ложно (не дольше `Cap` с), затем `Fn`.
//     Следующие шаги отсчитывают свои промежутки от момента ворот, а не от старта.
//   • `AfterInput(W, Fn, MinSec, Frames, Cap)` — разовый шаг этапного самотеста: `Fn` после доставки заказанного ввода,
//     не раньше `Frames` кадров и `MinSec` с после отпускания (потолок `Cap` с — тогда `Fn` всё равно: проверки покажут).
//   Смысл и число проверок не меняются: меняется только КОГДА проверка читает состояние. Только для самотестов: ничего не шлёт.
//
//   КАК ПОДКЛЮЧАЕТСЯ. В контексте прогона — поле `TSharedPtr<AsdaSelfTestSeq::FSeq> Seq;` (рядом с `W`), а вместо
//   `using AsdaSelfTestCtx::At;` — `using AsdaSelfTestSeq::At;`. Вызовы `At(C, t, …)` не меняются.
#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"

#include "UI/AsdaWindowSubsystem.h"

namespace AsdaSelfTestSeq
{
	// Заказанный клик/перетаскивание ещё не доставлен (нет отпускания). Нет мира/подсистемы — нет.
	inline bool InputPending(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		return WM && WM->IsSimClickPending();
	}

	// Очередь шагов одного прогона.
	struct FSeq
	{
		struct FStep
		{
			double T = 0.0;                  // время расписания (с от старта)
			int32 Order = 0;                 // порядок постановки (при равном T — как у таймеров)
			TFunction<void()> Fn;            // шаг
			TFunction<bool()> Gate;          // ворота (пусто — нет)
			double GateCap = 0.0;            // потолок ворот, с
		};
		static constexpr int32 kQuietFrames = 3;     // кадров после отпускания заказанного ввода
		static constexpr double kAfterInput = 0.25;  // не меньше этих секунд после отпускания (и не больше промежутка шага)
		static constexpr double kInputCap = 20.0;    // потолок ожидания доставки заказанного ввода, с (REVIEW6 М-2)
		static constexpr double kOnTime = 0.1;       // опоздание шага до этих секунд — «в срок» (якорь сдвигается на срок)

		TWeakObjectPtr<UWorld> W;        // мир прогона
		double AfterInputSec = kAfterInput;   // ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8) (МЕЛК-3): пауза после отпускания ввода (не больше промежутка шага)
		TArray<FStep> Pending;           // шаги, по возрастанию T
		int32 NextOrder = 0;             // счётчик порядка
		double CurT = 0.0;               // время расписания последнего выполненного шага
		double LastAt = 0.0;             // якорь: реальное время последнего выполненного шага (или старта очереди)
		double InputDoneAt = 0.0;        // реальное время отпускания последнего заказанного ввода
		double InputSince = 0.0;         // с какого момента ждём доставки текущего ввода
		double GateSince = 0.0;          // с какого момента держат ворота первого шага (0 — не держат)
		int32 Quiet = 0;                 // кадров после отпускания
		bool bInFlight = false;          // заказанный ввод сейчас идёт
		bool bInputCapped = false;       // ожидание текущего ввода упёрлось в потолок (больше его не ждём)
		bool bInputSinceStep = false;    // после последнего шага был заказанный ввод
		bool bTicking = false;           // тикер очереди поставлен

		// Поставить шаг через Sec с после текущего шага расписания (не меньше 0,01 с — как `AsdaSelfTestCtx::At`).
		void Add(float Sec, TFunction<void()> Fn, TFunction<bool()> Gate = nullptr, float GateCap = 0.f)
		{
			FStep S;
			S.T = CurT + FMath::Max(0.01f, Sec);
			S.Order = NextOrder++;
			S.Fn = MoveTemp(Fn);
			S.Gate = MoveTemp(Gate);
			S.GateCap = GateCap;
			int32 I = Pending.Num();
			while (I > 0 && Pending[I - 1].T > S.T) { --I; }   // устойчиво: равные T — в порядке постановки
			Pending.Insert(MoveTemp(S), I);
		}

		// Кадр очереди: выполнить все готовые шаги. false — очередь пуста или мира нет (тикер снимается).
		bool Tick()
		{
			UWorld* World = W.Get();
			if (!World || Pending.Num() == 0) { Pending.Reset(); bTicking = false; return false; }
			const double Now = FPlatformTime::Seconds();
			for (int32 Guard = 0; Guard < 64 && Pending.Num() > 0; ++Guard)
			{
				const bool bPend = InputPending(World);
				if (!bPend) { bInputCapped = false; }
				if (bPend && !bInputCapped)
				{
					if (!bInFlight) { bInFlight = true; InputSince = Now; }
					if (Now - InputSince < kInputCap) { Quiet = 0; return true; }
					bInputCapped = true;     // не доставлен за потолок — идём дальше, проверки шага покажут
				}
				if (bInFlight) { bInFlight = false; bInputSinceStep = true; InputDoneAt = Now; Quiet = 0; return true; }
				if (bInputSinceStep && Quiet < kQuietFrames) { ++Quiet; return true; }
				const double StepT = Pending[0].T;           // копии: предикат ворот может поставить шаг (REVIEW6 М-3)
				const double Gap = StepT - CurT;
				const double Due = LastAt + Gap;
				if (Now < Due) { return true; }
				if (bInputSinceStep && Now - InputDoneAt < FMath::Min(Gap, AfterInputSec)) { return true; }
				// ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8) (МЕЛК-1): якорь на «сейчас» — если держали ворота или шаг опоздал больше kOnTime (ввод, заминка);
				//   шаг в срок (в том числе после доставленного ввода) сдвигает якорь на срок — опоздания не копятся.
				bool bHeld = false;
				const int32 StepOrder = Pending[0].Order;     // ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8) (МЕЛК-2): какой шаг снимать после ворот
				if (Pending[0].Gate)
				{
					TFunction<bool()> Gate = Pending[0].Gate;
					const double Cap = Pending[0].GateCap;
					if (GateSince <= 0.0) { GateSince = Now; }
					const bool bOpen = Gate();
					if (!bOpen && Now - GateSince < Cap) { return true; }
					bHeld = bHeld || Now - GateSince > 0.0;
					GateSince = 0.0;
				}
				// ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8) (МЕЛК-2): предикат ворот мог поставить более ранний шаг — снимаем ТОТ, чей срок настал.
				int32 Ix = Pending.IndexOfByPredicate([StepOrder](const FStep& S) { return S.Order == StepOrder; });
				if (Ix == INDEX_NONE) { Ix = 0; }
				FStep X = MoveTemp(Pending[Ix]);
				Pending.RemoveAt(Ix);
				CurT = X.T;
				LastAt = (!bHeld && Now - Due <= kOnTime) ? Due : Now;
				bInputSinceStep = false;
				Quiet = 0;
				if (X.Fn) { X.Fn(); }
				if (!W.IsValid()) { Pending.Reset(); bTicking = false; return false; }
			}
			if (Pending.Num() == 0) { bTicking = false; return false; }
			return true;
		}
	};

	// Очередь прогона: создать при первом шаге, поставить тикер (раз в кадр), если не стоит. Мира нет — nullptr.
	inline FSeq* Queue(TSharedPtr<FSeq>& Seq, UWorld* W)
	{
		if (!W) { return nullptr; }
		if (!Seq.IsValid())
		{
			Seq = MakeShared<FSeq>();
			Seq->W = W;
			Seq->LastAt = FPlatformTime::Seconds();
		}
		if (!Seq->bTicking)
		{
			// Очередь пуста и тикер снят (шаг ставят не из шага) — отсчёт от вызова, а не от давно выполненного шага (REVIEW6 М-1).
			if (Seq->Pending.Num() == 0) { Seq->LastAt = FPlatformTime::Seconds(); Seq->bInputSinceStep = false; Seq->Quiet = 0; }
			Seq->bTicking = true;
			TSharedRef<FSeq> Q = Seq.ToSharedRef();
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Q](float) -> bool { return Q->Tick(); }));
		}
		return Seq.Get();
	}

	// Шаг через Sec с после предыдущего шага расписания (см. шапку). Требование к контексту: поля `W` и `Seq`.
	template <class TCtx> void At(const TSharedRef<TCtx>& C, float Sec, TFunction<void()> Fn)
	{
		if (FSeq* Q = Queue(C->Seq, C->W.Get())) { Q->Add(Sec, MoveTemp(Fn)); }
	}

	// Шаг-ворота: через Sec с — держать очередь, пока Pred() ложно (не дольше Cap с), затем Fn.
	template <class TCtx> void When(const TSharedRef<TCtx>& C, float Sec, TFunction<bool()> Pred, float Cap, TFunction<void()> Fn)
	{
		if (FSeq* Q = Queue(C->Seq, C->W.Get())) { Q->Add(Sec, MoveTemp(Fn), MoveTemp(Pred), Cap); }
	}

	// Разовый шаг после доставки заказанного ввода: не раньше Frames кадров и MinSec с после отпускания; потолок Cap с.
	inline void AfterInput(UWorld* W, TFunction<void()> Fn, float MinSec = 0.25f, int32 Frames = 3, float Cap = 20.f)
	{
		if (!W) { return; }
		struct FState
		{
			TWeakObjectPtr<UWorld> W;
			TFunction<void()> Fn;
			double Start = 0.0, DoneAt = 0.0;
			int32 Quiet = 0;
			float MinSec = 0.f, Cap = 0.f;
			int32 Frames = 0;
		};
		TSharedRef<FState> St = MakeShared<FState>();
		St->W = W; St->Fn = MoveTemp(Fn); St->Start = FPlatformTime::Seconds(); St->MinSec = MinSec; St->Cap = Cap; St->Frames = Frames;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([St](float) -> bool
		{
			UWorld* World = St->W.Get();
			if (!World) { return false; }
			const double Now = FPlatformTime::Seconds();
			const bool bCapped = Now - St->Start >= St->Cap;
			if (!bCapped)
			{
				if (InputPending(World)) { St->DoneAt = 0.0; St->Quiet = 0; return true; }
				if (St->DoneAt <= 0.0) { St->DoneAt = Now; return true; }
				if (++St->Quiet < St->Frames || Now - St->DoneAt < St->MinSec) { return true; }
			}
			TFunction<void()> Fn = MoveTemp(St->Fn);
			if (Fn) { Fn(); }
			return false;
		}));
	}
}
