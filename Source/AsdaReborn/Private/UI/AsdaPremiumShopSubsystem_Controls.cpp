// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumShopSubsystem_Controls.cpp — ★ R2 10.11.2026 (разрез AsdaPremiumShopSubsystem.cpp, дословный перенос): виджет и контролы: SetupControls, полосы/категории, карточки, корзина, пакеты.
//   Общее для частей — в AsdaPremiumShopSubsystemInternal.h; шапка и реверс — в AsdaPremiumShopSubsystem.cpp.
#include "UI/AsdaPremiumShopSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"   // ★ 06.11.2026: звуки окна 6000 (4/5/6)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaNoticeColors.h"   // ★ 02.11.2026: цвет −11514 чата 12919 (гейт `uiCanInteractWithTarget`)
#include "UI/AsdaHairShopSubsystem.h"        // ★ ревизия М-3: сессия парикмахерской (`CObject_GetField166`)
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ ревизия М-3: барабан гачи 10050 (`CObject_GetField254`); ★ 31.10.2026: окно «Вид» 15600
#include "UI/AsdaWarehouseSubsystem.h"      // ★ ревизия М-3: свиток склада (`CObject_GetField5581`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейты 6947 (открытие) и `uiCanInteractWithTarget` (покупка) — окно обмена 2008

#include "AsdaDataTableCache.h"
#include "Login/AsdaButtonRelay.h"
#include "UI/AsdaEquipShopSubsystem.h"      // ★ ОБЩИЕ ТОЧКИ витрины `.she` (закон port-shared-points)
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaPackagePanelSubsystem.h"   // ★ панель состава набора 26500 — уже портирована
#include "UI/AsdaBoosterShopSubsystem.h"   // ★ самотест общий: печатает и магазин бустеров 15300
#include "UI/AsdaPremiumMallSubsystem.h"    // консольные команды дёргают и диспетчер 30000
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка подтверждения 1559
#include "UI/AsdaCashGiftSubsystem.h"       // ★ 01.11.2026 P101_CashGift: подарок 5072/5074 (кнопки 6085 + 9i, панели 6296/6333)
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
// ★ волна В4 ремесла: хвост 5045 при закрытии (восстановление снимка окон)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNpcShopSubsystem.h"        // ★ волна В5: `uiInventoryOpenWindow` закрывает магазин NPC

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "AsdaBytes.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Internationalization/Regex.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ магазин 6000 молча не открывается
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): разбор аргументов самотеста (`quit` не путается с числом)
#include "Text/AsdaLang.h"          // ★ 26.10.2026: самотест имён бустеров — язык сессии (RU/EN)
#include "UObject/UObjectIterator.h" // ★ 26.10.2026: самотест имён бустеров — обход текстов открытых окон
// ★ 26.10.2026: самотест премиум-коробок — подбор ячеек вскрытия (ReservePackageSlots)   ★ R3: комментарий снятого включения "UI/AsdaInventorySubsystem.h" (файл им не пользовался) — сохранён
#include "Misc/FileHelper.h"                // ★ 26.10.2026: самотест премиум-коробок читает Tools/data/premium_box_sets.json
#include "Misc/Paths.h"                     // ★ 26.10.2026: путь спецификации от каталога проекта
#include "Dom/JsonObject.h"                 // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonReader.h"       // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonSerializer.h"   // ★ 26.10.2026: разбор спецификации наборов
// ★ 24.10.2026 (выход из сидения 1:1): `ApplyEtalonSceneStop` — стоп/«встать» при входе в сцену   ★ R3: комментарий снятого включения "World/AsdaPlayerCharacter.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSystemOptionSubsystem.h"      // ★ 25.10.2026 (ревизия sit108, М-3): гейт «персонаж занят» (`objStateAllowsRender`) до стопа сцены
#include "GameFramework/PlayerController.h"    // ★ 24.10.2026: свой павн для `ApplyEtalonSceneStop`
#include "AsdaPremiumShopSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPremiumShopSubsystem*.cpp

namespace {
	const TCHAR* kWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN");

	// ★★★ ПОДПИСИ ЖИВЫХ ПОЛОС — ЧЕТЫРЕ ЦИКЛА `itemBagDlgInit@0xC2C040` (`ida.c:480019-480051`).
	//   Ни одна строка не выдумана: код берёт их подряд от базового loc-id.
	constexpr int32 kCatLocBase  = 10401;   // 58200 ×8: Горячая новинка!, Новинка!, Инструмент,
	                                        //   Снаряжение, Волосы, Питомец, Элитный набор, Лавка мелочей
	constexpr int32 kToolLocBase = 10421;   // 58201 ×5: Общий вид, Улучшение предметов,
	                                        //   Расходные материалы, Средство передвижения, И т.д.
	constexpr int32 kEqLocBase   = 10426;   // 58202 ×3: Воин, Лучник, Маг
	constexpr int32 kSub2LocBase = 10441;   // 58203 ×8: Общий вид, Оружие, Шлем, Доспех, Перчатки,
	                                        //   Штаны, Ботинки, Бустер
	const TCHAR* kBinTablePath = TEXT("/Game/AsdaAssets/Data/NPC/DT_PremiumShopBin.DT_PremiumShopBin");

	// ── Статические подписи окна (`CGameDialog_InitControls@0xC321D0`, §8.4 отчёта) ──
	constexpr int32 kLocCartHdr   = 12906;  // 6029 «Список снаряжения»
	constexpr int32 kLocSumHdr    = 12958;  // 6080 «Итого»
	constexpr int32 kLocReset     = 6950;   // 6037 «Сброс»
	constexpr int32 kLocBuySel    = 6851;   // 6275 «Купить выбранные вещи»
	constexpr int32 kLocCashLbl   = 12904;  // 6025 «Сейчас GXP»
	constexpr int32 kLocCellOk    = 6944;   // подпись `_BUT0` ячейки «OK»
	constexpr int32 kLocPkgNormal = 99302;  // «Обычная цена»
	constexpr int32 kLocPkgSale   = 99303;  // «Цена со скидкой»
	constexpr int32 kLocPkgRate   = 99304;  // «Размер скидки»

	// ═══ ★ СЕГМЕНТ «Волосы» (кат. 4) — СКРЫТ ПО РЕШЕНИЮ ВЛАДЕЛЬЦА 2026-08-28 ═══════════════════
	//   «Должна вообще быть скрыта, т.к. у нас за причёски отвечает отдельный NPC» — вход в
	//   парикмахерскую выведен кнопкой NPC 132 «Парикмахер Юки»
	//   (`UAsdaHairShopSubsystem::OpenWindow`). ЭТО ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА: там сегмент живой
	//   и открывает витрину причёсок (198 записей `Category == 3` в `PremiumShop.BIN`).
	//   ★ Гасится ТОЛЬКО сегмент; нумерация остальных НЕ сдвигается — соответствие
	//     «сегмент → категория» остаётся позиционным, как в эталоне.
	static ASDA_DEV_CVAR(int32, CVarShowHairCategory,
		TEXT("asda.PremiumMall.ShowHairCategory"), 0,
		TEXT("0 = сегмент «Волосы» полосы 58200 скрыт (РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-28: причёски "
		     "продаёт NPC 132); 1 = показать, как в оригинале."),
		ECVF_Default);

	// ═══ ★ СЕГМЕНТ «Новинка!» (кат. 1) — СКРЫТ ПО РЕШЕНИЮ ВЛАДЕЛЬЦА 2026-08-28 («скрой ПОКА ЧТО») ═
	//   Отбор категории — `NewFlag == 2` (`ui_rebuildCategorizedItemList@0xC191F0`, `ida.c:473294`),
	//   и в нашем `PremiumShop.BIN` такого признака нет НИ У ОДНОЙ из 715 записей (у «Горячее»,
	//   `NewFlag == 1`, их 47). То есть страница пуста и в оригинале.
	//   ★ «ПОКА ЧТО» — ключевое слово: данные могут наполниться в любой момент, поэтому `LoadBin`
	//     ПРЕДУПРЕЖДАЕТ в журнал, если скрытая категория оказалась НЕ пустой (см. там же).
	static ASDA_DEV_CVAR(int32, CVarShowNewCategory,
		TEXT("asda.PremiumMall.ShowNewCategory"), 0,
		TEXT("0 = сегмент «Новинка!» полосы 58200 скрыт (в PremiumShop.BIN нет ни одной записи "
		     "с NewFlag==2, страница пуста); 1 = показать."),
		ECVF_Default);

	// ═══ ★ СЕГМЕНТ «Лавка мелочей» (кат. 7) — В ДАННЫХ ПУСТО ══════════════════════════════════
	//   Проверено по `PremiumShop.BIN` (715 записей): категорий там ровно пять —
	//   1 Инструмент 112, 2 Снаряжение 392, 3 Волосы 198, 4 Питомец 9, 5 Элитный набор 4.
	//   Записей `Category == 6` («Лавка мелочей») **НОЛЬ**, поэтому сегмент открывал пустую
	//   страницу и в ОРИГИНАЛЕ тоже. Прячем — это решение владельца, а не правка дефекта.
	static ASDA_DEV_CVAR(int32, CVarShowSundryCategory,
		TEXT("asda.PremiumMall.ShowSundryCategory"), 0,
		TEXT("0 = сегмент «Лавка мелочей» полосы 58200 скрыт (в PremiumShop.BIN нет ни одной "
		     "записи Category==6, страница всегда пуста); 1 = показать."),
		ECVF_Default);

