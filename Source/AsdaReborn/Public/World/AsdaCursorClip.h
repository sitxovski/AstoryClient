// Copyright (c) 2026 @sitxovski (Discord)
// FAsdaCursorClip — ★★★ [ИСПРАВЛЕНИЕ] 01.11.2026 КУРСОР НЕ ПОКИДАЕТ ОКНО ИГРЫ (ключ `asda.Input.ConfineCursor`, 0 — прежнее).
//
//   ЗАЧЕМ. Слово владельца 01.11.2026: «мышь должна в любом режиме отображения экрана (полный, безрамочный, окно) блокироваться
//   в окне. Сейчас она свободно его покидает».
//   ОРИГИНАЛ курсор не держит: в импортах `AsdaStory.exe` (`RE/ida/export/imports.tsv`) нет ни `ClipCursor`, ни `SetCapture`,
//   в строках (`strings.tsv`) и декомпиле (`AsdaStory_game.c`, эталон `RE/etalon/*.c`) — тоже; `SetCursorPos`
//   (`game_main.c:3442`, `:3862`) лишь ставит курсор в центр при входе. Поэтому — [ИСПРАВЛЕНИЕ] по решению владельца.
//   ПОРТ ДО ПРАВКИ: все `SetInputMode` порта (логин, выбор персонажа, мир, окна, чат) ставят `EMouseLockMode::DoNotLock`, а
//   `FSceneViewport::OnMouseButtonUp` при `DoNotLock` ещё и снимает лок после каждого клика — курсор свободно уходит из окна.
//
//   КАК УСТРОЕНО. Сторож на тикере ядра (`FTSTicker`: тикает на ВСЕХ экранах — вход, выбор персонажа, мир — без привязки к миру):
//     • ОБЛАСТЬ зажима (экранные пиксели ОС, пересчёт каждый кадр — смена режима/разрешения/перенос окна):
//       полноэкранный и безрамочный — клиентская область (`GetClientRect` + `ClientToScreen`); оконный С РАМКОЙ ОС
//       (`GameEngine.cpp` создаёт окно с рамкой, `bUseBorderlessWindow` у нас не задан) — ВСЁ окно (`GetWindowRect`, с невидимой
//       полосой изменения размера): иначе мышью не дотянуться до заголовка, рамки и крестика (ревизия 01.11). Ключ 2 —
//       клиентская область всегда;
//     • зажим идёт штатным путём Slate (`ICursor::Lock`, та же очередь, что у самого движка) — поэтому снятие лока движком
//       (`SetInputMode` с `DoNotLock`) не перебивает наш зажим навсегда: следующий кадр видит расхождение и зажимает снова;
//     • пока зажим нужен, режим лока вьюпорта = `LockAlways` (иначе `FSceneViewport::OnMouseButtonUp` снимал бы зажим после
//       каждого отпускания кнопки); когда не нужен — возвращается прежний режим;
//     • зажим СНИМАЕТСЯ, когда окно игры не на переднем плане (Alt+Tab, другое окно, системный диалог — в т. ч. своего процесса),
//       свёрнуто, приложение неактивно; возвращается сам при возврате фокуса (подписка на активацию + тот же тик);
//     • в редакторе (PIE) и без окна ОС (`-RenderOffscreen`) не работает — окно редактора рабочее место владельца;
//     • СТОРОЖ-ПОТОК (раз в 100 мс, независим от игрового): если наш зажим стоит, а окно игры не на переднем плане — снимает его
//       прямым `::ClipCursor(nullptr)`. Это закрывает случаи, когда игровой поток стоит и тик не идёт: модальный
//       `FMessageDialog`/`MessageBoxExt` (в игре у него нет общего хука — `FCoreDelegates::ModalMessageDialog` зовётся только в
//       редакторе, `MessageDialog.cpp:159`), Alt+Tab во время блокирующей загрузки. Чужой зажим (другая программа) не трогается —
//       снимается, только если ОС держит ровно наш прямоугольник;
//     • выход (`FCoreDelegates::OnPreExit`, `Shutdown`) снимает зажим синхронно `::ClipCursor(nullptr)` (`ICursor::Lock`
//       асинхронный — на выходе может не успеть).
//   Курсор Alt+Z (скрыт), вращение камеры ПКМ, перетаскивание окон интерфейса, курсор Pick и клики по миру не задеваются:
//   меняется только прямоугольник, по которому Windows пускает курсор.
//   Логика решения — чистые функции `Decide`/`Plan` (самотест `asda.SelfTest.CursorClip` гоняет их, не зажимая мышь владельца).
//
//   ★★★ [ИСПРАВЛЕНИЕ] 05.11.2026 F11 БОЛЬШЕ НЕ ПЕРЕКЛЮЧАЕТ ПОЛНЫЙ ЭКРАН (ключ `asda.Input.F11NoFullscreen`, 0 — прежнее).
//   Слово владельца: «убрать переключение полноэкранного режима с F11». Переключение вешал ДВИЖОК, не наш код:
//   `UGameViewportClient::InputKey` → `TryToggleFullscreenOnInputKey` (`GameViewportClient.cpp:672-683`) при
//   `UInputSettings::bF11TogglesFullscreen` (`DefaultInput.ini:65` = True) переключает режим окна и СЪЕДАЕТ клавишу во вьюпорте —
//   а F11 в игре — команда 28 раскладки (`AsdaKeybindConfig.cpp:85`, VK 122), у оригинала полноэкранного F11 нет вовсе.
//   Тот же страж (запуск, тикер ядра) держит `bF11TogglesFullscreen = !ключ` — смена ключа действует сразу. Alt+Enter оставлен:
//   Enter раскладкой не диспетчеризуется (`AsdaKeybindConfig.cpp:309`), игровой команды на Alt+Enter нет — конфликта нет.
#pragma once

