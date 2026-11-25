// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaSystemOptionSubsystem — полный разбор обоих окон в шапке
//   `Public/UI/AsdaSystemOptionSubsystem.h` (там же — эталонные адреса, дефекты и поправки к докам).
#include "UI/AsdaSystemOptionSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaEscChain.h"   // ★ 25.10.2026: шаг «ESC снимает выбранную цель» эталонной цепочки (между проходом окон и меню 69400)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaLocIds.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaShowOffSubsystem.h"
#include "UI/AsdaChatBalloonSubsystem.h"
#include "UI/AsdaAfterDeathSubsystem.h"   // ★ гейт ESC, ветка 1: принудительный экран смерти
#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Resources/AsdaBenchSubsystem.h"   // ★ 12.10.2026: «Автоподбор» = замер настоящего кадра на месте
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.09.2026: профиль игрока — штатное хранилище настроек окна
#include "Resources/AsdaSoundSubsystem.h"      // ★ 19.09.2026: громкость — через профиль, мимо приоритетов ключей консоли
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaKeybindProfile.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "AsdaNetSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Text/AsdaLang.h"   // ★ 09.10.2026: строка «Язык» (мультиязычность)
#include "Scalability.h"   // ★ фаза 2: уровни групп качества при открытии окна
#include "Test/AsdaSelfTestKit.h"   // ★ 12.10.2026: самотест asda.SysOpt.SelfTest (аргументы, отказ онлайн, итог)
#include "World/AsdaWorldSubsystem.h"   // ★ 18.10.2026: самотест строки «Цифры урона» — GetDamageTextMode/ShouldShowDamageText
#include "UnrealClient.h"                // ★ 18.10.2026: FScreenshotRequest — кадры окна в самотесте «Цифры урона»
#include "Misc/FileHelper.h"             // ★ 18.10.2026: чтение GameUserSettings.ini в самотесте «Цифры урона»
#include "Containers/Ticker.h"           // ★ 18.10.2026: пошаговый самотест «Цифры урона»
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окон 69400/69500
#include "World/AsdaCursorClip.h"           // ★ 01.11.2026: курсор не покидает окно игры
#include "World/AsdaOrbitCursor.h"          // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "World/AsdaCursorSpeed.h"          // ★ 19.10.2026: «Чувствительность мыши» (строка I2) — скорость курсора
#include "AsdaSystemOptionSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaSystemOptionSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaSysOpt);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ═══════════════════════════ ПУТИ АССЕТОВ ══════════════════════════════════════════════════════
//   Оба WBP уже лежат в проекте (импорт .JUI). Новых мы не создаём и эти не перегенерируем.
static const TCHAR* kMenuWbpPath   = TEXT("/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_MenuWIN_NEW");

// Имена окон в `UAsdaWindowSubsystem` = строковые `ID` контролов-окон в .JUI. По ним же
//   оконная подсистема находит запись JUI-меты (ручки перетаскивания `EVS{EVT=6, RET=0x20003}`).
static const FName kMenuWindowId   = FName(TEXT("SYSTEM_MenuWIN_NEW"));

namespace SysOptLoc {
// ═══════════════════════════ LOC-ИДЕНТИФИКАТОРЫ ════════════════════════════════════════════════
//   Все — по ОБРАБОТЧИКУ эталона, а не «по семье» (закон `loc-per-handler-not-by-family`).
	// Модалки ESC-меню (`CItem_BuildTooltipText@0xD6E790`, item_inventory.c:51593/51618).
	static constexpr int32 kExitGameText    = 2100;    // «Выйти из игры?»            (msgClass 3)
	static constexpr int32 kRestartText     = 2102;    // «Перезапустить игру?»       (msgClass 4)
	static constexpr int32 kNoExitInBattle  = 108106;  // «Во время боя выход из игры невозможен.»

	// ★ 26.10.2026 (ревизия wave_1025d 2.2) [ИСПРАВЛЕНИЕ]: строка порта — перенос подбора с Пробела на F не удался (F занята).
	static constexpr int32 kPickupUnbound = 990835;

	static constexpr int32 kGrpFoliage    = 990324;   // «Растительность» — строка снята 12.10.2026, loc сохранён
	static constexpr int32 kGrpView       = 990325;   // «Детализация вдали» — строка снята 12.10.2026, loc сохранён

	static constexpr int32 kGrpGI         = 990327;   // «Глобальное освещение» — строка снята 12.10.2026, loc сохранён

	static constexpr int32 kAniso         = 990338;   // «Анизотропная фильтрация» — строка снята 12.10.2026, loc сохранён
	static constexpr int32 kAO            = 990339;   // «Затенение окружения» — строка снята 12.10.2026, loc сохранён

	static constexpr int32 kLensFlare     = 990341;   // «Блики объектива» — строка снята 12.10.2026, loc сохранён
}

// true — `ApplyInterfaceStateToGame` зовёт накат нового мира (Tick), а не «ОК»/«Применить» окна. Только игровой поток.
bool GSysOptWorldApply = false;

