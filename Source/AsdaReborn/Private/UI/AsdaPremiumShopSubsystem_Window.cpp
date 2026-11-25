// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumShopSubsystem_Window.cpp — ★ R2 10.11.2026 (разрез AsdaPremiumShopSubsystem.cpp, дословный перенос): окно: открыть/закрыть, рамка выбора, страницы/панели, вкладки, корзина, витрина пакетов.
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
#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"      // ★ ОБЩИЕ ТОЧКИ витрины `.she` (закон port-shared-points)
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaPackagePanelSubsystem.h"   // ★ панель состава набора 26500 — уже портирована
#include "UI/AsdaBoosterShopSubsystem.h"   // ★ самотест общий: печатает и магазин бустеров 15300
#include "UI/AsdaPremiumMallSubsystem.h"    // консольные команды дёргают и диспетчер 30000
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка подтверждения 1559
#include "UI/AsdaCashGiftSubsystem.h"       // ★ 01.11.2026 P101_CashGift: подарок 5072/5074 (кнопки 6085 + 9i, панели 6296/6333)
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Npc/AsdaNpcDialogSubsystem.h"     // ★ волна В4 ремесла: хвост 5045 при закрытии (восстановление снимка окон)
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
#include "UI/AsdaInventorySubsystem.h"      // ★ 26.10.2026: самотест премиум-коробок — подбор ячеек вскрытия (ReservePackageSlots)
#include "Misc/FileHelper.h"                // ★ 26.10.2026: самотест премиум-коробок читает Tools/data/premium_box_sets.json
#include "Misc/Paths.h"                     // ★ 26.10.2026: путь спецификации от каталога проекта
#include "Dom/JsonObject.h"                 // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonReader.h"       // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonSerializer.h"   // ★ 26.10.2026: разбор спецификации наборов
#include "World/AsdaPlayerCharacter.h"          // ★ 24.10.2026 (выход из сидения 1:1): `ApplyEtalonSceneStop` — стоп/«встать» при входе в сцену
#include "UI/AsdaSystemOptionSubsystem.h"      // ★ 25.10.2026 (ревизия sit108, М-3): гейт «персонаж занят» (`objStateAllowsRender`) до стопа сцены
#include "GameFramework/PlayerController.h"    // ★ 24.10.2026: свой павн для `ApplyEtalonSceneStop`
#include "AsdaPremiumShopSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPremiumShopSubsystem*.cpp

namespace {
	// ★★★ ЖАЛОБА ВЛАДЕЛЬЦА 2026-08-28 (скриншот 3): «текст накладывается „Экипировка", неясные
	//   кнопки „Показать всё", „Воин"». Это подписи ШЕСТИ полос `SHELLITEM_STORE_MENU_NEW*`,
	//   у которых в `.JUI` `Visible = "0"` (разбор — в .h у `DeadNewBars`).
	// ═══ ★★★ ПОЛОСА 6002 «Новые вещи | Все вещи | <без имени>» — ЭТАЛОН ЕЁ ГАСИТ ═════════════
	//   Жалоба владельца 2026-08-28: «эти элементы нужно скрыть». Проверено по эталону — и это
	//   НЕ отступление, а ПРИВЕДЕНИЕ К ОРИГИНАЛУ:
	//     `CShellItemStore_InitControls@0xC28820` в самом конце делает
	//        `CControl_SetVisible(this->m_pIndexTxt /*6002*/, 0);`   (`ida.c:479948`)
	//        `CControl_SetVisible(this->m_pMenu     /*6009*/, 0);`   (`ida.c:479949`)
	//     и НИ ОДНОГО `SetVisible(…, 1u)` для них во всём бинаре НЕТ (все 7 упоминаний
	//     `m_pIndexTxt` и все 14 `m_pMenu` — это FindControlByID, SetItemString и SetSelectedIndex).
	//     ★ `CControl_SetVisible@0x12DFB00` — это ИМЕННО видимость (пишет `isVisible`), в отличие
	//       от `CControl_SetVisibleState@0x12FE000`, которое ставит `m_bToggleState` (галочку).
	//   ⇒ витрина 6000 в оригинале работает вообще БЕЗ обеих полос; индекс вкладки 6002 при этом
	//     продолжает считаться (эталон ставит его в 1 = «Все вещи»), просто переключать нечем.
	// ★ Запасной путь витрины: списки `Premium.SHE` (данные МЁРТВОЙ полосы 6009).
	// ★ 06.11.2026 (решение владельца): количество коробки на подписи карточки, корзины и подтверждения покупки.
	static ASDA_DEV_CVAR(int32, CVarCardQtyLabel,
		TEXT("asda.PremiumMall.CardQtyLabel"), 1,
		TEXT("1 = на подписи товара витрины 6000 количество коробки (RU « x3», EN «*3»; столбец CardQty); 0 = только имя предмета."),
		ECVF_Default);

	// ★ 08.11.2026 (пачка 16, баг 6) [ОРИГИНАЛ]: рамка выбора 6273 `SHELLITEM_POP00_SEL` — `uiTabPanel_MoveSelectionHighlight@0xC27030`
	//   (ui_core.c:26717). До правки порт её не показывал вовсе: выбранную карточку не было видно.
	static ASDA_DEV_CVAR(int32, CVarSelFrame,
		TEXT("asda.PremiumMall.SelFrame"), 1,
		TEXT("1 = белая рамка 6273 стоит на выбранной карточке окна 6000, как в оригинале; 0 = как было до 08.11.2026 (рамки нет)."),
		ECVF_Default);

	// ★ 06.11.2026 (решение владельца «подарок = покупка себе»): в 5074 уходит код выдачи карточки (коробка), а не код самой карточки.
	static ASDA_DEV_CVAR(int32, CVarGiftAsBuy,
		TEXT("asda.PremiumMall.GiftAsBuy"), 1,
		TEXT("1 = подарок выдаёт то же, что покупка себе (столбец GiftCode; сервер должен знать коробки в SellItemTable); 0 = код карточки, 1 штука."),
		ECVF_Default);

	// К3 — переполнение массива строк корзины в `uiRefreshItemSlotList@0xC36010`: источник 11
	//   слотов, приёмник 7 строк, а цикл-копир идёт до 11 без проверки границы приёмника.
	static ASDA_DEV_CVAR(int32, CVarFixCartBound,
		TEXT("asda.PremiumMall.FixCartBound"), 1,
		TEXT("1 = класть в корзину не больше 7 строк (исправление К3: эталон копировал 11 в массив "
		     "на 7); 0 = эталонное поведение."),
		ECVF_Default);
}

void UAsdaPremiumShopSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Widget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("SHELLITEM_STORE_WIN_0_%d"), 9);
}

// ═══════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══════════════════════════════════════════
// Порт `uiInventoryOpenWindow@0xC3DCB0` (`ida.c:485563`): показать окно, запросить баланс 5466.
void UAsdaPremiumShopSubsystem::OpenWindow(bool bWantPackage)
{
	const bool bWasOpenForSound = bOpen;   // ★ 06.11.2026: звук 5 — только при настоящем открытии
	// ★ 29.09.2026 (волна 0 торговли): [ОРИГИНАЛ] `charBeginAutoMove@0xC3E420` (МИСНЕЙМ, entity_char.c:45042-45046) — при
	//   закрытом магазине (`this[2075] == 0`) и открытом ОКНЕ обмена 2008 (`g_bPartyInviteFlag` = `CExchange+0x2C`) —
	//   6947 в чат (−6881324, 10 с), окно не открывается. Общая точка `UAsdaTradeStateSubsystem` (пока окна 2008 нет — false).
	//   Молчаливые гейты перед ним (парикмахерская, барабан, склад) — блок выше (ревизия М-3).
	// ★ Ревизия М-3 (29.09.2026): весь блок эталона стоит под `!this[2075] && !this[10327] && !CObject_GetField166(g_pHairShopWnd)
	//   && !CObject_GetField254(g_pInventoryWnd) && !CObject_GetField5581(g_pWarehouseWnd)` — иначе эталон МОЛЧИТ и окна не
	//   открывает. Порт: парикмахерская `IsSessionOpen`, барабан гачи 10050 `IsWindowOpen`, свиток склада `IsRecallActive`
	//   (+0x5734 взводит свиток возврата, см. `AsdaNpcDialogSubsystem.cpp` гейт 5). [ПОРТ] `this[10327]` не разобран — реестр.
	if (!bOpen)
	{
		UGameInstance* GIg = GetGameInstance();
		const UAsdaHairShopSubsystem* HairG = GIg ? GIg->GetSubsystem<UAsdaHairShopSubsystem>() : nullptr;
		const UAsdaBoosterPanelSubsystem* BarrelG = GIg ? GIg->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;
		const UAsdaWarehouseSubsystem* WareG = GIg ? GIg->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr;
		if ((HairG && HairG->IsSessionOpen()) || (BarrelG && BarrelG->IsWindowOpen()) || (WareG && WareG->IsRecallActive()))
		{
			UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] открытие отклонено молча: парикмахерская/барабан 10050/свиток склада (charBeginAutoMove, entity_char.c:45040)"));
			return;
		}
	}
	if (!bOpen && UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance()))
	{
		constexpr int32 kLocBusyTrade = 6947;   // «Открыто окно обмена…» (эталон `charBeginAutoMove`)
		// ★ Ревизия С-1: канал обработчика — чат цветом −6881324 (0xFF96FFD4), 10 с.
		AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, AsdaNotice::LocRaw(GetGameInstance(), kLocBusyTrade),
			FColor(0x96, 0xFF, 0xD4, 0xFF), 10000.f);
		UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] окно обмена 2008 открыто — не открываем, нотис 6947 (charBeginAutoMove)"));
		return;
	}
	// ★ 25.10.2026 (ревизия sit108, М-3) [ОРИГИНАЛ]: `charBeginAutoMove@0xC3E420` — `objStateAllowsRender(LP)`
	//   («персонаж занят»: бой 5, навык 24, смерть 6, 68…) → 6946 в чат (−6881324, 10 с), магазин не открывается и стопа сцены нет.
	//   Общая точка порта `UAsdaSystemOptionSubsystem::IsLocalPlayerBusy`. Ключ `asda.Char.SitExitEtalon 0` — прежнее (гейта нет).
	if (!bOpen && AAsdaPlayerCharacter::IsSitExitEtalon() && GetGameInstance() && GetGameInstance()->GetSubsystem<UAsdaSystemOptionSubsystem>() && GetGameInstance()->GetSubsystem<UAsdaSystemOptionSubsystem>()->IsLocalPlayerBusy())
	{
		constexpr int32 kLocBusyState = 6946;   // «Сейчас открыть нельзя» (эталон `charBeginAutoMove`)
		AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, AsdaNotice::LocRaw(GetGameInstance(), kLocBusyState),
			FColor(0x96, 0xFF, 0xD4, 0xFF), 10000.f);
		UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] персонаж занят — не открываем, нотис 6946 (charBeginAutoMove)"));
		return;
	}
	// ★ Волна 2-А (06.10.2026): [ОРИГИНАЛ] `charBeginAutoMove@0xC3E420` (entity_char.c:45055) — после нотисов 6947/6946 вся ветка
	//   открытия стоит под `*(BYTE*)(g_pRecvUiHandler3[0] + 292) != 1`: окно разборки 32000 ВИДНО ⇒ МОЛЧА, магазина нет.
	if (!bOpen)
	{
		const UAsdaDisassemblySubsystem* Ds = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaDisassemblySubsystem>() : nullptr;
		if (Ds && Ds->IsOpen())
		{
			UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] открыто окно разборки 32000 — не открываем молча (charBeginAutoMove, entity_char.c:45055)"));
			return;
		}
	}
	// ★ `.she` нужен только запасному пути (мёртвая полоса 6009): его отсутствие больше не повод
	//   не открывать окно — живая витрина читается из `PremiumShop.BIN`.
	const bool bShe = LoadShe();
	const bool bBin = LoadBin();
	if (!bShe && !bBin)
	{
		UE_LOG(LogAsdaPremShop, Error, TEXT("[премиум-магазин] витрина не прочитана — окно не открываем"));
		return;
	}
	if (!EnsureWidget()) { return; }

	// ★ Волна В4 ремесла (ревизия В3 С-1): `uiInventoryOpenWindow@0xC3DCB0` → `charBeginAutoMove@0xC3E420`
	//   (entity_char.c:45056-45059): `uiCaptureOpenWindowsState(g_pNpcWnd)` + `uiCloseAllWindows` (+ закрыть окно
	//   персонажа — оно в том же списке «закрыть всё»); вернёт окна закрытие магазина (`uiInventoryResetAndLeave` →
	//   5045, ui_core.c:28553). Общая точка снимка (владелец `Shop`), ключ `asda.UI.WindowSnapshot` (0 = поведение порта
	//   до 26.09: окна не закрываются и не возвращаются).
	// ★ Волна В5 (W4-3, вход из разговора — команда 30030): `uiInventoryOpenWindow` после `charBeginAutoMove` зовёт
	//   `dialogOnConfirmClose(g_pNpcWnd, 1)` — разговор ПРЯЧЕТСЯ под магазином и живёт до его закрытия (5045). Порт прячет
	//   его ДО «закрыть всё»: роспись порта закрыла бы разговор (эталонный `uiCloseAllWindows` его не трогает), а
	//   спрятанный разговор «закрыть всё» пропускает (`CloseFromCloseAll`). Без разговора — ничего не делает.
	if (!bOpen)
	{
		if (UAsdaNpcDialogSubsystem* DlgH = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr)
		{
			DlgH->HideForChildWindow(TEXT("6000 (uiInventoryOpenWindow → dialogOnConfirmClose)"));
		}
	}
	if (!bOpen && UAsdaWindowSubsystem::IsWindowSnapshotEnabled())
	{
		if (UAsdaWindowSubsystem* WMs = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WMs->CaptureOpenWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::Shop, TEXT("элитный магазин 6000 (charBeginAutoMove)"));
			WMs->CloseAllPlayerWindows();
			bSnapshotTaken = true;
		}
	}
	if (!bOpen)
	{
		// `uiInventoryOpenWindow` (ui_core.c:28452-28456): закрыть магазин NPC (`g_pRecvUiHandler2` vtbl+8) и окно ввода
		//   количества (`sub_9260C5` → `uiDialogClose`). `g_pRecvUiHandler` (полиморфный получатель) — окна в порту нет.
		UGameInstance* GIc = GetGameInstance();
		if (UAsdaNpcShopSubsystem* Ns = GIc ? GIc->GetSubsystem<UAsdaNpcShopSubsystem>() : nullptr) { if (Ns->IsOpen()) { Ns->CloseShop(); } }
		if (UAsdaWindowSubsystem* WMn = GIc ? GIc->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { if (WMn->IsNumPadOpen()) { WMn->CloseNumPad(false); } }
		// ★ 24.10.2026 (выход из сидения 1:1) [ОРИГИНАЛ]: `charBeginAutoMove@0xC3E420` — на ходу 4008, затем `charActionStateIs7to9 ? CharFx(9) : CharFx(0)`
		//   (сидящий встаёт БЕЗ 109, по концу «встаёт» — 4024 0; иначе покой). [ПОРТ] прежде шаг не был перенесён. Ключ `asda.Char.SitExitEtalon`.
		if (APlayerController* StopPc = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			if (AAsdaPlayerCharacter* StopCh = Cast<AAsdaPlayerCharacter>(StopPc->GetPawn())) { StopCh->ApplyEtalonSceneStop(TEXT("элитный магазин 6000 (charBeginAutoMove)")); }
		}
	}

	// `uiInventoryResetAndLeave@0xC3DE00` сбрасывает состояние окна при открытии:
	//   6002 → 1 («Все вещи»), 6009 → 0 («Воин»), страница 0, выбор −1, «Итого» 0.
	IndexTab = 1;
	MenuTab  = 0;
	// ★ ЖИВАЯ навигация: при открытии активна категория 0 «Горячая новинка!», под-полос нет
	//   (RadioButton 58200 по умолчанию на сегменте 0; `itemBagDlgInit` индекс не переставляет).
	CatTab   = 0;
	ToolSub  = 0;
	EqSub    = 0;
	EqSub2   = 0;
	bEqSub2Shown = false;
	Page     = 0;
	PkgPage  = 0;
	SelCard  = -1;
	bPending = false;
	PendingBuyCodes.Reset();
	CartRun.Stop();   // [ИСПРАВЛЕНИЕ] 25.10.2026: корзина при открытии пуста — и очереди её покупки нет
	CartNextBatch.Reset(); bCartNextSend = false;
	for (FAsdaPremiumCartRow& R : CartRows) { R = FAsdaPremiumCartRow(); }

	RefreshBarVisibility();   // ★ показать 58200, скрыть под-полосы, схлопнуть пустую область
	RefreshBarState();
	BuildPages();
	BuildPage();
	RefreshCart();
	RefreshTabState();
	RefreshCash();
	ShowPackageTab(bWantPackage);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWindowId, true);
			WM->BringWindowToFront(kWindowId);
		}
		// ★ 06.11.2026 [ОРИГИНАЛ]: хвост `charBeginAutoMove@0xC3E420` (entity_char.c:45103) — звук 5.
		if (!bWasOpenForSound) { AsdaPremiumUiSound(GI, 5); }
		// ★ Эталон при показе окна СРАЗУ шлёт запрос баланса (`ida.c:485587`).
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			// ⚠ Тело 5466 — charId (i32), НЕ userNum (закон `charid-vs-usernum`). При нуле/чужом
			//   значении сервер отвечает 5467 с кодом 2, баланс остаётся 0 и ЛЮБАЯ покупка
			//   отбивается гейтом «не хватает GXP» — это и была жалоба владельца 2026-08-28.
			if (Net->SelectedCharId <= 0)
			{
				UE_LOG(LogAsdaPremShop, Warning,
					TEXT("[премиум-магазин] запрос баланса 5466: charId = %d (не выбран!) — сервер ")
					TEXT("ответит отказом, баланс GXP останется 0 и покупка будет отбиваться"),
					Net->SelectedCharId);
			}
			else
			{
				UE_LOG(LogAsdaPremShop, Log,
					TEXT("[премиум-магазин] запрос баланса 5466: charId = %d"), Net->SelectedCharId);
			}
			Net->SendPremiumBalanceQuery5466(Net->SelectedCharId);
		}
	}
	// ★ Правило владельца: состояние защёлкиваем ПОСЛЕ показа.
	bOpen = true;
	UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] окно 6000 открыто (вкладка наборов: %s)"),
		bPackageTab ? TEXT("да") : TEXT("нет"));
}

void UAsdaPremiumShopSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	bOpen = false;
	bPending = false;
	AsdaPremiumUiSound(GetGameInstance(), 6);   // ★ 06.11.2026 [ОРИГИНАЛ]: `uiInventoryResetAndLeave@0xC3DE00` (ui_core.c:28558)
	// ★ 01.11.2026 P101_CashGift: панели подарка 6296/6333 — часть окна; при следующем открытии их быть не должно.
	ShowGiftPanel(GiftEquip, false);
	ShowGiftPanel(GiftFunc, false);
	if (CartRun.bRun) { StopCartRun(TEXT("окно 6000 закрыто")); }   // [ИСПРАВЛЕНИЕ] 25.10.2026: остаток корзины не отправляем
	CartNextBatch.Reset(); bCartNextSend = false;                     // и второй пакет смешанной корзины
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetWindowVisible(kWindowId, false); }
		// Общая модалка 1559 могла остаться от нас — закрываем без ответа.
		if (UAsdaPurchaseBoxSubsystem* Box = GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>())
		{
			if (Box->IsOpen() &&
				(Box->GetMode() == EAsdaPurchaseMode::CashEquip ||
				 Box->GetMode() == EAsdaPurchaseMode::CashFunc  ||
				 Box->GetMode() == EAsdaPurchaseMode::Package))
			{
				Box->Close();
			}
		}
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
		{
			Tip->HideTooltipFor(EAsdaTipOwner::PremiumShop);
			TipCard = -1;
		}
	}
	UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] окно 6000 закрыто"));
	// ★ Волна В4 ремесла (ревизия В3 С-1): хвост `uiInventoryResetAndLeave` — `uiToggleExpandablePanel(0)` и
	//   `UI_resetAllWindowsAndLeave_5045(g_pNpcWnd)` (ui_core.c:28547, :28553): восстановить снимок окон и вернуть HUD.
	if (bSnapshotTaken)
	{
		bSnapshotTaken = false;
		if (UAsdaNpcDialogSubsystem* Dlg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr)
		{
			Dlg->ResetAllWindowsAndLeave(TEXT("закрытие элитного магазина 6000 (uiInventoryResetAndLeave)"));
		}
	}
}