	// ═══ ★★ КОНТЕНТ ПОД РАЗРЕШЕНИЕ ЭКРАНА (просьба владельца 2026-08-28) ═══════════════════════
	//   Эталонная лесенка (`uiRowCountByScreenHeight@0xBD63E0`) зависит ТОЛЬКО от высоты экрана и
	//   даёт 15 при высоте ≤ 1024 и 21 при ≤ 1600 — то есть 5 и 7 РЯДОВ по 98 px. Но окно 6000
	//   в `.JUI` объявлено как `AlignYSize = "32 0 99"` (99 % высоты экрана), а корзина 6024
	//   прибита к его низу — значит фактически доступная под сетку высота ЗАВИСИТ ОТ ЭКРАНА, и на
	//   низком разрешении эталонные 15 карточек не влезают, а на высоком влезло бы больше.
	//   ⇒ Считаем, сколько рядов реально помещается между верхом сетки и верхом корзины, и берём
	//     МИНИМУМ с эталонной лесенкой (расширять сверх эталона не будем без слова владельца).
	//   Ключ возвращает чистую лесенку эталона.
	static ASDA_DEV_CVAR(int32, CVarFitScreen,
		TEXT("asda.PremiumMall.FitScreen"), 1,
		TEXT("1 = число карточек на страницу ограничивается тем, что РЕАЛЬНО влезает между полосами "
		     "и корзиной при текущем разрешении; 0 = чистая лесенка эталона (uiRowCountByScreenHeight)."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarShowItemTabs,
		TEXT("asda.PremiumMall.ShowItemTabs"), 0,
		TEXT("0 = полоса 6002 «Новые вещи | Все вещи» погашена — как её гасит эталон "
		     "(CShellItemStore_InitControls, ida.c:479948) и как просил владелец 2026-08-28; "
		     "1 = показать её (состояние порта до 2026-08-28)."),
		ECVF_Default);

	// ═══ ПОЛОСА КАТЕГОРИЙ 6009 — ★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА ПО РЕШЕНИЮ ВЛАДЕЛЬЦА ═════════════
	//   Эталон гасит и её (`ida.c:479949`, доказательство выше), но владелец 2026-08-28 просил
	//   ОСТАВИТЬ цветные вкладки и поднять к ним содержимое: без них переключать категории нечем
	//   (альтернативная навигация 58200…58940 в оригинале тоже скрыта — см. `DeadNewBars`).
	//   Ключ возвращает эталонное поведение целиком.
	static ASDA_DEV_CVAR(int32, CVarShowCategoryBar,
		TEXT("asda.PremiumMall.ShowCategoryBar"), 0,
		TEXT("1 = показывать цветную полосу категорий 6009 (РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-28; "
		     "оригинал гасит её в Init вместе с 6002); 0 = 1:1 эталон — полосы нет."),
		ECVF_Default);

	// ═══ КОРИЧНЕВАЯ ПОДЛОЖКА ПОД ШАПКОЙ — ★ ОТСТУПЛЕНИЕ ПО РЕШЕНИЮ ВЛАДЕЛЬЦА ═════════════════
	//   Это `ShellItem_BodyBg` (скин `Box_Brown`, **IDNum = −1**, `Origin = "5 39"`,
	//   `Size = "668 29"`) — кода у неё нет вовсе, оригинал рисует её ВСЕГДА.
	//   ★ ОНА ПУСТА ВО ВСЕХ РЕЖИМАХ, и это доказано, а не предположено: её единственные дети —
	//   чекбоксы-фильтры **6004** `SHELLITEM_STORE_CHECK` и **6007** `SHELLITEM_STORE_CHECK01`
	//   (оба `Visible = "0"` в `.JUI`) вместе со своими подписями 6005/6008 (дети чекбоксов,
	//   значит наследуют скрытие). Код их НЕ ПОКАЗЫВАЕТ: все обращения к `m_pCheckBtn`/`m_pCheck01`
	//   (`ida.c:477916`, `477937`, `481508`, `481511`, `488562`, `488563`) — это
	//   `CControl_SetVisibleState`, а она ставит `m_bToggleState` (галочку), а НЕ видимость
	//   (`CControl_SetVisibleState@0x12FE000`). ⚠ Прежний вывод шапки этого файла «эталон ГАСИТ
	//   фильтры в Init» был МИСНЕЙМОМ и здесь снят: эталон лишь СНИМАЕТ ГАЛОЧКУ, а скрыты они ДАННЫМИ.
	static ASDA_DEV_CVAR(int32, CVarShowBodyBg,
		TEXT("asda.PremiumMall.ShowBodyBg"), 0,
		TEXT("0 = коричневая подложка ShellItem_BodyBg под шапкой окна 6000 скрыта "
		     "(РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-28: она пуста во всех режимах); "
		     "1 = как оригинал — рисуется всегда."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarShowNewBars,
		TEXT("asda.PremiumMall.ShowNewBars"), 0,
		TEXT("0 = полосы SHELLITEM_STORE_MENU_NEW1/1_2/1_3/2/3/4 (58200/58204/58940/58201/58202/58203) "
		     "погашены целиком, как велят их Visible=0 в .JUI и эталонный обработчик 0xC4CFB0; "
		     "1 = показать их подписи (состояние порта до 2026-08-28: «Экипировка» наезжала на 6009)."),
		ECVF_Default);

	// К9 — «Деньги в подарок» 40000/40001: эталон привязывает, ставит текст и ТУТ ЖЕ гасит.
	static ASDA_DEV_CVAR(int32, CVarShowPreGift,
		TEXT("asda.PremiumMall.ShowPreGift"), 0,
		TEXT("0 = мёртвая пара «Деньги в подарок» (40000/40001) скрыта, как в оригинале (К9); "
		     "1 = показать (данных для неё сервер не присылает)."),
		ECVF_Default);
}

// ═══════════════════════════════ ВИДЖЕТ ════════════════════════════════════════════════════════
bool UAsdaPremiumShopSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kShopZ).Ensure(GetGameInstance(), Widget, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.UnregisterUiZone(kWindowId);
			WM.CloseWindow(kWindowId);
			Widget = nullptr; bReady = false; bOpen = false;
		},
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			UE_LOG(LogAsdaPremShop, Error, TEXT("[премиум-магазин] WBP_SHELLITEM_STORE_WIN не загрузился (%s)"), kWbpPath);
		},
		[this](UAsdaWindowSubsystem&)
		{
			bOpen = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

// Подпись поверх плана — как у 6800 (`MakeLabelLike`): геометрия берётся у образца, стиль — у донора.
UTextBlock* UAsdaPremiumShopSubsystem::MakeLabelLike(UWidget* Ref, const TCHAR* NewName, const UTextBlock* FontDonor)
{
	if (!Ref || !Widget || !Widget->WidgetTree) { return nullptr; }
	UPanelWidget* Parent = Ref->GetParent();
	const UCanvasPanelSlot* RefSlot = AsdaUi::SlotOf(Ref);
	if (!Parent || !RefSlot) { return nullptr; }
	UTextBlock* Tb = Widget->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(NewName));
	if (!Tb) { return nullptr; }
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Parent->AddChild(Tb)))
	{
		S->SetAnchors(RefSlot->GetAnchors());
		S->SetOffsets(RefSlot->GetOffsets());
		S->SetAlignment(RefSlot->GetAlignment());
		S->SetZOrder(RefSlot->GetZOrder() + 1);
	}
	if (FontDonor)
	{
		Tb->SetFont(FontDonor->GetFont());
		Tb->SetColorAndOpacity(FontDonor->GetColorAndOpacity());
	}
	Tb->SetAutoWrapText(false);          // закон `ui-autowrap-canvas-gotcha`
	Tb->SetJustification(ETextJustify::Center);
	Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	return Tb;
}

