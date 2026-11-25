// Copyright (c) 2026 @sitxovski (Discord)
// Реализация `UAsdaExtraMenuSubsystem` — доп-меню «Прочее» (окно `ETC_NEW_WIN`, IDNum 74600).
// Разбор фичи, состав таблицы, мисномеры эталона и список дефектов — в шапке заголовка.
#include "UI/AsdaExtraMenuSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"   // ★ R4-b (15.11.2026): самотест перетаскивания закрывает за собой рюкзак
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaDataTableCache.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"          // GEngine->Exec / DeferredCommands — шаги самотеста
#include "Engine/GameInstance.h"
#include "TimerManager.h"           // шаги самотеста разнесены таймерами (нужна отрисовка)
#include "UnrealClient.h"           // FScreenshotRequest — ИМЕНОВАННЫЙ кадр вместе со слоем Slate
#include "Login/AsdaButtonRelay.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaPartyFindSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"    // ★ kCmdPet — окно питомца 43000 (волна 2)
#include "UI/AsdaItemUpgradeSubsystem.h" // ★ kCmdUpgrade — окно улучшения снаряжения 20000 (25.09.2026)
#include "UI/AsdaCraftWindowSubsystem.h" // ★ kCmdCraft — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaDisassemblySubsystem.h" // ★ kCmdDisassembly — окно разборки 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaPremiumPostSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaTitleSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"   // cmd 2 «личная лавка» → функция 2/7 (`PlayEmotion`), волна Л1 торговли
#include "UI/AsdaSoulmateSubsystem.h"    // ★ cmd 1 «Меню Друга» → окно поиска 8200 / 5483 (волна 1-А, 06.10.2026)
#include "UI/AsdaFishBookSubsystem.h"    // ★ cmd 9 «книга рыбака» → окно 74000 `itemCollectUiOpen` (рыбалка, волна В3, 08.10.2026)
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): cmd 8 → `targetSelectNearestEntity` (StartAtNearestSpot)
#include "Text/AsdaLangRows.h"   // [ПОРТ] 09.10.2026 (мультиязычность): текст строк таблицы на языке сессии (копия, не сама строка)
#include "Text/AsdaLang.h"       // ★ 10.10.2026: FAsdaLang::OnLanguageChanged — пересборка таблицы при смене языка на лету

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEtc, Log, All);

namespace
{
	// ── окно ─────────────────────────────────────────────────────────────────────────────────────
	const TCHAR* kWinId   = TEXT("ETC_NEW_WIN");                                            // IDNum 74600
	const TCHAR* kWinPath = TEXT("/Game/AsdaAssets/UI/Player_UI/System/WBP_ETC_NEW_WIN");
	// Обычное окно игрока (не приоритетная полоса 8800 — закон `ui-window-z-layers`).
	//   Выше инвентаря (720) окно не поднимаем: эталон держит его в общем `g_pWindowMgr`.
	constexpr int32 kWinZ = 700;

	// Кнопка трея, которая открывает меню (`NEW_ETC_M_BUT`, таб 9).
	constexpr int32 kMenuButtonIdNum = 73409;

	// ── строки в `.JUI` ровно 16 (74701…74716) — предел эталонного цикла ──
	constexpr int32 kJuiRows = 16;
	// Габарит строки из `.JUI` (`SILVER_BAR` `Size = "39 39"`, подпись до x=203) — клик-зона строки.
	constexpr float kRowW = 203.f;
	constexpr float kRowH = 39.f;
	// Ячейка атласа иконок — 32×32 (эталон `DrawTextureRect(ctrl, tex, 32*X, 32*Y, 32, 32)`).
	constexpr int32 kIconCell = 32;

	// ── таблица ──
	const TCHAR* kDtPath = TEXT("/Game/AsdaAssets/Data/DT_ExtraMenu.DT_ExtraMenu");

	// ── НАШИ строки (id > 990000 — своя нумерация, общую таблицу не трогаем) ──
	constexpr int32 kLocNotPorted  = 990100;   // «Эта часть игры ещё не перенесена: [ss1].»
	constexpr int32 kLocEmptyTable = 990101;   // «Таблица доп-меню пуста — показывать нечего.»

	// ── коды команд эталона (`uiOpenWindowDispatch@0xECD570`) ──
	enum : int32
	{
		kCmdTitle        = 0,    // окно званий           `questLogWnd_Open(&g_pCharTitleMgr)`
		kCmdSoulmate     = 1,    // соулмейт «Меню Друга» `CFindSoulmateWin` / op 5483
		kCmdPrivateShop  = 2,    // личный магазин-лоток  `UI_DispatchTargetContextMenu(2, 7)` / op 6300
		kCmdPet          = 3,    // окно питомца          `CRefineWnd_Open` (мисномер)
		kCmdDisassembly  = 4,    // разборка снаряжения   `upgradeUiOnOpen(CDisjoint*)`
		kCmdCraft        = 5,    // создание снаряжения   `tileInteractGateAndOpen(g_pItemMainWnd)`
		kCmdUpgrade      = 6,    // улучшение снаряжения  `uiCombineWindow_Close` (мисномер, это тоггл)
		// ★★★ ВОССТАНОВЛЕНО ЗАХОДОМ «ПОИСК ОТРЯДА» (2026-08-29). В эталоне `case 7` в
		//   `uiOpenWindowDispatch@0xECD570` ОТСУТСТВУЕТ (байтовая таблица переходов `0xECD6B0`
		//   ведёт код 7 прямо в метку `default` `0xECD6A7`), и в RU-таблице этот код никто не
		//   использует. Но за ним стоит ЦЕЛАЯ, полностью написанная подсистема — окно 8500
		//   `Find_Party_WIn` (238 контролов, класс `CFindPartyWnd`, 8 опкодов, живой сервер).
		//   Доказательство соответствия «код 7 = поиск отряда»: удалённая запись таблицы —
		//   строка №2 доп-меню, её подсказка 110927 («…открой окно 'Поиск отряда'») —
		//   ЕДИНСТВЕННАЯ дырка в сплошном ряду 110925…110936, иконка (2,0) атласа `Extra_menu`
		//   (группа из четырёх персонажей) на месте, а код 7 — единственный свободный в
		//   таблице переходов. Разбор — `Reports/extra_menu_partyfind.md` §1.2…§2.
		kCmdPartyFind    = 7,    // поиск отряда (окно 8500) — ВОЗВРАЩЕНО, см. выше
		kCmdFishing      = 8,    // рыбалка               `targetSelectNearestEntity`, отказ 110700
		kCmdFishBook     = 9,    // книга рыбака (74000)  `itemCollectUiOpen`
		kCmdElitePost    = 10,   // элитный ящик (69000)  тоггл `g_pInventoryBagWindow`
		kCmdGathering    = 11,   // добыча                автомат `g_pAntiCheat` (мисномер), op 6752
	};
}

// ═══════════════════════ КЛЮЧИ ОТКАТА ИСПРАВЛЕННЫХ ДЕФЕКТОВ ОРИГИНАЛА ══════════════════════════
// Д1. Эталон крутит РОВНО 16 строк и при отсутствии контрола делает `return 0`, после чего
//   `CExtraMenu_UIInit@0xECD220` показывает модальный `MessageBoxA("ExtraMenu UI Init Error")`.
//   Таблица — ДАННЫЕ, и 17-я запись роняет инициализацию всего доп-меню.
static ASDA_DEV_CVAR(int32, CVarEtcLegacyOverflow,
	TEXT("asda.ExtraMenu.LegacyOverflow"), 0,
	TEXT("Доп-меню «Прочее»: 1 — эталонное поведение при таблице длиннее 16 записей ")
	TEXT("(инициализация окна ПАДАЕТ целиком, как MessageBox оригинала), ")
	TEXT("0 (по умолчанию) — лишние записи отбрасываются со строкой в журнал."),
	ECVF_Default);

// Д2. `loadExtraMenuTable@0xECD2A0` при отсутствии файла лишь пишет в лог движка и оставляет
//   `count = 0`: окно открывается ПУСТЫМ и игроку не говорится ни слова.
static ASDA_DEV_CVAR(int32, CVarEtcLegacySilentEmpty,
	TEXT("asda.ExtraMenu.LegacySilentEmpty"), 0,
	TEXT("Доп-меню «Прочее»: 1 — эталонное молчание при пустой/отсутствующей таблице, ")
	TEXT("0 (по умолчанию) — нотис 990101 и строка ошибки в журнал."),
	ECVF_Default);

// Д3. Проверка диапазона подсказки в `uiListControlOnUiEvent@0xECDC70` (событие 8) написана как
//   `a3 >= 74611 && a3 <= nRecordCount + 74626`: верх считается от ПОСЛЕДНЕГО контрола (74626)
//   вместо ПЕРВОГО (74611) — граница шире истинной на 15 строк.
static ASDA_DEV_CVAR(int32, CVarEtcLegacyTipRange,
	TEXT("asda.ExtraMenu.LegacyTipRange"), 0,
	TEXT("Доп-меню «Прочее»: 1 — эталонная (сдвинутая на +15) верхняя граница строк, у которых ")
	TEXT("спрашивается подсказка, 0 (по умолчанию) — граница по числу записей таблицы."),
	ECVF_Default);

// ★★★ КЛЮЧ ОТКАТА К ЭТАЛОННОМУ КЛЮЧУ ПУНКТА В СЛОТЕ ХОТБАРА (решение владельца 2026-08-30).
//   Эталон кладёт в слот типа 5 ПОРЯДКОВЫЙ НОМЕР строки меню, и он сдвигается при любом
//   изменении состава меню (у нас состав уже изменён — восстановлена запись «Поиск отряда»).
//   По умолчанию порт кладёт УСТОЙЧИВЫЙ код действия пункта и помечает это `SubType = 1`.
//   ⚠ ЧТЕНИЕ старых записей (`SubType = 0`) работает ВСЕГДА и от этого ключа не зависит.
static ASDA_DEV_CVAR(int32, CVarEtcLegacyRowKey,
	TEXT("asda.ExtraMenu.LegacyRowKey"), 0,
	TEXT("Доп-меню «Прочее»: 1 — класть в слот панели ПОРЯДКОВЫЙ НОМЕР строки, как эталон ")
	TEXT("(раскладка поедет при изменении состава меню), 0 (по умолчанию) — устойчивый код действия."),
	ECVF_Default);

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════

void UAsdaExtraMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `ETC_NEW_WIN` (IDNum 74600) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `quickSlotUI_RefreshIconSlots@0xECD870`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(FName(kWinId), FSimpleDelegate::CreateUObject(this, &UAsdaExtraMenuSubsystem::CloseWindow));
	}
	// Оконный менеджер и хотбар обязаны быть подняты ДО нас: у первого мы берём окно, второму
	//   отдаём мост слотов типа 5 (`GetSubsystem` во время чужого `Initialize` может вернуть
	//   ещё не инициализированный экземпляр, и мост тихо потерялся бы).
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaHotbarSubsystem::StaticClass());

	LoadTable();
	// ★ 10.10.2026 (язык на лету, до входа в мир): названия пунктов — заново на новом языке.
	FAsdaLang::OnLanguageChanged().AddWeakLambda(this, [this]() { LoadTable(); });

	// ★ МОСТ ХОТБАРА (слот типа 5 `EAsdaHotbarSlotType::ExtraMenu`). Приёмная сторона уже написана
	//   (`AsdaHotbarSubsystem.cpp` — иконка, активация, подсказка) и ждала только эту таблицу.
	//   Второй копии логики не заводим (закон `port-shared-points`): и клик по строке окна, и
	//   нажатие слота панели идут в ОДИН `ActivateItem`.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaHotbarSubsystem* Hot = GI->GetSubsystem<UAsdaHotbarSubsystem>())
		{
			FAsdaHotbarMiscBridge Bridge = Hot->GetMiscBridge();   // не затираем чужие поля (эмоции)
			// ⚠ Все три ветки разбирают ключ ОДНОЙ точкой `FindByStableKey` — второй копии
			//   трактовки «что лежит в Index» не заводим (иначе иконка и действие разошлись бы).
			Bridge.GetExtraMenuIcon = [this](int32 KeyKind, int32 Key, FString& OutTex, int32& OutX, int32& OutY) -> bool
			{
				const FAsdaExtraMenuRow* R = FindByStableKey(KeyKind, Key);
				if (!R) { return false; }
				OutTex = R->IconAtlas;
				OutX = kIconCell * R->IconX;   // эталон `32 * rec[104]`
				OutY = kIconCell * R->IconY;   // эталон `32 * rec[108]`
				return true;
			};
			Bridge.OpenExtraMenuItem = [this](int32 KeyKind, int32 Key)
			{
				const FAsdaExtraMenuRow* R = FindByStableKey(KeyKind, Key);
				if (!R)
				{
					// Эталон при `array116_LookupByIndexOrField == 0` просто ничего не делает —
					//   и это ровно то поведение, которого требует владелец: лучше НЕ СРАБОТАТЬ,
					//   чем открыть ЧУЖОЙ пункт.
					UE_LOG(LogAsdaEtc, Warning,
						TEXT("[доп-меню] слот панели: ключ (вид %d, значение %d) не разрешился в пункт — ")
						TEXT("действия нет (эталон в этом случае тоже молчит)"), KeyKind, Key);
					return;
				}
				UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] слот панели (вид ключа %d, ключ %d) → «%s», команда %d"),
					KeyKind, Key, *R->Name, R->Cmd);
				DispatchCommand(R->Cmd, R->Name);
			};
			Bridge.ShowExtraMenuTooltip = [this](int32 KeyKind, int32 Key)
			{
				// Эталон `tutorialShowItemNoticePopup@0xECD760`: заголовок = подпись записи,
				//   тело = строка `DescLocId`. Владельца плашки ставит САМ хотбар (он же её гасит).
				const FAsdaExtraMenuRow* R = FindByStableKey(KeyKind, Key);
				if (!R) { return; }
				UGameInstance* G = GetGameInstance();
				UAsdaMiniStateSubsystem* Mini = G ? G->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
				UAsdaTextSubsystem* Txt = G ? G->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
				if (!Mini) { return; }
				const FString Body = Txt ? Txt->GetTextById(R->DescLocId) : FString();
				Mini->ShowSharedTip(R->Name, Body, EAsdaTipOwner::Hotbar);
			};
			Hot->SetMiscBridge(Bridge);
			UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] мост хотбара (слоты типа 5) подключён"));
		}
	}
	UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] подсистема поднята, записей в таблице: %d"), Records.Num());
}

