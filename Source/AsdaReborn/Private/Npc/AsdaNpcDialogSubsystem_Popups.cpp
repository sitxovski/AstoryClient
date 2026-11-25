// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNpcDialogSubsystem_Popups.cpp — ★ 23.10.2026 часть UAsdaNpcDialogSubsystem, вынесенная из AsdaNpcDialogSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaNpcDialogSubsystem.plan.json).
//   Облачко выбора квеста MULTYQUEST_WIN, попап наград 540 (обводка, раскладка, подсказки), окно приёма задания, окно сведений о задании.
//   #include — те же, что у AsdaNpcDialogSubsystem.cpp, в том же порядке; общее — AsdaNpcDialogSubsystemInternal.h.
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"          // строка списка 2408/2410: ровно 20 px по эталону
#include "Components/ButtonSlot.h"       // выравнивание подписи внутри строки-кнопки
#include "Components/ContentWidget.h"    // обход строки списка (SizeBox → Button → TextBlock)
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "AsdaDataTableCache.h"
#include "Login/AsdaButtonRelay.h"
#include "Quest/AsdaQuestGuideTarget.h"   // ★ 31.10.2026: ключ asda.Npc.CellsBeyond10 (ячейки NPC сверх 10, патч сервера N22)
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaText.h"                 // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — подписи отказов по id
#include "UI/AsdaNotice.h"            // ★ 27.09 (clan_btn_6099_fix): MsgBoxLoc — окна нехватки/отказа обмена, как у эталона
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ гейт 13101: «открыто окно бустера» (эталонное поле +0x3F8)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"   // 30020 — магазин снаряжения, окно 6800
#include "UI/AsdaBoosterShopSubsystem.h" // 30034 — магазин бустеров, окно 15300
#include "Quest/AsdaQuestLogSubsystem.h"   // ★ волна В6 ремесла: смерть закрывает журнал квестов (`uiWnd_Close`, entity_char.c:16898)
#include "UI/AsdaHairShopSubsystem.h"    // 30029 — салон причёсок, окно 15000
// 30030 — элитный магазин 6000 (волна В5); хвост смерти   ★ R3: комментарий снятого включения "UI/AsdaPremiumShopSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaClanSubsystem.h"       // 30015/30016 — создание и роспуск клана
#include "UI/AsdaSoulmateSubsystem.h"   // ★ волна 1-А: 30023/30024/30025 — соулмейт (окно 9100, разрыв 5204)
#include "UI/AsdaExchangeSubsystem.h"   // 30017/30018/30019/30021/30028 — аукцион, окно 13000
#include "UI/AsdaWarehouseSubsystem.h"   // 30011/30012 — склад кладовщика, окно 4000 (+ свиток 5500)
#include "UI/AsdaConstellationSubsystem.h" // 30073 — выбор знака зодиака, окно 74500
#include "UI/AsdaRepairSubsystem.h"      // 30069 — окно ремонта 57300
#include "UI/AsdaWarShopSubsystem.h"
#include "UI/AsdaExtractShopSubsystem.h"   // 30035 — передача параметров, окно 16000
#include "UI/AsdaItemResetSubsystem.h"     // 30046/30047 — сброс улучшений, окно 20200 (2 режима)
#include "UI/AsdaItemOptionSubsystem.h"    // 30060 — смена параметров предмета, окно 50300     // 30070 — военный магазин 67200   // ★ магазин NPC: коды 30001…30004 / 30026 (окно 1300)
#include "UI/AsdaMarkup.h"          // UAsdaRichText::ApplyFormatted — теги <FC=…> в баннере 599
#include "UI/AsdaTipPanel.h"             // общая точка пересборки 9-slice (порт uiPanel9SliceLayout) + EAsdaTipOwner
#include "UI/AsdaItemTooltipSubsystem.h" // ОБЩАЯ точка подсказок предмета (второй копии билдеров нет)
#include "Engine/Engine.h"               // GEngine->GameViewport: размер вьюпорта для проверки центра
#include "Engine/GameViewportClient.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Quest/AsdaNsrScriptPage.h"       // ★ 04.11.2026: строка облачка без страницы — одно правило с метками
#include "Quest/AsdaQuestLogSubsystem.h"   // ОБЩАЯ точка `BuildRewardLines` — разбор RewardTable.BIN (второй копии нет)
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaIconCellGrid.h"   // общая точка: прозрачные клик-кнопки поверх JUI-ячеек (строки выбора награды)
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ 30009/30088/30010 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaCraftMakeWindowSubsystem.h"   // ★ хвост смерти entity_char.c:16910 — сброс окна изготовления 77500 (В8)
#include "UI/AsdaHuntReportSubsystem.h"         // ★ волна 2-Б: 30068 — окно «Доклад об охоте» 56450
#include "UI/AsdaTownWarSubsystem.h"            // ★ волна 2-Г: 30076 — окно налога города 79300
#include "TimerManager.h"
#include "EngineUtils.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ разговор молча не открывается
#include "Text/AsdaLangRows.h"              // [ПОРТ] 09.10.2026 (мультиязычность): перевод копии страницы сценария
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaNpcDialogSubsystem*.cpp (LogAsdaNpcDlg, чистые хелперы)
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: попап 540 «режим доски» (`uiQuestWin_openReward`), сброс ждущего сдачи
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ R3-c 12.11.2026: общее для частей AsdaNpcDialogSubsystem_Popups*.cpp

// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] — свой GC-якорь у кнопок каждого самостоятельного окна подсистемы (разбор у `RwRelays` в заголовке).
static ASDA_DEV_CVAR(int32, CVarAsdaNpcPopupRelayAnchor,
	TEXT("asda.Npc.PopupRelayAnchor"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = реле кнопок попапа наград 540 (567), окна приёма 750 (780), облачка 2400 (2411) и подсказки 599 (586) ")
	TEXT("держит свой якорь, пересоздание окна разговора 570 его не снимает. 0 = прежнее: общий якорь окна 570 — после его пересоздания ")
	TEXT("и сборки мусора кнопка молча не работает."),
	ECVF_Default);

TArray<TObjectPtr<UAsdaButtonRelay>>& UAsdaNpcDialogSubsystem::PopupRelayAnchor(TArray<TObjectPtr<UAsdaButtonRelay>>& Own)
{
	return CVarAsdaNpcPopupRelayAnchor.GetValueOnGameThread() != 0 ? Own : Relays;
}

// ★ Константа цвета подписей 615/616 (`kArrowTextArgb = 0xFF000000`) УДАЛЕНА 2026-08-27 вместе с
//   их построением: цвет теперь приезжает из `EXS TextColor` прямо в ассет (см. `SetupControls`).
// ── ЭТАПЫ Э3…Э5: адреса окон, таблиц и номера локализованных строк ──────────────────────────────
static const TCHAR* kMqWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_MULTYQUEST_WIN.WBP_MULTYQUEST_WIN_C");
static const TCHAR* kQiWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_QUEST_INFO_WIN.WBP_QUEST_INFO_WIN_C");

// Строка списка облачка: `uiChatLogAddLine(..., -722535, ...)`.
//   ★ ИСПРАВЛЕНО: раньше здесь стояло `0xFFF4F4D9`, и это была ОШИБКА АРИФМЕТИКИ.
//   −722535 = 0x1_0000_0000 − 722535 = 0xFFF4F999 (проверка: 722535 = 0xB0667, а
//   0xFFFFFFFF − 0xB0666 = 0xFFF4F999). Цвет — бледно-жёлтый, а не кремовый.
static constexpr uint32 kMqRowArgb = 0xFFF4F999u;
// ★★★ ПОДСВЕТКА СТРОКИ СПИСКА — ЭТО ЕСТЬ У ОРИГИНАЛА, а не наша выдумка (`ui_core.c:112988…113113`,
//   рендер `ListBox`). У списка два поля-индекса: `+326` — строка ПОД КУРСОРОМ, `+327` — ВЫБРАННАЯ
//   (ставится в `uiList_OnMouseUpSelect@0x12EB9F0`, а `+326` — в `uiList_OnMouseDownHover@0x12EB980`,
//   и ТОЛЬКО когда левая кнопка НЕ нажата: `!CObject_GetStateByteAt(mouse, 0)`, то есть это именно
//   наведение). Под каждый из индексов рендер кладёт под текст квад цветом `D3DRS_TEXTUREFACTOR`:
//     `+326` → литерал −1762594576 = 0x96F0F0F0 (полупрозрачная белая планка);
//     `+327` → `~цвет_строки`.
static constexpr uint32 kMqHoverArgb = 0x96F0F0F0u;
// ★ ДЕФЕКТ ОРИГИНАЛА (исправляем и помечаем, закон `fix-original-defects`): для выбранной строки
//   эталон инвертирует ВЕСЬ ARGB (`~0xFFF4F999 = 0x000B0666`) — вместе с альфой, и планка выбора
//   выходит ПОЛНОСТЬЮ ПРОЗРАЧНОЙ, то есть невидимой. Что это именно опечатка, доказывает соседний
//   кусок ТОЙ ЖЕ функции: текст выбранной строки там инвертируется аккуратно, с явным
//   восстановлением альфы (`color &= 0xFFFFFF; color |= 0xFF000000`, `ui_core.c:113205…113215`).
//   Делаем так же: инверсия только RGB, альфа непрозрачная.
static constexpr uint32 kMqSelectArgb = 0xFF000000u | ((~kMqRowArgb) & 0x00FFFFFFu);
// `questDlg_LayoutEpisodeText`: высота строки списка 20, добавка к высоте окна 45, минимум 64.
static constexpr int32 kMqRowHeight = 20;
static constexpr int32 kMqBlockPad  = 45;
static constexpr int32 kMqMinWinH   = 64;
// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (bug-quest-follow, патч сервера N22): у NPC бывает больше 10 ячеек «ВЫДАЁТ»/«ПРИНИМАЕТ»
//   (таблица расширения `QuestNpcTableExt.Bin`). Высота блока облачка — не больше 10 строк (эталонный максимум), остальные
//   строки — прокруткой списка (`ScrollBox`), чтобы окно не росло за экран.
static constexpr int32 kMqVisibleRows = 10;
// Геометрия из `.JUI` (`gamemain_jui.ini:52311…52545`), нужная переразметке ширины:
//   окно 2406 `Size = "258 58"`, хит-зона списка 2408/2410 `Size = "210 110"`,
//   плашка шапки 2407/2409 `Size = "190 21"`, подпись шапки в WBP — 168 px.
static constexpr float kMqJuiWinW   = 258.f;
static constexpr float kMqJuiListW  = 210.f;
// Запас справа от самой длинной строки, чтобы текст не упирался в рамку.
static constexpr float kMqTextPad   = 10.f;
// Предохранитель: шире этого окно не растёт даже под очень длинное название эпизода.
static constexpr float kMqMaxWinW   = 760.f;
// Подсказка `QUEST_INFO_WIN`: эталонный показ баннера — 10 000 мс.
//   ★ ИСПРАВЛЕНО 2026-08-22: было 7 с. Семёрка взята из `uiWindowMsgHandler` событие 24
//   (`ui_core.c:59729`, `objStartTimedAction(…, 7000.0)`), но там ДРУГОЙ контрол — `this[4313]`,
//   к баннеру 599 отношения не имеющий. Настоящий таймер баннера один — `uiNpcExchangeTimerTick`
//   (`ui_core.c:61032`): `if (now - start > 10000.0) npcExchangeUiReset();`.
static constexpr float kQuestInfoShowSec = 10.f;

static constexpr int32 kLocMqInProgress = 6164;       // заголовок 2407 «В процессе»
static constexpr int32 kLocMqCompleted  = 6163;       // заголовок 2409 «Завершено»

bool UAsdaNpcDialogSubsystem::EnsureMultiQuestWindow()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (MqWin && WM->IsWindowLive(MqWin)) { return true; }
	MqRelays.Reset();   // ★ 31.10.2026: окно 2400 пересоздаётся — реле прежнего дерева мертвы, якорь только этого окна

	MqTxt00 = MqTxt01 = nullptr; MqTxt00Text = MqTxt01Text = nullptr;
	MqList00 = MqList01 = nullptr; MqClose = nullptr;
	MqFrame.Reset(); MqBaseGeom.Reset(); MqRowRelays.Reset();
	MqHdrBar00 = MqHdrBar01 = MqRBase00 = MqRBase01 = nullptr;
	MqSelCtrl = MqSelIndex = -1;
	MqWin = WM->OpenWindowByPath(kMqWinId, kMqWbp, kDlgZ + 5, ESlateVisibility::SelfHitTestInvisible);
	if (!MqWin) { return false; }
	WM->SetWindowVisible(kMqWinId, false);   // корень 2406 в .JUI объявлен `Visible="0"`

	auto Node = [&](const TCHAR* N) { return AsdaUi::FindNodeByName(MqWin, N, N); };
	MqTxt00 = Node(TEXT("MULTYQUEST_TXT00_grp"));
	if (!MqTxt00) { MqTxt00 = Node(TEXT("MULTYQUEST_TXT00")); }
	MqTxt01 = Node(TEXT("MULTYQUEST_TXT01_grp"));
	if (!MqTxt01) { MqTxt01 = Node(TEXT("MULTYQUEST_TXT01")); }
	MqTxt00Text = Cast<UTextBlock>(Node(TEXT("MULTYQUEST_TXT00_Text")));
	MqTxt01Text = Cast<UTextBlock>(Node(TEXT("MULTYQUEST_TXT01_Text")));
	// Обводки нет и здесь: строитель облачка (ui_core.c:59596-59620, контролы 2400…2405)
	//   `CClass_SetField926` не зовёт ни разу. См. §1 отчёта `npc_visual_fix.md`.
	// ★ УБРАНО 2026-08-27 — постоянные узлы окна, обводка снята в ассете (см. выше).
	MqClose = Cast<UButton>(Node(TEXT("MULTYQUEST_CLOSE")));
	if (MqClose)
	{
		AsdaUi::BindButton(this, MqClose, 2411, [this](int32 Id) { OnDialogButton(Id); }, PopupRelayAnchor(MqRelays));   // ★ U20: реле клика — общая точка
	}

	// Списки 2408 / 2410 — в `.JUI` это ListBox (скин `Action_List`), у которого импортёр оставил
	//   только хит-зону. Строим `UScrollBox` на ТОТ ЖЕ прямоугольник — как сделано у тела реплики.
	// ★ ГРАБЛИ, ПОЙМАННЫЕ ЖИВЫМ ДАМПОМ: `FindNodeByPrefix` сравнивает имена БЕЗ УЧЁТА РЕГИСТРА, и
	//   по префиксу «MULTYQUEST_LIST00» первым попадается декоративная планка
	//   `MultyQuest_List00_LBase_3_0` (14×21) — список получался шириной 14 пикселей. Настоящая
	//   хит-зона списка называется `MULTYQUEST_LIST00_5_HIT` (4,23,210,110), поэтому ищем узел,
	//   имя которого начинается с префикса И заканчивается на `_HIT`.
	auto FindHit = [&](const TCHAR* Prefix) -> UWidget*
	{
		UWidget* Best = nullptr;
		if (!MqWin || !MqWin->WidgetTree) { return nullptr; }
		MqWin->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (Best || !W) { return; }
			const FString N = W->GetName();
			if (N.StartsWith(Prefix, ESearchCase::IgnoreCase) && N.EndsWith(TEXT("_HIT"), ESearchCase::IgnoreCase))
			{
				Best = W;
			}
		});
		return Best;
	};
	auto MakeList = [&](const TCHAR* Prefix, const TCHAR* NewName) -> UScrollBox*
	{
		UWidget* Hit = FindHit(Prefix);
		UCanvasPanel* Host = Hit ? Cast<UCanvasPanel>(Hit->GetParent()) : nullptr;
		UCanvasPanelSlot* HS = Hit ? AsdaUi::SlotOf(Hit) : nullptr;
		if (!Host || !HS || !MqWin->WidgetTree) { return nullptr; }
		UScrollBox* SB = MqWin->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), FName(NewName));
		if (!SB) { return nullptr; }
		SB->SetOrientation(Orient_Vertical);
		SB->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* CS = Host->AddChildToCanvas(SB))
		{
			AsdaUi::CopyCanvasGeometry(HS, CS);   // дедуп Д6: та же общая точка, что и у списка реплик
			CS->SetZOrder(5);
		}
		return SB;
	};
	MqList00 = MakeList(TEXT("MULTYQUEST_LIST00"), TEXT("MULTYQUEST_LIST00_Lines"));
	MqList01 = MakeList(TEXT("MULTYQUEST_LIST01"), TEXT("MULTYQUEST_LIST01_Lines"));

	// ── УЗЛЫ ПЕРЕРАЗМЕТКИ (высота по эталону + ширина по тексту) ────────────────────────────────
	//   Рамка окна: скин `Popup_SmWin` объявлен `Type = "ResizeWindow"` (`JuiSkin.ini:40445`), все
	//   девять планов видимые, углы 16×16 — импортёр положил их прямо в корневую канву именами
	//   `MULTYQUEST_WIN_0_0…0_8` (живой дамп `Reports/headless__probe_multyquest_win.log`).
	for (int32 k = 0; k < 9; ++k)
	{
		MqFrame.Add(Cast<UImage>(MqFindExact(MqWin, *FString::Printf(TEXT("MULTYQUEST_WIN_0_%d"), k))));
	}
	// Плашка шапки и правый «уголок» — ищем среди ПРЯМЫХ детей группы: по префиксу имени сюда
	//   попали бы и подпись `_Text`, и сама группа `_grp` (закон `false-negatives-method`).
	auto ChildImageByPrefix = [](UWidget* Grp, const TCHAR* Prefix) -> UImage*
	{
		UPanelWidget* P = Cast<UPanelWidget>(Grp);
		if (!P) { return nullptr; }
		for (int32 i = 0; i < P->GetChildrenCount(); ++i)
		{
			UImage* Im = Cast<UImage>(P->GetChildAt(i));
			if (Im && Im->GetName().StartsWith(Prefix, ESearchCase::IgnoreCase)) { return Im; }
		}
		return nullptr;
	};
	MqHdrBar00 = ChildImageByPrefix(MqTxt00, TEXT("MULTYQUEST_TXT00_"));
	MqHdrBar01 = ChildImageByPrefix(MqTxt01, TEXT("MULTYQUEST_TXT01_"));
	MqRBase00  = ChildImageByPrefix(MqTxt00, TEXT("MultyQuest_List00_RBase"));
	MqRBase01  = ChildImageByPrefix(MqTxt01, TEXT("MultyQuest_List01_RBase"));

	// Исходная геометрия ВСЕХ двигаемых узлов — снимок сразу после сборки окна, до первого показа.
	{
		auto Remember = [this](UWidget* W)
		{
			if (UCanvasPanelSlot* S = W ? AsdaUi::SlotOf(W) : nullptr)
			{
				MqBaseGeom.Add({ W, S->GetOffsets(), S->GetAlignment() });
			}
		};
		for (const TObjectPtr<UImage>& Im : MqFrame) { Remember(Im); }
		Remember(MqClose);
		Remember(MqTxt00);   Remember(MqTxt01);
		Remember(MqHdrBar00); Remember(MqHdrBar01);
		Remember(MqTxt00Text); Remember(MqTxt01Text);
		Remember(MqRBase00); Remember(MqRBase01);
		Remember(MqList00);  Remember(MqList01);
	}

	// Заголовки 2407 / 2409 эталон ПЕРЕЗАПИСЫВАЕТ текстами 6164 / 6163 (`CWidget_SetTextAll4`).
	if (UAsdaTextSubsystem* TS = GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>())
	{
		if (MqTxt00Text) { MqTxt00Text->SetText(FText::FromString(TS->GetTextById(kLocMqInProgress))); }
		if (MqTxt01Text) { MqTxt01Text->SetText(FText::FromString(TS->GetTextById(kLocMqCompleted))); }
	}
	WM->RegisterWindowCloser(kMqWinId, FSimpleDelegate::CreateUObject(this, &UAsdaNpcDialogSubsystem::HideMultiQuest));
	int32 FramePlanes = 0;
	for (const TObjectPtr<UImage>& Im : MqFrame) { if (Im) { ++FramePlanes; } }
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] MULTYQUEST_WIN разобран: txt00=%d txt01=%d list00=%d list01=%d закрыть=%d ")
		TEXT("рамка=%d/9 шапки=%d/%d уголки=%d/%d база=%d узлов"),
		MqTxt00 ? 1 : 0, MqTxt01 ? 1 : 0, MqList00 ? 1 : 0, MqList01 ? 1 : 0, MqClose ? 1 : 0,
		FramePlanes, MqHdrBar00 ? 1 : 0, MqHdrBar01 ? 1 : 0,
		MqRBase00 ? 1 : 0, MqRBase01 ? 1 : 0, MqBaseGeom.Num());
	return true;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ПЕРЕРАЗМЕТКА ОБЛАЧКА 2406 — порт `questDlg_LayoutEpisodeText@0xDA1BB0` (`quest.c:7976…8123`)
