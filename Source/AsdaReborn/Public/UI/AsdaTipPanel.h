// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTipPanel.h — ОДНА реализация «плашки-тултипа» порта (рамка 9-slice + заголовок + тело
//   с inline-разметкой + укладка у курсора с ограничением экраном).
//
// ★ ПОЧЕМУ ОТДЕЛЬНЫЙ КЛАСС, А НЕ ЕЩЁ ОДНА КОПИЯ В ПОДСИСТЕМЕ
//   В оригинале плашка ОДНА на всю игру — глобальный объект `g_ItemTooltip` класса
//   `CItemTooltipWnd` (`CItemTipWin_InitUI@0xEA15E0`), и он держит СРАЗУ ДВА окна:
//     • `ITEM_TIPWIN`        (IDNum 5000) — основной тултип         (5001 заголовок, 5002 тело);
//     • `ItemCompareTip_Win` (IDNum 5050) — окно «НАДЕТЫЙ ПРЕДМЕТ»  (5051 заголовок, 5052 тело).
//   Один и тот же объект показывает тултип предмета, подсказку Plus Package, подсказку эффекта,
//   подсказку слота хотбара и т.д. Поэтому и у нас это ОДИН класс с несколькими экземплярами,
//   а владелец экземпляров — `UAsdaItemTooltipSubsystem` (порт `g_ItemTooltip`).
//   `UAsdaMiniStateSubsystem` больше своей копии не держит: он берёт готовую плашку у подсистемы.
//
// Реверс раскладки: `uiTooltipShowAndLayout@0xEA1E20` (ui_core.c:82541),
//   `Tooltip_LayoutNearCursor@0xEA26F0` (ui_core.c:82770),
//   `uiClampPopupToScreen@0xEA6770` (ui_core.c:82944), `uiPanel9SliceLayout@0x13005D0`.
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Layout/Visibility.h"
#include "Fonts/SlateFontInfo.h"
#include "AsdaTipPanel.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UGameInstance;