void UAsdaExtraMenuSubsystem::Deinitialize()
{
	FAsdaLang::OnLanguageChanged().RemoveAll(this);   // ★ 10.10.2026: подписка «язык на лету»
	HideRowTip();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.RemoveDynamic(this, &UAsdaExtraMenuSubsystem::OnMenuCommand);
		}
	}
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaExtraMenuSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	HideRowTip();
	bTipShown = false;
	TipRow = -1;
	bEtcDragActive = false;
	RowDragArm.Disarm();
	(void)Reason;
}

void UAsdaExtraMenuSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.ExtraMenu");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★ Покадровый опрос курсора — только пока окно живо (или пока не отпустили начатый захват).
	TickPointer();

	// ★ Подписка ЛЕНИВАЯ: панель меню и её кнопки появляются позже подсистемы.
	//   Подключений ДВА, и они РАЗНЫЕ (грабля из шапки `UAsdaSystemMenuSubsystem`):
	//     `OnMenuCommand` — САМА КОМАНДА кнопки (открыть/закрыть окно);
	//     `RegisterWindow` — ТОЛЬКО подсветка кнопки из фактической видимости окна.
	if (bMenuBound) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaSystemMenuSubsystem* Menu = GI ? GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
	if (!Menu) { return; }
	Menu->OnMenuCommand.AddDynamic(this, &UAsdaExtraMenuSubsystem::OnMenuCommand);
	Menu->RegisterWindow(kMenuButtonIdNum);      // цель `ETC_NEW_WIN` уже прописана в таблице меню
	bMenuBound = true;
	UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] подписка на кнопку трея %d выполнена (таб 9, нотис 9800)"),
		kMenuButtonIdNum);
}

// ═══════════════════════════════════ ТАБЛИЦА ═══════════════════════════════════════════════════

void UAsdaExtraMenuSubsystem::LoadTable()
{
	Records.Reset();
	const UDataTable* Dt = AsdaDt::Cached(kDtPath);
	if (!Dt)
	{
		UE_LOG(LogAsdaEtc, Error,
			TEXT("[доп-меню] таблица %s не найдена — окно будет пустым. Собери её скриптом ")
			TEXT("Tools/asda_build_extra_menu_dt.py из RawAssets/Data/Extra_menu_table.BIN"), kDtPath);
		return;
	}
	// Порядок строк = порядок записей файла; ключ строки — порядковый номер (см. импортёр).
	TArray<FName> Names = Dt->GetRowNames();
	Names.Sort([](const FName& A, const FName& B)
	{
		return FCString::Atoi(*A.ToString()) < FCString::Atoi(*B.ToString());
	});
	for (const FName& N : Names)
	{
		if (const FAsdaExtraMenuRow* R = Dt->FindRow<FAsdaExtraMenuRow>(N, TEXT("доп-меню"), false))
		{
			// ★ [ПОРТ] 09.10.2026 (мультиязычность): подпись пункта — на языке сессии (оверлей `ExtraMenu`, ключ `<строка>.Name`).
			FAsdaExtraMenuRow& Rec = Records.Add_GetRef(*R);
			Rec.Name = AsdaLangRows::Text(Dt, R, TEXT("Name"), R->Name);
		}
	}
	// ★ ДЕФЕКТ Д1 ОРИГИНАЛА. Эталон при `count > 16` не находит контрол 74717 и роняет весь
	//   `CExtraMenu_UIInit` модальным `MessageBoxA`. Мы отбрасываем лишнее и говорим об этом.
	if (Records.Num() > kJuiRows)
	{
		if (CVarEtcLegacyOverflow.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogAsdaEtc, Error,
				TEXT("[доп-меню] ЭТАЛОННОЕ ПОВЕДЕНИЕ (asda.ExtraMenu.LegacyOverflow 1): записей %d > %d ")
				TEXT("мест в окне — эталон здесь падает с MessageBox «ExtraMenu UI Init Error». ")
				TEXT("Окно не строим."), Records.Num(), kJuiRows);
			Records.Reset();
			return;
		}
		UE_LOG(LogAsdaEtc, Warning,
			TEXT("[доп-меню] в таблице %d записей, а мест в окне `.JUI` ровно %d (74701…74716) — ")
			TEXT("лишние %d отброшены (в оригинале это модальный MessageBox и отказ всего меню; ")
			TEXT("вернуть — asda.ExtraMenu.LegacyOverflow 1)"),
			Records.Num(), kJuiRows, Records.Num() - kJuiRows);
		Records.SetNum(kJuiRows);
	}
}

const FAsdaExtraMenuRow* UAsdaExtraMenuSubsystem::FindByIndex(int32 Index) const
{
	return Records.IsValidIndex(Index) ? &Records[Index] : nullptr;
}

// ★ Порядковый номер записи в ИСХОДНОЙ таблице: считаем только НЕвосстановленные записи.
//   Для восстановленной возвращаем −1 — в исходной таблице её не было, и старая раскладка
//   ссылаться на неё физически не могла.
int32 UAsdaExtraMenuSubsystem::OriginalOrdinalOf(int32 Index) const
{
	if (!Records.IsValidIndex(Index)) { return INDEX_NONE; }
	if (Records[Index].Restored) { return INDEX_NONE; }
	int32 N = 0;
	for (int32 I = 0; I < Index; ++I) { if (!Records[I].Restored) { ++N; } }
	return N;
}

// ★★★ ЕДИНАЯ ТОЧКА РАЗБОРА КЛЮЧА ПУНКТА В СЛОТЕ ХОТБАРА (тип 5).
//
//   ВИД 1 (`kExtraKeyCmd`) — НАШ УСТОЙЧИВЫЙ КЛЮЧ, код действия (`rec+68`). Свой нумератор мы НЕ
//   изобретали: этот код уже есть в данных и уже используется эталоном как ключ поиска —
//   `array116_LookupByIndexOrField@0xECD4C0` во втором режиме (`a3 != 1`) ищет запись ИМЕННО по
//   полю +68. Он уникален (в RU-таблице 12 записей — 12 различных кодов 0…11) и лежит далеко в
//   пределах `u16` поля `Index`. Поиск повторяет эталонный режим: побеждает ПОСЛЕДНЯЯ подходящая.
//
//   ВИД 0 (`kExtraKeyRowOrdinal`) — ЭТАЛОННАЯ СТАРАЯ ЗАПИСЬ, порядковый номер строки. Такую
//   раскладку писал ОРИГИНАЛЬНЫЙ клиент, у которого записи «Поиск отряда» в таблице не было,
//   поэтому считать надо по ИСХОДНОЙ нумерации — то есть по записям с `Restored == false`.
//   Именно поэтому пометка `Restored` живёт в ДАННЫХ, а не хардкодом «пропусти индекс 2».
//   ⚠ Развести виды по ВЕЛИЧИНЕ невозможно: и номера строк, и коды действий лежат в 0…11.
const FAsdaExtraMenuRow* UAsdaExtraMenuSubsystem::FindByStableKey(int32 KeyKind, int32 Key) const
{
	if (KeyKind == kExtraKeyCmd) { return FindByCommand(Key); }

	if (KeyKind != kExtraKeyRowOrdinal)
	{
		// Неизвестный вид ключа — НЕ УГАДЫВАЕМ. Лучше не сработать, чем открыть чужой пункт.
		UE_LOG(LogAsdaEtc, Warning,
			TEXT("[доп-меню] слот панели: неизвестный вид ключа %d (знаем 0 = номер строки, 1 = код действия)"),
			KeyKind);
		return nullptr;
	}
	// Старая запись: N-я строка ИСХОДНОЙ таблицы.
	for (int32 I = 0; I < Records.Num(); ++I)
	{
		if (OriginalOrdinalOf(I) == Key) { return &Records[I]; }
	}
	UE_LOG(LogAsdaEtc, Warning,
		TEXT("[доп-меню] старая раскладка: строки №%d в ИСХОДНОЙ таблице нет (живых записей %d) — ")
		TEXT("слот ничего не откроет"), Key, Records.Num());
	return nullptr;
}

const FAsdaExtraMenuRow* UAsdaExtraMenuSubsystem::FindByCommand(int32 Cmd) const
{
	// Эталон `array116_LookupByIndexOrField@0xECD4C0`, режим `a3 != 1`: цикл БЕЗ выхода, поэтому
	//   при дублях побеждает ПОСЛЕДНЯЯ подходящая запись. Повторяем ровно это.
	const FAsdaExtraMenuRow* Found = nullptr;
	for (const FAsdaExtraMenuRow& R : Records)
	{
		if (R.Cmd == Cmd) { Found = &R; }
	}
	return Found;
}

// ═══════════════════════════════════ ОКНО ══════════════════════════════════════════════════════

bool UAsdaExtraMenuSubsystem::EnsureWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	// ★★★ ГВАРД ИМЕННО `IsWindowLive`, А НЕ ПРОВЕРКА НА null: `CloseAll()` (вход в мир, разборка
	//   подсистемы) выкидывает окна из вьюпорта и чистит реестр, но наш `UPROPERTY`-указатель
	//   остаётся ненулевым — без этой проверки окно «открывалось» бы в никуда.
	if (bReady && WM->IsWindowLive(Win)) { return true; }

	// Дерево пересоздаётся ⇒ ВСЕ кэши, живущие в нём, сбрасываются.
	RowGroups.Reset();
	RowLabels.Reset();
	RowIcons.Reset();
	RowGrid = nullptr;        // сетка держит прозрачные кнопки-оверлеи внутри старого дерева
	Relays.Reset();
	bTipShown = false;
	TipRow = -1;
	bReady = false;
	// ★ Призрак — РЕБЁНОК КОРНЕВОГО КАНВАСА окна: дерево пересоздано ⇒ структуру сбрасываем
	//   целиком, иначе следующий захват тянулся бы к узлу в уже мёртвом дереве (шапка `FAsdaDragGhost`).
	RowDragGhost = FAsdaDragGhost();
	RowDragArm.Disarm();
	bEtcDragActive = false;

	Win = WM->OpenWindowByPath(FName(kWinId), kWinPath, kWinZ, ESlateVisibility::SelfHitTestInvisible);
	if (!Win)
	{
		UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] окно 74600 не загрузилось (%s)"), kWinPath);
		return false;
	}
	WM->SetWindowVisible(FName(kWinId), false);   // в `.JUI` у 74600 `Visible = "0"`

	// ★★★ ПРОВЕРЕННОЕ ОТРИЦАТЕЛЬНОЕ (2026-08-29). В headless-прогоне 8-я строка левой колонки не
	//   рисовалась и не ловила мышь, и это ЛЕГКО принять за дефект окна. Разбор показал обратное:
	//   отсекает не окно и не панель строк (у обеих живой прямоугольник полный, `отсечение=0`), а
	//   ГРАНИЦА ВЬЮПОРТА — в прогоне она стояла на y≈362, и она НЕ ДВИГАЛАСЬ при двух совершенно
	//   разных положениях окна (доказательство: строка, сдвинутая `asda.ExtraMenu.MoveRow 7 206 166`
	//   выше границы, рисуется и нажимается). Окно `.JUI` имеет высоту 368 в базе 768 и в вьюпорт
	//   такой высоты не влезает физически. ⇒ править в порту нечего; прогон гоняем в окне побольше
	//   (см. `Tools/asda_extra_menu_live_check.py`, `LevelEditorPlaySettings`).
	//   ⚠ Попытка «вылечить» это растягиванием слоя окна во вьюпорте (`SetAnchorsInViewport`)
	//   ОТКАЧЕНА: граница осталась на том же y≈362, а окно уехало из центра — то есть лечили
	//   не ту болезнь (закон `no-revert-without-asking` — откатили СВОЮ правку, не чужую).
	BuildRows();
	bReady = true;
	return true;
}

