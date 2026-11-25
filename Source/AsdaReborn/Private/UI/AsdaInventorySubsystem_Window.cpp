// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Window.cpp — ★ R3-c 12.11.2026 (разрез AsdaInventorySubsystem.cpp, дословный перенос): виджет и контролы окна, зоны, видимость, открыть/закрыть, раскрытие панели, полоса прокрутки.
//   Общее для частей — в AsdaInventorySubsystem_Private.h; шапка и реверс — в AsdaInventorySubsystem.cpp.
#include "UI/AsdaInventorySubsystem.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель

// ★ Волна 1 «применение предметов»: эффект/звук по `linkCode` и состояния 265/266 из 5029 —
//   через подсистему эффектов (своих копий спавна и поиска актора не заводим, [[port-shared-points]]).
#include "UI/AsdaAnimatedPlane.h"
// ★ Только ради ОДНОЙ статической таблицы цветов заточки (`itemGetGradeColor@0xD29940`) — второй
//   копии палитры не заводим (закон «аудит дублей»). Сам файл тултипа отсюда не правится.
#include "Resources/AsdaSoundSubsystem.h"  // ★ кнопка 449 — звук id 5 ветки 66561 (волна 5 заточки)
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
#include "Login/AsdaButtonRelay.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
// ★ Явно, а не «через unity»: файл зовёт `GI->GetSubsystem<>()` больше сотни раз. Без этого include
//   одиночная сборка файла (`Build.bat … -SingleFile=…`) не проходит — а она единственный способ
//   проверить компиляцию, пока в редакторе активен Live Coding и полная сборка запрещена.
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaInventorySubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaInventorySubsystem*.cpp

static const TCHAR* kInvenWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_INVEN_WIN.WBP_INVEN_WIN_C");

namespace AsdaInvScroll {
// ═══════════ ПОЛОСА ПРОКРУТКИ: ДАННЫЕ СКИНА `Nomal_Scr` (JuiSkin.ini, [Ctrl] Name = "Nomal_Scr") ═══════════
//   Разбор целиком — в шапке `AsdaInventorySubsystem.h`, раздел «ПОЛОСА ПРОКРУТКИ». Здесь ТОЛЬКО
//   числа из данных: ни одна константа не подобрана «на глаз».
//     Type = "ScrollBar", ScrollBarType = "1" (вертикальный), ImageFileName = "N_01.tga" (512×512),
//     Diffuse: дорожка ES0 = FFFFFFFF, стрелки и бегунок ES1–ES3 = 64FFFFFF (альфа ≈ 0.39) — тинт кисти
//     = Diffuse плана (★ волна 13: прежняя строка «всех планов FFFFFFFF» была неверной, см. `AsdaNomalScr`).
// * D3 + Дд1 (аудит `code_dedup_audit_2.md`): ДАННЫЕ СКИНА `Nomal_Scr` (атлас, ширина, кнопка,
//   минимальная длина бегунка, все пять UV) переехали в `AsdaNomalScr` (UI/AsdaScrollBarNomal.h) —
//   они побайтово совпадали со вторым экземпляром в книге навыков, и правка UV в одном месте
//   разъезжала полосу другого окна. Здесь остаются ТОЛЬКО числа ЭТОГО окна.
	// Позиция полосы = (rect.right, rect.top) КОЛЁСНОГО ОКНА 494 (`uiControl_attachScrollbar`),
	//   а его rect в JUI — "198 164" при Origin (126,320) в базе 1024x768.
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): было зашито 198 = «ширина 6 колонок»; сетка 12×5 ⇒ смещение = kGridCols · 33.
	//   При 12×5 полоса не показывается вовсе (прокручивать нечего, `IsScrollBarShown`), смещение — для полноты.
	static constexpr float kOffsetX = UAsdaInventorySubsystem::kGridCols * 33.f;
	static constexpr float kHeight  = 164.f;
	static constexpr float kJuiStep = 33.f;    // авторский шаг сетки — база для коэффициента масштаба
}

// ★★★ Д10: МЕНЮ ВЫБОРА ВИДА СОРТИРОВКИ — ОТКЛЮЧЕНО ПО РЕШЕНИЮ ЮЗЕРА (2026-08-13).
//   И у эталона, и у Global всплывашка 80410/83690 бессмысленна: её пункты запускают ОДИН И ТОТ ЖЕ
//   порядок (аргумент `cmdId − 80411` телом функции не читается, таблица порядка в бинаре одна,
//   нотисы 112116/112406 встречаются в `.text` ровно по одному разу), а третий пункт — «Закрыть».
//   Поэтому кнопка «A» раскладывает сразу, без промежуточного меню.
static ASDA_DEV_CVAR(int32, CVarLegacyArrangeMenu,
	TEXT("asda.Inven.LegacyArrangeMenu"),
	0,
	TEXT("Меню выбора вида сортировки: 0 = кнопка сортирует СРАЗУ (Д10, решение юзера); "
	     "1 = как в оригинале — кнопка раскрывает всплывашку 80410 с пунктами 80411-80413."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarLegacyScrollRange,
	TEXT("asda.Inven.LegacyScrollRange"),
	0,
	TEXT("Диапазон прокрутки сетки: 0 = считается по РАЗМЕРУ контейнера (Д2); "
	     "1 = эталонные SetScrollRange(4,9)/(4,5) — на вкладке «Снаряжение» лишняя позиция с 6 пустыми ячейками."),
	ECVF_Default);

// ★★★ Д4 ОТМЕНЁН (круг 2 теста). Прошлый заход разрешил стрелку 408 на обеих вкладках, сославшись
//   на то, что правая панель «умеет показывать и расходник». ПЕРЕПРОВЕРКА ЭТАЛОНА ЭТО ОПРОВЕРГЛА:
//   в `CInventoryUI_OnSlotClick@0xD01F50` весь блок клика по ячейке сумки (вместе с
//   `ItemTooltip_PopulateInfoPanel(this, 1, -1)` и авто-раскрытием) стоит ПОД условием
//   `if (*(_DWORD *)&this->gap_1B0[84])` (item_inventory.c:36266) — это флаг «выбрана вкладка
//   Снаряжение», который ставит `inventoryItemDetailSetVisible@0xD173A0`. На вкладке «Расходники»
//   клик уходит в ветку 36355+ и панель НЕ наполняет вовсе. Значит гейт кнопки 408 — не дефект,
//   а часть модели окна. Возвращаем эталон; Д4 остаётся ключом на случай, если юзер захочет обратно.
static ASDA_DEV_CVAR(int32, CVarExpandOnBothTabs,
	TEXT("asda.Inven.ExpandOnBothTabs"),
	0,
	TEXT("Стрелка 408 «развернуть»: 0 = как эталон — только на вкладке «Снаряжение»; "
	     "1 = старое поведение Д4, работает и на «Расходниках»."),
	ECVF_Default);

// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 17, баг 5: «стрелка капризная, надо кликнуть мимо, а потом на предмет»).
//   [ОРИГИНАЛ] Стрелка 408 видна и на «Расходниках» (`uiPanelRelayoutForMode@0xD199B0` показывает её при любой вкладке, окно
//   открывается на вкладке 0), но нажатие работает только при `GetSelectedIndex(402) == 1` (ui_core.c:47526, тот же гейт
//   в shop_trade.c:11998). На «Расходниках» видимая кнопка мертва; панель там открывает только щелчок по надетой вещи
//   (ui_core.c:33973) или смена вкладки — это и есть «кликнуть мимо, потом на предмет». Самотест `asda.SelfTest.InvArrow`:
//   на «Снаряжении» стрелка срабатывает при любом порядке щелчков, отказ — только на «Расходниках».
//   [ИСПРАВЛЕНИЕ] Панель справа — панель снаряжения (на «Расходниках» эталон её не наполняет), поэтому стрелка на «Расходниках»
//   сама переводит сумку на «Снаряжение» и раскрывает панель. Память вкладки (`asda.Inven.RememberTab`) переход не пишет —
//   как и автопереход по надетой вещи. Ключ `asda.Inven.ExpandOnBothTabs 1` важнее: тогда панель раскрывается без смены вкладки.
static ASDA_DEV_CVAR(int32, CVarArrowOpensEquipTab,
	TEXT("asda.Inven.ArrowOpensEquipTab"),
	1,
	TEXT("[ИСПРАВЛЕНИЕ] Стрелка 408 на вкладке «Расходники»: 1 (по умолчанию) — переводит сумку на «Снаряжение» и раскрывает "
	     "панель; 0 — как оригинал: видна, но не делает ничего."),
	ECVF_Default);

// ★ Д8 (просьба ЮЗЕРА, отступление от эталона). Эталон авто-раскрывает правую панель при КАЖДОМ
//   клике по предмету на вкладке «Снаряжение» (`if (!IsVisible(m_pRightSideBack)) uiPanelExpand`,
//   item_inventory.c:36325/36337) и НИЧЕГО не помнит о ручной свёртке — то есть нажатую юзером
//   стрелку 412 следующий же клик отменяет. Юзер просил, чтобы ручное скрытие уважалось.
static ASDA_DEV_CVAR(int32, CVarRememberPanelCollapse,
	TEXT("asda.Inven.RememberPanelCollapse"),
	1,
	TEXT("Ручная свёртка правой панели (412): 1 = запоминается, авто-раскрытие по клику предмета "
	     "не срабатывает до смены вкладки/переоткрытия окна (Д8, просьба юзера); "
	     "0 = как эталон — любой клик по предмету снова раскрывает панель."),
	ECVF_Default);

// ★ Д5 — стиль подписей вкладок. Решение ЮЗЕРА: чёрный текст без обводки.
static ASDA_DEV_CVAR(int32, CVarLegacyTabTextStyle,
	TEXT("asda.Inven.LegacyTabTextStyle"),
	0,
	TEXT("Подписи вкладок 402: 0 = ЧЁРНЫЕ без обводки (решение юзера, Д5); "
	     "1 = как положил импортёр из данных JUI (CapInfo-серый с обводкой)."),
	ECVF_Default);

// ★ D5 (аудит `code_dedup_audit_2.md`): разбор имени плана переехал в `AsdaUi::ParsePlaneName`
//   (закон `jui-importer-widget-naming`: интерактив носит ТОЧНОЕ имя idc, планы — два числовых
//   хвоста). Было четыре копии формулы — стала одна.
// ══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════
// ★ [ИСПРАВЛЕНИЕ] 07.11.2026 (просьба владельца, пачка 12; новая возможность, у оригинала нет: `uiStorageWndReset` всегда открывает вкладку 0).
//   Сумка открывается на вкладке, которую игрок последней выбрал РУЧНО (кнопки 402), в том числе после перезапуска игры. Хранение — как у
//   трекера заданий: `GGameUserSettingsIni`, секция `AsdaInventory`, ключ `LastTab`, запись сразу на диск. Значение общее для установки.
//   Автопереход при нажатии на надетый слот (`asda.Inven.EquipSelectTab`) сюда не пишется: он важнее памяти, пока окно открыто,
//   и не подменяет выбор игрока. 1 = запоминать; 0 = как оригинал, всегда вкладка 0.
static ASDA_DEV_CVAR(int32, CVarRememberTab,
	TEXT("asda.Inven.RememberTab"), 1,
	TEXT("1 = сумка открывается на вкладке последнего ручного выбора, и между запусками игры; 0 = как оригинал, всегда вкладка 0."),
	ECVF_Default);
static const TCHAR* kInvTabSection = TEXT("AsdaInventory");   // секция настроек сумки в GameUserSettings
static const TCHAR* kInvTabKey     = TEXT("LastTab");         // номер вкладки 0/1

// Записать ручной выбор вкладки (молча при выключенном ключе).
static void AsdaInvSaveTab(int32 Tab)
{
	if (CVarRememberTab.GetValueOnGameThread() == 0 || !GConfig) { return; }
	GConfig->SetInt(kInvTabSection, kInvTabKey, Tab, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);   // без `Flush` значение пропало бы при вылете (как `AsdaQuestTracker`)
}

// Прочитать запомненную вкладку; 0 — нет записи, ключ выключен или мусор.
static int32 AsdaInvLoadTab()
{
	int32 Tab = 0;
	if (CVarRememberTab.GetValueOnGameThread() == 0 || !GConfig) { return 0; }
	GConfig->GetInt(kInvTabSection, kInvTabKey, Tab, GGameUserSettingsIni);
	return Tab == 1 ? 1 : 0;
}

bool UAsdaInventorySubsystem::EnsureWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(InvenWidget))
	{
		return true;
	}
	ResetWidget();
	// Корень SelfHitTestInvisible: растянутый на вьюпорт UUserWidget с Visible съел бы клики по миру;
	//   блокировку мира даёт ЗОНА ИНТЕРФЕЙСА (RegisterUiZone), а не hit-test виджета.
	InvenWidget = WM->OpenWindowByPath(kInvenWindowId, kInvenWbp, kInvenZ, ESlateVisibility::SelfHitTestInvisible);
	if (!InvenWidget)
	{
		UE_LOG(LogAsdaInven, Error, TEXT("[инвентарь] WBP_INVEN_WIN не загрузился (%s)"), kInvenWbp);
		return false;
	}
	SetupControls();
	RegisterZone();
	// ★★★ ШАГ 2 ЭТАЛОННОГО `uiCloseAllWindows@0xD61960` (ui_core.c:54353):
	//     `if (sub_93248D(g_pInventoryWnd)) sowelWindowClose(g_pInventoryWnd, 1);`
	//   `sowelWindowClose@0xCFF760` — МИСНОМЕР IDA, на деле это `CInventoryWindow::Close`
	//   (item_inventory.c:35781: `CControl_SetVisible(m_pInvenWin, 0)`, `m_bOpened = 0`, сброс
	//   выборов, `uiPanel_SetTabVisible(g_pSystemMainWnd, 1, 0)`), а `sub_93248D` — геттер
	//   «инвентарь открыт». То есть вход в сцену премиум-гачи в ОРИГИНАЛЕ закрывает инвентарь.
	//   Расписываемся в менеджере окон СВОИМ `CloseWindow` — он и флаг `bWindowOpen` снимет, и
	//   выборы сбросит. Без этой росписи прежний порт звал `WM->CloseAll()`, флаг оставался
	//   взведённым, и `Tick` → `EnsureWidget` воскрешал окно следующим же кадром
	//   (доказано по живому логу владельца, `Reports/gacha_close_windows.md` §2).
	if (UAsdaWindowSubsystem* WMgr = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WMgr->RegisterWindowCloser(kInvenWindowId,
			FSimpleDelegate::CreateUObject(this, &UAsdaInventorySubsystem::CloseWindow));
	}
	ApplyWindowVisibility();
	RefreshTabVisual();
	RefreshGrid();
	RefreshEquip();
	RefreshMoney();
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] окно INVEN_WIN открыто (z=%d), контролы разобраны: %s"),
		kInvenZ, bControlsReady ? TEXT("да") : TEXT("НЕТ"));
	return true;
}

