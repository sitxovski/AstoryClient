// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystem_Raise.cpp — ★ R2 10.11.2026 (разрез AsdaWindowSubsystem.cpp, дословный перенос): подъём окна: тело/показ, всегда сверху, фокус нажатия, кнопки.
//   Общее для частей — в AsdaWindowSubsystemInternal.h; шапка и реверс — в AsdaWindowSubsystem.cpp.
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableText.h"   // поле ввода 1583 диалога SHOUTING_WIN
#include "Components/CheckBox.h"         // ★ приёмка: галки под ручкой (INVEN_NEW_WIN 83503)
#include "Components/EditableTextBox.h"  // ★ приёмка: поля ввода под ручкой
#include "Components/MultiLineEditableText.h"  // ★ приёмка: многострочный ввод
#include "Components/ComboBoxString.h"   // ★ приёмка: выпадающие списки
#include "Components/Slider.h"           // ★ приёмка: ползунки тянут мышь сами
#include "Components/ScrollBox.h"        // ★ приёмка: прокрутка тянет мышь сама
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"   // DPI-скейл вьюпорта: абсолютная дельта курсора → юниты канвы
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#if !UE_BUILD_SHIPPING   // ★ 31.10.2026: журнал синтетического клика — дев-диагностика, в Shipping не входит
#include "Widgets/Input/SButton.h"         // ★ 31.10.2026: состояние кнопки под синтетическим кликом (журнал `[симклик]`)
#include "Types/ReflectionMetadata.h"   // ★ 31.10.2026: имя виджета под синтетическим кликом (журнал `[симклик]`)
#endif
#include "Fonts/FontMeasure.h"   // метрики строки для asda.UI.DumpTextMetrics   // фокус на игровой вьюпорт при базовом GameAndUI
#include "Misc/ConfigCacheIni.h"   // раскладка окон между сессиями: секция [AsdaWindowLayout] в ini игры
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::Loc — подписи кнопок ТОЛЬКО из таблицы DT_GameText
// игровые loc-подписи (2288 «ОК» / 2289 «Отмена») для TWOBUT_MSGBOX   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaUiHelpers.h"         // D1b: общая `AsdaUi::WidgetAbsRect` (хит-тест окна и рамки)
// D2: фолбэк однокнопочного msgbox, когда WBP не загрузился   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
// шаг 13 `uiCloseAllWindows`: блокировка/разблокировка хотбара   ★ R3: комментарий снятого включения "UI/AsdaHotbarSubsystem.h" (файл им не пользовался) — сохранён
// шаги 23-24: сворачивание трея и запрет его кнопок   ★ R3: комментарий снятого включения "UI/AsdaSystemMenuSubsystem.h" (файл им не пользовался) — сохранён
#include "Resources/AsdaSoundSubsystem.h" // звуки открытия/закрытия окна количества (5 / 6)
// ★ 27.09.2026: панель 1530 для `DropTopMsgBoxBeforeNotice`   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaPurchaseBoxSubsystem.h"  // ★ 27.09.2026: панель 1559 для `DropTopMsgBoxBeforeNotice`
#include "Styling/SlateStyleRegistry.h"   // ★ гашение синей рамки фокуса: подмена кисти FocusRectangle
#include "Styling/SlateStyle.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateNoResource.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Login/AsdaText.h"                 // FAsdaText::Loc — строки диалога `asda.UI.ConfirmPreview` (07.10.2026)
#include "Login/AsdaLoginGameMode.h"          // ★ 08.10.2026: подъём при показе не трогает экран логина
#include "World/AsdaWorldKind.h"               // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "GameFramework/GameModeBase.h"     // GetAuthGameMode — проверка экрана логина
#include "UnrealClient.h"                   // С-1 (07.10.2026): FViewport::ViewportResizedEvent — смена размера вьюпорта
#include "Blueprint/GameViewportSubsystem.h" // М-1 (07.10.2026): слот окна во вьюпорте — полноэкранный ли он
#include "Widgets/SOverlay.h"               // ★ 08.10.2026: подъём окна БЕЗ снятия с вьюпорта (слот оверлея вьюпорта)
#include "UI/AsdaNotice.h"                  // ★ 08.10.2026 (ревизия В3 рыбалки): нотис 6835 строгого нумпада — LocRaw / ChatColored
// ★ 08.10.2026: ColGold — цвет −11514 нотиса 6835   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
// ★ 09.10.2026: UAsdaRichText::ApplyFormatted — теги <FC=…> в теле окон-сообщений   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Components/RichTextBlock.h"      // ★ 09.10.2026: тело окна-сообщения после первой подмены — URichTextBlock
#include "AsdaWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWindowSubsystem*.cpp (LogAsdaWindow, чистые хелперы)
#include "AsdaWindowSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaWindowSubsystem*.cpp

