// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна 43300 «Тултип материалов» (на деле — ЦЕПОЧКА РАЗВИТИЯ ПОРОДЫ, см. шапку `.h`).
#include "UI/AsdaPetEvoTipSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"   // ★ волна 9: гейт показа 43300 — видимость панели 43058
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Pet/AsdaPetTableRows.h"
#include "AsdaBytes.h"

#include "UI/AsdaNotice.h"              // ★ loc-строка заголовка/подписи «О питомце»
#include "Login/AsdaButtonRelay.h"     // ★ мостик «клик крестика → CloseInfoWindow»

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetEvoTip, Log, All);

// ═══════ ★★★ ЕДИНСТВЕННЫЙ КЛЮЧ ОТКАТА ОБОИХ ОТСТУПЛЕНИЙ 25.09.2026 (разбор — в шапке `.h`) ═════
//   Владелец потребовал ДВУХ вещей сразу, и ключ обязан переключать их ВМЕСТЕ: иначе при «0»
//   получилось бы состояние, которого нет ни в оригинале, ни в требовании (кнопка есть, а окно
//   всплывает по наведению — и лезет поверх открытой панели, ровно то, от чего эталон защищался
//   гейтом `ui_core.c:90433`).
//   ★ ЧИТАЕТСЯ ОН РОВНО В ОДНОМ МЕСТЕ — `UAsdaPetEvoTipSubsystem::IsInfoModeOn()`. Панель 43058
//   спрашивает то же самое через него же: две копии `GetValueOnGameThread` рано или поздно
//   разошлись бы (закон `check-existing-before-creating` — общая точка, а не второй читатель).
static ASDA_DEV_CVAR(int32, CVarPetInfoWindow,
	TEXT("asda.Pet.InfoWindow"), 1,
	TEXT("ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА по прямому требованию владельца (25.09.2026). ")
	TEXT("1 (по умолчанию) — в панели 43058 ЕСТЬ шестая кнопка «О питомце», она открывает окно ")
	TEXT("43300 ЗАКРЕПЛЁННЫМ (заголовок, крестик, перетаскивание, память позиции), а показ 43300 ")
	TEXT("ПО НАВЕДЕНИЮ на слот ВЫКЛЮЧЕН. 0 — ДОСЛОВНЫЙ ЭТАЛОН: шестой кнопки нет, закреплённого ")
	TEXT("режима нет, подсказка всплывает по наведению (refineGetSlotItemState@0xDC7550) и ")
	TEXT("гаснет по уходу курсора. ")
	TEXT("★ ПОЛОВИНА ПРО КНОПКУ ПРИМЕНЯЕТСЯ ПРИ СЛЕДУЮЩЕМ ОТКРЫТИИ ОКНА 43000: шестая кнопка ")
	TEXT("рождается клоном ОДИН РАЗ, при разборе панели 43058, поэтому переключение ключа на ")
	TEXT("живом её не появит и не уберёт немедленно. Половина про наведение действует сразу."),
	ECVF_Default);

bool UAsdaPetEvoTipSubsystem::IsInfoModeOn()
{
	return CVarPetInfoWindow.GetValueOnGameThread() != 0;
}

static const FName  kEvoTipWindowId(TEXT("Pet_Tooltips_Win"));
static const TCHAR* kEvoTipWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_Tooltips_Win.WBP_Pet_Tooltips_Win_C");

// Окно-владелец подсказки — «Дом для питомца» 43000. Имя то же, что и у `kCageWindowId` в
//   `AsdaPetCageSubsystem.cpp`: в эталоне подсказка 43300 и клетка 43000 принадлежат ОДНОМУ
//   объекту (`CPetMgr` + `CPetData`), и подсказка обязана рисоваться НАД клеткой.
static const FName  kCageWindowIdForTip(TEXT("Pet_Cage_Win"));

// ═════════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════

void UAsdaPetEvoTipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Cells.SetNum(kStages);
	LastStageTypes.Init(-1, kStages);   // фиксированная длина: только Init (закон `tarray-reset-breaks-fixed-length`)
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// В реестре окон 43300 помечено закрываемым по Esc — закрытие у него одно: спрятаться.
		WM->RegisterEscCloser(kEvoTipWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaPetEvoTipSubsystem::Hide));
	}
	UE_LOG(LogAsdaPetEvoTip, Log, TEXT("[питомец-цепочка] подсистема поднята (окно 43300)"));
}

void UAsdaPetEvoTipSubsystem::Deinitialize()
{
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaPetEvoTipSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	Hide();
	ShownForUid = -1;
	LastStageTypes.Init(-1, kStages);
	// ★ Правило сессии: каждое ЗАПОМНЕННОЕ ИЗ ПАКЕТА/СПИСКА поле обнуляется — иначе номера
	//   и породы ЧУЖОЙ сессии переживают переподключение и попадают в диагностику как свои.
	LastAskedUid = -1;
	LastAskedPetIndex = -1;
	LastAskedEvoKey = -1;
	LastSkipReason.Empty();
	bEmptyTipWarned = false;
	bHoverOffWarned = false;
	// ★ Закрепление принадлежит СЕССИИ: после пересоздания сокета список питомцев другой, и
	//   окно, оставшееся «открытым по кнопке», показывало бы данные ЧУЖОЙ сессии. `Hide()`
	//   выше уже снял признак; строка оставлена явной, чтобы правка `Hide` не унесла это молча.
	bPinned = false;
	(void)Reason;
}