// ══════════════════════════════ РАЗБОР ДЕРЕВА ВИДЖЕТОВ ═════════════════════════════════════════
//   Имена — закон `jui-importer-widget-naming`: план = `{idc}_{juiIdx}_{plane}`, интерактив/Text =
//   ТОЧНОЕ имя idc, группа = `{idc}_grp`. Скин `NoImage_icon` у ячеек имеет Type = Text, поэтому
//   импортёр сделал на каждую ячейку ПАРУ: план-картинку (иконка) и TextBlock (счётчик стака).
//   ★ Цвет/выравнивание/обводка/кадры кнопок импортёр УЖЕ берёт из данных (`ui-text-color-exs`,
//   `ui-button-hover-click-rule` — импортёр починен 2026-08-08), поэтому здесь их НЕ дублируем;
//   рантайм трогает только кегль счётчика, которого в .JUI нет вовсе.
// ★★★ Позиция виджета в системе координат КОРНЯ окна: сумма Left/Top всех CanvasPanelSlot по
//   цепочке родителей. Нужна потому, что импортёр раскладывает JUI-иерархию по ВЛОЖЕННЫМ канвасам:
//   ячейки сетки лежат в `INVEN_Icon_Base_grp` (сам он смещён на 5,5 внутри `INVEN_ITEMTRAY2_grp`),
//   а курсор выбора 409 — прямо в `INVEN_ITEMTRAY2_grp`. Ставить курсору Offsets ячейки «как есть»
//   значит промахнуться ровно на смещение канваса — это и был сдвиг анимированной рамки (п.10 теста).
// ★ D6 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::CanvasRootPos` (вторая копия
//   жила в подсистеме самоцветов). Файловый форвард — чтобы не переписывать восемь вызовов ниже.
static FORCEINLINE FVector2D CanvasRootPos(const UWidget* W) { return AsdaUi::CanvasRootPos(W); }