// Собрать список строк окна и их узлов. ★ ИМЕНА ВИДЖЕТОВ НЕ ЗАШИВАЕМ: импортёр называет строки
//   неоднородно (`BOX_1_grp`, `___grp`, `SILVER_BAR_grp`, `SILVER_BAR_grp_1`…), потому что поле
//   `ID` у части контролов `.JUI` пустое (закон `jui-importer-widget-naming`). Берём ПОРЯДОК
//   детей-канвасов у `ETC_NEW_Scroll_grp` — он и есть порядок строк 74701…74716.
void UAsdaExtraMenuSubsystem::BuildRows()
{
	if (!Win) { return; }

	// ── крестик 74689 (эталон: `a4 == 66561 && a3 == 74689` → тот же тоггл окна) ──
	if (UButton* X = Cast<UButton>(Win->GetWidgetFromName(TEXT("ETC_NEW_Xbtn"))))
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { CloseWindow(); };
		X->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, X);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	else
	{
		UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] крестик ETC_NEW_Xbtn не найден в WBP"));
	}

	UPanelWidget* Scroll = Cast<UPanelWidget>(Win->GetWidgetFromName(TEXT("ETC_NEW_Scroll_grp")));
	if (!Scroll)
	{
		UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] панель строк ETC_NEW_Scroll_grp не найдена — ")
			TEXT("разметка окна разошлась с `.JUI`"));
		return;
	}
	TArray<FName> RowNames;
	for (int32 i = 0; i < Scroll->GetChildrenCount(); ++i)
	{
		UWidget* C = Scroll->GetChildAt(i);
		if (!C || !C->IsA(UCanvasPanel::StaticClass())) { continue; }   // хит-планы скролла — не строки
		RowGroups.Add(C);
		RowNames.Add(FName(*C->GetName()));

		// Подпись строки — единственный `UTextBlock` шириной 160 (`.JUI`: `Size = "160 18"`).
		// Иконка — первый `UImage` 32×32 (в части строк он лежит в своей группе `*_grp`).
		UTextBlock* Label = nullptr;
		UImage* Icon = nullptr;
		TArray<UWidget*> Stack{ C };
		while (Stack.Num() > 0)
		{
			UWidget* W = Stack.Pop();
			if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(W))
			{
				const FVector2D Sz = S->GetSize();
				if (!Label && W->IsA(UTextBlock::StaticClass()) && FMath::IsNearlyEqual(Sz.X, 160.f, 1.f))
				{
					Label = Cast<UTextBlock>(W);
				}
				if (!Icon && W->IsA(UImage::StaticClass())
					&& FMath::IsNearlyEqual(Sz.X, 32.f, 1.f) && FMath::IsNearlyEqual(Sz.Y, 32.f, 1.f)
					&& W->GetVisibility() != ESlateVisibility::Collapsed)   // отсекает гашеные накладки
				{
					Icon = Cast<UImage>(W);
				}
			}
			if (UPanelWidget* P = Cast<UPanelWidget>(W))
			{
				// В обратном порядке — чтобы Pop() шёл по детям слева направо, как в `.JUI`.
				for (int32 k = P->GetChildrenCount() - 1; k >= 0; --k) { Stack.Add(P->GetChildAt(k)); }
			}
		}
		RowLabels.Add(Label);
		RowIcons.Add(Icon);
		if (!Label || !Icon)
		{
			UE_LOG(LogAsdaEtc, Warning, TEXT("[доп-меню] строка %d («%s»): подпись %s, иконка %s"),
				RowGroups.Num() - 1, *C->GetName(),
				Label ? TEXT("есть") : TEXT("НЕ НАЙДЕНА"), Icon ? TEXT("есть") : TEXT("НЕ НАЙДЕНА"));
		}
	}
	if (RowGroups.Num() != kJuiRows)
	{
		UE_LOG(LogAsdaEtc, Error,
			TEXT("[доп-меню] строк в окне найдено %d, а в `.JUI` их ровно %d (74701…74716)"),
			RowGroups.Num(), kJuiRows);
	}

	// ── клик-зоны: ОБЩАЯ ТОЧКА `UAsdaIconCellGrid`, а не своя копия оверлея ──
	//   Зоны строятся ТОЛЬКО над ЖИВЫМИ строками: эталон гасит строки с `i >= count`, и они
	//   мышь не ловят. Строим по абсолютной позиции в корне (сетка умеет — строки лежат на
	//   третьем уровне вложенности, закон `jui-cell-chain-nesting`).
	const int32 Live = FMath::Min(Records.Num(), RowGroups.Num());
	RowNames.SetNum(Live);
	RowGrid = NewObject<UAsdaIconCellGrid>(this);
	RowGrid->SetOverlayTag(TEXT("ETCROW"));
	RowGrid->SetCellSizePx(kRowW, kRowH);
	RowGrid->SetHoverHandlers(
		[this](int32 Row) { ShowRowTip(Row); },
		[this]()          { HideRowTip(); });
	RowGrid->SetupNamed(Win, RowNames, /*bMale*/true,
		[](int32) { return 0; },                       // иконки рисуем сами из атласа доп-меню
		[this](int32 Row) { ActivateItem(Row); });

	RefreshRows();
}

// Порт `quickSlotUI_RefreshIconSlots@0xECD870`: подпись + иконка каждой живой строки,
//   строки с `i >= count` — погасить.
void UAsdaExtraMenuSubsystem::RefreshRows()
{
	for (int32 i = 0; i < RowGroups.Num(); ++i)
	{
		const FAsdaExtraMenuRow* R = FindByIndex(i);
		UWidget* Grp = RowGroups[i];
		if (!Grp) { continue; }
		if (!R)
		{
			Grp->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}
		Grp->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UTextBlock* T = RowLabels[i])
		{
			T->SetText(FText::FromString(R->Name));
			// ★ Autowrap в канвасе даёт дёрганье — ширина строки фиксирована слотом `.JUI` (160).
			T->SetAutoWrapText(false);
			T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		if (UImage* Img = RowIcons[i])
		{
			// Эталон: `TexturePath_Format(rec + 72)` → атлас, `DrawTextureRect(…, 32*X, 32*Y, 32, 32)`.
			if (!AsdaUi::ApplyJuiIconCell(Img, R->IconAtlas, kIconCell * R->IconX, kIconCell * R->IconY,
				kIconCell, (float)kIconCell))
			{
				UE_LOG(LogAsdaEtc, Warning,
					TEXT("[доп-меню] строка %d «%s»: атлас иконок «%s» не загрузился"),
					i, *R->Name, *R->IconAtlas);
			}
			Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
}

void UAsdaExtraMenuSubsystem::OpenWindow()
{
	if (!EnsureWindow()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }

	// ★ ДЕФЕКТ Д2 ОРИГИНАЛА: при пустой таблице эталон открывает ПУСТОЕ окно и молчит.
	if (Records.Num() == 0)
	{
		if (CVarEtcLegacySilentEmpty.GetValueOnGameThread() == 0)
		{
			UE_LOG(LogAsdaEtc, Error,
				TEXT("[доп-меню] таблица пуста — показывать нечего (в оригинале окно открывалось ")
				TEXT("пустым и молча; вернуть — asda.ExtraMenu.LegacySilentEmpty 1)"));
			if (UAsdaMessageSubsystem* Msg = GI->GetSubsystem<UAsdaMessageSubsystem>())
			{
				Msg->PostLoc(EAsdaMsgCategory::System, kLocEmptyTable);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка («[#id]»)
			}
			return;
		}
		UE_LOG(LogAsdaEtc, Warning, TEXT("[доп-меню] ЭТАЛОННОЕ ПОВЕДЕНИЕ: таблица пуста, ")
			TEXT("окно открывается пустым и без слов"));
	}

	WM->SetWindowVisible(FName(kWinId), true);
	WM->BringWindowToFront(FName(kWinId));
	UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] окно 74600 показано, живых строк %d"), Records.Num());
}

void UAsdaExtraMenuSubsystem::CloseWindow()
{
	HideRowTip();
	// Окно уходит с экрана посреди захвата — снять и призрак, и взвод (эталон гасит призрак
	//   вместе с окном-владельцем: `uiReleaseDragImageWindow@0x12BB9F0`).
	CancelRowDragVisual(RowDragArm.KeyA);
	RowDragArm.Disarm();
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(kWinId), false);
	}
	UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] окно 74600 закрыто"));
}

bool UAsdaExtraMenuSubsystem::IsWindowVisible() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsWindowVisible(FName(kWinId));
}

// Эталон `uiToggleSkillTreePanel@0xECDB70` (мисномер — это тоггл ДОП-МЕНЮ): видно → скрыть,
//   иначе показать; в обоих случаях гасится общая плашка подсказки.
void UAsdaExtraMenuSubsystem::ToggleWindow()
{
	if (IsWindowVisible()) { CloseWindow(); } else { OpenWindow(); }
}

void UAsdaExtraMenuSubsystem::OnMenuCommand(int32 ButtonIdNum, int32 /*TargetWindowId*/, bool bOpen)
{
	if (ButtonIdNum != kMenuButtonIdNum) { return; }
	UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] кнопка трея %d → %s"),
		ButtonIdNum, bOpen ? TEXT("открыть") : TEXT("закрыть"));
	if (bOpen) { OpenWindow(); } else { CloseWindow(); }
}

// ═══════════════════════════════════ ПОДСКАЗКА ═════════════════════════════════════════════════

void UAsdaExtraMenuSubsystem::ShowRowTip(int32 Row)
{
	// ★ ГЕЙТ ЭТАЛОНА: пока идёт захват, подсказка не всплывает вовсе (`if (!helper_getField1(
	//   g_pUIManager))` перед веткой события 8, `ui_core.c:32869`; сам захват гасит её первым делом —
	//   `uiSetFocusIndexFromCmd` зовёт `CControl_KillFocus(&g_ItemTooltip)`). Без гейта плашка
	//   всплывала бы прямо под «призраком»: кнопки-оверлеи сетки продолжают слать наведение.
	if (bEtcDragActive) { return; }
	// ★ ДЕФЕКТ Д3 ОРИГИНАЛА: верхняя граница диапазона считалась от последнего контрола (74626),
	//   а не от первого (74611) — то есть была шире истинной на 15 строк.
	const int32 Limit = (CVarEtcLegacyTipRange.GetValueOnGameThread() != 0)
		? FMath::Min(Records.Num() + kJuiRows - 1, RowGroups.Num())
		: Records.Num();
	if (Row < 0 || Row >= Limit) { return; }
	const FAsdaExtraMenuRow* R = FindByIndex(Row);
	if (!R) { return; }                                   // эталон здесь тоже просто не покажет плашку
	UGameInstance* GI = GetGameInstance();
	UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (!Mini) { return; }
	const FString Body = Txt ? Txt->GetTextById(R->DescLocId) : FString();
	Mini->ShowSharedTip(R->Name, Body, EAsdaTipOwner::ExtraMenu);
	bTipShown = true;
	TipRow = Row;
}

void UAsdaExtraMenuSubsystem::HideRowTip()
{
	if (!bTipShown) { return; }
	bTipShown = false;
	TipRow = -1;
	UGameInstance* GI = GetGameInstance();
	if (UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr)
	{
		// ★ Гасит ТОЛЬКО владелец (закон `ui-shared-tip-ownership`).
		Mini->HideSharedTip(EAsdaTipOwner::ExtraMenu);
	}
}

// ═════════════════ ПЕРЕТАСКИВАНИЕ СТРОКИ В ПАНЕЛЬ БЫСТРОГО ДОСТУПА (слот типа 5) ═══════════════
//   Полный разбор эталона, доказательства «это есть в оригинале» и границы броска — в шапке
//   заголовка, раздел «ПЕРЕТАСКИВАНИЕ СТРОКИ В ПАНЕЛЬ».

// Покадровый опрос курсора. Здесь ТОЛЬКО перетаскивание: наведение (подсказка) и обычный клик
//   маршрутизируют кнопки-оверлеи сетки `UAsdaIconCellGrid`, второго пути активации не заводим.
void UAsdaExtraMenuSubsystem::TickPointer()
{
	// Окно не построено или спрятано, и захвата нет — опрашивать нечего.
	if (!Win || !RowGrid) { return; }
	if (!bEtcDragActive && !IsWindowVisible()) { return; }

	// ★ Общая точка опроса мыши с готовыми ФРОНТАМИ (`FAsdaMouseFrame`) — та же, что у лотка
	//   эмоций, сумки и книги навыков. `static` здесь намеренно: фронты считаются один раз на кадр
	//   на весь процесс, ровно как в `UAsdaCharaInfoSubsystem::TickPointer`.
	static FAsdaMouseFrame Mouse;
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в бросок строки в панель быстрого доступа, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		// Только ВИЗУАЛ захвата: `EndRowDrag` выполнил бы бросок, а под модалкой это отмена.
		if (bEtcDragActive) { CancelRowDragVisual(RowDragArm.KeyA); }
		RowDragArm.Disarm();
		Mouse.Reset();
		return;
	}

	// ── НАЖАТИЕ: взвести захват (эталон событие 5 → `uiSetFocusIndexFromCmd` → возврат 2) ──
	//   Строку под курсором спрашиваем у САМОЙ КЛИК-ЗОНЫ (живая геометрия кнопки-оверлея), а не
	//   считаем по слоту: строки лежат на третьем уровне вложенности, слот про абсолютное
	//   положение врёт (закон `jui-cell-chain-nesting`).
	if (Mouse.bLmbDown)
	{
		const int32 Row = RowGrid->HitTestCellUnderCursor();
		// Захватывать можно только ЖИВУЮ строку: у эталона зона захвата ограничена диапазоном
		//   idnum 74611…74626, а погашенные строки (`i >= count`) мышь не ловят вовсе.
		if (Row >= 0 && FindByIndex(Row) != nullptr)
		{
			RowDragArm.Arm(Row, -1, Mouse.Cursor);
		}
	}

	// ── ПОРОГ ПРОЙДЕН ПРИ ЗАЖАТОЙ КНОПКЕ: поднять «призрак» ──
	if (Mouse.bLmb && RowDragArm.ShouldStart(Mouse.Cursor))
	{
		BeginRowDrag(RowDragArm.KeyA);
	}
	if (bEtcDragActive)
	{
		RowDragGhost.Tick(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	}

	// ── ОТПУСКАНИЕ (эталон событие 7 → `uiHotbarSlotDispatch`) ──
	if (Mouse.bLmbUp)
	{
		const int32 Row = RowDragArm.KeyA;
		const bool  bWasDrag = bEtcDragActive;
		EndRowDrag(bWasDrag, Row);
		RowDragArm.Disarm();
	}
}

// ★★★ ЗАХВАТ СТРОКИ. Эталон на событии 5 делает ровно три вещи: гасит общую плашку подсказки
//   (`CControl_KillFocus(&g_ItemTooltip)`), запоминает номер строки (`this[2] = idnum % 74611`)
//   и возвращает код 2 — по нему общий `CControl_OnLButtonDown@0x12DB870` создаёт окно-призрак.
//   У нас призрак — общая точка `FAsdaDragGhost` (та же, что у эмоций, сумки и книги навыков).
void UAsdaExtraMenuSubsystem::BeginRowDrag(int32 Row)
{
	if (!Win || !RowIcons.IsValidIndex(Row)) { return; }
	const FAsdaExtraMenuRow* R = FindByIndex(Row);
	if (!R) { return; }

	RowDragArm.MarkStarted();
	bEtcDragActive = true;
	HideRowTip();                                       // эталон: `CControl_KillFocus(&g_ItemTooltip)`

	if (UImage* Src = RowIcons[Row].Get())
	{
		if (UCanvasPanel* Root = Cast<UCanvasPanel>(Win->GetRootWidget()))
		{
			RowDragGhost.Begin(Win, Root, Src->GetBrush(), FVector2D((float)kIconCell, (float)kIconCell),
				kRowDragGhostOpacity, FVector2D(-16.f, -16.f), TEXT("ETC_DragGhost"),
				GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
		}
		Src->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, kRowDragSourceOpacity));
	}
	UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню→панель] захват строки %d «%s» (команда %d)"),
		Row, *R->Name, R->Cmd);
}

