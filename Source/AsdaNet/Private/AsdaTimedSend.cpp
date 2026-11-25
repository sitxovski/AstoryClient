// Copyright (c) 2026 @sitxovski (Discord)
// Точная отправка к абсолютному дедлайну — разбор и контракт у объявления (`Public/AsdaTimedSend.h`).
#include "AsdaTimedSend.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

static FAsdaTimedSend* GAsdaTimedSend = nullptr;   // создаётся при первом обращении, уничтожается в `Shutdown`
static bool GAsdaTimedSendShutdown = false;         // ★ ревизия сети НИЗК-11: после выгрузки модуля поток не поднимается

FAsdaTimedSend& FAsdaTimedSend::Get()
{
	if (!GAsdaTimedSend)
	{
		GAsdaTimedSend = new FAsdaTimedSend();
		GAsdaTimedSend->bInert = GAsdaTimedSendShutdown;   // после `Shutdown` — инертный: постановки не принимает
	}
	return *GAsdaTimedSend;
}

void FAsdaTimedSend::Shutdown()
{
	GAsdaTimedSendShutdown = true;
	delete GAsdaTimedSend;
	GAsdaTimedSend = nullptr;
}

FAsdaTimedSend::FAsdaTimedSend()
{
	Wake = FPlatformProcess::GetSynchEventFromPool(false);
}

FAsdaTimedSend::~FAsdaTimedSend()
{
	bStop = true;
	if (Wake) { Wake->Trigger(); }
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	{
		FScopeLock L(&QueueLock);
		Queue.Reset();
	}
	if (Wake) { FPlatformProcess::ReturnSynchEventToPool(Wake); Wake = nullptr; }
}

bool FAsdaTimedSend::EnsureThread()
{
	if (Thread) { return true; }
	if (bInert) { return false; }
	// Повышенный приоритет: поток спит почти всё время, а просыпаться обязан вовремя.
	Thread = FRunnableThread::Create(this, TEXT("AsdaTimedSend"), 64 * 1024, TPri_AboveNormal);
	return Thread != nullptr;
}

uint64 FAsdaTimedSend::Schedule(double DeadlineSec, TFunction<void(double)> Action)
{
	if (!Action) { return 0; }
	uint64 Ticket = 0;
	{
		FScopeLock L(&QueueLock);
		if (!EnsureThread()) { return 0; }
		FItem It;
		It.Ticket = NextTicket++;
		It.DeadlineSec = DeadlineSec;
		It.Action = MoveTemp(Action);
		Ticket = It.Ticket;
		int32 At = Queue.Num();
		for (int32 i = 0; i < Queue.Num(); ++i) { if (Queue[i].DeadlineSec > DeadlineSec) { At = i; break; } }
		Queue.Insert(MoveTemp(It), At);
		++QueueVersion;   // ★ ревизия сети НИЗК-8: добор перед дедлайном перечитает голову очереди
	}
	Wake->Trigger();
	return Ticket;
}

bool FAsdaTimedSend::Cancel(uint64 Ticket)
{
	if (Ticket == 0) { return false; }
	{
		FScopeLock L(&QueueLock);
		for (int32 i = 0; i < Queue.Num(); ++i)
		{
			if (Queue[i].Ticket == Ticket) { Queue.RemoveAt(i); ++QueueVersion; Wake->Trigger(); return true; }
		}
	}
	// Не в очереди: либо уже выполнено, либо выполняется сейчас — дождаться конца выполнения.
	FScopeLock R(&RunLock);
	return false;
}

void FAsdaTimedSend::Stop()
{
	bStop = true;
	if (Wake) { Wake->Trigger(); }
}

uint32 FAsdaTimedSend::Run()
{
	while (!bStop)
	{
		double Deadline = 0.0;
		bool bHave = false;
		uint32 SeenVersion = 0;
		{
			FScopeLock L(&QueueLock);
			if (Queue.Num() > 0) { Deadline = Queue[0].DeadlineSec; bHave = true; }
			SeenVersion = QueueVersion.Load();
		}
		if (!bHave) { Wake->Wait(); continue; }   // ждать постановки
		const double Left = Deadline - FPlatformTime::Seconds();
		// ★ Окно точного добора — 20 мс, больше кванта системного таймера Windows (15.6 мс). UE ставит `timeBeginPeriod(1)`
		//   (WindowsPlatformMisc.cpp:1076), но Windows 11 не выполняет запрос разрешения таймера у скрытого/свёрнутого окна
		//   (`-RenderOffscreen`; у игрока — Alt+Tab): ожидание события просыпается с точностью кванта, и окно в 2 мс давало
		//   опоздание до ~14 мс (самотест после перезагрузки: удар 540 мс уходил на +4…+10 мс сверх запаса). Добор уступками
		//   потока — доли миллисекунды; цена — около 2–3 % одного ядра на время боя (стенд ревизора).
		if (Left > kSpinWindowS)
		{
			// Грубое ожидание до «дедлайн − окно» — событие разбудит раньше при новой постановке/отмене/стопе.
			Wake->Wait(FTimespan::FromSeconds(Left - kSpinWindowS));
			continue;   // перечитать голову очереди (могла смениться)
		}
		// Точный добор: уступки потока до наступления дедлайна.
		// ★ Ревизия сети НИЗК-8: постановка/отмена во время добора — перечитать голову очереди (могла появиться более ранняя).
		bool bChanged = false;
		while (!bStop && FPlatformTime::Seconds() < Deadline)
		{
			if (QueueVersion.Load() != SeenVersion) { bChanged = true; break; }
			FPlatformProcess::YieldThread();
		}
		if (bStop) { break; }
		if (bChanged) { continue; }
		FItem It;
		{
			FScopeLock R(&RunLock);   // взять ДО снятия с очереди: отмена, не нашедшая действие в очереди, ждёт его конца здесь
			{
				FScopeLock L(&QueueLock);
				if (Queue.Num() == 0 || Queue[0].DeadlineSec > FPlatformTime::Seconds()) { continue; }   // отменено/подменено
				It = MoveTemp(Queue[0]);
				Queue.RemoveAt(0);
			}
			It.Action(FPlatformTime::Seconds());
		}
	}
	return 0;
}