// ★★★ ВЛАДЕЛЕЦ ПЛАШКИ. Плашка ОДНА на всю игру (эталонный `g_ItemTooltip`), и её просят показать
//   ПЯТЬ разных потребителей. В эталоне это безопасно: там показ/скрытие идут через фокус контрола
//   (`CControl_SetFocus`/`CControl_KillFocus`), то есть скрыть может только тот, кто держит фокус.
//   У порта опрос курсора свой у каждой подсистемы, и после дедупа 2026-08-08 (когда мини-статус
//   перестал держать ВТОРУЮ копию окна) получилось, что ЛЮБОЙ потребитель гасил ЧУЖОЙ тултип:
//   `UAsdaSoulGuardSubsystem::TickPointer` звал `HideSharedTip()` КАЖДЫЙ КАДР, пока окно хранителя
//   закрыто, — тултип предмета жил ровно один кадр (или ни одного, в зависимости от порядка тиков).
//   Поэтому у плашки теперь есть ВЛАДЕЛЕЦ: скрыть может только он (или явный безусловный Hide).
UENUM()
enum class EAsdaTipOwner : uint8
{
	None      UMETA(DisplayName = "никто (плашка скрыта)"),
	Item      UMETA(DisplayName = "тултип предмета (инвентарь/экипировка/склад/хотбар-предмет)"),
	// ★ Гнёзда самоцветов правой инфо-панели инвентаря (441…444, эталон `invSlotBuildTooltip`,
	//   ветка 0x1B9…0x1BC). ОТДЕЛЬНЫЙ владелец нужен из-за гонки тиков: инвентарь на переходе
	//   «курсор ушёл с ячейки» гасит свой тултип (владелец `Item`), а мы — свой; с общим владельцем
	//   уход с гнезда НА ЯЧЕЙКУ мог погасить только что показанный тултип предмета.
	InfoSocket UMETA(DisplayName = "гнездо самоцвета правой инфо-панели (441…444)"),
	MiniState UMETA(DisplayName = "мини-статус: Plus Package и иконки эффектов"),
	Hotbar    UMETA(DisplayName = "слот хотбара (навык/эмоция/пункт меню)"),
	SoulGuard UMETA(DisplayName = "хранитель души (окно 56000)"),
	// ★ Строка списка «хвастовства» `Opend_Item_Win` (28110/28120/28130, эталон ui_core.c:47894 —
	//   событие 8 → `ItemTooltip_Build`). ОТДЕЛЬНЫЙ владелец обязателен: инвентарь каждым тиком
	//   гасит плашку владельца `Item`, и с общим владельцем тултип строки жил бы один кадр.
	ShowOff   UMETA(DisplayName = "строка окна хвастовства (28110/28120/28130)"),
	SysMenu   UMETA(DisplayName = "системное меню"),
	// ★ Слот дерева навыков окна `SKILL_WIN` (12000) и кнопка 4-й профессии 12401.
	//   Эталон: `skillWndOnUICommand@0xE4A8E0` case 8/9/0xA. ОТДЕЛЬНЫЙ владелец обязателен по той же
	//   причине, что у ShowOff/SoulGuard: окно навыков опрашивает курсор каждый кадр и с общим
	//   владельцем гасило бы чужую плашку (и наоборот — своя жила бы один кадр).
	SkillWin  UMETA(DisplayName = "слот дерева навыков (окно 12000)"),
	// ★ Ячейка панели «Состав пакета» (26500). Эталон: `stampWnd_ShowItemTooltip@0xDB29B0`,
	//   маршрут msg 9/10 в `stampCraftWnd_msgProc@0xDB2C80`. Отдельный владелец обязателен по той
	//   же причине, что у ShowOff/SkillWin: панель опрашивает курсор каждый кадр, и с общим
	//   владельцем `Item` инвентарь гасил бы её плашку (а её плашка — тултип ячейки инвентаря).
	Package   UMETA(DisplayName = "ячейка панели состава пакета (26500)"),
	// ★ Отказы окна «Барабан гачи» (10050). Эталон шлёт их в ОБЩУЮ ПЛАШКУ
	//   (`CItem_BuildTooltipText(g_pItemTooltipUI, …)`), а не в чат.
	Booster   UMETA(DisplayName = "окно барабана гачи (10050)"),
	// ★ Ячейка окна «Вид» гачи (15600 `BOOSTER_PREV_WIN`): 5 «особых» 15603…15611 и 15 «обычных»
	//   15614…15642. Эталон `uiInventoryShowSlotTooltip@0xB87200` строит по ним ОБЫЧНЫЙ тултип
	//   предмета (`ItemTooltip_Build(&g_ItemTooltip, …)` / `CItem_BuildSowelTooltipText`) — тот же,
	//   что у предметов сумки. Владелец ОТДЕЛЬНЫЙ от `Booster`: та плашка занята текстовыми отказами
	//   окна барабана, и с общим владельцем они гасили бы друг друга (закон `ui-shared-tip-ownership`).
	BoosterPrev UMETA(DisplayName = "ячейка окна «Вид» гачи (15600)"),
	// ★ ПРЕМИУМ-ГАЧА (сцена `CGachaScene`): 25 ячеек витрины призов 68311…68335 окна 68300 и иконка
	//   приза 68453 окна 68450. Эталон: `GachaScene_OnControlMessage@0xB704C0` msg 8 →
	//   `cItemBar_BuildSlotTooltip@0xB6F3C0` (item_inventory.c:5422), msg 9 → `Tooltip_LayoutNearCursor`,
	//   msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`. Владелец ОТДЕЛЬНЫЙ от `Booster`/`BoosterPrev`:
	//   премиум-гача — самостоятельная система (закон `premium-gacha-vs-booster`), а её сцена живёт
	//   одновременно с инвентарём, который каждым тиком гасит плашку владельца `Item`
	//   (закон `ui-shared-tip-ownership`).
	PremiumGacha UMETA(DisplayName = "ячейка витрины/иконка приза премиум-гачи (68311…68335, 68453)"),
	// ★ ЭЛИТНЫЙ ПОЧТОВЫЙ ЯЩИК (окно 69000): 49 иконок 69011…69077. Эталон
	//   `uiInventoryHandleCommand@0xEF1CC0` msg 8 → `uiItemGridFindTooltip@0xEF2040`, msg 9 →
	//   `Tooltip_LayoutNearCursor`, msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой: окно ящика по требованию владельца открывается ВМЕСТЕ с
	//   инвентарём, а инвентарь каждым тиком гасит плашку владельца `Item` — с общим владельцем
	//   подсказка ящика жила бы один кадр (закон `ui-shared-tip-ownership`).
	PremiumPost UMETA(DisplayName = "иконка строки элитного почтового ящика (69011…69077)"),
	// ★ ПОПАП НАГРАД ЗА ЗАДАНИЕ `EPISODEPOP_WIN` (540): иконки «Обычной награды» 544/546/548/550/
	//   552/2401 и «Дополнительной награды» 557/559/561/563/565/2404. Эталон
	//   `uiWindowMsgHandler@0xD91DB0` msg 8 → `uiShowEquipSlotTooltip2@0xD925A0`, msg 9 →
	//   `Tooltip_LayoutNearCursor`, msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): попап живёт одновременно с
	//   окном разговора и с HUD, а любой чужой покадровый опросчик курсора (инвентарь, хотбар,
	//   хранитель души) гасит плашку СВОЕГО владельца — с общим владельцем подсказка награды
	//   жила бы один кадр, а наша гасила бы чужую.
	QuestReward UMETA(DisplayName = "иконка награды попапа задания (540)"),
	// ★ ОКНО ХАРАКТЕРИСТИК (900): строки статов 12310…12322, кнопка звания 905 и слоты эмоций
	//   1110…1151. Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно живёт
	//   одновременно с инвентарём и хотбаром, которые каждым тиком гасят плашку СВОЕГО владельца —
	//   с общим владельцем наша подсказка жила бы один кадр, а мы гасили бы чужую.
	CharaInfo UMETA(DisplayName = "строка статов / звание / эмоция окна характеристик (900)"),
	// ★ МАГАЗИН NPC (окно `NPCSHOP_WIN` 1300): 36 ячеек витрины 1320…1355 и 5 ячеек корзины
	//   1360…1364. Эталон `uiHandleWindowCommand@0xE24DE0` msg 8 → `npcShopRefreshSlotByMsgId`
	//   → vtbl+40 (у витрины `itemTooltip_setStateFromRecord@0xE285B0`, у продажи
	//   `inventory_FindItemAndShowTooltip@0xE307F0`), msg 9 → `Tooltip_LayoutNearCursor`,
	//   msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): магазин открывается вместе
	//   с инвентарём, а тот каждым тиком гасит плашку владельца `Item`.
	NpcShop   UMETA(DisplayName = "ячейка витрины/корзины магазина NPC (1320…1355, 1360…1364)"),
	// ★ ОКНО РЕМОНТА `INVEN_Repair_Win` (57300): 11 ячеек надетого 57311…57321, 30 ячеек сумки
	//   57331…57360 и подсказки-«туториал» кнопок 57305/57306. Эталон `shopSellWndProc@0xEDC090`
	//   msg 8 → `shopSellPriceTooltipUpdate@0xEDB960` (тултип предмета), msg 9 →
	//   `Tooltip_LayoutNearCursor` + `uiShowTutorialPopupByCode@0xEDC2E0`, msg 0xA →
	//   `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно открывается вместе с
	//   инвентарём, а тот каждым тиком гасит плашку владельца `Item`.
	Repair    UMETA(DisplayName = "ячейка/кнопка окна ремонта (57311…57360, 57305, 57306)"),
	// ★ ОКНО ПИТОМЦА `Pet_Cage_Win` (43000) и его статус-панель `Pet_Status_Win` (43200):
	//   двухстрочные подсказки полос 43203 (опыт, loc 100606 + 101304) и 43204 (сытость,
	//   loc 100608 + 101303). Эталон `CUpgradeWindow_OnTooltip@0xDC7C40` (item_enhance.c:4304-4314)
	//   зовёт их через `tutorialPopupLayoutSmall(&g_ItemTooltip, заголовок, тело, −1, −1)`
	//   на событии 9, а событие 0xA гасит плашку (`CControl_KillFocus(&g_ItemTooltip)`).
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно питомца живёт
	//   одновременно с инвентарём и HUD, а те каждым тиком гасят плашку СВОЕГО владельца.
	PetCage   UMETA(DisplayName = "полосы опыта/сытости окна питомца (43203, 43204)"),
	// ★ ОКНО ИНКУБАТОРА `Pet_Hatchery_Win` (42000): тултипы ПРЕДМЕТА над слотом яйца 42010,
	//   слотом бустера 42012, сеткой яиц 42050…42055 и сеткой бустеров 42070…42075.
	//   Эталон `petWindow_MsgProc@0xDC2450` (npc_mob_pet.c:4868-4874): событие 9 →
	//   `petWindowBuildItemTooltipByMsgId@0xDC1140` + `Tooltip_LayoutNearCursor`, событие 0xA →
	//   `CControl_KillFocus(&g_ItemTooltip)`. Владелец ОБЯЗАН быть свой (закон
	//   `ui-shared-tip-ownership`): инкубатор живёт одновременно с сумкой, а та каждым тиком
	//   гасит плашку владельца `Item`.
	PetHatchery UMETA(DisplayName = "слоты и сетки окна инкубатора (42010, 42012, 42050…42055, 42070…42075)"),
	// ★ ОКНО РОСТА ПИТОМЦА `Pet_LimitBreak_Win` (44200): двухстрочная подсказка над подписью
	//   талисмана 44242 (заголовок loc 110113 «Предел», тело loc 110643 «Этот свиток…»).
	//   Эталон `uiUpgradeDialogWndProc@0xEE2FB0` (ui_core.c:90825-90835): событие 8 →
	//   `tutorialPopupLayoutSmall(&g_ItemTooltip, 110113, 110643, −1, −1)` при
	//   `!helper_getField1(g_pUIManager)`, событие 9 → `Tooltip_LayoutNearCursor`,
	//   событие 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно роста живёт вместе с
	//   сумкой и окном клетки, а оба каждым тиком гасят плашку СВОЕГО владельца — с общим
	//   владельцем подсказка талисмана жила бы один кадр.
	PetGrowth UMETA(DisplayName = "подпись талисмана окна роста питомца (44242)"),
	// ★ ОКНО «ИЗМЕНЕНИЕ ПАРАМЕТРОВ ПРЕДМЕТА» `ItemOptionValue_WIN` (50300): 30 ячеек сумки
	//   снаряжения 50304…50333 и слот выбранного предмета 50302. Эталон `itemUpgradeWndProc@0xB639B0`
	//   msg 8 → `equipSlotTooltipByCtrlId@0xB63B70`, msg 9 → `Tooltip_LayoutNearCursor`,
	//   msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно опрашивает курсор каждый
	//   кадр, и с общим владельцем `Item` гасило бы плашку инвентаря (и наоборот).
	ItemOption UMETA(DisplayName = "ячейка/слот окна смены параметров (50302, 50304…50333)"),
	// ★ ВОЕННЫЙ МАГАЗИН `HONOR_itemmall_Win` (67200): 12 иконок товара 67214+10i, 12 значков цены
	//   67218+10i и 12 значков «только победившей армии» 67401…67412. Эталон
	//   `guildRankUiOnEvent@0xB77060` msg 8 → `uiEquip_OnSlotHoverTooltip@0xB778C0`,
	//   msg 9 → `Tooltip_LayoutNearCursor` + `tutorialPopupLayoutSmall`,
	//   msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`. Владелец свой по той же причине.
	WarShop   UMETA(DisplayName = "карточка товара военного магазина (67210…67328, 67401…67412)"),
	// ★ МАГАЗИН СНАРЯЖЕНИЯ `SHELLITEM_STORE2_WIN` (6800): 21 карточка витрины (6857+6k) и 7 строк
	//   «Списка снаряжения» (6812+5j).
	//   ⚠ ЭТО НАША ДОБАВКА, А НЕ ВОСПРОИЗВЕДЕНИЕ. В оригинале подсказок у этого окна НЕТ:
	//   в `.JUI` у всех его контролов `ToopTip = ""`, а обработчика сообщений 8/9/0xA
	//   (`itemTooltip_setStateFromRecord` / `Tooltip_LayoutNearCursor` / `CControl_KillFocus`)
	//   для `CShellItemStore` в эталоне нет вовсе (проверено grep-ом по обоим декомпилам, см.
	//   `Reports/equip_shop_port.md` §6). Заведено по требованию владельца 2026-08-26 №2,
	//   ключ отката — `asda.EquipShop.Tooltips 0`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно живёт одновременно с
	//   инвентарём, а тот каждым тиком гасит плашку владельца `Item`.
	EquipShop UMETA(DisplayName = "карточка/строка магазина снаряжения (6857+6k, 6812+5j) — ДОБАВКА"),
	// ★ ОКНО СБРОСА/ВОССТАНОВЛЕНИЯ УЛУЧШЕНИЙ `Item_Initializ_Win` (20200): 30 ячеек списка
	//   20240…20269 и 5 ячеек очереди целей 20330…20334. Эталон `uiInventoryShowItemTooltip
	//   @0xD30AA0` (ui_core.c:49680) и `itemInitWin_ShowSlotTooltip@0xD2D720`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно живёт одновременно с
	//   инвентарём, который каждым тиком гасит плашку владельца `Item`.
	ItemReset UMETA(DisplayName = "ячейка окна сброса улучшений (20240…20269, 20330…20334)"),
	// ★ ОКНО УЛУЧШЕНИЯ СНАРЯЖЕНИЯ (ЗАТОЧКА) `Item_Upgrade_Win` (20000), 25.09.2026: сетка 20020…20049,
	//   свитки 20050…20055, помощники 20081…20083 / 20090…20092, иконка цели 20003, карточки
	//   20150/20153/20156 и подсказка полосы 20195. Эталон `itemInitWin_ShowSlotTooltip@0xD2D720`
	//   (msg 9) и `tutorialPopupLayoutSmall` для 20195 (msg 8, ui_core.c:47824).
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно живёт одновременно с
	//   инвентарём, который каждым тиком гасит плашку владельца `Item`.
	ItemUpgrade UMETA(DisplayName = "ячейка/иконка окна улучшения снаряжения (20020…20197)"),
	// ★ ОКНО «ПЕРЕДАЧА ПАРАМЕТРОВ» `EXTRACT_WIN` (16000): 12 ячеек источника 16011…16022,
	//   12 ячеек приёмника 16061…16072, две крупные иконки 16045/16088 и 6 строк списка
	//   свитков-бустеров 16400…16405. Эталон `extractShop_HandleMessage@0xC8E720` msg 8 →
	//   `itemDlgShowSlotTooltip@0xC8FEB0` / `petUiWin_ShowItemTooltip@0xC941B0`, msg 9 →
	//   `Tooltip_LayoutNearCursor`, msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно живёт одновременно
	//   с инвентарём, а тот каждым тиком гасит плашку владельца `Item`.
	ExtractShop UMETA(DisplayName = "ячейка/иконка окна передачи параметров (16011…16072, 16045, 16088, 16400…16405)"),
	// ★ СКЛАД `WARE_WIN` (4000): 30 ячеек вкладки 0 (4008…4037), 5 ячеек корзины переноса
	//   (4039…4043) и 8×30 ячеек плавающих окон-сумок (4055…4084, 4089…4118, 4123…4152,
	//   4157…4186, 75411…75440, 75511…75540, 75611…75640, 75711…75740). Эталон
	//   `uiStore_handleEvent@0xE87790` msg 8 → `warehouseSlotSetTooltipOrState@0xE89060`,
	//   msg 9 → ведение за курсором, msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): склад открывается вместе
	//   с окном разговора NPC и живёт рядом с инвентарём, а тот каждым тиком гасит плашку
	//   владельца `Item` — с общим владельцем подсказка склада жила бы один кадр.
	Warehouse UMETA(DisplayName = "ячейка склада/корзины/сумки (4008…4043, 4055…75740)"),
	// ★ Строка/ячейка окна АУКЦИОНА 13000 (выдача 13429…13464, «мои лоты» 13205…13240, строки
	//   лота 13016…13048, сетка выставления 13064…13123). Владелец свой по закону
	//   `ui-shared-tip-ownership`: аукцион живёт рядом с инвентарём, а тот каждым тиком гасит
	//   плашку владельца `Item` — с общим владельцем подсказка аукциона жила бы один кадр.
	Exchange  UMETA(DisplayName = "ячейка/строка окна аукциона (13000)"),
	// ★ ПРЕМИУМ-МАГАЗИН `SHELLITEM_STORE_WIN` (6000): 21 карточка витрины (6084+9i), 7 строк
	//   корзины (6038+6j) и 12 карточек витрины наборов (58310+20i).
	//   ⚠ ЭТО НАША ДОБАВКА, А НЕ ВОСПРОИЗВЕДЕНИЕ — ровно как у `EquipShop`: у `CShellItemStore`
	//   обработчика сообщений 8/9/0xA в эталоне нет, `ToopTip` у контролов окна пуст.
	//   Ключ отката — `asda.PremiumMall.Tooltips 0`. Владелец ОБЯЗАН быть свой (закон
	//   `ui-shared-tip-ownership`): молл живёт рядом с инвентарём, а тот каждым тиком гасит
	//   плашку владельца `Item`.
	PremiumShop UMETA(DisplayName = "карточка/строка премиум-магазина (6084+9i, 6038+6j, 58310+20i)"),
	// ★ МАГАЗИН БУСТЕРОВ `BOOSTER_SHOP_WIN` (15300): 21 ячейка витрины (15306 + 9i).
	//   ⚠ ЭТО НАША ДОБАВКА, А НЕ ВОСПРОИЗВЕДЕНИЕ: у `CBoosterShop` обработчика сообщений
	//   8/9/0xA в эталоне нет вовсе, а у контролов окна `ToopTip = ""`. Ключ отката —
	//   `asda.PremiumMall.BoosterTooltips 0`. Владелец ОБЯЗАН быть свой (закон
	//   `ui-shared-tip-ownership`): магазин живёт рядом с инвентарём, а тот каждым тиком гасит
	//   плашку владельца `Item`; с общим владельцем наша подсказка жила бы один кадр.
	BoosterShop UMETA(DisplayName = "ячейка витрины магазина бустеров (15306 + 9i)"),
	// ★ СТРОКА ДОП-МЕНЮ «Прочее» (окно `ETC_NEW_WIN` 74600, строки 74701…74716). Эталон
	//   `uiListControlOnUiEvent@0xECDC70` msg 8 → `tutorialShowItemNoticePopup@0xECD760`
	//   (заголовок = подпись записи `Extra_menu_table`, тело = её строка-подсказка),
	//   msg 9 → `Tooltip_LayoutNearCursor`, msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): меню живёт рядом с
	//   инвентарём и хотбаром, а те каждым тиком гасят плашку СВОЕГО владельца — с общим
	//   владельцем подсказка строки жила бы один кадр, а мы гасили бы чужую.
	ExtraMenu UMETA(DisplayName = "строка доп-меню «Прочее» (74701…74716)"),
	// ★ ОКНО ЗВАНИЙ `Title_Win` (18700) и значок-уведомление `Title_Notice_BG` (50000). Эталон
	//   `questLog_OnUiEvent@0xE97B00` case 8 → `tutorialPopupLayoutSmall(&g_ItemTooltip, …)` для
	//   контролов 50000 (нотисы 20500…20503 + заголовок 20504), 18737/18738/18727/18728
	//   (20400…20403 + заголовки 20404…20407), case 9 → `Tooltip_LayoutNearCursor`,
	//   case 0xA → `CControl_KillFocus(&g_ItemTooltip)`. Владелец ОБЯЗАН быть свой (закон
	//   `ui-shared-tip-ownership`): окно званий живёт рядом с инвентарём и хотбаром, а те каждым
	//   тиком гасят плашку СВОЕГО владельца — с общим владельцем наша подсказка жила бы один кадр.
	TitleWin  UMETA(DisplayName = "кнопка окна званий (18700) / значок уведомления (50000)"),
	// ★ ОКНО «ПОИСК ОТРЯДА» `Find_Party_WIn` (8500): кнопки «Свернуть» 8652/8698, «Отмена» 8653,
	//   «Обновить» 8654 и «Применить» правки 8662. Эталон `sub_DB8AA0` (обработчик событий окна,
	//   в IDA не размечен) событие 8 → байт-таблица `0xDB92D8` с базой 8652 → четыре ветки
	//   `UI_BuildHelpTooltipText(&g_ItemTooltip, тема, 0)` с темами **32 / 33 / 34 / 35**
	//   (`0xDB8FB2`, `0xDB8FC5`, `0xDB8FD8`, `0xDB8FEB`); событие 9 → `Tooltip_LayoutNearCursor`,
	//   событие 0xA → `CControl_KillFocus(&g_ItemTooltip)` + сброс флага `this+0x1648`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно поиска отряда живёт
	//   рядом с инвентарём и хотбаром, а те каждым тиком гасят плашку СВОЕГО владельца.
	PartyFind UMETA(DisplayName = "кнопка окна поиска отряда (8652, 8653, 8654, 8662, 8698)"),
	// ★ ОКНО «СВЕДЕНИЯ О ЧУЖОМ ПЕРСОНАЖЕ» `OTHERCHARA_DATA` (4200): 11 слотов надетого
	//   4240…4250 и 4 гнезда самоцветов 4227…4230. Эталон `npcTalkWnd_HandleMessage@0xDA9B70`
	//   (МИСНЕЙМ, к NPC-разговору отношения не имеет) msg 8 → `npcTradeWndOnHover@0xDA98C0`
	//   (у слота — `ItemTooltip_Build`, у гнезда — `CControl_SetState(&g_ItemTooltip, 1, sowelId,…)`),
	//   msg 9 → `Tooltip_LayoutNearCursor`, msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец ОБЯЗАН быть свой (закон `ui-shared-tip-ownership`): окно открывается поверх мира
	//   вместе с инвентарём и хотбаром, а те каждым тиком гасят плашку СВОЕГО владельца.
	OtherChara UMETA(DisplayName = "слот надетого / гнездо самоцвета окна чужого персонажа (4240…4250, 4227…4230)"),
	// ★ ОКНО ВЫСОКОЙ ПЕРЕПЛАВКИ `Smelting3_Win` (78200), 25.09.2026 (волна 7 заточки): сетка снаряжения
	//   78211…78224, иконка результата 78271, материалы 78241/78251/78261. Эталон `uiTooltipWndProc@0xCF36B0`
	//   msg 8 → `uiItemTooltipOnHoverDispatch@0xCF4120`, msg 9 → `Tooltip_LayoutNearCursor`, msg 0xA →
	//   `CControl_KillFocus(&g_ItemTooltip)`. Владелец свой (закон `ui-shared-tip-ownership`): окно живёт
	//   рядом с инвентарём и окном заточки, а они каждым тиком гасят плашки СВОИХ владельцев.
	HighRefine UMETA(DisplayName = "ячейка/иконка окна высокой переплавки (78211…78224, 78241…78271)"),
	// ★ ОКНА РЕМЕСЛА (создание снаряжения), 25.09.2026 волна В2: строки списка рецептов 76020…76076
	//   (тултип предмета-результата, `uiCraft_ShowItemTooltip@0xFE0E50`) и подсказки 76005/76006
	//   (`tutorialPopupLayoutSmall`, заголовок 12077 + тело 115301/115300). Эталон `shopGridWndProc@0xFDD720`
	//   (миснейм, shop_trade.c:22079-22129): msg 8 — показ, msg 9 — ведение, msg 0xA — `CControl_KillFocus`.
	//   Владелец свой (закон `ui-shared-tip-ownership`): окно живёт рядом с инвентарём и хотбаром.
	Craft     UMETA(DisplayName = "строка/подсказка окна создания снаряжения (76005, 76006, 76020…76076)"),
	// ★ ЛИЧНАЯ ЛАВКА (волна Л1 торговли, 29.09.2026): ячейки сумки 22100…22129 и иконки лотов 22308+20N окна подготовки
	//   22000, товары витрины хозяина 23121+10N окна 23000. Эталон `shopWndBuildSlotTooltip@0xDD3100` (msg 8 процедуры
	//   `uiSellWindowMsgProc@0xDD16E0`), msg 9 → `Tooltip_LayoutNearCursor`, msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	//   Владелец свой (закон `ui-shared-tip-ownership`): окна лавки живут рядом с инвентарём и хотбаром.
	SelfStore UMETA(DisplayName = "ячейка/лот окон личной лавки (22100…22129, 22308+20N, 23121+10N)"),
	// ★ ОКНО ОБМЕНА `TRAN_WIN` 2008 (волна О2 торговли, 29.09.2026): кандидаты 2013…2036, мой лоток 2037…2041, лоток партнёра
	//   2042…2046. Эталон `exchangeWnd_OnMessage@0xC6F400`: msg 8 → `equipSlotShowTooltip@0xC70DB0`, msg 9 →
	//   `Tooltip_LayoutNearCursor`, msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`. Владелец свой (закон
	//   `ui-shared-tip-ownership`): окно живёт рядом с хотбаром, который каждым тиком гасит плашку СВОЕГО владельца.
	ExchangeP2P UMETA(DisplayName = "ячейка окна обмена игрок-игрок (2013…2046)"),
	// ★ ЯБЛОКО ДРУЖБЫ на панели соулмейта 6500 (волна 1-Б, 06.10.2026): 6692 яблоко, 6693 золотое, 6695 «трясущееся».
	//   Эталон `SoulmateUI_HandleEvent@0xE67EC0` msg 8 → `uiTooltipSetTitleBody(&g_ItemTooltip, loc 12183 «Очки опыта: [ss1]»)`,
	//   msg 9 → `Tooltip_LayoutNearCursor`, msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`. Владелец свой (закон
	//   `ui-shared-tip-ownership`): панель живёт рядом с инвентарём и хотбаром, которые гасят плашки СВОИХ владельцев.
	SoulApple UMETA(DisplayName = "Яблоко Дружбы панели соулмейта (6692, 6693, 6695)"),
	// ★ Волна 2-Б (06.10.2026): ячейки окна «Доклад об охоте» 56450 (56461…56490) и слот выбранного знака 56451.
	HuntReport UMETA(DisplayName = "ячейка/слот окна доклада об охоте (56451, 56461…56490)"),
	// ★ Волна 2-А (06.10.2026): ОКНО РАЗБОРКИ `Disassembly_Win` 32000 — список 32050…32079, превью 32010…32015, выбранный
	//   32004, результат 32016. Эталон `upgradeWnd_onMessage@0xE0C0F0` msg 9 → `itemSlotShowTooltipById@0xE0F140`, msg 0xA →
	//   `CControl_KillFocus(&g_ItemTooltip)`. Владелец свой (закон `ui-shared-tip-ownership`): окно живёт рядом с инвентарём.
	Disassembly UMETA(DisplayName = "ячейка окна разборки (32004, 32010…32016, 32050…32079)"),
	// ★ Волна В2 рыбалки (08.10.2026): подсказки кнопок окна рыбалки 71300 — 71328 / 71329 / 71349. Эталон
	//   `uiControlEventDispatch@0xFC9110` событие 9 → `tutorialPopupLayoutSmall(&g_ItemTooltip, 111096, 111064…66)`,
	//   0xA → `CControl_KillFocus(&g_ItemTooltip)`. Владелец свой (закон `ui-shared-tip-ownership`).
	FishWin   UMETA(DisplayName = "кнопка окна рыбалки (71328, 71329, 71349)"),
	// ★ Волна В3 рыбалки (08.10.2026): подсказка НАГРАДЫ книги рыбака 74081…74084. Эталон `uiItemWindow_WndProc@0xFD5A70`
	//   событие 8 → `itemSlot_BuildTooltipForEquip@0xFDAAD0` (ветки = `cItemBar_BuildSlotTooltip`), 9 → `Tooltip_LayoutNearCursor`,
	//   0xA → `CControl_KillFocus(&g_ItemTooltip)`. Владелец свой (закон `ui-shared-tip-ownership`).
	FishBook  UMETA(DisplayName = "иконка награды книги рыбака (74081…74084)"),
	// ★ ПАНЕЛЬ ДРУГА `SOULMATE_POP_WIN` 6500 (23.10.2026, «SM bar»): уровни 6501/6505, полоса опыта 6504 и 28 значков
	//   эффектов Друга 33000…33027. Эталон `SoulmateUI_HandleEvent@0xE67EC0` msg 8 → `helpTooltipDispatchById@0xE6ABA0`
	//   (темы 16/17/23 `UI_BuildHelpTooltipText`) и `uiHelpEntryLayoutAndShow[Alt]` по значку, msg 9 → `Tooltip_LayoutNearCursor`,
	//   msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`. Владелец свой (закон `ui-shared-tip-ownership`): яблоко той же панели —
	//   у `SoulApple`, а хотбар и инвентарь каждым тиком гасят плашки СВОИХ владельцев.
	SoulPop   UMETA(DisplayName = "панель Друга 6500 (6501, 6504, 6505, 33000…33027)"),
	// ★ ДОСКА ОБЪЯВЛЕНИЙ `Subjugate_WIN` 75000 (28.10.2026): подсказка кнопки 75281 (событие 8 → нотис 3598,
	//   `CControl_SetState(&g_ItemTooltip, 8, 3598…)`, quest.c:9393). Уход (0xA) → `CControl_KillFocus`. Иконкам наград эталон
	//   подсказки не даёт (в `questUiOnControlEvent@0xEE9860` событие 8 только у 75281). Владелец свой (`ui-shared-tip-ownership`).
	BoardQuest UMETA(DisplayName = "доска объявлений 75000 (кнопка 75281)"),
	// ★ СПРАВОЧНЫЕ ПОДСКАЗКИ КНОПОК HUD (30.10.2026, `AsdaHelpTopics`, темы `UI_BuildHelpTooltipText@0xEB11A0`). У каждого окна свой
	//   владелец по той же причине, что у соседей: хотбар и инвентарь каждым тиком гасят плашки СВОИХ владельцев.
	//   Мини-карта 1700: `MiniMap_BigMapWindowProc@0xD65210` msg 8 → `uiBuildHelpTooltipByMsgId@0xD65ED0` (1704/1706/1707/1708/1719).
	Minimap   UMETA(DisplayName = "кнопки мини-карты 1700 (1704, 1706, 1707, 1708, 1719)"),
	//   Панель отряда 2200: `partyWnd_onCommand` msg 8 → `uiShowHelpTooltipByControlId@0xB9B9C0` (2209…2279).
	PartyHud  UMETA(DisplayName = "панель отряда 2200 (2209…2279)"),
	//   Окно чата: `uiChatWindowMsgProc` msg 8 → `uiHelp_ShowTooltipForControl@0xBB3D80` и `tutorialPopupLayoutSmall`.
	ChatWin   UMETA(DisplayName = "кнопки окна чата"),
	//   ★ 30.10.2026 Окно итога «Битвы армий» 49800: `CClan_OnMemberListUI` msg 9 по полосам 49811/21/31 и 49814/24/34 →
	//   `tutorialPopupLayoutSmall(&g_ItemTooltip, 111093, 111094|111095, 0xFFFF0000, −1)`; 0xA → `CControl_KillFocus`.
	CampWar   UMETA(DisplayName = "полосы окна итога битвы армий (49811…49834)"),
	Console   UMETA(DisplayName = "консольная команда"),
	Legacy    UMETA(DisplayName = "вызов без указания владельца"),
	//   ★ 31.10.2026 Окно «Разделение улучшения» 50500: `itemUpgradeWindowOnEvent@0xB67470` msg 8 →
	//   `itemTooltip_ShowForControlId@0xB68690` (50516/50526/50536/50551…50580), msg 9 → `Tooltip_LayoutNearCursor`,
	//   0xA → `CControl_KillFocus`. В КОНЦЕ перечисления — номера прежних значений не сдвигаются. Свой владелец: окно живёт вместе с инвентарём (закон `ui-shared-tip-ownership`).
	UpgradeSplit UMETA(DisplayName = "ячейка окна разделения улучшения (50516/50526/50536/50551…50580)"),
	//   ★ 04.11.2026 Иконки состояний ЦЕЛИ (6703…6730): `HUD_CommandHandler` msg 8 → `HUD_TargetBuffIcon_OnClick@0xE7FA70` →
	//   `uiHelpEntryLayoutAndShow[Alt]` (ui_core.c:76782-76786, 77361-77381), 9 → `Tooltip_LayoutNearCursor`, 0xA → гашение.
	//   Свой владелец: панель цели опрашивает курсор каждый кадр (закон `ui-shared-tip-ownership`).
	TargetState UMETA(DisplayName = "иконка состояния цели (6703…6730)"),
	// ★ 07.11.2026: ячейка лотка навыков Дружбы (6526…6537), эталон `SoulmateUI_HandleEvent` msg 8/9/0xA. Свой владелец: лоток опрашивает курсор каждый кадр.
	SoulTray UMETA(DisplayName = "ячейка лотка навыков Дружбы (6526…6537)"),
};