// ★ Ревизия подъёма (СРЕДН-2): 1 — возвращать фокус строке ввода прямо на НАЖАТИИ (прежняя редакция: рвёт клик фокусируемой
//   кнопки — `SButton::OnFocusLost` → `Release()`); 0 (по умолчанию) — на отпускании. Ключ — для контрольной пары самотеста.
static ASDA_DEV_CVAR(int32, CVarRaiseFocusAtPress,
	TEXT("asda.UI.RaiseFocusAtPress"), 0,
	TEXT("0 — фокус строки ввода возвращается на отпускании кнопки мыши; 1 — на нажатии (прежняя редакция, съедает клик фокусируемой кнопки)."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] Рабочий AlwaysTop корневых окон — 20 вызовов `set_o229@0xB6E310` (thunk 0x929A2C), аргументы сверены capstone.
//   Точки эталона — сборка UI при входе в игру или показ окна; окно порта собирается при открытии, поэтому значение применяется
//   при открытии (`OpenWindow`/`RegisterWindow`). Исключение — кнопка дня гачи 68075: флаг ставит порт-точка
//   `premiumGacha_BeginRoll` (`UAsdaPremiumGachaSubsystem`, `SetWindowAlwaysTop`), как в эталоне — в начале прокрута. Дочерние контролы (2580 `CHAT_NEW_Que_btn_tooltip` chat.c:1243, 15496
//   `BOOSTER_ITEM_SEL` shop_trade.c:262) на порядок корневых окон не влияют — в таблице их нет.
static const TMap<FName, bool>& GRuntimeAlwaysTop()
{
	static const TMap<FName, bool> M = {
		{ FName(TEXT("QUEST_WIN")),                 false },  // CQuest_Episode_BuildUI quest.c:1524 (0xC5B721), в .JUI 1
		{ FName(TEXT("MINIMAP_WIN")),               false },  // MiniMap_BuildUI world_map.c:7111 (0xD639A4)
		{ FName(TEXT("SystemMsg_MDown")),           true  },  // CChat_Init chat.c:557 (0xB9D4F1), 18600
		{ FName(TEXT("CHAT_SETUP_Win")),            true  },  // CChat_BuildTabUI chat.c:862 (0xB9E903), 2670
		{ FName(TEXT("EXTRACT_WIN")),               true  },  // CExtractShop_InitUI item_enhance.c:679 (0xC8D0B1), 16000
		{ FName(TEXT("BOOSTER_SHOP_WIN")),          true  },  // BoosterShop_InitWindow shop_trade.c:177 (0xB7E52C), 15300
		{ FName(TEXT("BOOSTER_PREV_WIN")),          true  },  // boosterWindowPopulate shop_trade.c:630 (0xB82DDC) = «показано», +0x9A0 = 15600
		{ FName(TEXT("CHAT_Color_Select_Win")),     true  },  // uiShowChildWindow ui_core.c:11609 (0xBA05FB), единственный вызов — палитра 2700
		{ FName(TEXT("SHELLITEM_STORE_WIN")),       true  },  // uiInventoryOpenWindow ui_core.c:28460 (0xC3DD77), +4 = 6000
		{ FName(TEXT("CAMPWAR_ATTACK_Position")),   true  },  // uiSynthResultWinShow ui_core.c:36503 (0xC9CE65), +0x5C8 = 49400
		{ FName(TEXT("HAIRSHOP_WIN")),              true  },  // uiHairShopWindowOnShow ui_core.c:45673 (0xCEF07B), CHairShopWnd +4
		{ FName(TEXT("Package_Contents_Win")),      true  },  // stampWnd_PopulateItemSlots item_enhance.c:3593 (+0x91C = 26500). ui_core.c:63195 —
		                                                      //   НЕ это окно: поле +0xC18 = 58850 Package_Purchase_NEW, дочерний контрол SHELLITEM_STORE_WIN
		{ FName(TEXT("CashCharge_Win")),            true  },  // CCashChargeWin_InitControls ui_core.c:86037 (0xEBD4A3), 18000 (в .JUI и так 1)
		{ FName(TEXT("ITEM_making_R_BOX")),         true  },  // uiCraftWnd_OpenForItem ui_core.c:101174 (0xFE21A0), +8 = 77000
		{ FName(TEXT("ITEM_TIPWIN")),               true  },  // CItemTipWin_InitUI item_inventory.c:59483 (0xEA188A), 5000
		{ FName(TEXT("ItemCompareTip_Win")),        true  },  // CItemTipWin_InitUI item_inventory.c:59463 (0xEA1751), 5050
	};
	return M;
}

// Разбор у объявления. Карта «старый z → новый z» строится по РАЗЛИЧНЫМ значениям полосы по возрастанию:
//   подвижное значение получает «предыдущее новое + 1», значение с окном AlwaysTop остаётся на месте (новое ≤ старого
//   всегда, поэтому якорь всегда выше предыдущего нового — порядок не нарушается). Перевешивание — по возрастанию
//   нового z, чтобы равные z легли во вьюпорт в прежнем порядке обхода.
int32 UAsdaWindowSubsystem::CompactRaiseBand(const TCHAR* Why)
{
	TMap<int32, int32> Map;              // старый z → новый z
	TArray<int32> Values;                // различные z (для журнала)
	int32 Prev = kRaiseBandFloor - 1;    // последнее выданное новое значение (обычная полоса)
	if (CVarAlwaysTopBand.GetValueOnGameThread() != 0)
	{
		// ★ Ревизия (СРЕДН-1): ДВЕ независимые полосы. Обычная [700, 8600) → подряд от 700; AlwaysTop [8600, 8699] → подряд
		//   от 8600 (вместе с дочерними окнами владельцев-AlwaysTop, они живут там же). 8700…8799 (всплывающие окна порта) не трогаются.
		auto Band = [&](int32 Lo, int32 Hi, int32& OutTop)
		{
			TArray<int32> V;
			for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
			{
				const int32 Z = Kv.Value.ZOrder;
				if (Z >= Lo && Z <= Hi && !Kv.Value.bModal) { V.AddUnique(Z); }
			}
			V.Sort();
			int32 P = Lo - 1;
			for (const int32 Z : V) { Map.Add(Z, ++P); Values.Add(Z); }
			OutTop = P;
		};
		int32 TopAt = kAlwaysTopZ - 1;
		Band(kRaiseBandFloor, kAlwaysTopZ - 1, Prev);
		Band(kAlwaysTopZ, kAlwaysTopZMax, TopAt);
	}
	else
	{
		TSet<int32> Anchors;             // z окон AlwaysTop в полосе — не двигаются (прежняя редакция)
		for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
		{
			const int32 Z = Kv.Value.ZOrder;
			if (Z < kRaiseBandFloor || Z >= kPriorityZ || Kv.Value.bModal) { continue; }
			Values.AddUnique(Z);
			if (Kv.Value.bAlwaysTop) { Anchors.Add(Z); }
		}
		Values.Sort();
		for (const int32 Z : Values)
		{
			const int32 NewZ = Anchors.Contains(Z) ? Z : Prev + 1;
			Map.Add(Z, NewZ);
			Prev = NewZ;
		}
	}
	// Кого перевешивать — по возрастанию нового z.
	TArray<TPair<int32, FName>> Moves;
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		const int32* NewZ = Map.Find(Kv.Value.ZOrder);
		if (NewZ && *NewZ != Kv.Value.ZOrder && !Kv.Value.bModal) { Moves.Emplace(*NewZ, Kv.Key); }
	}
	Moves.StableSort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B) { return A.Key < B.Key; });
	for (const TPair<int32, FName>& M : Moves) { SetWindowZOrder(M.Value, M.Key); }
	// Сохранённые z слоёв «поверх всего» — той же картой (иначе PopTopmost вернул бы окно на старое, высокое место).
	for (TPair<FName, FTopmostClaim>& Kv : TopmostClaims)
	{
		if (const int32* NewZ = Map.Find(Kv.Value.SavedZ)) { Kv.Value.SavedZ = *NewZ; }
	}
	UE_LOG(LogAsdaWindow, Log, TEXT("[z-порядок] СЖАТИЕ полосы %d…%d (%s): значений %d, окон сдвинуто %d, новый верх обычной полосы %d"),
		kRaiseBandFloor, kPriorityZ - 1, Why, Values.Num(), Moves.Num(), Prev);
	return Moves.Num();
}

