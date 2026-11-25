// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ТОЧНАЯ ОТПРАВКА К АБСОЛЮТНОМУ ДЕДЛАЙНУ — исполнитель на отдельном потоке (15.10.2026, ревизия PORT-RHYTHM СРЕДН-2). [ПОРТ]
//   Отчёт: `Reports/anticheat/wave_PORT_attack_rhythm.md`, раздел «после ревизии».
//
// ЗАЧЕМ. Удар 4028 обязан уйти не раньше серверной длительности и не позже её + 5 %. Игровой поток может отправить пакет
//   только внутри кадра: при 20 FPS кадр = 50 мс, а бюджет «+5 %» у удара 900 мс — 45 мс, у ×0.5 (450 мс) — 22 мс. Таймер
//   «от кадра» физически не попадает в бюджет. Здесь — один фоновый поток, который выполняет действие ровно в заданный
//   момент монотонных часов `FPlatformTime::Seconds()` (QPC): ждёт событие с таймаутом до «дедлайн − 20 мс», остаток
//   добирает уступками потока (`YieldThread`). Точность — доли миллисекунды независимо от FPS игры и кванта таймера ОС.
//
// КОНТРАКТ. Действие выполняется на ФОНОВОМ потоке: оно не имеет права трогать UObject/игровое состояние — только
//   потокобезопасные объекты (сокет под замком соединения, атомики результата). Результат игровой поток забирает опросом.
//   Отменённое до срабатывания действие не выполняется; выполняющееся отмена дожидается (замок исполнителя).
#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"

class FRunnableThread;
class FEvent;

class ASDANET_API FAsdaTimedSend : public FRunnable
{
public:
	// Единственный исполнитель процесса (поток создаётся при первой постановке).
	static FAsdaTimedSend& Get();
	// Остановить поток (выгрузка модуля AsdaNet). Невыполненные действия отбрасываются.
	static void Shutdown();

	// Выполнить Action на фоновом потоке в момент DeadlineSec (FPlatformTime). Аргумент Action — фактическое время запуска.
	//   Возврат — номер постановки (для отмены), 0 — не поставлено (поток не поднялся).
	uint64 Schedule(double DeadlineSec, TFunction<void(double FiredAtSec)> Action);
	// Отменить невыполненное действие. true — снято до запуска; false — уже выполнено/выполняется (дождались конца).
	bool Cancel(uint64 Ticket);

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override;

	~FAsdaTimedSend();

private:
	FAsdaTimedSend();
	bool EnsureThread();

	struct FItem
	{
		uint64 Ticket = 0;
		double DeadlineSec = 0.0;
		TFunction<void(double)> Action;
	};
	FCriticalSection QueueLock;     // очередь постановок
	FCriticalSection RunLock;       // выполнение действия (отмена дожидается его конца)
	TArray<FItem> Queue;            // по возрастанию дедлайна
	uint64 NextTicket = 1;
	FEvent* Wake = nullptr;         // новая постановка / отмена / стоп
	FRunnableThread* Thread = nullptr;
	TAtomic<bool> bStop{ false };
	TAtomic<uint32> QueueVersion{ 0 };   // меняется при постановке/отмене (добор перечитывает голову)
	bool bInert = false;                 // после выгрузки модуля — постановки не принимаются (ревизия сети НИЗК-11)
	static constexpr double kSpinWindowS = 0.02;   // окно точного добора перед дедлайном (больше кванта таймера Windows)
};