// Формула высоты плашки.
UENUM()
enum class EAsdaTipHeightMode : uint8
{
	// ПО СОДЕРЖИМОМУ (наш способ): верх тела + строк·высотаСтроки + нижний отступ.
	//   ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА, уже принятое заходом мини-статуса: эталонные формулы опираются
	//   на метрики D3D-растеризации оригинала и в UE дают «рамку с пустотой снизу».
	ByContent  UMETA(DisplayName = "по содержимому (по умолчанию)"),
	// Эталон `ItemTooltip_Build`: n = строк+2; h = глиф·n + интервал·(n−1) + 15; окно = h + 12.
	ItemLegacy UMETA(DisplayName = "эталон тултипа предмета"),
	// Эталон `uiHelpEntryLayoutAndShow@0xEA6EC0`: h = 16·(строк+1) + 12.
	BuffLegacy UMETA(DisplayName = "эталон подсказки эффекта"),
	// Эталон `tutorialPopupLayoutLarge@0xEB6EE0`: h = интервал·(L+1) + высотаСтроки·(L+2) + 52.
	PlusPackLegacy UMETA(DisplayName = "эталон подсказки Plus Package"),
};

// Метрики одной укладки. Все значения — из эталона, см. комментарии у полей.
struct FAsdaTipMetrics
{
	// >0 — ширина ПАНЕЛИ задана жёстко (эталон `ItemTooltip_Build`: `pTooltipMgr->propMap = 250`
	//   для обычного предмета — ширина НЕ зависит от текста). 0 — считать по тексту.
	float FixedWidth = 0.f;
	// Прибавка к clamp(ширинаТекста, MinW, MaxW): 38 у тултипа предмета/Plus Package,
	//   27 у подсказки эффекта и `itemTooltipBuildByCode`.
	float WidthAdd = 38.f;
	float MinW = 90.f;     // эталонный нижний кламп
	float MaxW = 218.f;    // эталонный верхний кламп (он же ширина переноса)
	int32 FontPx = 8;      // кегль тега <FS##>; база DT_AsdaRichStyle = 14 px, весь UI импортирован с 8
	int32 ExtraLines = 2;  // эталон: строк = перенос + ExtraLines (2 у тултипа, 1 у `itemTooltipBuildByCode`)
	EAsdaTipHeightMode HeightMode = EAsdaTipHeightMode::ByContent;
	// Константы эталонных формул (сняты из live.dmp `g_ItemTooltip@0x2C433B0`).
	float LegacyLineGap    = 3.f;    // filler[268] / this[67]
	float LegacyLineHeight = 12.f;   // filler[264] / this[66]
	float LegacyChromeH    = 52.f;   // «+52» у Plus Package; у тултипа предмета — 15 (см. ItemLegacy)