void UAsdaPremiumShopSubsystem::SetupControls()
{
	bReady = false;
	Relays.Reset();
	HiddenByCode.Reset();
	PackageTabTree.Reset();
	GridTree.Reset();
	MenuIdleColors.Reset();
	IndexIdleColors.Reset();
	if (!Widget || !Widget->WidgetTree) { return; }

	auto F = [this](const FString& N) -> UWidget* { return AsdaUi::FindNodeByName(Widget, *N, nullptr); };
	auto T = [&F](const FString& N) -> UTextBlock* { return Cast<UTextBlock>(F(N)); };

	BtnClose   = Cast<UButton>(F(TEXT("SHELLITEM_STORE_CLOSE")));       // 6001
	BtnReset   = Cast<UButton>(F(TEXT("SHELLITEM_STORE_BUT00")));       // 6037
	BtnBuySel  = Cast<UButton>(F(TEXT("SHELLITEM_STORE_BUT07")));       // 6275
	BtnCharge  = Cast<UButton>(F(TEXT("SHELLITEM_MAC_CHARGE")));        // 6276
	BtnBalance = Cast<UButton>(F(TEXT("ShellItem_CashCheck_Btn")));     // 6277
	// ★★★ ЖИВАЯ ПАГИНАЦИЯ ОКНА 6000 — ГРУППА «EQUIP» (6011…6021), А НЕ «FUNC» (6278…6288).
	//   ДОКАЗАНО ДАННЫМИ И КОДОМ, а не именем: (1) корень группы FUNC — контрол 6278
	//   `SHELLITEM_FUNC_SLASH` — эталон ГАСИТ в Init (`SetVisible(m_pFuncSlash, 0)`), а видимость
	//   в оригинале НАСЛЕДУЕТСЯ, поэтому вместе с ним пропадают и счётчики 6279/6280 (в `.JUI`
	//   они его ДЕТИ, и импортёр честно свернул всю ветку); (2) пять базовых контролов группы
	//   EQUIP (6011/6012/6013/6014/6016) `CGameDialog_InitControls` ПОКАЗЫВАЕТ, и в ассете вся
	//   ветка `SHELLITEM_EQUIP_SLASH_grp` видима (её родитель — контейнер корзины 6024).
	//   ⚠ Проверено самотестом: со счётчиками из группы FUNC они выходили с нулевой геометрией
	//   и `Collapsed` — ровно тот случай «узел на месте, но НЕ ВИДЕН», о котором предупреждает
	//   свежий урок владельца.
	// ★ ИМЕНА ПАГИНАЦИИ ИНВЕРТИРОВАНЫ (дефект оригинала К15): `END*` стоят СЛЕВА (скин
	//   `Index_Prev` = «в начало»), `FIRST*` — СПРАВА (`Index_Next` = «в конец»). Верим
	//   координатам и скину, а НЕ имени (закон «★ ИМЕНАМ КОНТРОЛОВ НЕ ВЕРЬ»).
	//   ⚠ И ещё миснейм импортёра: `..._NEXT`/`_BACKS`/`_END1`/`_FIRST1` — это ТЕКСТЫ, а кнопки
	//   зовутся `..._NEXTP`/`_BACKP`/`_END2`/`_FIRST2`.
	BtnNext    = Cast<UButton>(F(TEXT("SHELLITEM_EQUIP_NEXTP")));       // 6015
	BtnBack    = Cast<UButton>(F(TEXT("SHELLITEM_EQUIP_BACKP")));       // 6017
	BtnFirst   = Cast<UButton>(F(TEXT("SHELLITEM_EQUIP_END2")));        // 6021 — СЛЕВА, «в начало»
	BtnLast    = Cast<UButton>(F(TEXT("SHELLITEM_EQUIP_FIRST2")));      // 6019 — СПРАВА, «в конец»
	TxtPage    = T(TEXT("SHELLITEM_EQUIP_NUM00"));                      // 6012
	TxtPages   = T(TEXT("SHELLITEM_EQUIP_NUM01"));                      // 6013
	TxtCash    = T(TEXT("SHELLITEM_STORE_TXT09"));                      // 6026
	TxtCashLbl = T(TEXT("SHELLITEM_STORE_TXT_11"));                     // 6025
	TxtSum     = T(TEXT("SHELLITEM_STORE_SUM_VAL"));                    // 6081
	TxtSumHdr  = T(TEXT("SHELLITEM_STORE_SUM_Text"));                   // 6080
	TxtCartHdr = T(TEXT("SHELLITEM_STORE_LIST_TITLE_Text"));            // 6029
	CapReset   = T(TEXT("SHELLITEM_STORE_BUT00_Text"));
	CapBuySel  = T(TEXT("SHELLITEM_STORE_BUT07_Text"));

	auto SetLoc = [this](UTextBlock* Tb, int32 Id)
	{
		if (!Tb) { return; }
		Tb->SetAutoWrapText(false);
		const FString S = Loc(Id);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtCartHdr, kLocCartHdr);
	SetLoc(TxtSumHdr,  kLocSumHdr);
	SetLoc(CapReset,   kLocReset);
	SetLoc(CapBuySel,  kLocBuySel);
	SetLoc(TxtCashLbl, kLocCashLbl);

	// ── Полосы вкладок ──
	for (int32 i = 0; i < kMenuTabs; ++i)
	{
		MenuButtons[i] = Cast<UButton>(F(FString::Printf(TEXT("SHELLITEM_STORE_MENU_OPT%d"), i)));
		MenuLabels[i]  = T(i == 0 ? FString(TEXT("SHELLITEM_STORE_MENU_Text"))
		                          : FString::Printf(TEXT("SHELLITEM_STORE_MENU_Text%d"), i + 1));
		if (MenuLabels[i]) { MenuLabels[i]->SetAutoWrapText(false); SetLoc(MenuLabels[i].Get(), kTabLoc[i]); }
	}
	for (int32 i = 0; i < kIndexTabs; ++i)
	{
		IndexButtons[i] = Cast<UButton>(F(FString::Printf(TEXT("SHELLITEM_STORE_INDEX_OPT%d"), i)));
		IndexLabels[i]  = T(i == 0 ? FString(TEXT("SHELLITEM_STORE_INDEX_Text"))
		                           : FString::Printf(TEXT("SHELLITEM_STORE_INDEX_Text%d"), i + 1));
		if (IndexLabels[i]) { IndexLabels[i]->SetAutoWrapText(false); }
		if (kIndexLoc[i] > 0) { SetLoc(IndexLabels[i].Get(), kIndexLoc[i]); }
	}
	// ★ К8: третий сегмент 6002 кодом не заполняется — в оригинале там остаётся корейская
	//   заглушка скина. Гасим и кнопку, и подпись.
	if (CVarHideIndexSeg3.GetValueOnGameThread() != 0)
	{
		if (IndexButtons[2]) { IndexButtons[2]->SetVisibility(ESlateVisibility::Collapsed); HiddenByCode.Add(IndexButtons[2].Get()); }
		if (IndexLabels[2])  { IndexLabels[2]->SetVisibility(ESlateVisibility::Collapsed);  HiddenByCode.Add(IndexLabels[2].Get()); }
	}

	// ── Что эталон ГАСИТ в Init (`CShellItemStore_InitControls@0xC28820`) ──
	//   фильтры 6004/6005/6007/6008, кнопки 6028/6036/6274, рамка выделения 6273, слэш 6278,
	//   мёртвая пара «подарочный GXP» 40000/40001 (К9).
	{
		TArray<FString> ToHide = {
			TEXT("SHELLITEM_STORE_CHECK"), TEXT("SHELLITEM_STORE_CHECK_grp"),
			TEXT("SHELLITEM_STORE_TXT00"), TEXT("SHELLITEM_STORE_TXT00_14_HIT"),
			TEXT("SHELLITEM_STORE_CHECK01"), TEXT("SHELLITEM_STORE_CHECK01_grp"),
			TEXT("SHELLITEM_STORE_TXT11"), TEXT("SHELLITEM_STORE_TXT11_16_HIT"),
			TEXT("SHELLITEM_STORE_BUT04"), TEXT("SHELLITEM_STORE_BUT04_Text"),
			TEXT("SHELLITEM_STORE_BUT05"), TEXT("SHELLITEM_STORE_BUT05_Text"),
			TEXT("SHELLITEM_STORE_BUT06"), TEXT("SHELLITEM_STORE_BUT06_Text"),
			TEXT("SHELLITEM_FUNC_SLASH_grp"),
		};
		if (CVarShowPreGift.GetValueOnGameThread() == 0)
		{
			ToHide.Add(TEXT("ShellItem_PreCashTxt_grp"));
			ToHide.Add(TEXT("ShellItem_PreCash_grp"));
		}
		for (const FString& N : ToHide)
		{
			if (UWidget* W = F(N)) { W->SetVisibility(ESlateVisibility::Collapsed); HiddenByCode.Add(W); }
		}
	}

	SetupCards();
	SetupSelFrame();   // ★ 08.11.2026 (пачка 16, баг 6): рамка выбора 6273
	SetupCart();
	SetupPackageCards();
	SetupGiftPanels();   // ★ 01.11.2026 P101_CashGift: панели 6296/6333 (режим подарка)
	SetupBars();          // ★ четыре ЖИВЫЕ полосы 58200…58203

	// ── Поддерево вкладки наборов 58300 и поддерево сетки 6083 — целиком ──
	auto CollectTree = [](UWidget* Root, TArray<TObjectPtr<UWidget>>& Out)
	{
		if (!Root) { return; }
		Out.Add(Root);
		TArray<UWidget*> Stack; Stack.Add(Root);
		while (Stack.Num() > 0)
		{
			UWidget* Cur = Stack.Pop(EAllowShrinking::No);
			if (UPanelWidget* Pan = Cast<UPanelWidget>(Cur))
			{
				for (int32 i = 0; i < Pan->GetChildrenCount(); ++i)
				{
					if (UWidget* Kid = Pan->GetChildAt(i)) { Out.Add(Kid); Stack.Add(Kid); }
				}
			}
		}
	};
	CollectTree(F(TEXT("SHELLITEM_STORE_Packageshop_grp")), PackageTabTree);
	CollectTree(F(TEXT("SHELLITEM_ITEMLIST_grp")), GridTree);

	// ── ★★★ ДЕВЯТЬ ПЛАНОВ БЕЛОЙ РАМКИ ВИТРИНЫ 6083 (скин `Win_Tab`, `ResizeWindow` 3×3) ──
	//   Имя плана — `{idc}_{juiIdx}_{plane}`, индекс `juiIdx` меняется при переимпорте, поэтому
	//   ищем регулярным выражением (закон `jui-importer-widget-naming`).
	{
		PanelPlanes.Reset();
		PanelPlanes.SetNum(9);
		TArray<UWidget*> All;
		Widget->WidgetTree->GetAllWidgets(All);
		const FRegexPattern Pat(TEXT("^SHELLITEM_ITEMLIST_(\\d+)_([0-8])$"));
		int32 Got = 0;
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			FRegexMatcher M(Pat, W->GetName());
			if (!M.FindNext()) { continue; }
			const int32 Plane = FCString::Atoi(*M.GetCaptureGroup(2));
			if (PanelPlanes.IsValidIndex(Plane) && !PanelPlanes[Plane]) { PanelPlanes[Plane] = W; ++Got; }
		}
		if (Got != 9)
		{
			UE_LOG(LogAsdaPremShop, Warning,
				TEXT("[премиум-магазин] рамка витрины 6083: найдено %d планов из 9 — высота подгоняться не будет"), Got);
			PanelPlanes.Reset();
		}
		PanelHeightApplied = 0;
	}

	// ── ★★★ ШЕСТЬ ПОЛОС `SHELLITEM_STORE_MENU_NEW*`: ГАСИМ ЦЕЛИКОМ ──
	//   Разбор и доказательства — в .h у `DeadNewBars`. Гасим по ПРЕФИКСУ имени контрола, чтобы
	//   не осталось ни планов, ни кнопок, ни подписей (`*_Text`, `*_Text2` … лежат в КОРНЕ окна,
	//   поэтому «свернуть группу» их не убирает — закон `jui-uneven-node-layout`).
	{
		DeadNewBars.Reset();
		// ★★★ ИСПРАВЛЕНО 2026-08-28 (владелец сверил с оригиналом): гасить здесь надо ТОЛЬКО ДВЕ
		//   полосы — 58204 `NEW1_2` и 58940 `NEW1_3`. Их `itemBagDlgInit@0xC2C040` НЕ ПРИВЯЗЫВАЕТ
		//   вовсе (он знает только 58200…58203, `ida.c:480014-480018`), и во всём бинаре ссылок на
		//   них нет — это мёртвые заготовки. Остальные четыре — ЖИВАЯ навигация витрины, ими
		//   управляет `SetupBars` / `RefreshBarVisibility`.
		static const TCHAR* kNewBarPrefixes[] = {
			TEXT("SHELLITEM_STORE_MENU_NEW1_2"),   // 58204 — мёртвая заготовка
			TEXT("SHELLITEM_STORE_MENU_NEW1_3"),   // 58940 — мёртвая заготовка («Экипировка»)
		};
		TArray<UWidget*> All;
		Widget->WidgetTree->GetAllWidgets(All);
		const bool bShow = (CVarShowNewBars.GetValueOnGameThread() != 0);
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			for (const TCHAR* Pfx : kNewBarPrefixes)
			{
				if (!N.StartsWith(Pfx)) { continue; }
				DeadNewBars.AddUnique(W);
				break;
			}
		}
		for (const TObjectPtr<UWidget>& W : DeadNewBars)
		{
			AsdaUi::SetShownDecor(W, bShow);
		}
		UE_LOG(LogAsdaPremShop, Log,
			TEXT("[премиум-магазин] полосы NEW1/1_2/1_3/2/3/4 (58200/58204/58940/58201/58202/58203): "
			     "%d узлов %s (в .JUI у всех Visible=0)"),
			DeadNewBars.Num(), bShow ? TEXT("ПОКАЗАНЫ ключом") : TEXT("погашены"));
	}
	// ── ★★★ ПОЛОСА 6002, ПОЛОСА 6009 И КОРИЧНЕВАЯ ПОДЛОЖКА (жалобы владельца 2026-08-28) ──
	//   Доказательства и ключи — у соответствующих `CVar*` в начале файла. Гасим ПО ПРЕФИКСУ
	//   имени контрола: подписи полос (`*_Text`, `*_Text2`…) лежат в КОРНЕВОМ канвасе окна, и
	//   сворачивание группы их не убирает (закон `jui-uneven-node-layout`).
	{
		struct FBarRule { const TCHAR* Prefix; bool bShow; const TCHAR* Tag; };
		const FBarRule Rules[] = {
			// ★★★ ОБЕ СТАРЫЕ ПОЛОСЫ ЭТАЛОН ГАСИТ В `CShellItemStore_InitControls@0xC28820`
			//   (`ida.c:479948-479949`) и больше НИКОГДА не показывает — живая навигация это
			//   `SHELLITEM_STORE_MENU_NEW1…4` (58200…58203), см. `SetupBars`.
			{ TEXT("SHELLITEM_STORE_INDEX"), CVarShowItemTabs.GetValueOnGameThread()    != 0, TEXT("6002 «Новые вещи | Все вещи» (мёртвая)") },
			{ TEXT("SHELLITEM_STORE_MENU_"), CVarShowCategoryBar.GetValueOnGameThread() != 0, TEXT("6009 старая полоса категорий (мёртвая)") },
			{ TEXT("ShellItem_BodyBg"),      CVarShowBodyBg.GetValueOnGameThread()      != 0, TEXT("коричневая подложка") },
		};
		TArray<UWidget*> All;
		Widget->WidgetTree->GetAllWidgets(All);
		for (const FBarRule& R : Rules)
		{
			int32 N = 0;
			for (UWidget* W : All)
			{
				if (!W) { continue; }
				const FString Nm = W->GetName();
				if (!Nm.StartsWith(R.Prefix)) { continue; }
				// ⚠ `SHELLITEM_STORE_MENU_NEW*` уже погашены своим правилом — под 6009 они не идут.
				if (Nm.StartsWith(TEXT("SHELLITEM_STORE_MENU_NEW"))) { continue; }
				++N;
				if (R.bShow) { continue; }
				W->SetVisibility(ESlateVisibility::Collapsed);
				HiddenByCode.Add(W);
			}
			UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] %s: %d узлов %s"),
				R.Tag, N, R.bShow ? TEXT("оставлены видимыми") : TEXT("погашены"));
		}
	}

	if (PackageTabTree.Num() == 0)
	{
		UE_LOG(LogAsdaPremShop, Warning,
			TEXT("[премиум-магазин] узла SHELLITEM_STORE_Packageshop_grp (58300) нет — витрина наборов не покажется"));
	}

	// ── Кнопки окна ──
	auto Bind = [this](UButton* B, int32 Idx, TFunction<void(int32)> Cb)
	{
		if (!B) { return; }
		AsdaUi::BindButton(this, B, Idx, MoveTemp(Cb), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnClose,   6001, [this](int32) { CloseWindow(); });
	Bind(BtnReset,   6037, [this](int32) { OnResetClicked(); });
	Bind(BtnBuySel,  6275, [this](int32) { OnBuySelectedClicked(); });
	Bind(BtnCharge,  6276, [this](int32) { OnChargeClicked(); });
	Bind(BtnBalance, 6277, [this](int32) { OnBalanceClicked(); });
	// ★ 06.11.2026 [ОРИГИНАЛ]: `uiQuantityStepperHandleCmd@0xC404F0` — звук 4 на всех четырёх кнопках страниц (ui_core.c:28818-28856).
	Bind(BtnNext,    6015, [this](int32) { AsdaPremiumUiSound(GetGameInstance(), 4); SetPage(Page + 1); });
	Bind(BtnBack,    6017, [this](int32) { AsdaPremiumUiSound(GetGameInstance(), 4); SetPage(Page - 1); });
	Bind(BtnFirst,   6021, [this](int32) { AsdaPremiumUiSound(GetGameInstance(), 4); SetPage(0); });
	Bind(BtnLast,    6019, [this](int32) { AsdaPremiumUiSound(GetGameInstance(), 4); SetPage(MaxPage); });
	for (int32 i = 0; i < kMenuTabs; ++i)  { Bind(MenuButtons[i].Get(),  6009, [this, i](int32) { SetMenuTab(i); }); }
	for (int32 i = 0; i < kIndexTabs; ++i) { Bind(IndexButtons[i].Get(), 6002, [this, i](int32) { SetIndexTab(i); }); }

	bReady = (BtnClose != nullptr) || (Cards[0].ItemIcon != nullptr);
	UE_LOG(LogAsdaPremShop, Log, TEXT("%s"), *DumpControls());
}