#include "CoreMinimal.h"

struct ASDAREBORN_API FAsdaCursorClip
{
	// Входы решения за кадр (собирает тик; самотест подаёт свои).
	struct FInputs
	{
		bool bKeyOn = true;          // ключ `asda.Input.ConfineCursor` != 0
		bool bClientOnly = false;    // ключ = 2: только клиентская область и в оконном режиме с рамкой
		bool bFramed = false;        // оконный режим с рамкой ОС (заголовок, крестик)
		FIntRect Frame;              // всё окно игры с рамкой в экранных пикселях ОС (`GetWindowRect`)
		bool bEditor = false;        // редактор / PIE
		bool bHasWindow = false;     // окно игры с хэндлом ОС есть
		bool bAppActive = false;     // приложение активно (`FSlateApplication::IsActive`)
		bool bForeground = false;    // окно игры — окно переднего плана ОС (не чужое, не диалог своего процесса)
		bool bMinimized = false;     // окно игры свёрнуто
		FIntRect Client;             // клиентская область окна игры в экранных пикселях ОС
	};

	// Что сделать в этот кадр.
	enum class EAction : uint8 { None, Lock, Unlock };

	// Состояние стража между кадрами.
	struct FState
	{
		bool bApplied = false;       // наш зажим стоит
		FIntRect Rect;               // прямоугольник нашего зажима
	};

	// Прямоугольник зажима для входов: окно с рамкой — всё окно (кроме ключа 2), иначе — клиентская область.
	static FIntRect TargetRect(const FInputs& In);
	// Нужен ли зажим; OutWhy — причина «нет» (или «держим»).
	static bool Decide(const FInputs& In, FString& OutWhy);
	// План кадра: по входам, нашему состоянию и фактическому зажиму ОС (bActualValid — прочитался) → действие; OutRect —
	//   прямоугольник для `Lock`. Состояние меняет только `Commit`.
	static EAction Plan(const FInputs& In, const FState& St, const FIntRect& Actual, bool bActualValid, FIntRect& OutRect, FString& OutWhy);
	// Применить план к состоянию (после исполнения действия).
	static void Commit(FState& St, EAction A, const FIntRect& Rect);

	// Один раз на процесс (зовёт `UAsdaSystemOptionSubsystem::Initialize`): тикер ядра и подписка на активацию окна.
	static void Startup();
	// Снять зажим, вернуть режим лока вьюпорта, отписаться (`UAsdaSystemOptionSubsystem::Deinitialize`).
	static void Shutdown();
	// Сторож запущен.
	static bool IsStarted();
	// Наш зажим стоит сейчас.
	static bool IsApplied();
	// Собрать входы с живого окна (самотест сверяет путь `-RenderOffscreen`).
	static FInputs GatherInputs();
	// Снять зажим СЕЙЧАС синхронно (`::ClipCursor(nullptr)`), если ОС держит наш прямоугольник; Why — для журнала.
	static void ReleaseNow(const TCHAR* Why);
	// Привести `UInputSettings::bF11TogglesFullscreen` к ключу `asda.Input.F11NoFullscreen` (зовут Startup и каждый кадр стража).
	static void ApplyFullscreenKeys();
	// Сейчас F11 переключает полный экран движком (самотест).
	static bool IsF11FullscreenOn();
	// Сторож-поток запущен (самотест).
	static bool IsWatchdogRunning();
	// Строка состояния для журнала/самотеста (последняя причина, зажим, режим лока вьюпорта).
	static FString Describe();
};