	// ★ «ЭЛИТНЫЙ» ПОДВАЛ (эталон): у предмета с `categoryFlag == 3` включается значок премиума
	//   (5007 / 5057) — `itemTipWinSetModeVisible(mgr, режим, 1)`, а `Tooltip_LayoutNearCursor`,
	//   увидев его, показывает НИЖНЮЮ плашку (5005 / 5055) с текстом (5006 / 5056) = нотис 109832
	//   и считает её высоту как `глиф·n + интервал·(n−1)` по числу перенесённых строк.
	bool    bPremiumFooter = false;
	FString FooterText;
};

// Плашка-тултип: одно окно JUI + кэш его узлов + вся раскладка.
UCLASS()
class ASDAREBORN_API UAsdaTipPanel : public UObject
{
	GENERATED_BODY()

public:
	// Привязка к окну. WindowId — ключ реестра `UAsdaWindowSubsystem` (окно создаётся ОДИН раз и
	//   переиспользуется всеми, кто спросит тот же id — как единственный `g_ItemTooltip` эталона).
	//   TitleName/BodyName — имена узлов JUI (idc), PlanePrefix — префикс 9 планов рамки.
	//   HideOtherPrefixes — узлы, которые надо свернуть (вторая/третья плашка окна, болты).
	bool Bind(UGameInstance* GI, FName WindowId, const TCHAR* WbpPath, int32 ZOrder,
	          const TCHAR* TitleName, const TCHAR* BodyName, const TCHAR* PlanePrefix,
	          const TArray<FString>& HideOtherPrefixes);