void UAsdaInventorySubsystem::SetupControls()
{
	bControlsReady = false;
	if (!InvenWidget || !InvenWidget->WidgetTree) { return; }

	GridUi.SetNum(kGridCells);
	EquipUi.SetNum(kEquipSlots);

	TArray<UWidget*> All;
	InvenWidget->WidgetTree->GetAllWidgets(All);

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		// ── интерактив: точное имя idc ──
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("INVEN_WINCLOSEBUT"))       { BtnClose = Btn;    continue; }
			if (N == TEXT("INVEN_OPENBUT"))           { BtnExpand = Btn;   continue; }
			if (N == TEXT("INVEN_CLOSEBUT"))          { BtnCollapse = Btn; continue; }
			if (N == TEXT("INVEN_TRASHBUT"))          { BtnTrash = Btn;    continue; }
			if (N == TEXT("INVEN_CHOICEBUT_OPT0"))    { TabOpt0 = Btn;     continue; }
			if (N == TEXT("INVEN_CHOICEBUT_OPT1"))    { TabOpt1 = Btn;     continue; }
			if (N == TEXT("Inven_auto_array"))        { BtnAutoArrange = Btn; continue; }
			// Нижний ряд (интерактив ⇒ имя ТОЧНОЕ, закон `jui-importer-widget-naming`).
			if (N == TEXT("Inven_ItemUpgrade_Btn"))   { BtnItemUpgrade = Btn; continue; }
			if (N == TEXT("Inven_Smelting_Btn"))      { BtnSmelting = Btn;    continue; }
			if (N == TEXT("Inven_Disassembly_Btn"))   { BtnDisassembly = Btn; continue; }
			if (N == TEXT("Inven_PetCageBtn"))        { BtnPetCage = Btn;     continue; }
			if (N == TEXT("Inven_primium_item_btn"))  { BtnPremiumItem = Btn; continue; }
			for (int32 D = 1; D <= 3; ++D)
			{
				if (N == FString::Printf(TEXT("Inven_auto_array_box_d%d"), D))
				{
					AutoArrangeItems.SetNum(FMath::Max(AutoArrangeItems.Num(), D));
					AutoArrangeItems[D - 1] = Btn;
					break;
				}
			}
			continue;
		}

		// ── тексты (TextBlock с точным именем idc) ──
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			Tb->SetAutoWrapText(false);   // закон `ui-autowrap-canvas-gotcha`
			if (N == TEXT("INVEN_MONEYTXT"))         { MoneyText = Tb; continue; }
			// ★ П10 аудита: подпись «Золото» (10042). Эталон `InitControls` ставит ей нотис 12006
			//   (item_inventory.c:35484-35489) и НЕ трогает видимость — в .JUI контрол скрыт,
			//   поэтому текст ставим, а `Visible = 0` оставляем эталонным.
			if (N == TEXT("INVEN_CASHTXT"))          { Tb->SetText(FText::FromString(LocText(12006, TEXT("")))); continue; }
			if (N == TEXT("INVEN_CHOICEBUT_Text"))   { TabText = Tb;   continue; }
			// ★ Подпись ВТОРОЙ вкладки лежит В АССЕТЕ (её кладёт шаг `Tools/asda_ui_tabbar_captions.py`
			//   вместе с геометрией и базовым цветом). Берём её, а не строим свою: оформление живёт
			//   в ассете (закон владельца 2026-08-27). Если узла нет (окно не пересобрано) —
			//   ниже сработает прежняя ветка создания.
			if (N == TEXT("INVEN_CHOICEBUT_Text2"))  { TabText1 = Tb;  continue; }
			if (N == TEXT("Inven_auto_array_Text"))  { AutoArrangeLabel = Tb; continue; }
			// Подписи нижнего ряда (импортёр даёт кнопке пару `{idc}` + `{idc}_Text`).
			if (N == TEXT("Inven_ItemUpgrade_Btn_Text"))  { TxtItemUpgrade = Tb; continue; }
			if (N == TEXT("Inven_Smelting_Btn_Text"))     { TxtSmelting = Tb;    continue; }
			if (N == TEXT("Inven_Disassembly_Btn_Text"))  { TxtDisassembly = Tb; continue; }
			if (N == TEXT("Inven_primium_item_btn_Text")) { TxtPremiumItem = Tb; continue; }
			for (int32 D = 1; D <= 3; ++D)
			{
				if (N == FString::Printf(TEXT("Inven_auto_array_box_d%d_Text"), D))
				{
					AutoArrangeItemText.SetNum(FMath::Max(AutoArrangeItemText.Num(), D));
					AutoArrangeItemText[D - 1] = Tb;
					break;
				}
			}
			// счётчики стака ячеек сетки / слотов экипировки
			bool bTaken = false;
			for (int32 C = 0; C < kGridCells && !bTaken; ++C)
			{
				if (N == FString::Printf(TEXT("INVEN_SET%d"), C))
				{
					GridUi[C].StackText = Tb;
					GridUi[C].StackTextColor = Tb->GetColorAndOpacity().GetSpecifiedColor();
					bTaken = true;
				}
			}
			if (bTaken) { Tb->SetText(FText::GetEmpty()); ApplyStackTextStyle(Tb); continue; }
			for (int32 S = 0; S < kEquipSlots && !bTaken; ++S)
			{
				if (N == FString::Printf(TEXT("EQUIP_SET%d"), S))
				{
					EquipUi[S].StackText = Tb;
					EquipUi[S].StackTextColor = Tb->GetColorAndOpacity().GetSpecifiedColor();
					bTaken = true;
				}
			}
			if (bTaken) { Tb->SetText(FText::GetEmpty()); ApplyStackTextStyle(Tb); continue; }
			continue;
		}

		// ── группы ──
		if (N == TEXT("INVEN_RIGHISIDEBACK_grp"))   { RightPanel = W;     continue; }
		if (N == TEXT("Inven_auto_array_box_grp"))  { AutoArrangeBox = W; continue; }

		// ── планы ──
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx)) { continue; }
		UAsdaAnimatedPlane* Anim = Cast<UAsdaAnimatedPlane>(W);
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }

		if (Anim)
		{
			if (Idc == TEXT("INVEN_ITEMTRAYSEL"))  { GridSel  = Anim; continue; }
			if (Idc == TEXT("EQUIP_TRAYICONSEL"))  { EquipSel = Anim; continue; }
			if (Idc == TEXT("Inven_AniGear"))      { AniGear  = Anim; continue; }
			continue;
		}
		if (Idc == TEXT("Inven_Gear"))        { Gear = Img;      continue; }
		// Спец-подсказки при наведении (эталон, событие 8): 449 и 10008.
		if (Idc == TEXT("Inven_ItemUpgrade_Btn")) { HintUpgradeBtn = Img; continue; }
		if (Idc == TEXT("INVEN_EQUIICON_BAR"))    { if (!HintDuriBar) { HintDuriBar = Img; } continue; }
		if (Idc == TEXT("INVEN_CHOICEBUT"))   { (PlaneIdx == 0 ? TabPlane0 : TabPlane1) = Img; continue; }

		bool bTaken = false;
		for (int32 C = 0; C < kGridCells && !bTaken; ++C)
		{
			if (Idc == FString::Printf(TEXT("INVEN_SET%d"), C))
			{
				GridUi[C].Icon = Img;
				if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Img))
				{
					const FMargin O = CS->GetOffsets();
					GridUi[C].IconPos  = FVector2D(O.Left, O.Top);
					GridUi[C].IconSize = FVector2D(O.Right, O.Bottom);
					GridUi[C].IconRootPos = CanvasRootPos(Img);
				}
				bTaken = true;
			}
		}
		if (bTaken) { continue; }
		for (int32 S = 0; S < kEquipSlots && !bTaken; ++S)
		{
			if (Idc == FString::Printf(TEXT("EQUIP_SET%d"), S))
			{
				EquipUi[S].Icon = Img;
				if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Img))
				{
					const FMargin O = CS->GetOffsets();
					EquipUi[S].IconPos  = FVector2D(O.Left, O.Top);
					EquipUi[S].IconSize = FVector2D(O.Right, O.Bottom);
					EquipUi[S].IconRootPos = CanvasRootPos(Img);
				}
				bTaken = true;
			}
		}
	}

	// ── кнопки: реле (UButton::OnClicked не передаёт отправителя) ──
	auto Bind = [this](UButton* Btn, int32 Index, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, Index, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};

	// ★★★ ВОЛНА 9 ЗАТОЧКИ: звук id 5 (`S_e_button06`) на КАЖДОЙ кнопке процедуры инвентаря — эталон
	//   `CGameWindow_OnCommand`, ветка 66561 (ui_core.c:47649, после разбора 83671…15902 и `boosterWnd_OnMessage`,
	//   до разбора остальных cmdId) и ветка 66817 радиогруппы 402 (ui_core.c:47628-47631; 66817 поднимает только
	//   `uiList_OnItemClickSelect@0x12F6440` — на КАЖДЫЙ клик по пункту, в том числе по уже выбранному).
	//   Одна общая точка `AsdaUi::PlayInvenButtonSfx`. У 403 само закрытие у эталона идёт событием окна 0x15
	//   (cmdId 400), а нажатие кнопки даёт 66561 со звуком — поэтому звук и закрытие вместе.
	// 403 — закрытие окна (эталон: msgType 0x15 cmdId 400 и 0x10401 → `sowelWindowClose(self, 1)`).
	Bind(BtnClose, 0, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); CloseWindow(); });
	// 408 / 412 — раскрыть/свернуть правую панель (ширина 648 ↔ 228).
	Bind(BtnExpand,   0, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); SetPanelExpanded(true,  /*bFromUser=*/true); });
	Bind(BtnCollapse, 0, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); SetPanelExpanded(false, /*bFromUser=*/true); });
	// 402 — переключатель вкладок (RadioButton → пара кнопок OPT0/OPT1); событие 66817 — тоже звук id 5.
	Bind(TabOpt0, 0, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); SetTab(0); AsdaInvSaveTab(0); });
	Bind(TabOpt1, 1, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); SetTab(1); AsdaInvSaveTab(1); });
	// ★ 405 — корзина. ИСПРАВЛЕНО: прежний порт считал, что клик по ней в оригинале ничего не даёт.
	//   На деле обработчик 405 (`item_inventory.c:41739/41803`) вызывает `sowelHandleSlotSelect(this, 1)`
	//   и для ВЫБРАННОЙ ячейки, т.е. корзина работает и кликом, а не только броском.
	Bind(BtnTrash, 0, [this](int32) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); DestroySelected(); });
	// ★ Д1: автосортировка. Кнопка «A» (80400) раскрывает всплывашку 80410 с ТРЕМЯ пунктами
	//   80411/80412/80413 («정돈타입1~/2~/3~» — подписи-заглушки самого .JUI).
	// ★★★ СКОЛЬКО НА САМОМ ДЕЛЕ ТИПОВ — снято с **ЖИВОГО ДАМПА Asda Global**
	//   (реверс 2026-08-13, отчёт `Reports/inventory_autosort_global_dump.md`).
	//   ⚠ Прежний отчёт `inventory_autosort_global.md` снимался с `NEW/WorldOfDreams/system/Asda2.exe`
	//   — это ДРУГАЯ ИГРА, он помечен недостоверным; все адреса ниже — из образа памяти самого
	//   `AsdaGlobal.exe` (`RE/dumps/global/global_image_t12.bin`, ImageBase 0x400000, VA = runtime;
	//   база IDA `RE/ida/global_work/dump/GlobalDump.exe.i64`, 65 537 функций).
	//   Найдено в Global (все адреса — «VA Global»):
	//     • всплывашку заполняет `CInvenNewWnd_InitControls@0x0076F400` циклом
	//         for (i = 0; i < 3; ++i) { c = GetControl(83691 + i); if (c) SetCaption(Loc(112400 + i)); }
	//       (`GetControl` — 0x00C7BCE0, `GetTextById` — 0x00420185; сама всплывашка — id 83690,
	//       кнопка «A» — 83671/83675, окно — 83500). Строки «Point NULL» рядом подписаны СТАРЫМИ
	//       id эталона («80400», «80410», «80411»): это доказывает, что 83xxx-окно Global — прямой
	//       потомок нашего INVEN_WIN 400, а не отдельная ветка.
	//     • диспетчер команд `CGameWindow_OnCommand@0x007813E0` (ветки @0x007814C5 и @0x007816A3):
	//         if (cmd == 83691 || cmd == 83692) { arrange(cmd − 83691); closePopup(); }
	//         if (cmd == 83693)                 {                        closePopup(); }
	//       (`arrange` — тханк 0x0041BF3B → 0x0078D840, `closePopup` — 0x00412C10 → 0x0078EA20.)
	//     • `inventoryAutoArrange@0x0078D840` объявлен `__thiscall (this, int a2)` и кончается
	//       `ret 4`, но **аргумент в теле не используется ни разу** — проверено двумя методами:
	//       (1) Hex-Rays (в декомпиле `a2` не встречается), (2) сплошной дизассемблер тела
	//       0x0078D840..0x0078E1F8 — ни одного обращения к `[ebp+8]`. Контейнер берётся из ПОЛЯ
	//       ВКЛАДКИ `this+0x260` (`this+0x175C = this+0x260`).
	//     • нотисы 112116 (готово) и 112406 (троттл) встречаются в .text РОВНО ПО ОДНОМУ РАЗУ
	//       (0x0078E278 и 0x0078D897) ⇒ реализация автосортировки в Global ОДНА, второй нет.
	//   ⇒ 80411 → AutoArrange(0), 80412 → AutoArrange(1) (порядок один и тот же),
	//      80413 → ТОЛЬКО закрыть всплывашку. Прежнее «d3 тоже сортирует» было нашей догадкой и снято.
	// ★★★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА ПО РЕШЕНИЮ ЮЗЕРА (2026-08-13): всплывашка выбора вида сортировки
	//   НЕ ОТКРЫВАЕТСЯ — кнопка «A» сразу раскладывает текущую вкладку. Основание: и у эталона, и у
	//   Global все её пункты запускают ОДИН И ТОТ ЖЕ порядок (аргумент телом не читается, таблица
	//   порядка одна, нотисы 112116/112406 встречаются в .text ровно по разу), то есть выбор вида
	//   сортировки — меню из одного варианта плюс «Закрыть». Лишний клик без смысла.
	//   Вернуть меню: `asda.Inven.LegacyArrangeMenu 1` (обработчики пунктов ниже сохранены целиком).
	Bind(BtnAutoArrange, 0, [this](int32)
	{
		if (CVarLegacyNoAutoArrange.GetValueOnGameThread() != 0) { return; }
		if (CVarLegacyArrangeMenu.GetValueOnGameThread() == 0)
		{
			AutoArrange(0);   // сразу раскладываем; аргумент — как в Global, телом не используется
			return;
		}
		const bool bShow = !(AutoArrangeBox && AutoArrangeBox->GetVisibility() != ESlateVisibility::Collapsed);
		SetVis(AutoArrangeBox, bShow);
		for (UButton* B : AutoArrangeItems) { AsdaUi::SetShownHit(B, bShow); }   // ★ R5-fix-b 13.11.2026: `nullptr` общая точка пропускает сама
		for (UTextBlock* T : AutoArrangeItemText) { SetVis(T, bShow); }
	});
	for (int32 D = 0; D < AutoArrangeItems.Num(); ++D)
	{
		Bind(AutoArrangeItems[D], D, [this](int32 Idx)
		{
			// Закрытие всплывашки — общее для всех трёх пунктов (Global: `closePopup@0x00D54010`
			//   вызывается и по 83691/83692, и по 83693).
			SetVis(AutoArrangeBox, false);
			for (UButton* B : AutoArrangeItems) { if (B) { B->SetVisibility(ESlateVisibility::Collapsed); } }
			for (UTextBlock* T : AutoArrangeItemText) { SetVis(T, false); }
			// Третий пункт (80413 ≡ 83693 в Global) — это «Закрыть», сортировку он НЕ запускает.
			if (Idx >= 2) { return; }
			AutoArrange(Idx);   // Idx = cmdId − 80411, как в Global cmdId − 83691; телом не читается
		});
	}

	// ★★★ НИЖНИЙ РЯД КНОПОК 449 / 452 / 453 / 454 / 10082. Клик каждой в эталоне ОТКРЫВАЕТ ЧУЖОЕ
	//   ОКНО (список — в шапке у `OnSideButtonClicked`). Этих окон в порту ещё нет, поэтому мы
	//   ЧЕСТНО транслируем нажатие делегатом и пишем в лог, какая именно система должна открыться.
	//   Своего UI и своих текстов здесь не появляется (законы `no-invented-ui` / `no-simplifications-stubs`).
	auto BindSide = [this, &Bind](UButton* Btn, int32 IdNum, const TCHAR* What)
	{
		if (!Btn) { return; }
		Bind(Btn, IdNum, [this, What](int32 Id)
		{
			// ★ Волна 9 заточки: звук id 5 ветки 66561 звучит у оригинала на любой кнопке, до разбора cmdId.
			AsdaUi::PlayInvenButtonSfx(GetGameInstance());
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] кнопка %d: эталон открывает «%s» — система в порту ещё не сделана"), Id, What);
			OnSideButtonClicked.Broadcast(Id);
		});
	};
	// ★ 449 ПОРТИРОВАНА 25.09.2026 (заточка, волна 3): эталон `ui_core.c:47655` —
	//   `if (cmdId == 449) uiCombineWindow_Close(self)`, а `uiCombineWindow_Close@0xD28340` — ТУМБЛЕР
	//   окна улучшения снаряжения 20000 (имя IDA врёт). Гейта открытия у него НЕТ. Своего UI здесь не
	//   появляется, вся логика — в `UAsdaItemUpgradeSubsystem` (закон `port-shared-points`).
	//   Делегат `OnSideButtonClicked` по-прежнему стреляет: на него подписаны диагностические потребители.
	if (BtnItemUpgrade)
	{
		Bind(BtnItemUpgrade, 449, [this](int32 Id)
		{
			// ★ Волна 5 заточки (25.09.2026): ветка msgType 66561 эталона играет звук id 5 (`S_e_button06`)
			//   ДО разбора cmdId (ui_core.c:47649) — у 449 он звучит перед тумблером окна 20000.
			//   ★ Волна 9: общая точка `AsdaUi::PlayInvenButtonSfx` (её зовут все кнопки процедуры инвентаря).
			AsdaUi::PlayInvenButtonSfx(GetGameInstance());
			if (UAsdaItemUpgradeSubsystem* Up = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemUpgradeSubsystem>() : nullptr)
			{
				Up->ToggleWindow();
				UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] кнопка 449 «улучшение снаряжения» → окно 20000 %s"),
					Up->IsOpen() ? TEXT("открыто") : TEXT("закрыто"));
			}
			else
			{
				UE_LOG(LogAsdaInven, Error, TEXT("[инвентарь] кнопка 449: подсистема окна улучшения снаряжения не поднята"));
			}
			OnSideButtonClicked.Broadcast(Id);
		});
	}
	// ★ 452 ПОРТИРОВАНА волной В2 ремесла (25.09.2026): эталон `boosterWnd_OnMessage@0xD16ED0` (миснейм,
	//   shop_trade.c:11970-11973) → `tileInteractGateAndOpen@0xFDD1A0(g_pItemMainWnd)` — ТУМБЛЕР окна списка
	//   рецептов 76000 со всеми гейтами. Своего UI здесь нет — вся логика в `UAsdaCraftWindowSubsystem`
	//   (закон `port-shared-points`). Звук id 5 — общая ветка 66561 процедуры инвентаря (как у 449/454).
	if (BtnSmelting)
	{
		Bind(BtnSmelting, 452, [this](int32 Id)
		{
			AsdaUi::PlayInvenButtonSfx(GetGameInstance());
			if (UAsdaCraftWindowSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr)
			{
				const bool bDone = Craft->GateAndToggle(TEXT("кнопка инвентаря 452"));
				UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] кнопка 452 «создание предмета» → окно 76000 %s%s"),
					Craft->IsOpen() ? TEXT("открыто") : TEXT("закрыто"), bDone ? TEXT("") : TEXT(" (отклонено гейтом)"));
			}
			else
			{
				UE_LOG(LogAsdaInven, Error, TEXT("[инвентарь] кнопка 452: подсистема окна ремесла не поднята"));
			}
			OnSideButtonClicked.Broadcast(Id);
		});
	}
	// ★ 453 ПОРТИРОВАНА волной 2-А (06.10.2026): эталон ui_core.c:47658-47659 — `if (cmdId == 453)
	//   upgradeUiOnOpen(g_pRecvUiHandler3)` — ТУМБЛЕР окна разборки 32000 (имя IDA врёт). Гейтов открытия у него нет.
	//   Своего UI здесь нет — вся логика в `UAsdaDisassemblySubsystem` (закон `port-shared-points`). Звук id 5 — общая
	//   ветка 66561 процедуры инвентаря (как у 449/452/454).
	if (BtnDisassembly)
	{
		Bind(BtnDisassembly, 453, [this](int32 Id)
		{
			AsdaUi::PlayInvenButtonSfx(GetGameInstance());
			if (UAsdaDisassemblySubsystem* Dis = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaDisassemblySubsystem>() : nullptr)
			{
				Dis->Toggle(TEXT("кнопка инвентаря 453"));
				UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] кнопка 453 «демонтаж снаряжения» → окно 32000 %s"),
					Dis->IsOpen() ? TEXT("открыто") : TEXT("закрыто"));
			}
			else
			{
				UE_LOG(LogAsdaInven, Error, TEXT("[инвентарь] кнопка 453: подсистема окна разборки не поднята"));
			}
			OnSideButtonClicked.Broadcast(Id);
		});
	}
	// ★ 454 ПОРТИРОВАНА волной 2 (21.09.2026): эталон `CRefineWnd_Open@0xDC3390` — тумблер окна
	//   питомца 43000 со всеми четырьмя гейтами. Своего UI здесь не появляется, вся логика —
	//   в `UAsdaPetCageSubsystem` (закон `port-shared-points`). Делегат `OnSideButtonClicked`
	//   по-прежнему стреляет: на него подписаны диагностические потребители.
	if (BtnPetCage)
	{
		Bind(BtnPetCage, 454, [this](int32 Id)
		{
			AsdaUi::PlayInvenButtonSfx(GetGameInstance());   // ★ волна 9: звук id 5 ветки 66561 (ui_core.c:47649)
			if (UAsdaPetCageSubsystem* PetWin = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr)
			{
				const bool bOpened = PetWin->OpenCage();
				UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] кнопка 454 «клетка пета» → окно 43000 %s"),
					bOpened ? TEXT("открыто") : TEXT("закрыто/отклонено гейтом"));
			}
			else
			{
				UE_LOG(LogAsdaInven, Error, TEXT("[инвентарь] кнопка 454: подсистема окна питомца не поднята"));
			}
			OnSideButtonClicked.Broadcast(Id);
		});
	}
	BindSide(BtnPremiumItem, 10082, TEXT("элитный почтовый ящик (uiBagWindow_OpenAndSync)"));

	// Подписи нижнего ряда — строго из `DT_GameText` теми же нотисами, что ставит `InitControls`
	//   (item_inventory.c:35200-35232 и :35604-35616). ★ У 453 эталон КЛАДЁТ ДВА текста подряд
	//   (100003, затем 15306) — берём ПОСЛЕДНИЙ, как и оригинал. У 454 подписи нет вовсе.
	if (TxtItemUpgrade) { TxtItemUpgrade->SetText(FText::FromString(LocText(98300,  TEXT("")))); }
	if (TxtSmelting)    { TxtSmelting->SetText(FText::FromString(LocText(15305,  TEXT("")))); }
	if (TxtDisassembly) { TxtDisassembly->SetText(FText::FromString(LocText(15306,  TEXT("")))); }
	// ★ 10082 — та самая «пустая четвёртая строка»: подписи в .JUI нет, её ставит код нотисом 110666,
	//   и он же принудительно показывает кнопку (`CControl_SetVisible(pInvenWnd[5], 1)`).
	if (TxtPremiumItem) { TxtPremiumItem->SetText(FText::FromString(LocText(110666, TEXT("")))); }
	if (BtnPremiumItem) { BtnPremiumItem->SetVisibility(ESlateVisibility::Visible); }
	SetVis(TxtPremiumItem, true);

	// ── подписи из таблицы текста (закон: UI-текст ТОЛЬКО из DT_GameText) ──
	//   ★ Пункты автосортировки: в .JUI подписи корейские-заглушки («정돈타입1~/2~/3~»), а живой
	//   клиент их ПЕРЕЗАПИСЫВАЕТ. В Global это делает `CInvenNewWnd_InitControls@0x00D2BB20`
	//   циклом `SetCaption(Loc(112400 + i))` на контролах 83691+i (= наши 80411+i).
	//   Ид 112400..112402 в нашем срезе `error list.bin` ОТСУТСТВУЮТ (эталонная таблица — 4771
	//   запись, формат v1; в Global таблица уже Ver2.0 на 7703 записи), поэтому если `DT_GameText`
	//   их не отдаст, подставляем перевод ОРИГИНАЛЬНЫХ строк Global — той же схемой, что уже
	//   используется для нотисов 112116/112406 ниже по файлу. Ничего не выдумано:
	//     112400 = "Organize Inventory 1", 112401 = "Organize Inventory 2", 112402 = "Close".
	//   [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэки убраны — строки 112400..112402 заведены в DT_GameText (ведомость
	//   gametext_overrides.json), английский — из Global (см. выше).
	{
		for (int32 D = 0; D < AutoArrangeItemText.Num() && D < 3; ++D)
		{
			if (UTextBlock* T = AutoArrangeItemText[D])
			{
				T->SetText(FText::FromString(LocText(112400 + D)));
			}
		}
	}
	//   Заголовки вкладок 13247/13248 ставит RefreshTabVisual.

	// ★ Д1: контролы автосортировки в JUI имеют Visible = 0 (обработчика не было). Включаем кнопку;
	//   всплывашку показываем только по клику.
	if (CVarLegacyNoAutoArrange.GetValueOnGameThread() == 0)
	{
		if (BtnAutoArrange) { BtnAutoArrange->SetVisibility(ESlateVisibility::Visible); }
		SetVis(AutoArrangeLabel, true);
	}
	SetVis(AutoArrangeBox, false);
	for (UButton* B : AutoArrangeItems) { if (B) { B->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : AutoArrangeItemText) { SetVis(T, false); }

	// ★★★ п.10: база систем координат курсоров выбора. Позиция родительского канваса курсора в
	//   координатах корня = RootPos(курсор) − его собственный Offsets. Дальше ставим курсору
	//   `Offsets = IconRootPos(ячейка) − эта база`, и рамка садится РОВНО в ячейку в любом канвасе.
	auto SelParentRoot = [](const UWidget* Sel) -> FVector2D
	{
		if (!Sel) { return FVector2D::ZeroVector; }
		FVector2D Own = FVector2D::ZeroVector;
		if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Sel))
		{
			const FMargin O = CS->GetOffsets();
			Own = FVector2D(O.Left, O.Top);
		}
		return CanvasRootPos(Sel) - Own;
	};
	GridSelParentRoot  = SelParentRoot(GridSel);
	EquipSelParentRoot = SelParentRoot(EquipSel);

	// ★★★ Д6: ДВЕ подписи вкладок. Эталон `InventoryWindow_InitControls` (item_inventory.c:35287-35290)
	//   ставит их ПОШТУЧНО: `CListBox_SetItemString(402, 0, текст 13247)` и `(402, 1, текст 13248)`,
	//   т.е. каждая надпись центрируется в СВОЕЙ половине переключателя (две кнопки по 108 px).
	//   Импортёр слил их в ОДИН TextBlock шириной 218 px → надпись вставала между вкладками (п.9 теста).
	//   Чиним геометрией: существующий контрол сажаем на прямоугольник опции 0, для опции 1 создаём
	//   второй TextBlock той же стилистики (регенерацию WBP не запускаем).
	if (TabText && TabOpt0 && TabOpt1 && InvenWidget && InvenWidget->WidgetTree)
	{
		auto RectOf = [](const UWidget* W, FMargin& Out) -> bool
		{
			const UCanvasPanelSlot* CS = W ? AsdaUi::SlotOf(W) : nullptr;
			if (!CS) { return false; }
			Out = CS->GetOffsets();
			return true;
		};
		FMargin R0, R1;
		if (RectOf(TabOpt0, R0) && RectOf(TabOpt1, R1))
		{
			if (!TabText1)
			{
				TabText1 = NewObject<UTextBlock>(InvenWidget, UTextBlock::StaticClass(),
					MakeUniqueObjectName(InvenWidget, UTextBlock::StaticClass(), FName(TEXT("INVEN_CHOICEBUT_Text1"))));
				if (UCanvasPanel* Root = Cast<UCanvasPanel>(InvenWidget->WidgetTree->RootWidget))
				{
					if (UCanvasPanelSlot* TS = Root->AddChildToCanvas(TabText1))
					{
						TS->SetAnchors(FAnchors(0.f, 0.f));
						TS->SetAlignment(FVector2D(0.f, 0.f));
						TS->SetAutoSize(false);
						TS->SetZOrder(1);
					}
				}
			}
			if (TabText1)
			{
				TabText1->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				TabText1->SetAutoWrapText(false);
				TabText1->SetFont(TabText->GetFont());
				TabText1->SetJustification(ETextJustify::Center);
			}
			TabText->SetJustification(ETextJustify::Center);
			// Текст ставим ПОВЕРХ прямоугольника своей кнопки-опции (высота/отступ — как у импортёра).
			auto PlaceOn = [](UTextBlock* Tb, const FMargin& Btn, const FMargin& Src)
			{
				UCanvasPanelSlot* CS = Tb ? AsdaUi::SlotOf(Tb) : nullptr;
				if (!CS) { return; }
				FMargin O = CS->GetOffsets();
				O.Left  = Btn.Left;
				O.Top   = Src.Top;          // вертикаль оставляем ту, что положил импортёр
				O.Right = Btn.Right;        // ширина = ширина ОДНОЙ вкладки
				O.Bottom= Src.Bottom;
				CS->SetOffsets(O);
			};
			FMargin SrcRect;
			if (RectOf(TabText, SrcRect))
			{
				PlaceOn(TabText,  R0, SrcRect);
				PlaceOn(TabText1, R1, SrcRect);
			}
			// ★ Д5 (решение ЮЗЕРА): подписи вкладок БЕЗ ОБВОДКИ. Откат — `asda.Inven.LegacyTabTextStyle 1`.
			//   ★★★ ЦВЕТ ОТСЮДА УБРАН (правка 2026-08-27). ЧТО БЫЛО: обе подписи красились в ЧЁРНЫЙ,
			//   поэтому активную вкладку было не отличить от соседней. КАК СТАЛО: решение владельца
			//   «активная чёрная, неактивная тёмно-серая» — тёмно-серый лежит В АССЕТЕ, чёрный ставит
			//   активной ОБЩАЯ ТОЧКА `AsdaUi::ApplyTabCaptionState` (зовётся из `RefreshTabVisual`).
			if (CVarLegacyTabTextStyle.GetValueOnGameThread() == 0)
			{
				for (UTextBlock* Tb : { TabText.Get(), TabText1.Get() })
				{
					if (!Tb) { continue; }
					FSlateFontInfo F = Tb->GetFont();
					F.OutlineSettings.OutlineSize = 0;
					F.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
					Tb->SetFont(F);
				}
			}
		}
	}

	// 413 скрывается сразу в `InventoryWindow_InitControls` — окно открывается СВЁРНУТЫМ (228 px).
	SetVis(RightPanel, bPanelExpanded);
	SetVis(GridSel, false);
	SetVis(EquipSel, false);
	SetVis(AniGear, false);
	SetVis(Gear, true);

	int32 GridOk = 0, EquipOk = 0;
	for (const FAsdaInvCellUi& C : GridUi)  { if (C.Icon) { ++GridOk; } }
	for (const FAsdaInvCellUi& C : EquipUi) { if (C.Icon) { ++EquipOk; } }
	bControlsReady = (GridOk == kGridCells);

	// ★ ПОЛОСА ПРОКРУТКИ. Виджетов в WBP нет и быть не может (её создаёт движок оригинала по
	//   ключу скина `ScrollBarFileNameVert`, а импортёр читает только .JUI) — строим сами из
	//   данных скина `Nomal_Scr`. После `bControlsReady`, иначе `IsScrollBarShown` погасит её.
	BuildScrollBar();
	LayoutScrollBar();
	UE_LOG(LogAsdaInven, Log,
		TEXT("[инвентарь] контролы: сетка %d/%d, экип %d/%d, вкладки=%s, деньги=%s, корзина=%s, автосорт=%s"),
		GridOk, kGridCells, EquipOk, kEquipSlots,
		(TabOpt0 && TabOpt1) ? TEXT("да") : TEXT("НЕТ"),
		MoneyText ? TEXT("да") : TEXT("НЕТ"),
		BtnTrash ? TEXT("да") : TEXT("НЕТ"),
		BtnAutoArrange ? TEXT("да") : TEXT("НЕТ"));
}