// ═══════════════ ★★★ ЖИВАЯ НАВИГАЦИЯ ВИТРИНЫ: ЧЕТЫРЕ ПОЛОСЫ 58200…58203 ═══════════════════════
//   Порт `itemBagDlgInit@0xC2C040` (`ida.c:480014-480051`): привязка, показ 58200 и четыре цикла
//   подписей. Порядок сегментов и их loc-id — ровно как там, ничего не подобрано.
void UAsdaPremiumShopSubsystem::SetupBars()
{
	CatButtons.SetNum(kCatTabs);   CatLabels.SetNum(kCatTabs);
	ToolButtons.SetNum(kToolTabs); ToolLabels.SetNum(kToolTabs);
	EqButtons.SetNum(kEquipTabs);  EqLabels.SetNum(kEquipTabs);
	Sub2Buttons.SetNum(kSub2Tabs); Sub2Labels.SetNum(kSub2Tabs);
	BarCatTree.Reset(); BarToolTree.Reset(); BarEqTree.Reset(); BarSub2Tree.Reset();
	CatIdle.Reset(); ToolIdle.Reset(); EqIdle.Reset(); Sub2Idle.Reset();

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	// ⚠ Префикс `SHELLITEM_STORE_MENU_NEW1` ловит и мёртвые `NEW1_2` / `NEW1_3` — отсекаем явно
	//   (закон «★ ИМЕНАМ КОНТРОЛОВ НЕ ВЕРЬ»: у планов имена вида `{idc}_{juiIdx}_{plane}`, и
	//   остаток «2_877_0» у NEW1_2 неотличим от «456_0» у NEW1 по одному лишь виду).
	auto Collect = [&All](const FString& Base, TArray<TObjectPtr<UWidget>>& Out)
	{
		Out.Reset();
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			if (N != Base && !N.StartsWith(Base + TEXT("_"))) { continue; }
			if (N.StartsWith(TEXT("SHELLITEM_STORE_MENU_NEW1_2")) ||
			    N.StartsWith(TEXT("SHELLITEM_STORE_MENU_NEW1_3"))) { continue; }
			Out.Add(W);
		}
	};
	auto BindBar = [this](const FString& Base, int32 Count, int32 LocBase,
		TArray<TObjectPtr<UButton>>& Btns, TArray<TObjectPtr<UTextBlock>>& Labs) -> int32
	{
		int32 Found = 0;
		for (int32 i = 0; i < Count; ++i)
		{
			const FString BtnName = FString::Printf(TEXT("%s_OPT%d"), *Base, i);
			// Подписи общий шаг вкладок именует `_Text`, `_Text2`, `_Text3`, … (`asda_ui_tabbar_captions`).
			const FString TxtName = (i == 0) ? (Base + TEXT("_Text"))
			                                 : FString::Printf(TEXT("%s_Text%d"), *Base, i + 1);
			Btns[i] = Cast<UButton>(AsdaUi::FindNodeExact(Widget, *BtnName));
			Labs[i] = Cast<UTextBlock>(AsdaUi::FindNodeExact(Widget, *TxtName));
			if (Btns[i]) { ++Found; }
			if (Labs[i])
			{
				Labs[i]->SetAutoWrapText(false);            // закон `ui-autowrap-canvas-gotcha`
				const FString Cap = Loc(LocBase + i);
				if (!Cap.IsEmpty()) { Labs[i]->SetText(FText::FromString(Cap)); }
			}
		}
		return Found;
	};

	const FString B1(TEXT("SHELLITEM_STORE_MENU_NEW1"));
	const FString B2(TEXT("SHELLITEM_STORE_MENU_NEW2"));
	const FString B3(TEXT("SHELLITEM_STORE_MENU_NEW3"));
	const FString B4(TEXT("SHELLITEM_STORE_MENU_NEW4"));
	Collect(B1, BarCatTree); Collect(B2, BarToolTree); Collect(B3, BarEqTree); Collect(B4, BarSub2Tree);
	const int32 F1 = BindBar(B1, kCatTabs,   kCatLocBase,  CatButtons,  CatLabels);
	const int32 F2 = BindBar(B2, kToolTabs,  kToolLocBase, ToolButtons, ToolLabels);
	const int32 F3 = BindBar(B3, kEquipTabs, kEqLocBase,   EqButtons,   EqLabels);
	const int32 F4 = BindBar(B4, kSub2Tabs,  kSub2LocBase, Sub2Buttons, Sub2Labels);

	auto Hook = [this](TArray<TObjectPtr<UButton>>& Btns, TFunction<void(int32)> Cb)
	{
		for (int32 i = 0; i < Btns.Num(); ++i)
		{
			if (!Btns[i]) { continue; }
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = i; R->Callback = Cb;
			Btns[i]->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(Relays, R, Btns[i]);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		}
	};
	Hook(CatButtons,  [this](int32 i) { SetCategory(i); });
	Hook(ToolButtons, [this](int32 i) { SetToolSub(i); });
	Hook(EqButtons,   [this](int32 i) { SetEquipSub(i); });
	Hook(Sub2Buttons, [this](int32 i) { SetEquipSub2(i); });

	// ★ Узлы КАЖДОГО сегмента полосы категорий по отдельности — для уплотнения (`CompactCatBar`).
	//   План сегмента импортёр называет `{idc}_{juiIdx}_{seg}`; кнопка `_OPT{seg}`; подпись
	//   `_Text` / `_Text{seg+1}` (закон `jui-importer-widget-naming`).
	CatSegNodes.Reset();
	CatSegNodes.SetNum(kCatTabs);
	for (int32 Seg = 0; Seg < kCatTabs; ++Seg)
	{
		FAsdaPremiumBarSeg& S2 = CatSegNodes[Seg];
		if (CatButtons.IsValidIndex(Seg) && CatButtons[Seg]) { S2.Nodes.Add(CatButtons[Seg].Get()); }
		if (CatLabels.IsValidIndex(Seg)  && CatLabels[Seg])  { S2.Nodes.Add(CatLabels[Seg].Get()); }
		const FRegexPattern Pat(FString::Printf(TEXT("^SHELLITEM_STORE_MENU_NEW1_\\d+_%d$"), Seg));
		for (const TObjectPtr<UWidget>& W : BarCatTree)
		{
			if (!W) { continue; }
			FRegexMatcher M(Pat, W->GetName());
			if (M.FindNext()) { S2.Nodes.AddUnique(W.Get()); }
		}
	}

	// Узлы, которые двигает схлопывание: сетка 6083 и вкладка наборов 58300 (обе на `Origin "4 149"`).
	ShiftTargets.Reset();
	if (UWidget* W = AsdaUi::FindNodeExact(Widget, TEXT("SHELLITEM_ITEMLIST_grp"))) { ShiftTargets.Add(W); }
	if (UWidget* W = AsdaUi::FindNodeExact(Widget, TEXT("SHELLITEM_STORE_Packageshop_grp"))) { ShiftTargets.Add(W); }

	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] полосы: 58200 %d/%d (узлов %d) · 58201 %d/%d (%d) · 58202 %d/%d (%d) · 58203 %d/%d (%d)"),
		F1, kCatTabs, BarCatTree.Num(), F2, kToolTabs, BarToolTree.Num(),
		F3, kEquipTabs, BarEqTree.Num(), F4, kSub2Tabs, BarSub2Tree.Num());
}