//
//  ЧТО ДЕЛАЕТ ЭТАЛОН, построчно:
//    1. `quest.c:8015…8020` — СНАЧАЛА ставит окну позицию «центр экрана»:
//         dlgCenterX = CConfig_GetWidth()/2  − ширина_окна/2;
//         dlgCenterY = CConfig_GetHeight()/2 − высота_окна/2;   SetField74_75(2406, …)
//       ⇒ облачко у оригинала РОЖДАЕТСЯ ПОСЕРЕДИНЕ ЭКРАНА, а `Origin = "373 255"` из `.JUI` —
//         только редакторская заготовка. В порт это перенесено не было — отсюда претензия №1.
//    2. высота списка = `20 × количество строк` (`SetSize(2408/2410, ширина_как_была, 20·N)`);
//    3. ВЫСОТА ОКНА:
//         режим 0 (только выдача) : `20·give + 45`, но не меньше 64  (`quest.c:8108…8116`);
//         режим 1 (только сдача)  : `20·take + 45`, но не меньше 64  (`quest.c:8043…8048`);
//         режим 2 (оба)           : `(20·give + 45) + (20·take + 45)`, не меньше 64 (`8079…8090`);
//       ширину окна эталон НЕ трогает (берёт текущую и кладёт обратно) — отсюда претензия №2.
//    4. режим 1: блок «Готово» переезжает НА позицию блока «Выбор задания»;
//       режим 2: блок «Готово» встаёт в `(x_блока_2407, 20·give + 45)` — координата АБСОЛЮТНАЯ в
//       системе окна, а не «плюс к своей» (`quest.c:8081`).
//
//  ★ ЧТО ДОБАВЛЕНО НАМИ (помечено, закон `fix-original-defects`): ШИРИНА ПО ТЕКСТУ. Оригинал её не
//    считает, потому что корейское название эпизода в 210 px влезало всегда; русское — нет, и
//    владелец видит текст, вылезающий за рамку. Ширина берётся общей точкой
//    `AsdaUi::MeasureTextWidth` по САМОЙ ДЛИННОЙ фактической строке обоих списков и по обеим
//    шапкам, растягиваются рамка (`UAsdaTipPanel::Resize9Slice`), плашки шапок, подписи, правые
//    «уголки», хит-зоны списков и кнопка закрытия (в `.JUI` она `AlignXPos = "2 0 0"` — прибита к
//    ПРАВОМУ краю окна, поэтому обязана ехать вместе с ним).
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaNpcDialogSubsystem::ResetMultiQuestGeometry()
{
	for (const FMqBase& B : MqBaseGeom)
	{
		if (UCanvasPanelSlot* S = B.W.IsValid() ? AsdaUi::SlotOf(B.W.Get()) : nullptr)
		{
			// Якорь возвращаем ТОЧЕЧНЫЙ: `CenterWindowOnScreen` переводит корневые слоты на 0.5,
			//   и без сброса вторая переразметка считала бы от уже сдвинутых координат.
			S->SetAnchors(FAnchors(0.f, 0.f));
			S->SetAlignment(B.Align);
			S->SetOffsets(B.Off);
		}
	}
}

