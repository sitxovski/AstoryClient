// Copyright (c) 2026 @sitxovski (Discord)
// AsdaOffscreenGpuSync.cpp — ★★★ [ИСПРАВЛЕНИЕ / ПОРТ 11.11.2026, задача D2] ожидание видеокарты в конце кадра при запуске
//   БЕЗ ОКНА (`-RenderOffscreen`: самотесты и общая проверка собранной игры).
//
// ЧТО ПАДАЛО. `Fatal error: D3D12Util.cpp:1062 … pSet->Open() failed … Too many residency sets are open concurrently`
//   (21 запуск из 537 в общей проверке R1+R2; память процесса игры 10–16 ГБ; перед падением 5–12 предупреждений
//   «Descriptor cache ran out of sub allocated descriptor blocks»).
//
// ПРИЧИНА (движок UE 5.8, не игра). Обычная игра в конце каждого кадра ждёт, пока видеокарта закончит предыдущий:
//   `FD3D12DynamicRHI::RHIEndDrawingViewport` (D3D12Viewport.cpp:910–931) → `WaitForFrameEventCompletion`.
//   С ключом `-RenderOffscreen` на Windows создаётся Null-приложение (WindowsPlatformApplicationMisc.cpp:173), Slate рисует
//   в текстуру, а не во viewport (SlateRHIRenderer.cpp:321–325): `FD3D12Viewport` не создаётся, `EndDrawingViewport` не
//   вызывается вовсе (SlateRHIRenderer.cpp:1310–1321) → ожидания кадра НЕТ. Другого ограничителя нет: `FFrameEndSync`
//   держит игровой поток относительно потоков рендера и RHI, но не относительно видеокарты; страховка движка для
//   свёрнутого окна (`tick.MinimizedSyncDrawToGPU`, GameEngine.cpp:2057) не срабатывает — Null-окно считается видимым.
//   Процессор готовит кадры быстрее, чем видеокарта их рисует (особенно когда на одной карте идут две игры), и очередь
//   неисполненных кадров растёт без предела:
//     • память процесса — гигабайты списков команд (отсюда 10–16 ГБ за 10–60 с работы);
//     • блоки дескрипторов возвращаются только после исполнения кадра видеокартой (`FD3D12OnlineDescriptorManager::
//       FreeHeapBlock` → отложенное удаление): 250 блоков (500 000 / 2 000) кончаются → предупреждение и переход на
//       кучи контекста;
//     • поток отправки при нехватке видеопамяти стоит в `ProcessPagingWork` → `WaitForSyncPoint` (d3dx12residency.h),
//       пока видеокарта не доработает; списки команд, тронувшие reserved-ресурсы, держат свой «residency set» открытым до
//       обработки потоком отправки (D3D12Submission.cpp:746; остальные закрывают его в `FD3D12CommandList::Close`);
//       предел библиотеки — 1024 одновременно открытых (`MAX_NUM_CONCURRENT_CMD_LISTS`, d3dx12residency.h:34) → Fatal.
//   Разбор по 382 журналам игр: падение через 9–98 с после старта, в 43 случаях из 45 — когда шли две игры сразу; ещё в 43
//   играх предупреждение о дескрипторах было, а до падения не дошло. Номер теста в пачке и окна магазина/ремесла ни при чём
//   (падали и `Net.SelfTest4024`, `SelfTest.UpdateCheck`); чаще других падали первые по алфавиту тесты пачки — они идут
//   в первые секунды работы.
//
// ПРАВКА. Без окна в конце каждого кадра ставим команду потоку рендера «отправить всё и дождаться видеокарты»
//   (`SubmitAndBlockUntilGPUIdle`) — тот же приём, которым движок страхует свёрнутое окно (GameEngine.cpp:2057–2063).
//   У игрока viewport есть всегда, ожидание делает движок, и эта правка НЕ действует (условие — ключ командной строки
//   `RenderOffScreen`). Редактор без `-game` правка не трогает (`GIsEditor`).
//   Ключ `asda.Render.OffscreenGpuSync`: 1 = ждать (умолчание), 0 = прежнее поведение (очередь без предела).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CVAR — дев-ключ (в Shipping — константа-умолчание)
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/App.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMemory.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include <atomic>

// Журнал правки (итог ожиданий пишется при выходе из игры).
DEFINE_LOG_CATEGORY_STATIC(LogAsdaOffscreenSync, Log, All);