// Порт `hairShop_UpdatePanelVisibility@0xC4CFB0` (`ida.c:490816-490850`) — ★ МИСНЕЙМ M5: к
//   парикмахерской функция отношения не имеет, это переключение ПОД-ПОЛОС витрины 6000.
//   Эталон (a2 = idnum поднявшего событие 0x10502 контрола, a3 = индекс сегмента):
//     любое событие 58200…58299 сперва ГАСИТ 58201, 58202, 58203;
//     a2 == 58200 && a3 == 2 → показать 58201;   a2 == 58200 && a3 == 3 → показать 58202;
//     a2 == 58201            → показать 58201;
//     a2 == 58202            → показать 58202 И 58203;   a2 == 58203 → показать 58202 И 58203.
//   ⇒ у категорий 0/1/4/5/6/7 под-полосы НЕТ ВОВСЕ — ровно то, что владелец видит в оригинале.
void UAsdaPremiumShopSubsystem::RefreshBarVisibility()
{
	const bool bTool = (CatTab == 2);                 // «Инструмент» → 58201
	const bool bEq   = (CatTab == 3);                 // «Снаряжение» → 58202
	const bool bSub2 = bEq && bEqSub2Shown;           // третий уровень — только после выбора класса

	auto Show = [](const TArray<TObjectPtr<UWidget>>& Tree, bool bOn)
	{
		const ESlateVisibility V = bOn ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
		for (const TObjectPtr<UWidget>& W : Tree) { if (W) { W->SetVisibility(V); } }
	};
	// 58200 эталон показывает ОДИН РАЗ в `itemBagDlgInit` и больше не трогает.
	Show(BarCatTree, true);
	Show(BarToolTree, bTool);
	Show(BarEqTree,   bEq);
	Show(BarSub2Tree, bSub2);
	// Кнопки обязаны ЛОВИТЬ мышь (закон `selfhittest-vs-hittest-invisible`).
	auto Live = [](const TArray<TObjectPtr<UButton>>& B, bool bOn)
	{
		for (const TObjectPtr<UButton>& W : B)
		{
			AsdaUi::SetShownHit(W, bOn);
		}
	};
	Live(CatButtons, true); Live(ToolButtons, bTool); Live(EqButtons, bEq); Live(Sub2Buttons, bSub2);

	// ★ ДВА СЕГМЕНТА ПОЛОСЫ 58200 СКРЫТЫ (обоснование и ключи — у `CVarShowHairCategory` /
	//   `CVarShowSundryCategory`). Гасим ТОЛЬКО их кнопку и подпись: соседи не двигаются, и
	//   соответствие «индекс сегмента → категория» остаётся эталонным.
	for (int32 Seg = 0; Seg < kCatTabs; ++Seg)
	{
		if (!IsCatHidden(Seg)) { continue; }
		for (const TObjectPtr<UWidget>& W : CatSegNodes[Seg].Nodes)
		{
			if (W) { W->SetVisibility(ESlateVisibility::Collapsed); }
		}
	}
	CompactCatBar();
	ApplyContentShift();
}

void UAsdaPremiumShopSubsystem::RefreshBarState()
{
	AsdaUi::ApplyTabCaptionState(CatLabels,  CatButtons,  CatTab,  CatIdle);
	AsdaUi::ApplyTabCaptionState(ToolLabels, ToolButtons, ToolSub, ToolIdle);
	AsdaUi::ApplyTabCaptionState(EqLabels,   EqButtons,   EqSub,   EqIdle);
	AsdaUi::ApplyTabCaptionState(Sub2Labels, Sub2Buttons, EqSub2,  Sub2Idle);
}

// Скрыта ли категория ключом владельца. ★ Три ключа, ни одного «если код N» в логике витрины:
//   ассортимент и наполнение категорий берутся ИЗ ДАННЫХ (`PremiumShop.BIN`), ключи лишь прячут
//   сегмент полосы. Наполнится таблица — категория оживёт без правки кода (см. предупреждение
//   в `LoadBin`).
bool UAsdaPremiumShopSubsystem::IsCatHidden(int32 Cat) const
{
	switch (Cat)
	{
	case 1: return CVarShowNewCategory.GetValueOnGameThread() == 0;      // «Новинка!»
	case 4: return CVarShowHairCategory.GetValueOnGameThread() == 0;     // «Волосы»
	case 7: return CVarShowSundryCategory.GetValueOnGameThread() == 0;   // «Лавка мелочей»
	default: return false;
	}
}

// ★★★ УПЛОТНЕНИЕ ПОЛОСЫ КАТЕГОРИЙ (жалоба владельца 2026-08-28: «кнопки должны автоматически
//   сдвигаться, если между ними пустота»).
//   КАК: идём по сегментам слева направо; видимый встаёт вплотную к предыдущему видимому, скрытый
//   пропускается. Ширину берём ФАКТИЧЕСКУЮ (у сегментов она разная — подписи разной длины:
//   62/62/100/79/79/98/106/83 по Rect'ам скина `BoxBar_PremiumMall_1`), а не шагом.
//   ⚠ Сдвиг считается ОТ ИСХОДНОГО смещения (`AsdaUi::ShiftSlotXFromBase`) — иначе повторный
//     вызов (а он бывает при каждом переключении категории) сложил бы сдвиги дважды.
//   ★ СООТВЕТСТВИЕ «СЕГМЕНТ → КАТЕГОРИЯ» НЕ МЕНЯЕТСЯ: двигаются УЗЛЫ сегмента `i`, а его кнопка
//     как звала `SetCategory(i)`, так и зовёт. Проверено путём игрока.
void UAsdaPremiumShopSubsystem::CompactCatBar()
{
	if (CatSegNodes.Num() != kCatTabs) { return; }
	float Cursor = FLT_MAX;
	FString Diag;
	for (int32 Seg = 0; Seg < kCatTabs; ++Seg)
	{
		if (IsCatHidden(Seg)) { continue; }
		UWidget* Btn = CatButtons.IsValidIndex(Seg) ? CatButtons[Seg].Get() : nullptr;
		FMargin Base;
		if (!AsdaUi::BaseOffsetsOf(BaseOffsets, Btn, Base)) { continue; }
		if (Cursor == FLT_MAX) { Cursor = Base.Left; }        // первый видимый остаётся на месте
		const float dX = Base.Left - Cursor;
		for (const TObjectPtr<UWidget>& W : CatSegNodes[Seg].Nodes)
		{
			AsdaUi::ShiftSlotXFromBase(BaseOffsets, W.Get(), dX);
		}
		Diag += FString::Printf(TEXT(" %d:%.0f→%.0f(ш%.0f)"), Seg, Base.Left, Base.Left - dX, Base.Right);
		Cursor += Base.Right;                                  // POINT-анкор: Right = ШИРИНА
	}
	UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] уплотнение полосы 58200:%s"), *Diag);
}

// ★★★ СХЛОПЫВАНИЕ ПУСТОЙ ОБЛАСТИ ПОД ПОЛОСАМИ (просьба владельца «как со складом»).
//   Приём тот же, что у `UAsdaWarehouseSubsystem::ApplyTabAreaCollapse` (дефект D19), и та же
//   ЛОВУШКА: сдвиг считаем ОТ ИСХОДНЫХ смещений — общая точка `AsdaUi::ShiftSlotYFromBase`,
//   иначе повторный вызов даёт нулевой сдвиг и окно перестаёт перестраиваться.
//   Числа — из `gamemain_jui.ini`, ничего не подобрано:
//       58200 `Origin "5 71"`   Size 669×23 → низ  94
//       58201 `Origin "5 97"` / 58202 `Origin "146 97"`  Size ×23 → низ 120
//       58203 `Origin "146 123"` Size 462×23 → низ 146
//       сетка 6083 `Origin "4 149"` ⇒ зазор «низ последней полосы → верх сетки» = 149 − 146 = 3.
//   ⇒ верх сетки = (низ последней ВИДИМОЙ полосы) + 3; подъём = 149 − этот верх.
//   Плюс, если скрыта коричневая подложка `ShellItem_BodyBg` (39…68), блок полос и сетка
//   поднимаются ещё на её вертикаль 29 px.
void UAsdaPremiumShopSubsystem::ApplyContentShift()
{
	if (CVarLegacyPanelHeight.GetValueOnGameThread() != 0) { return; }

	constexpr float kBarCatBottom  = 94.f;
	constexpr float kBarSubBottom  = 120.f;
	constexpr float kBarSub2Bottom = 146.f;
	constexpr float kGridTop       = 149.f;
	constexpr float kGap           = kGridTop - kBarSub2Bottom;   // 3
	constexpr float kBodyBgHeight  = 29.f;                        // подложка 39…68

	const bool bTool = (CatTab == 2);
	const bool bEq   = (CatTab == 3);
	const bool bSub2 = bEq && bEqSub2Shown;
	const float LastBottom = bSub2 ? kBarSub2Bottom : ((bTool || bEq) ? kBarSubBottom : kBarCatBottom);
	const float BarShift  = (CVarShowBodyBg.GetValueOnGameThread() == 0) ? kBodyBgHeight : 0.f;
	const float GridShift = BarShift + (kGridTop - (LastBottom + kGap));

	for (const TObjectPtr<UWidget>& W : BarCatTree)   { AsdaUi::ShiftSlotYFromBase(BaseOffsets, W.Get(), BarShift); }
	for (const TObjectPtr<UWidget>& W : BarToolTree)  { AsdaUi::ShiftSlotYFromBase(BaseOffsets, W.Get(), BarShift); }
	for (const TObjectPtr<UWidget>& W : BarEqTree)    { AsdaUi::ShiftSlotYFromBase(BaseOffsets, W.Get(), BarShift); }
	for (const TObjectPtr<UWidget>& W : BarSub2Tree)  { AsdaUi::ShiftSlotYFromBase(BaseOffsets, W.Get(), BarShift); }
	for (const TObjectPtr<UWidget>& W : ShiftTargets) { AsdaUi::ShiftSlotYFromBase(BaseOffsets, W.Get(), GridShift); }

	if (!FMath::IsNearlyEqual(ShiftApplied, GridShift))
	{
		UE_LOG(LogAsdaPremShop, Log,
			TEXT("[премиум-магазин] схлопывание: категория %d (инструмент=%d снаряжение=%d уровень3=%d) ")
			TEXT("низ полос %.0f, полосы вверх на %.0f px, сетка вверх на %.0f px"),
			CatTab, bTool ? 1 : 0, bEq ? 1 : 0, bSub2 ? 1 : 0, LastBottom, BarShift, GridShift);
		ShiftApplied = GridShift;
	}
}