FVector2D UAsdaNpcDialogSubsystem::RelayoutMultiQuest(int32 Mode, int32 Give, int32 Take)
{
	ResetMultiQuestGeometry();
	// Видимых строк в блоке — не больше kMqVisibleRows; остальные прокручиваются (см. объявление). Ключ
	//   `asda.Npc.CellsBeyond10 0` — как было: высота по числу строк (их там не больше 10).
	if (AsdaQuestGuideTarget::CellsBeyond10On())
	{
		Give = FMath::Min(Give, kMqVisibleRows);
		Take = FMath::Min(Take, kMqVisibleRows);
	}

	// ── рамка есть целиком? `Resize9Slice` при неполной девятке молча выходит ────────────────────
	int32 HavePlanes = 0;
	for (const TObjectPtr<UImage>& Im : MqFrame) { if (Im && AsdaUi::SlotOf(Im)) { ++HavePlanes; } }
	UCanvasPanelSlot* P0 = (MqFrame.Num() == 9 && MqFrame[0]) ? AsdaUi::SlotOf(MqFrame[0]) : nullptr;
	if (HavePlanes != 9 || !P0)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] MULTYQUEST: рамка 2406 неполная (планов %d из 9) — размер окна НЕ пересчитан"),
			HavePlanes);
		return FVector2D(kMqJuiWinW, (float)kMqMinWinH);
	}
	const FVector2D Origin(P0->GetOffsets().Left, P0->GetOffsets().Top);

	// ── 1. ВЫСОТА — ровно эталонная формула ──────────────────────────────────────────────────────
	const int32 Block0 = kMqRowHeight * Give + kMqBlockPad;   // блок «выдаёт»
	const int32 Block1 = kMqRowHeight * Take + kMqBlockPad;   // блок «принимает»
	int32 H = kMqMinWinH;
	if (Mode == 0)      { H = FMath::Max(Block0, kMqMinWinH); }
	else if (Mode == 1) { H = FMath::Max(Block1, kMqMinWinH); }
	else if (Mode == 2) { H = FMath::Max(Block0 + Block1, kMqMinWinH); }

	// ── 2. ШИРИНА — наше дополнение: по самой длинной ФАКТИЧЕСКОЙ строке ─────────────────────────
	float NeedList = 0.f;   // сколько пикселей нужно спискам 2408/2410
	auto ScanBox = [&NeedList](UScrollBox* Box)
	{
		if (!Box) { return; }
		for (int32 i = 0; i < Box->GetChildrenCount(); ++i)
		{
			// строка = SizeBox → Button → TextBlock (см. `Fill` в `ShowMultiQuest`)
			UWidget* Cur = Box->GetChildAt(i);
			UTextBlock* T = nullptr;
			for (int32 Depth = 0; Cur && Depth < 4 && !T; ++Depth)
			{
				T = Cast<UTextBlock>(Cur);
				UPanelWidget* Pan = Cast<UPanelWidget>(Cur);
				UContentWidget* Cw = Cast<UContentWidget>(Cur);
				Cur = T ? nullptr : (Pan && Pan->GetChildrenCount() > 0 ? Pan->GetChildAt(0)
				                    : (Cw ? Cw->GetContent() : nullptr));
			}
			if (T)
			{
				NeedList = FMath::Max(NeedList,
					AsdaUi::MeasureTextWidth(T->GetText().ToString(), T->GetFont(), 0.f));
			}
		}
	};
	ScanBox(MqList00);
	ScanBox(MqList01);
	float DeltaW = FMath::Max(0.f, NeedList + kMqTextPad - kMqJuiListW);
	// Шапки: подпись в WBP шириной 168 px внутри плашки 190 px; если перевод длиннее — тянем тоже.
	auto ScanHdr = [&DeltaW](UTextBlock* T)
	{
		UCanvasPanelSlot* S = T ? AsdaUi::SlotOf(T) : nullptr;
		if (!S) { return; }
		const float Have = S->GetOffsets().Right;
		const float Need = AsdaUi::MeasureTextWidth(T->GetText().ToString(), T->GetFont(), 0.f);
		DeltaW = FMath::Max(DeltaW, Need + kMqTextPad - Have);
	};
	if (Mode == 0 || Mode == 2) { ScanHdr(MqTxt00Text); }
	if (Mode == 1 || Mode == 2) { ScanHdr(MqTxt01Text); }
	DeltaW = FMath::Clamp(FMath::CeilToFloat(DeltaW), 0.f, kMqMaxWinW - kMqJuiWinW);
	const float W = kMqJuiWinW + DeltaW;

	// ── 3. применяем ────────────────────────────────────────────────────────────────────────────
	UAsdaTipPanel::Resize9Slice(MqFrame, FVector2D(W, (float)H), Origin);
	// Кнопка 2411 прибита к правому краю окна (`.JUI`: `AlignXPos = "2 0 0"`, `Origin = "-22 7"`).
	if (UCanvasPanelSlot* CS = MqClose ? AsdaUi::SlotOf(MqClose) : nullptr)
	{
		const FMargin O = CS->GetOffsets();
		CS->SetOffsets(FMargin(O.Left + DeltaW, O.Top, O.Right, O.Bottom));
	}
	// Растяжка блока: плашка шапки, подпись, правый «уголок» шапки и хит-зона списка.
	auto Grow = [DeltaW](UWidget* W2, bool bMove)
	{
		if (UCanvasPanelSlot* S = W2 ? AsdaUi::SlotOf(W2) : nullptr)
		{
			const FMargin O = S->GetOffsets();
			S->SetOffsets(bMove ? FMargin(O.Left + DeltaW, O.Top, O.Right, O.Bottom)
			                    : FMargin(O.Left, O.Top, O.Right + DeltaW, O.Bottom));
		}
	};
	Grow(MqHdrBar00, false); Grow(MqTxt00Text, false); Grow(MqRBase00, true);
	Grow(MqHdrBar01, false); Grow(MqTxt01Text, false); Grow(MqRBase01, true);
	Grow(MqList00, false);   Grow(MqList01, false);
	// Сами группы 2407/2409 — тоже шире (канва не режет по границе, но размер обязан быть честным).
	Grow(MqTxt00, false);    Grow(MqTxt01, false);

	// Высота списков — эталонная `20 × N` (позиция блоков ниже).
	auto SetBoxHeight = [](UScrollBox* Box, int32 N)
	{
		if (UCanvasPanelSlot* CS = Box ? AsdaUi::SlotOf(Box) : nullptr)
		{
			const FMargin O = CS->GetOffsets();
			CS->SetOffsets(FMargin(O.Left, O.Top, O.Right, (float)(kMqRowHeight * FMath::Max(N, 0))));
		}
	};
	SetBoxHeight(MqList00, Give);
	SetBoxHeight(MqList01, Take);

	// Позиция блока «Готово» (2409). У эталона координата в системе ОКНА, поэтому считаем от
	//   верхнего края рамки: `y = Origin.Y + 20·give + 45`; блок 2407 стоит в `.JUI` на «24 14», а
	//   группа в WBP начинается на 14 px левее (декоративная планка `LBase`) — значит X блока
	//   «Готово» обязан совпасть с X группы 2407 (эталон берёт именно X контрола 2407).
	if (UCanvasPanelSlot* S0 = MqTxt00 ? AsdaUi::SlotOf(MqTxt00) : nullptr)
	{
		if (UCanvasPanelSlot* S1 = MqTxt01 ? AsdaUi::SlotOf(MqTxt01) : nullptr)
		{
			const FMargin A = S0->GetOffsets(), B = S1->GetOffsets();
			if (Mode == 1)      { S1->SetOffsets(FMargin(A.Left, A.Top, B.Right, B.Bottom)); }
			else if (Mode == 2) { S1->SetOffsets(FMargin(A.Left, Origin.Y + (float)Block0, B.Right, B.Bottom)); }
		}
	}
	return FVector2D(W, (float)H);
}