// Снять ВИЗУАЛ захвата: погасить призрак и вернуть иконке строки полную непрозрачность.
void UAsdaExtraMenuSubsystem::CancelRowDragVisual(int32 Row)
{
	if (bEtcDragActive)
	{
		RowDragGhost.Cancel();
		bEtcDragActive = false;
	}
	if (RowIcons.IsValidIndex(Row))
	{
		if (UImage* Ic = RowIcons[Row].Get()) { Ic->SetColorAndOpacity(FLinearColor::White); }
	}
}

// ★★★ ОТПУСКАНИЕ. Без сдвига — это обычный клик, и его отдаёт кнопка-оверлей сетки
//   (`UButton::OnClicked` → `ActivateItem`); ЗДЕСЬ мы в этом случае не делаем НИЧЕГО, иначе
//   пункт открывался бы дважды. Со сдвигом — бросок (эталон событие 7 `uiHotbarSlotDispatch`).
void UAsdaExtraMenuSubsystem::EndRowDrag(bool bWasDragging, int32 Row)
{
	// ⚠ ПОРЯДОК ВАЖЕН: призрак — это `UImage` в канвасе НАШЕГО окна, висящий ровно под курсором.
	//   Гасим его ДО хит-теста панели и до проверки перекрытия, иначе «под курсором» всегда
	//   оказывалось бы наше собственное окно и бросок не проходил бы никогда (та же грабля уже
	//   ловилась в `UAsdaInventorySubsystem::TryDropToHotbar`).
	CancelRowDragVisual(Row);
	if (!bWasDragging || Row < 0) { return; }
	DropRowToHotbar(Row);
}

// ★★★ ЕДИНАЯ ТОЧКА УКЛАДКИ — порт `uiHotbarAssignSlot@0xECDFB0`. Ею пользуются и бросок мышью,
//   и консольная проверка `asda.ExtraMenu.Drop` (мышью в headless-прогоне не бросить, а второй
//   путь укладки ради теста заводить нельзя).
bool UAsdaExtraMenuSubsystem::DropRowToHotbar(int32 Row, int32 AbsSlot)
{
	const FAsdaExtraMenuRow* R = FindByIndex(Row);
	if (!R)
	{
		UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню→панель] строки %d в таблице нет (записей %d) — отказ"),
			Row, Records.Num());
		return false;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb)
	{
		UE_LOG(LogAsdaEtc, Warning,
			TEXT("[доп-меню→панель] подсистема панели недоступна — бросок строки %d потерян"), Row);
		return false;
	}

	// Хит-тест слота панели = эталонная проверка «idnum контрола под курсором попал в диапазон».
	const int32 Abs = (AbsSlot >= 0) ? AbsSlot : Hb->HitTestSlotUnderCursor();
	if (Abs < 0)
	{
		UE_LOG(LogAsdaEtc, Log,
			TEXT("[доп-меню→панель] бросок строки %d мимо панели — эталон в этом случае ничего не делает"),
			Row);
		return false;
	}
	// ★ Перекрытие окон. У эталона под курсором ВСЕГДА РОВНО ОДИН контрол — верхний; если наше
	//   окно накрыло слот панели, событие 7 придёт с чужим idnum и в диапазоны броска не попадёт.
	//   Наш хит-тест геометрический и «видит» слот сквозь окно — восстанавливаем приоритет явно.
	if (AbsSlot < 0)
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			if (WM->IsCursorOverWindow(FName(kWinId)))
			{
				UE_LOG(LogAsdaEtc, Log,
					TEXT("[доп-меню→панель] слот %d панели закрыт окном 74600 — эталон здесь бросок не обрабатывает"),
					Abs);
				return false;
			}
		}
	}
	// ⚠ КОЛОНКА 12 — слот оружия: эталонный диапазон броска (73011…73022) её НЕ включает,
	//   ровно как у эмоции и навыка. Эталон при этом МОЛЧИТ — нотиса нет (см. таблицу в заголовке).
	if (Hb->ColumnOfAbsSlot(Abs) == 12)
	{
		UE_LOG(LogAsdaEtc, Log,
			TEXT("[доп-меню→панель] слот оружия (колонка 12) пункт доп-меню не принимает — отказ, как в эталоне"));
		return false;
	}

	// ★★★ ЗАПИСЬ 10 б: тип 5, **подтип = вид ключа**, Index = КОД ДЕЙСТВИЯ пункта, Count = 0, Id = 0.
	//   Эталон клал сюда ПОРЯДКОВЫЙ НОМЕР строки (`uiHotbarAssignSlot@0xECDFB0`: `Src[1] = номер`,
	//   `SubType` = 0 от `memset`). Мы кладём УСТОЙЧИВЫЙ код действия и помечаем это `SubType = 1`
	//   — решение владельца от 2026-08-30: номер строки сдвигается при любом изменении состава
	//   меню и уводит чужие раскладки на соседний пункт.
	//   ⚠ Отступление от эталона МИНИМАЛЬНОЕ и обратимое: формат пакета, размер записи и поведение
	//   сервера не меняются ни на байт (`SubType` для типа 5 не читает НИКТО — ни клиент, ни
	//   `Recv_DBQuickSlotInfo_Save@0x4DAD60`, ни процедуры БД `UPQuickSlot*`). Ключ отката —
	//   `asda.ExtraMenu.LegacyRowKey 1`: писать, как эталон, порядковый номер.
	const bool bLegacyKey = (CVarEtcLegacyRowKey.GetValueOnGameThread() != 0);
	const uint8 KeyKind = bLegacyKey ? (uint8)kExtraKeyRowOrdinal : (uint8)kExtraKeyCmd;
	const int32 Key = bLegacyKey ? Row : R->Cmd;
	//   `SetSlotContent` сам пересобирает иконку/подсказку и шлёт страницу пакетом 5067 — то же,
	//   что делает `Hotbar_DragDrop_SetSlot@0xDE9DF0` через `Hotbar_SendLayout@0xDF2070`.
	const bool bOk = Hb->SetSlotContent(Abs, (uint8)EAsdaHotbarSlotType::ExtraMenu, KeyKind, Key, 0, 0);
	UE_LOG(LogAsdaEtc, Log,
		TEXT("[доп-меню→панель] строка %d «%s» (команда %d) → слот %d панели, ключ: вид %d значение %d ")
		TEXT("(страница %d уходит пакетом 5067): %s"),
		Row, *R->Name, R->Cmd, Abs, (int32)KeyKind, Key, Abs / UAsdaHotbarSubsystem::kColsPerPage,
		bOk ? TEXT("уложена") : TEXT("ОТКАЗ"));
	return bOk;
}

// ★ Живой прямоугольник иконки строки — точка, за которую «берётся» пункт (см. заголовок).
bool UAsdaExtraMenuSubsystem::GetRowIconAbsRect(int32 Row, FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!RowIcons.IsValidIndex(Row)) { return false; }
	const UImage* Ic = RowIcons[Row].Get();
	if (!Ic || Ic->GetVisibility() == ESlateVisibility::Collapsed) { return false; }
	return AsdaUi::WidgetAbsRect(Ic, OutMin, OutMax);
}

// ★★★ Заказ настоящего перетаскивания строка → слот (обоснование — в заголовке).
bool UAsdaExtraMenuSubsystem::SimDragRowToSlot(int32 Row, int32 AbsSlot, int32 HoldFrames)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!Hb || !WM) { return false; }
	FVector2D RMn, RMx, SMn, SMx;
	if (!GetRowIconAbsRect(Row, RMn, RMx))
	{
		UE_LOG(LogAsdaEtc, Warning, TEXT("[доп-меню→панель] строка %d не отрисована — тащить не с чего"), Row);
		return false;
	}
	if (!Hb->GetSlotIconAbsRect(AbsSlot, SMn, SMx))
	{
		UE_LOG(LogAsdaEtc, Warning,
			TEXT("[доп-меню→панель] слот %d панели не отрисован (не та страница / T-ряд скрыт) — некуда бросать"),
			AbsSlot);
		return false;
	}
	const FVector2D From = (RMn + RMx) * 0.5f;
	const FVector2D To   = (SMn + SMx) * 0.5f;
	UE_LOG(LogAsdaEtc, Warning, TEXT("[доп-меню→панель] СИМДРАГ строка %d (%.0f,%.0f) → слот %d (%.0f,%.0f)"),
		Row, From.X, From.Y, AbsSlot, To.X, To.Y);
	WM->SimulateDrag(From, To, HoldFrames);
	return true;
}

// ★★★ ДИАГНОСТИКА УСТОЙЧИВОСТИ КЛЮЧА: сдвинуть нумерацию строк, вставив в НАЧАЛО списка `Count`
//   синтетических записей. Ровно это и происходит с раскладкой игрока, когда в меню добавляют или
//   убирают пункт выше по списку. Таблицу на диске не трогаем — только рантайм; `0` возвращает
//   всё как было (перечитывает `DT_ExtraMenu`).
//   ⚠ У синтетических записей `Cmd = -1` и `Restored = true`: они не должны ни совпасть с чужим
//   кодом действия, ни попасть в счёт ИСХОДНОЙ нумерации.
void UAsdaExtraMenuSubsystem::DebugShiftTable(int32 Count)
{
	LoadTable();                                   // всегда начинаем с настоящей таблицы
	for (int32 I = 0; I < Count; ++I)
	{
		FAsdaExtraMenuRow Fake;
		Fake.Idx = 0;
		Fake.Name = FString::Printf(TEXT("[тест] лишний пункт %d"), I + 1);
		Fake.Cmd = -1;
		Fake.IconAtlas = Records.Num() > 0 ? Records[0].IconAtlas : TEXT("Extra_menu");
		Fake.IconX = 0; Fake.IconY = 0; Fake.DescLocId = 0;
		Fake.Restored = true;
		Records.Insert(Fake, 0);
	}
	// Перенумеровываем `Idx`, как это сделал бы сборщик таблицы.
	for (int32 I = 0; I < Records.Num(); ++I) { Records[I].Idx = I; }
	UE_LOG(LogAsdaEtc, Warning,
		TEXT("[доп-меню] ДИАГНОСТИКА: в начало списка вставлено %d синтетических строк, записей теперь %d. ")
		TEXT("Строка «%s» была №%d, стала №%d"),
		Count, Records.Num(),
		Records.IsValidIndex(Count) ? *Records[Count].Name : TEXT("—"), 0, Count);
	if (bReady)
	{
		// Окно уже собрано — пересобрать подписи/иконки и клик-зоны под новый состав.
		//   ⚠ `EnsureWindow` пересоздаёт окно и прячет его (в `.JUI` у 74600 `Visible = "0"`),
		//   поэтому видимость запоминаем и возвращаем: иначе кадр после сдвига был бы без окна и
		//   «доказывал» бы совсем не то.
		const bool bWasVisible = IsWindowVisible();
		bReady = false;
		EnsureWindow();
		RefreshRows();
		if (bWasVisible) { OpenWindow(); }
	}
}

// ═══════════════════════════════════ ДЕЙСТВИЕ ══════════════════════════════════════════════════

void UAsdaExtraMenuSubsystem::ActivateItem(int32 Index)
{
	const FAsdaExtraMenuRow* R = FindByIndex(Index);
	if (!R)
	{
		UE_LOG(LogAsdaEtc, Warning, TEXT("[доп-меню] активация пункта %d: записи нет (в таблице %d)"),
			Index, Records.Num());
		return;
	}
	UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] клик по строке %d «%s» → команда %d"),
		Index, *R->Name, R->Cmd);
	DispatchCommand(R->Cmd, R->Name);
}