// ═══════════════ ДАННЫЕ ЖИВОЙ НАВИГАЦИИ: `PremiumShop.BIN` через DataTable ════════════════════
bool UAsdaPremiumShopSubsystem::LoadBin()
{
	if (bBinLoaded) { return BinRows.Num() > 0; }
	bBinLoaded = true;
	UDataTable* DT = LoadObject<UDataTable>(nullptr, kBinTablePath);
	if (!DT)
	{
		UE_LOG(LogAsdaPremShop, Error,
			TEXT("[премиум-магазин] нет таблицы %s — живая навигация 58200 работать не будет. ")
			TEXT("Импорт: Tools/asda_import_premium_shop_bin.py"), kBinTablePath);
		return false;
	}
	for (const TPair<FName, uint8*>& It : DT->GetRowMap())
	{
		if (const FAsdaPremiumShopBinRow* R = reinterpret_cast<const FAsdaPremiumShopBinRow*>(It.Value))
		{
			BinRows.Add(*R);
			if (R->Category != 3 && R->CardQty > 1) { CardQtyByCode.Add(R->ItemId, R->CardQty); }
			if (R->Category != 3 && R->GiftCode > 0) { GiftCodeByCode.Add(R->ItemId, R->GiftCode); }
		}
	}
	// ★ Порядок эталона: записи вставляются в отсортированный контейнер по ключу
	//   `1000*Category + 100*Sub1 (если >0) + Sub2 (если >0)` (`ida.c:473236-473241`).
	//   При равных ключах порядок файловый ⇒ сортировка СТАБИЛЬНАЯ.
	BinRows.StableSort([](const FAsdaPremiumShopBinRow& A, const FAsdaPremiumShopBinRow& B)
	{
		auto Key = [](const FAsdaPremiumShopBinRow& R)
		{
			int32 K = 1000 * R.Category;
			if (R.Sub1 > 0) { K += 100 * R.Sub1; }
			if (R.Sub2 > 0) { K += R.Sub2; }
			return K;
		};
		return Key(A) < Key(B);
	});
	// ★ ДИАГНОСТИКА ПУСТЫХ КАТЕГОРИЙ: «страница пустая» должна быть видна в журнале, а не только
	//   глазами владельца. Считаем ровно теми же критериями, что и `BuildActiveCodes`.
	int32 ByCat[kCatTabs] = { 0 };
	for (const FAsdaPremiumShopBinRow& R : BinRows)
	{
		if (R.NewFlag == 1) { ++ByCat[0]; }
		if (R.NewFlag == 2) { ++ByCat[1]; }
		for (int32 C = 2; C < kCatTabs; ++C)
		{
			if (kCatToField[C] == R.Category) { ++ByCat[C]; }
		}
	}
	FString Line;
	for (int32 C = 0; C < kCatTabs; ++C)
	{
		Line += FString::Printf(TEXT(" %d«%s»=%d"), C, *Loc(kCatLocBase + C), ByCat[C]);
		if (ByCat[C] == 0)
		{
			UE_LOG(LogAsdaPremShop, Warning,
				TEXT("[премиум-магазин] категория %d («%s») ПУСТА в PremiumShop.BIN — её страница "
				     "будет без товаров"), C, *Loc(kCatLocBase + C));
		}
		// ★★★ «СКРОЙ ПОКА ЧТО» НЕ ДОЛЖНО СТАТЬ «СКРЫТО НАВСЕГДА» (просьба владельца 2026-08-28):
		//   если скрытая категория НАПОЛНИЛАСЬ, это обязано попасть в журнал, иначе мы годами
		//   будем прятать живую витрину.
		const bool bHidden =
			(C == 1 && CVarShowNewCategory.GetValueOnGameThread() == 0) ||
			(C == 4 && CVarShowHairCategory.GetValueOnGameThread() == 0) ||
			(C == 7 && CVarShowSundryCategory.GetValueOnGameThread() == 0);
		if (bHidden && ByCat[C] > 0)
		{
			UE_LOG(LogAsdaPremShop, Warning,
				TEXT("[премиум-магазин] ★ категория %d («%s») СКРЫТА, но в данных %d записей — ")
				TEXT("проверь, не пора ли её вернуть (ключ asda.PremiumMall.Show%sCategory 1)"),
				C, *Loc(kCatLocBase + C), ByCat[C],
				C == 1 ? TEXT("New") : (C == 4 ? TEXT("Hair") : TEXT("Sundry")));
		}
	}
	UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] PremiumShop.BIN: %d записей, по категориям:%s"),
		BinRows.Num(), *Line);
	return BinRows.Num() > 0;
}

// Порт `ui_rebuildCategorizedItemList@0xC191F0`: какие записи попадают в текущий вектор.
void UAsdaPremiumShopSubsystem::BuildActiveCodes(TArray<int32>& Out) const
{
	Out.Reset();
	for (const FAsdaPremiumShopBinRow& R : BinRows)
	{
		bool bTake = false;
		switch (CatTab)
		{
		case 0: bTake = (R.NewFlag == 1); break;                    // «Горячая новинка!» (+24 == 1)
		case 1: bTake = (R.NewFlag == 2); break;                    // «Новинка!»         (+24 == 2)
		case 2:                                                     // «Инструмент» (Category 1)
			bTake = (R.Category == 1) && (ToolSub == 0 || R.Sub1 == ToolSub);
			break;
		case 3:                                                     // «Снаряжение» (Category 2)
		{
			if (R.Category != 2) { break; }
			if (R.Sub1 != EqSub + 1) { break; }                     // Sub1: 1 Воин, 2 Лучник, 3 Маг
			if (!bEqSub2Shown || EqSub2 == 0) { bTake = true; break; }   // сегм.0 «Общий вид» = весь класс
			const int32 Want = kSub2ToField[FMath::Clamp(EqSub2, 0, kSub2Tabs - 1)];
			bTake = (Want >= 0) && (R.Sub2 == Want);
			break;
		}
		default:                                                    // 4 Волосы, 5 Питомец, 6 Набор, 7 Лавка
			bTake = (kCatToField[FMath::Clamp(CatTab, 0, kCatTabs - 1)] == R.Category);
			break;
		}
		if (bTake) { Out.Add(R.ItemId); }
	}
}