// ══════════════════════════════ ЗОНА ИНТЕРФЕЙСА И КОЛЕСО ═══════════════════════════════════════
bool UAsdaInventorySubsystem::GetWindowRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bWindowOpen || !InvenWidget) { return false; }
	// Прямоугольник считаем по узлам, которые ОБРАЗУЮТ корпус окна (закон: «объединение всех узлов»
	//   раздувает зону на весь экран — уже ловили на SCROLL_DOWN чата).
	static const TCHAR* kBodyNodes[] = {
		TEXT("INVEN_WIN_0_4"),        // центральная плита рамки окна
		TEXT("INVEN_ITEMTRAY1_grp"),  // подложка сетки
		TEXT("EQUIP_TRAYICON_grp"),   // манекен экипировки
	};
	bool bAny = false;
	FVector2D Mn(FLT_MAX, FLT_MAX), Mx(-FLT_MAX, -FLT_MAX);
	auto Acc = [&](const UWidget* W)
	{
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed) { return; }
		// ★ Аудит №4 (A9/B1): живой абсолютный прямоугольник — общая точка `AsdaUi::WidgetAbsRect`
		//   (она же отсеивает нулевую геометрию). Накопитель min/max остаётся здесь.
		FVector2D P, PMax;
		if (!AsdaUi::WidgetAbsRect(W, P, PMax)) { return; }
		Mn = FVector2D(FMath::Min(Mn.X, P.X), FMath::Min(Mn.Y, P.Y));
		Mx = FVector2D(FMath::Max(Mx.X, PMax.X), FMath::Max(Mx.Y, PMax.Y));
		bAny = true;
	};
	for (const TCHAR* Nm : kBodyNodes) { Acc(FindNode(Nm, Nm)); }
	if (bPanelExpanded) { Acc(RightPanel); }
	if (!bAny) { return false; }
	OutMin = Mn; OutMax = Mx;
	return true;
}