// ═══════════════════════════════ РЕНДЕР ════════════════════════════════════════════════════════
void UAsdaPremiumShopSubsystem::ShowCard(int32 Card, bool bVisible)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	FAsdaPremiumCardUi& C = Cards[Card];
	const ESlateVisibility V = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	for (const TObjectPtr<UWidget>& W : C.Parts) { if (W) { W->SetVisibility(V); } }
	// Кнопка покупки обязана ЛОВИТЬ мышь (закон `selfhittest-vs-hittest-invisible`).
	AsdaUi::SetShownHit(C.BuyButton, bVisible);
	AsdaUi::SetShownHit(C.ItemIcon, bVisible);
	// ★ Гасим «всегда скрытые» ПОСЛЕ общего показа — иначе они вернулись бы на экран.
	for (const TObjectPtr<UWidget>& W : C.AlwaysHidden) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	// ★ 01.11.2026 P101_CashGift [ИСПРАВЛЕНИЕ]: кнопка «Подарок» 6085 + 9i в оригинале не показывалась никогда (серверной половины
	//   не было); при включённом подарке (`AsdaCashGiftOn`, сервер с P101) она видна. Кнопка ловит мышь, прочее — нет.
	if (bVisible && AsdaCashGiftOn())
	{
		for (const TObjectPtr<UWidget>& W : C.GiftParts)
		{
			if (!W) { continue; }
			W->SetVisibility(W == C.GiftButton ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
		}
	}
}

// ★ 08.11.2026 (пачка 16, баг 6): сбор рамки выбора 6273 и корней карточек. Имена — из дампа ассета
//   (`Reports/batch16/shop_outline/dump_selframe_before.txt`): 8 картинок `SHELLITEM_POP00_SEL_<n>_k` без группы прямо в
//   `SHELLITEM_ITEMLIST_grp`, последними детьми (рисуются поверх карточек), в ассете COLLAPSED (`.JUI` Visible=0, NoSelect=1).
void UAsdaPremiumShopSubsystem::SetupSelFrame()
{
	SelFrame.Reset();
	CardRoots.Reset();
	SelFrameHome = FVector2D::ZeroVector;
	CardHome0 = FVector2D::ZeroVector;
	if (!Widget || !Widget->WidgetTree) { return; }
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	TArray<UWidget*> Planes;
	for (UWidget* W : All)
	{
		if (W && W->GetName().StartsWith(TEXT("SHELLITEM_POP00_SEL_"))) { Planes.Add(W); }
	}
	// Первый план — по имени с хвостом `_0` (угол), чтобы опора не зависела от порядка обхода дерева.
	Planes.Sort([](const UWidget& A, const UWidget& B) { return A.GetName() < B.GetName(); });
	for (UWidget* W : Planes) { SelFrame.Add(W); }
	CardRoots.SetNum(kCards);
	for (int32 i = 0; i < kCards; ++i)
	{
		CardRoots[i] = AsdaUi::FindNodeByName(Widget, *FString::Printf(TEXT("SHELLITEM_POP%02d_grp"), i), nullptr);
	}
	const UCanvasPanelSlot* F0 = SelFrame.Num() > 0 ? AsdaUi::SlotOf(SelFrame[0].Get()) : nullptr;
	const UCanvasPanelSlot* C0 = CardRoots.Num() > 0 ? AsdaUi::SlotOf(CardRoots[0].Get()) : nullptr;
	if (F0) { SelFrameHome = F0->GetPosition(); }
	if (C0) { CardHome0 = C0->GetPosition(); }
	for (const TObjectPtr<UWidget>& W : SelFrame) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	int32 Roots = 0;
	for (const TObjectPtr<UWidget>& W : CardRoots) { Roots += W ? 1 : 0; }
	UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] рамка выбора 6273: планов %d, корней карточек %d/%d, дом рамки (%.0f,%.0f), карточки 00 (%.0f,%.0f)"),
		SelFrame.Num(), Roots, kCards, SelFrameHome.X, SelFrameHome.Y, CardHome0.X, CardHome0.Y);
}