static ASDA_DEV_CVAR(int32, CVarLegacyMenuAsym,
	TEXT("asda.SysOpt.LegacyMenuAsym"), 1,
	TEXT("Дефект Д1 оригинала: ESC-меню РАЗРЕШАЕТ кнопки трея при закрытии, но при открытии их\n")
	TEXT("никто не запрещает. 1 = как оригинал (умолчание), 0 = симметрично (запрет при открытии)."),
	ECVF_Default);

// ★ Четырёхчастный гейт тоггла ESC-меню (разбор — в шапке `IsEscMenuGateBlocked`).
//   1 = как оригинал (умолчание). 0 = гейт снят: ESC тоггает меню всегда — нужен, чтобы
//   отличить «загейтили лишнего» от настоящей поломки меню, не пересобирая проект.
ASDA_DEV_CVAR(int32, CVarEscGate,
	TEXT("asda.SysOpt.EscGate"), 1,
	TEXT("Гейт ESC-меню 69400 как в оригинале (`inputHandleInventoryAndEscHotkeys@0xC6E710`):\n")
	TEXT("экран смерти / перетаскивание предмета / модальное окно. 1 = как оригинал, 0 = снять гейт."),
	ECVF_Default);

// ★ 25.10.2026 [ОРИГИНАЛ]: клавиша, нажатая в захвате, игровую команду/ход/прыжок не исполняет (разбор у `IsVkHeldFromCapture`).
// ★ 26.10.2026 (ревизия wave_1025d 2.1) [ИСПРАВЛЕНИЕ ПОРТА]: набор клавиш, зажатых в момент захвата, чистится для ВСЕХ клавиш —
//   отпущенная клавиша без команды (её опрос раскладки `PollKeys` не видит) снимается в `PollHotkeys`, весь набор — при сбросе сессии.
//   Прежде такая клавиша оставалась в наборе навсегда, и первое её нажатие после назначения съедалось. 0 — как 25.10.
ASDA_DEV_CVAR(int32, CVarAsdaKeysCaptureHeldCleanup,
	TEXT("asda.Keys.CaptureHeldCleanup"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — клавиши, зажатые в момент захвата, снимаются из набора «съесть до отпускания» по отпусканию и для клавиш ")
	TEXT("без команды, весь набор — при сбросе сессии; 0 — как 25.10 (клавиша без команды оставалась в наборе)."),
	ECVF_Default);

bool FAsdaOptState::operator==(const FAsdaOptState& O) const
{
	for (int32 i = 0; i < 7;  ++i) { if (Int1[i] != O.Int1[i]) { return false; } }
	for (int32 i = 0; i < 6;  ++i) { if (Int2[i] != O.Int2[i]) { return false; } }
	for (int32 i = 0; i < 10; ++i) { if (Int3[i] != O.Int3[i]) { return false; } }
	for (int32 i = 0; i < 9;  ++i) { if (D3[i]   != O.D3[i])   { return false; } }
	for (int32 i = 0; i < 8;  ++i) { if (Groups[i] != O.Groups[i]) { return false; } }
	return MusicMuted == O.MusicMuted && SfxMuted == O.SfxMuted
		&& MusicVolume == O.MusicVolume && SfxVolume == O.SfxVolume
		&& WindowMode == O.WindowMode && Gamma == O.Gamma
		&& ScreenW == O.ScreenW && ScreenH == O.ScreenH && ScreenBpp == O.ScreenBpp
		&& GlowLevel == O.GlowLevel
		&& RenderScale == O.RenderScale && FpsLimit == O.FpsLimit && VSync == O.VSync && RhiDx12 == O.RhiDx12
		&& Lang == O.Lang && DamageTextMode == O.DamageTextMode && CursorSpeed == O.CursorSpeed && CameraTurn == O.CameraTurn
		&& CrouchMode == O.CrouchMode
		&& PresetPick == O.PresetPick && DrawDistance == O.DrawDistance && AAMethod == O.AAMethod
		&& SharpenX100 == O.SharpenX100 && Aniso == O.Aniso && AO == O.AO && MotionBlur == O.MotionBlur
		&& LensFlare == O.LensFlare && Clouds == O.Clouds;
}

// ═══════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════════
void UAsdaSystemOptionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	// ★★★ 10.10.2026 [ОРИГИНАЛ] (ревизия `owner_1010/overlaps_fixes2_REVIEW.md`, п. 2): ESC ЗАКРЫВАЕТ 69400 И 69500 ПЕРВЫМ,
	//   ЕСЛИ ОНИ ВЕРХНИЕ. Эталон `uiContainerHandleKey@0x12AF390` (ui_core.c:104068) ловит ESC до раздачи детям и шлёт
	//   событие 21 первому видимому корневому окну с признаком `CControl+0x228`; обоим окнам признак ставит
	//   `uiNoticeDialogInitControls@0xED3080` (ui_core.c:88079/88081). Событие 21 → `uiOptionsWindow_WndProc` (ui_core.c:
	//   88592-88599): 69400 → `sub_93C2AD`, 69500 → `optionsWnd_ApplyAndSave(this, 0)`. Без росписи проход порта ПРОПУСКАЛ
	//   эти окна и закрывал ESC-окно ПОД ними (инвентарь под настройками). Отмена захвата клавиши остаётся ДО прохода
	//   (`PollHotkeys`), ветка `IsOptionsShown → CloseOptions` там — запасная при `asda.UI.EscCloseTopWindow 0`.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->RegisterEscCloser(kMenuWindowId,   FSimpleDelegate::CreateUObject(this, &UAsdaSystemOptionSubsystem::EscHideMenu));
		WM->RegisterEscCloser(kOptionWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaSystemOptionSubsystem::EscCloseOptions));
	}
	for (int32 t = 0; t < 3; ++t)
	{
		for (int32 p = 0; p < kMaxPages; ++p)
		{
			const int32 k = PageSlot(t, p);
			OptSubPage[k] = nullptr; OptListPlane[k] = nullptr; OptListBtn[k] = nullptr; OptListText[k] = nullptr;
			bOptListBrushValid[k] = false;
		}
	}
	for (int32 p = 0; p < 4; ++p) { HotkeyPageHost[p] = nullptr; }
	BuildCheckTable();
	// Настройки живут дольше сессии: читаем их сразу, чтобы игра стартовала с тем, что игрок выбрал.
	LoadStateFromConfig();
	bStateLoaded = true;
	// ★★★ 25.10.2026 [ОРИГИНАЛ] Блок опций персонажа из 1027 → буфер окна (`optionsApplyServerFlags@0xED1B40`).
	if (UAsdaNetSubsystem* NetS = Collection.InitializeDependency<UAsdaNetSubsystem>())
	{
		ServerOptionFlagsHandle = NetS->OnServerOptionFlags.AddUObject(this, &UAsdaSystemOptionSubsystem::ApplyServerOptionFlags);
	}
	// ★ 19.10.2026: «Чувствительность мыши» действует с первого кадра, без открытия окна (и долг прошлого запуска — вернуть).
	FAsdaCursorSpeed::Startup();
	// ★ 01.11.2026 [ИСПРАВЛЕНИЕ]: курсор не покидает окно игры (страж на тикере ядра — все экраны, ключ `asda.Input.ConfineCursor`).
	FAsdaCursorClip::Startup();
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: при зажатой ПКМ курсор скрыт и стоит на месте (сторож на тикере ядра, ключ `asda.Camera.OrbitCursorLock`).
	FAsdaOrbitCursor::Startup();
	// ★★★ 19.09.2026: СТАРТОВЫЙ НАКАТ — ИЗ ПРОФИЛЯ ИГРОКА, А НЕ ЧЕРЕЗ КЛЮЧИ КОНСОЛИ.
	//   Громкость и «заглушить» раздаёт сам профиль (`UAsdaGameUserSettings::ApplyAudioSettings`):
	//   в самостоятельной игре его и так зовёт движок из `ApplyNonResolutionSettings` при старте, а
	//   здесь накат повторяется для запуска из редактора (PIE), где движок этого не делает.
	//   Прежде здесь стояли `IConsoleVariable::Set(…, ECVF_SetByGameSetting)` — и любое значение,
	//   однажды заданное из консоли (история владельца хранит `asda.Sound.MusicVolume 0`), глушило их
	//   молча: ключ с приоритетом «консоль» не принимает записей ниже рангом.
	//   ⚠ Яркость в редакторе НЕ накатываем: `GEngine->DisplayGamma` общий с вьюпортами редактора, и
	//   игровая настройка перекрасила бы редактор. В самостоятельной игре её накатывает движок.
	if (UAsdaGameUserSettings* Profile = UAsdaGameUserSettings::Get())
	{
		Profile->ApplyAudioSettings();
		if (!GIsEditor) { Profile->ApplyGammaSetting(); }
	}
	else
	{
		UE_LOG(LogAsdaSysOpt, Warning,
			TEXT("[система] профиль игрока UAsdaGameUserSettings не зарегистрирован (GameUserSettingsClassName) — ")
			TEXT("настройки окна 69500 не будут сохраняться"));
	}

	// ── Консоль (диагностика и проверка «путём игрока», закон `ui-check-game-path-geometry`) ──
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.Dump"),
		TEXT("Сводка ESC-меню 69400 и окна настроек 69500: что найдено в WBP и что лежит в буфере."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogAsdaSysOpt, Display, TEXT("\n%s"), *DebugState());
		}), ECVF_Default));
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.Menu"),
		TEXT("Тогл ESC-меню 69400 (то же, что клавиша ESC и кнопка главного меню 73406)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { ToggleMenu(); }), ECVF_Default));
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.Open"),
		TEXT("Открыть окно настроек 69500 (то же, что кнопка SYSTEM_MENU_btn3 69403)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { ShowOptions(); }), ECVF_Default));
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.Tab"),
		TEXT("asda.SysOpt.Tab <0|1|2> — вкладка окна настроек: 0 интерфейс, 1 система, 2 клавиши."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() > 0) { SelectTab(FCString::Atoi(*Args[0])); }
		}), ECVF_Default));
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.Page"),
		TEXT("asda.SysOpt.Page <вкладка> <страница> — под-страница вкладки окна настроек."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() > 1) { SelectSubPage(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1])); }
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ 19.09.2026: прокрутка страницы клавиш без колеса мыши — для проверки обрезки зоны 69649
	//   «путём игрока» в прогоне без окон (колесо в headless-прогоне не подать).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.HkScroll"),
		TEXT("asda.SysOpt.HkScroll <позиция> — прокрутка текущей страницы вкладки «Горячие клавиши» (шаг 19 px, 0…16 у страницы 0)."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() > 0) { SetHotkeyScroll(FCString::Atoi(*Args[0])); }
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ 10.10.2026: выбор пункта «Язык» без мыши — проверка пути «Настройки → Язык → ОК → Перезапустить» в прогоне
	//   без окон (тот же обработчик, что выбор строки списка; дальше — настоящие клики `asda.UI.SimClickWidget`).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.SetLang"),
		TEXT("asda.SysOpt.SetLang <ru|en> — выбрать язык в списке «Язык» открытого окна настроек (в игру — по «ОК»)."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			EAsdaLang L;
			if (Args.Num() > 0 && FAsdaLang::ParseCode(Args[0], L)) { OnExtraPicked(SysOptExtra::S7_Lang, (int32)L); }
			UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] язык в буфере окна: «%s»"), FAsdaLang::Code((EAsdaLang)State.Lang));
		}), ECVF_Default));
	// ★ 12.10.2026: самотест окна (состав строк после сокращения, ползунок дальности → asda.Cull.Scale, «ОК» → профиль).
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.SelfTest"),
		TEXT("[ПОРТ] Самотест окна настроек 69500: открытие, ровно ожидаемые строки фазы 2 есть (22 записи kRows: 20 контролов + 2 примечания) и снятых 12.10.2026 нет, ")
		TEXT("ползунок «Дальность прорисовки» → буфер → «ОК» → asda.Cull.Scale и профиль; откат значения. [quit]"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args) { RunSelfTest(Args); }), ECVF_Default));
	// ★ 18.10.2026: самотест строки «Цифры урона» путём игрока (клики, геометрия, 0→1→2, «Применить/Отмена/ОК», ini, кадры).
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.SelfTestDmgText"),
		TEXT("[ПОРТ] Самотест строки «Цифры урона» окна настроек 69500 путём игрока: вкладка «Настройки игры» → «Отображение имён», ")
		TEXT("геометрия строки и соседей, клики 0→1→2 + «Применить» → DamageTextMode профиля и GetDamageTextMode сразу, «Отмена», повторное ")
		TEXT("открытие, «ОК», строка в GameUserSettings.ini; возврат режима. [quit] [shot=<папка> — кадры окна]"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args) { RunDmgTextSelfTest(Args); }), ECVF_Default));
	// ★ 19.10.2026: самотест строк «Чувствительность мыши» / «…поворота камеры» путём игрока (клики, геометрия, «Применить/Отмена», ini).
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.SelfTestMouse"),
		TEXT("[ПОРТ] Самотест строк I2/I3 окна настроек 69500 («Разное»): полосы и подписи, геометрия против соседей, клики по стрелкам → буфер, ")
		TEXT("«Применить» → поля профиля CursorSpeedPercent/CameraTurnPercent и ini, скорость Windows без окна не тронута, «Отмена», таблица уровней. ")
		TEXT("[quit] [shot=<папка>]"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args) { RunMouseSelfTest(Args); }), ECVF_Default));
	// ★ 25.10.2026: самотест строки «Присед» путём игрока (клики, геометрия, «Нажатие»↔«Зажатие», «Применить/Отмена/ОК», ini).
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.SysOpt.SelfTestCrouch"),
		TEXT("[ПОРТ] Самотест строки I4 «Присед» окна настроек 69500 («Разное») путём игрока: подпись и пункты, геометрия против соседей, ")
		TEXT("клики «Зажатие»/«Нажатие» + «Применить» → CrouchMode профиля и GetCrouchMode сразу, «Отмена», повторное открытие, «ОК», ")
		TEXT("строка в GameUserSettings.ini; возврат режима. [quit] [shot=<папка>]"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args) { RunCrouchModeSelfTest(Args); }), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	UE_LOG(LogAsdaSysOpt, Log, TEXT("[система] подсистема поднята: чекбоксов в таблице %d"), Checks.Num());
}