// Порт `questNpc_CollectDeliverableItems@0xD89550` (ветка `a2 == 0`, `quest.c:6070-6146`).
// ★★★ ЭТО ТА ЖЕ ЭТАЛОННАЯ ФУНКЦИЯ, что и у меток над головой NPC / на карте: там она портирована
//   в режиме СЧЁТА `a2 == 1` — `UAsdaQuestMarkerSubsystem::CollectDeliverableForNsr`. Режимы
//   различаются ТОЛЬКО тем, считает функция строки или заполняет ими списки облачка; НАБОР ГЕЙТОВ
//   у них общий. Поэтому любая правка гейтов здесь обязана повторяться там, и наоборот — иначе
//   игрок видит противоречие: над NPC метка «есть дело», а облачко пустое (или наоборот).
//
// ★ ИСПРАВЛЕНО 2026-09-02 (гейт списка «ВЫДАЁТ»). БЫЛО: `QM->IsQuestTaken(E.QuestId)` — это порт
//   ДРУГОЙ эталонной функции, `questFindActiveSlotByType@0xC61EE0` (`quest.c:3042`):
//       for (i = 0; i < 12; ++i) if (слот+8 == a2 && слот+0 >= 2001) return 1;
//   Эталон же в этом месте зовёт `CQuest_HasActiveQuestOrReq(g_pQuestMgr, 0, questId)`
//   (`quest.c:6076`, тело — `0xC62E10`, `quest.c:3343`), а это совсем другая проверка:
//       1) в `Episode_Table` ищется строка с `+236 == questId` (ПОСЛЕДНЯЯ совпавшая);
//       2) если она есть и её `+284` (категория) == 1 — «да» возвращается, когда в журнале уже
//          лежит ЛЮБОЙ другой квест категории 1 с ДРУГИМ кодом эпизода (`+200`). Смысл правила:
//          у игрока не может быть двух «категорийных» (сюжетных) заданий сразу;
//       3) финальный цикл `слот+0 == questId`.
//   Расхождение доказано и по коду, и по данным (второй метод, закон `false-negatives-method`):
//     · п.3 у оригинала МЁРТВ: `слот+0` — это КОД ЭПИЗОДА (в `EpisodeTable.json` 2001…2466), а
//       `questId` записи SNQ — КЛЮЧ квеста (`QuestKey`, 0…467, пересечения нет ни одного);
//       у пустого слота там отрицательное значение (`cquestStoreQuestSlotData@0xC61F60`
//       занимает слот по условию `< 0`). Дефект оригинала; НЕ «чиним» — иначе гейт стал бы строже
//       оригинала и вырезал бы из облачка законные строки;
//     · п.2 у оригинала ЖИВОЙ: категорию 1 в `EpisodeTable.json` имеют 49 эпизодов, и 31 запись
//       выдачи `SNQInfo.Bin` ссылается на такие квесты. Прежний гейт этого правила не знал вовсе —
//       облачко предлагало сюжетный квест при уже взятом другом сюжетном, а метка (она уже
//       считает по эталону) молчала;
//     · «квест уже взят» оригинал отсекает НЕ здесь, а условием самой записи `SNQICT.Bin`
//       (поля `QuestFreeA` / `QuestFreeB` = «квест не взят и не пройден»): из 354 пар
//       (запись выдачи → ключ `SNQICT`) такое условие стоит у 348. Прежний гейт дублировал его и
//       заодно рубил оставшиеся 6 (ключи 142, 146, 172, 569, 984, 987 — там только уровень/класс).
//   Вычислитель НЕ заводился: `HasActiveQuestOrReqByKey` уже есть в порту (`AsdaQuestMarkerSubsystem`),
//   им же пользуются метки и гейт сдачи `questCheckAcceptPrereq` — закон `check-existing-before-creating`.
void UAsdaNpcDialogSubsystem::BuildQuestLists()
{
	GiveList.Reset(); TakeList.Reset();
	UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	const FAsdaNpcSnqRow* Snq = AsdaDt::RowByInt<FAsdaNpcSnqRow>(kDtNpcSnq, CurrentNpcId, TEXT("NpcDialog"));
	if (!QM || !Snq) { return; }

	// Эталон перебирает РОВНО 10 записей каждого списка (`for (i = 0; i < 10; ++i)`), столько же
	//   их и в `SNQInfo.Bin`.
	// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (bug-quest-follow): ячейки сверх 10 — из таблицы расширения (`QuestNpcTableExt.Bin`, патч
	//   NpcServer N22 принимает слот ≥ 10): перебираем все ячейки строки `DT_NpcSnq`, номер ячейки = слот в 5041/5043.
	//   Ключ `asda.Npc.CellsBeyond10 0` — как было: 10 ячеек и не больше 10 строк в списке.
	const int32 Lim     = AsdaQuestGuideTarget::NpcCellLimit();
	const int32 GiveNum = FMath::Min(Snq->Give.Num(), Lim);
	const int32 TakeNum = FMath::Min(Snq->Take.Num(), Lim);

	// «ВЫДАЁТ» (категория 0): гейт — `!CQuest_HasActiveQuestOrReq(mode 0)` И `!questIsCompleteFlagSet`;
	//   затем по каждому из пяти ключей — запись SNQICT, её условие, и в список кладётся тройка
	//   (questId, ключ, talkId записи).
	// ★ Приёмник эталона (`self+24013`) держит ровно 10 троек, а счётчик `collectCount` эталон не ограничивает и при 11-й
	//   строке пишет за границу буфера, прямо в список «принимает» (`self+24043`) — дефект оригинала. У нас списки —
	//   `TArray`, строк сколько пропустят условия (ячеек у NPC может быть больше 10, см. выше).
	for (int32 i = 0; i < GiveNum && GiveList.Num() < Lim; ++i)
	{
		const FAsdaSnqEntry& E = Snq->Give[i];
		if (AsdaNsrScript::StartClosed(E.QuestId)) { continue; }   // ★ 06.11.2026: выдача закрыта
		if (E.QuestId < 0) { continue; }                              // эталон: `SLOWORD(...) >= 0`
		if (QM->HasActiveQuestOrReqByKey(E.QuestId)) { continue; }    // CQuest_HasActiveQuestOrReq(0, …)
		if (QM->IsQuestCompletedPublic(E.QuestId)) { continue; }      // questIsCompleteFlagSet (sub_9318A8)
		for (int32 j = 0; j < E.Keys.Num() && GiveList.Num() < Lim; ++j)
		{
			const FAsdaSnqIctRow* R = AsdaDt::RowByInt<FAsdaSnqIctRow>(kDtSnqIct, E.Keys[j], TEXT("NpcDialog"));
			if (!R || R->Key < 0) { continue; }                       // эталон: `rec && *rec >= 0`
			// ★ 04.11.2026 [ИСПРАВЛЕНИЕ ДЕФЕКТА ДАННЫХ] (questmark-chmella): 15 из 887 ячеек ведут на страницу, которой нет в сценарии
			//   (Фхашу 190/191/193/192/196, Тид 127/128, Туран 48, talkId 0 у 127/83/96npc; tomb1 127/128/129 — сценарий возвращён из
			//   донора, см. `Tools/asda_nsr_donor_restore.py`). Эталон показывает
			//   строку, выбор гасит облачко и не рисует ничего — тупик. Строка ведёт на страницу шага с тем же ключом (12 ячеек),
			//   иначе не показывается; метка считает ровно те же строки (одно правило `AsdaNsrScript::ResolveCellTalkId`).
			const int32 Talk = AsdaNsrScript::ResolveCellTalkId(CurrentNsr, R->TalkId, R->Key);
			if (Talk < 0) { continue; }
			FString Why;
			if (!QM->EvalSnqIctCond(*R, &Why)) { continue; }
			GiveList.Add({ E.QuestId, R->Key, Talk });
		}
	}
	// «ПРИНИМАЕТ» (категория 1): гейт — `CQuest_HasActiveQuestOrReq(mode 1)`, то есть «квест с этим
	//   номером просто занимает слот журнала». Порт этого режима — `IsQuestReadyToHandIn`, он же
	//   стоит в счётной ветке меток; правки не потребовал.
	for (int32 i = 0; i < TakeNum && TakeList.Num() < Lim; ++i)
	{
		const FAsdaSnqEntry& E = Snq->Take[i];
		if (E.QuestId < 0) { continue; }
		if (!QM->IsQuestReadyToHandIn(E.QuestId)) { continue; }       // CQuest_HasActiveQuestOrReq(1, …)
		for (int32 j = 0; j < E.Keys.Num() && TakeList.Num() < Lim; ++j)
		{
			const FAsdaSnqIctRow* R = AsdaDt::RowByInt<FAsdaSnqIctRow>(kDtSnqIct, E.Keys[j], TEXT("NpcDialog"));
			if (!R || R->Key < 0) { continue; }
			const int32 Talk = AsdaNsrScript::ResolveCellTalkId(CurrentNsr, R->TalkId, R->Key);   // тупик без страницы — см. выше
			if (Talk < 0) { continue; }
			if (!QM->TakeCellOpenAtStage(CurrentNpcId, E.QuestId, R->Key)) { continue; }   // ★ 06.11.2026: чужая стадия — сервер не примет
			FString Why;
			if (!QM->EvalSnqIctCond(*R, &Why)) { continue; }
			TakeList.Add({ E.QuestId, R->Key, Talk });
		}
	}
}