// Порт `uiOpenWindowDispatch@0xECD570`. Порядок веток и их номера — эталонные; ветки, чьи системы
//   в порту ещё не существуют, ОТКАЗЫВАЮТ ЧЕСТНО (закон `no-simplifications-stubs`).
void UAsdaExtraMenuSubsystem::DispatchCommand(int32 Cmd, const FString& SourceName)
{
	UGameInstance* GI = GetGameInstance();
	switch (Cmd)
	{
	case kCmdTitle:
		// ★ ПОРТИРОВАНО (соседний заход, `UAsdaTitleSubsystem`). Эталон `questLogWnd_Open@0xE98380`
		//   — именно ТОГГЛ: первым делом проверяет `*(this+1)+292 == 1` («окно уже видно») и тогда
		//   зовёт `uiTitleHide@0xE985C0`. Свою копию окна не заводим (закон `port-shared-points`),
		//   эталонные гейты открытия живут в самой подсистеме званий.
		if (UAsdaTitleSubsystem* Title = GI ? GI->GetSubsystem<UAsdaTitleSubsystem>() : nullptr)
		{
			if (Title->IsTitleWindowOpen()) { Title->CloseTitleWindow(); }
			else                            { Title->OpenTitleWindow(); }
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема званий не поднята"), *SourceName);
		}
		break;
	case kCmdSoulmate:
		// ★ ПОРТИРОВАНО волной 1-А (06.10.2026). [ОРИГИНАЛ] `uiOpenWindowDispatch@0xECD570` case 1 (AsdaStory_game.c:510454-510458):
		//   `sub_938F68(g_pFindSoulmateWin) == 1` (окно поиска Друга 8200 `Find_Soulmate_Win` видно) → `cFindSoulmateWinHide`;
		//   иначе `netSendMaskedPacket_op5483` — окно поднимет ответ сервера 5485. Окна 71000/71100, названные здесь прежде, —
		//   подтверждённо мёртвые (`UNUSED_REPORT.md`), к пункту отношения не имеют. Своей копии логики нет (`port-shared-points`).
		if (UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr)
		{
			Soul->ToggleSearchFromExtraMenu();
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема соулмейта не поднята"), *SourceName);
		}
		break;
	case kCmdPrivateShop:
		// ★ ПОРТИРОВАНО волной Л1 торговли (29.09.2026). Эталон `uiOpenWindowDispatch@0xECD570` case 2 (ui_core.c:87165-87166):
		//   `UI_DispatchTargetContextMenu(g_pLocalPlayer, 2, 7)` → гейт «верхом» 2040, затем если окно подготовки лавки 22000
		//   НЕ открыто — `netSendPlayerInfo6300(g_pSellWindow)`. Одна точка входа — `UAsdaCharaInfoSubsystem::PlayEmotion`
		//   (там же эмоция 207 и хоткей 65), своей копии гейтов здесь нет. Реестр отложенного Э3 этим закрыт.
		if (UAsdaCharaInfoSubsystem* CI = GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr)
		{
			const bool bOk = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 7));
			UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] «%s» → функция 2/7 (личная лавка): %s"), *SourceName,
				bOk ? TEXT("6300 отправлен") : TEXT("отказ/молча"));
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема окна характеристик не поднята"), *SourceName);
		}
		break;
	case kCmdPet:
		// ★ ПОРТИРОВАНО волной 2 (21.09.2026). Эталон `CRefineWnd_Open@0xDC3390` — именно ТУМБЛЕР:
		//   первым делом он проверяет видимость окна 43000 и тогда просто прячет его
		//   (`item_enhance.c:3807-3808`), а уже потом идут четыре гейта. Своей копии окна здесь
		//   не заводим (закон `port-shared-points`) — все гейты живут в самой подсистеме.
		//   Реестр отложенного `PORT_PENDING_ON_SYSTEMS.md:759` (Э4) этим закрыт.
		if (UAsdaPetCageSubsystem* PetWin = GI ? GI->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr)
		{
			const bool bOpened = PetWin->OpenCage();
			UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] «%s»: окно питомца 43000 %s"),
				*SourceName, bOpened ? TEXT("открыто") : TEXT("закрыто/отклонено гейтом"));
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема окна питомца не поднята"), *SourceName);
		}
		break;
	case kCmdDisassembly:
		// ★ ПОРТИРОВАНО волной 2-А (06.10.2026). Эталон `uiOpenWindowDispatch@0xECD570` case 4 (ui_core.c:87170-87171) →
		//   `upgradeUiOnOpen@0xE0D530(g_pRecvUiHandler3)` — ТУМБЛЕР окна разборки 32000 (имя IDA врёт); звука у ветки нет.
		//   Ячейка хотбара вида 5 приходит сюда же. Своей копии логики нет (`port-shared-points`).
		if (UAsdaDisassemblySubsystem* Dis = GI ? GI->GetSubsystem<UAsdaDisassemblySubsystem>() : nullptr)
		{
			Dis->Toggle(FString::Printf(TEXT("доп-меню «%s»"), *SourceName));
			UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] «%s»: окно разборки 32000 %s"), *SourceName, Dis->IsOpen() ? TEXT("открыто") : TEXT("закрыто"));
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема окна разборки не поднята"), *SourceName);
		}
		break;
	case kCmdCraft:
		// ★ ПОРТИРОВАНО волной В2 ремесла (25.09.2026). Эталон `uiOpenWindowDispatch` case 5
		//   (ui_core.c:87173-87175) → `tileInteractGateAndOpen@0xFDD1A0(g_pItemMainWnd)` — ТУМБЛЕР окна 76000
		//   с гейтами; звука у ветки нет. Ячейка хотбара вида 5 приходит сюда же (`Hotbar_SlotActivate`
		//   ui_core.c:67464-67468 → этот диспетчер). Реестр отложенного `PORT_PENDING_ON_SYSTEMS.md:761` (Э6).
		if (UAsdaCraftWindowSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr)
		{
			const bool bDone = Craft->GateAndToggle(*FString::Printf(TEXT("доп-меню «%s»"), *SourceName));
			UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] «%s»: окно создания снаряжения 76000 %s%s"),
				*SourceName, Craft->IsOpen() ? TEXT("открыто") : TEXT("закрыто"), bDone ? TEXT("") : TEXT(" (отклонено гейтом)"));
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема окна ремесла не поднята"), *SourceName);
		}
		break;
	case kCmdUpgrade:
		// ★ ПОРТИРОВАНО 25.09.2026 (заточка, волна 3). Эталон `uiOpenWindowDispatch` case 6
		//   (ui_core.c:87177) → `uiCombineWindow_Close@0xD28340` — ТУМБЛЕР окна 20000 (имя IDA врёт),
		//   гейта открытия у него нет. Своей копии окна не заводим (закон `port-shared-points`).
		//   Реестр отложенного `PORT_PENDING_ON_SYSTEMS.md:760-762` (Э7) этим закрыт.
		if (UAsdaItemUpgradeSubsystem* Up = GI ? GI->GetSubsystem<UAsdaItemUpgradeSubsystem>() : nullptr)
		{
			Up->ToggleWindow();
			UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] «%s»: окно улучшения снаряжения 20000 %s"),
				*SourceName, Up->IsOpen() ? TEXT("открыто") : TEXT("закрыто"));
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема окна улучшения снаряжения не поднята"), *SourceName);
		}
		break;
	case kCmdPartyFind:
		// ★ ПОРТИРОВАНО этим заходом. Эталон `sub_DB9B80@0xDB9B80` — именно ТОГГЛ: первым делом
		//   проверяет видимость окна и тогда «сворачивает» его с нотисом 11906. Свою копию окна
		//   не заводим (закон `port-shared-points`) — все гейты живут в самой подсистеме.
		if (UAsdaPartyFindSubsystem* Pf = GI ? GI->GetSubsystem<UAsdaPartyFindSubsystem>() : nullptr)
		{
			Pf->ToggleWindow();
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема поиска отряда не поднята"),
				*SourceName);
		}
		break;
	case kCmdFishing:
		// ★ ПОРТИРОВАНО (рыбалка, волна В4, 08.10.2026). Эталон: `mov ecx, 0x351fbb0` (0xECD621) → `targetSelectNearestEntity@0xFC6490`
		//   (g:604677-604746): среди зон ТЕКУЩЕЙ карты — ближайшая по XZ строго внутри `r·100`; есть → выбрать (`sub_FC3590`, только
		//   при fishState 0) и `inputHotkeyAction109` (вопрос 81 «Хочешь здесь порыбачить?»); нет → чат 110700 и fishState := 0.
		//   Комментарий «найти и начать» был неточен: пункт НЕ начинает рыбалку, а выбирает точку и переходит в 109.
		//   [ОРИГИНАЛ] Д9 (`fishState := 0` безусловно) сохранён 1:1: во время рыбалки (fs ≠ 0) ходить нельзя, зона под игроком,
		//   ветка «нет зоны» на практике недостижима; при телепорте/смене карты автомат и так сбрасывается.
		//   Ячейка хотбара типа 5 приходит сюда же (`Hotbar_SlotActivate` → `OpenExtraMenuItem` → этот диспетчер).
		if (UAsdaFishingSubsystem* Fish = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
		{
			const bool bFound = Fish->StartAtNearestSpot();
			UE_LOG(LogAsdaEtc, Log, TEXT("[доп-меню] «%s»: рыбалка — %s"), *SourceName,
				bFound ? TEXT("ближайшая зона выбрана, 109") : TEXT("зоны в радиусе нет, 110700"));
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема рыбалки не поднята"), *SourceName);
		}
		break;
	case kCmdFishBook:
		// ★ ПОРТИРОВАНО (рыбалка, волна В3, 08.10.2026). Эталон: `mov ecx, 0x352137c` (0xECD62D) → `itemCollectUiOpen@0xFD3A20` —
		//   ОТКРЫТЬ, а не тоггл: при уже видимом окне ничего не делает (ui_book.md §11). Своей копии окна не заводим.
		if (UAsdaFishBookSubsystem* Book = GI ? GI->GetSubsystem<UAsdaFishBookSubsystem>() : nullptr)
		{
			Book->Open();
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема книги рыбака не поднята"), *SourceName);
		}
		break;
	case kCmdElitePost:
		// ★ ЕДИНСТВЕННЫЙ ПОРТИРОВАННЫЙ ПУНКТ. Эталон: окно 69000 видно → скрыть, иначе открыть
		//   и синхронизировать (`uiBagWindow_OpenAndSync@0xEF1B30`). В порту это ровно `ToggleWindow`.
		if (UAsdaPremiumPostSubsystem* Post = GI ? GI->GetSubsystem<UAsdaPremiumPostSubsystem>() : nullptr)
		{
			Post->ToggleWindow();
		}
		else
		{
			UE_LOG(LogAsdaEtc, Error, TEXT("[доп-меню] «%s»: подсистема элитного ящика не поднята"),
				*SourceName);
		}
		break;
	case kCmdGathering:
		// Эталон: автомат добычи (состояние 4 → 5, 0 → 1) с антидребезгом 2000 мс; состояние 1
		//   требует надетой лопаты и заряда, иначе нотис 111129 в ОБЩУЮ ПЛАШКУ.
		// ★ 28.10.2026: прежняя приписка «WBP_Subjugate_WIN 75000» была ОШИБКОЙ — `sub_FE9760@0xFE9760` к окну 75000 не обращается
		//   (тело: автомат состояний 0→1, 4→5); 75000 — доска объявлений (`Docs/Port/systems/BOARD_QUEST_FLOW.md` §1), у добычи
		//   она лишь гейт `tileInteractGateAndOpen` (открыта доска → взаимодействие с тайлом молча запрещено).
		RefuseNotPorted(Cmd, SourceName, TEXT("добыча (`sub_FE9760@0xFE9760`, опкод 6752)"));
		break;
	default:
		// Эталон: `default: return` — молча. Для команды 7 ветки в оригинале НЕТ вовсе.
		UE_LOG(LogAsdaEtc, Warning,
			TEXT("[доп-меню] «%s»: код команды %d в разборе эталона отсутствует (`default: return`) — ")
			TEXT("действия нет и в оригинале"), *SourceName, Cmd);
		break;
	}
}

void UAsdaExtraMenuSubsystem::RefuseNotPorted(int32 Cmd, const FString& SourceName, const TCHAR* What)
{
	UE_LOG(LogAsdaEtc, Warning,
		TEXT("[доп-меню] «%s» (команда %d): %s в порту ещё НЕ ПОРТИРОВАНА — открывать нечего. ")
		TEXT("Запись в реестре `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`"), *SourceName, Cmd, What);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		// Своя строка (id > 990000), маркер `[ss1]` = подпись пункта — подставляет `UAsdaTextSubsystem`.
		Msg->PostLocFmt(EAsdaMsgCategory::System, kLocNotPorted, { SourceName }, {});
	}
}

// ═══════════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════

void UAsdaExtraMenuSubsystem::SimulateClick(int32 Row)
{
	// ★ Прогон ТЕМ ЖЕ путём, что и прозрачная кнопка-оверлей сетки (второго пути обработки клика
	//   ради теста не заводим — закон из брифинга «диагностика обязана жать саму кнопку»).
	if (!EnsureWindow()) { return; }
	if (RowGrid) { RowGrid->SimulateClick(Row); }
}

void UAsdaExtraMenuSubsystem::DebugMoveRow(int32 Row, float X, float Y)
{
	if (!RowGroups.IsValidIndex(Row) || !RowGroups[Row]) { return; }
	if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(RowGroups[Row]))
	{
		CS->SetPosition(FVector2D(X, Y));
		UE_LOG(LogAsdaEtc, Warning, TEXT("[доп-меню] ДИАГНОСТИКА: строка %d сдвинута в слот (%.0f,%.0f)"),
			Row, X, Y);
	}
}

