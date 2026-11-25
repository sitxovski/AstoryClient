// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ TAsdaNetQueue — ОЧЕРЕДЬ ТЕЛ ВХОДЯЩИХ ПАКЕТОВ «КОЛБЭК КОПИТ, TICK ПРИМЕНЯЕТ» (общая точка порта).
//   ★ N10 аудита дублей №2 (В1, 11.10.2026, `Reports/dedup_audit2/02_cpp_world_net_data.md`): перенесена сюда из
//   `UI/AsdaUiHelpers.h` — очередь нужна и ядру (рыбалка, мир), а ядро не должно зависеть от заголовка интерфейса.
//   `AsdaUiHelpers.h` включает этот файл, поэтому прежние включения продолжают работать без правок.
#pragma once
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"   // FCriticalSection внутри TAsdaNetQueue
#include "Misc/ScopeLock.h"        // FScopeLock там же

// ── ★ Аудит №3 (B10): ОЧЕРЕДЬ «КОЛБЭК ТОЛЬКО КОПИТ, РАБОТА В TICK» ──────────────────────────────
//
// ★★★ ЗАКОН `no-reentrant-net-callbacks`. Разбор входящего потока идёт в `DrainFrames` внутри
//   `UAsdaNetSubsystem::Tick`; тронуть из колбэка виджеты (а тем более отправить пакет или
//   пере-подключиться) — значит войти в сеть повторно и порвать разбор буфера. Поэтому колбэк ТОЛЬКО
//   кладёт тело в очередь, а применяет её `Tick` подсистемы.
//
// ЧТО БЫЛО. Шесть подсистем повторяли одно и то же тело: своё поле `FCriticalSection PendingLock`,
//   `Push` = `{ FScopeLock; Массив.Add(…) }`, `Drain` = `{ FScopeLock; Готово = MoveTemp(Массив);
//   Массив.Reset(); }` + цикл применения. Отличий по существу не было — кроме `UAsdaShowOffSubsystem`,
//   у которого замка не было ВОВСЕ (описано в отчёте волны).
//
// ⚠ ПРО ЗАМОК. Пакеты приходят на ИГРОВОМ потоке (`UAsdaNetSubsystem::Tick` → `Conn->Tick()` →
//   `DrainFrames`), поэтому замок здесь — не синхронизация двух потоков, а страховка на случай, если
//   приём когда-нибудь уедет в свой поток. Снимать его нельзя: цена — ноль, а без него будущий
//   перенос приёма молча испортит массив.
template<typename T>
struct TAsdaNetQueue
{
	// Положить тело в очередь (из колбэка пакета). Перемещающая и копирующая формы — как у прежних
	//   тел: часть колбэков делала `Add(MoveTemp(…))`, часть `Add(Body)` по константной ссылке.
	void Push(T&& Item)
	{
		FScopeLock Lock(&Guard);
		Items.Add(MoveTemp(Item));
	}
	void Push(const T& Item)
	{
		FScopeLock Lock(&Guard);
		Items.Add(Item);
	}

	// Забрать НАКОПЛЕННОЕ целиком и очистить очередь. true — было что забирать.
	//   ★ `Out` перезаписывается только при непустой очереди: так же вело себя прежнее
	//   `if (Pending.Num()) { Ready = MoveTemp(Pending); Pending.Reset(); }`.
	//   `Reset()` (а не `Empty()`) сохраняет выделенную ёмкость — как и раньше.
	bool Drain(TArray<T>& Out)
	{
		FScopeLock Lock(&Guard);
		if (Items.Num() == 0) { return false; }
		Out = MoveTemp(Items);
		Items.Reset();
		return true;
	}

	// Длина очереди — только для диагностики (лог/консольные дампы).
	int32 Num() const
	{
		FScopeLock Lock(&Guard);
		return Items.Num();
	}

	// ★ N10 (аудит дублей №2, В1, 11.10.2026): выбросить из очереди элементы, для которых Pred = true, НЕ трогая
	//   порядок оставшихся. Заменяет трёхшаговку «Drain → RemoveAll → Push обратно» сброса сессии (рыбалка, сумка):
	//   итог тот же (RemoveAll устойчив), но под одним замком. Возврат — сколько выброшено.
	template<typename PredT>
	int32 RemoveIf(PredT&& Pred)
	{
		FScopeLock Lock(&Guard);
		return Items.RemoveAll(Forward<PredT>(Pred));
	}

	// Выбросить накопленное, не применяя (пересоздание окна/выход из мира).
	void Clear()
	{
		FScopeLock Lock(&Guard);
		Items.Reset();
	}

private:
	TArray<T> Items;
	// mutable: `Num()` — константный метод, а замок в нём брать надо.
	mutable FCriticalSection Guard;
};