// Сколько карточек на страницу. Эталон выбирает лесенку ПО ВЕТКЕ (`uiItemDlg_onCommand`):
//   ветка 58200 (`case 0xE358`) → `uiRowCountByScreenHeight@0xBD63E0` (`ida.c:478326-478328`);
//   ветка 58201 (`case 0xE359`) → она же (`ida.c:478170-478172`);
//   ветка 58203 (`case 0xE35B`) при выбранном под-сегменте → `uiList_GetVisibleRows@0xBD6270`;
//   ★ категория 6 «Элитный набор» — ЖЁСТКО 6 (`pItemDlg[2061] = 6`, `ida.c:478386`).
int32 UAsdaPremiumShopSubsystem::PerPageForTab() const
{
	// ★ «Элитный набор» — ЖЁСТКО 6 у эталона (`pItemDlg[2061] = 6`, `ida.c:478386`).
	//   ⚠ ЭТОГО ЧИСЛА В ДАННЫХ НЕТ: оно зашито в коде оригинала, и будущий редактор магазинов его
	//   не изменит. Через `FitToScreen` пропускаем, чтобы карточки не вылезали за экран.
	if (CatTab == 6) { return FitToScreen(6); }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaEquipShopSubsystem* Eq = GI ? GI->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr;
	if (CatTab == 3 && bEqSub2Shown && EqSub2 > 0)
	{
		return Eq ? FMath::Max(1, Eq->PanelRows()) : 6;        // `uiList_GetVisibleRows`
	}
	// `uiRowCountByScreenHeight`: зависит ТОЛЬКО от высоты экрана (<=0x400 → 15, <=0x640 → 21, иначе 6).
	const FIntPoint Res = (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		? FIntPoint(GEngine->GameViewport->Viewport->GetSizeXY()) : FIntPoint(1280, 720);
	int32 ByRes = 6;
	if (Res.Y <= 0x400)      { ByRes = 15; }
	else if (Res.Y <= 0x640) { ByRes = 21; }
	return FitToScreen(ByRes);
}

// ★★ Сколько карточек РЕАЛЬНО влезает при текущем разрешении (см. `CVarFitScreen`).
//   Геометрия — из слотов канваса, а не константами: верх сетки уже сдвинут `ApplyContentShift`,
//   а корзина 6024 прибита к низу окна (`SHELLITEM_STORE_DIS_grp`, анкор Y ≈ 0.99, Top < 0).
//   Ряд карточки — 98 px, хром рамки 9-slice — 48 px (та же пара, что в `ApplyPanelHeight`).
int32 UAsdaPremiumShopSubsystem::FitToScreen(int32 ByRes) const
{
	if (CVarFitScreen.GetValueOnGameThread() == 0) { return ByRes; }
	if (!Widget) { return ByRes; }
	const UCanvasPanelSlot* GridSlot = AsdaUi::SlotOf(
		AsdaUi::FindNodeExact(Widget, TEXT("SHELLITEM_ITEMLIST_grp")));
	const UCanvasPanelSlot* CartSlot = AsdaUi::SlotOf(
		AsdaUi::FindNodeExact(Widget, TEXT("SHELLITEM_STORE_DIS_grp")));
	if (!GridSlot || !CartSlot) { return ByRes; }

	// Высота окна: у корня она равна высоте виджета; до первой отрисовки берём 99 % высоты экрана
	//   (`AlignYSize = "32 0 99"` окна 6000 в `.JUI`).
	// ⚠ НЕ `GetCachedGeometry()`: `BuildPages` зовётся ДО отрисовки, и там лежит размер прошлого
	//   кадра (в прогоне 2026-08-28 он дал 495 вместо 760 и урезал страницу втрое).
	//   Считаем по правилу `.JUI`: высота окна 6000 = `AlignYSize "32 0 99"` = 99 % высоты экрана.
	const FIntPoint R = (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		? FIntPoint(GEngine->GameViewport->Viewport->GetSizeXY()) : FIntPoint(1280, 720);
	const float WinH = R.Y * 0.99f;
	const FMargin G = GridSlot->GetOffsets();
	const FMargin C = CartSlot->GetOffsets();
	const float GridTop = G.Top;
	const float CartTop = CartSlot->GetLayout().Anchors.Minimum.Y * WinH + C.Top;
	const float Avail   = CartTop - GridTop - 48.f;              // 48 — хром рамки 9-slice
	const int32 Rows    = FMath::Clamp(FMath::FloorToInt(Avail / 98.f), 1, kCards / 3);
	const int32 Fit     = Rows * 3;
	if (Fit < ByRes)
	{
		UE_LOG(LogAsdaPremShop, Log,
			TEXT("[премиум-магазин] раскладка под экран: экран %dx%d, окно %.0f px, сетка с %.0f, корзина с %.0f ")
			TEXT("⇒ свободно %.0f px = %d рядов = %d карточек (лесенка эталона дала %d)"),
			R.X, R.Y, WinH, GridTop, CartTop, Avail, Rows, Fit, ByRes);
	}
	return FMath::Min(ByRes, Fit);
}

// ═══ ПЕРЕКЛЮЧЕНИЕ ПОЛОС (порт `uiItemDlg_onCommand` case 0xE358…0xE35B) ═══
void UAsdaPremiumShopSubsystem::SetCategory(int32 Cat)
{
	const int32 C = FMath::Clamp(Cat, 0, kCatTabs - 1);
	// ★ Скрытые сегменты не должны открываться и командой/кодом — иначе владелец увидит витрину,
	//   которой на полосе нет (ключи `asda.PremiumMall.ShowHairCategory` / `…ShowSundryCategory`).
	if ((C == 1 && CVarShowNewCategory.GetValueOnGameThread() == 0) ||
	    (C == 4 && CVarShowHairCategory.GetValueOnGameThread() == 0) ||
	    (C == 7 && CVarShowSundryCategory.GetValueOnGameThread() == 0))
	{
		UE_LOG(LogAsdaPremShop, Warning,
			TEXT("[премиум-магазин] категория %d («%s») скрыта решением владельца — переход отклонён"),
			C, *Loc(kCatLocBase + C));
		return;
	}
	CatTab = C;
	Page = 0; SelCard = -1;
	// Эталон при входе в категорию 2 сбрасывает под-вкладку в 0 и рекурсивно зовёт её обработчик;
	//   при входе в 3 — сбрасывает класс, если он вне 0…2 (`ida.c:478347-478363`).
	if (C == 2) { ToolSub = 0; }
	if (C == 3) { if (EqSub < 0 || EqSub > 2) { EqSub = 0; } bEqSub2Shown = false; EqSub2 = 0; }
	else        { bEqSub2Shown = false; }
	RefreshBarVisibility();
	RefreshBarState();
	BuildPages();
	BuildPage();
}

// ★ Пересборка раскладки под текущее разрешение: полосы → схлопывание → число карточек → страница.
void UAsdaPremiumShopSubsystem::RelayoutForScreen()
{
	if (!bOpen) { return; }
	RefreshBarVisibility();
	RefreshBarState();
	BuildPages();
	BuildPage();
}

void UAsdaPremiumShopSubsystem::SetToolSub(int32 Sub)
{
	ToolSub = FMath::Clamp(Sub, 0, kToolTabs - 1);
	Page = 0; SelCard = -1;
	RefreshBarState();
	BuildPages();
	BuildPage();
}

void UAsdaPremiumShopSubsystem::SetEquipSub(int32 Sub)
{
	EqSub = FMath::Clamp(Sub, 0, kEquipTabs - 1);
	// ★ Эталон: клик по 58202 ПОКАЗЫВАЕТ третий уровень 58203 и ставит его сегмент в 0
	//   (`ida.c:478223-478232`: `SetSelectedIndex(pItemDlg[950], 0)` + `onCommand(0xE35B, 0)`).
	bEqSub2Shown = true;
	EqSub2 = 0;
	Page = 0; SelCard = -1;
	RefreshBarVisibility();
	RefreshBarState();
	BuildPages();
	BuildPage();
}

void UAsdaPremiumShopSubsystem::SetEquipSub2(int32 Sub)
{
	EqSub2 = FMath::Clamp(Sub, 0, kSub2Tabs - 1);
	Page = 0; SelCard = -1;
	RefreshBarState();
	BuildPages();
	BuildPage();
}

void UAsdaPremiumShopSubsystem::SetupCards()
{
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	TArray<FName> IconNames; IconNames.SetNum(kCards);
	int32 Resolved = 0;
	int32 Hooked = 0;
	for (int32 i = 0; i < kCards; ++i)
	{
		FAsdaPremiumCardUi& C = Cards[i];
		C.Parts.Reset(); C.AlwaysHidden.Reset(); C.Badges.Reset();
		C.GiftParts.Reset();   // ★ 04.11.2026 ИСПРАВЛЕНИЕ ДЕФЕКТА P101 (согласовано с сессией P101): без сброса каждая смена карты дописывала `_BUT2*` нового окна к узлам снесённого (+85 ссылок за переход; самотест `asda.SelfTest.IconsAfterTravel`)
		const FString P = FString::Printf(TEXT("SHELLITEM_POP%02d"), i);
		const FString Pref = P + TEXT("_");
		// ★ Второй префикс — для узлов с мусорным `[ ` в `ID` (см. ниже, ячейка 00).
		const FString Pref2 = FString(TEXT("__")) + Pref;

		// ★★★ ВСЁ ПОДДЕРЕВО КАРТОЧКИ — ПО ПРЕФИКСУ (закон `jui-uneven-node-layout`): раскладка
		//   ячеек в `.JUI` неоднородна, точечные имена покрывают не все планы, и непогашенные
		//   узлы оставались бы на экране поверх пустой карточки.
		//   ⚠ ИСКЛЮЧЕНИЕ: `SHELLITEM_POP00_SEL_*` — это ОБЩАЯ рамка выделения всей сетки
		//   (контрол 6273), а не часть карточки 00; в её `Parts` она попасть не должна.
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			// ★★★ МИСНЕЙМ ИМПОРТЁРА, найден 2026-08-28 по жалобе «две кнопки Купить».
			//   У ячейки **00** поле `ID` в `gamemain_jui.ini` содержит МУСОРНЫЙ ПРЕФИКС `[ `
			//   (`id=[ SHELLITEM_POP00_BUT0`, `id=[ SHELLITEM_POP00_BUT2` — строки 111312 и 111354),
			//   и импортёр превратил его в ДВА ПОДЧЁРКИВАНИЯ: `__SHELLITEM_POP00_BUT0_grp`.
			//   У ячеек 01…20 имена чистые. Из-за этого префикс `SHELLITEM_POP00_` НЕ ЛОВИЛ
			//   настоящие кнопки первой карточки: они не гасились, не показывались и НЕ БЫЛИ
			//   ПРИВЯЗАНЫ К КЛИКУ — первая карточка не покупалась вовсе.
			if (N != P && !N.StartsWith(Pref) && !N.StartsWith(Pref2)) { continue; }
			if (N.StartsWith(P + TEXT("_SEL"))) { continue; }
			// ★ ДЕФЕКТ ОРИГИНАЛА К13 (copy-paste в именах `.JUI`): узел `SHELLITEM_POP00_TXT12`
			//   принадлежит ячейке POP12, а не POP00 — его родитель `SHELLITEM_POP12_grp`.
			//   Отсекаем по фактическому родителю, а не по имени.
			if (N.StartsWith(P + TEXT("_TXT12"))) { continue; }
			C.Parts.AddUnique(W);
		}

		// Роли ищем по РЕГУЛЯРНОМУ ВЫРАЖЕНИЮ, а не по вычисленным индексам импортёра: индексы
		//   узлов (`_18_0`, `_26_0`, …) меняются при каждом переимпорте окна.
		auto FindRe = [&All, &Pref](const FString& Tail) -> UWidget*
		{
			// `(?:__)?` — тот самый мусорный префикс ячейки 00 (см. выше).
			const FRegexPattern Pat(FString::Printf(TEXT("^(?:__)?%s%s$"), *Pref, *Tail));
			for (UWidget* W : All)
			{
				if (!W) { continue; }
				FRegexMatcher M(Pat, W->GetName());
				if (M.FindNext()) { return W; }
			}
			return nullptr;
		};

		C.BuyButton  = Cast<UButton>(FindRe(TEXT("BUT0")));
		C.BuyCaption = Cast<UTextBlock>(FindRe(TEXT("BUT0_Text")));
		C.NameText   = Cast<UTextBlock>(FindRe(TEXT("TXT00_Text")));
		C.PriceText  = Cast<UTextBlock>(FindRe(TEXT("TXT01")));
		C.ItemIcon   = Cast<UImage>(FindRe(TEXT("ICON_\\d+_0")));
		C.GiftButton = FindRe(TEXT("BUT2"));
		C.GiftCaption = Cast<UTextBlock>(FindRe(TEXT("BUT2_Text")));
		if (UWidget* W = FindRe(TEXT("SHOP_NEW_\\d+_0"))) { C.Badges.Add(W); }
		if (UWidget* W = FindRe(TEXT("SHOP_HOT\\d*_\\d+_0"))) { C.Badges.Add(W); }

		// Подпись кнопки покупки — loc 6944 «OK» (ставит `CGameDialog_InitControls`).
		if (C.BuyCaption)
		{
			C.BuyCaption->SetAutoWrapText(false);
			const FString S = Loc(kLocCellOk);
			if (!S.IsEmpty()) { C.BuyCaption->SetText(FText::FromString(S)); }
		}
		if (C.NameText)  { C.NameText->SetAutoWrapText(false); }
		if (C.PriceText) { C.PriceText->SetAutoWrapText(false); }
		// ★ Кнопка «Подарок» (b+1) и бейджи NEW/HOT в оригинале СКРЫТЫ. Кнопка подарка — ещё и
		//   потому, что её путь идёт опкодом 5074, которого сервер НЕ ПРИНИМАЕТ.
		if (C.GiftButton) { C.AlwaysHidden.Add(C.GiftButton.Get()); }
		for (const TObjectPtr<UWidget>& B : C.Badges) { if (B) { C.AlwaysHidden.Add(B.Get()); } }

		// ═══ ★★★ ЧЕТЫРЕ КНОПКОПОДОБНЫХ УЗЛА НА КАРТОЧКЕ — ВИДИМА ДОЛЖНА БЫТЬ ОДНА ═══════════
		//   Разбор по данным и эталону (жалоба владельца 2026-08-28 «две кнопки Купить»):
		//     • `_BUT0`   **6089** `Mac_Btn_2` @(-124, 23) — `Visible = "1"`. ЭТО И ЕСТЬ КНОПКА
		//       ПОКУПКИ: её IDNum и есть код команды (`uiMapCommandIdToSlotIndex@0xC24E10`:
		//       6089 + 9i → индекс карточки). Подпись ставит эталон:
		//       `SetTextAll4(pSub[4], loc **6944**)` = «OK» (`ida.c:481575-481576`).
		//     • `_BUT2`   **6085** `Mac_Btn_2` @(-124, 48) — `Visible = "0"`. Эталон кладёт в неё
		//       текст `loc 6821` «Подарок» (`ida.c:481568-481569`), но `SetVisible` для неё НЕ
		//       ВЫЗЫВАЕТСЯ НИ РАЗУ во всём бинаре (проверено grep'ом по `m_aPremStore`: SetVisible
		//       есть только у `pBase`, `pSub[6]`, `pSub[7]`) ⇒ подарок не показывается. Это 1:1
		//       согласуется с тем, что опкод подарка 5074 сервер НЕ ПРИНИМАЕТ.
		//     • `_BUT0_b` и `_BUT2_b` — скин `BKBtn_W122`, **IDNum = −1**, `Visible = "0"`:
		//       МЁРТВЫЕ УЗЛЫ ДАННЫХ (запасной вид кнопок), код о них не знает вовсе. Их подписи
		//       лежат прямо в `.JUI`: «구입하기» = «Купить» и «선물하기» = «Подарить».
		//   ⇒ Порт показывал ВСЁ поддерево карточки и вытаскивал на экран все четыре: на карточке 0
		//     выходило «Купить» + «Купить», на прочих — «ОК» + «Подарить». Гасим по ПРЕФИКСУ, чтобы
		//     накрыть и группы, и подписи, и значки-стрелки (`_BUT2i`, `_BUT0i`).
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			const bool bMine = N.StartsWith(Pref) || N.StartsWith(Pref2);
			if (!bMine) { continue; }
			const FString Tail = N.RightChop(N.StartsWith(Pref2) ? Pref2.Len() : Pref.Len());
			if (Tail.StartsWith(TEXT("BUT2")) || Tail.StartsWith(TEXT("BUT0_b")))
			{
				C.AlwaysHidden.AddUnique(W);
			}
			// ★ 01.11.2026 P101_CashGift: настоящая кнопка «Подарок» 6085 + 9i — всё `_BUT2*`, кроме мёртвой запасной `_BUT2_b`.
			if (Tail.StartsWith(TEXT("BUT2")) && !Tail.StartsWith(TEXT("BUT2_b")))
			{
				C.GiftParts.AddUnique(W);
			}
		}

		if (C.ItemIcon) { IconNames[i] = FName(*C.ItemIcon->GetName()); ++Resolved; }

		// ★ 01.11.2026 P101_CashGift: кнопка «Подарок» 6085 + 9i → LABEL_53 (ui_core.c:25705-25713). Подпись — loc 6821 (`ida.c:481568`).
		if (UButton* GiftBtn = Cast<UButton>(C.GiftButton.Get()))
		{
			UAsdaButtonRelay* RG = NewObject<UAsdaButtonRelay>(this);
			RG->Index = i;
			RG->Callback = [this](int32 Idx) { OnCardGiftClicked(Idx); };
			GiftBtn->OnClicked.AddDynamic(RG, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(Relays, RG, GiftBtn);
		}
		if (C.GiftCaption)
		{
			const FString S = Loc(6821);
			if (!S.IsEmpty()) { C.GiftCaption->SetText(FText::FromString(S)); }
		}

		// Клик по кнопке карточки — покупка одной вещи; клик по самой карточке — в корзину.
		if (C.BuyButton)
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = i;
			R->Callback = [this](int32 Idx) { OnCardBuyClicked(Idx); };
			C.BuyButton->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(Relays, R, C.BuyButton);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
			++Hooked;
		}
	}
	// ★★★ «Ничего не происходит» бывает и буквально — когда кнопка не найдена и клик некуда вести.
	//   Проверяем ВСЕ 21 карточку, а не первую (у ячейки 00 имена несут мусорный префикс `__`).
	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] кнопок покупки привязано %d из %d"), Hooked, kCards);
	if (Hooked != kCards)
	{
		UE_LOG(LogAsdaPremShop, Error,
			TEXT("[премиум-магазин] у %d карточек кнопка покупки НЕ НАЙДЕНА — клик по ним «не делает ничего»"),
			kCards - Hooked);
	}
	if (Resolved != kCards)
	{
		UE_LOG(LogAsdaPremShop, Warning,
			TEXT("[премиум-магазин] сетка: разобрано %d иконок из %d — часть карточек не покажется"),
			Resolved, kCards);
	}

	// Иконки товара — ОБЩАЯ ТОЧКА `UAsdaIconCellGrid` (только отрисовка иконок).
	// ★ 10.10.2026: кнопки-оверлеи клика НЕ строим — клик ловит `Tick` по живой геометрии иконки (обоснование там):
	//   оверлей ставился по позиции сборки и после раскладки под экран оказывался не над иконкой.
	if (!IconGrid) { IconGrid = NewObject<UAsdaIconCellGrid>(this); }
	IconGrid->SetOverlayTag(TEXT("PremiumShop"));
	IconGrid->SetClickOverlayEnabled(false);
	IconGrid->SetupNamed(Widget, IconNames, /*bMale*/true,
		[this](int32 Cell) -> int32 { return Cards.IsValidIndex(Cell) ? Cards[Cell].ItemCode : 0; },
		[this](int32 Cell) { OnCardIconPressed(Cell); });
}