bool UAsdaPetEvoTipSubsystem::EnsureWindow()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kEvoTipWindowId, kEvoTipWbp, kTipZ).Ensure(
		GetGameInstance(), Win, bReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kEvoTipWindowId, this, Win, bShown,
				TEXT("Pet_Tooltips_Win_0_%d"), 9);
			// ═══ ДЕФЕКТ ВЛАДЕЛЬЦА №1 (25.09.2026): ПОДСКАЗКА 43300 УХОДИЛА ПОД ОКНО 43000 ═══
			//   ПРИЧИНА, ДОКАЗАННАЯ КОДОМ (не догадка). 24.09 в `OpenCage` появился подъём окна
			//   43000 наверх обычного слоя (`asda.Pet.CageRaiseOnOpen` → `BringWindowToFront`,
			//   порт `CClass_MapInsert` из `CRefineWnd_Open`, `item_enhance.c:3831`). Он ставит
			//   клетке z = «верх обычного слоя + 1», то есть 746 при статическом `kTipZ = 745`
			//   (`AsdaPetEvoTipSubsystem.h`) — и подсказка, чей z НИКТО не двигал, оказывалась
			//   ПОД клеткой. Панель 43058 от той же беды спасли РОДСТВОМ (`SetWindowOwner`,
			//   `AsdaPetCageSubsystem.cpp:727`), а подсказку 43300 в той волне ПРОСТО ЗАБЫЛИ.
			//   ⚠ Заметить это по логу было нечем: окно ПОКАЗЫВАЛОСЬ (видимость=1, геометрия
			//   живая) — не видно его было только на экране. Ровно поэтому владелец и прислал кадр.
			//   ЛЕЧЕНИЕ — ТО ЖЕ, ЧТО У ПАНЕЛИ, И В ТОЙ ЖЕ ТОЧКЕ (в момент создания окна, когда
			//   цель родства заведомо есть в реестре): `RaiseOwnedAbove` внутри
			//   `BringWindowToFront(43000)` сам держит потомков на ступень выше владельца.
			//   ЭТО НЕ ОТСТУПЛЕНИЕ: у эталона подсказка и клетка — части одного `CPetMgr`, и
			//   порядок в списке детей у оригинала именно такой (подсказка всегда поверх).
			WM.SetWindowOwner(kEvoTipWindowId, kCageWindowIdForTip);
		},
		[]()
		{
			UE_LOG(LogAsdaPetEvoTip, Error, TEXT("[питомец-цепочка] WBP_Pet_Tooltips_Win не загрузился (%s)"), kEvoTipWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

// ★★★ ПРОГРЕВ ОКНА 43300 (ВОЛНА 8, БЛОКЕР-1) — разбор болезни и доказательства у объявления
//   в `.h`. Здесь только сама сборка: `EnsureWindow` внутри себя заканчивает
//   `SetWindowVisible(…, false)` (`AsdaJuiWindowHandle.cpp`, общая точка), то есть окно
//   остаётся СКРЫТЫМ — ровно в том состоянии, в каком оно бывает после «навёл → увёл».
//   Ключ отката `asda.Pet.Prewarm` читает вызывающий (`UAsdaPetCageSubsystem::OpenCage`):
//   здесь ему не место, иначе диагностический вызов `asda.Pet.EvoTipPrewarm` молча ничего
//   бы не делал при выключенном ключе.
bool UAsdaPetEvoTipSubsystem::PrewarmWindow()
{
	// Уже собрано — второй раз не платим (EnsureWindow это и так отсекает, но лог был бы врущим).
	if (bReady && Win)
	{
		return true;
	}
	const double T0 = FPlatformTime::Seconds();
	const bool bOk = EnsureWindow();
	UE_LOG(LogAsdaPetEvoTip, Log,
		TEXT("[питомец-цепочка] прогрев 43300: %s за %.1f мс (откат asda.Pet.Prewarm 0)"),
		bOk ? TEXT("собрано скрытым") : TEXT("НЕ собрано"), (FPlatformTime::Seconds() - T0) * 1000.0);
	return bOk;
}

void UAsdaPetEvoTipSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kEvoTipWindowId);
		WM->CloseWindow(kEvoTipWindowId);
	}
	Win = nullptr;
	TxtPetName = nullptr;
	TxtFamily = nullptr;
	for (FAsdaPetEvoTipCell& C : Cells) { C = FAsdaPetEvoTipCell(); }
	// ★ Крестик — КЛОН, он жил в дереве уничтожаемого виджета; мостик подписки — тоже наш.
	//   Не обнулить их здесь значит оставить висящий указатель на мёртвое поддерево (ровно та
	//   грабля, что уже ловилась на `ImgPreview3d` окна 43000).
	BtnInfoClose = nullptr;
	Relays.Reset();
	bPinned = false;
	bReady = false;
	bShown = false;
}

// ★ РАЗБОР КОНТРОЛОВ. Порядок — `uiUpgradeDialogBindControls@0xEE0E30` (ui_core.c:90403-90419).
//   Имена виджетов — из живого дампа `Reports/pet/ui_dump/43300_Pet_Tooltips_Win.md`.
//   Коллизий имён в этом окне НЕТ (проверено по `wbp_raw/WBP_Pet_Tooltips_Win.txt`): все семь
//   иконок и семь подписей названы по-разному — `Pet_Tooltips_Top_Mark`,
//   `Pet_Tooltips_1_Mark1..3`, `Pet_Tooltips_2_Mark1..3` и соответствующие `*_T*_Text`.
void UAsdaPetEvoTipSubsystem::SetupControls()
{
	bReady = false;
	if (!Win || !Win->WidgetTree) { return; }

	// Пара «иконка → подпись» на каждую из семи стадий, в порядке 43302/43303 … 43314/43315.
	static const TCHAR* IconNames[kStages] = {
		TEXT("Pet_Tooltips_Top_Mark_29_0"),    // 43302
		TEXT("Pet_Tooltips_1_Mark1_23_0"),     // 43304
		TEXT("Pet_Tooltips_1_Mark2_25_0"),     // 43306
		TEXT("Pet_Tooltips_1_Mark3_27_0"),     // 43308
		TEXT("Pet_Tooltips_2_Mark1_16_0"),     // 43310
		TEXT("Pet_Tooltips_2_Mark2_18_0"),     // 43312
		TEXT("Pet_Tooltips_2_Mark3_20_0"),     // 43314
	};
	static const TCHAR* LabelNames[kStages] = {
		TEXT("Pet_Tooltips_Top_T2_Text"),      // 43303
		TEXT("Pet_Tooltips_1_T1_Text"),        // 43305
		TEXT("Pet_Tooltips_1_T2_Text"),        // 43307
		TEXT("Pet_Tooltips_1_T3_Text"),        // 43309
		TEXT("Pet_Tooltips_2_T1_Text"),        // 43311
		TEXT("Pet_Tooltips_2_T2_Text"),        // 43313
		TEXT("Pet_Tooltips_2_T3_Text"),        // 43315
	};

	TArray<UWidget*> All;
	Win->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Pet_Tooltips_Top_T1"))        { TxtPetName = T; continue; }   // 43301
			if (N == TEXT("Pet_Tooltips_Text_last"))     { TxtFamily  = T; continue; }   // 43319
			for (int32 i = 0; i < kStages; ++i)
			{
				if (N == LabelNames[i]) { Cells[i].Label = T; break; }
			}
			continue;
		}
		if (UImage* I = Cast<UImage>(W))
		{
			for (int32 i = 0; i < kStages; ++i)
			{
				if (N == IconNames[i]) { Cells[i].Icon = I; break; }
			}
		}
	}

	int32 Found = 0;
	for (const FAsdaPetEvoTipCell& C : Cells) { if (C.Icon && C.Label) { ++Found; } }
	bReady = (TxtPetName != nullptr && TxtFamily != nullptr && Found == kStages);
	UE_LOG(LogAsdaPetEvoTip, Log,
		TEXT("[питомец-цепочка] 43300 разобрано: 43301=%s 43319=%s пар «иконка+подпись» %d из %d"),
		TxtPetName ? TEXT("да") : TEXT("НЕТ"), TxtFamily ? TEXT("да") : TEXT("НЕТ"), Found, kStages);
}

// ═════════════ ТОЧКА ВХОДА: refineGetSlotItemState@0xDC7550 (item_enhance.c:4265) ═════════════