void UAsdaWindowSubsystem::SetRaiseOnBodyClick(FName WindowId, bool bRaise)
{
	if (FAsdaWindowEntry* E = Windows.Find(WindowId)) { E->bRaiseOnBodyClick = bRaise; }
}

// ★★★ [ОРИГИНАЛ] Окна с байтом +0x24D («переставлять») = 0. Конструктор `uiControlConstructor` ставит 1 (0x12DAA95), обнуляет
//   только `set_o24D@0xB807C0` из пяти мест: `CChat_Init` (214 CHAT_BACKWIN, chat.c:230), `CChat_BuildTabUI` (2500, chat.c:788),
//   `CChat_BuildServNoticeUI` (2570, chat.c:984), `BoosterShop_InitWindow` (15600, shop_trade.c:461), `UIPackageStampWnd_Init`
//   (26500, ui_core.c:63039). У `sub_12AFF80` такое окно без AlwaysTop не переставляется НИКОГДА.
static const TSet<FName>& GNonRaisableWindows()
{
	static const TSet<FName> S = {
		FName(TEXT("CHAT_BACKWIN")), FName(TEXT("CHAT_NEW")), FName(TEXT("CHAT_NEW_SYSTEM")),
		FName(TEXT("BOOSTER_PREV_WIN")), FName(TEXT("Package_Contents_Win")) };
	return S;
}

// ★★★ [ОРИГИНАЛ] Корни окон с `NoSelect="1"` (байт +0x127, разбор `ui_core.c:104979-104984`) — `sub_12AFF80` их не переставляет.
//   Признака у меты окна НЕТ (импортёр кладёт в `NoHitWidgets` имена виджетов, а не флаг корня), перегенерация UI запрещена —
//   поэтому список выписан ИЗ ДАННЫХ: все корневые `[Ctrl]` `Data/Decoded/jui/gamemain_jui.ini` / `commonwin_jui.ini` / `temp_jui.ini`
//   с `NoSelect = "1"` (18 шт.; у WARMESSAGE_WIN, VILLAGE_NAME, NOTICE_WIN, EXPBar_WIn ещё и AlwaysTop).
const TSet<FName>& GNoSelectRootWindows()
{
	static const TSet<FName> S = {
		FName(TEXT("TARGETMARK_MON")), FName(TEXT("TARGETMARK_MAN")), FName(TEXT("TARGETMARK_MAN1")), FName(TEXT("GET_QUESTITEM")),
		FName(TEXT("QUEST_INFO_WIN")), FName(TEXT("TALKBOXW_WIN")), FName(TEXT("TALKBOXB_WIN")), FName(TEXT("ITEM_TIPWIN")),
		FName(TEXT("Con_Banner2")), FName(TEXT("ChangeJob_Banner")), FName(TEXT("TALKBOX_Mon_WIN")), FName(TEXT("SHELLITEM_MsgBase")),
		FName(TEXT("ItemCompareTip_Win")), FName(TEXT("PVP_Gfont_Win")), FName(TEXT("WARMESSAGE_WIN")), FName(TEXT("VILLAGE_NAME")),
		FName(TEXT("NOTICE_WIN")), FName(TEXT("EXPBar_WIn")) };
	return S;
}

// Окно-оверлей с NoSelect у корня (объявление — `AsdaWindowSubsystemInternal.h`).
bool AsdaIsNoSelectRootWindow(FName WindowId) { return GNoSelectRootWindows().Contains(WindowId); }