// ★ 08.11.2026 (пачка 16, баг 6) [ОРИГИНАЛ]: порт `uiTabPanel_MoveSelectionHighlight@0xC27030` (ui_core.c:26717):
//   `SetField74_75(рамка 6273, Vec2_74(карточка this[20*idx+84] = 6084+9*idx))` + `SetVisible(рамка, 1)`.
// ⚠ [ОТСТУПЛЕНИЕ, как у парикмахерской 15149 `UAsdaHairShopSubsystem::MoveSelectionFrame`] Эталон ставит угол рамки РОВНО в угол
//   карточки; порт сохраняет авторское смещение `.JUI` (рамка 216×94 в (11,6) у карточки 212×90 в (13,9)), иначе рамка на 4 px
//   выступала бы только вправо-вниз. Позиция — по ЖИВОМУ слоту карточки (сетка перекладывается под разрешение), сдвиг всех
//   восьми планов общей точкой `AsdaUi::ShiftCanvasSlot` от их текущего места.
void UAsdaPremiumShopSubsystem::MoveSelectionFrame(int32 Card)
{
	if (SelFrame.Num() == 0) { return; }
	const bool bWant = CVarSelFrame.GetValueOnGameThread() != 0 && !bPackageTab
		&& Cards.IsValidIndex(Card) && Cards[Card].ItemCode > 0
		&& CardRoots.IsValidIndex(Card) && CardRoots[Card]
		&& CardRoots[Card]->GetVisibility() != ESlateVisibility::Collapsed
		&& CardRoots[Card]->GetVisibility() != ESlateVisibility::Hidden;
	const UCanvasPanelSlot* CardSlot = bWant ? AsdaUi::SlotOf(CardRoots[Card].Get()) : nullptr;
	const UCanvasPanelSlot* Ref      = bWant ? AsdaUi::SlotOf(SelFrame[0].Get()) : nullptr;
	if (!CardSlot || !Ref)
	{
		for (const TObjectPtr<UWidget>& W : SelFrame) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
		return;
	}
	const FVector2D Shift = SelFrameHome + (CardSlot->GetPosition() - CardHome0) - Ref->GetPosition();
	for (const TObjectPtr<UWidget>& W : SelFrame)
	{
		if (!W) { continue; }
		AsdaUi::ShiftCanvasSlot(W.Get(), Shift);
		W->SetVisibility(ESlateVisibility::HitTestInvisible);   // `.JUI` NoSelect=1: рамка мышь не ловит
	}
	UE_LOG(LogAsdaPremShop, Verbose, TEXT("[премиум-магазин] рамка выбора 6273 -> карточка %d (код %d)"), Card, Cards[Card].ItemCode);
}

void UAsdaPremiumShopSubsystem::PopulateCard(int32 Card, int32 ItemCode)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	FAsdaPremiumCardUi& C = Cards[Card];
	C.ItemCode = ItemCode;
	if (ItemCode <= 0) { ShowCard(Card, false); return; }

	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FString Name;
	int32 Price = 0;
	if (const FAsdaItemRecord* R = IT ? IT->GetItem(ItemCode) : nullptr)
	{
		Name  = IT->GetItemName(ItemCode);   // ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — геттером (язык сессии)
		Price = R->Price;             // ★ поле +0xA4 — ЦЕНА В GXP (не золото; имя «cash» — миснейм)
	}
	else
	{
		FAsdaSpecialItemlistRow Sp;
		if (IT && IT->GetSpecialItem(ItemCode, Sp)) { Name = Sp.Name; Price = Sp.Price; }
	}
	if (C.NameText)  { C.NameText->SetText(FText::FromString(CardLabel(ItemCode, Name))); }
	if (C.PriceText) { C.PriceText->SetText(FText::FromString(AsdaUi::FormatThousands(Price))); }
	ShowCard(Card, true);
}

FString UAsdaPremiumShopSubsystem::CardLabel(int32 Code, const FString& Name) const
{
	const int32* Qty = CardQtyByCode.Find(Code);
	if (!Qty || Name.IsEmpty() || CVarCardQtyLabel.GetValueOnGameThread() == 0) { return Name; }
	return FAsdaLang::IsRu() ? FString::Printf(TEXT("%s x%d"), *Name, *Qty) : FString::Printf(TEXT("%s*%d"), *Name, *Qty);
}

int32 UAsdaPremiumShopSubsystem::GiftWireCode(int32 Code) const
{
	const int32* Gift = GiftCodeByCode.Find(Code);
	return Gift && CVarGiftAsBuy.GetValueOnGameThread() != 0 ? *Gift : Code;
}

UWidget* UAsdaPremiumShopSubsystem::DebugNode(const FString& Name) const
{
	if (!Widget) { return nullptr; }
	UWidget* N = Widget->GetWidgetFromName(FName(*Name));
	if (!Cast<UComboBoxString>(N) || !Widget->WidgetTree) { return N; }
	// Движковый комбо всегда скрыт (`UAsdaJuiComboBox::Setup`), щелчок игрока приходит на план тела `<idc>_<jui>_0`.
	UWidget* Body = N;
	Widget->WidgetTree->ForEachWidget([&](UWidget* W)
	{
		FString Idc; int32 Jui = 0, Plane = 0;
		if (W && AsdaUi::ParsePlaneName(W->GetName(), Idc, Jui, Plane) && Idc == Name && Plane == 0) { Body = W; }
	});
	return Body;
}

UWidget* UAsdaPremiumShopSubsystem::DebugBuySelWidget() const
{
	return BtnBuySel.Get();
}

UWidget* UAsdaPremiumShopSubsystem::DebugCardBuyWidget(int32 Card) const
{
	return Cards.IsValidIndex(Card) ? Cards[Card].BuyButton.Get() : nullptr;
}

UWidget* UAsdaPremiumShopSubsystem::DebugCardGiftWidget(int32 Card) const
{
	return Cards.IsValidIndex(Card) ? Cards[Card].GiftButton.Get() : nullptr;
}

UWidget* UAsdaPremiumShopSubsystem::DebugGiftGiveWidget() const
{
	return GiftEquip.PreBtn.Get();
}

FString UAsdaPremiumShopSubsystem::DebugEquipPrice() const
{
	return GiftEquip.Price1 ? GiftEquip.Price1->GetText().ToString() : FString();
}

FString UAsdaPremiumShopSubsystem::DebugCartRowText(int32 Row) const
{
	return CartUi.IsValidIndex(Row) && CartUi[Row].NameTxt ? CartUi[Row].NameTxt->GetText().ToString() : FString();
}

FString UAsdaPremiumShopSubsystem::DebugCardText(int32 Card) const
{
	return Cards.IsValidIndex(Card) && Cards[Card].NameText ? Cards[Card].NameText->GetText().ToString() : FString();
}