FString UAsdaExtraMenuSubsystem::DumpState() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	FString S = FString::Printf(
		TEXT("=== ДОП-МЕНЮ «Прочее» (ETC_NEW_WIN 74600) ===\n")
		TEXT("  окно: %s, видимо=%d, готово=%d; записей таблицы=%d, строк в WBP=%d\n")
		TEXT("  подписка на кнопку трея %d: %s; подсказка показана=%d (строка %d)\n"),
		Win ? TEXT("есть") : TEXT("НЕТ"),
		(WM && WM->IsWindowVisible(FName(kWinId))) ? 1 : 0, bReady ? 1 : 0,
		Records.Num(), RowGroups.Num(),
		kMenuButtonIdNum, bMenuBound ? TEXT("есть") : TEXT("НЕТ"), bTipShown ? 1 : 0, TipRow);

	// ★ РАЗМЕР ВЬЮПОРТА И DPI. Без них «строка не нарисовалась» неразличимо с «строка за краем
	//   экрана»: окно 432×368 в базе 1024×768 просто не влезает в маленький вьюпорт.
	if (const UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		const FVector2D VP = AsdaUi::ViewportSizeUnits(Wd);
		S += FString::Printf(TEXT("  вьюпорт: %.0fx%.0f юнитов, DPI=%.3f\n"),
			VP.X, VP.Y, AsdaUi::ViewportDpiScale(Wd));
	}
	// ★ Живой прямоугольник ПАНЕЛИ СТРОК и её отсечение: если строка не нарисовалась, первым делом
	//   надо знать, не обрезал ли её родитель (закон `false-negatives-method` — «нет» бывает ложным).
	if (Win)
	{
		if (const UWidget* Scroll = Win->GetWidgetFromName(TEXT("ETC_NEW_Scroll_grp")))
		{
			FVector2D Mn(0, 0), Mx(0, 0);
			const bool bOk = AsdaUi::WidgetAbsRect(Scroll, Mn, Mx);
			const UCanvasPanelSlot* SS = AsdaUi::SlotOf(Scroll);
			S += FString::Printf(
				TEXT("  панель строк ETC_NEW_Scroll_grp: слот %.0fx%.0f, живая %s(%.0f,%.0f)-(%.0f,%.0f), отсечение=%d\n"),
				SS ? SS->GetSize().X : -1.f, SS ? SS->GetSize().Y : -1.f,
				bOk ? TEXT("") : TEXT("НЕТ "), Mn.X, Mn.Y, Mx.X, Mx.Y, (int32)Scroll->GetClipping());
			// ★ ВСЕ дети панели в порядке дерева: имя, есть ли Slate-виджет, живой прямоугольник.
			//   Нужно, чтобы отличить «строку не нарисовали» от «строку взяли не ту».
			if (const UPanelWidget* SP = Cast<UPanelWidget>(Scroll))
			{
				for (int32 k = 0; k < SP->GetChildrenCount(); ++k)
				{
					const UWidget* C = SP->GetChildAt(k);
					if (!C) { continue; }
					FVector2D CMn(0, 0), CMx(0, 0);
					const bool bC = AsdaUi::WidgetAbsRect(C, CMn, CMx);
					const UCanvasPanelSlot* CSS = AsdaUi::SlotOf(C);
					S += FString::Printf(
						TEXT("      дитя %2d «%s» [%s] slate=%d вид=%d слот (%.0f,%.0f) %.0fx%.0f авторазмер=%d z=%d живая %s(%.0f,%.0f)-(%.0f,%.0f)\n"),
						k, *C->GetName(), *C->GetClass()->GetName(),
						C->GetCachedWidget().IsValid() ? 1 : 0, (int32)C->GetVisibility(),
						CSS ? CSS->GetPosition().X : -1.f, CSS ? CSS->GetPosition().Y : -1.f,
						CSS ? CSS->GetSize().X : -1.f, CSS ? CSS->GetSize().Y : -1.f,
						(CSS && CSS->GetAutoSize()) ? 1 : 0, CSS ? CSS->GetZOrder() : -999,
						bC ? TEXT("") : TEXT("НЕТ "), CMn.X, CMn.Y, CMx.X, CMx.Y);
				}
			}
		}
	}

	for (int32 i = 0; i < RowGroups.Num(); ++i)
	{
		const FAsdaExtraMenuRow* R = FindByIndex(i);
		const UWidget* G = RowGroups[i];
		FVector2D Mn(0, 0), Mx(0, 0);
		const bool bGeom = G && AsdaUi::WidgetAbsRect(G, Mn, Mx);
		const UTextBlock* T = RowLabels.IsValidIndex(i) ? RowLabels[i].Get() : nullptr;
		// ★ Слот и анкоры печатаем ВСЕГДА: «нет живой геометрии» само по себе не говорит, ушёл ли
		//   узел за край или его просто не рисовали (закон `false-negatives-method`).
		const UCanvasPanelSlot* CS = G ? AsdaUi::SlotOf(G) : nullptr;
		const FAnchors An = CS ? CS->GetAnchors() : FAnchors(0.f);
		S += FString::Printf(
			TEXT("      слот (%.0f,%.0f) %.0fx%.0f анкор (%.2f,%.2f)-(%.2f,%.2f), родитель «%s», отсечение=%d\n"),
			CS ? CS->GetPosition().X : -1.f, CS ? CS->GetPosition().Y : -1.f,
			CS ? CS->GetSize().X : -1.f, CS ? CS->GetSize().Y : -1.f,
			An.Minimum.X, An.Minimum.Y, An.Maximum.X, An.Maximum.Y,
			(G && G->GetParent()) ? *G->GetParent()->GetName() : TEXT("—"),
			G ? (int32)G->GetClipping() : -1);
		S += FString::Printf(
			TEXT("  строка %2d «%s»: %s | запись: %s | видимость=%d | живая геометрия %s(%.0f,%.0f)-(%.0f,%.0f) | подпись на экране «%s»\n"),
			i, G ? *G->GetName() : TEXT("—"),
			R ? TEXT("ЖИВАЯ") : TEXT("погашена (записи нет)"),
			R ? *FString::Printf(TEXT("cmd=%d иконка=(%d,%d) подсказка=%d «%s»"),
					R->Cmd, R->IconX, R->IconY, R->DescLocId, *R->Name)
			  : TEXT("—"),
			G ? (int32)G->GetVisibility() : -1,
			bGeom ? TEXT("") : TEXT("НЕТ "), Mn.X, Mn.Y, Mx.X, Mx.Y,
			T ? *T->GetText().ToString() : TEXT("—"));
		// ★ Живой прямоугольник ПОДПИСИ и ИКОНКИ строки. Нужен, когда у самой группы геометрии нет:
		//   он отвечает на вопрос «строка не нарисована ВООБЩЕ или не нарисована только её панель».
		{
			FVector2D TMn(0, 0), TMx(0, 0), IMn(0, 0), IMx(0, 0);
			const bool bT = T && AsdaUi::WidgetAbsRect(T, TMn, TMx);
			const UImage* Ic = RowIcons.IsValidIndex(i) ? RowIcons[i].Get() : nullptr;
			const bool bI = Ic && AsdaUi::WidgetAbsRect(Ic, IMn, IMx);
			S += FString::Printf(
				TEXT("      подпись живая %s(%.0f,%.0f)-(%.0f,%.0f); иконка «%s» живая %s(%.0f,%.0f)-(%.0f,%.0f)\n"),
				bT ? TEXT("") : TEXT("НЕТ "), TMn.X, TMn.Y, TMx.X, TMx.Y,
				Ic ? *Ic->GetName() : TEXT("—"),
				bI ? TEXT("") : TEXT("НЕТ "), IMn.X, IMn.Y, IMx.X, IMx.Y);
		}
	}
	// ★ ЦЕПОЧКА ПРЕДКОВ 8-й строки (индекс 7). Именно она не рисовалась: живой прогон 2026-08-29
	//   показал, что тот же виджет на месте (206,166) рисуется, а на своём (2,289) — нет ⇒ режет
	//   кто-то ВЫШЕ. Печатаем прямоугольники всех предков, чтобы виновник был назван, а не угадан.
	if (RowGroups.IsValidIndex(7) && RowGroups[7])
	{
		S += TEXT("  --- предки 8-й строки (индекс 7) ---\n");
		for (const UWidget* A = RowGroups[7]; A; A = A->GetParent())
		{
			FVector2D AMn(0, 0), AMx(0, 0);
			const bool bA = AsdaUi::WidgetAbsRect(A, AMn, AMx);
			S += FString::Printf(TEXT("    «%s» [%s] отсечение=%d вид=%d живая %s(%.0f,%.0f)-(%.0f,%.0f)\n"),
				*A->GetName(), *A->GetClass()->GetName(), (int32)A->GetClipping(), (int32)A->GetVisibility(),
				bA ? TEXT("") : TEXT("НЕТ "), AMn.X, AMn.Y, AMx.X, AMx.Y);
		}
	}
	if (RowGrid)
	{
		S += RowGrid->DumpHitZonesLive(TEXT("строки доп-меню"), kJuiRows);
		S += RowGrid->DumpHitZones(TEXT("строки доп-меню"));
	}
	else
	{
		S += TEXT("  клик-зоны: СЕТКА НЕ ПОСТРОЕНА\n");
	}
	return S;
}

// ═══════════════════════════════════ КОНСОЛЬ ═══════════════════════════════════════════════════