// ★★★ Правила эталона подъёма по нажатию (разбор полностью — в отчёте; строки ниже — ui_core.c/AsdaStory_game.c):
//   1) `uiWindowMgrDispatchMouse@0x12AF540` (ui_core.c:104134-104176): при захвате `+104` (модалка) мышь получает ТОЛЬКО она,
//      подъёма нет; иначе окна — сверху вниз (`rbegin`), первое, у которого `uiContainerHandleMouse` вернул 1 (нажатие ЛКМ/ПКМ/СКМ
//      в плане любого контрола окна), «съедает» событие и, если байт +0x244 = 0 (выставлен только у MINIMAP_WIN 1702,
//      `MiniMap_BuildUI` world_map.c:7110), передаётся в `sub_12AFF80`;
//   2) `sub_12AFF80@0x12AFF80` (миснейм «uiControl_RemoveChild», ui_core.c:104549-104585): переставляет, только если НЕ показана
//      подсказка (+0x1B0/+0x1B1) и у окна НЕТ AlwaysTop (+0x229), НЕТ NoSelect (+0x127) и ЕСТЬ «переставлять» (+0x24D); ветка
//      «AlwaysTop без +0x24D» в данных пуста (у пяти окон с +0x24D=0 AlwaysTop="0");
//   3) `uiControl_AddChildOrdered@0x12B2B60` (ui_core.c:105923): поднятое окно встаёт перед первым AlwaysTop — то есть наверх ОБЫЧНОГО
//      слоя; все AlwaysTop после загрузки .JUI собраны в конец списка (`mapClearMatchingEntries@0x12B2C40`, вызов ui_core.c:105126).
bool UAsdaWindowSubsystem::IsBodyRaiseEligible(FName WindowId, FString* OutWhy) const
{
	auto Say = [OutWhy](const TCHAR* Why, bool b) { if (OutWhy) { *OutWhy = Why; } return b; };
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return Say(TEXT("нет окна"), false); }
	if (CVarRaiseOnBodyClickAll.GetValueOnGameThread() == 0)
	{
		const bool bOld = E->DragHandles.Num() > 0 || E->bRaiseOnBodyClick;
		return Say(bOld ? TEXT("ключ 0: перетаскиваемое/явный признак") : TEXT("ключ 0: не поднимается (прежнее правило)"), bOld);
	}
	if (E->bModal || E->ZOrder >= kModalZ) { return Say(TEXT("модальное (захват +104: подъёма нет)"), false); }
	if (E->ZOrder >= kTopmostZ) { return Say(TEXT("[ПОРТ] слой поверх всего 9000+ (PushTopmost/подсказки) — выше обычного слоя"), false); }
	if (E->ZOrder >= kPriorityZ) { return Say(TEXT("[ПОРТ] полоса приоритетных 8800+ — уже выше обычного слоя"), false); }
	if (E->bAlwaysTopRuntime)
	{
		// ★ Ревизия (КРИТ-1): признак РАБОЧИЙ. Ветка `sub_12AFF80` «AlwaysTop без +0x24D» (ui_core.c:104571-104575) — окно
		//   переставляется `uiControl_AddChildOrdered` в КОНЕЦ списка (верх полосы AlwaysTop): BOOSTER_PREV_WIN, Package_Contents_Win.
		if (GNonRaisableWindows().Contains(WindowId) && CVarAlwaysTopBand.GetValueOnGameThread() != 0)
		{
			return Say(TEXT("[ОРИГИНАЛ] AlwaysTop без +0x24D — наверх полосы AlwaysTop (push_back)"), true);
		}
		return Say(TEXT("[ОРИГИНАЛ] AlwaysTop (+0x229, рабочий признак) — не переставляется"), false);
	}
	if (GNonRaisableWindows().Contains(WindowId)) { return Say(TEXT("[ОРИГИНАЛ] +0x24D=0 — не переставляется (свой порядок)"), false); }
	if (WindowId == FName(TEXT("MINIMAP_WIN"))) { return Say(TEXT("[ОРИГИНАЛ] +0x244=1 — мышь ловит, подъёма нет"), false); }
	if (GNoSelectRootWindows().Contains(WindowId) || E->NoHitWidgets.Contains(WindowId)) { return Say(TEXT("[ОРИГИНАЛ] NoSelect у корня (+0x127) — не переставляется"), false); }
	if (!E->bHasMeta)
	{
		// [ПОРТ] окна нет в .JUI — у оригинала его нет вовсе; прежнее правило порта.
		const bool bOld = E->DragHandles.Num() > 0 || E->bRaiseOnBodyClick;
		return Say(bOld ? TEXT("[ПОРТ] нет в .JUI, явный признак") : TEXT("[ПОРТ] нет в .JUI — оригинал о нём не знает, не поднимается"), bOld);
	}
	return Say(TEXT("[ОРИГИНАЛ] поднимается нажатием по телу"), true);
}

FName UAsdaWindowSubsystem::FindWindowCatchingAt(FVector2D Pt) const
{
	if (UAsdaUiVisibilitySubsystem::HiddenBlocksUiHitTest(this)) { return NAME_None; }   // ★ 21.10.2026 (Alt+Z): окна скрыты
	const bool bModalUp = IsAnyModalOpen();
	FName Best = NAME_None;
	int32 BestZ = MIN_int32;
	// Тот же порядок, что у `TickDrag`: по убыванию z, первое попадание.
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		const FAsdaWindowEntry& E = Kv.Value;
		// ★ 10.10.2026: гейт мыши модалки — общая точка `PassesModalMouseGate` (учитывает и модалки эталона без `bModal`);
		//   modal_lock2 (МЕЛК-2): с точкой — корень вложенного контрола ловит мышь только в прямоугольнике узла.
		if (!E.Widget || (bModalUp && !PassesModalMouseGateAt(Kv.Key, E, Pt)) || E.ZOrder <= BestZ) { continue; }
		const ESlateVisibility WV = E.Widget->GetVisibility();
		if (WV == ESlateVisibility::Collapsed || WV == ESlateVisibility::Hidden) { continue; }
		FName Hit;
		if (HitTestWidgetTree(E.Widget->GetRootWidget(), E.NoHitWidgets, Pt, Hit, AsdaDrawnBodyFor(Kv.Key, E))) { Best = Kv.Key; BestZ = E.ZOrder; }
	}
	return Best;
}

bool UAsdaWindowSubsystem::IsWindowHitAt(FName WindowId, FVector2D Pt) const
{
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return false; }
	const ESlateVisibility WV = E->Widget->GetVisibility();
	if (WV == ESlateVisibility::Collapsed || WV == ESlateVisibility::Hidden) { return false; }
	FName Hit;
	return HitTestWidgetTree(E->Widget->GetRootWidget(), E->NoHitWidgets, Pt, Hit, AsdaDrawnBodyFor(WindowId, *E));
}

bool UAsdaWindowSubsystem::HasCapturingControlAt(FName WindowId, FVector2D Pt) const
{
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	return E && E->Widget && FindSlateCapturingWidgetUnderCursor(E->Widget->GetRootWidget(), Pt);
}

TArray<FName> UAsdaWindowSubsystem::GetRegisteredWindowIds() const
{
	TArray<FName> Ids;
	Windows.GetKeys(Ids);
	return Ids;
}

bool UAsdaWindowSubsystem::ResolveAlwaysTop(FName WindowId) const
{
	if (const bool* O = AlwaysTopOverride.Find(WindowId)) { return *O; }          // выставлено кодом (порт `set_o229`)
	if (const bool* T = GRuntimeAlwaysTop().Find(WindowId)) { return *T; }        // таблица эталона (сборка/показ окна)
	const UAsdaJuiWindowMetaAsset* Meta = GetJuiMeta();
	const FAsdaJuiWindowMeta* M = Meta ? Meta->FindWindow(WindowId) : nullptr;
	return M && M->bAlwaysTop;                                                    // атрибут `.JUI`
}