// ★★★ СКОЛЬКО РЯДОВ У ПАНЕЛИ ВИТРИНЫ. Эталон выбирает лесенку ПО ВКЛАДКЕ 6009:
//   сегмент 9 «Инструмент» → `uiRowCountByScreenHeight@0xBD63E0`, остальные →
//   `uiList_GetVisibleRows@0xBD6270` (`ida.c:486078-486100`). Обе — общие точки магазина 6800.
int32 UAsdaPremiumShopSubsystem::PanelRowsForTab() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaEquipShopSubsystem* Eq = GI ? GI->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr;
	if (!Eq) { return 6; }
	// ★ Вторая лесенка (`uiRowCountByScreenHeight`) в порту отдельной точки не имеет: она зависит
	//   ТОЛЬКО от высоты экрана (`<=0x400 → 15`, иначе 21/6) и на наших разрешениях даёт 21.
	//   Заводить ради неё третью общую точку не станем — считаем прямо здесь, по эталонной таблице.
	if (MenuTab == 9)
	{
		const FIntPoint Res = GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport
			? FIntPoint(GEngine->GameViewport->Viewport->GetSizeXY())
			: FIntPoint(1280, 720);
		const int32 H = Res.Y;
		if (H <= 0x400) { return 15; }
		return (H <= 0x640) ? 21 : 6;
	}
	return Eq->PanelRows();
}

// ★★★ ВЫСОТА БЕЛОЙ РАМКИ ВИТРИНЫ 6083 — 1:1 `inventorySowelWin_SwitchTab@0xC3F070`:
//     h = 98 * (рядов / 3) + 48
//   ⚠ И ТА ЖЕ ПОПРАВКА, ЧТО У ОКНА 6800 (жалоба владельца 2026-08-26): число рядов эталон берёт
//   из ТАБЛИЦЫ РАЗРЕШЕНИЙ, а карточки раскладываются по `ItemsPerPage` (ДРУГАЯ лесенка,
//   `uiList_GetVisibleRows2`). На разрешении, которого в таблице нет, первая даёт 6 (2 ряда), а
//   на экране рисуется 12 карточек (4 ряда) — два ряда оказывались ВНЕ рамки. Берём максимум:
//   эталонную раскладку не ломаем, но рамка гарантированно накрывает всё, что реально показано.
void UAsdaPremiumShopSubsystem::ApplyPanelHeight(int32 VisibleCards)
{
	if (CVarLegacyPanelHeight.GetValueOnGameThread() != 0) { return; }

	// ★ Волна 3: проверка 9 планов, расчёт рядов и перекладка рамки 3×3 — общая точка `AsdaShopUi::ApplyShowcaseHeight`
	//   (порядок прежний: планы → ряды по разрешению → «высота не изменилась» → слоты → перекладка).
	AsdaShopUi::FShowcaseHeight H;
	if (!AsdaShopUi::ApplyShowcaseHeight(PanelPlanes, [this]() { return FMath::Max(3, PanelRowsForTab()); }, VisibleCards, PanelHeightApplied, H)) { return; }
	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] рамка витрины 6083: вкладка %d, по разрешению %d, показано карточек %d ")
		TEXT("⇒ рядов %d, высота %d px (середина %.0f, низ на %.0f)"),
		MenuTab, H.ByRes, H.Shown, H.Rows, H.NewH, H.MidH, H.BotY);
}

void UAsdaPremiumShopSubsystem::BuildPage()
{
	CardDbl.Reset();   // [ПОРТ] 10.10: страница перестроена — под тем же номером карточки другой товар
	for (int32 i = 0; i < kCards; ++i) { Cards[i].ItemCode = 0; ShowCard(i, false); }
	int32 Shown = 0;
	for (const FAsdaPremiumEntry& E : Entries)
	{
		if (E.Page != Page) { continue; }
		if (!Cards.IsValidIndex(E.SlotInPage)) { continue; }
		PopulateCard(E.SlotInPage, E.ItemCode);
		Shown = FMath::Max(Shown, E.SlotInPage + 1);
	}
	if (IconGrid) { IconGrid->Refresh(); }
	// ★ Рамку подгоняем ЗДЕСЬ: только после заполнения известно, сколько карточек реально показано.
	ApplyPanelHeight(Shown);
	RefreshPageLabels();
	// ★ 08.11.2026 (пачка 16, баг 6): рамка 6273 — за выбранной карточкой. Листание, вкладки и открытие сбрасывают `SelCard` в −1
	//   (оригинал гасит рамку: LABEL_57 листания `ida.c:266158`, Init `ida.c:269957`), а пересборка под разрешение и возврат с
	//   вкладки наборов 58300 (её показ поддерева сетки зажигает и планы рамки) ставят рамку на живую позицию той же карточки.
	MoveSelectionFrame(SelCard);
}

void UAsdaPremiumShopSubsystem::RefreshPageLabels()
{
	// ★ Волна 3: общая точка `AsdaShopUi::SetPageLabels` (порт `tradeWinUpdatePageLabels@0xC37BC0`: обе метки «номер + 1»).
	AsdaShopUi::SetPageLabels(TxtPage, TxtPages, Page, MaxPage);
}

void UAsdaPremiumShopSubsystem::RefreshCash()
{
	if (TxtCash) { TxtCash->SetText(FText::FromString(AsdaUi::FormatThousands(CashGxp))); }
}

void UAsdaPremiumShopSubsystem::RefreshTabState()
{
	AsdaUi::ApplyTabCaptionState(MenuLabels, MenuButtons, MenuTab, MenuIdleColors);
	AsdaUi::ApplyTabCaptionState(IndexLabels, IndexButtons, IndexTab, IndexIdleColors);
}