void UAsdaSystemOptionSubsystem::Deinitialize()
{
	FAsdaCursorSpeed::Shutdown();   // ★ 19.10.2026: вернуть скорость указателя Windows
	FAsdaOrbitCursor::Shutdown();   // ★ 07.11.2026: снять захват курсора при зажатой ПКМ (до зажима: нужна Slate и окно)
	FAsdaCursorClip::Shutdown();    // ★ 01.11.2026: снять зажим курсора в окне
	for (IConsoleObject* Cmd : ConsoleCmds)
	{
		if (Cmd) { IConsoleManager::Get().UnregisterConsoleObject(Cmd); }
	}
	ConsoleCmds.Reset();
	// Роспись ESC снимается вместе с подсистемой (образец — `UAsdaDisassemblySubsystem::Deinitialize`).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterEscCloser(kMenuWindowId);
		WM->UnregisterEscCloser(kOptionWindowId);
	}
	ResetMenuWidget();
	ResetOptionWidget();
	// ★ 25.10.2026: подписка на блок опций 1027.
	if (UAsdaNetSubsystem* NetS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		NetS->OnServerOptionFlags.Remove(ServerOptionFlagsHandle);
	}
	ServerOptionFlagsHandle.Reset();
	Super::Deinitialize();
}

void UAsdaSystemOptionSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Виджеты обоих окон принадлежат МИРУ: при травеле они мертвы, кэш обязан обнулиться.
	//   Буфер настроек живёт дальше — он персональный и уже сохранён в конфиг.
	if (!AsdaSessionResetKeepsWorldAlive(Reason))
	{
		ResetMenuWidget();
		ResetOptionWidget();
	}
	// Захват клавиши — состояние ОТКРЫТОГО окна, переживать травел он не должен.
	bCapturing = false;
	CaptureAction = -1;
	// ★ 26.10.2026 (ревизия wave_1025d 2.1): клавиши, «съедаемые до отпускания», — тоже (новый контроллер их нажатий не видел).
	if (CVarAsdaKeysCaptureHeldCleanup.GetValueOnGameThread() != 0) { CaptureHeldVk.Reset(); }
	// ★ 26.10.2026 (ревизия wave_1025d 3.1) [ОРИГИНАЛ]: «кто меня ударил» веток ESC — поле персонажа эталона, при любой причине сброса.
	AsdaEscChain::ResetSelfHitState(AsdaSessionResetReasonName(Reason));
	// Новый мир — новый контроллер: флаги «Интерфейса» обязаны доехать и до него (см. `Tick`).
	InterfaceAppliedPC.Reset();
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[система] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
}

void UAsdaSystemOptionSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.SystemOption");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ── Подключение к кнопке главного меню 73406 (один раз за жизнь подсистемы) ──
	//   ★ Подключений ДВА и они разные: `OnMenuCommand` — команда кнопки, `RegisterWindow` —
	//   ТОЛЬКО подсветка кнопки по видимости окна. Журнал заданий когда-то сделал лишь второе,
	//   и кнопка не открывала окно (грабля из шапки `AsdaSystemMenuSubsystem.h`).
	if (!bMenuBound)
	{
		if (UAsdaSystemMenuSubsystem* Menu = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr)
		{
			Menu->OnMenuCommand.AddDynamic(this, &UAsdaSystemOptionSubsystem::OnMenuCommand);
			Menu->RegisterWindow(73406);   // подсветка кнопки по видимости SYSTEM_MenuWIN_NEW (69400)
			bMenuBound = true;
			UE_LOG(LogAsdaSysOpt, Log, TEXT("[система] кнопка меню 73406: команда + подсветка подключены"));
		}
	}

	// ── ★ 19.09.2026: СОХРАНЁННЫЕ ФЛАГИ «ИНТЕРФЕЙСА» — В КАЖДЫЙ НОВЫЙ ИГРОВОЙ МИР ──
	//   Владельцы этих флагов (хотбар, облачка, контроллер с «всегда бежать») появляются только с
	//   миром. Раньше сохранённое доезжало до них лишь по «ОК/Применить», а при открытии окна
	//   `LoadStateFromGame` читал живые умолчания владельцев и ЗАТИРАЛ ими сохранённое — после
	//   перезапуска игры такие настройки «сбрасывались». Раздаём один раз на каждый новый контроллер.
	if (const UGameInstance* GIa = GetGameInstance())
	{
		UWorld* Wa = GIa->GetWorld();
		APlayerController* PCa = Wa ? Wa->GetFirstPlayerController() : nullptr;
		if (PCa && Cast<AAsdaPlayerController>(PCa) && InterfaceAppliedPC.Get() != PCa)
		{
			InterfaceAppliedPC = PCa;
			GSysOptWorldApply = true;    // ★ ревизия 3.1: накат нового мира — галки хранителей не гоним (шапка у `AsdaSoulFxSingleSourceOn`)
			ApplyInterfaceStateToGame();
			GSysOptWorldApply = false;
			UE_LOG(LogAsdaSysOpt, Log, TEXT("[настройки] сохранённые флаги «Интерфейса» разданы новому миру (%s)"),
				*Wa->GetName());
		}
		// ★ 26.10.2026 (ревизия wave_1025d 2.2) [ИСПРАВЛЕНИЕ]: миграция старого файла оставила подбор без клавиши (F занята) —
		//   сказать игроку один раз за процесс, когда в мире есть свой персонаж (чат уже принимает строки).
		if (PCa && PCa->GetPawn() && Cast<AAsdaPlayerController>(PCa))
		{
			static bool bPickupNoticeShown = false;   // один раз за процесс (миграция повторяется на каждой загрузке раскладки)
			const int32 FOwner = UAsdaKeybindConfig::ConsumePickupUnboundNotice();
			if (FOwner != 0 && !bPickupNoticeShown)
			{
				bPickupNoticeShown = true;
				ChatNotice(SysOptLoc::kPickupUnbound);
				UE_LOG(LogAsdaSysOpt, Log, TEXT("[клавиши] нотис %d: подбор без клавиши — F занята «%s»"), SysOptLoc::kPickupUnbound,
					*UAsdaKeybindConfig::GetCmdName(FOwner));
			}
		}
	}

	if (!IsOptionsShown())
	{
		// Окно закрыто — латч мыши сбрасываем, иначе удержанная кнопка не даст фронта при открытии
		//   (грабля B3 общей точки `FAsdaMouseFrame`).
		OptMouse.Reset();
		return;
	}
	OptMouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		// Собственная защёлка страницы — «ползунок схвачен»: без сброса полоса поехала бы за
		//   курсором, как только нотис закроется.
		for (FAsdaOptSlider& S : Sliders) { S.bDragging = false; }
		OptMouse.Reset();
		return;
	}
	TickOptionInput(OptMouse);
}