// Порт `questDlgUpdateEpisodeState@0xDA1A60` + `questDlg_LayoutEpisodeText@0xDA1BB0`.
void UAsdaNpcDialogSubsystem::ShowMultiQuest()
{
	if (!EnsureMultiQuestWindow()) { return; }
	BuildQuestLists();

	const int32 Give = GiveList.Num();
	const int32 Take = TakeList.Num();
	// Режим: 0 = только выдача, 1 = только сдача, 2 = оба, −1 = ничего.
	const int32 Mode = (Give > 0 && Take > 0) ? 2 : (Give > 0 ? 0 : (Take > 0 ? 1 : -1));

	UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	MqSelCtrl = MqSelIndex = -1;
	MqRowRelays.Reset();
	// ★★★ СТРОКА СПИСКА — КНОПКА, А НЕ НАДПИСЬ (претензия №3: «ничего не могу выбрать»).
	//   У оригинала контролы 2408/2410 — движковый `ListBox` (скин `Action_List`,
	//   `JuiSkin.ini:7524`, `Type = "ListBox"`), и клик по строке обрабатывает САМ КЛАСС списка:
	//     `uiList_OnMouseUpSelect@0x12EB9F0` → `CControl_RaiseEvent(…, 69889, индекс_строки)` →
	//     `uiWindowMsgHandler@0xD91EC0` (`ui_core.c:59723`) → `npcDialog_RenderPageByCode`.
	//   Импортёр от `ListBox` оставил только хит-зону-картинку, поэтому в порту строки были
	//   обычными `UTextBlock` — кликать было НЕЧЕМ, а `OnMultiQuestPick` звался лишь из консоли.
	//   Строим строку как `SizeBox(20) → Button → TextBlock`: высота 20 — эталонная (`20 × N`),
	//   кнопка даёт наведение/нажатие, `Button` под `SizeBox` растягивается на всю ширину списка.
	//   ★ Общий механизм состояний подписи (`AsdaUi::BindButtonCaptionStates`) здесь НЕ применим:
	//     он берёт состояния из КАДРОВ скина, а у `Action_List` кадр ровно один
	//     (`PlaneAlwaysInvisible = "1"`, `SS = "0"` единственный) — состояний в оформлении нет.
	//     Их даёт КЛАСС списка: полупрозрачная планка под наведённой строкой и планка выбора
	//     (см. константы `kMqHoverArgb` / `kMqSelectArgb` выше).
	auto Fill = [&](UScrollBox* Box, const TArray<FAsdaQuestPick>& List, int32 CtrlId)
	{
		if (!Box || !MqWin || !MqWin->WidgetTree) { return; }
		// ★ Старые строки убираем ИЗ ДЕРЕВА, а не только из панели: `ConstructWidget` даёт им
		//   ЯВНЫЕ имена (`MULTYQUEST_ROW_<контрол>_<индекс>`), а окно показывается много раз.
		//   Оставленный в дереве прежний виджет с тем же именем — это и мусор, и риск, что
		//   `GetWidgetFromName` (им пользуются симуляция клика и дампы) вернёт СТАРУЮ строку.
		{
			TArray<UWidget*> Old;
			for (int32 i = 0; i < Box->GetChildrenCount(); ++i) { Old.Add(Box->GetChildAt(i)); }
			Box->ClearChildren();
			for (UWidget* W2 : Old)
			{
				if (!W2) { continue; }
				TArray<UWidget*> Sub;
				MqWin->WidgetTree->GetChildWidgets(W2, Sub);
				for (UWidget* Ch : Sub) { MqWin->WidgetTree->RemoveWidget(Ch); }
				MqWin->WidgetTree->RemoveWidget(W2);
			}
		}
		const FLinearColor Col = AsdaUi::SrgbU32ToLinear(kMqRowArgb);
		for (int32 Idx = 0; Idx < List.Num(); ++Idx)
		{
			const FAsdaQuestPick& P = List[Idx];
			UTextBlock* T = MqWin->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			if (!T) { break; }
			if (NameText) { T->SetFont(NameText->GetFont()); }
			AsdaUi::SetTextOutline(T, 0);   // §1 отчёта: список облачка 2408/2410 (скин Action_List) — без обводки
			T->SetColorAndOpacity(FSlateColor(Col));
			const FString Title = QM ? QM->EpisodeTitleByQuestKey(P.QuestId) : FString();
			T->SetText(FText::FromString(Title.IsEmpty()   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Задание %d» = 990446
				? UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990446), { FString::FromInt(P.QuestId) }) : Title));
			T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // клик ловит кнопка, не подпись

			UButton* B = MqWin->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
				FName(*FString::Printf(TEXT("MULTYQUEST_ROW_%d_%d"), CtrlId, Idx)));
			if (!B) { break; }
			// ★ Синее кольцо фокуса UE снимаем СРАЗУ, до попадания кнопки в панель: `SButton`
			//   читает флаг только в `Construct`, а строится он на `AddChild`. Общая точка —
			//   `AsdaUi::DisableButtonFocusRing` (оконная версия зовёт её же в цикле).
			AsdaUi::DisableButtonFocusRing(B);
			FButtonStyle St = B->GetStyle();
			// ★ Кисти — РОВНО ЗАЛИВКА (`FSlateColorBrush`), без картинки и без рамки: эталон рисует
			//   под строкой ПЛОСКИЙ КВАД цветом `D3DRS_TEXTUREFACTOR`. Стандартные кисти кнопки UE
			//   несут скруглённую подложку с обводкой — на кадре это была синяя рамка вокруг
			//   выбранной строки, которой у оригинала нет.
			St.Normal  = FSlateColorBrush(FLinearColor(0.f, 0.f, 0.f, 0.f));            // покой — прозрачно
			St.Hovered = FSlateColorBrush(AsdaUi::SrgbU32ToLinear(kMqHoverArgb));       // `+326`
			St.Pressed = FSlateColorBrush(AsdaUi::SrgbU32ToLinear(kMqSelectArgb));      // `+327`
			St.NormalPadding = St.PressedPadding = FMargin(0.f);
			B->SetStyle(St);
			B->SetClickMethod(EButtonClickMethod::MouseUp);   // эталон выбирает на ОТПУСКАНИИ мыши
			if (UButtonSlot* BS = Cast<UButtonSlot>(B->AddChild(T)))
			{
				BS->SetPadding(FMargin(4.f, 0.f, 2.f, 0.f));
				BS->SetHorizontalAlignment(HAlign_Left);
				BS->SetVerticalAlignment(VAlign_Center);
			}
			AsdaUi::BindButton(this, B, Idx, [this, CtrlId](int32 RowIdx) { OnMultiQuestPick(CtrlId, RowIdx); }, MqRowRelays);   // ★ U20: реле клика — общая точка

			USizeBox* SzB = MqWin->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			if (!SzB) { break; }
			SzB->SetHeightOverride((float)kMqRowHeight);   // ровно эталонные 20 px на строку
			SzB->AddChild(B);
			Box->AddChild(SzB);
		}
	};
	Fill(MqList00, GiveList, 2408);
	Fill(MqList01, TakeList, 2410);

	UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
	if (Mode < 0)
	{
		// Эталон при mode == −1 всё равно показывает окно (пустое). 1:1, но со строкой в лог.
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] MULTYQUEST: у NPC %d нет ни выдачи, ни сдачи — окно пустое"), CurrentNpcId);
	}
	if (WM) { WM->SetWindowVisible(kMqWinId, true); WM->BringWindowToFront(kMqWinId); }

	SetVis(MqTxt00, Mode == 0 || Mode == 2);
	SetVis(Cast<UWidget>(MqList00), Mode == 0 || Mode == 2);
	SetVis(MqTxt01, Mode == 1 || Mode == 2);
	SetVis(Cast<UWidget>(MqList01), Mode == 1 || Mode == 2);
	if (MqClose) { SetVis(MqClose, true); }

	// Высота окна и блоков — эталонная формула; ширина — по фактическому тексту (наше дополнение).
	//   Считать ОБЯЗАТЕЛЬНО после `Fill`: ширина берётся по уже созданным строкам.
	const FVector2D Size = RelayoutMultiQuest(Mode, Give, Take);

	// ★★★ ОКНО РОЖДАЕТСЯ ПОСЕРЕДИНЕ ЭКРАНА — это ЭТАЛОННОЕ поведение (`quest.c:8015…8020`),
	//   а не наша вольность: `questDlg_LayoutEpisodeText` первым же делом ставит окну
	//   `(ширина_экрана/2 − w/2, высота_экрана/2 − h/2)`. В порту это делает ОБЩАЯ ТОЧКА
	//   `UAsdaWindowSubsystem::CenterWindowOnScreen` (её же использует попап наград 540), и
	//   опорным контролом отдаём ЦЕНТРАЛЬНЫЙ план рамки: его прямоугольник после `Resize9Slice`
	//   всегда концентричен рамке, поэтому по центру экрана встаёт именно ОКНО, а не общий
	//   bbox (в него входят группы 2407/2409, вылезающие ниже рамки, и центр уехал бы вниз).
	//   ★ ДЕФЕКТ ОРИГИНАЛА, который мы не повторяем: эталон центрирует по РАЗМЕРУ ДО пересчёта
	//     высоты (строки 8015…8020 идут ПЕРЕД `SetSize` в строках 8044/8087/8112), поэтому у него
	//     окно с двумя блоками уезжает вниз на половину прироста. Мы центрируем ПОСЛЕ.
	if (WM && MqFrame.Num() == 9 && MqFrame[4])
	{
		WM->CenterWindowOnScreen(MqWin, MqFrame[4]);
	}

	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] MULTYQUEST показан: npcId=%d режим=%d выдача=%d сдача=%d размер=%.0fx%.0f (JUI 258x58)"),
		CurrentNpcId, Mode, Give, Take, Size.X, Size.Y);
}