void UAsdaPremiumShopSubsystem::SetupCart()
{
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	for (int32 j = 0; j < kCartRows; ++j)
	{
		FAsdaPremiumCartUi& U = CartUi[j];
		U.Parts.Reset();
		const FString Base = FString::Printf(TEXT("SHELLITEM_SELBASE%d"), j + 1);
		U.Row      = Cast<UButton>(AsdaUi::FindNodeByName(Widget, *Base, nullptr));
		U.NameTxt  = Cast<UTextBlock>(AsdaUi::FindNodeByName(Widget, *FString::Printf(TEXT("SHELLITEM_SELBASE_TIT%d"), j + 1), nullptr));
		U.PriceTxt = Cast<UTextBlock>(AsdaUi::FindNodeByName(Widget, *FString::Printf(TEXT("SHELLITEM_SELBASE_PRI%d"), j + 1), nullptr));

		// `_CHK{n}` и `_DIM{n}` — ДВЕ КАРТИНКИ-СОСТОЯНИЯ (а не чекбокс): у них индексный суффикс
		//   импортёра, поэтому ищем по префиксу.
		const FString ChkPref = FString::Printf(TEXT("SHELLITEM_SELBASE_CHK%d_"), j + 1);
		const FString DimPref = FString::Printf(TEXT("SHELLITEM_SELBASE_DIM%d_"), j + 1);
		const FString GrpPref = Base + TEXT("_");
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			if (N.StartsWith(ChkPref)) { U.CheckOn  = W; }
			if (N.StartsWith(DimPref)) { U.CheckOff = W; }
			if (N == Base || N.StartsWith(GrpPref)) { U.Parts.AddUnique(W); }
		}
		if (U.NameTxt)  { U.NameTxt->SetAutoWrapText(false);  U.Parts.AddUnique(U.NameTxt.Get()); }
		if (U.PriceTxt) { U.PriceTxt->SetAutoWrapText(false); U.Parts.AddUnique(U.PriceTxt.Get()); }
		if (U.CheckOn)  { U.Parts.AddUnique(U.CheckOn.Get()); }
		if (U.CheckOff) { U.Parts.AddUnique(U.CheckOff.Get()); }

		if (U.Row)
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = j;
			R->Callback = [this](int32 Idx) { OnCartRowClicked(Idx); };
			U.Row->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(Relays, R, U.Row);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		}
	}
}

void UAsdaPremiumShopSubsystem::SetupPackageCards()
{
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	TArray<FName> PkgIcons; PkgIcons.SetNum(kPkgCards);
	for (int32 i = 0; i < kPkgCards; ++i)
	{
		FAsdaPackageCardUi& C = PkgUi[i];
		C.Parts.Reset();
		const FString P = FString::Printf(TEXT("Package_%d"), i + 1);
		const FString Pref = P + TEXT("_");
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			if (N == P || N.StartsWith(Pref)) { C.Parts.AddUnique(W); }
		}
		auto Get = [this, &P](const TCHAR* Tail) -> UWidget*
		{
			return AsdaUi::FindNodeByName(Widget, *FString::Printf(TEXT("%s_%s"), *P, Tail), nullptr);
		};
		C.NameText   = Cast<UTextBlock>(Get(TEXT("ItemName_Text")));
		C.ItemIcon   = Cast<UImage>(Get(TEXT("ItemIcon")));
		C.BuyButton  = Cast<UButton>(Get(TEXT("PurBtn")));
		C.BuyCaption = Cast<UTextBlock>(Get(TEXT("PurBtn_Text")));
		C.NormalLbl  = Cast<UTextBlock>(Get(TEXT("NTxt00")));
		C.NormalVal  = Cast<UTextBlock>(Get(TEXT("NorPrice00")));
		C.RateLbl    = Cast<UTextBlock>(Get(TEXT("RTxt00")));
		C.RateVal    = Cast<UTextBlock>(Get(TEXT("DiscRate00")));
		C.PriceLbl   = Cast<UTextBlock>(Get(TEXT("PTxt00")));
		C.PriceVal   = Cast<UTextBlock>(Get(TEXT("Price00")));
		for (UTextBlock* Tb : { C.NameText.Get(), C.NormalLbl.Get(), C.NormalVal.Get(),
		                        C.RateLbl.Get(), C.RateVal.Get(), C.PriceLbl.Get(), C.PriceVal.Get() })
		{
			if (Tb) { Tb->SetAutoWrapText(false); }
		}
		// Подписи трёх строк цены — loc 99302/99303/99304 (`uiItemListRefreshPage@0xDB1F40`).
		auto SetLoc = [this](UTextBlock* Tb, int32 Id)
		{
			if (!Tb) { return; }
			const FString S = Loc(Id);
			if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
		};
		SetLoc(C.NormalLbl, kLocPkgNormal);
		SetLoc(C.PriceLbl,  kLocPkgSale);
		SetLoc(C.RateLbl,   kLocPkgRate);

		if (C.ItemIcon) { PkgIcons[i] = FName(*C.ItemIcon->GetName()); }
		if (C.BuyButton)
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = i;
			R->Callback = [this](int32 Idx) { OnPackageBuyClicked(Idx); };
			C.BuyButton->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(Relays, R, C.BuyButton);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		}
	}
	if (!PkgIconGrid) { PkgIconGrid = NewObject<UAsdaIconCellGrid>(this); }
	PkgIconGrid->SetOverlayTag(TEXT("PremiumPkg"));
	PkgIconGrid->SetCellSizePx(128.f);      // карточка набора: иконка 128×128 (`.JUI`)
	PkgIconGrid->SetupNamed(Widget, PkgIcons, /*bMale*/true,
		[this](int32 Cell) -> int32 { return PkgUi.IsValidIndex(Cell) ? PkgUi[Cell].ItemCode : 0; },
		[this](int32 Cell) { OnPackageBuyClicked(Cell); });
}