// ═══════════════════════════ ОКНА: СОЗДАНИЕ И РАЗБОР ═══════════════════════════════════════════
bool UAsdaSystemOptionSubsystem::EnsureMenuWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// ★ Дедуп №2, В1-B (U22, 11.10.2026): каркас «открыть при первом показе» `FAsdaJuiWindowHandle::EnsureLazy` (кэш годен →
	//   true; сброс полей → открыть свёрнутым → разбор контролов → постановка). Порядок прежний, закрывателя у окна нет.
	// Эталонное `Visible = "0"` окна 69400 в .JUI: создаём скрытым, показывает только `ShowMenu`.
	return FAsdaJuiWindowHandle::Succeeded(FAsdaJuiWindowHandle(kMenuWindowId, kMenuWbpPath, kMenuZ).EnsureLazy(*WM, MenuWidget, /*bReady*/true,
		ESlateVisibility::Collapsed,
		[this]() { ResetMenuWidget(); },
		[]() { UE_LOG(LogAsdaSysOpt, Error, TEXT("[ESC-меню] WBP_SYSTEM_MenuWIN_NEW не загрузился (%s)"), kMenuWbpPath); },
		[this](UAsdaWindowSubsystem& Wm)
		{
			SetupMenuControls();
			// Окно 190×204 с анкором «16 50 47 / 32 50 47»: центр рамки в точке 50 % по X и 47 % по Y (не центр экрана — прежний
			//   комментарий был неточен). ★ 08.10.2026 [ОРИГИНАЛ] (window_default_pos.md §6 п.3): этот дефолт WBP уже несёт (импортёр
			//   переносит якорь .JUI), прежнее центрирование сдвигало окно на 3 % экрана вниз. Откат: `asda.UI.JuiDefaultPos 0`.
			if (!UAsdaWindowSubsystem::UseOriginalDefaultPos()) { Wm.CenterWindowOnScreen(MenuWidget, nullptr); }
		}));
}