	// Узлы «элитного» подвала (значок премиума + нижняя плашка с текстом). Любой из них может
	//   отсутствовать в WBP — тогда подвал просто не рисуется.
	void BindFooter(const TCHAR* PremiumIconName, const TCHAR* FooterPlanePrefix, const TCHAR* FooterTextName);

	bool IsBound() const { return Widget != nullptr; }
	UUserWidget* GetWidget() const { return Widget; }
	bool IsShown() const { return bShown; }
	FVector2D GetLastSize() const { return LastSize; }
	// Последняя ВЫСТАВЛЕННАЯ позиция в единицах вьюпорта (то, что ушло в SetPositionInViewport).
	//   ★ Читать её, а не `GetCachedGeometry().GetAbsolutePosition()`: геометрия отстаёт на кадр и
	//   живёт в АБСОЛЮТНОМ пространстве десктопа — смешивать эти две системы нельзя.
	FVector2D GetLastViewportPos() const { return LastPos; }

	// ── ВЛАДЕЛЕЦ (см. EAsdaTipOwner) ─────────────────────────────────────────────────────────────
	EAsdaTipOwner GetOwner() const { return Owner; }
	void SetOwner(EAsdaTipOwner InOwner) { Owner = InOwner; }
	// ★ 31.10.2026: окно реестра, над которым показали плашку (`asda.UI.TipFollowsWindow`): ушло с экрана — плашка гаснет.
	//   NAME_None — показ не над окном (консоль), такая плашка за окном не следит. `Hide()` сбрасывает.
	FName GetSourceWindow() const { return SourceWindow; }
	void SetSourceWindow(FName InWindow) { SourceWindow = InWindow; }
	// Скрыть, ТОЛЬКО если плашкой владеет `InOwner`. Возвращает true, если реально скрыли.
	bool HideFor(EAsdaTipOwner InOwner);