// Планка выбранной строки (эталонное поле списка `+327`, `ui_core.c:112990…112993`).
void UAsdaNpcDialogSubsystem::ApplyMultiQuestSelection()
{
	auto Paint = [this](UScrollBox* Box, int32 CtrlId)
	{
		if (!Box) { return; }
		for (int32 i = 0; i < Box->GetChildrenCount(); ++i)
		{
			USizeBox* SzB = Cast<USizeBox>(Box->GetChildAt(i));
			UButton* B = SzB ? Cast<UButton>(SzB->GetContent()) : nullptr;
			if (!B) { continue; }
			FButtonStyle St = B->GetStyle();
			const bool bSel = (CtrlId == MqSelCtrl && i == MqSelIndex);
			St.Normal = FSlateColorBrush(bSel ? AsdaUi::SrgbU32ToLinear(kMqSelectArgb)
			                                  : FLinearColor(0.f, 0.f, 0.f, 0.f));
			B->SetStyle(St);
		}
	};
	Paint(MqList00, 2408);
	Paint(MqList01, 2410);
}

void UAsdaNpcDialogSubsystem::HideMultiQuest()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kMqWinId, false);
	}
}

// Порт `npcDialog_RenderPageByCode@0xDA2440` + `npcDialog_RenderPageByMode@0xDA2510`.
// ★ ПОДТВЕРЖДЕНО ПО КОДУ: при выборе строки НА СЕРВЕР НЕ УХОДИТ НИЧЕГО. Обе функции содержат
//   ровно три действия: проверить `this[437]`, погасить окно 2406 и вызвать `npcDialogRenderPage`
//   с talkId, который был положен в третий элемент тройки ЕЩЁ ПРИ ПОСТРОЕНИИ списка.
//   Чем тогда фиксируется выбор — см. отчёт: НИЧЕМ, кроме перехода на страницу; сам квест
//   принимается позже, кнопкой шага, пакетом 5043 (`AcceptQuestId` страницы, на которую перешли).
void UAsdaNpcDialogSubsystem::OnMultiQuestPick(int32 CtrlId, int32 Index)
{
	if (bRewardPopupMode) { return; }                 // гейт `this[437]`
	const TArray<FAsdaQuestPick>& List = (CtrlId == 2408) ? GiveList : TakeList;
	if (Index <= -1 || !List.IsValidIndex(Index)) { return; }
	if (List[Index].QuestId <= -1) { return; }
	// Эталон запоминает выбранную строку в поле списка `+327` (`uiList_OnMouseUpSelect`) — держим
	//   то же состояние и перекрашиваем строку, чтобы выбор был виден и в момент закрытия окна.
	MqSelCtrl = CtrlId; MqSelIndex = Index;
	ApplyMultiQuestSelection();
	HideMultiQuest();
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] MULTYQUEST: выбран %s квест %d (ключ %d) → страница %d; на сервер НИЧЕГО не уходит"),
		(CtrlId == 2408) ? TEXT("выдаваемый") : TEXT("сдаваемый"),
		List[Index].QuestId, List[Index].Key, List[Index].TalkId);
	RenderPage(List[Index].TalkId);
}

bool UAsdaNpcDialogSubsystem::EnsureQuestInfoWindow()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (QiWin && WM->IsWindowLive(QiWin)) { return true; }
	QiRelays.Reset();   // ★ 31.10.2026: подсказка 599 пересоздаётся — реле прежнего дерева мертвы, якорь только этого окна
	QiText = nullptr; QiClose = nullptr;
	QiWin = WM->OpenWindowByPath(kQiWinId, kQiWbp, kDlgZ + 6, ESlateVisibility::SelfHitTestInvisible);
	if (!QiWin) { return false; }
	WM->SetWindowVisible(kQiWinId, false);
	// ★ ИМЕНА ВЗЯТЫ ИЗ ЖИВОГО ДАМПА WBP (`Tools/_probe_npc_dialog_widgets.py`), а не угаданы:
	//   `QUEST_INFO` — сам текст (JUI 587), `QUEST_INFO_WIN` — ДРУГОЙ TextBlock (корень 599),
	//   `QUEST_INFO_x` — кнопка закрытия (586), `QUEST_INFO_GodRobot_Alarm_4_0` — анимированная
	//   иконка робота (скин `Ani_GodRobot_Alarm`, 16 кадров 4×4; в порту это `UAsdaAnimatedPlane`).
	QiText = MqFindExact(QiWin, TEXT("QUEST_INFO"));
	// ★ Стиль снимаем ЗДЕСЬ, пока 587 ещё `UTextBlock` из JUI: после первого `ApplyFormatted`
	//   это будет `URichTextBlock`, и исходные кегль/цвет/выравнивание/перенос уже не прочитать.
	if (UTextBlock* Tb = Cast<UTextBlock>(QiText))
	{
		QiBaseStyle = FTextBlockStyle()
			.SetFont(Tb->GetFont())
			.SetColorAndOpacity(Tb->GetColorAndOpacity())
			.SetShadowOffset(Tb->GetShadowOffset())
			.SetShadowColorAndOpacity(Tb->GetShadowColorAndOpacity());
		QiJustify = AsdaUi::ReadJustification(Tb);   // общая точка: свойство protected, геттера нет
		bQiWrap   = Tb->GetAutoWrapText();
		bQiStyleCaptured = true;
	}
	// ★ Текст 587 в эталоне получает `CClass_SetField926(self[4288], 0x200000)` (ui_core.c:59691) —
	//   это ЖИРНЫЙ (font_text.c:6832, глиф дважды со сдвигом +1 X), а НЕ обводка (0x800000).
	//   ⇒ обводку снимаем, начертание оставляем шрифту ассета (псевдо-жирного у UTextBlock нет;
	//   вопрос вынесен в реестр отложенного строкой про начертание 587).
	// ★ УБРАНО 2026-08-27 — постоянный узел окна, обводка снята в ассете (см. выше).
	QiClose = Cast<UButton>(MqFindExact(QiWin, TEXT("QUEST_INFO_x")));
	QiTalkBox = MqFindExact(QiWin, TEXT("QUEST_INFO_TalkBox_grp"));
	QiRobot = AsdaUi::FindNodeByPrefix(QiWin, TEXT("QUEST_INFO_GodRobot_Alarm"));
	if (QiClose)
	{
		AsdaUi::BindButton(this, QiClose, 586, [this](int32 Id) { OnDialogButton(Id); }, PopupRelayAnchor(QiRelays));   // ★ U20: реле клика — общая точка
	}
	WM->RegisterWindowCloser(kQiWinId, FSimpleDelegate::CreateUObject(this, &UAsdaNpcDialogSubsystem::HideQuestInfo));
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] QUEST_INFO_WIN разобран: текст=%d закрыть=%d"),
		QiText ? 1 : 0, QiClose ? 1 : 0);
	return true;
}