void UAsdaSystemOptionSubsystem::ResetMenuWidget()
{
	MenuBtnExit = nullptr; MenuBtnRestart = nullptr; MenuBtnOptions = nullptr;
	MenuBtnQuit = nullptr; MenuBtnClose = nullptr;
	MenuWidget = nullptr;
}

// Имена виджетов — ТОЧНЫЕ `idc` контролов .JUI (закон `jui-importer-widget-naming`), снято
//   headless-дампом дерева WBP: 32 узла, все пять кнопок на месте.
void UAsdaSystemOptionSubsystem::SetupMenuControls()
{
	if (!MenuWidget || !MenuWidget->WidgetTree) { return; }

	auto Btn = [this](const TCHAR* Name) -> UButton*
	{
		return Cast<UButton>(MenuWidget->GetWidgetFromName(FName(Name)));
	};
	MenuBtnExit    = Btn(TEXT("SYSTEM_MENU_btn1"));   // 69401 GAME_X_btn
	MenuBtnRestart = Btn(TEXT("SYSTEM_MENU_btn2"));   // 69402 GAME_restart_btn
	MenuBtnOptions = Btn(TEXT("SYSTEM_MENU_btn3"));   // 69403 GAMEOP_btn
	MenuBtnQuit    = Btn(TEXT("SYSTEM_MENU_btn4"));   // 69404 GAMEOUT_btn
	MenuBtnClose   = Btn(TEXT("SYSTEM_MENU_Xbtn"));   // 69419 CloseBtn_18

	auto Bind = [this](UButton* B, int32 Which)
	{
		if (!B) { return; }
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = Which;
		R->Callback = [this](int32 W) { OnMenuButton(W); };
		B->OnClicked.Clear();
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, B);   // ★ 31.10.2026: подписка уже сделана — якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	};
	Bind(MenuBtnExit, 0);
	Bind(MenuBtnRestart, 1);
	Bind(MenuBtnOptions, 2);
	Bind(MenuBtnQuit, 3);
	Bind(MenuBtnClose, 3);   // эталон: 0x10F1C и 0x10F2B — ОДНА ветка

	if (!MenuBtnExit || !MenuBtnRestart || !MenuBtnOptions || !MenuBtnQuit || !MenuBtnClose)
	{
		UE_LOG(LogAsdaSysOpt, Warning,
			TEXT("[ESC-меню] не все кнопки найдены: 69401=%d 69402=%d 69403=%d 69404=%d 69419=%d"),
			MenuBtnExit != nullptr, MenuBtnRestart != nullptr, MenuBtnOptions != nullptr,
			MenuBtnQuit != nullptr, MenuBtnClose != nullptr);
	}
}