namespace
{
	UAsdaExtraMenuSubsystem* EtcSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaExtraMenuSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GEtcToggle(
	TEXT("asda.ExtraMenu.Toggle"),
	TEXT("Доп-меню «Прочее» (74600): переключить окно (эталонный тоггл кнопки трея 73409)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaExtraMenuSubsystem* S = EtcSub(W)) { S->ToggleWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GEtcDump(
	TEXT("asda.ExtraMenu.Dump"),
	TEXT("Доп-меню «Прочее»: состав таблицы, состояние всех 16 строк и ЖИВЫЕ клик-зоны."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaExtraMenuSubsystem* S = EtcSub(W)) { UE_LOG(LogAsdaEtc, Warning, TEXT("%s"), *S->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEtcClick(
	TEXT("asda.ExtraMenu.Click"),
	TEXT("Доп-меню «Прочее»: нажать строку N тем же путём, что и мышь (asda.ExtraMenu.Click 9)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaExtraMenuSubsystem* S = EtcSub(W);
		if (!S) { return; }
		const int32 Row = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		S->SimulateClick(Row);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ДИАГНОСТИЧЕСКИЙ СДВИГ СТРОКИ. Заведён под разбор «строка 7 не рисуется»: если тот же виджет
//   на другой позиции рисуется — дело в раскладке/отсечении, если нет — в самом узле.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEtcMoveRow(
	TEXT("asda.ExtraMenu.MoveRow"),
	TEXT("asda.ExtraMenu.MoveRow <строка> <X> <Y> — сдвинуть слот строки (ТОЛЬКО диагностика)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaExtraMenuSubsystem* S = EtcSub(W);
		if (!S || Args.Num() < 3) { return; }
		S->DebugMoveRow(FCString::Atoi(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ПРОВЕРКА БРОСКА БЕЗ МЫШИ. Ведёт в ТУ ЖЕ `DropRowToHotbar`, что и перетаскивание: второго пути
//   укладки нет. Без второго аргумента слот берётся хит-тестом панели под курсором — тогда команда
//   проверяет и сам хит-тест; с аргументом слот задаётся явно (headless-прогон, где мыши нет).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEtcDrop(
	TEXT("asda.ExtraMenu.Drop"),
	TEXT("asda.ExtraMenu.Drop <строка> [абс.слот] — положить пункт доп-меню в слот панели ")
	TEXT("(тип 5, страница уходит пакетом 5067). Без слота — хит-тест под курсором."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaExtraMenuSubsystem* S = EtcSub(W);
		if (!S || Args.Num() < 1) { return; }
		const int32 Row = FCString::Atoi(*Args[0]);
		const int32 Abs = (Args.Num() > 1) ? FCString::Atoi(*Args[1]) : -1;
		const bool bOk = S->DropRowToHotbar(Row, Abs);
		UE_LOG(LogAsdaEtc, Warning, TEXT("asda.ExtraMenu.Drop %d %d → %s"),
			Row, Abs, bOk ? TEXT("УЛОЖЕНО") : TEXT("ОТКАЗ"));
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ПРОВЕРКА ПУТЁМ ИГРОКА. Считает ЖИВЫЕ прямоугольники иконки строки и иконки слота панели и
//   заказывает НАСТОЯЩЕЕ перетаскивание Slate (`asda.UI.SimDrag`): нажатие над строкой, движение
//   курсора кадр за кадром, отпускание над слотом. Никакой обработчик напрямую не зовётся — путь
//   тот же, что у мыши: Slate → `TickPointer` → порог `FAsdaDragArm` → хит-тест панели → укладка.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEtcSimDrag(
	TEXT("asda.ExtraMenu.SimDragToSlot"),
	TEXT("asda.ExtraMenu.SimDragToSlot <строка> <абс.слот> [кадров=24] — перетащить строку доп-меню ")
	TEXT("в слот панели НАСТОЯЩИМИ событиями мыши (проверка путём игрока)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: синтетическое перетаскивание по координатам — при живом соединении 5067 перезаписал бы раскладку панели на сервере.
		static const uint16 kGuardOps[] = { 5067 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.ExtraMenu.SimDragToSlot"), 10.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaExtraMenuSubsystem* S = EtcSub(W);
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!S || !Hb || !WM || Args.Num() < 2)
		{
			UE_LOG(LogAsdaEtc, Warning, TEXT("использование: asda.ExtraMenu.SimDragToSlot <строка> <абс.слот> [кадров]"));
			return;
		}
		(void)Hb; (void)WM;   // проверены выше: без них заказ бессмысленен
		S->SimDragRowToSlot(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]),
			Args.Num() > 2 ? FCString::Atoi(*Args[2]) : 24);
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ САМОТЕСТ ПЕРЕТАСКИВАНИЯ ПУТЁМ ИГРОКА (`-game`, закон `ui-check-game-path-geometry`).
//   ЗАЧЕМ ТАЙМЕРЫ. `-ExecCmds` выполняет все команды НА НУЛЕВОМ КАДРЕ, а живая геометрия готова
//   только после отрисовки; само перетаскивание к тому же занимает десятки кадров. Поэтому шаги
//   разнесены таймерами — так же устроены `asda.PartyFind.SelfTest` и `asda.Title.SelfTest`.
//   ЗАЧЕМ ИМЕНОВАННЫЕ КАДРЫ. Безымянный `shot` пишет `ScreenShotNNNNN.png` в общую папку, и
//   параллельный прогон чужого агента их перетирает.
//   Запуск (окон Unreal на экране не появляется; `-NullRHI` НЕЛЬЗЯ — Slate не будет рисовать):
//     UnrealEditor-Cmd.exe <uproject> "/Game/Map/alpen/alpen?game=…" -game -RenderOffscreen
//       -ExecCmds="asda.ExtraMenu.DragSelfTest quit"
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEtcDragSelfTest(
	TEXT("asda.ExtraMenu.DragSelfTest"),
	TEXT("Доп-меню «Прочее»: полный прогон перетаскивания строки в панель быстрого доступа ")
	TEXT("НАСТОЯЩИМИ событиями мыши, с кадрами. `quit` — выйти после прогона."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: самотест тащит строку в слот панели и зовёт Quick.Set/Recv5059/Use — при живом соединении ушли бы синтетическая раскладка 5067 (×3) и 11004, а FakeSelfSpawn подменил бы тайл/userNum живой сессии.
		static const uint16 kGuardOps[] = { 5067, 11004 };
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.ExtraMenu.DragSelfTest"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaExtraMenuSubsystem* S = EtcSub(W);
		if (!S || !W) { return; }
		bool bQuit = false;
		bool bPreOpen = false;   // ★ R4-b хвосты-2 (15.11.2026): `preopen` — рюкзак, книга и 74600 открыты до снимка (проверка возврата М1)
		for (const FString& A : Args)
		{
			if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			if (A.Equals(TEXT("preopen"), ESearchCase::IgnoreCase)) { bPreOpen = true; }
		}
		// Что тащим: строка 9. ⚠ Это «Книга о рыболовстве», а НЕ «Элитный почтовый ящик»: в нашей
		//   `DT_ExtraMenu` восстановлена запись «Поиск отряда» (`Idx=2`), и всё, что ниже, сдвинуто
		//   на единицу. Для проверки это даже лучше — система книги рыбака в порту ещё не
		//   перенесена, поэтому клик по СЛОТУ панели обязан дать ту же строку честного отказа
		//   («Эта часть игры ещё не перенесена: …»), что и клик по СТРОКЕ окна; совпадение этих
		//   двух строк и доказывает, что действие у слота и у пункта ОДНО.
		const int32 kRow = 9;
		const int32 kSlot = 3;     // абс. 3 = колонка 3 страницы 0 (обычная колонка)
		const int32 kWeaponSlot = 12;   // колонка оружия — принимать пункт НЕ ДОЛЖНА
		const int32 kHold = 150;   // кадров удержания: хватает снять ДВА кадра посреди перетаскивания

		// ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте —
		//   общий флаг шагов `At` (проверку делает первый сработавший шаг, при отказе пропускаются все).
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.ExtraMenu.DragSelfTest"), bQuit);
		// ★ R4-b (15.11.2026): окна рюкзака и книги рыбака до прогона (Key — рюкзак, Value — книга): закрыть то, что открыл самотест
		//   сам (шаги 3/5/7/8 открывают книгу и ящик, ящик оставляет рюкзак открытым — соседу по пачке `uiCanInteractWithTarget`
		//   отказывает: `asda.PremiumMall.CartSelfTest`, разбор `r4/b/REPORT.md`, раздел 4).
		if (bPreOpen)   // ★ R4-b хвосты-2 (15.11.2026) (М1): открыть до снимка — уборщик обязан вернуть их открытыми
		{
			if (UGameInstance* Gp = W->GetGameInstance())
			{
				// ★ R4-b хвосты-2 (15.11.2026) (ревизия 3) (В1): книга при открытии закрывает сумку — сначала книга, потом сумка (сумка книгу не закрывает).
				if (UAsdaFishBookSubsystem* Bp = Gp->GetSubsystem<UAsdaFishBookSubsystem>()) { if (!Bp->IsOpen()) { Bp->Open(); } }
				if (UAsdaInventorySubsystem* Ip = Gp->GetSubsystem<UAsdaInventorySubsystem>()) { if (!Ip->IsWindowOpen()) { Ip->OpenWindow(); } }
			}
			if (!S->IsWindowVisible()) { S->OpenWindow(); }
			{
				UGameInstance* Gl = W->GetGameInstance();
				const UAsdaInventorySubsystem* Il = Gl ? Gl->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
				const UAsdaFishBookSubsystem* Bl = Gl ? Gl->GetSubsystem<UAsdaFishBookSubsystem>() : nullptr;
				UE_LOG(LogAsdaEtc, Display, TEXT("САМОТЕСТ preopen: до снимка рюкзак=%d, книга рыбака=%d, 74600=%d"),   // ★ R4-b хвосты-2 (15.11.2026) (ревизия 3) (В1)
					Il && Il->IsWindowOpen() ? 1 : 0, Bl && Bl->IsOpen() ? 1 : 0, S->IsWindowVisible() ? 1 : 0);
			}
		}
		const TSharedRef<TPair<bool, bool>> WasOpen = MakeShared<TPair<bool, bool>>(false, false);
		if (UGameInstance* G0 = W->GetGameInstance())
		{
			if (const UAsdaInventorySubsystem* I0 = G0->GetSubsystem<UAsdaInventorySubsystem>()) { WasOpen->Key = I0->IsWindowOpen(); }
			if (const UAsdaFishBookSubsystem* B0 = G0->GetSubsystem<UAsdaFishBookSubsystem>()) { WasOpen->Value = B0->IsOpen(); }
		}
		// ★ R4-b хвосты (15.11.2026) (ревизия, мелочь 7): доп-меню 74600 (Key) и второй ряд панели (Value) до прогона; ключи 13 слотов страницы 0
		//   в виде записей 5059 (10 байт) — шаги 3/7/8/9 пишут слоты 3/4/5, шаг 1 включает второй ряд. Пустой массив — панели нет.
		const TSharedRef<TPair<bool, bool>> MenuWas = MakeShared<TPair<bool, bool>>(S->IsWindowVisible(), true);
		const TSharedRef<TArray<uint8>> HotWas = MakeShared<TArray<uint8>>();
		if (UGameInstance* G0 = W->GetGameInstance())
		{
			if (const UAsdaHotbarSubsystem* Q0 = G0->GetSubsystem<UAsdaHotbarSubsystem>())
			{
				MenuWas->Value = Q0->IsSecondRowVisible();
				HotWas->SetNumZeroed(13 * 10);   // 13 колонок страницы × запись 10 байт (как тело 5059)
				for (int32 Col = 0; Col < 13; ++Col)
				{
					const FAsdaHotbarSlot& Sl = Q0->GetSlot(Col);
					uint8* R = HotWas->GetData() + Col * 10;
					R[0] = Sl.Type; R[1] = Sl.SubType;
					R[2] = (uint8)(Sl.Index & 0xFF); R[3] = (uint8)((Sl.Index >> 8) & 0xFF);
					R[4] = (uint8)(Sl.Count & 0xFF); R[5] = (uint8)((Sl.Count >> 8) & 0xFF); R[6] = (uint8)((Sl.Count >> 16) & 0xFF); R[7] = (uint8)((Sl.Count >> 24) & 0xFF);
					R[8] = (uint8)(Sl.Id & 0xFF); R[9] = (uint8)((Sl.Id >> 8) & 0xFF);
				}
			}
		}
		auto At = [W, S, Gate](float Sec, TFunction<void()> Fn)
		{
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(S,
				[Fn, Gate, S]() { if (!Gate->Pass(S)) { return; } Fn(); }), Sec, false);
		};
		auto Exec = [W](const TCHAR* C) { if (GEngine) { GEngine->Exec(W, C); } };
		auto Shot = [](const TCHAR* Name)
		{
			UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ: КАДР → %s.png"), Name);
			FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		};
		auto Hot = [W]() -> UAsdaHotbarSubsystem*
		{
			UGameInstance* G = W ? W->GetGameInstance() : nullptr;
			return G ? G->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
		};

		// 1. Мир: панель быстрого доступа поднимается ТОЛЬКО при живом своём павне
		//    (`AsdaHotbarSubsystem::Tick`: `CurrentMapId >= 0` и `WS->LocalPlayerActor`).
		//    Оба крюка — штатные тестовые точки порта, а не подделка состояния.
		At(1.0f, [Exec]()
		{
			Exec(TEXT("Asda.Load.CurrentMap 3"));
			Exec(TEXT("Asda.World.FakeSelfSpawn 256 256 ТестДопМеню"));
			Exec(TEXT("asda.Quick.SecondRow 1"));
		});
		// 2. Окно доп-меню открываем САМОЙ кнопкой трея 73409.
		At(3.0f, [W]()
		{
			UGameInstance* G = W->GetGameInstance();
			if (UAsdaSystemMenuSubsystem* M = G ? G->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr)
			{
				M->ClickButton(kMenuButtonIdNum);
			}
		});
		At(4.0f, [S, Hot]()
		{
			UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ 1: окно доп-меню\n%s"), *S->DumpState());
			if (UAsdaHotbarSubsystem* Q = Hot()) { UE_LOG(LogAsdaEtc, Warning, TEXT("%s"), *Q->DumpControls()); }
		});
		At(4.6f, [Shot]() { Shot(TEXT("asda_etcdrag_1_open")); });

		// 3. ПЕРЕТАСКИВАНИЕ путём игрока.
		At(5.2f, [S]() { S->SimDragRowToSlot(kRow, kSlot, kHold); });
		// Кадр ПОСРЕДИ: обязан показать призрак у курсора и приглушённую строку-источник.
		At(5.9f, [Shot]() { Shot(TEXT("asda_etcdrag_2_grabbed")); });
		// ★ Второй кадр — когда курсор уже НАД ПАНЕЛЬЮ, то есть ВНЕ окна доп-меню. Он отвечает на
		//   отдельный вопрос: не обрезает ли призрака окно-владелец (призрак живёт в его канвасе).
		At(6.6f, [Shot]() { Shot(TEXT("asda_etcdrag_2b_over_panel")); });
		At(6.65f, [Hot]()
		{
			if (UAsdaHotbarSubsystem* Q = Hot())
			{
				UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ 2: слот под курсором посреди захвата = %d"),
					Q->HitTestSlotUnderCursor());
			}
		});
		// 4. После отпускания: модель + кадр.
		At(8.0f, [Hot]()
		{
			if (UAsdaHotbarSubsystem* Q = Hot()) { UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ 3: после броска\n%s"), *Q->DumpSlots()); }
		});
		At(8.6f, [Shot]() { Shot(TEXT("asda_etcdrag_3_dropped")); });

		// 5. Клик по слоту панели обязан сделать ТО ЖЕ, что клик по строке окна.
		At(9.4f, [Exec]() { Exec(TEXT("asda.Quick.Use 3")); });
		At(10.4f, [Shot]() { Shot(TEXT("asda_etcdrag_4_used")); });

		// 6. Закрыть и открыть доп-меню — иконка в слоте обязана остаться.
		At(11.2f, [W]()
		{
			UGameInstance* G = W->GetGameInstance();
			if (UAsdaSystemMenuSubsystem* M = G ? G->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr)
			{
				M->ClickButton(kMenuButtonIdNum);
			}
		});
		At(12.0f, [W]()
		{
			UGameInstance* G = W->GetGameInstance();
			if (UAsdaSystemMenuSubsystem* M = G ? G->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr)
			{
				M->ClickButton(kMenuButtonIdNum);
			}
		});
		At(13.0f, [Hot]()
		{
			if (UAsdaHotbarSubsystem* Q = Hot()) { UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ 4: после переоткрытия окна\n%s"), *Q->DumpSlots()); }
		});
		At(13.6f, [Shot]() { Shot(TEXT("asda_etcdrag_5_persist")); });

		// 7. Бросок в КОЛОНКУ ОРУЖИЯ — эталон её не принимает и МОЛЧИТ.
		At(14.4f, [S]() { S->SimDragRowToSlot(kRow, kWeaponSlot, 30); });
		At(16.0f, [Hot]()
		{
			if (UAsdaHotbarSubsystem* Q = Hot()) { UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ 5: после броска в слот оружия\n%s"), *Q->DumpSlots()); }
		});

		// 7б. Бросок МИМО ПАНЕЛИ — эталон в этом случае НЕ ДЕЛАЕТ НИЧЕГО (`if (a2)` в
		//     `uiHotbarSlotDispatch`): слот не меняется и ниоткуда ничего не снимается.
		At(16.4f, [S, W]()
		{
			FVector2D Mn, Mx;
			UGameInstance* G = W->GetGameInstance();
			UAsdaWindowSubsystem* WM = G ? G->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			if (WM && S->GetRowIconAbsRect(kRow, Mn, Mx))
			{
				WM->SimulateDrag((Mn + Mx) * 0.5f, FVector2D(150.f, 230.f), 30);
			}
		});
		At(17.6f, [Hot]()
		{
			if (UAsdaHotbarSubsystem* Q = Hot()) { UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ 5б: после броска МИМО панели\n%s"), *Q->DumpSlots()); }
		});

		// 8. Раскладка С СЕРВЕРА (5059) обязана восстановить тип 5 — это и есть «сохраняется».
		At(18.4f, [Exec]() { Exec(TEXT("asda.Quick.Recv5059 0 5 0 9 0 0")); });
		At(19.4f, [Hot]()
		{
			if (UAsdaHotbarSubsystem* Q = Hot()) { UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ 6: раскладка с сервера\n%s"), *Q->DumpSlots()); }
		});
		At(20.0f, [Shot]() { Shot(TEXT("asda_etcdrag_6_from_server")); });

		// ═══ 9. СТАРАЯ ЗАПИСЬ (вид ключа 0 — порядковый номер строки ИСХОДНОЙ таблицы). ═══
		//   Кладём в слот 5 то, что записал бы ОРИГИНАЛЬНЫЙ клиент: `SubType = 0`, `Index = 9`.
		//   В исходной таблице строка №9 — «Элитный почтовый ящик»; в НАШЕЙ таблице под номером 9
		//   стоит «Книга о рыболовстве» — то есть наивная трактовка открыла бы ЧУЖОЙ пункт.
		//   Правильный разбор считает только записи с `Restored == false`.
		//   ⚠ Кладём ОБЕ записи ЗДЕСЬ, а не раньше: шаг 8 (`Recv5059`) применяет страницу ЦЕЛИКОМ и
		//   стирает всё, чего в ней нет, — слот 3 из шага 3 к этому моменту уже затёрт сервером.
		//   Слот 4 — НОВЫЙ ключ (подтип 1, значение 9 = код действия «Книга о рыболовстве»),
		//   слот 5 — СТАРЫЙ (подтип 0, значение 9 = 9-я строка ИСХОДНОЙ таблицы).
		At(20.8f, [Exec]()
		{
			Exec(TEXT("asda.Quick.Set 4 5 1 9 0 0"));
			Exec(TEXT("asda.Quick.Set 5 5 0 9 0 0"));
		});
		At(21.4f, [Exec, Hot]()
		{
			Exec(TEXT("asda.ExtraMenu.ResolveKey 1 9"));
			Exec(TEXT("asda.ExtraMenu.ResolveKey 0 9"));
			if (UAsdaHotbarSubsystem* Q = Hot()) { UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ 7: старая запись в слоте 5 -- %s"), *Q->DumpSlots()); }
		});
		At(22.0f, [Exec]() { Exec(TEXT("asda.Quick.Use 5")); });
		At(22.8f, [Shot]() { Shot(TEXT("asda_etcdrag_7_legacy")); });

		// ═══ 10. ★★★ СОСТАВ МЕНЮ ИЗМЕНИЛСЯ — суть задачи. ═══
		//   Вставляем ДВЕ строки в начало меню: все настоящие пункты съезжают на 2 позиции.
		//   Оба слота обязаны открывать СВОИ пункты: слот 3 — по устойчивому коду действия,
		//   слот 5 — по исходной нумерации (синтетические строки помечены `Restored`).
		At(23.6f, [Exec]() { Exec(TEXT("asda.ExtraMenu.ShiftTable 2")); });
		At(24.4f, [Exec, Hot]()
		{
			Exec(TEXT("asda.ExtraMenu.ResolveKey 1 9"));
			Exec(TEXT("asda.ExtraMenu.ResolveKey 0 9"));
			if (UAsdaHotbarSubsystem* Q = Hot()) { UE_LOG(LogAsdaEtc, Warning, TEXT("САМОТЕСТ 8: состав меню сдвинут на 2 -- %s"), *Q->DumpSlots()); }
		});
		At(25.2f, [Exec]() { Exec(TEXT("asda.Quick.Use 4")); });
		At(26.0f, [Exec]() { Exec(TEXT("asda.Quick.Use 5")); });
		At(26.8f, [Shot]() { Shot(TEXT("asda_etcdrag_8_shifted")); });
		At(27.6f, [Exec]() { Exec(TEXT("asda.ExtraMenu.ShiftTable 0")); });

		// ★ R4-b (15.11.2026): книга рыбака и рюкзак — как до прогона, на любом выходе (и без `quit`, и при отказе флага шагов посреди
		//   прогона): свой таймер, не `At`.
		{
			FTimerHandle HC;
			// ★ R4-b (15.11.2026) (ревизия, важн. 2–3): при отказе флага шагов окна открывал не этот тест — не трогать (сосед по пачке);
			//   экземпляр игры — слабой ссылкой: таймер живёт в его менеджере и переживает смену карты, мир — нет.
			const TWeakObjectPtr<UGameInstance> WGI = W->GetGameInstance();
			// ★ R4-b хвосты-2 (15.11.2026) (ревизия хвостов, М4): расхождение страницы 0 со снимком — все 5 полей записи 5059, как их кладёт `ApplyPendingPages`
			//   (пустой тип → слот очищен, все поля 0). -1 — снимка или панели нет.
			auto PageDiff = [](const UAsdaHotbarSubsystem* Q, const TArray<uint8>& Hw) -> int32
			{
				if (!Q || Hw.Num() != 13 * 10) { return -1; }
				int32 D = 0;
				for (int32 Col = 0; Col < 13; ++Col)
				{
					const uint8* R = Hw.GetData() + Col * 10;
					const FAsdaHotbarSlot& Sl = Q->GetSlot(Col);
					const bool bE = R[0] == 0;
					const int32 Idx = bE ? 0 : (int32)((uint16)R[2] | ((uint16)R[3] << 8));
					const int32 Cnt = bE ? 0 : (int32)((uint32)R[4] | ((uint32)R[5] << 8) | ((uint32)R[6] << 16) | ((uint32)R[7] << 24));
					const int32 Id = bE ? 0 : (int32)((uint16)R[8] | ((uint16)R[9] << 8));
					if (Sl.Type != R[0] || Sl.SubType != (bE ? 0 : R[1]) || Sl.Index != Idx || Sl.Count != Cnt || Sl.Id != Id) { ++D; }
				}
				return D;
			};
			W->GetTimerManager().SetTimer(HC, FTimerDelegate::CreateWeakLambda(S, [WGI, WasOpen, MenuWas, HotWas, Gate, S, PageDiff]()
			{
				if (Gate->IsRefused()) { return; }
				UGameInstance* G2 = WGI.Get();
				// ★ R4-b хвосты (15.11.2026) (ревизия, мелочь 6): при открытом окне вступления `CloseOrToggle` снимает только его — второй вызов закрывает книгу.
				// ★ R4-b хвосты-2 (15.11.2026) (М1): окно, открытое до прогона и закрытое тестом (кнопка трея и `asda.Quick.Use` — переключатели), — открыть снова.
				if (UAsdaFishBookSubsystem* B2 = G2 ? G2->GetSubsystem<UAsdaFishBookSubsystem>() : nullptr)
				{
					if (!WasOpen->Value) { for (int32 k = 0; k < 2 && B2->IsOpen(); ++k) { B2->CloseOrToggle(); } }
					else if (!B2->IsOpen()) { B2->Open(); UE_LOG(LogAsdaEtc, Display, TEXT("САМОТЕСТ уборка: книга рыбака открыта снова")); }   // ★ R4-b хвосты-2 (15.11.2026) (ревизия 3)
				}
				if (UAsdaInventorySubsystem* I2 = G2 ? G2->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
				{
					if (!WasOpen->Key && I2->IsWindowOpen()) { I2->CloseWindow(); }
					else if (WasOpen->Key && !I2->IsWindowOpen()) { I2->OpenWindow(); UE_LOG(LogAsdaEtc, Display, TEXT("САМОТЕСТ уборка: рюкзак открыт снова")); }   // ★ R4-b хвосты-2 (15.11.2026) (ревизия 3)
				}
				// ★ R4-b хвосты (15.11.2026) (ревизия, мелочь 7): доп-меню 74600 — как до прогона, и без `quit` (★ 11.10.2026, В3 S1: открытое окно
				//   перекрывало чужие проверки наведения в пачке); страница 0 и второй ряд панели — как до прогона, локально (без 5067).
				if (!MenuWas->Key && S->IsWindowVisible()) { S->CloseWindow(); }
				else if (MenuWas->Key && !S->IsWindowVisible()) { S->OpenWindow(); UE_LOG(LogAsdaEtc, Display, TEXT("САМОТЕСТ уборка: 74600 открыто снова")); }   // ★ R4-b хвосты-2 (15.11.2026) (М1)
				if (UAsdaHotbarSubsystem* Q2 = G2 ? G2->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
				{
					// ★ R4-b хвосты-2 (15.11.2026) (М2): в очередь — только если страница отличается: совпавшая страница в очереди позже легла бы поверх слотов соседа.
					if (PageDiff(Q2, *HotWas) > 0) { Q2->SimulateLoadPage(0, *HotWas); }
					if (Q2->IsSecondRowVisible() != MenuWas->Value) { Q2->SetSecondRowVisible(MenuWas->Value); }
				}
			}), 29.3f, false);
			// ★ R4-b хвосты (15.11.2026) (ревизия, мелочь 9): ИТОГ — сверка уборки со снимком. Перетаскивание сам тест не проверяет (журнал и кадры).
			// ★ R4-b хвосты-2 (15.11.2026) (М2): страница 0 применяется в тике панели — ИТОГ опрашивает каждые 0,1 с (с 29,4 с, до 30 раз), пока страница не
			//   совпадёт со снимком; `quit` — только после ИТОГ. Не совпала за 3 с — провал «страница 0 в очереди панели» (явный, не тихий).
			//   Тот же блок, что у уборщика (общая `PageDiff`).
			const TSharedRef<FTimerHandle> HV = MakeShared<FTimerHandle>();
			const TSharedRef<int32> Tries = MakeShared<int32>(0);
			const TWeakObjectPtr<UGameInstance> WGV = W->GetGameInstance();
			W->GetTimerManager().SetTimer(*HV, FTimerDelegate::CreateWeakLambda(S, [WGV, WasOpen, MenuWas, HotWas, Gate, S, PageDiff, HV, Tries, bQuit]()
			{
				UGameInstance* G3 = WGV.Get();
				// ★ R4-b (16.11.2026) (падение в сборке): снятие этого таймера уничтожает его делегат с захватами (движок `InternalClearTimer`, ветка `Executing`) —
				//   снимать последним действием и через локальную копию ручки (`ClearTimer` пишет `Invalidate` в ручку).
				if (Gate->IsRefused() || !G3) { if (G3) { FTimerHandle Hl = *HV; G3->GetTimerManager().ClearTimer(Hl); } return; }
				const UAsdaHotbarSubsystem* Q3 = G3->GetSubsystem<UAsdaHotbarSubsystem>();
				const int32 Diff = PageDiff(Q3, *HotWas);
				if (Diff > 0 && ++(*Tries) < 30) { return; }   // ждать тика панели
				int32 Ok = 0, Fail = 0;
				auto Chk = [&Ok, &Fail](bool b, const FString& What) { if (b) { ++Ok; } else { ++Fail; UE_LOG(LogAsdaEtc, Error, TEXT("САМОТЕСТ уборка FAIL: %s"), *What); } };
				const UAsdaInventorySubsystem* I3 = G3->GetSubsystem<UAsdaInventorySubsystem>();
				const UAsdaFishBookSubsystem* B3 = G3->GetSubsystem<UAsdaFishBookSubsystem>();
				Chk(I3 && I3->IsWindowOpen() == WasOpen->Key, FString::Printf(TEXT("рюкзак открыт=%d, до прогона %d"), I3 && I3->IsWindowOpen() ? 1 : 0, WasOpen->Key ? 1 : 0));
				Chk(B3 && B3->IsOpen() == WasOpen->Value, FString::Printf(TEXT("книга рыбака открыта=%d, до прогона %d"), B3 && B3->IsOpen() ? 1 : 0, WasOpen->Value ? 1 : 0));
				Chk(S->IsWindowVisible() == MenuWas->Key, FString::Printf(TEXT("доп-меню 74600 открыто=%d, до прогона %d"), S->IsWindowVisible() ? 1 : 0, MenuWas->Key ? 1 : 0));
				Chk(Q3 && Q3->IsSecondRowVisible() == MenuWas->Value, FString::Printf(TEXT("второй ряд панели=%d, до прогона %d"), Q3 && Q3->IsSecondRowVisible() ? 1 : 0, MenuWas->Value ? 1 : 0));
				Chk(Diff == 0, Diff < 0 ? FString(TEXT("страница 0 панели: снимка или панели нет"))
					: FString::Printf(TEXT("страница 0 в очереди панели: слотов не как до прогона %d из 13 через %d опросов (тик панели не применил страницу)"), Diff, *Tries));
				UE_LOG(LogAsdaEtc, Display, TEXT("САМОТЕСТ asda.ExtraMenu.DragSelfTest ИТОГ уборки: пройдено %d, провалов %d (опросов страницы %d)"), Ok, Fail, *Tries);
				if (bQuit && GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }   // ★ R4-b хвосты-2 (15.11.2026): выход — после ИТОГ
				FTimerHandle Hl = *HV;   // ★ R4-b (16.11.2026) (падение в сборке): последним — после этой строки захватов уже нет
				G3->GetTimerManager().ClearTimer(Hl);
			}), 0.1f, true, 29.4f);
		}
		// ★ 11.10.2026 (В3 S1, загрязнение пакета): окно 74600 закрывается за собой — ★ R4-b хвосты (15.11.2026): в уборщике выше, на любом выходе.
		// ★ R4-b хвосты-2 (15.11.2026) (М2): `quit` даёт ИТОГ уборки (после применения страницы 0), а не шаг 29,5 с.
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Сдвиг состава меню — доказательство устойчивости ключа (см. `DebugShiftTable`).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEtcShift(
	TEXT("asda.ExtraMenu.ShiftTable"),
	TEXT("asda.ExtraMenu.ShiftTable <N> — вставить N синтетических строк В НАЧАЛО меню (сдвинуть ")
	TEXT("нумерацию всех настоящих). 0 — вернуть настоящую таблицу. Только рантайм."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		if (UAsdaExtraMenuSubsystem* S = EtcSub(W))
		{
			S->DebugShiftTable(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1);
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Во что разрешается ключ ПРЯМО СЕЙЧАС — тем же входом, которым пользуется слот панели.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEtcResolve(
	TEXT("asda.ExtraMenu.ResolveKey"),
	TEXT("asda.ExtraMenu.ResolveKey <видКлюча 0|1> <ключ> — какой пункт откроет слот панели с таким ")
	TEXT("ключом (0 = порядковый номер строки ИСХОДНОЙ таблицы, 1 = код действия)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaExtraMenuSubsystem* S = EtcSub(W);
		if (!S || Args.Num() < 2) { return; }
		const int32 Kind = FCString::Atoi(*Args[0]);
		const int32 Key  = FCString::Atoi(*Args[1]);
		const FAsdaExtraMenuRow* R = S->FindByStableKey(Kind, Key);
		UE_LOG(LogAsdaEtc, Warning, TEXT("[доп-меню] ключ (вид %d, значение %d) → %s"),
			Kind, Key, R ? *FString::Printf(TEXT("«%s» (команда %d, иконка %d,%d)"),
				*R->Name, R->Cmd, R->IconX, R->IconY) : TEXT("ПУНКТ НЕ НАЙДЕН (слот ничего не откроет)"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GEtcMenuClick(
	TEXT("asda.ExtraMenu.MenuButton"),
	TEXT("Доп-меню «Прочее»: нажать САМУ кнопку трея 73409 (проверка всей цепочки, а не своих полей)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (UAsdaSystemMenuSubsystem* M = GI ? GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr)
		{
			M->ClickButton(kMenuButtonIdNum);
		}
	}));
#endif // ASDA_DEV_CONSOLE