bool UAsdaPetEvoTipSubsystem::OnCageSlotHovered(int32 PetUniqueId)
{
	// ═══ ★★★ ОТСТУПЛЕНИЕ №2 (25.09.2026): ЭТАЛОННЫЙ ПОКАЗ ПО НАВЕДЕНИЮ УБРАН ЦЕЛИКОМ ═══════════
	//   ТРЕБОВАНИЕ ВЛАДЕЛЬЦА ДОСЛОВНО: «нет, мы переносим на кнопку. Только там».
	//   ЧТО ИМЕННО УБРАНО: весь порт `refineGetSlotItemState@0xDC7550` (item_enhance.c:4265-4280)
	//   перестаёт что-либо ПОКАЗЫВАТЬ. Сам маршрут оставлен на месте и под ключом восстанавливается
	//   дословно — это не удаление кода, а развилка (закон `no-revert-without-asking`:
	//   реверсированное поведение не выбрасывается, оно остаётся достижимым).
	//   ⚠ И ГАСИТЬ ЗДЕСЬ ТОЖЕ НЕЧЕГО: закреплённое окно события мыши не трогают (в этом весь
	//   смысл «отдельного окошка»), а всплывающего в этом режиме не бывает.
	//   Откат: `asda.Pet.InfoWindow 0`.
	if (IsInfoModeOn())
	{
		if (!bHoverOffWarned)
		{
			bHoverOffWarned = true;
			UE_LOG(LogAsdaPetEvoTip, Log,
				TEXT("[питомец-цепочка] наведение на слот 43300 НЕ показывает — отступление по ")
				TEXT("требованию владельца 25.09.2026 («переносим на кнопку, только там»). ")
				TEXT("Окно открывается кнопкой «О питомце» панели 43058. Откат: asda.Pet.InfoWindow 0"));
		}
		return false;
	}

	// :4270 — `sub_930FAC(g_PetUIDialog)` выполняется ВСЕГДА, первой строкой, до любых проверок.
	Hide();

	if (PetUniqueId < 0)
	{
		// :4275 — пустой слот: `uiTooltipShowByItemId(44)`.
		// ★★★ ВОЛНА 9: ВОСПРОИЗВОДИТЬ НЕЧЕГО — СПИСОК `g_pTooltipList` ПУСТ В САМОМ ОРИГИНАЛЕ.
		//   Это не пробел порта: единственный загрузчик класса (`CStrList_LoadStrListMgs@0x989570`)
		//   не вызывается ниоткуда, а в живом дампе объект в послеконструкторном состоянии
		//   (`+40 = -1`). Гейт `uiTooltipShowByItemId` — `if (count < id) return 0`, и при
		//   `count = −1` он истинен ВСЕГДА ⇒ оригинал не показывает эту подсказку НИКОГДА.
		//   Полное доказательство пятью методами — в шапке `.h`. Значит «ничего не показать»
		//   и есть дословное поведение эталона.
		if (!bEmptyTipWarned)
		{
			bEmptyTipWarned = true;
			UE_LOG(LogAsdaPetEvoTip, Log,
				TEXT("[питомец-цепочка] пустой слот → uiTooltipShowByItemId(%d): в ОРИГИНАЛЕ список ")
				TEXT("подсказок пуст по конструкции (count = −1, загрузчик не вызывается), поэтому ")
				TEXT("подсказка не показывается НИКОГДА. Порт повторяет это 1:1 — доделывать нечего."),
				kEmptySlotTipId);
		}
		return false;
	}
	// :4278 — занятый слот: показать 43300 на этом питомце.
	return ShowForPet(PetUniqueId);
}

// ═══════════════ ПОКАЗ: uiUpgradeDialog_Open_F50@0xEE0F50 (ui_core.c:90425-90443) ═════════════

bool UAsdaPetEvoTipSubsystem::ShowForPet(int32 PetUniqueId)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return false; }
	const FAsdaPetSlot* Pet = P->FindByUniqueId(PetUniqueId);
	if (!Pet) { return false; }
	if (!EnsureWindow()) { return false; }

	// ★★★ ГЕЙТ ЭТАЛОНА `if (*(BYTE*)(dword_2C2F2C4 + 292) != 1)` (ui_core.c:90433) — ВОСПРОИЗВЕДЁН
	//   волной 9. Отреверсено: `dword_2C2F2C4` = поле `+0x2F4` глобального `CRefineWnd` (окно
	//   43000) по `0x2C2EFD0`, и лежит в нём указатель на контрол **43058 `Pet_Button_Win`** —
	//   всплывающую панель действий питомца; `+292` = `CControl::isVisible` (`structs_ru.h:393`).
	//   Писатель — `upgradeWindow_BindResultLabels@0xDCBDA0` (`item_enhance.c:5117`).
	//   Подтверждено живым дампом (`Pet_Button_Win` + пять его кнопок в соседних полях).
	//   СМЫСЛ: подсказка и панель ставятся в ОДНУ точку «курсор + 2» (`ui_core.c:90439-90440`),
	//   поэтому под открытой панелью подсказка не всплывает — иначе лезла бы поверх меню.
	//   ⚠ «Панели ещё нет» = ПРОПУСКАТЬ: у эталона поле не пусто уже после привязки контролов
	//   (до первого ПКМ), а `isVisible` там 0. Полный разбор — в шапке `.h`.
	if (const UAsdaPetCageSubsystem* Cage = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr)
	{
		if (Cage->IsActionPanelShown()) { return false; }
	}

	ShownForUid = PetUniqueId;                 // :90434 `*(DWORD*)v4->gap_4F4 = a2`
	LastAskedUid = PetUniqueId;                // диагностика: переживает `Hide()`
	bShown = true;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kEvoTipWindowId, true);   // :90435 `CControl_SetVisible(43300, 1)`
		// ★★★ ВТОРАЯ ПОЛОВИНА ДЕФЕКТА №1: ПОДЪЁМ БЫЛ ТОЛЬКО В КОММЕНТАРИИ.
		//   Здесь стояла одна строка с припиской «:90435 + :90437 (показать и поднять)», но
		//   `SetWindowVisible` z НЕ ТРОГАЕТ вовсе (`AsdaWindowSubsystem.cpp:2043-2053` — только
		//   `SetVisibility`). Эталонный шаг `:90437` — это отдельный вызов
		//   `CClass_MapInsert((CClass *)g_pWindowMgr, *(_DWORD *)v4->gap_4AC)`
		//   (`uiUpgradeDialog_Open_F50@0xEE0F50`, `ui_core.c:90437`), то есть постановка окна в
		//   очередь отложенного подъёма менеджера; наш эквивалент — `BringWindowToFront`
		//   (тот же вывод и то же соответствие, что у `CRefineWnd_Open` в `OpenCage`).
		//   ⇒ Подъём исполняется ЗДЕСЬ, а родство выше страхует от подъёма самой клетки.
		WM->BringWindowToFront(kEvoTipWindowId);       // :90437 `CClass_MapInsert(g_pWindowMgr, 43300)`
	}

	// :90436 — заполнение семи стадий. Оно само прячет окно, если данных нет (:90488).
	if (!DrawStages())
	{
		Hide();
		return false;
	}
	PlaceAtCursor();                            // :90439-90440
	return true;
}

// ★★★ ГАШЕНИЕ ПО ЭТАЛОННОМУ СОБЫТИЮ МЫШИ (разбор и перечень вызывающих — у объявления в `.h`).
//   Закреплённое окно такие события игнорирует; всё остальное — прежний `Hide()` слово в слово.
void UAsdaPetEvoTipSubsystem::HideOnEtalonEvent()
{
	if (bPinned)
	{
		return;   // «отдельное окошко» закрывается только крестиком, Esc или закрытием 43000
	}
	Hide();
}

void UAsdaPetEvoTipSubsystem::Hide()
{
	// ★ Закреплённое окно жёсткое гашение ЗАКРЫВАЕТ, а не «сворачивает»: иначе следующий показ
	//   пришёл бы с чужим признаком и событие мыши погасило бы окно, которое им не управляется.
	bPinned = false;
	if (!bShown) { return; }
	bShown = false;
	ShownForUid = -1;
	// ★ Причина обязана соответствовать состоянию (ревизия волны 4, МЕЛК-5): раньше после
	//   гашения диагностика печатала «показано=нет» вместе с «почему не показано: — (показано)»,
	//   то есть причину ПРЕДЫДУЩЕГО показа. Теперь гашение называет себя само.
	LastSkipReason = TEXT("скрыто событием (курсор ушёл со слота, ЛКМ по слоту, закрытие 43000 или Esc)");
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kEvoTipWindowId, false);
	}
}

// ══════════ ★★★ ЗАКРЕПЛЁННОЕ ОКНО «О ПИТОМЦЕ» — ДОБАВКА ПОРТА (разбор целиком в шапке `.h`) ════