bool UAsdaWindowSubsystem::IsWindowAlwaysTop(FName WindowId) const
{
	if (const FAsdaWindowEntry* E = Windows.Find(WindowId)) { return E->bAlwaysTopRuntime; }
	return ResolveAlwaysTop(WindowId);
}

bool UAsdaWindowSubsystem::HasAlwaysTopOwner(FName WindowId) const
{
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	for (int32 Guard = 0; E && !E->OwnerWindowId.IsNone() && Guard < 16; ++Guard)
	{
		const FAsdaWindowEntry* O = Windows.Find(E->OwnerWindowId);
		if (O && O->bAlwaysTopRuntime) { return true; }
		E = O;
	}
	return false;
}

// Верх полосы AlwaysTop без учёта самого окна и его потомков. Порядок внутри полосы — по показу (последний показанный выше),
//   как у эталона: AlwaysTop щелчком не переставляется, в конец списка его кладёт только загрузка/показ.
int32 UAsdaWindowSubsystem::NextAlwaysTopZ(FName WindowId)
{
	auto TopOfBand = [this, WindowId]() -> int32
	{
		int32 Top = kAlwaysTopZ - 1;
		for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
		{
			if (Kv.Key == WindowId || IsOwnedBy(Kv.Key, WindowId)) { continue; }
			const int32 Z = Kv.Value.ZOrder;
			if (Z >= kAlwaysTopZ && Z <= kAlwaysTopZMax) { Top = FMath::Max(Top, Z); }
		}
		return Top;
	};
	int32 Top = TopOfBand();
	if (Top + 1 > kAlwaysTopZMax && CVarZCompact.GetValueOnGameThread() != 0)
	{
		CompactRaiseBand(*FString::Printf(TEXT("показ AlwaysTop «%s» у потолка полосы %d"), *WindowId.ToString(), kAlwaysTopZMax));
		Top = TopOfBand();
	}
	return FMath::Min(Top + 1, kAlwaysTopZMax);
}

void UAsdaWindowSubsystem::SetWindowAlwaysTop(FName WindowId, bool bAlwaysTop)
{
	AlwaysTopOverride.Add(WindowId, bAlwaysTop);
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E) { return; }
	const bool bWas = E->bAlwaysTopRuntime;
	E->bAlwaysTopRuntime = bAlwaysTop;
	if (bWas == bAlwaysTop || CVarAlwaysTopBand.GetValueOnGameThread() == 0) { return; }
	UE_LOG(LogAsdaWindow, Log, TEXT("[z-порядок] «%s»: рабочий AlwaysTop %d → %d"), *WindowId.ToString(), bWas ? 1 : 0, bAlwaysTop ? 1 : 0);
	BringWindowToFront(WindowId);   // с флагом — верх полосы AlwaysTop; без — верх обычного слоя
}

// ═══════════════ ★★★ ОТКРЫТИЕ ПОВЕРХ ВСЕХ (правило владельца 08.10.2026) ═══════════════════════════════════════════
//   Обоснование — у объявления `IsShowRaiseEligible` в заголовке; отчёт `Reports/owner_1008/ui_bugs.md`.
// ★ [ПОРТ] Постоянный HUD ниже обычного слоя (z < 700): его показ — не «открытие окна игроком», а часть экрана (вход в мир,
//   выбор цели, полоса каста). Эталон такие контролы при показе не переставляет (`CControl_SetVisible` без `CClass_MapInsert`),
//   а поднятый наверх HUD накрыл бы открытые окна. Окна, которые игрок открывает сам (персонаж 94, навыки 94, разговор с NPC
//   120…129, большая карта 130, сообщество 400, …), в список НЕ входят — они поднимаются при показе.
static const TSet<FName>& GShowRaiseHudWindows()
{
	static const TSet<FName> S = {
		FName(TEXT("QUICK_NEW_WIN")),      // хотбар 92
		FName(TEXT("SYSTEMMAIN_NEW_WIN")), // трей меню 93 — при показе эталон кладёт его ВНИЗ (ui_core.c:79409), см. ShowHide
		FName(TEXT("MINISTATE_WIN")),      // мини-статус 95
		FName(TEXT("PVP_TARGET")),         // рамка цели 95
		FName(TEXT("SKILL_BAR_Win")),      // полоса каста 92
		FName(TEXT("EXPBar_WIn")),         // полоса опыта 90
		FName(TEXT("MINIMAP_WIN")), FName(TEXT("MINIMAPMIN_WIN")), FName(TEXT("OCCUPATION_BOX")),   // мини-карта 120
		FName(TEXT("Quest_State_Win")),    // трекер квестов 11
		// Ревизия ui_bugs_REVIEW СРЕДН-1 (08.10.2026): постоянные плашки HUD, которые игрок сам не открывает, —
		//   эталон показывает их `CControl_SetVisible` без `CClass_MapInsert`, то есть БЕЗ перестановки.
		FName(TEXT("SOULMATE_POP_WIN")),   // плашка соулмейта 40 (AsdaSoulmateSubsystem.h:267 «обязана лежать НИЖЕ окон»)
		FName(TEXT("PARTY_WIN")),          // ростер отряда 120 (показ по 5107, `SetRosterVisible`)
		FName(TEXT("Title_Notice_BG")) };  // плашка нотиса звания 300 (открывается видимой)
	// CAMPWAR_Time_Win — в этот же список, когда окно появится в порту (PORT_PENDING_ON_SYSTEMS.md).
	return S;
}