void UAsdaInventorySubsystem::RegisterZone()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	WM->RegisterUiZone(kInvenWindowId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Mn, FVector2D& Mx) { return GetWindowRect(Mn, Mx); }));
	// Прокрутка сетки колесом — эталонное событие 23 (0x17) с cmdId 494 (`INVEN_WHEELWIN`).
	//   ★ 494 — это НЕ полоса прокрутки, а невидимое «колёсное окно» (`WheelWin_Nomal`, Visible = 0),
	//   накрывающее ровно сетку (126,320,198,164). Поэтому у нас это обработчик колеса, а не виджет.
	WM->RegisterWheelHandler(kInvenWindowId, FAsdaUiWheelHandler::CreateUObject(this, &UAsdaInventorySubsystem::OnWheel));
}

void UAsdaInventorySubsystem::UnregisterZone()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kInvenWindowId);
		WM->UnregisterWheelHandler(kInvenWindowId);
	}
}

bool UAsdaInventorySubsystem::OnWheel(int32 Notches)
{
	if (!bWindowOpen) { return false; }
	// ★ 21.10.2026: колёсное окно 495 СПИСКА КАМНЕЙ — тоже контрол ЭТОГО окна, поэтому колесо над ним разбирается
	//   на z инвентаря, а не последним (прежде у списка был свой обработчик под id «INVEN_SOWELLWHEELWIN», которого
	//   нет в реестре окон ⇒ z = −1 ⇒ его опрашивали после всех окон, и любое окно с обработчиком выше перехватывало
	//   колесо — «список камней не листается»).
	if (UAsdaSowelSubsystem* Sw = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSowelSubsystem>() : nullptr)
	{
		if (Sw->HandleListWheel(Notches)) { return true; }
	}
	// Колесо действует только над сеткой (эталонное «колёсное окно» 494 накрывает ровно её).
	const UWidget* Tray = FindNode(TEXT("INVEN_ITEMTRAY1_grp"), TEXT("INVEN_ITEMTRAY1_grp"));
	if (!IsCursorOver(Tray)) { return false; }
	ScrollBy(-Notches);   // «от себя» (+1) = вверх по списку = меньший ряд
	return true;
}