// Порт `uiRefreshItemSlotList@0xC36010`. ★ ИСПРАВЛЕН ДЕФЕКТ К3: у эталона источник — 11 слотов,
//   приёмник — массив на 7 строк, а цикл-копир идёт до 11 БЕЗ проверки границы приёмника
//   (`ida.c:482779-482818`), то есть при 8+ позициях он пишет за границу.
void UAsdaPremiumShopSubsystem::RefreshCart()
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const int32 Limit = (CVarFixCartBound.GetValueOnGameThread() != 0) ? kCartRows : kCartRows;

	for (int32 j = 0; j < kCartRows; ++j)
	{
		FAsdaPremiumCartUi& U = CartUi[j];
		const FAsdaPremiumCartRow& R = CartRows[j];
		const bool bUsed = (j < Limit) && (R.ItemCode > 0);

		for (const TObjectPtr<UWidget>& W : U.Parts)
		{
			AsdaUi::SetShownDecor(W, bUsed);
		}
		AsdaUi::SetShownHit(U.Row, bUsed);
		if (!bUsed) { continue; }

		FString Name; int32 Price = 0;
		if (const FAsdaItemRecord* Rec = IT ? IT->GetItem(R.ItemCode) : nullptr) { Name = IT->GetItemName(R.ItemCode); Price = Rec->Price; }   // ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — геттером (язык сессии)
		else
		{
			FAsdaSpecialItemlistRow Sp;
			if (IT && IT->GetSpecialItem(R.ItemCode, Sp)) { Name = Sp.Name; Price = Sp.Price; }
		}
		if (U.NameTxt)  { U.NameTxt->SetText(FText::FromString(CardLabel(R.ItemCode, Name))); }
		if (U.PriceTxt) { U.PriceTxt->SetText(FText::FromString(AsdaUi::FormatThousands(Price))); }
		// ★ `_CHK` и `_DIM` — ДВЕ КАРТИНКИ СОСТОЯНИЯ, а не чекбокс: показывается ровно одна.
		AsdaUi::SetShownDecor(U.CheckOn, R.bChecked);
		AsdaUi::SetShownDecor(U.CheckOff, !R.bChecked);
	}
	if (TxtSum) { TxtSum->SetText(FText::FromString(AsdaUi::FormatThousands(CartTotal()))); }
}

int32 UAsdaPremiumShopSubsystem::FindFreeCartSlot() const
{
	for (int32 j = 0; j < kCartRows; ++j) { if (CartRows[j].ItemCode <= 0) { return j; } }
	return -1;
}

int32 UAsdaPremiumShopSubsystem::CartTotal() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	int32 Sum = 0;
	for (const FAsdaPremiumCartRow& R : CartRows)
	{
		if (R.ItemCode <= 0 || !R.bChecked) { continue; }
		if (const FAsdaItemRecord* Rec = IT ? IT->GetItem(R.ItemCode) : nullptr) { Sum += Rec->Price; continue; }
		FAsdaSpecialItemlistRow Sp;
		if (IT && IT->GetSpecialItem(R.ItemCode, Sp)) { Sum += Sp.Price; }
	}
	return Sum;
}

// ═══════════════════════════════ ВКЛАДКИ И СТРАНИЦЫ ════════════════════════════════════════════
void UAsdaPremiumShopSubsystem::SetMenuTab(int32 Tab)
{
	const int32 T = FMath::Clamp(Tab, 0, kMenuTabs - 1);
	if (T == MenuTab) { return; }
	MenuTab = T;
	Page = 0;
	SelCard = -1;
	BuildPages();
	BuildPage();
	RefreshTabState();
	if (kTabToList[T] < 0)
	{
		UE_LOG(LogAsdaPremShop, Warning,
			TEXT("[премиум-магазин] сегмент 9 «Инструмент» (loc %d) собственного списка НЕ ИМЕЕТ: "
			     "у эталона это ветка `default:` (дефект К17). asda.PremiumMall.FixTab9=%d"),
			kTabLoc[T], CVarFixTab9.GetValueOnGameThread());
	}
}

void UAsdaPremiumShopSubsystem::SetIndexTab(int32 Tab)
{
	const int32 T = FMath::Clamp(Tab, 0, kIndexTabs - 1);
	// ★ Сегмент 2 мёртв (К8) — на него не переключаемся.
	if (T == 2 && CVarHideIndexSeg3.GetValueOnGameThread() != 0) { return; }
	if (T == IndexTab) { return; }
	IndexTab = T;
	// ★ Обе живые вкладки 6002 читают ОДИН И ТОТ ЖЕ набор списков: в
	//   `inventorySowelWin_SwitchTab@0xC3F070` условие входа — `if ((unsigned)a2 <= 1)`, то есть
	//   индекс полосы 6002 на выбор списка НЕ ВЛИЯЕТ (влияет только полоса 6009).
	Page = 0;
	SelCard = -1;   // ★ 08.11.2026 (пачка 16, баг 6): страница пересобрана — под тем же номером другой товар, рамка 6273 гаснет
	BuildPages();
	BuildPage();
	RefreshTabState();
}

void UAsdaPremiumShopSubsystem::SetPage(int32 NewPage)
{
	const int32 P = FMath::Clamp(NewPage, 0, MaxPage);
	if (P == Page) { return; }
	Page = P;
	SelCard = -1;
	BuildPage();
}

// ═══════════════════════════════ ВИТРИНА НАБОРОВ 58300 ═════════════════════════════════════════
// ★★★ Живая витрина набора — это ВКЛАДКА 58300 ВНУТРИ окна 6000, а НЕ окно 26000
//   (доказательство — в шапке `AsdaPremiumMallSubsystem.h`). Показывает её код окна 6000:
//   `SetVisible(this[951] /*=58300*/, 1)` (`ida.c:473636`), одновременно гася сетку товаров
//   (`SetVisible(this[25], 0)` и `this[83]`).
void UAsdaPremiumShopSubsystem::ShowPackageTab(bool bOn)
{
	bPackageTab = bOn;
	for (const TObjectPtr<UWidget>& W : PackageTabTree)
	{
		AsdaUi::SetShownDecor(W, bOn);
	}
	for (const TObjectPtr<UWidget>& W : GridTree)
	{
		AsdaUi::SetShownDecor(W, !bOn);
	}
	if (bOn) { BuildPackagePage(); }
	else     { BuildPage(); }
}