bool UAsdaWindowSubsystem::IsShowRaiseEligible(FName WindowId, FString* OutWhy) const
{
	auto Say = [OutWhy](const TCHAR* Why, bool b) { if (OutWhy) { *OutWhy = Why; } return b; };
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || !E->Widget) { return Say(TEXT("нет окна"), false); }
	if (CVarOpenOnTop.GetValueOnGameThread() == 0) { return Say(ASDA_DEV_TEXT(TEXT("ключ asda.UI.OpenOnTop 0 — прежнее поведение"), TEXT("прежнее поведение")), false); }
	// Логин — свои окна и свой порядок (закон «Login не трогать»).
	if (const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		if (AsdaWorldKind::IsLoginScene(W)) { return Say(TEXT("экран логина — не трогаем"), false); }   // ★ F7 (дедуп №2, В3): общий признак
	}
	if (E->bModal || E->ZOrder >= kModalZ) { return Say(TEXT("модальное — своя полоса 20000+, новое и так выше"), false); }
	if (E->ZOrder > kAlwaysTopZMax) { return Say(TEXT("своя полоса: всплывающие 8700+ / приоритетные 8800+ / поверх всего 9000+"), false); }
	// HUD и NoSelect-плашки (полоса опыта, нотисы, баннеры — часть их с AlwaysTop) — не «открываемые окна»: порядок не трогаем.
	if (GShowRaiseHudWindows().Contains(WindowId)) { return Say(TEXT("[ПОРТ] постоянный HUD — не открываемое окно"), false); }
	if (GNoSelectRootWindows().Contains(WindowId)) { return Say(TEXT("[ОРИГИНАЛ] NoSelect у корня — плашка/подсказка/баннер, мышь не ловит"), false); }
	if (E->bAlwaysTopRuntime) { return Say(TEXT("[ОРИГИНАЛ] AlwaysTop — показ кладёт в конец списка: верх полосы AlwaysTop"), true); }
	if (GNonRaisableWindows().Contains(WindowId)) { return Say(TEXT("[ОРИГИНАЛ] +0x24D=0 (чат/журнал) — свой порядок"), false); }
	return Say(TEXT("поднимается при показе (правило владельца 08.10.2026)"), true);
}

void UAsdaWindowSubsystem::WatchShow(FName WindowId, FAsdaWindowEntry& E)
{
	UnwatchShow(E);
	if (!E.Widget) { return; }
	const ESlateVisibility V = E.Widget->GetVisibility();
	E.bShownForRaise = V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
	E.ShowWatch = E.Widget->AddFieldValueChangedDelegate(UWidget::FFieldNotificationClassDescriptor::Visibility,
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &UAsdaWindowSubsystem::OnShowWatchVisibility, WindowId));
}

void UAsdaWindowSubsystem::UnwatchShow(FAsdaWindowEntry& E)
{
	if (E.ShowWatch.IsValid() && E.Widget)
	{
		E.Widget->RemoveFieldValueChangedDelegate(UWidget::FFieldNotificationClassDescriptor::Visibility, E.ShowWatch);
	}
	E.ShowWatch.Reset();
}

// Событие приходит ИЗНУТРИ чужого `SetVisibility` — здесь только отметка и очередь; перевешивание — в разборе очереди.
void UAsdaWindowSubsystem::OnShowWatchVisibility(UObject* Obj, UE::FieldNotification::FFieldId /*Field*/, FName WindowId)
{
	FAsdaWindowEntry* E = Windows.Find(WindowId);
	if (!E || E->Widget != Obj) { return; }   // событие старого виджета того же id
	const ESlateVisibility V = E->Widget->GetVisibility();
	const bool bShown = V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
	const bool bEdge = bShown && !E->bShownForRaise;
	E->bShownForRaise = bShown;
	if (!bEdge) { return; }
	// ★ 08.10.2026 (пакет 5, СРЕДН-1 ревизии пакета 4): экран менялся, пока окно с постановщиком было скрыто, — поставить его
	//   под текущий экран сейчас, на фронте показа, до первой отрисовки (модальные не исключаются: 1530 — модальное). Постановщик
	//   копируется: 1530 внутри себя заново заявляет `RePlacer` своей же записи. Запись ищется заново — после вызова.
	if (E->bRePlaceOnShow && E->RePlacer)
	{
		E->bRePlaceOnShow = false;
		const TFunction<void()> Placer = E->RePlacer;   // копия: вызов может переписать исходник
		Placer();
		UE_LOG(LogAsdaWindow, Log, TEXT("[раскладка окон] «%s»: показ после смены размера экрана — постановка оригинала заново"),
			*WindowId.ToString());
		E = Windows.Find(WindowId);
		if (!E || E->Widget != Obj) { return; }
	}
	// ★ 08.10.2026 (ревизия ui_bugs_REVIEW М-2): окно, утащенное в этой же сессии частично за край, при следующем показе
	//   проходит правило «за экраном» (как при запуске) — раньше кламп шёл только при восстановлении из ini. Здесь только
	//   заявка; мерит и двигает живой кламп в тике (`TickLayoutRestore` → `ClampWindowIntoViewport`), окно в экране не двигается.
	if (E->AppliedOffsets.Num() > 0 && !E->bModal && E->ZOrder < kModalZ)
	{
		for (const TPair<FName, FVector2D>& Kv : E->AppliedOffsets) { E->ClampPendingKeys.Add(Kv.Key); }
		LiveClampNotBeforeFrame = FMath::Max(LiveClampNotBeforeFrame, GFrameCounter + 1);   // геометрию показанного — со следующего кадра
		PendingClamp.Add(WindowId);
	}
	const bool bHeld = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Num() > 0;
	if (bHeld || bFlushingShowRaise) { EnqueueShowRaise(WindowId, TEXT("показ при зажатой кнопке мыши")); }
	else { RaiseOnShowNow(WindowId, TEXT("показ")); }
}

void UAsdaWindowSubsystem::RaiseOnShowNow(FName WindowId, const TCHAR* Why)
{
	FString WhyNot;
	if (!IsShowRaiseEligible(WindowId, &WhyNot))
	{
		UE_LOG(LogAsdaWindow, Verbose, TEXT("[открытие поверх] «%s»: не поднимается — %s"), *WindowId.ToString(), *WhyNot);
		return;
	}
	const FAsdaWindowEntry* E = Windows.Find(WindowId);
	const int32 Was = E ? E->ZOrder : -1;
	BringWindowToFront(WindowId);   // во время щелчка по другому окну — попадёт в `RaisedDuringPress`
	PendingShowRaise.Remove(WindowId);
	const FAsdaWindowEntry* E2 = Windows.Find(WindowId);
	UE_LOG(LogAsdaWindow, Log, TEXT("[открытие поверх] «%s»: z %d → %d (%s, сразу)"), *WindowId.ToString(), Was, E2 ? E2->ZOrder : -1, Why);
}