bool UAsdaPetEvoTipSubsystem::OpenInfoWindow(int32 PetUniqueId)
{
	if (!IsInfoModeOn())
	{
		// Ключ выключен ⇒ этого окна в порту как бы нет вовсе. Кнопки, которая сюда зовёт,
		//   при выключенном ключе тоже не существует — это страховка от консольного вызова.
		UE_LOG(LogAsdaPetEvoTip, Log,
			TEXT("[питомец-цепочка] «О питомце» не открыто: asda.Pet.InfoWindow 0 (режим эталона)"));
		return false;
	}

	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetSlot* Pet = P ? P->FindByUniqueId(PetUniqueId) : nullptr;
	if (!Pet)
	{
		LastAskedUid = PetUniqueId;
		LastSkipReason = TEXT("«О питомце»: питомца с таким uid в списке нет");
		UE_LOG(LogAsdaPetEvoTip, Warning, TEXT("[питомец-цепочка] %s"), *LastSkipReason);
		return false;
	}
	if (!EnsureWindow()) { return false; }

	// ★ ГЕЙТА `if (*(BYTE*)(dword_2C2F2C4 + 292) != 1)` ЗДЕСЬ НЕТ — И ЭТО НЕ ПРОПУСК.
	//   Эталонный гейт запрещал показ, пока видна панель 43058, по ОДНОЙ причине: подсказка и
	//   панель ставились в одну точку «курсор + 2» (`ui_core.c:90439-90440`) и подсказка легла бы
	//   поверх меню. Здесь наоборот: окно открывает САМА панель своей кнопкой и сама же себя
	//   прячет следом (`OnPanelInfo` → `HideActionPanel`), а позиция считается от корпуса клетки,
	//   а не от курсора. ⇒ Причины гейта не осталось, воспроизводить его было бы карго-культом.

	ShownForUid = PetUniqueId;
	LastAskedUid = PetUniqueId;
	bShown  = true;
	bPinned = true;                     // ★ именно это делает окно «отдельным»: мышь его не гасит

	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kEvoTipWindowId, true);
		WM->BringWindowToFront(kEvoTipWindowId);
	}

	if (!DrawStages())
	{
		// Данных нет (у породы ключ карты развития −1 — штатный исход для 20 пород из 32).
		//   ★ Молча гасить закреплённое окно НЕЛЬЗЯ: игрок нажал кнопку и обязан понять, что
		//   произошло. Поэтому причина — в лог, а окно закрывается честно.
		UE_LOG(LogAsdaPetEvoTip, Log,
			TEXT("[питомец-цепочка] «О питомце» закрыто сразу: %s"), *LastSkipReason);
		Hide();
		return false;
	}

	EnsureCloseButton();
	PlaceInfoWindow();
	UE_LOG(LogAsdaPetEvoTip, Log,
		TEXT("[питомец-цепочка] окно «О питомце» открыто на uid=%d (порода %d, карта %d). ")
		TEXT("ДОБАВКА ПОРТА по требованию владельца 25.09.2026, откат asda.Pet.InfoWindow 0"),
		PetUniqueId, LastAskedPetIndex, LastAskedEvoKey);
	return true;
}

// ★★★ СЛЕДОВАНИЕ ЗА ВЫБОРОМ (разбор и края — у объявления в `.h`).
void UAsdaPetEvoTipSubsystem::RetargetIfPinned(int32 PetUniqueId)
{
	if (!bPinned) { return; }

	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetSlot* Pet = (P && PetUniqueId >= 0) ? P->FindByUniqueId(PetUniqueId) : nullptr;
	if (!Pet)
	{
		// ★ КРАЙ 2: питомца выбросили/удалили либо выбор снят. Оставлять на экране ПРОШЛОГО
		//   питомца нельзя — это ровно то «показывает прошлого», от которого предостерёг владелец.
		UE_LOG(LogAsdaPetEvoTip, Log,
			TEXT("[питомец-цепочка] «О питомце» закрыто: питомца uid=%d в списке больше нет ")
			TEXT("(выбор снят, питомца выбросили или сменилась сессия)"), PetUniqueId);
		Hide();
		return;
	}
	if (ShownForUid == PetUniqueId)
	{
		return;   // тот же питомец — не перерисовываем, иначе окно мигало бы на каждом клике
	}

	ShownForUid = PetUniqueId;
	LastAskedUid = PetUniqueId;
	// ★ КРАЙ 3: ПОЗИЦИЯ НЕ ТРОГАЕТСЯ. `PlaceInfoWindow` здесь НЕ зовётся сознательно — окно,
	//   перетащенное пользователем, обязано остаться на его месте. Меняется только содержимое.
	const bool bDrawn = DrawStages();
	UE_LOG(LogAsdaPetEvoTip, Log,
		TEXT("[питомец-цепочка] «О питомце» переведено на uid=%d (порода %d, карта %d): %s"),
		PetUniqueId, LastAskedPetIndex, LastAskedEvoKey,
		bDrawn ? TEXT("цепочка перерисована") : *LastSkipReason);
}

void UAsdaPetEvoTipSubsystem::CloseInfoWindow()
{
	if (!bPinned && !bShown) { return; }
	UE_LOG(LogAsdaPetEvoTip, Log, TEXT("[питомец-цепочка] окно «О питомце» закрыто"));
	Hide();
}

// ★★★ КРЕСТИК — КЛОН ЖИВОЙ КНОПКИ 43002 `Pet_Cage_ClsBtn` ОКНА 43000.
//   ПОЧЕМУ КЛОН, А НЕ ПРАВКА АССЕТА: ассеты правятся только с разрешения владельца
//   (`no-ui-regeneration-without-permission`, `owner-asset-edits-never-lose`), а переимпорт
//   `.JUI` стёр бы дописанный контрол (`tools-no-import-side-effects`). Клон же даёт ровно тот
//   вид, что у соседних окон, — потому что берётся ИХ кнопка, со всеми её скинами и состояниями.
//   ПОЧЕМУ ИМЕННО ИЗ 43000, А НЕ ИЗ 43400/43500: клетка — ВЛАДЕЛЕЦ этого окна (`SetWindowOwner`
//   выше), она заведомо жива, пока «О питомце» можно открыть, и её крестик 13×13 — тот же
//   `WinSm`-скин, что у всей группы окон питомцев (дамп `wbp_raw/WBP_Pet_Cage_Win.txt`).
bool UAsdaPetEvoTipSubsystem::EnsureCloseButton()
{
	if (BtnInfoClose) { return true; }
	if (!Win || !Win->WidgetTree) { return false; }

	UAsdaPetCageSubsystem* Cage = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr;
	UUserWidget* CageWin = Cage ? Cage->GetCageWidget() : nullptr;
	UWidget* SrcBtn = CageWin ? AsdaUi::FindNodeExact(CageWin, TEXT("Pet_Cage_ClsBtn")) : nullptr;
	UCanvasPanel* Root = Cast<UCanvasPanel>(Win->WidgetTree->RootWidget);
	if (!SrcBtn || !Root)
	{
		// ★ ЭТО НЕ ОТКАЗ, А «ЕЩЁ РАНО» (закон `no-state-latch-before-shown`): окно 43000 могло
		//   быть не собрано. Повторим при следующем открытии; окно пока без крестика, но живое —
		//   Esc и закрытие 43000 его всё равно закрывают.
		UE_LOG(LogAsdaPetEvoTip, Warning,
			TEXT("[питомец-цепочка] крестик «О питомце» не склонирован: %s"),
			!SrcBtn ? TEXT("кнопки 43002 Pet_Cage_ClsBtn в живом 43000 нет")
			        : TEXT("у 43300 нет корневой канвы"));
		return false;
	}

	UWidget* Clone = AsdaUi::CloneWidgetSubtree(Win, SrcBtn, Root,
		[](const UWidget*) { return FString(TEXT("Pet_Tooltips_Win_X_OPT")); });
	BtnInfoClose = Cast<UButton>(Clone);
	if (!BtnInfoClose) { return false; }

	// Посадка: правый край полосы заголовка 43301 (`pp_31_0` 450,223 370×20 в базе .JUI).
	//   Считаем ОТ ЖИВОГО СЛОТА полосы, а не от чисел .JUI: окно к этому моменту уже могло быть
	//   сдвинуто (память позиции, постановка рядом с клеткой), и абсолютные числа разъехались бы.
	FVector2D BarPos(0.0, 0.0), BarSize(0.0, 0.0);
	UWidget* Bar = AsdaUi::FindNodeExact(Win, TEXT("pp_31_0"));
	FVector2D BtnPos(0.0, 0.0), BtnSize(13.0, 13.0);
	AsdaUi::SlotRect(BtnInfoClose, BtnPos, BtnSize);
	// ★★★ ПОПРАВКА РЕВИЗИИ (СРЕДН-2). ЗДЕСЬ БЫЛ ЛОЖНЫЙ «ДА» В ЛОГЕ: печатались `BtnPos`,
	//   прочитанные ДО посадки, то есть координаты ДОНОРА из окна 43000 (его слот L31 T6
	//   относительно `WinSm_RBar_grp`). А если полосы `pp_31_0` не нашлось, посадки не было
	//   ВОВСЕ — клон оставался в углу окна, — и лог всё равно рапортовал «посажен».
	//   Теперь печатается фактическая точка, а обе ветки названы своими именами.
	bool bSeated = false;
	if (Bar && AsdaUi::SlotRect(Bar, BarPos, BarSize) && BarSize.X > 0.0)
	{
		BtnPos.X = BarPos.X + BarSize.X - BtnSize.X - (double)kInfoCloseGapPx;
		BtnPos.Y = BarPos.Y + FMath::Max(0.0, (BarSize.Y - BtnSize.Y) * 0.5);
		AsdaUi::SetSlotRect(BtnInfoClose, BtnPos, BtnSize);
		bSeated = true;
	}
	BtnInfoClose->SetVisibility(ESlateVisibility::Visible);
	AsdaUi::DisableButtonFocusRing(BtnInfoClose);

	// Подписка: делегаты клон НЕ наследует (`CloneWidgetSubtree` их намеренно пропускает),
	//   поэтому без этой строки крестик был бы мёртвой картинкой.
	UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
	R->Index = 43068;                         // ★ НАШ номер: в .JUI такого контрола нет (см. шапку)
	R->Callback = [this](int32) { CloseInfoWindow(); };
	BtnInfoClose->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
	AsdaUi::AnchorRelay(Relays, R, BtnInfoClose);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)

	if (bSeated)
	{
		UE_LOG(LogAsdaPetEvoTip, Log,
			TEXT("[питомец-цепочка] крестик «О питомце» склонирован из 43002 и посажен в (%.0f,%.0f) %.0fx%.0f"),
			BtnPos.X, BtnPos.Y, BtnSize.X, BtnSize.Y);
	}
	else
	{
		UE_LOG(LogAsdaPetEvoTip, Warning,
			TEXT("[питомец-цепочка] крестик «О питомце» склонирован, но НЕ ПОСАЖЕН: полосы ")
			TEXT("заголовка `pp_31_0` в окне нет, кнопка осталась на координатах донора (%.0f,%.0f)"),
			BtnPos.X, BtnPos.Y);
	}
	return true;
}