// ══════════════════════════════ ОКНО: ОТКРЫТЬ / ЗАКРЫТЬ / РАСКРЫТЬ ═════════════════════════════
void UAsdaInventorySubsystem::ApplyWindowVisibility()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !InvenWidget) { return; }
	WM->SetWindowVisible(kInvenWindowId, bWindowOpen, ESlateVisibility::SelfHitTestInvisible);
	if (bWindowOpen)
	{
		// Эталон при показе делает `CClass_MapInsert(g_pWindowMgr, m_pInvenWin)` — регистрация
		//   «верхним окном». В порту это `BringWindowToFront`.
		WM->BringWindowToFront(kInvenWindowId);
	}
}

void UAsdaInventorySubsystem::OpenWindow()
{
	if (bWindowOpen) { return; }
	UGameInstance* GI = GetGameInstance();
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `uiOpenInventoryWindow` (ui_core.c:46605) — окно ещё не открыто → байт 79, до гейтов.
	AsdaTutorial::Mark(GI, EAsdaTutorialFlag::InventoryOpen);
	// ★ 31.10.2026: `uiOpenInventoryWindow@0xCFF300` (ui_core.c:46606) первым делом снимает взвод камня переноса
	//   улучшения (`sub_938F54` = `wnd_ResetCursorAndDrag@0xB65720`) — до гейтов открытия.
	if (UAsdaUpgradeStoneSubsystem* Stone = GI ? GI->GetSubsystem<UAsdaUpgradeStoneSubsystem>() : nullptr)
	{
		Stone->CancelPick(TEXT("открытие сумки"));
	}
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// Гейт эталона `uiOpenInventoryWindow` пп.4-12: инвентарь не открывается поверх модального
	//   диалога (магазин / диалог количества / склад / тултип — их окон в порту ещё нет, а вот
	//   модалки уже есть, и это тот же по смыслу запрет).
	if (WM && WM->IsAnyModalOpen())
	{
		// Кнопка меню уже «нажалась» — подсветку снимет сама панель, увидев, что окно не открылось
		//   (`SyncCheckedFromWindows`, лечение эталонного дефекта Д7). Здесь второго пути не заводим.
		Notice(110644);
		return;
	}
	// ★ Волна В3 ремесла (ревизия В0–В2 С-1): `uiOpenInventoryWindow@0xCFF300` (ui_core.c:46608) — весь блок открытия
	//   (и сброс страниц `uiStorageWndReset`, :46959/:46962) под `!helper_getField2075(g_pShopWnd) &&
	//   !tileInteract_isControlVisible(g_pItemMainWnd) && !isBoundControlVisible(g_pItemQtyMakeDlg)` — МОЛЧА.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] открытие отклонено молча: открыто окно ремесла 76000/77500 (ui_core.c:46608)"));
		return;
	}
	// ★ 29.09.2026 (волна 0 торговли): [ОРИГИНАЛ] `uiOpenInventoryWindow@0xCFF300` (ui_core.c:46618) — внутри того же блока
	//   `(!g_pRecvUiHandler || …) && g_bPartyInviteFlag != 1`: при ОТКРЫТОМ окне обмена 2008 (`CExchange+0x2C`) рюкзак не
	//   открывается, МОЛЧА. Общая точка `UAsdaTradeStateSubsystem` (пока окна 2008 нет — false).
	// ★ Волна Л5 торговли (30.09.2026): [ОРИГИНАЛ] гейты лавки того же блока, ДО гейта окна обмена (ui_core.c:46610-46616):
	//   подготовка 22000 (`sub_934E86(g_pSellWindow)`) → 107022 в чат (−11514, 10 с); витрина 23000 (`sub_929DC4`) → `goto LABEL_10` =
	//   13106 «Ничего не выйдет, пока идет работа с бустером.» в чат — ★ разбор B §6 («витрина → молча») НЕВЕРЕН: второй метод (дизасм
	//   0xCFF3DA: `call 0x929dc4 / test / je` → `push 0x3332` = 13106 → `0x91e3fc` → `0x91f63a`) подтверждает строку. Текст про
	//   бустер — [ИСПРАВЛЕНИЕ] К-17 ключом `asda.Trade.StoreFixK17` (1 — 107022, та же строка, что эталон ставит всем читателям
	//   «лавка открыта»; 0 — 13106, как оригинал).
	if (UAsdaTradeStateSubsystem::IsSellWindowOpen(GI))
	{
		UAsdaTradeStateSubsystem::ChatStoreGate(GI, 107022, TEXT("uiOpenInventoryWindow (подготовка 22000)"));
		return;
	}
	if (UAsdaTradeStateSubsystem::IsShopDlgOpen(GI))
	{
		UAsdaTradeStateSubsystem::ChatStoreGate(GI, UAsdaTradeStateSubsystem::InventoryShowcaseRefusalLoc(), TEXT("uiOpenInventoryWindow (витрина 23000)"));
		return;
	}
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI))
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] открытие отклонено молча: открыто окно обмена 2008 (ui_core.c:46618)"));
		return;
	}
	bWindowOpen = true;
	if (!EnsureWidget()) { bWindowOpen = false; return; }

	// Эталонное начальное состояние (`ui_core.c:46970-46990`): страницы обнулены, вкладка 0,
	//   `SetSelectedIndex(402, 0)`, `SetScrollRange(494, 4, 9)`, `SetChildScrollbarPos(494, 0)`,
	//   `CInventoryUI_RefreshStoragePage(this, 0)`.
	ScrollRow[0] = 0; ScrollRow[1] = 0;
	SelCell = -1; SelEquipSlot = -1;
	CurrentTab = 0;
	// Открываемся на «Расходниках» ⇒ панель подробностей свёрнута (эталон: ширина 228, колёсное
	//   окно 494 показано). Память ручной свёртки (Д8) на переоткрытии сбрасывается.
	bPanelCollapsedByUser = false;
	SetPanelExpanded(false, /*bFromUser=*/false);
	if (Tooltip.ClearInfoPanel) { Tooltip.ClearInfoPanel(); }
	SowelNotifySelect(GetGameInstance(), 0, -1);   // ★ ветка самоцветов гаснет вместе с инфо-панелью
	ApplyWindowVisibility();
	RefreshTabVisual();
	RefreshGrid();
	RefreshEquip();
	RefreshMoney();
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] `asda.Inven.RememberTab`: вкладка последнего ручного выбора (разбор у ключа). Через `SetTab` — та же панель и прокрутка.
	if (AsdaInvLoadTab() == 1) { SetTab(1); }
	// Подсветку кнопки 73402 ставит панель меню сама, увидев `INVEN_WIN` видимым (единый механизм).
	// ★ Эталон `uiOpenInventoryWindow@0xCFF300` первым делом зовёт `sub_928A8C` (спрятать 15900) и
	//   `sub_9241EE` (снять взвод рыболовной снасти) — открытие инвентаря отменяет незавершённый выбор.
	if (UAsdaFishingToolSubsystem* Fish = GI ? GI->GetSubsystem<UAsdaFishingToolSubsystem>() : nullptr)
	{
		Fish->OnInventoryOpened();
	}
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] окно открыто (вкладка 0 «Расходники», контейнер 2)"));
}