	// Диагностика одной строкой: окно, видимость, владелец, размер/позиция, геометрия, узлы.
	//   Печатается командой `asda.ItemTip.State` и при `asda.ItemTip.Debug 1` на каждом показе.
	FString DebugState() const;

	// Уложить и показать. Title/Body — СЫРЫЕ строки с inline-тегами (<FC=..>/<FS..>/</F>).
	//   TitleArgb (0xAARRGGBB, sRGB) — БАЗОВЫЙ цвет заголовка, эталон `CControl_SetField923`;
	//   inline-теги внутри Title его перекрывают (так эталон рисует «+15% [Шедевр] Имя»).
	void Show(const FString& Title, uint32 TitleArgb, const FString& Body, const FAsdaTipMetrics& M);

	void Hide();

	// Поставить левый-верх плашки в координаты вьюпорта (уже поделённые на DPI вызывающим).
	void SetViewportPos(const FVector2D& Pos);

	// Уложить у курсора: эталон `Tooltip_LayoutNearCursor` — смещение +32/+32 от курсора,
	//   затем `uiClampPopupToScreen` (отступ 5 px от краёв). ExtraW — ширина «прицепленного»
	//   справа окна сравнения (эталон складывает ширины перед клампом).
	void LayoutAtCursor(float ExtraW = 0.f);