// ═══════════════════════════ ESC-МЕНЮ 69400 ════════════════════════════════════════════════════
bool UAsdaSystemOptionSubsystem::IsMenuShown() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsWindowVisible(kMenuWindowId);
}

void UAsdaSystemOptionSubsystem::ShowMenu()
{
	if (!EnsureMenuWidget()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	WM->SetWindowVisible(kMenuWindowId, true);
	// ★ ДЕФЕКТ Д1 ОРИГИНАЛА. `uiPanelToggleOrClearTabs@0xE8FA70` РАЗРЕШАЕТ кнопки трея главного
	//   меню при ЗАКРЫТИИ ESC-меню (`uiPanelClearAllTabFlags`), но при ОТКРЫТИИ их не запрещает —
	//   пара несимметрична. По умолчанию воспроизводим оригинал; `asda.SysOpt.LegacyMenuAsym 0`
	//   включает симметричное поведение (запрет при открытии).
	if (CVarLegacyMenuAsym.GetValueOnGameThread() == 0)
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->SetAllButtonsEnabled(false);
		}
	}
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[ESC-меню] открыто"));
}

void UAsdaSystemOptionSubsystem::HideMenu()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	WM->SetWindowVisible(kMenuWindowId, false);
	// Эталон при закрытии зовёт `uiPanelClearAllTabFlags` = РАЗРЕШИТЬ все кнопки трея
	//   (`CControl_ClearField609@0xC776F0`; в порту это `SetAllButtonsEnabled(true)`).
	if (UAsdaSystemMenuSubsystem* Menu = GI ? GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr)
	{
		Menu->SetAllButtonsEnabled(true);
	}
	UE_LOG(LogAsdaSysOpt, Verbose, TEXT("[ESC-меню] закрыто"));
}

void UAsdaSystemOptionSubsystem::EscHideMenu()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !WM->IsWindowVisible(kMenuWindowId)) { return; }
	// Эталон `sub_93C2AD` → `sub_ED5760` = `CControl_SetVisible(this[0], 0)`: только спрятать меню. Кнопки трея оригинал
	//   при этом не трогает — и при открытии их не запрещал (дефект Д1, `asda.SysOpt.LegacyMenuAsym 1`), поэтому они уже
	//   разрешены. ★ [ПОРТ] В симметричном режиме (`LegacyMenuAsym 0`) `ShowMenu` их запретил — ESC обязан вернуть, иначе
	//   трей остался бы запертым после закрытия меню по ESC.
	WM->SetWindowVisible(kMenuWindowId, false);
	if (CVarLegacyMenuAsym.GetValueOnGameThread() == 0)
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>()) { Menu->SetAllButtonsEnabled(true); }
	}
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[ESC-меню] закрыто по ESC (событие 21 → sub_93C2AD)"));
}

void UAsdaSystemOptionSubsystem::ToggleMenu()
{
	if (IsMenuShown()) { HideMenu(); }
	else               { ShowMenu(); }
}

// Кнопка 73406 панели главного меню. Эталон на ОБОИХ направлениях тоггла (67073 «включили» и
//   67074 «выключили») уходит в ОДНУ функцию `uiPanelToggleOrClearTabs@0xE8FA70`, которая сама
//   решает по видимости окна 69400, что делать. Поэтому здесь важен ФАКТ клика, а не bOpen.
void UAsdaSystemOptionSubsystem::OnMenuCommand(int32 ButtonIdNum, int32 /*TargetWindowId*/, bool bOpen)
{
	if (ButtonIdNum != 73406) { return; }
	if (bOpen) { if (!IsMenuShown()) { ShowMenu(); } }
	else       { if (IsMenuShown())  { HideMenu(); } }
}

void UAsdaSystemOptionSubsystem::OnMenuButton(int32 Which)
{
	switch (Which)
	{
	case 0:   // 69401 — выйти из игры (модалка типа 3)
		AskExitGame();
		HideMenu();
		break;
	case 1:   // 69402 — вернуться к выбору персонажа (модалка типа 4)
		AskReturnToCharSelect();
		HideMenu();
		break;
	case 2:   // 69403 — открыть настройки
		HideMenu();
		ShowOptions();
		break;
	default:  // 69404 / 69419 — просто закрыть
		HideMenu();
		break;
	}
}