void UAsdaInventorySubsystem::CloseWindow()
{
	if (!bWindowOpen) { return; }
	bWindowOpen = false;
	// Эталон `sowelWindowClose@0xCFF760`: спрятать окно, сбросить выборы, погасить эффекты.
	CancelDragVisual();
	HideCellTooltip();
	SelCell = -1; SelEquipSlot = -1;
	SetVis(GridSel, false);
	SetVis(EquipSel, false);
	SetVis(AutoArrangeBox, false);
	for (UButton* B : AutoArrangeItems) { if (B) { B->SetVisibility(ESlateVisibility::Collapsed); } }
	for (UTextBlock* T : AutoArrangeItemText) { SetVis(T, false); }
	if (Tooltip.ClearInfoPanel) { Tooltip.ClearInfoPanel(); }
	SowelNotifySelect(GetGameInstance(), 0, -1);   // ★ ветка самоцветов гаснет вместе с инфо-панелью
	ApplyWindowVisibility();
	// Подсветка кнопки 73402 гаснет сама: панель меню видит `INVEN_WIN` скрытым (единый механизм).
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] окно закрыто"));
}

void UAsdaInventorySubsystem::ToggleWindow()
{
	// ⚠ Открытие/закрытие может прилететь из колбэка меню — переносим в Tick (см. PendingWindowAction).
	PendingWindowAction = bWindowOpen ? 2 : 1;
}

// ★★★ Прогрев окна (load_hitch 11.09.2026) — см. объявление. 1 = собирать окно скрытым при снятии
//   загрузочного экрана; 0 = как было (окно собирается при первом открытии).
static ASDA_DEV_CVAR(int32, CVarAsdaInvenPrewarm,
	TEXT("asda.Inven.Prewarm"),
	1,
	TEXT("1 = собрать окно инвентаря скрытым в кадре снятия загрузочного экрана (первое открытие без подвисания); 0 = как было."),
	ECVF_Default);

FSoftClassPath UAsdaInventorySubsystem::GetWindowClassPath()
{
	return FSoftClassPath(kInvenWbp);
}

void UAsdaInventorySubsystem::PrewarmWindow()
{
	// Открытое окно не трогаем: его и так ведёт Tick → EnsureWidget.
	if (bWindowOpen || CVarAsdaInvenPrewarm.GetValueOnGameThread() == 0) { return; }
	const double T0 = FPlatformTime::Seconds();
	// EnsureWidget при bWindowOpen == false сам вызывает ApplyWindowVisibility → окно Collapsed в том же тике,
	//   до первой отрисовки. Уже живое окно EnsureWidget не пересобирает.
	const bool bOk = EnsureWidget();
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] прогрев окна: %s за %.1f мс (asda.Inven.Prewarm)"),
		bOk ? TEXT("собрано скрытым") : TEXT("НЕ собрано"), (FPlatformTime::Seconds() - T0) * 1000.0);
}

// ★★★ РАСКРЫТИЕ/СВЁРТКА ПРАВОЙ ПАНЕЛИ — полная модель эталона (круг 2).
//   Раскрытие  — `uiPanelExpand@0xD19900`         : показать 413 и 412, спрятать 408, ширина 648.
//   Свёртка    — `uiPanelRelayoutForMode@0xD199B0`: спрятать 413 и 412, показать 408, ширина 228.
//   Кто их зовёт (все места среза):
//     • кнопка 408 — `ui_core.c:47526`, гейт `GetSelectedIndex(ctrl 402) == 1` («Снаряжение»);
//     • кнопка 412 — `ui_core.c:47536`, БЕЗ гейта (это и есть «скрыть той же стрелкой»);
//     • смена вкладки — `inventoryItemDetailSetVisible@0xD173A0` (item_inventory.c:41540):
//         вкладка 1 → `uiPanelExpand`, вкладка 0 → свёртка (ширина 228, обратно колёсное окно);
//     • КЛИК ПО ПРЕДМЕТУ на вкладке «Снаряжение» — item_inventory.c:36325/36337, авто-раскрытие;
//     • клик по НАДЕТОМУ слоту — `ui_core.c:33973`, тоже `uiPanelExpand`.
//   `bFromUser` = нажата настоящая стрелка (тогда гейт вкладки и память ручной свёртки работают);
//   внутренние вызовы (смена вкладки, авто-раскрытие) идут с `bFromUser = false`.
void UAsdaInventorySubsystem::SetPanelExpanded(bool bExpanded, bool bFromUser)
{
	if (bFromUser && bExpanded && CurrentTab != 1 && CVarExpandOnBothTabs.GetValueOnGameThread() == 0)
	{
		if (CVarArrowOpensEquipTab.GetValueOnGameThread() == 0)
		{
			// ЭТАЛОН: на «Расходниках» стрелка 408 молча не делает ничего (клик даже не наполняет панель).
			UE_LOG(LogAsdaInven, Log,
				TEXT("[инвентарь] стрелка 408 работает только на вкладке «Снаряжение» (эталон; ключ asda.Inven.ArrowOpensEquipTab 1 — переход на «Снаряжение»)"));
			return;
		}
		// [ИСПРАВЛЕНИЕ] `asda.Inven.ArrowOpensEquipTab`: вкладка «Снаряжение» (та же смена, что щелчок по 402, без записи памяти вкладки),
		//   затем обычное раскрытие по кнопке ниже.
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] стрелка 408 на «Расходниках»: переход на «Снаряжение» (asda.Inven.ArrowOpensEquipTab)"));
		SetTab(1);
	}
	if (bFromUser)
	{
		// Д8: помним ручную свёртку — до смены вкладки / переоткрытия окна авто-раскрытия не будет.
		bPanelCollapsedByUser = !bExpanded;
	}
	// ★ 08.11.2026: раскрытие стрелкой — отложенная свёртка подсказки (Д1 «нет выбора — панель свёрнута», её ставит `ClearInfoPanel`,
	//   например из `SetTab` выше) ручное раскрытие не отменяет: правило Д1 ручную стрелку не касается.
	if (bFromUser && bExpanded)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>()) { Tip->CancelPendingPanelCollapse(); }
		}
	}
	bPanelExpanded = bExpanded;
	SetVis(RightPanel, bExpanded);
	// Кнопки-стрелки взаимоисключающи: 408 видна свёрнутым, 412 — раскрытым (она лежит ВНУТРИ 413).
	AsdaUi::SetShownHit(BtnExpand, !bExpanded);
	AsdaUi::SetShownHit(BtnCollapse, bExpanded);
	// ЭТАЛОН `inventoryItemDetailSetVisible@0xD173A0`: раскрытие панели ПРЯЧЕТ колёсное окно 494
	//   (а с ним и полосу), сворачивание — показывает обратно вместе с `SetScrollRange(4, 9)`.
	LayoutScrollBar();
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] панель %s (ширина окна %d)%s"),
		bExpanded ? TEXT("раскрыта") : TEXT("свёрнута"),
		bExpanded ? kWinWidthExpanded : kWinWidthCollapsed,
		bFromUser ? TEXT(", по кнопке") : TEXT(""));
}

// ★ Авто-раскрытие по выбору предмета (эталон item_inventory.c:36325/36337 и ui_core.c:33973).
//   • ячейка сетки — только на вкладке «Снаряжение»: весь блок эталона стоит под флагом
//     `gap_1B0[84]` («выбрана вкладка Снаряжение»), на «Расходниках» клик уходит в ветку 36355+
//     и панель не трогает вовсе;
//   • НАДЕТЫЙ слот (контейнер 3) — эталон раскрывает панель безусловно (`ui_core.c:33973`,
//     там же `ItemTooltip_PopulateInfoPanel(g_pInventoryWnd, 3, слот)`).
//     ⚠ Эталон при этом ещё и переводит внутренний флаг вкладки в 1, НЕ трогая сам радио-контрол
//     402 — воспроизводить это рассогласование не станем (закон `fix-original-defects`).
void UAsdaInventorySubsystem::AutoExpandOnSelect(int32 ContainerId, int32 Cell)
{
	if (!bWindowOpen || bPanelExpanded) { return; }
	if (ContainerId != 3 && CurrentTab != 1) { return; }
	// Эталон раскрывает панель только когда в ячейке ЕСТЬ предмет
	//   (`if (bagItemPtr && *(int *)bagItemPtr > 0)`, item_inventory.c:36271); по пустой ячейке он
	//   наоборот сбрасывает выбор и поля (36345-36353).
	const FAsdaItemInstance* It = GetCell(ContainerId, Cell);
	if (!It || It->IsEmpty()) { return; }
	if (bPanelCollapsedByUser && CVarRememberPanelCollapse.GetValueOnGameThread() != 0)
	{
		return;   // Д8: юзер свернул панель вручную — уважаем до смены вкладки
	}
	SetPanelExpanded(true, /*bFromUser=*/false);
}