// ★★★ РАСКЛАДКА: ЧЕТВЁРТОЕ ОКНО НАБОРА. Три уже расставлены (`PlaceChildBesideCage`): клетка
//   43000 по центру, 43500 слева, 44200 справа. «О питомце» садится ПОД клетку — единственное
//   свободное направление, и оно же самое естественное: окно низкое (376×320 против 512×385
//   у клетки) и под ней не мешает ни одному из боковых.
//   ★ ПЕРЕТАЩЕННОЕ ПОЛЬЗОВАТЕЛЕМ ОКНО НЕ ТРОГАЕМ — это проверяет сам `PlaceChildBesideCage`
//   через `HasSavedWindowOffset` (прямое предупреждение шапки `UAsdaWindowSubsystem`).
// ★★★ ОБЛАСТЬ ЦЕПОЧКИ ЦЕЛИКОМ (требование владельца 25.09.2026 «не пустота без объяснения»).
//
//   ЗАЧЕМ. У ДВАДЦАТИ пород из 32 ключ карты развития равен −1. Эталон в этом случае просто
//   прячет всё окно, а закреплённое окно владелец просил оставлять открытым. Но если оставить
//   его «как есть», на экране висят ТРИ ЖЁЛТЫЕ СТРЕЛКИ, указывающие на ПУСТЫЕ серые плашки —
//   игрок читает это как сломанное окно, а не как «у этой породы нет карты развития».
//   Поэтому область цепочки гасится целиком, и в окне остаётся то, что ЕСТЬ в данных:
//   заголовок «О питомце — ‹имя›» и подпись семейства породы (`pettable + 404`).
//
//   ★ ЭТО НАША ДОБАВКА, А НЕ ЭТАЛОН (закон `never-invent-only-original`): оригинал до этой
//     ветки не доходит вовсе, потому что прячет окно. Действует ТОЛЬКО в закреплённом режиме —
//     при `asda.Pet.InfoWindow 0` сюда не попасть, эталонная подсказка гаснет, как и раньше.
//   ★ ЭТО НЕ «РАНТАЙМ-ФИКС РАЗМЕТКИ» (закон `no-runtime-widget-fixes`): разметка 43300 исправна,
//     и гашение контрола по состоянию — штатный приём самого оригинала (`CControl_SetVisible`
//     рассыпан по всем его окнам). Чинить в ассете здесь нечего.
//
//   ТРИ УЗЛА — ЭТО ВСЯ ОБЛАСТЬ (сверено с деревом ассета `wbp_raw/WBP_Pet_Tooltips_Win.txt`):
//     `Pet_Tooltips_Top_Mark_grp` — верхняя ячейка (43302/43303);
//     `Pet_Tooltips_1_BOX_grp`    — ряд из трёх (43304…43309);
//     `Arrow_w4_Green_grp`        — стрелки между ними.
//   ⚠ `Pet_Tooltips_2_BOX_grp` (вторая тройка 43310…43315) НЕ ТРОГАЕМ: он `Collapsed` в самом
//     `.JUI`, и оригинал его не показывает никогда — трогать его значило бы разойтись с эталоном.
void UAsdaPetEvoTipSubsystem::SetChainVisible(bool bVisible)
{
	if (!Win) { return; }
	static const TCHAR* kChainGroups[] = {
		TEXT("Pet_Tooltips_Top_Mark_grp"),
		TEXT("Pet_Tooltips_1_BOX_grp"),
		TEXT("Arrow_w4_Green_grp"),
	};
	for (const TCHAR* N : kChainGroups)
	{
		AsdaUi::SetShown(AsdaUi::FindNodeExact(Win, N), bVisible);
	}
}

void UAsdaPetEvoTipSubsystem::PlaceInfoWindow()
{
	UAsdaPetCageSubsystem* Cage = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr;
	if (!Cage || !Win) { return; }
	// ★ Результат ОБЯЗАТЕЛЬНО в лог: у общей точки есть МОЛЧАЛИВЫЕ выходы (ключ выключен,
	//   позиция уже сохранена пользователем, геометрии ещё нет и заявка отложена). Без этой строки
	//   «окно не встало рядом» неотличимо от поломки (закон `false-negatives-method`).
	const bool bPlaced = Cage->PlaceChildBesideCage(kEvoTipWindowId, Win);
	UE_LOG(LogAsdaPetEvoTip, Log,
		TEXT("[питомец-цепочка] раскладка «О питомце»: PlaceChildBesideCage = %s"),
		bPlaced ? TEXT("поставлено") : TEXT("НЕТ (ключ/сохранённая позиция/заявка на следующий кадр)"));
}