// Порт `objStateAllowsRender@0xA2A2F0` от `CClass_GetField142C(&g_InventoryBag)` (= локальный игрок).
//   Мисномер: функция отвечает «персонаж в боевом/занятом состоянии» (номера 1, 7, 8, 9, 33…47,
//   51, 60…67 либо 0 с под-состоянием 10/20). Порт не моделирует ВЕСЬ эталонный набор номеров
//   состояний, поэтому «занят» собирается из того, что порт знает, — ровно как уже сделано в
//   гейте сцены премиум-гачи (`AsdaPremiumGachaSubsystem.cpp`, нотис 6813).
bool UAsdaSystemOptionSubsystem::IsLocalPlayerBusy() const
{
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return false; }
	const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
	if (!PC) { return false; }
	const AAsdaPlayerCharacter* Pawn = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
	return PC->bAutoAttack || PC->bInCombatStance || PC->bCombatStarted
		|| (Pawn && Pawn->IsIncapacitated());
}

void UAsdaSystemOptionSubsystem::AskExitGame()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;

	// ★ Гейт эталона (item_inventory.c:51593-51603): в бою — ОДНОКНОПОЧНЫЙ нотис 108106 и всё.
	//   ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U2): было «нет оконной подсистемы ⇒ молча» (ранний выход до гейта) — стало общая
	//   политика `AsdaNotice::MsgBox` (окна нет ⇒ лента категорией System, канал прежнего вызова).
	if (IsLocalPlayerBusy())
	{
		AsdaNotice::MsgBox(GI, EAsdaMsgCategory::System, Loc(SysOptLoc::kNoExitInBattle), AsdaLoc::kBtnOk);
		UE_LOG(LogAsdaSysOpt, Log, TEXT("[ESC-меню] выход из игры отклонён: персонаж занят (нотис %d)"),
			SysOptLoc::kNoExitInBattle);
		return;
	}
	if (!WM) { return; }   // двухкнопочному вопросу без оконной подсистемы показаться негде (как было)
	WM->ShowGameConfirm(Loc(SysOptLoc::kExitGameText),
		[this](bool bYes) { if (bYes) { DoExitGame(); } },
		/*GaugeGateSec*/ 0.f, AsdaLoc::kBtnOk, AsdaLoc::kBtnCancel, /*очередь: эталон (3, -1) ui_core.c:88659*/-1, 3);
}

void UAsdaSystemOptionSubsystem::AskReturnToCharSelect()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Гейта боя у типа 4 в эталоне НЕТ — ветка `case 4` (item_inventory.c:51618) сразу строит модалку.
	WM->ShowGameConfirm(Loc(SysOptLoc::kRestartText),
		[this](bool bYes) { if (bYes) { DoReturnToCharSelect(); } },
		/*GaugeGateSec*/ 0.f, AsdaLoc::kBtnOk, AsdaLoc::kBtnCancel, /*очередь: эталон (4, -1) ui_core.c:88663*/-1, 4);
}

// Порт `gameReturnToCharSelectTimer@0xED2D90` (game_main.c:12108) — это ВЫХОД ИЗ ИГРЫ:
//   сохранить сундук, отправить раскладки хотбара, `g_GameState = 0` ⇒ главный цикл возвращает 0
//   и приложение завершается.
void UAsdaSystemOptionSubsystem::DoExitGame()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hot->SendAllPageLayouts();   // эталон `hotbar_SendAllLayouts`
	}
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Тот же «чистый выход», что и у возврата к выбору персонажа: без него серверный статус
		//   аккаунта застревает в «в игре» и следующий вход блокируется.
		Net->SendReturnToCharSelect();
	}
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[ESC-меню] выход из игры подтверждён"));
	UKismetSystemLibrary::QuitGame(GI ? GI->GetWorld() : nullptr, nullptr,
		EQuitPreference::Quit, /*bIgnorePlatformRestrictions*/ false);
}

// Порт `gameLeaveToCharSelect@0xED2E10` (game_main.c:12125): сейв, сброс панелей, `g_GameState = 14`.
//   В порту «состояние 14» = травел на карту логина; всё, что эталон гасит поимённо, чистит
//   общая точка `UAsdaSessionResetSubsystem` по `PostLoadMapWithWorld` (причина `LeftWorld`).
void UAsdaSystemOptionSubsystem::DoReturnToCharSelect()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hot->SendAllPageLayouts();
	}
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SendReturnToCharSelect();   // опкод 1028
	}
	HideMenu();
	CloseOptions(false);
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (W)
	{
		// Карта выбора персонажа = `GameDefaultMap` проекта (`Config/DefaultEngine.ini`).
		UGameplayStatics::OpenLevel(W, FName(TEXT("/Game/Map/Login")));
	}
	UE_LOG(LogAsdaSysOpt, Log, TEXT("[ESC-меню] возврат к выбору персонажа подтверждён"));
}