// ══════════════════════════════ ВКЛАДКИ И ПРОКРУТКА ════════════════════════════════════════════
int32 UAsdaInventorySubsystem::GetMaxScrollRow() const
{
	const int32 Size = GetContainerSize(GetGridContainer());
	if (CVarLegacyScrollRange.GetValueOnGameThread() != 0)
	{
		// Дефект оригинала (Д2): диапазоны зашиты числами `SetScrollRange(4, 9)` / `(4, 5)`.
		// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): числа эталона — под сетку 6×5; при 12 колонках контейнер помещается целиком,
		//   и зашитые 5/1 листали бы ряды ЗА концом контейнера (пустая сетка). Эталонные числа — только пока сетка 6-колоночная.
		if (FMath::DivideAndRoundUp(Size, kGridCols) <= kGridRows) { return 0; }
		return (CurrentTab == 1) ? 1 : 5;
	}
	const int32 Rows = FMath::DivideAndRoundUp(Size, kGridCols);
	return FMath::Max(0, Rows - kGridRows);
}

// ★ Аргументы эталонного `uiScrollbarSetRange(a2 = видимых, a3 = всего)`. От пары зависит ТОЛЬКО
//   длина бегунка (`Дорожка · a2 / a3`, минимум 6 px); предел прокрутки — у `GetMaxScrollRow()`.
void UAsdaInventorySubsystem::GetScrollRangeRows(int32& OutVisibleRows, int32& OutTotalRows) const
{
	if (CVarLegacyScrollRange.GetValueOnGameThread() != 0)
	{
		// Числа эталона как есть: `SetScrollRange(494, 4, 9)` на «Расходниках» (InitControls,
		//   `uiStorageWndReset`, `CInventoryUI_ResetAndRefresh`, `inventoryItemDetailSetVisible(0)`)
		//   и `(494, 4, 5)` на «Снаряжении» (`uiInventoryDetail_Reset@0xD17300`).
		OutVisibleRows = 4;
		OutTotalRows   = (CurrentTab == 1) ? 5 : 9;
		return;
	}
	// Д2: считаем по данным. Видимых рядов РОВНО пять — контролов `INVEN_SET0..29` тридцать штук
	//   при шести колонках; «4» эталона занижает длину бегунка (63 px вместо 71) и на «Снаряжении»
	//   даёт лишнюю позицию прокрутки поверх контейнера ровно из 5 рядов.
	OutVisibleRows = kGridRows;
	OutTotalRows   = FMath::Max(kGridRows, FMath::DivideAndRoundUp(GetContainerSize(GetGridContainer()), kGridCols));
}

// ══════════════════════════════ ПОЛОСА ПРОКРУТКИ (494 → CScrollBar «Nomal_Scr») ════════════════
//
// ★★★ ПОЧЕМУ ВИДЖЕТОВ ПОЛОСЫ НЕТ В WBP И ИСКАТЬ ИХ БЕСПОЛЕЗНО.
//   Импортёр разбирает `gamemain_jui.ini` — а полосы там нет ни одним контролом. Оригинал создаёт
//   её В РАНТАЙМЕ: `uiControl_AttachVertScrollbar@0x12F3160` читает у СКИНА колёсного окна
//   (`WheelWin_Nomal`) ключ `ScrollBarFileNameVert = "Nomal_Scr"` и подвешивает `CScrollBar`
//   ребёнком контрола 494. Поэтому четыре плана собираем здесь — из тех же данных скина.
//   Прошлый заход отчёта был прав только наполовину: 494 действительно НЕ полоса, но именно он
//   её ВЛАДЕЛЕЦ, и оба факта надо держать вместе.
//
// Раскладка планов при высоте H (`uiScrollbarLayoutPlanes@0x12FE500`, ScrollBarType = 1):
//   ↑ (0, 0, 11, 11) · дорожка (0, 11, 11, H−11) · ↓ (0, H−11, 11, H) · бегунок поверх дорожки.
void UAsdaInventorySubsystem::BuildScrollBar()
{
	if (ScrollBar.IsBuilt() || !InvenWidget || !InvenWidget->WidgetTree) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(InvenWidget->WidgetTree->RootWidget);
	if (!Root) { return; }
	// * D3: сборку планов из данных скина делает общий компонент (UI/AsdaScrollBarNomal.h).
	ScrollBar.Build(InvenWidget, Root, TEXT("INVEN_WHEELWIN_Scr"), /*ZTrack*/20);
}

// Полоса — РЕБЁНОК 494, значит видна ровно тогда, когда видно само колёсное окно:
//   `uiToggleSubWindowVisible@0xD1E770` и `inventoryItemDetailSetVisible(1)` прячут 494 на вкладке
//   «Снаряжение» и при раскрытой правой панели, `inventoryItemDetailSetVisible(0)` /
//   `CInventoryUI_ResetAndRefresh` показывают обратно.
bool UAsdaInventorySubsystem::IsScrollBarShown() const
{
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): + «есть что прокручивать». При 12×5 обе вкладки помещаются целиком
	//   (MaxRow 0), и полоса без бегунка (стрелки + пустая дорожка) осталась бы справа от сетки мусором.
	return bWindowOpen && bControlsReady && ScrollBar.IsBuilt() && CurrentTab == 0 && !bPanelExpanded
		&& GetMaxScrollRow() > 0;
}

bool UAsdaInventorySubsystem::IsCursorOverScrollBar() const
{
	if (!IsScrollBarShown()) { return false; }
	return ScrollBar.IsCursorOver();
}

void UAsdaInventorySubsystem::LayoutScrollBar()
{
	if (!ScrollBar.IsBuilt()) { return; }
	if (!IsScrollBarShown()) { ScrollBar.Hide(); return; }
	// База = левый-верх ячейки 0 в координатах КОРНЕВОГО канваса. Это ровно левый-верх колёсного
	//   окна 494: JUI `INVEN_ITEMTRAY2` (121,343) + Origin (5,-23) = (126,320) = позиция `INVEN_SET0`.
	//   Берём по ЖИВОМУ виджету, потому что окно таскается мышью (`UAsdaWindowSubsystem` двигает слоты).
	const UWidget* Cell0 = GridUi.IsValidIndex(0) ? GridUi[0].Icon.Get() : nullptr;
	if (!Cell0) { return; }
	const FVector2D Base = CanvasRootPos(Cell0);
	// Коэффициент на случай, если импортёр когда-нибудь начнёт масштабировать окно: считаем по
	//   живому шагу сетки, а не по авторским 33 px. Сейчас в WBP шаг ровно 33 => множитель 1.
	float Step = AsdaInvScroll::kJuiStep;
	if (GridUi.IsValidIndex(1) && GridUi[1].Icon)
	{
		const float D = (float)(CanvasRootPos(GridUi[1].Icon) - Base).X;
		if (D > 1.f) { Step = D; }
	}
	const float S = Step / AsdaInvScroll::kJuiStep;

	// Диапазон рядов сетки — это уже модель ЭТОГО окна; доля видимого = видимые ряды / всего рядов.
	int32 VisRows = kGridRows, TotalRows = kGridRows;
	GetScrollRangeRows(VisRows, TotalRows);
	const int32 MaxRow = GetMaxScrollRow();
	const int32 Pos    = FMath::Clamp(ScrollRow[FMath::Clamp(CurrentTab, 0, 1)], 0, FMath::Max(0, MaxRow));
	// ★ Волна 14 (МЕЛК-1/2): доля — целыми аргументами эталона `SetScrollRange(VisRows, TotalRows)`,
	//   длина бегунка считается в пикселях `.JUI` и только потом умножается на S.
	ScrollBar.Layout(Base + FVector2D(AsdaInvScroll::kOffsetX * S, 0.f),
	                 AsdaInvScroll::kHeight * S, S, VisRows, TotalRows, MaxRow, Pos);
}

// Ввод полосы. Все ветки — `uiScrollbarOnButtonEvent@0x12FF2C0` и `uiScrollbarOnMouseDrag@0x12FF020`
//   (живут в общем компоненте). Шаг стрелки и шаг «страницы» ОБА равны 1 ряду:
//   `uiControlSetScrollPage(494, 1, 1)` (item_inventory.c:35387).
void UAsdaInventorySubsystem::TickScrollBar(bool bLmb, bool bLmbDownEdge)
{
	if (!IsScrollBarShown())
	{
		ScrollBar.bThumbGrab = false;
		ScrollBar.PressedPlane = -1;
		return;
	}
	int32 Value = 0;
	switch (ScrollBar.Tick(bLmb, bLmbDownEdge, GetMaxScrollRow(), Value))
	{
	case EAsdaScrollAction::ScrollBy: ScrollBy(Value);    break;   // стрелка / «страница» = 1 ряд
	case EAsdaScrollAction::SetRow:   SetScrollRow(Value); break;  // перетаскивание бегунка
	default: break;
	}
}

FString UAsdaInventorySubsystem::DumpScrollBar() const
{
	int32 VisRows = 0, TotalRows = 0;
	GetScrollRangeRows(VisRows, TotalRows);
	return FString::Printf(
		TEXT("=== Полоса прокрутки (CScrollBar «Nomal_Scr», ребёнок колёсного окна 494) ===\n")
		TEXT("%s")
		TEXT("видна: %s (вкладка %d, панель %s)\n")
		TEXT("SetScrollRange(%d, %d) => max=%d, ряд=%d\n"),
		*ScrollBar.Dump(),
		IsScrollBarShown() ? TEXT("да") : TEXT("нет"), CurrentTab,
		bPanelExpanded ? TEXT("раскрыта") : TEXT("свёрнута"),
		VisRows, TotalRows, GetMaxScrollRow(), ScrollRow[FMath::Clamp(CurrentTab, 0, 1)]);
}