void UAsdaWindowSubsystem::EnqueueShowRaise(FName WindowId, const TCHAR* Why)
{
	if (CVarOpenOnTop.GetValueOnGameThread() == 0) { return; }
	PendingShowRaise.Remove(WindowId);
	PendingShowRaise.Add(WindowId);
	UE_LOG(LogAsdaWindow, Verbose, TEXT("[открытие поверх] «%s»: в очередь подъёма (%s), в очереди %d"), *WindowId.ToString(), Why, PendingShowRaise.Num());
}

int32 UAsdaWindowSubsystem::FlushShowRaise(const TCHAR* Why)
{
	if (PendingShowRaise.Num() == 0 || bFlushingShowRaise) { return 0; }
	// Ждём: подъём тела по щелчку ещё впереди (показанное щелчком должно встать ВЫШЕ него) или зажата кнопка мыши
	//   (перевешивание окна рвёт захват Slate — разбор у `SetWindowZOrder`).
	if (!PendingRaiseWindowId.IsNone()) { return 0; }
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Num() > 0) { return 0; }
	const TArray<FName> Queue = MoveTemp(PendingShowRaise);
	PendingShowRaise.Reset();
	TGuardValue<bool> Guard(bFlushingShowRaise, true);
	// Перевешивание окна во вьюпорте снимает клавиатурный фокус с его поля (окно открылось с фокусом в строке ввода —
	//   имя, количество, сообщение): фокус возвращается тому же виджету после подъёмов (эталонная перестановка фокус не трогает).
	const TSharedPtr<SWidget> Foc = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetKeyboardFocusedWidget() : nullptr;
	int32 N = 0;
	for (const FName& Id : Queue)
	{
		const FAsdaWindowEntry* E = Windows.Find(Id);
		if (!E || !AsdaWindowIsEntryVisible(*E)) { continue; }   // успели спрятать/закрыть — не поднимаем
		FString WhyNot;
		if (!IsShowRaiseEligible(Id, &WhyNot))
		{
			UE_LOG(LogAsdaWindow, Verbose, TEXT("[открытие поверх] «%s»: не поднимается — %s"), *Id.ToString(), *WhyNot);
			continue;
		}
		const int32 Was = E->ZOrder;
		BringWindowToFront(Id);
		const FAsdaWindowEntry* E2 = Windows.Find(Id);
		const int32 Now = E2 ? E2->ZOrder : -1;
		if (Now != Was) { ++N; }
		UE_LOG(LogAsdaWindow, Log, TEXT("[открытие поверх] «%s»: z %d → %d (%s)"), *Id.ToString(), Was, Now, Why);
	}
	if (N > 0) { RestoreFocusAfterRaise(Foc); }
	return N;
}

FName UAsdaWindowSubsystem::FindWindowOwningSlate(const TSharedPtr<SWidget>& W) const
{
	if (!W.IsValid()) { return NAME_None; }
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (!Kv.Value.Widget) { continue; }
		const TSharedPtr<SWidget> Root = Kv.Value.Widget->GetCachedWidget();
		if (!Root.IsValid()) { continue; }
		for (TSharedPtr<SWidget> P = W; P.IsValid(); P = P->GetParentWidget())
		{
			if (P == Root) { return Kv.Key; }
		}
	}
	return NAME_None;
}

// ★★★ ФОКУС ПОЛЯ ВВОДА НА НАЖАТИИ — правило эталона. Поле (`CEditBox`, vtable 0x150B458) теряет фокус в слоте 27
//   (`uiEditSetModalFocus@0x12F9780`, ui_core.c:116492): `uiContainerHandleMouse` зовёт его с (−1, 0) у КАЖДОГО обойдённого
//   контрола, которому нажатие не досталось (ui_core.c:107815, хвост `uiContainerHandleMouse`: `(resultFlags & 0x400) == 0`).
//   Обход окон идёт сверху вниз и ОСТАНАВЛИВАЕТСЯ на поймавшем окне ⇒ поля окон НИЖЕ поймавшего не обходятся и фокус
//   СОХРАНЯЮТ; поля окон выше (промах) и самого поймавшего (тело = план корня, дети обойдены раньше) — теряют.
//   Чат 2500/2570/2540 эталон держит В НАЧАЛЕ списка (`listMoveToEnd@0x12AFEA0` — миснейм, это push_front, chat.c:789/903/985)
//   ⇒ поле чата фокус теряет только при нажатии в мир или в само окно чата. Текст поля фокус НЕ стирает (`SetFocus(0)`).
//   В порту Slate на нажатии сам уводит фокус на вьюпорт — возвращаем его, если эталон бы его не снял.
void UAsdaWindowSubsystem::ApplyPressFocusRule(FName Catcher)
{
	PendingFocusRestore.Reset();
	const TSharedPtr<SWidget> Was = FocusBeforePress.Pin();
	if (!Was.IsValid() || Catcher.IsNone() || !FSlateApplication::IsInitialized()) { return; }
	FSlateApplication& App = FSlateApplication::Get();
	const TSharedPtr<SWidget> Now = App.GetKeyboardFocusedWidget();
	if (Now == Was) { return; }                                  // фокус не ушёл
	// Нажатие отдало фокус ДРУГОМУ полю (клик в поле ввода окна) — эталон так же: `uiEditSetModalFocus` снимает прежнее.
	if (Now.IsValid() && Now->GetType().ToString().Contains(TEXT("Editable"))) { return; }
	const FName Owner = FindWindowOwningSlate(Was);
	if (Owner.IsNone() || Owner == Catcher) { return; }          // поле не в окне / в самом поймавшем — эталон снимает
	static const TSet<FName> ChatBottom = { FName(TEXT("CHAT_NEW")), FName(TEXT("CHAT_NEW_SYSTEM")), FName(TEXT("CHAT_btn_Win")) };
	const FAsdaWindowEntry* EO = Windows.Find(Owner);
	const FAsdaWindowEntry* EC = Windows.Find(Catcher);
	if (!EO || !EC) { return; }
	const bool bOwnerBelow = ChatBottom.Contains(Owner) || EO->ZOrder < EC->ZOrder;
	if (!bOwnerBelow) { return; }                                // окно поля выше поймавшего — обойдено, фокус снят
	// ★★★ Ревизия (СРЕДН-2): НЕ на нажатии. Slate на нажатии отдал фокус фокусируемому виджету под курсором; если это `SButton`,
	//   возврат фокуса в том же кадре зовёт `SButton::OnFocusLost` → `Release()` — на отпускании `OnClicked` не придёт, клик съеден.
	//   Возвращаем на отпускании, когда клик уже доставлен (`TickDrag`, ветка «кнопки отпущены»).
	if (CVarRaiseFocusAtPress.GetValueOnGameThread() != 0)
	{
		App.SetKeyboardFocus(Was, EFocusCause::SetDirectly);
		UE_LOG(LogAsdaWindow, Log, TEXT("[фокус] (ключ RaiseFocusAtPress 1) возврат НА НАЖАТИИ: поле окна «%s» ниже «%s»"), *Owner.ToString(), *Catcher.ToString());
		return;
	}
	PendingFocusRestore = Was;
	UE_LOG(LogAsdaWindow, Log, TEXT("[фокус] нажатие поймало «%s» (z %d) — поле окна «%s» (z %d) ниже, фокус вернётся на отпускании (эталон: окно не обойдено)"),
		*Catcher.ToString(), EC->ZOrder, *Owner.ToString(), EO->ZOrder);
}