// Полоса заголовка 43301. Эталон кладёт туда ГОЛОЕ имя питомца (`ui_core.c:90467`); закреплённое
//   окно владелец просил назвать «О питомце», поэтому имя не теряем, а дописываем к заголовку —
//   иначе игрок, открывший окно на одном из шести питомцев, не увидит, на ком оно открыто.
void UAsdaPetEvoTipSubsystem::ApplyTitle(const FString& PetName)
{
	if (!TxtPetName) { return; }
	if (!bPinned)
	{
		TxtPetName->SetText(FText::FromString(PetName));   // :90467 — дословно эталон
		return;
	}
	// ★ СТРОКА НАША (см. `kInfoTitleLocId` в `.h`): в оригинале её нет и быть не может — шестой
	//   кнопки там нет вовсе. Она живёт в `RawAssets/Text/GameText.csv` и, с разрешения владельца
	//   от 25.09.2026, ПЕРЕНЕСЕНА В `DT_GameText` (переимпорт сверен построчно: 4909 → 4910 строк,
	//   единственное отличие — наша `990347`).
	// ★★★ ЗАПАСНОГО ЛИТЕРАЛА ЗДЕСЬ БОЛЬШЕ НЕТ — сознательно, требование владельца: подпись
	//   обязана приходить ИЗ ТАБЛИЦЫ. Молчать при её отсутствии нельзя: в заголовке осталось бы
	//   голое имя питомца, и «таблицу забыли переимпортировать» было бы неотличимо от нормы.
	const FString Head = AsdaNotice::Loc(GetGameInstance(), kInfoTitleLocId);
	if (Head.IsEmpty())
	{
		UE_LOG(LogAsdaPetEvoTip, Error,
			TEXT("[питомец-цепочка] loc %d («О питомце») в DT_GameText НЕ НАЙДЕНА — заголовок ")
			TEXT("остаётся без него. Лечение: python Tools/import_gametext.py"), kInfoTitleLocId);
		TxtPetName->SetText(FText::FromString(PetName));
		return;
	}
	TxtPetName->SetText(FText::FromString(
		PetName.IsEmpty() ? Head : FString::Printf(TEXT("%s — %s"), *Head, *PetName)));
}

// ═══════════ ЗАПОЛНЕНИЕ: uiUpgradeDialogDrawStones@0xEE1030 (ui_core.c:90448-90492) ════════════

bool UAsdaPetEvoTipSubsystem::DrawStages()
{
	if (!bReady) { LastSkipReason = TEXT("контролы 43300 не разобраны"); return false; }
	LastStageTypes.Init(-1, kStages);
	LastSkipReason.Empty();
	LastAskedPetIndex = -1;
	LastAskedEvoKey = -1;

	// :90460-90466 — сначала ВСЁ гасится: семь пустых иконок 32×32, семь пустых подписей и 43319.
	for (int32 i = 0; i < kStages; ++i)
	{
		AsdaUi::ClearImageBrush(Cells[i].Icon);
		if (Cells[i].Label) { Cells[i].Label->SetText(FText::GetEmpty()); }
	}
	if (TxtFamily) { TxtFamily->SetText(FText::GetEmpty()); }

	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetSlot* Pet = (P && ShownForUid >= 0) ? P->FindByUniqueId(ShownForUid) : nullptr;
	if (!Pet)                                         // :90467 `if (*(DWORD*)this->gap_4F4)`
	{
		LastSkipReason = TEXT("питомца с таким uid в списке нет");
		return false;
	}
	LastAskedPetIndex = Pet->PetIndex;

	// :90467 — 43301 = имя питомца (`CPetSlot + 0x0C`). ★ В ЗАКРЕПЛЁННОМ виде к нему спереди
	//   приписывается заголовок «О питомце» — единственная правка этой строки (см. `ApplyTitle`).
	ApplyTitle(Pet->Name);

	// :90468 — строка `pettable[PetIndex]`; её нет ⇒ окно прячется.
	FAsdaPetRow Pr;
	if (!PetRow(Pet->PetIndex, Pr))
	{
		LastSkipReason = FString::Printf(TEXT("строки pettable для породы %d нет"), Pet->PetIndex);
		return false;
	}

	// :90468 — 43319 = строка `pettable + 404`. Колонки у неё в генерируемом заголовке нет,
	//   поэтому читаем из сырой записи (`DataHex` — вся запись 472 б, 1:1 от нуля).
	if (TxtFamily)
	{
		TArray<uint8> Raw;
		if (AsdaBytes::HexToBytes(Pr.DataHex, Raw))
		{
			const FString Fam = AsdaBytes::AsciiFieldAt(Raw, kPetRowFamilyTextOfs, 32);
			if (!Fam.IsEmpty()) { TxtFamily->SetText(FText::FromString(Fam)); }
		}
	}

	// :90469 — ключ карты развития = `pettable + 468` (колонка `WeaponClass`, МИСНЕЙМ, см. шапку).
	const int32 EvoKey = Pr.WeaponClass;
	LastAskedEvoKey = EvoKey;
	const FAsdaPetEvoStageRow* Evo = P->GetEvoStage(EvoKey);
	if (!Evo)                                         // :90489 записи нет ⇒ спрятать окно
	{
		// ★ ЭТО ШТАТНЫЙ ИСХОД, А НЕ ПОЛОМКА. Ключ карты (+0x1D4) есть ровно у ДВЕНАДЦАТИ пород
		//   из 32, у остальных ДВАДЦАТИ он равен −1:
		//     карта 0 → 13, 21, 22, 23;  карта 1 → 20, 24, 25, 26;  карта 2 → 14, 27, 28, 29.
		//   (Пересчитано по замечанию ревизии волны 4, СРЕДН-4: в первой редакции стояло «24 из 32»
		//    при верном перечне — арифметика была неверной, перечень верным.)
		//   Проверено прямым чтением `RawAssets/Data/Character/pettable.bin` (заголовок 20 б,
		//   шаг 472 б, поле +468 = последний i32) и `Data/PetEvolutionTable.bin`.
		//   Для пород без карты оригинал тоже ничего не показывает.
		LastSkipReason = FString::Printf(
			TEXT("у породы %d ключ карты развития (+468) = %d — карты нет"),
			Pet->PetIndex, EvoKey);
		// ★★★ КРАЙ 1 (требование владельца 25.09.2026): «окно должно вести себя разумно, а не
		//   показывать прошлого или пустоту без объяснения». Ключ карты равен −1 у ДВАДЦАТИ пород
		//   из 32, то есть это самый частый исход, а не поломка.
		//   ЧТО ДЕЛАЕМ ПО РЕЖИМАМ — И ПОЧЕМУ ПО-РАЗНОМУ:
		//     • ЭТАЛОННАЯ подсказка (`!bPinned`, ключ `asda.Pet.InfoWindow 0`) — ПРЯЧЕМ окно,
		//       дословно как оригинал (`ui_core.c:90489`: записи нет ⇒ `CControl_SetVisible(0)`).
		//       Трогать эталонную ветку нельзя, её и не трогаем;
		//     • ЗАКРЕПЛЁННОЕ окно — ОСТАВЛЯЕМ ОТКРЫТЫМ. Игрок сам нажал кнопку и сам выбрал
		//       питомца; окно, молча схлопнувшееся под курсором, и есть «пустота без объяснения».
		//       Показанного хватает, чтобы понять состояние, и ВСЁ ЭТО — ДАННЫЕ ЭТАЛОНА, ничего
		//       не выдумано: заголовок 43301 = «О питомце — ‹имя питомца›» (уже проставлен выше),
		//       строка 43319 = подпись семейства породы (`pettable + 404`, тоже выше), а место
		//       цепочки остаётся ПУСТЫМ, потому что карты у этой породы нет.
		//   ⚠ ЧЕГО ЗДЕСЬ НАМЕРЕННО НЕТ: прямой надписи «у этой породы нет карты развития».
		//     Готовой строки в локализации нет (сверено по `RawAssets/Text/GameText.csv`:
		//     ни одной со смыслом «нет карты развития»), а выдумывать текст самовольно
		//     запрещено (`never-invent-only-original`). Нужна такая надпись — заводим ВТОРУЮ
		//     портовую строку и спрашиваем владельца, как спросили про 990347.
		SetChainVisible(false);
		// ★★★ НАДПИСЬ «нет карты развития» (разрешение владельца 25.09.2026, строка 990348).
		//   БЕЗ НЕЁ окно у такой породы было просто пустым, и владелец прямо назвал это
		//   «пустотой без объяснения». Кладём её в 43319 `Pet_Tooltips_Text_last` — это и есть
		//   свободная текстовая область окна (366×75, три строки), и она уже показывает
		//   подпись семейства породы, выставленную выше. Семейство НЕ ТЕРЯЕМ: надпись идёт
		//   следующей строкой под ним.
		//   ⚠ ЗАПАСНОГО ЛИТЕРАЛА НЕТ (то же правило, что у 990347): нет строки — громкая
		//   ошибка в лог, а не выдуманный текст в окне.
		if (bPinned && TxtFamily)
		{
			const FString Note = AsdaNotice::Loc(GetGameInstance(), kNoEvoMapLocId);
			if (Note.IsEmpty())
			{
				UE_LOG(LogAsdaPetEvoTip, Error,
					TEXT("[питомец-цепочка] loc %d («нет карты развития») в DT_GameText НЕ НАЙДЕНА — ")
					TEXT("окно останется без объяснения. Лечение: python Tools/import_gametext.py"),
					kNoEvoMapLocId);
			}
			else
			{
				const FString Fam = TxtFamily->GetText().ToString();
				TxtFamily->SetText(FText::FromString(
					Fam.IsEmpty() ? Note : Fam + TEXT("\n") + Note));
			}
		}
		return bPinned;
	}

	SetChainVisible(true);   // ★ карта есть — область цепочки показываем (снимаем гашение выше)

	// :90472-90490 — семь стадий: порода из `PetType[j]`, иконка `pettable +0x118/+0x138/+0x13C`,
	//   подпись — имя породы `pettable + 4`.
	for (int32 j = 0; j < kStages; ++j)
	{
		const int32 Type = Evo->PetType.IsValidIndex(j) ? Evo->PetType[j] : -1;
		LastStageTypes[j] = Type;
		if (Type < 0) { continue; }                   // стадии нет — ячейка остаётся пустой
		FAsdaPetRow Sr;
		if (!PetRow(Type, Sr)) { continue; }          // :90480 `if (v8)` — строки нет, ячейка пуста
		AsdaUi::ApplyJuiIconCell(Cells[j].Icon, Sr.ResultIcon0, Sr.ResultIcon0U, Sr.ResultIcon0V,
			kIconPx, (float)kIconPx);
		if (Cells[j].Label) { Cells[j].Label->SetText(FText::FromString(Sr.Name)); }
	}
	return true;
}