	// ── общие статические хелперы (раньше жили копиями в подсистемах) ─────────────────────────
	// Пересборка 9-slice под новый размер (эталон `uiPanel9SliceLayout`).
	// ★ Origin — левый верхний угол рамки В КООРДИНАТАХ РОДИТЕЛЯ. Ноль (по умолчанию) годится, когда
	//   девять планов лежат в СВОЁЙ группе с началом в (0,0) — так у тултипа, мини-статуса и чата.
	//   Попап наград 540 импортёр положил девятью планами ПРЯМО в корневую канву окна (началo
	//   140,179), поэтому ему нужен явный Origin, иначе рамка уехала бы в угол экрана.
	// ★ ЦЕНТР (индекс 4) МОЖЕТ БЫТЬ nullptr — и это НЕ ошибка. У скинов-«обводок» (`Box_Orange3Stroke`
	//   и т. п.) средний план объявлен `PlaneAlwaysInvisible = "1"`, а импортёр невидимые планы
	//   отбрасывает (`asda_jui_generic_umg.py:366`), поэтому в WBP их ровно ВОСЕМЬ. Ширины колонок и
	//   высоты строк берутся из УГЛОВЫХ планов (0, 2, 6), так что отсутствующий центр на математику
	//   не влияет — он просто не двигается. Все остальные восемь планов обязаны быть, иначе
	//   рамка получилась бы разорванной, и функция ничего не делает.
	static void Resize9Slice(const TArray<TObjectPtr<UImage>>& Nine, const FVector2D& NewSize,
		const FVector2D& Origin = FVector2D::ZeroVector);
	// Число строк С УЧЁТОМ ПЕРЕНОСА (эталон `textFontCountWrappedLines_Active@0x12C25D0`).
	static int32 CountWrappedLines(const FString& Plain, const struct FSlateFontInfo& Fnt, float WrapW);

private:
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget>     TitleW = nullptr;   // 5001 / 5051
	UPROPERTY(Transient) TObjectPtr<UWidget>     BodyW  = nullptr;   // 5002 / 5052 (создаётся в рантайме)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> Planes;          // 9 планов рамки
	UPROPERTY(Transient) TObjectPtr<UWidget>     PremiumIcon = nullptr;   // 5007 / 5057
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> FooterPlanes;         // планы нижней плашки 5005 / 5055
	UPROPERTY(Transient) TObjectPtr<UWidget>     FooterText = nullptr;    // 5006 / 5056