// Порт `npcExchangeUiSetText@0xDA3D60` (+ ветка очереди `uiPostDeathRedNotice`): текст в 587,
//   окно видно, таймер авто-скрытия 10 с.
void UAsdaNpcDialogSubsystem::ShowQuestInfoEx(const FString& Text, bool bMarkBusy, bool bMayDefer)
{
	if (!EnsureQuestInfoWindow()) { return; }

	// ★★★ ОЧЕРЕДЬ НА ОДИН ОТЛОЖЕННЫЙ ТЕКСТ (порт `uiPostDeathRedNotice`, `ui_core.c:32946`):
	//   `if (+370 != 1 || +4211) показать_сразу; else в_очередь;`
	//   То есть пока висит баннер, НЕ пометивший себя «занят» (это подсказка «Минопио» —
	//   `BuffNameList_BuildColoredText` ставит только +370), отложенный текст ждёт, а не стирает её.
	if (bMayDefer && bQiActive && !bQiBusy)
	{
		QiQueuedText = Text;
		bQiQueued = true;
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] QUEST_INFO: «%s» отложен в очередь (баннер занят)"), *Text);
		return;
	}

	if (QiText)
	{
		// ★★★ РАЗМЕТКА. Текст приходит СЫРЫМ, с тегами `<FC=AARRGGBB>` (подсказка «Минопио» красит
		//   названия карт, уведомление о смерти — всю строку). Показать их может только
		//   `UAsdaRichText::ApplyFormatted`; он ПОДМЕНЯЕТ `UTextBlock` на `URichTextBlock` в том же
		//   слоте и ВОЗВРАЩАЕТ новый узел — поэтому кэш обязательно переприсваиваем, иначе он
		//   осиротеет на выброшенном виджете и текст перестанет обновляться.
		//   Стиль отдаём свой, снятый из JUI (иначе rich возьмёт дефолт `DT_AsdaRichStyle`).
		FAsdaRichApplyOpts Opts;
		if (bQiStyleCaptured)
		{
			Opts.BaseStyle   = &QiBaseStyle;
			Opts.bSetJustify = true;
			Opts.Justify     = QiJustify;
			Opts.bWrap       = bQiWrap;
		}
		if (UWidget* New = UAsdaRichText::ApplyFormatted(QiText, Text, bQiStyleCaptured ? &Opts : nullptr))
		{
			QiText = New;
		}
		SetVis(QiText, true);
	}
	// Корень 599 в `.JUI` объявлен `Visible="0"` — разворачиваем пузырь и анимированную иконку явно.
	SetVis(QiTalkBox, true);
	SetVis(QiRobot, true);
	if (QiClose) { SetVis(QiClose, true); }
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kQiWinId, true);
		WM->BringWindowToFront(kQiWinId);
	}
	bQiActive = true;                       // +370
	if (bMarkBusy) { bQiBusy = true; }      // +4211
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		W->GetTimerManager().ClearTimer(QuestInfoTimer);
		W->GetTimerManager().SetTimer(QuestInfoTimer, FTimerDelegate::CreateUObject(this, &UAsdaNpcDialogSubsystem::OnQuestInfoExpired),
			kQuestInfoShowSec, false);
	}
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] QUEST_INFO: «%s» на %.0f с (занят=%d)"),
		*Text, kQuestInfoShowSec, bQiBusy ? 1 : 0);
}

// Порт ветки тика `uiNpcExchangeTimerTick@0xD98840` после истечения 10 с: погасить, затем
//   ПОКАЗАТЬ ОТЛОЖЕННЫЙ текст, если он есть (эталон при этом взводит +4211), иначе снять «занят».
void UAsdaNpcDialogSubsystem::OnQuestInfoExpired()
{
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] QUEST_INFO: истекли %.0f с — гашу баннер 599 (в очереди=%d)"),
		kQuestInfoShowSec, bQiQueued ? 1 : 0);
	HideQuestInfo();
	if (bQiQueued)
	{
		const FString Queued = QiQueuedText;
		bQiQueued = false;
		QiQueuedText.Empty();
		ShowQuestInfoEx(Queued, /*bMarkBusy*/ true, /*bMayDefer*/ false);
	}
	else
	{
		bQiBusy = false;
	}
}

void UAsdaNpcDialogSubsystem::HideQuestInfo()
{
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		W->GetTimerManager().ClearTimer(QuestInfoTimer);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kQiWinId, false);
	}
	// Порт `npcExchangeUiReset@0xDA3DF0`: гасит окно и снимает +370.
	//   ⚠ Признак «занят» (+4211) и ОЧЕРЕДЬ (+4210 / +16848) он НЕ трогает — воспроизводим 1:1.
	//   Следствие оригинала: текст, отложенный в очередь, при закрытии крестиком не показывается
	//   сразу, а всплывает по истечении следующего показа. Это поведение эталона, не наша ошибка.
	bQiActive = false;
}

// ★ ЖИВОЙ ДАМП ПОДСКАЗКИ 599 — то, что нельзя увидеть на снимке: ЭФФЕКТИВНАЯ видимость (с учётом
//   всех родителей), живой прямоугольник крестика и остаток таймера авто-скрытия.
FString UAsdaNpcDialogSubsystem::DumpQuestInfo() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const float Left = (W && QuestInfoTimer.IsValid())
		? W->GetTimerManager().GetTimerRemaining(QuestInfoTimer) : -1.f;
	FString S = FString::Printf(
		TEXT("=== QUEST_INFO_WIN (599) ===\n")
		TEXT("  виджет окна=%d в реестре видим=%d | +370 показан=%d +4211 занят=%d очередь=%d\n")
		TEXT("  таймер авто-скрытия: %s | диалог NPC открыт=%d попап наград=%d\n"),
		QiWin ? 1 : 0,
		(WM && WM->IsWindowVisible(kQiWinId)) ? 1 : 0,
		bQiActive ? 1 : 0, bQiBusy ? 1 : 0, bQiQueued ? 1 : 0,
		(Left >= 0.f) ? *FString::Printf(TEXT("%.2f с"), Left) : TEXT("не заведён"),
		bDialogOpen ? 1 : 0, bRewardPopupMode ? 1 : 0);
	auto Line = [&](const TCHAR* Tag, const UWidget* Wd)
	{
		FVector2D Mn(0, 0), Mx(0, 0);
		const bool bRect = Wd && AsdaUi::WidgetAbsRect(Wd, Mn, Mx);
		S += FString::Printf(TEXT("  %-26s виджет=%d видим=%d rect=%s\n"),
			Tag, Wd ? 1 : 0, (Wd && IsEffectivelyVisible(Wd)) ? 1 : 0,
			bRect ? *FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y) : TEXT("нет"));
	};
	Line(TEXT("QUEST_INFO_WIN (599)"), QiWin);
	Line(TEXT("QUEST_INFO (587) текст"), QiText);
	Line(TEXT("QUEST_INFO_TalkBox_grp"), QiTalkBox);
	Line(TEXT("QUEST_INFO_GodRobot_Alarm"), QiRobot);
	Line(TEXT("QUEST_INFO_x (586)"), QiClose);
	if (QiClose)
	{
		S += FString::Printf(TEXT("  крестик: подписчиков OnClicked=%d, видимость узла=%d\n"),
			QiClose->OnClicked.IsBound() ? 1 : 0, static_cast<int32>(QiClose->GetVisibility()));
	}
	return S;
}

// ★ Нажатие крестика ТЕМ ЖЕ путём, что и мышь: через `OnClicked` самой кнопки. Прямой вызов
//   `OnDialogButton(586)` проверял бы вторую реализацию правила и пропустил бы ровно тот дефект,
//   из-за которого крестик не работал (гейт на входе `OnDialogButton`).
bool UAsdaNpcDialogSubsystem::PressQuestInfoClose()
{
	if (!QiClose)
	{
		UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] QuestInfoX: кнопки QUEST_INFO_x нет (окно 599 не собрано)"));
		return false;
	}
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] QuestInfoX ДО:\n%s"), *DumpQuestInfo());
	QiClose->OnClicked.Broadcast();
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] QuestInfoX ПОСЛЕ:\n%s"), *DumpQuestInfo());
	return true;
}