void UAsdaPetEvoTipSubsystem::PlaceAtCursor()
{
	// :90439-90440 — `copyPair79(g_pWindowMgr, v3)` даёт последнюю точку мыши оконного менеджера,
	//   и окно ставится в (x + 2, y + 2). Корень JUI у импортёра расплющен, поэтому двигаем всех
	//   детей корневой канвы одним смещением — тем же приёмом, что панель 43058 волны 2.
	// ★ Курсора нет (офскрин-прогон, окно не в фокусе) ⇒ окно остаётся на авторской позиции
	//   `.JUI` (447,220). Ровно это делает оригинал, когда менеджер не дал точки.
	FVector2D Cur;
	if (!AsdaUi::CursorInViewportUnits(GetWorld(), Cur))
	{
		UE_LOG(LogAsdaPetEvoTip, Log,
			TEXT("[питомец-цепочка] 43300: курсор недоступен — окно оставлено на авторской позиции .JUI"));
		return;
	}
	FAsdaPopupMenu::PlaceAt(Win, Cur + FVector2D((double)kCursorOffsetPx, (double)kCursorOffsetPx));
	UE_LOG(LogAsdaPetEvoTip, Log, TEXT("[питомец-цепочка] 43300 поставлено в (%.0f,%.0f)"),
		Cur.X + kCursorOffsetPx, Cur.Y + kCursorOffsetPx);
}

// ═══════════════════════════════════════ ДАННЫЕ ═══════════════════════════════════════════════

UAsdaPetSubsystem* UAsdaPetEvoTipSubsystem::Pets() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
}

UAsdaItemTableSubsystem* UAsdaPetEvoTipSubsystem::Items() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
}

bool UAsdaPetEvoTipSubsystem::PetRow(int32 PetIndex, FAsdaPetRow& Out) const
{
	// ★ ВОЛНА 8 (дубли, пункт 7): тело — в общей точке `AsdaPetUi::PetRow`.
	//   ★ ЗДЕСЬ ЭТО НЕ ПРОСТО ПЕРЕЕЗД: прежняя копия ходила СВОИМ `Dt->FindRow("pettable")`
	//   мимо общей `UAsdaItemTableSubsystem::GetPet` (та внутри `ResolveRow`), то есть поведение
	//   четвёртой копии могло разойтись с тремя остальными на любой правке разрешения строк.
	//   Возврат по-прежнему ПО ЗНАЧЕНИЮ: окно держит до восьми строк разом (своя + семь стадий).
	return AsdaPetUi::PetRow(GetGameInstance(), PetIndex, Out);
}

// ════════════════════════════════════ ДИАГНОСТИКА ═════════════════════════════════════════════

FString UAsdaPetEvoTipSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно 43300 «Цепочка развития»: разобранные контролы ===\n");
	S += FString::Printf(TEXT("  43301 Pet_Tooltips_Top_T1      %s\n"), TxtPetName ? TEXT("есть") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  43319 Pet_Tooltips_Text_last   %s\n"), TxtFamily ? TEXT("есть") : TEXT("НЕТ"));
	for (int32 i = 0; i < kStages; ++i)
	{
		S += FString::Printf(TEXT("  %d иконка=%s · %d подпись=%s\n"),
			kFirstIconId + 2 * i, Cells[i].Icon ? TEXT("есть") : TEXT("НЕТ"),
			kFirstIconId + 2 * i + 1, Cells[i].Label ? TEXT("есть") : TEXT("НЕТ"));
	}
	return S;
}