	// Шрифт заголовка снимается ОДИН РАЗ при привязке: `ApplyFormatted` заменяет UTextBlock на
	//   URichTextBlock, и второго шанса прочитать кегль/семейство уже не будет.
	FSlateFontInfo TitleFont;

	// ★★★ ПАРАМЕТРЫ ПРИВЯЗКИ, СОХРАНЁННЫЕ ДЛЯ ПЕРЕПРИВЯЗКИ (разбор — у `Bind` в .cpp).
	//   `UAsdaWindowSubsystem::CloseAll()` (порт `uiCloseAllWindows(g_pUIManager)`) выкидывает ВСЕ
	//   окна из вьюпорта и чистит реестр. Плашка тултипа — тоже окно реестра (`ITEM_TIPWIN` 5000),
	//   поэтому после любого `CloseAll` наш `Widget` остаётся ненулевым, но уже НИ К ЧЕМУ НЕ
	//   ПРИКРЕПЛЁН: `Show()` отрабатывает без ошибок, а на экране ничего нет.
	FName            BoundWindowId;
	FString          BoundWbp;
	int32            BoundZ = 0;
	FString          BoundTitleName, BoundBodyName, BoundPlanePfx;
	TArray<FString>  BoundHidePfx;
	// ⚠ Имена полей НЕ должны совпадать с параметрами `BindFooter` (иначе C4458 «hides class member»).
	FString          FooterIconIdc, FooterPlaneIdc, FooterTextIdc;
	bool             bFooterBound = false;

	bool          bShown = false;
	FVector2D     LastSize = FVector2D::ZeroVector;
	FVector2D     LastPos  = FVector2D::ZeroVector;
	EAsdaTipOwner Owner    = EAsdaTipOwner::None;
	FName         SourceWindow;   // окно-источник показа (см. GetSourceWindow)

	// JUI ITEM_TIPCONTENS: AlignXPos="5 8 27" ⇒ отступ 8 по бокам; верх тела 20 (заголовок y=2 h=18).
	static constexpr float kBodyTop    = 20.f;
	static constexpr float kBodyBottom = 8.f;
	static constexpr float kBodyInsetX = 8.f;
};