// ★ [ИСПРАВЛЕНИЕ 11.11.2026, D2] 1 = без окна (`-RenderOffscreen`) в конце кадра ждать видеокарту, как движок делает с окном;
//   0 = прежнее поведение (очередь кадров без предела → «Too many residency sets»).
static ASDA_DEV_CVAR(int32, CVarAsdaOffscreenGpuSync,
	TEXT("asda.Render.OffscreenGpuSync"),
	1,
	TEXT("1 = при запуске без окна (-RenderOffscreen) в конце каждого кадра ждать, пока видеокарта исполнит кадр "
	     "(с окном это делает сам движок); 0 = прежнее: очередь кадров растёт без предела, память 10-16 ГБ, "
	     "падение D3D12 «Too many residency sets»."),
	ECVF_Default);

// Число выполненных ожиданий (поток рендера пишет, игровой читает при выходе).
static std::atomic<int64> GAsdaOffscreenSyncCount(0);
// Суммарное время ожиданий, микросекунды (поток рендера пишет, игровой читает при выходе).
static std::atomic<int64> GAsdaOffscreenSyncMicros(0);
// Самое долгое одно ожидание, микросекунды (пишет только поток рендера).
static std::atomic<int64> GAsdaOffscreenSyncMaxMicros(0);

// Запуск без окна? Тот же признак, по которому движок не создаёт цепочку обмена (WindowsD3D12Viewport.cpp:34).
static bool AsdaIsOffscreenRun()
{
	static const bool bOffscreen = FParse::Param(FCommandLine::Get(), TEXT("RenderOffScreen"));
	return bOffscreen;
}

// Конец кадра игрового потока: без окна — команда потоку рендера «дождаться видеокарты».
static void AsdaOffscreenGpuSync_OnEndFrame()
{
	if (CVarAsdaOffscreenGpuSync.GetValueOnGameThread() == 0 || !FApp::CanEverRender())
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(AsdaOffscreenGpuSync)(
		[](FRHICommandListImmediate& RHICmdList)
		{
			const double T0 = FPlatformTime::Seconds();
			{
				// Ожидание видеокарты — простой потока рендера, а не его работа (иначе искажается статистика времени рендера).
				FRenderThreadIdleScope IdleScope(ERenderThreadIdleTypes::WaitingForGPUQuery);
				RHICmdList.SubmitAndBlockUntilGPUIdle();
			}
			const int64 Micros = (int64)((FPlatformTime::Seconds() - T0) * 1000000.0);
			GAsdaOffscreenSyncCount++;
			GAsdaOffscreenSyncMicros += Micros;
			if (Micros > GAsdaOffscreenSyncMaxMicros.load())
			{
				GAsdaOffscreenSyncMaxMicros.store(Micros);
			}
		});
}

// Выход из игры: итог ожиданий и пик памяти процесса одной строкой (для сверки стенда «до/после»; пишется и при ключе 0).
static void AsdaOffscreenGpuSync_OnPreExit()
{
	const int64 N = GAsdaOffscreenSyncCount.load();
	const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
	UE_LOG(LogAsdaOffscreenSync, Log, TEXT("[без окна] ожиданий видеокарты %lld, всего %.1f с, в среднем %.2f мс, самое долгое %.1f мс; память процесса: пик %.0f МБ, сейчас %.0f МБ"),
		N, GAsdaOffscreenSyncMicros.load() / 1000000.0, N > 0 ? (GAsdaOffscreenSyncMicros.load() / 1000.0) / N : 0.0,
		GAsdaOffscreenSyncMaxMicros.load() / 1000.0, Mem.PeakUsedPhysical / (1024.0 * 1024.0), Mem.UsedPhysical / (1024.0 * 1024.0));
}

// Регистрация в конце запуска движка: только игра (не редактор) и только запуск без окна.
static FDelayedAutoRegisterHelper GAsdaOffscreenGpuSyncRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []()
{
	if (GIsEditor || !AsdaIsOffscreenRun())
	{
		return;
	}
	FCoreDelegates::OnEndFrame.AddStatic(&AsdaOffscreenGpuSync_OnEndFrame);
	FCoreDelegates::OnEnginePreExit.AddStatic(&AsdaOffscreenGpuSync_OnPreExit);
	UE_LOG(LogAsdaOffscreenSync, Log, TEXT("[без окна] цепочки обмена нет — в конце кадра ждём видеокарту (состояние ключа %d)"),
		CVarAsdaOffscreenGpuSync.GetValueOnGameThread());
});