FString UAsdaPetEvoTipSubsystem::DumpState() const
{
	FString S = TEXT("\n═══════════ 43300 «Цепочка развития»: состояние ═══════════\n");
	S += FString::Printf(TEXT("  показано=%s, контролы разобраны=%s, на питомце uid=%d\n"),
		bShown ? TEXT("да") : TEXT("нет"), bReady ? TEXT("да") : TEXT("нет"), ShownForUid);
	// ★ Печатаем ПОСЛЕДНИЙ ЗАПРОС, а не только показанное: у большинства пород карты нет, и
	//   «окно не показалось» — правильный исход, который обязан быть объяснён числом.
	S += FString::Printf(TEXT("  последний запрос: uid=%d порода=%d ключ карты развития (+468)=%d\n"),
		LastAskedUid, LastAskedPetIndex, LastAskedEvoKey);
	S += FString::Printf(TEXT("  почему не показано: %s\n"),
		LastSkipReason.IsEmpty() ? TEXT("— (показано)") : *LastSkipReason);
	UAsdaPetSubsystem* P = Pets();
	const FAsdaPetSlot* Pet = (P && ShownForUid >= 0) ? P->FindByUniqueId(ShownForUid) : nullptr;
	if (Pet)
	{
		FAsdaPetRow Pr;
		const bool bRow = PetRow(Pet->PetIndex, Pr);
		S += FString::Printf(TEXT("  питомец: порода=%d имя=[%s], строка pettable=%s, ключ карты развития (+468)=%d\n"),
			Pet->PetIndex, *Pet->Name, bRow ? TEXT("есть") : TEXT("НЕТ"), bRow ? Pr.WeaponClass : -1);
	}
	S += TEXT("  породы семи стадий: ");
	for (int32 i = 0; i < LastStageTypes.Num(); ++i)
	{
		S += FString::Printf(TEXT("%d%s"), LastStageTypes[i], (i + 1 < LastStageTypes.Num()) ? TEXT(", ") : TEXT("\n"));
	}
	S += FString::Printf(TEXT("  ветка пустого слота (uiTooltipShowByItemId %d): в ОРИГИНАЛЕ список ")
		TEXT("подсказок пуст (count = −1) ⇒ не показывается никогда; порт 1:1%s\n"), kEmptySlotTipId,
		bEmptyTipWarned ? TEXT(" (ветка уже проходилась в этой сессии)") : TEXT(""));
	// ★ Состояние гейта обязано быть видно в диагностике, а не только в комментарии (шапка .h).
	{
		const UAsdaPetCageSubsystem* Cage = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr;
		const bool bPanel = (Cage != nullptr) && Cage->IsActionPanelShown();
		S += FString::Printf(
			TEXT("  гейт Open_F50 (`dword_2C2F2C4 + 292`): ОТРЕВЕРСЕН — это видимость панели 43058\n")
			TEXT("    `Pet_Button_Win` (поле +0x2F4 окна 43000, +292 = CControl::isVisible).\n")
			TEXT("    Панель сейчас %s ⇒ подсказка 43300 %s\n"),
			bPanel ? TEXT("ВИДНА") : TEXT("скрыта"),
			bPanel ? TEXT("НЕ показывается (как в оригинале)") : TEXT("показывается"));
	}
	S += TEXT("  вторая тройка стадий 43310…43315: скрыта родителем `Pet_Tooltips_2_BOX` (Visible=0\n")
	     TEXT("    в .JUI, idnum у него нет) — оригинал её тоже не показывает; ячейки заполняются\n");
	// ★★★ ОТСТУПЛЕНИЯ 25.09.2026 ОБЯЗАНЫ БЫТЬ ВИДНЫ В ДИАГНОСТИКЕ, А НЕ ТОЛЬКО В КОММЕНТАРИИ.
	//   Иначе «окно не всплыло по наведению» неотличимо от поломки, а это штатный режим.
	S += FString::Printf(
		TEXT("  ★ режим «О питомце» (asda.Pet.InfoWindow) = %s\n")
		TEXT("    окно сейчас %s\n")
		TEXT("    крестик (клон 43002 Pet_Cage_ClsBtn) = %s\n")
		TEXT("    показ по наведению на слот: %s\n")
		TEXT("    перетаскивание: ручка объявлена В ДАННЫХ .JUI (EVS EVT=6 RET=20003 CtrlID=43300\n")
		TEXT("      у узла Pet_Tooltips_Top_T1, gamemain_jui.ini:90828) ⇒ память позиции в\n")
		TEXT("      [AsdaWindowLayout] работает общим механизмом, своего кода у окна нет\n"),
		IsInfoModeOn() ? TEXT("1 — как просит владелец (ОТСТУПЛЕНИЕ)") : TEXT("0 — ДОСЛОВНЫЙ ЭТАЛОН"),
		bPinned ? TEXT("ЗАКРЕПЛЕНО (открыто кнопкой, мышью не гасится)")
		        : (bShown ? TEXT("показано всплывающей подсказкой (эталон)") : TEXT("скрыто")),
		// ★ Причина обязана соответствовать режиму: при ключе 0 крестика НЕТ ПО ЗАМЫСЛУ, и писать
		//   про «клетку не собрали» значило бы врать (ровно тот класс ложных «нет», от которого
		//   предостерегает закон `false-negatives-method`).
		BtnInfoClose ? TEXT("есть")
		             : (IsInfoModeOn() ? TEXT("НЕТ — клетка 43000 не была собрана в момент открытия")
		                               : TEXT("нет по замыслу (ключ 0: закреплённого режима не бывает)")),
		IsInfoModeOn() ? TEXT("ВЫКЛЮЧЕН (требование владельца «переносим на кнопку, только там»)")
		               : TEXT("включён (эталон, refineGetSlotItemState@0xDC7550)"));
	return S;
}

// ════════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ══════════════════════════════════════════

static UAsdaPetEvoTipSubsystem* EvoTipFromWorld(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAsdaPetEvoTipSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdEvoTipHover(
	TEXT("asda.Pet.EvoTipHover"),
	TEXT("asda.Pet.EvoTipHover <uid|-1> — пройти БОЕВОЙ путь наведения на слот окна 43000 ")
	TEXT("(`refineGetSlotItemState`): −1 = ветка пустого слота, иначе показ цепочки развития."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetEvoTipSubsystem* S = EvoTipFromWorld(W);
		if (!S || Args.Num() < 1) { return; }
		const int32 Uid = FCString::Atoi(*Args[0]);
		const bool bOk = S->OnCageSlotHovered(Uid);
		UE_LOG(LogAsdaPetEvoTip, Log, TEXT("[питомец-цепочка] asda.Pet.EvoTipHover %d → %s"),
			Uid, bOk ? TEXT("показано") : TEXT("не показано"));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ КОМАНДА ДЛЯ ЖИВОЙ ПРОВЕРКИ ДОБАВКИ ПОРТА: открыть/закрыть окно «О питомце» боевым путём.
//   ⚠ Это НЕ замена проверке настоящей кнопкой: путь игрока проверяется `asda.UI.SimClickWidget
//   Pet_Button_Win Pet_Button_INFO_OPT` (закон `diagnostic-must-use-real-path`). Команда нужна
//   там, где панель поднять нечем (сверка состояния, прогон без мыши).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetInfoOpen(
	TEXT("asda.Pet.InfoOpen"),
	TEXT("asda.Pet.InfoOpen <uid> — открыть окно «О питомце» (43300 в закреплённом виде). ")
	TEXT("ДОБАВКА ПОРТА по требованию владельца 25.09.2026, откат asda.Pet.InfoWindow 0."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetEvoTipSubsystem* S = EvoTipFromWorld(W);
		if (!S || Args.Num() < 1) { return; }
		const int32 Uid = FCString::Atoi(*Args[0]);
		UE_LOG(LogAsdaPetEvoTip, Log, TEXT("[питомец-цепочка] asda.Pet.InfoOpen %d → %s"),
			Uid, S->OpenInfoWindow(Uid) ? TEXT("открыто") : TEXT("НЕ открыто"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetInfoClose(
	TEXT("asda.Pet.InfoClose"),
	TEXT("asda.Pet.InfoClose — закрыть окно «О питомце» (то же, что крестик)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetEvoTipSubsystem* S = EvoTipFromWorld(W)) { S->CloseInfoWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdEvoTipControls(
	TEXT("asda.Pet.EvoTipControls"),
	TEXT("asda.Pet.EvoTipControls — печать КАЖДОГО разобранного контрола окна 43300."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetEvoTipSubsystem* S = EvoTipFromWorld(W))
		{
			UE_LOG(LogAsdaPetEvoTip, Log, TEXT("%s"), *S->DumpControls());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdEvoTipState(
	TEXT("asda.Pet.EvoTipState"),
	TEXT("asda.Pet.EvoTipState — состояние окна 43300: на ком показано, семь пород стадий, ветка пустого слота."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetEvoTipSubsystem* S = EvoTipFromWorld(W))
		{
			UE_LOG(LogAsdaPetEvoTip, Log, TEXT("%s"), *S->DumpState());
		}
	}));
#endif // ASDA_DEV_CONSOLE