// Порт `uiBuildPackageItemList@0xDB1DD0` + `uiItemListRefreshPage@0xDB1F40` (`ida.c:616731`, `616768`).
//
// ★★★ ИСТОЧНИК ВИТРИНЫ НАБОРОВ — НЕ `Premium.she`, А `PackAgeList.BIN`.
//   `uiBuildPackageItemList` обходит СВОЙ массив записей по 0x24 = 36 б (`{i32 code; char[32] name}`),
//   который грузится из `./Data/Character/PackAgeList.BIN` (`ida.c:616252`: header 0x14, затем
//   `fread(self[11], 0x24, self[12], stream)`). В порту это `DT_packagelist`.
//   Отбор: `itemSubType == 200 || == 201` И существует строка `PackAgeTable.BIN` по
//   `ItemRecord->durationOrEnchant` — ровно то, что делает общая точка
//   `UAsdaItemTableSubsystem::IsPackageItem`.
//
// ★★★ ПОЛЯ ЦЕН ЛЕЖАТ В `packagetable`, А НЕ В `pricetable` (МИСНЕЙМ ПОРТ-СТРУКТУРЫ).
//   Эталонный `statRec` — это запись `PackAgeTable.BIN` 0x38 = 56 б = 14 dword'ов, и витрина
//   читает у неё индексы 11/12/13. В порт-структуре `FAsdaPackagetableRow` эти же dword'ы
//   называются `Item5` / `Qty5` / `Reserved52` — то есть «шестая пара предметов», про которую в
//   комментарии структуры сказано «её никто не читает». ЧИТАЕТ — ИМЕННО ВИТРИНА НАБОРОВ:
//     statRec[11] = `Item5`      → «Обычная цена» (loc 99302), строка видна только при [12] > 0
//     statRec[12] = `Qty5`       → «Размер скидки» «%d%%» (loc 99304)
//     statRec[13] = `Reserved52` → ВИД ПЛАШКИ: ≤ 11 — «скидка» (одна цифра, `10*(k+1) == [12]`),
//                                  иначе — «купи N получи M» (десятки [13]/10, единицы [13]%10)
//   «Цена со скидкой» (loc 99303) — `itemPtr[41]`, то есть `ItemRecord->Price` (поле +0xA4).
//   Имя карточки — ИМЯ ПРЕДМЕТА (`ItemData_FindByCode`), а НЕ имя из `PackAgeList.BIN`.
//
// ⚠ ОТЛОЖЕНО С ПРИЧИНОЙ. (1) ИКОНКА: эталон рисует не иконку из атласа предметов, а ОТДЕЛЬНУЮ
//   текстуру 128×128 по имени из `PackAgeList.BIN` (`TexturePath_Format(&entryRec[8])` →
//   `CControl_DrawTextureRect(..., 128, 128)`). Разрешателя «текстура по имени» для этой папки в
//   порту нет (рантайм читает только ассеты UE), поэтому карточка показывает ИКОНКУ ПРЕДМЕТА
//   общей точкой `UAsdaIconCellGrid`. (2) ПЛАШКИ-ШТАМПЫ (скидка 58700/58701 и «купи N получи M»
//   26350/26360/26370 + цифры): эталон КЛОНИРУЕТ их, причём вторую — из ДРУГОГО окна (26000,
//   «склад шаблонов»). Клонирование поддерева между двумя WBP — отдельная работа; сами числа
//   ([12] и [13]) уже читаются и печатаются в журнал, так что данные для плашек готовы.
void UAsdaPremiumShopSubsystem::BuildPackagePage()
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	// Витрина = `DT_packagelist`, отфильтрованный тем же условием, что у эталона.
	TArray<int32> PkgCodes;
	if (const UDataTable* Dt = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/ItemDB/DT_packagelist.DT_packagelist")))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaPackagelistRow* R = reinterpret_cast<const FAsdaPackagelistRow*>(It.Value);
			if (!R || R->Code <= 0) { continue; }
			if (IT && IT->IsPackageItem(R->Code)) { PkgCodes.AddUnique(R->Code); }
		}
	}
	else
	{
		UE_LOG(LogAsdaPremShop, Error,
			TEXT("[премиум-магазин] нет таблицы DT_packagelist — витрина наборов пуста"));
	}
	PkgCodes.Sort();          // порядок строк `GetRowMap` не гарантирован, а страницы от него зависят

	const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(PkgCodes.Num(), kPkgCards));
	PkgPage = FMath::Clamp(PkgPage, 0, Pages - 1);

	for (int32 i = 0; i < kPkgCards; ++i)
	{
		FAsdaPackageCardUi& C = PkgUi[i];
		const int32 Idx = PkgPage * kPkgCards + i;
		const int32 Code = PkgCodes.IsValidIndex(Idx) ? PkgCodes[Idx] : 0;
		C.ItemCode = Code;
		const bool bUsed = (Code > 0);
		for (const TObjectPtr<UWidget>& W : C.Parts)
		{
			AsdaUi::SetShownDecor(W, bUsed);
		}
		AsdaUi::SetShownHit(C.BuyButton, bUsed);
		AsdaUi::SetShownHit(C.ItemIcon, bUsed);
		if (!bUsed) { continue; }

		const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
		if (C.NameText && Rec) { C.NameText->SetText(FText::FromString(IT->GetItemName(Code))); }   // ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — геттером (язык сессии)
		// «Цена со скидкой» — `itemPtr[41]` = `ItemRecord->Price`.
		if (C.PriceVal && Rec) { C.PriceVal->SetText(FText::FromString(AsdaUi::FormatThousands(Rec->Price))); }

		int32 NormalPrice = 0, Rate = 0, StampKind = 0;
		if (Rec)
		{
			if (const FAsdaPackagetableRow* Pk = IT->FindPackageRow(Rec->DurationOrEnchant))
			{
				NormalPrice = Pk->Item5;        // statRec[11]
				Rate        = Pk->Qty5;         // statRec[12]
				StampKind   = Pk->Reserved52;   // statRec[13]
			}
		}
		// Строка «Обычная цена» видна ТОЛЬКО при ненулевой скидке — 1:1 `if (statRec[12] > 0)`.
		const bool bHasSale = (Rate > 0);
		AsdaUi::SetShownDecor(C.NormalLbl, bHasSale);
		if (C.NormalVal)
		{
			C.NormalVal->SetText(FText::FromString(bHasSale ? AsdaUi::FormatThousands(NormalPrice) : FString()));
		}
		if (C.RateVal) { C.RateVal->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), Rate))); }
		// Подпись «Размер скидки» эталон гасит и показывает только в ветке «скидка» при [12] > 0.
		if (C.RateLbl)
		{
			const bool bShowRate = (StampKind <= 11) && bHasSale;
			AsdaUi::SetShownDecor(C.RateLbl, bShowRate);
		}
		UE_LOG(LogAsdaPremShop, Verbose,
			TEXT("[премиум-магазин] набор %d «%s»: обычная %d, скидка %d%%, вид плашки %d"),
			Code, Rec ? *Rec->ItemName : TEXT("?"), NormalPrice, Rate, StampKind);
	}
	if (PkgIconGrid) { PkgIconGrid->Refresh(); }
	if (TxtPage)  { TxtPage->SetText(FText::FromString(FString::Printf(TEXT("%d"), PkgPage + 1))); }
	if (TxtPages) { TxtPages->SetText(FText::FromString(FString::Printf(TEXT("%d"), Pages))); }
	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] витрина наборов 58300 (источник DT_packagelist): %d наборов, страница %d из %d"),
		PkgCodes.Num(), PkgPage + 1, Pages);
}