void UAsdaWindowSubsystem::RestoreFocusAfterRaise(const TSharedPtr<SWidget>& Was)
{
	if (!Was.IsValid() || !FSlateApplication::IsInitialized()) { return; }
	FSlateApplication& App = FSlateApplication::Get();
	if (App.GetKeyboardFocusedWidget() == Was) { return; }
	if (FindWindowOwningSlate(Was).IsNone()) { return; }         // виджет уже не в живом окне
	App.SetKeyboardFocus(Was, EFocusCause::SetDirectly);
	UE_LOG(LogAsdaWindow, Log, TEXT("[фокус] перевешивание окна во вьюпорте сняло фокус поля — возвращён (эталонная перестановка фокус не трогает)"));
}

void UAsdaWindowSubsystem::AttachJuiMeta(FName WindowId, FAsdaWindowEntry& Entry)
{
	Entry.DragHandles.Reset();
	Entry.NoHitWidgets.Reset();
	Entry.bAlwaysTop = false;
	Entry.bHasMeta = false;
	const UAsdaJuiWindowMetaAsset* Meta = GetJuiMeta();
	if (!Meta) { return; }
	const FAsdaJuiWindowMeta* M = Meta->FindWindow(WindowId);
	if (!M) { return; }
	Entry.bHasMeta = true;
	Entry.bAlwaysTop = M->bAlwaysTop;
	Entry.NoHitWidgets.Append(M->NoHitWidgets);
	if (GDragDisabledWindows().Contains(WindowId))
	{
		UE_LOG(LogAsdaWindow, Log,
			TEXT("[перетаскивание] «%s»: ручек в .JUI %d, но окно в списке-исключении (требование юзера) — drag выключен"),
			*WindowId.ToString(), M->DragHandles.Num());
		return;
	}
	Entry.DragHandles = M->DragHandles;
	if (Entry.DragHandles.Num() > 0)
	{
		UE_LOG(LogAsdaWindow, Verbose, TEXT("[перетаскивание] «%s»: ручек %d, NoSelect-виджетов %d"),
			*WindowId.ToString(), Entry.DragHandles.Num(), Entry.NoHitWidgets.Num());
	}
}

void UAsdaWindowSubsystem::ApplyButtonStateData(FName WindowId, UUserWidget* Widget, FAsdaWindowEntry& Entry)
{
	Entry.CaptionRelays.Reset();
	// ★★ Снять синее кольцо фокуса UE со ВСЕХ кнопок окна (обоснование — `AsdaUiHelpers.h`).
	//   Здесь, а не в импортёре, потому что починить надо ВСЕ уже собранные окна, а не только
	//   переимпортированные заново (тот же приём, что у `SetTextOutline`).
	const int32 F = AsdaUi::DisableButtonFocusRings(Widget);
	const int32 N = AsdaUi::BindButtonCaptionStates(Widget, Entry.CaptionRelays);
	if (F > 0)
	{
		UE_LOG(LogAsdaWindow, Verbose, TEXT("[фокус] «%s»: кольцо фокуса снято с %d кнопок"), *WindowId.ToString(), F);
	}
	if (N > 0)
	{
		UE_LOG(LogAsdaWindow, Verbose, TEXT("[подпись-состояния] «%s»: кнопок с hover/press-подписью %d"),
			*WindowId.ToString(), N);
	}
}

// ════════ ★★★ РАСКЛАДКА ОКОН МЕЖДУ СЕССИЯМИ (позиции + размеры) — ОДИН МЕХАНИЗМ НА ВСЕ ОКНА ═════
// Полное обоснование (эталон сохранения НЕ имеет; почему сдвиг, а не абсолютная точка; по какому
// признаку окно считается сохраняемым) — в шапке блока в `AsdaWindowSubsystem.h`. Здесь — кухня.
//
// ФОРМАТ ini (`GGameUserSettingsIni`, секция `[AsdaWindowLayout]`), плоские человекочитаемые ключи:
//     Pos.<ОкноЦель>.<КонтролЦель>=<dx>,<dy>     сдвиг цели относительно раскладки .JUI, юниты канваса
//     Size.<Окно>=<ширина>,<высота>              размер окна в БАЗОВЫХ px (только окна с ресайзом)
//     Flag.<Окно>=0|1                            одиночный признак состояния раскладки владельца
// Разделитель полей — запятая (в `%f` её не бывает), разделитель частей ключа — точка (в именах
// контролов .JUI точек нет: там только буквы/цифры/подчёркивание — проверено по gamemain_jui.ini).
const TCHAR* UAsdaWindowSubsystem::kLayoutSection = TEXT("AsdaWindowLayout");
