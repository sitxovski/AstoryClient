// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHairShopSubsystem.cpp — ПАРИКМАХЕРСКАЯ, окно `HAIRSHOP_WIN` (15000) + панель 15168.
//   Реверс, адреса, дефекты и таблица нотисов — в шапке `Public/UI/AsdaHairShopSubsystem.h`
//   и в `Reports/npc_hairshop_30029.md`. Ни одно число и ни один текст не выдуманы.
#include "UI/AsdaHairShopSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"

#include "Blueprint/WidgetLayoutLibrary.h"   // размер вьюпорта для расчёта числа строк
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "TimerManager.h"
// ★ Живой зонд «кто съел клик»: путь виджетов Slate под точкой + настоящий клик по SButton.
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/Input/SButton.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейт 3 `actionExecuteSlotAbility` — окно обмена 2008 открыто
#include "Npc/AsdaNpcDialogSubsystem.h"   // ★ волна В6 ремесла (W5-7): `dialogOnConfirmClose` при входе и 5045 при выходе
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): разбор аргументов самотеста (`quit` не путается с числом)
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): имя/описание причёски на языке сессии
#include "UI/AsdaSystemOptionSubsystem.h"      // ★ 25.10.2026 (ревизия sit108, М-3): гейт «персонаж занят» (`objStateAllowsRender`) до стопа сцены
#include "World/AsdaPlayerCharacter.h"          // ★ 24.10.2026 (выход из сидения 1:1): `ApplyEtalonSceneStop` — стоп/«встать» при входе в сцену
#include "GameFramework/PlayerController.h"    // ★ 24.10.2026: свой павн для `ApplyEtalonSceneStop`

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHair, Log, All);

bool AsdaRelayAnchorSplitOn();   // ключ `asda.UI.RelayAnchorSplit` (UI/AsdaUiHelpers.cpp) — свой GC-якорь реле у каждого окна

namespace
{
	const FName  kWindowId(TEXT("HAIRSHOP_WIN"));
	const TCHAR* kWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_HAIRSHOP_WIN");
	const FName  kHairTable(TEXT("hairtable"));
	// Имя-ключ нашего примерочного оверрайда в общей точке `UAsdaWorldSubsystem::PushEquipTryOn`.
	const FName  kTryOnOwner(TEXT("HairShop"));

	// ── Адрес доната: эталон `DonateButton_OpenURL@0xCF2480`; в НАШЕМ клиенте URL уже заменён
	//    (`AsdaStory_ida.c:545272`), тот же, что у кнопки 6276 магазина 6000.
	const TCHAR* kDonateUrl = TEXT("");   // ★ 27.10.2026 (владелец): ссылка на area-game.ru снята — «Пополнить» пока ничего не открывает

	// ── Статические подписи (`CUIHairShop_InitControls@0xCEC1B0`, `ItemDetailWnd_Fill@0xCF0E70`) ──
	constexpr int32 kLocCashLbl   = 12904;  // 15163 «Сейчас GXP»
	constexpr int32 kLocPurAsk    = 13002;  // 15177 «Ты точно хочешь именно такую прическу?»
	constexpr int32 kLocPurLvLbl  = 8487;   // 15175 «Уровень»  (`ida.c:594868`)
	constexpr int32 kLocPurMacLbl = 2303;   // 15176 «Цена»     (`ida.c:594876`)
	constexpr int32 kLocLevelSuf  = 13000;  // «Уровень () и выше» — суффикс карточки, формат "%d %s"
	constexpr int32 kLocGuide0    = 6960;   // 15190 «-»                       (D9: тут же скрывается)
	constexpr int32 kLocGuide1    = 6961;   // 15191 «[Предмет нельзя вернуть…]»(D9)
	constexpr int32 kLocPreCash   = 12959;  // 40002 «Деньги в подарок»        (D11: мёртвый узел)
	// ★★ ПОДПИСЬ КНОПКИ 15182. РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-28: «Как было» → «Отменить выбор».
	//   ⚠ ИСТОЧНИК НЕ `GameText.csv`. У 15182 в `.JUI` СТАТИЧЕСКАЯ подпись `CaptionText = "원래대로"`,
	//   и русский текст ей ставит импортёр по словарю `Tools/kor_ru_ui.json` — то есть строки-нотиса
	//   у этой кнопки в оригинале нет вовсе (эталон `CUIHairShop_InitControls` текст 15182 не пишет).
	//   ★ КЛЮЧ `원래대로` ОБЩИЙ: тем же словом подписана `SHELLITEM_STORE_BUT00` премиум-молла
	//   (`gamemain_jui.ini:119175` и `:173818` — ровно два контрола на весь файл). Значит правка
	//   словаря переименовала бы и ЧУЖУЮ кнопку, да ещё потребовала бы переимпорта UI (запрещён).
	//   ⇒ Разводим правильно: заводим СВОЮ строку в `GameText.csv` (диапазон 900000+ уже занят
	//   проектными строками 900001/900002) и ставим её В РАНТАЙМЕ ровно так же, как ставятся
	//   остальные подписи этого окна (15163/15175/15176/15177). Чужая кнопка не тронута.
//   ★ id взят из проектного диапазона выше 990000 — правило зафиксировано в changelog
//   (там же прецедент: 990001 «Купить» для кнопки бустер-шопа).
	// Подписи кнопок ОБЩЕЙ модалки 1559 — их же ставим на 15178/15179 (см. пункт 2).
	constexpr int32 kLocBtnOk     = 6857;   // «OK»    (общая с магазинами и гачей — НЕ правим)
	constexpr int32 kLocBtnCancel = 2289;   // «Отмена» (общая — НЕ правим)
	// ★ Цвета отметки «Куплено»: золотая подложка + чёрная подпись (формулировка владельца).
	//   Заданы в ЛИНЕЙНОМ пространстве через общую точку `AsdaUi::SrgbHexToLinear` — как все
	//   остальные цвета порта (закон `ui-srgb-gamma-rule`), а не «на глаз».
	const FLinearColor kOwnedGold = AsdaUi::SrgbHexToLinear(TEXT("FFD9A521"));  // золото подложки
	const FLinearColor kOwnedText = AsdaUi::SrgbHexToLinear(TEXT("FF101010"));  // чёрная подпись
	constexpr int32 kLocOwned     = 12546;  // «Куплено» — готовая строка, читателей в коде нет
	constexpr int32 kLocOriginBtn = 990010; // 15182 «Отменить выбор» (своя строка порта, id > 990000)

	// ── Нотисы (таблица «событие → id → канал», §11 отчёта; канал у ВСЕЙ системы — ЧАТ) ──
	constexpr int32 kLocHello1    = 13013;  // открытие, 1-я строка
	constexpr int32 kLocHello2    = 13014;  // открытие, 2-я строка
	constexpr int32 kLocBye       = 13015;  // выход
	constexpr int32 kLocPanelBusy = 13008;  // действие при открытой панели 15168
	constexpr int32 kLocNoAnswer  = 1026;   // busy-флаг: «Сервер не отвечает.»
	constexpr int32 kLocNoRecord  = 13012;  // запись причёски не найдена
	// Коды результата 5471 → строка (§5.4). Индекс = код; 1 (успех) — своя ветка.
	constexpr int32 kResLoc[6] = { 13007, 13005, 13003, 13017, 13018, 12165 };

	// ═══════════ КЛЮЧИ ОТКАТА ДЕФЕКТОВ ОРИГИНАЛА (закон `fix-original-defects`) ═══════════
	// D1 ★★★ ТЕЧЁТ BUSY-ФЛАГ. `boosterShop_SendOpenRequest@0xCF1890` ставит
	//   `g_pHairShopWnd->gap_298[4] = 1` (`ida.c:545224`), а приёмник `hairShop_HandleResult`
	//   сбрасывает ДРУГОЙ флаг — `g_pShopWnd[2068]` (`shop_trade.c:6219`). `gap_298[4]` обнуляет
	//   только `uiHairShopWindowResetState@0xCEED50` (открытие/закрытие окна) ⇒ после ПЕРВОЙ
	//   покупки все следующие в этой сессии отбиваются нотисом 1026 «Сервер не отвечает».
	static ASDA_DEV_CVAR(int32, CVarBusyFlagLeak,
		TEXT("asda.HairShop.BusyFlagLeak"), 0,
		TEXT("1 = дефект оригинала D1: busy-флаг покупки причёски НЕ снимается приёмом 5471 "
		     "(вторая покупка в сессии отбивается нотисом 1026); 0 = исправлено."),
		ECVF_Default);

	// D2 ★★★ ЦЕНА НА ЭКРАНЕ ≠ СПИСАНИЮ — расхождение ДАННЫХ, УСТРАНЕНО СВЕДЕНИЕМ ТАБЛИЦ 2026-08-28.
	//   Витрину рисует клиентская `hairtable` (+40), деньги списывает сервер по своей
	//   `ItemTable_Info/HairTypeTable.txt` (+69). Замер показал 198 расхождений из 228 строк ровно
	//   по цене; клиентская таблица приведена к серверной инструментом `Tools/asda_hairtable_sync.py`
	//   (он же сверяет все шесть общих полей). ⚠ ЧИСЕЛ ЗДЕСЬ НЕТ И БЫТЬ НЕ ДОЛЖНО: серверной
	//   таблицы клиент в рантайме не видит, сверка — задача инструмента, а не кода.
	//   Ключ оставляет напоминание в журнале, чтобы правило «правишь ассортимент — гони сверку»
	//   не потерялось (это же понадобится будущему редактору магазинов).
	static ASDA_DEV_CVAR(int32, CVarPriceWarn,
		TEXT("asda.HairShop.PriceMismatch"), 1,
		TEXT("1 = напоминать в журнале, что симметрию цен клиент/сервер поддерживает "
		     "Tools/asda_hairtable_sync.py; 0 = молчать."),
		ECVF_Default);

	// D7 чек-бокс фильтра 15002 `Visible = "0"` в данных, хотя подпись 15003 видна и код его
	//   привязывает — переключить фильтр «только доступные мне» нечем.
	static ASDA_DEV_CVAR(int32, CVarFilterCheckHidden,
		TEXT("asda.HairShop.FilterCheckHidden"), 1,
		TEXT("1 = как оригинал (дефект D7): чек-бокс фильтра 15002 скрыт; 0 = показать его."),
		ECVF_Default);

	// D8 карточки 11…14 скрыты в данных — они НЕ ВЛЕЗАЮТ в подложку 15004 (446×520): строка 5
	//   начинается на y = 505, строка 6 — на 603. Оставляем 1:1.
	static ASDA_DEV_CVAR(int32, CVarRows11to14,
		TEXT("asda.HairShop.Rows11to14Hidden"), 1,
		TEXT("1 = как оригинал: карточки 11…14 скрыты (они выходят за подложку 446×520); "
		     "0 = показать все 14 (вылезут за белую рамку)."),
		ECVF_Default);

	// D8-b ★ ПАГИНАЦИЯ. Эталон раскладывает страницу по 14 и считает страницы по 14, хотя видимы
	//   только 10 ⇒ четыре записи на каждой странице НЕДОСТИЖИМЫ. По умолчанию считаем по числу
	//   ВИДИМЫХ карточек — тогда доступны все 198 включённых причёсок.
	static ASDA_DEV_CVAR(int32, CVarFixPageSize,
		TEXT("asda.HairShop.FixPageSize"), 1,
		TEXT("1 = страница = число ВИДИМЫХ карточек (ничего не теряется); "
		     "0 = как оригинал: страница 14, из них 4 карточки невидимы."),
		ECVF_Default);

	// D9 оба предупреждения (15190 «-» и 15191 «[Предмет нельзя вернуть или обменять]») эталон
	//   ставит и ТУТ ЖЕ скрывает (`ui_core.c:45222-45223`) — игрок не видит, что покупка необратима.
	static ASDA_DEV_CVAR(int32, CVarGuideTextHidden,
		TEXT("asda.HairShop.GuideTextHidden"), 1,
		TEXT("1 = как оригинал (дефект D9): предупреждения 15190/15191 скрыты; 0 = показать."),
		ECVF_Default);

	// ═══ ★★ РАСШИРЕНИЕ (не дефект оригинала): СЕТКА ПОД ВЫСОТУ ЭКРАНА ═══════════════════════════
	//   ⚠ ЧТО В ОРИГИНАЛЕ. Окно 15000 задаёт число карточек ЖЁСТКО: `refineWindowRefreshAll`
	//     (`item_enhance.c:1167` и `:1177`) дважды идёт `for (i = 0; i < 14; ++i)`, а «лесенки»
	//     `uiRowCountByScreenHeight@0xB…` / `uiList_GetVisibleRows` (ими считают ячейки склад,
	//     инвентарь и магазин бустеров — `item_inventory.c:20312, 21105, 21118`) в коде
	//     парикмахерской НЕТ ВОВСЕ. Отрицание доказано вторым методом: grep по ВСЕМ трём диапазонам
	//     её функций (`ui_core.c` 45000-45700, `item_enhance.c` 1100-1400, `item_inventory.c`
	//     34400-34800) — 0 попаданий.
	//   ⇒ Растягивание списка — РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-28 («окно не использует место»),
	//     поэтому ключ отката возвращает РОВНО оригинал (14 гнёзд, из них видимы 10).
	//   ★ Формула — не подгонка: `frameH = 98·строк + 30` при 5 строках даёт 520, то есть В ТОЧНОСТИ
	//     `Size` подложки `HAIR_ITEMBASE` в `.JUI`, а зазор до пагинации 534 − 520 = 14. На базовой
	//     высоте экрана 768 расчёт возвращает 5 строк ⇒ раскладка совпадает с оригинальной.
	static ASDA_DEV_CVAR(int32, CVarFitScreen,
		TEXT("asda.HairShop.FitScreen"), 1,
		TEXT("1 = число карточек считается ПО ВЫСОТЕ ЭКРАНА (расширение порта; на 768 даёт ровно "
		     "оригинальные 5 строк); 0 = как оригинал: жёстко 14 гнёзд, видимы 10."),
		ECVF_Default);

	// ★ БЕЙДЖ УРОВНЯ `BoxIcon_Level` (красный квадратик с латинской «L») — узел
	//   `HAIR_ITEM_LV{nn}_Icon` в `.JUI` (IDNum = −1, ребёнок надписи уровня 15013+9i).
	//   ЧТО В ОРИГИНАЛЕ: `refineFillMaterialSlotInfo@0xCF0660` (`item_enhance.c:1349-1360`) пишет
	//   в надпись «N Уровень () и выше» ЛИБО пустую строку (при `rec+35 == 0`), а видимость самого
	//   бейджа НЕ ТРОГАЕТ — значит показывает его ВСЕГДА. А `RequiredLevel` в живых данных равен 0
	//   у ВСЕХ 228 записей (дефект D12 отчёта) ⇒ игрок видит красный значок уровня БЕЗ ЧИСЛА.
	//   ИСПРАВЛЕНО: гасим бейдж, когда числа нет. Ключ возвращает поведение оригинала.
	static ASDA_DEV_CVAR(int32, CVarLevelBadgeAlways,
		TEXT("asda.HairShop.LevelBadgeAlways"), 0,
		TEXT("1 = как оригинал: красный бейдж уровня (BoxIcon_Level, «L») виден всегда, даже когда "
		     "требуемый уровень 0 и числа рядом нет; 0 = показывать только вместе с числом."),
		ECVF_Default);

	// ★ ШАГ 7 ЭТАЛОНА — СНЯТИЕ ШЛЕМА НА ВХОДЕ (`CCharacter_LoadHelmetPart(char, 0)`,
	//   `ui_core.c:45658`), возврат — в `charAppearance_ApplyHairHelmet@0xCF2940` при выходе.
	//   Реализовано ОБЩЕЙ ТОЧКОЙ «примерочный оверрайд экипировки»
	//   (`UAsdaWorldSubsystem::PushEquipTryOn/PopEquipTryOn`), а не частным хаком: примерка нужна
	//   и другим системам (запись #42а `PORT_PENDING_ON_SYSTEMS.md`).
	static ASDA_DEV_CVAR(int32, CVarHideHelmet,
		TEXT("asda.HairShop.HideHelmet"), 1,
		TEXT("1 = как эталон (шаг 7): на входе в салон шлём снимается, на выходе возвращается; "
		     "0 = не трогать шлем."),
		ECVF_Default);

	// ★★★★★ КЛЮЧ ВОСПРОИЗВЕДЕНИЯ ДЕФЕКТА ПОРТА «кнопки Купить не нажимаются» (2026-08-28).
	//   Прозрачная плашка-хит на всю карточку раньше опускалась вниз вызовом `ShiftChild`, а тот
	//   переставляет только UPROPERTY-массив `Slots` и НЕ трогает уже построенный `SConstraintCanvas`
	//   ⇒ плашка оставалась САМОЙ ВЕРХНЕЙ и съедала клик у кнопки «Купить». Теперь порядок задаёт
	//   `ZOrder = −1`, который канвас учитывает при раскладке всегда.
	//   Ключ возвращает прежний (сломанный) ZOrder — им дефект воспроизводится живым хит-тестом
	//   `asda.HairShop.Probe <c> slate`: с 1 кнопки «ПЕРЕКРЫТА», с 0 — «в пути ЕСТЬ».
	static ASDA_DEV_CVAR(int32, CVarLegacyCardHitZ,
		TEXT("asda.HairShop.LegacyCardHitZ"), 0,
		TEXT("1 = воспроизвести дефект порта: плашка-хит карточки лежит ПОВЕРХ кнопки «Купить» "
		     "(ZOrder 0, как до правки) и съедает клик; 0 = исправлено (ZOrder −1)."),
		ECVF_Default);

	// ★ ПУНКТ 1 ВЛАДЕЛЬЦА: ДВОЙНОЕ ПОДТВЕРЖДЕНИЕ УБРАНО.
	//   В оригинале путь покупки ДВУХШАГОВЫЙ: кнопка «Купить» 15009+9i открывает панель 15168
	//   («Ты точно хочешь именно такую прическу?», `uiHairShopWndShow@0xCF0D10`), и только её
	//   кнопка 15178 зовёт `upgradeUiFillConfirmPanel@0xD7BD90` — ОБЩУЮ модалку 1559.
	//   ⇒ Игрок подтверждал покупку ДВАЖДЫ. Решение владельца 2026-08-28: оставить только 1559.
	//   ⚠ ЧТО ТЕРЯЕТСЯ ВМЕСТЕ С ПАНЕЛЬЮ: единственное место, где показывалось ОПИСАНИЕ причёски
	//     (контрол 15174 ← `hairtable.Description`, поле +111). Всё остальное у панели дублирует
	//     модалку (имя, цена, уровень) либо скрыто самим оригиналом (бейджи NEW/HOT, guide-тексты
	//     15190/15191 — дефект D9). Примерка причёски происходит ДО панели, в самом клике по
	//     карточке, поэтому она не теряется.
	static ASDA_DEV_CVAR(int32, CVarLegacyConfirmPanel,
		TEXT("asda.HairShop.LegacyConfirmPanel"), 0,
		TEXT("1 = как оригинал: «Купить» открывает панель 15168, и только её «Да» ведёт к модалке "
		     "1559 (двойное подтверждение); 0 = сразу модалка 1559."),
		ECVF_Default);

	// ★ ПУНКТ 4 ВЛАДЕЛЬЦА: отметка «Куплено» на причёске, которая уже надета на персонаже.
	//   В оригинале такой отметки НЕТ вовсе — расширение, ключ гасит его целиком.
	static ASDA_DEV_CVAR(int32, CVarMarkOwned,
		TEXT("asda.HairShop.MarkOwned"), 1,
		TEXT("1 = текущая причёска персонажа помечается в списке (золотая подложка, чёрная подпись, "
		     "кнопка «Куплено» неактивна); 0 = как оригинал, без отметки."),
		ECVF_Default);

	// ★★★ ПУНКТ 7 ВЛАДЕЛЬЦА: БАЛАНС ПОСЛЕ ПОКУПКИ БРАТЬ ЗАПРОСОМ, А НЕ ИЗ ОТВЕТА.
	//   Живые числа владельца: 592786 → покупка → показано 592748 → «Проверка» (5466) → 592767.
	//   Значит поле «остаток» пакета 5471 (+5, i32) НЕ СХОДИТСЯ с базой ровно на одну цену.
	//   Разбор сервера: 5471 несёт КЭШ в памяти (`v22 = sub_43FC20`, `GameServer.c:94779`), который
	//   перед этим уменьшили на цену (`sub_43FC70(..., price, 0, 0)` идёт в ветку «вычесть»,
	//   `GameServer.c:26817-26827`), а 5467 приходит из БАЗЫ и кэш ЗАНОВО ПРОШИВАЕТ
	//   (`sub_43FC70(..., 1, 1u)` — ветка «присвоить», `sub_4F4810@0x4F4810`). Кэш и база
	//   расходятся, и авторитет — 5467.
	//   ⇒ После успешной покупки шлём 5466 и показываем то, что вернёт база.
	static ASDA_DEV_CVAR(int32, CVarTrustResultBalance,
		TEXT("asda.HairShop.TrustResultBalance"), 0,
		TEXT("1 = как оригинал: баланс берётся из поля +5 пакета 5471 (может расходиться с базой); "
		     "0 = после покупки перезапрашивать баланс 5466 и показывать базу."),
		ECVF_Default);

	// D5 отказ гейтов открытия у оригинала молчалив; заготовленный текст 13011 не эмитируется
	//   НИГДЕ (проверено grep'ом по всем 30 файлам эталона и по мастер-декомпилу — 0 попаданий).
	//   В чат его не тащим (это было бы выдумкой), но причину пишем в журнал.
	static ASDA_DEV_CVAR(int32, CVarOpenNoticeLog,
		TEXT("asda.HairShop.OpenNotice"), 1,
		TEXT("1 = писать причину отказа во входе в журнал (дефект D5 оригинала — молчаливый отказ); "
		     "0 = молчать, как оригинал."),
		ECVF_Default);
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaHairShopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Cards.SetNum(kCards);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			HResult  = Net->OnHairShopResult.AddUObject(this, &UAsdaHairShopSubsystem::OnHairResult);
			HBalance = Net->OnPremiumBalance.AddUObject(this, &UAsdaHairShopSubsystem::OnBalance);
		}
	}
	// ★ ПУНКТ 5 ВЛАДЕЛЬЦА («окно не закрывается при отходе от NPC»). Правило ОРИГИНАЛА — закрытие
	//   по НАЧАЛУ ДВИЖЕНИЯ, а не по дистанции: `UI_resetAllWindowsAndLeave_5045@0xD995C0` зовут
	//   переходы состояния игрока. В порту для этого уже есть ОБЩИЙ реестр — регистрируемся в нём
	//   одной строкой, своего механизма не заводим (закон `port-shared-points`).
	if (UGameInstance* GI2 = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI2->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->RegisterNpcWindowCloser(kWindowId,
				FAsdaNpcWindowCloser::CreateWeakLambda(this, [this]() { CloseWindow(); }));
			// ★ 10.10.2026 [ОРИГИНАЛ]: ESC — своя роспись (эталонная ветка события 0x15, разбор у `EscClose`); проход ESC
			//   берёт её раньше закрывателя «уход от NPC» (`UAsdaWindowSubsystem::ExecuteOwnClose`).
			WM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaHairShopSubsystem::EscClose));
		}
	}
	UE_LOG(LogAsdaHair, Log,
		TEXT("[парикмахерская] подсистема 15000 поднята: %d карточек (видимых %d), опкоды 5470/5471/5472"),
		kCards, kCardsFit);
}

void UAsdaHairShopSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnHairShopResult.Remove(HResult);
			Net->OnPremiumBalance.Remove(HBalance);
		}
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->UnregisterNpcWindowCloser(kWindowId);
			WM->UnregisterEscCloser(kWindowId);
			WM->CloseWindow(kWindowId);
		}
	}
	Widget = nullptr;
	bOpen = bReady = bPanelOpen = false;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaHairShopSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseWindow();
	bPanelOpen = false;
	bOnlyMine = false;
	Page = 0; MaxPage = 0; SelCard = -1; PanelHairId = -1;
	CashGxp = 0;
	// Снимок «своей» причёски: у другого персонажа он другой.
	// ★ 10.10.2026 (МЕЛК-3 ревизии barber_minors): `bOwnedStyleSaved` здесь НЕ сбрасывается — образцы «Купить» (цвета и
	//   подпись) — свойство АССЕТА, а не персонажа. Сброс при живом виджете перехватывал бы образцы с карточки 01 в виде
	//   «Куплено», и «Куплено» встало бы на всех карточках. Флаг сбрасывается только вместе с виджетом (`EnsureWidget`).
	SavedStyle = -1; SavedColor = -1; CachedOwnedId = -1;
	ListIds.Reset();
	(void)Reason;
}

FString UAsdaHairShopSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

// ★ Канал у ВСЕЙ системы — ЧАТ (`CChat_AddTokenizedLines(g_pChatWnd, txt, −11514, 10000, 0)`),
//   §11 отчёта. Маршрут задаёт `DT_MessageRoutes`, окно кода не знает.
void UAsdaHairShopSubsystem::NoticeChat(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::Trade, LocId);
}

// ═══════════════════════════════ ДАННЫЕ ════════════════════════════════════════════════════════
const FAsdaHairtableRow* UAsdaHairShopSubsystem::RowById(int32 HairId) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UDataTable* DT = IT ? IT->GetDataTable(kHairTable) : nullptr;
	if (!DT) { return nullptr; }
	// ★ Эталонный `findRecordById@0xCED9F0` адресует ПРЯМО (`base + 312*id`) и содержит цикл,
	//   тело которого `i` не использует (дефект D10). У нас ключ строки = порядковый номер записи,
	//   поэтому сверяем поле `Id` явно — «дырявый» id вернёт nullptr, а не ЧУЖУЮ запись.
	for (const TPair<FName, uint8*>& It : DT->GetRowMap())
	{
		const FAsdaHairtableRow* R = reinterpret_cast<const FAsdaHairtableRow*>(It.Value);
		if (R && R->Id == HairId) { return R; }
	}
	return nullptr;
}

// ★ [ПОРТ] 09.10.2026 (мультиязычность): поле строки `hairtable` для показа игроку — на языке сессии (оверлей `hairtable`,
//   ключ `<_row>.<Field>`). `R` — строка самой таблицы (из `RowById`).
static FString HairText(const UGameInstance* GI, const FAsdaHairtableRow* R, const TCHAR* Field, const FString& Ru)
{
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	return AsdaLangRows::Text(IT ? IT->GetDataTable(kHairTable) : nullptr, R, Field, Ru);
}

// Порт `inventoryRebuildFilteredList@0xCEF480` + `invBagBuildLevelFilteredGrid@0xCEFB30`:
//   отбор по ПОЛУ (`+34`) и признаку «включена» (`+36`); фильтр по УРОВНЮ (`+35`) — только когда
//   взведён чек-бокс 15002 «только доступные мне».
void UAsdaHairShopSubsystem::BuildList()
{
	ListIds.Reset();
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UDataTable* DT = IT ? IT->GetDataTable(kHairTable) : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!DT)
	{
		UE_LOG(LogAsdaHair, Error, TEXT("[парикмахерская] DT_hairtable недоступна — список пуст"));
		MaxPage = 0;
		return;
	}
	const int32 WantGender = Net ? (int32)Net->ResolveSelfGender() : 1;   // hairtable: 1 = М, 2 = Ж
	const int32 MyLevel    = AsdaSelf::GetLevel(Net);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5c): уровень — общая точка `AsdaSelf::GetLevel` (до 4005/4036 — уровень выбранного персонажа, а не 0)

	int32 OffGender = 0, OffDisabled = 0, OffLevel = 0;
	TArray<const FAsdaHairtableRow*> Rows;
	for (const TPair<FName, uint8*>& It : DT->GetRowMap())
	{
		if (const FAsdaHairtableRow* R = reinterpret_cast<const FAsdaHairtableRow*>(It.Value)) { Rows.Add(R); }
	}
	// Порядок — по Id (порядок записей файла), как у эталона: он идёт по таблице подряд.
	Rows.Sort([](const FAsdaHairtableRow& A, const FAsdaHairtableRow& B) { return A.Id < B.Id; });

	for (const FAsdaHairtableRow* R : Rows)
	{
		if (R->Enabled != 1) { ++OffDisabled; continue; }
		if (R->Gender != 0 && R->Gender != WantGender) { ++OffGender; continue; }
		// ★ Поле `RequiredLevel` (в старой схеме звалось `Reserved35` — миснейм M10): `*(u8*)(rec+35) <= charLevel`
		//   (`item_inventory.c:34705`). В живых данных он 0 у всех 228 записей (дефект D12).
		if (bOnlyMine && R->RequiredLevel > MyLevel) { ++OffLevel; continue; }
		ListIds.Add(R->Id);
	}
	const int32 PerPage = FMath::Max(1, CardsPerPage());
	MaxPage = ListIds.Num() > 0 ? (ListIds.Num() - 1) / PerPage : 0;
	Page = FMath::Clamp(Page, 0, MaxPage);
	UE_LOG(LogAsdaHair, Log,
		TEXT("[парикмахерская] список: %d причёсок (пол %d) | отсеяно: выключено %d, чужой пол %d, уровень %d ")
		TEXT("| страница %d/%d по %d"),
		ListIds.Num(), WantGender, OffDisabled, OffGender, OffLevel, Page + 1, MaxPage + 1, PerPage);
}

int32 UAsdaHairShopSubsystem::CardsPerPage() const
{
	// ★ РАСШИРЕНИЕ: страница = ровно то, что разложено по высоте экрана (`ApplyGridLayout`).
	if (CVarFitScreen.GetValueOnGameThread() != 0 && GridRows > 0) { return GridRows * 2; }
	if (CVarFixPageSize.GetValueOnGameThread() == 0) { return kCards; }         // 1:1 эталон (D8)
	return (CVarRows11to14.GetValueOnGameThread() != 0) ? kCardsFit : kCards;
}

// ★★ СКОЛЬКО СТРОК ВЛЕЗАЕТ. Все слагаемые — из `.JUI` (см. константы в .h):
//     сверху  `kBaseTop` = 69 (Origin.y подложки 15004);
//     сетка   `kRowStep`·строк + `kFramePad` (= высота белой рамки; при 5 строках даёт 520 — данные);
//     ниже    `kPageGap` + `kPageH` (зазор 14 и ряд пагинации 23 — данные: 534 и 23×23);
//     снизу   нижний ряд кнопок сидит на анкоре 0.99 с Origin.y = −68, плюс воздух `kBottomMargin`.
//   ⇒ kRowStep·строк ≤ 0.99·H − (69 + 30 + 14 + 23 + 68 + 8) = 0.99·H − 212.
//   При H = 768 (база оригинала) это 5 строк — ровно оригинальная раскладка.
int32 UAsdaHairShopSubsystem::RowsForScreen() const
{
	const int32 OrigRows = FMath::CeilToInt((float)kCardsFit / 2.f);   // 5 строк = 10 видимых карточек
	if (CVarFitScreen.GetValueOnGameThread() == 0) { LastUiHeight = 0.f; return OrigRows; }

	// Высота в ЛОКАЛЬНЫХ единицах канваса окна (DPI проекта = 1.0, закон `resolution-aspect-system`).
	float H = 0.f;
	if (Widget)
	{
		const FVector2D Local = Widget->GetCachedGeometry().GetLocalSize();
		if (Local.Y > 1.f) { H = (float)Local.Y; }
	}
	if (H <= 1.f)
	{
		// Геометрии ещё нет (первый кадр) — берём вьюпорт напрямую.
		if (const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		{
			const FVector2D VP = UWidgetLayoutLibrary::GetViewportSize(const_cast<UWorld*>(W));
			float Scale = UWidgetLayoutLibrary::GetViewportScale(const_cast<UWorld*>(W));
			if (Scale <= KINDA_SMALL_NUMBER) { Scale = 1.f; }
			H = (float)VP.Y / Scale;
		}
	}
	LastUiHeight = H;
	if (H <= 1.f) { return OrigRows; }   // ничего не известно — оригинал

	const float Budget = kBottomAnchor * H
		- (kBaseTop + kFramePad + kPageGap + kPageH - kBottomBtnY + kBottomMargin);
	const int32 Rows = FMath::FloorToInt(Budget / kRowStep);
	return FMath::Clamp(Rows, 1, kMaxRows);
}

// ★ Клонирование недостающих карточек с эталонной `HAIR_ITEM01_grp` (общая точка
//   `AsdaUi::CloneWidgetSubtree`; прецедент — окно 1800, где в `.JUI` 6 записей, а надо 12).
//   Имя узла-клона = имя источника с ПЕРВЫМ вхождением «01», заменённым на номер карточки:
//   `HAIR_ITEM01_grp` → `HAIR_ITEM15_grp`, `HAIR_ITEM_BUY01` → `HAIR_ITEM_BUY15` и т.д. — те же
//   имена, по которым карточку потом ищет `SetupCards` (и они же читаются в дампах окна).
bool UAsdaHairShopSubsystem::EnsureCardWidgets(int32 Want)
{
	Want = FMath::Clamp(Want, kCards, kMaxRows * 2);
	if (Cards.Num() >= Want) { return true; }
	if (!Widget || !Widget->WidgetTree) { return false; }

	UWidget* Src = AsdaUi::FindNodeExact(Widget, TEXT("HAIR_ITEM01_grp"));
	UPanelWidget* Dst = Src ? Src->GetParent() : nullptr;   // `HAIR_ITEMBASE_grp`
	if (!Src || !Dst)
	{
		UE_LOG(LogAsdaHair, Error,
			TEXT("[парикмахерская] нечего клонировать: HAIR_ITEM01_grp %s, подложка %s — остаёмся на %d карточках"),
			Src ? TEXT("есть") : TEXT("НЕТ"), Dst ? TEXT("есть") : TEXT("НЕТ"), Cards.Num());
		return false;
	}
	const int32 Was = Cards.Num();
	for (int32 i = Was; i < Want; ++i)
	{
		const FString NN = FString::Printf(TEXT("%02d"), i + 1);
		if (AsdaUi::FindNodeExact(Widget, *FString::Printf(TEXT("HAIR_ITEM%s_grp"), *NN))) { continue; }
		UWidget* Clone = AsdaUi::CloneWidgetSubtree(Widget, Src, Dst,
			[&NN](const UWidget* Node) -> FString
			{
				FString N = Node->GetName();
				const int32 At = N.Find(TEXT("01"));   // первое вхождение = номер карточки-образца
				if (At != INDEX_NONE) { N = N.Left(At) + NN + N.Mid(At + 2); }
				return N;
			});
		if (!Clone)
		{
			UE_LOG(LogAsdaHair, Error, TEXT("[парикмахерская] клон карточки %d не создался"), i + 1);
			break;
		}
	}
	Cards.SetNum(Want);
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] карточек в дереве: было %d, стало %d (клонированы с HAIR_ITEM01)"),
		Was, Cards.Num());
	return true;
}

// ★ Расстановка: карточки по сетке 2×N, 9-срезовая подложка 15004 по высоте сетки, ряд пагинации
//   под ней. Двигаем ТОЛЬКО то, что рассчитано от чисел `.JUI`; ничего не подбираем на глаз.
void UAsdaHairShopSubsystem::ApplyGridLayout()
{
	if (!Widget) { return; }
	const int32 Rows = RowsForScreen();
	const int32 Want = Rows * 2;
	EnsureCardWidgets(Want);
	SetupCards();                       // разметить в т.ч. только что склонированные карточки
	GridRows = Rows;

	// Карточки: x = 7 / 223, y = 15 + 98·строка (числа `.JUI`).
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Cards[i].Root.Get()))
		{
			S->SetPosition(FVector2D(kColX0 + kColStep * (float)(i % 2), kRowY0 + kRowStep * (float)(i / 2)));
			S->SetSize(FVector2D(kCardW, kCardH));
		}
	}

	const float FrameH = kRowStep * (float)Rows + kFramePad;   // 5 строк → 520 = размер из `.JUI`
	const float PageY  = FrameH + kPageGap;                    // 5 строк → 534 = позиция из `.JUI`

	// Подложка `HAIR_ITEMBASE` — девять планов 3×3 (иерархия .JUI расплющена, размер группы
	//   на них не влияет). Тянем ОБЩЕЙ ТОЧКОЙ порта, своей копии обхода не заводим.
	if (!AsdaUi::StretchFramePlanesHeight(ItemBaseSlices, FrameH))
	{
		UE_LOG(LogAsdaHair, Warning,
			TEXT("[парикмахерская] подложку 15004 растянуть не удалось (планов %d, нужно 9)"),
			ItemBaseSlices.Num());
	}
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(ItemBaseRoot.Get()))
	{
		// Группа подложки должна вмещать и сетку, и ряд пагинации — иначе часть детей окажется
		//   за пределами группы (в UMG это не режет, но дампы и хит-тест читать труднее).
		S->SetSize(FVector2D(S->GetSize().X, PageY + kPageH));
	}
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(PageRow.Get()))
	{
		S->SetPosition(FVector2D(S->GetPosition().X, PageY));
		S->SetZOrder(1);   // см. ниже: клоны дописываются в КОНЕЦ канваса подложки
	}
	// ★ ПОРЯДОК РИСОВАНИЯ. Клонированные карточки (15-я и дальше) `AddChild` вешает в КОНЕЦ детей
	//   подложки, то есть ПОВЕРХ рамки выделения 15149 и ряда пагинации, которые в `.JUI` лежат
	//   после четырнадцатой карточки. Геометрически они не пересекаются, но рамка выделения
	//   обязана рисоваться НАД карточкой — иначе выбор клонированной карточки не был бы виден.
	//   Поднимаем рамку и пагинацию `ZOrder`ом: его канвас учитывает независимо от порядка детей.
	for (const TObjectPtr<UWidget>& W : SelFrame)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(W.Get())) { S->SetZOrder(2); }
	}
	UE_LOG(LogAsdaHair, Log,
		TEXT("[парикмахерская] сетка: высота UI %.0f ед. → %d строк × 2 = %d карточек | подложка %.0f px, "
		     "пагинация на y=%.0f | ключ asda.HairShop.FitScreen %d (0 = оригинальные 14 гнёзд, видимы 10)"),
		LastUiHeight, Rows, Want, FrameH, PageY, CVarFitScreen.GetValueOnGameThread());
}

// ★★ КАКАЯ ПРИЧЁСКА НА ПЕРСОНАЖЕ СЕЙЧАС (пункт 4 владельца).
//   ДОКАЗАТЕЛЬСТВО СПОСОБА: эталон определяет её РОВНО ТАК ЖЕ, когда строит меш волос —
//   `findRecordByThreeKeys(g_pHairShopWnd, self->gender, self->gap_43B[38], self->gap_43B[39])`
//   (`CCharacter_LoadHairModel@0x9E2700`, `entity_char.c:11452` у мужчин и `:11491` у женщин).
//   То есть ключ — ТРОЙКА ПОЛ + HairStyle + ColorIdx из данных ПЕРСОНАЖА, а не индекс карточки,
//   не порядок списка и не «код предмета» (записи предмета у причёски нет вовсе).
//   ⚠ Стиль/цвет берём СОХРАНЁННЫЕ при входе: во время примерки текущие уже подменены, и по ним
//   «купленной» помечалась бы примеряемая причёска.
//   ★ Сверка идёт ПО ТАБЛИЦЕ `DT_hairtable` — поменяет владелец строку в редакторе, отметка
//   поедет за данными; никакого «если код N» в коде нет.
int32 UAsdaHairShopSubsystem::OwnedHairId() const
{
	if (SavedStyle < 0) { return -1; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UDataTable* DT = IT ? IT->GetDataTable(kHairTable) : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!DT) { return -1; }
	const int32 WantGender = Net ? (int32)Net->ResolveSelfGender() : 1;
	for (const TPair<FName, uint8*>& It : DT->GetRowMap())
	{
		const FAsdaHairtableRow* R = reinterpret_cast<const FAsdaHairtableRow*>(It.Value);
		if (R && R->Gender == WantGender && R->HairStyle == SavedStyle && R->ColorIdx == SavedColor)
		{
			return R->Id;
		}
	}
	return -1;
}

// ═══════════════════════════════ ВИДЖЕТ ════════════════════════════════════════════════════════
bool UAsdaHairShopSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kWinZ).Ensure(GetGameInstance(), Widget, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.CloseWindow(kWindowId);
			Widget = nullptr;
			bReady = false;
			bOwnedStyleSaved = false;   // ★ 10.10.2026 (МЕЛК-3): новый виджет — образцы «Купить» снимаются заново с его карточки 01
		},
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
		},
		[]()
		{
			UE_LOG(LogAsdaHair, Error, TEXT("[парикмахерская] WBP_HAIRSHOP_WIN не загрузился (%s)"), kWbpPath);
		});
	return FAsdaJuiWindowHandle::Succeeded(R);
}

void UAsdaHairShopSubsystem::SetupControls()
{
	bReady = false;
	Relays.Reset();
	CardRelays.Reset();   // ★ 31.10.2026: окно пересобирается — и реле карточек вместе с ним
	PanelTree.Reset();
	PanelAlwaysHidden.Reset();
	SelFrame.Reset();
	DeadPreCash.Reset();
	if (!Widget || !Widget->WidgetTree) { return; }

	auto F = [this](const TCHAR* N) -> UWidget* { return AsdaUi::FindNodeExact(Widget, N); };
	auto T = [&F](const TCHAR* N) -> UTextBlock* { return Cast<UTextBlock>(F(N)); };

	BtnClose   = Cast<UButton>(F(TEXT("HAIR_CLOSEBTN")));        // 15001, команда 0x3A99
	BtnOrigin  = Cast<UButton>(F(TEXT("HAIR_ORIGINHAIR")));      // 15182, команда 0x3B4E
	BtnCharge  = Cast<UButton>(F(TEXT("HAIR_LMAC_CHARGE")));     // 15166, команда 0x3B3E
	BtnBalance = Cast<UButton>(F(TEXT("HAIR_CashCheck_Btn")));   // 15167, команда 0x3B3F
	// ★ МИСНЕЙМ M12: `FIRST` ведёт В КОНЕЦ, `END` — В НАЧАЛО (`ui_core.c:45563-45584`).
	BtnNext    = Cast<UButton>(F(TEXT("HAIR_PAGENEXT0")));       // 15154, 0x3B32 страница +1
	BtnPrev    = Cast<UButton>(F(TEXT("HAIR_PAGEPRE0")));        // 15156, 0x3B34 страница −1
	BtnLast    = Cast<UButton>(F(TEXT("HAIR_PAGEFIRST0")));      // 15158, 0x3B36 → В КОНЕЦ
	BtnFirst   = Cast<UButton>(F(TEXT("HAIR_PAGEEND0")));        // 15160, 0x3B38 → В НАЧАЛО
	TxtPage    = T(TEXT("HAIR_PAGENUM1"));                       // 15151
	TxtPages   = T(TEXT("HAIR_PAGENUM2"));                       // 15152
	TxtCash    = T(TEXT("HAIR_MAC_VALUE"));                      // 15164
	TxtCashLbl = T(TEXT("HAIR_TEXT1"));                          // 15163
	ChkOnlyMine= Cast<UCheckBox>(F(TEXT("HAIR_POSSABLECHK")));   // 15002 (D7)

	// ── Подложка сетки 15004 и ряд пагинации: их геометрию ведёт `ApplyGridLayout` ──
	ItemBaseRoot = AsdaUi::FindNodeExact(Widget, TEXT("HAIR_ITEMBASE_grp"));
	PageRow      = AsdaUi::FindNodeExact(Widget, TEXT("HAIR_SLASH_grp"));
	ItemBaseSlices.Reset();
	if (UPanelWidget* BaseP = Cast<UPanelWidget>(ItemBaseRoot.Get()))
	{
		// 9 срезов подложки — ПЕРВЫЕ девять детей группы, имена `HAIR_ITEMBASE_<n>_0…8`
		//   (индекс `<n>` даёт импортёр и он меняется при переимпорте — ищем по префиксу).
		for (int32 i = 0; i < BaseP->GetChildrenCount() && ItemBaseSlices.Num() < 9; ++i)
		{
			UWidget* Kid = BaseP->GetChildAt(i);
			if (Kid && Kid->GetName().StartsWith(TEXT("HAIR_ITEMBASE_"))) { ItemBaseSlices.Add(Kid); }
		}
	}

	auto SetLoc = [this](UTextBlock* Tb, int32 Id)
	{
		if (!Tb) { return; }
		Tb->SetAutoWrapText(false);          // закон `ui-autowrap-canvas-gotcha`
		const FString S = Loc(Id);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtCashLbl, kLocCashLbl);
	// ★★ 15182: подпись из НАШЕЙ строки локализации (см. разбор у `kLocOriginBtn`). Импортёр
	//   положил её текст в отдельный узел `HAIR_ORIGINHAIR_Text` — его и правим, не кнопку.
	SetLoc(T(TEXT("HAIR_ORIGINHAIR_Text")), kLocOriginBtn);

	// ── Панель 15168: собираем ВСЁ поддерево по группе (закон `jui-uneven-node-layout`) ──
	{
		TArray<UWidget*> All;
		Widget->WidgetTree->GetAllWidgets(All);
		UWidget* PanelRoot = AsdaUi::FindNodeByPrefix(Widget, TEXT("HAIR_PURCHASE_WIN_grp"));
		if (!PanelRoot) { PanelRoot = AsdaUi::FindNodeByPrefix(Widget, TEXT("HAIR_PURCHASE_WIN")); }
		TArray<UWidget*> Stack;
		if (PanelRoot) { PanelTree.Add(PanelRoot); Stack.Add(PanelRoot); }
		while (Stack.Num() > 0)
		{
			UWidget* Cur = Stack.Pop(EAllowShrinking::No);
			if (UPanelWidget* P = Cast<UPanelWidget>(Cur))
			{
				for (int32 i = 0; i < P->GetChildrenCount(); ++i)
				{
					if (UWidget* Kid = P->GetChildAt(i)) { PanelTree.Add(Kid); Stack.Add(Kid); }
				}
			}
		}
		PurTitle   = T(TEXT("HAIR_PUR_TITLE"));        // 15173 имя причёски
		PurContent = T(TEXT("HAIR_PUR_CONTENT"));      // 15174 описание (`hairtable` +111)
		PurIcon    = Cast<UImage>(AsdaUi::FindNodeByPrefix(Widget, TEXT("HAIR_PUR_ICON_")));  // 15170
		PurLvLbl   = T(TEXT("HAIR_PUR_LV"));           // 15175 (loc 8487)
		PurLvVal   = T(TEXT("HAIR_PUR_LV_Num"));       // 15183
		PurMacLbl  = T(TEXT("HAIR_PUR_MAC"));          // 15176 (loc 2303)
		PurMacVal  = T(TEXT("HAIR_PUR_MAC_Num"));      // 15184
		PurAsk     = T(TEXT("HAIR_PUR_TXT"));          // 15177 (loc 13002)
		PurOk      = Cast<UButton>(F(TEXT("HAIR_PUR_CONFIRM")));   // 15178, 0x3B4A
		PurCancel  = Cast<UButton>(F(TEXT("HAIR_PUR_CANCEL")));    // 15179, 0x3B4B
		SetLoc(PurAsk,    kLocPurAsk);
		// ★★ ПУНКТ 2 ВЛАДЕЛЬЦА: «Нажми» / «Быстрее» — это НЕ кривой перевод, а дословный
		//   перенос корейских ДИЗАЙНЕРСКИХ ЗАГЛУШЕК `눌러~` («нажми~») и `빨랑~` («быстрей~»)
		//   из `gamemain_jui.ini` (CaptionText контролов 15178/15179), которые импортёр
		//   перевёл по словарю `Tools/kor_ru_ui.json`. По смыслу это «ОК» и «Отмена»
		//   (§3.5 отчёта: 15178 «Да» → 0x3B4A, 15179 «Нет» → 0x3B4B).
		//   ⇒ Ставим ТЕ ЖЕ строки, что стоят на кнопках общей модалки 1559 (6857 и 2289):
		//   готовые, ничего не выдумано и ни одна чужая строка не изменена.
		SetLoc(T(TEXT("HAIR_PUR_CONFIRM_Text")), kLocBtnOk);
		SetLoc(T(TEXT("HAIR_PUR_CANCEL_Text")),  kLocBtnCancel);
		SetLoc(PurLvLbl,  kLocPurLvLbl);
		SetLoc(PurMacLbl, kLocPurMacLbl);
		// ★ D9: guide-тексты 15190/15191 эталон наполняет и ТУТ ЖЕ скрывает.
		for (const TCHAR* N : { TEXT("HAIR_PUR_GuideTxt00"), TEXT("HAIR_PUR_GuideTxt01") })
		{
			if (UTextBlock* Tb = T(N))
			{
				SetLoc(Tb, FCString::Strcmp(N, TEXT("HAIR_PUR_GuideTxt00")) == 0 ? kLocGuide0 : kLocGuide1);
				if (CVarGuideTextHidden.GetValueOnGameThread() != 0) { PanelAlwaysHidden.Add(Tb); }
			}
		}
		// Бейджи HOT/NEW панели (15171/15172) эталон держит скрытыми.
		for (const TCHAR* Pfx : { TEXT("HAIR_PUR_HOT_"), TEXT("HAIR_PUR_NEW_") })
		{
			if (UWidget* W = AsdaUi::FindNodeByPrefix(Widget, Pfx)) { PanelAlwaysHidden.Add(W); }
		}
	}

	// ── Рамка выделения 15149 `HAIR_ITEM_SELECT_*` — одна на всю сетку (как 6273 у окна 6000) ──
	{
		TArray<UWidget*> All;
		Widget->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (W && W->GetName().StartsWith(TEXT("HAIR_ITEM_SELECT"))) { SelFrame.Add(W); }
		}
		// ★ Дедуп №2, В3-S10: домашнее место первого плана — с ЭТОГО виджета (сдвиг прошлой копии окна сюда не переносится).
		const UCanvasPanelSlot* S0 = SelFrame.Num() > 0 ? AsdaUi::SlotOf(SelFrame[0].Get()) : nullptr;
		SelFrameHome = S0 ? S0->GetPosition() : FVector2D::ZeroVector;
	}

	// ── D11: мёртвая пара «подарочный кэш» 40002/40003 ──
	for (const TCHAR* N : { TEXT("Hair_PreCashTxt_grp"), TEXT("Hair_PreCashTxt"),
	                        TEXT("Hair_PreCash_grp"), TEXT("Hair_PreCash") })
	{
		if (UWidget* W = F(N))
		{
			if (UTextBlock* Tb = Cast<UTextBlock>(W)) { SetLoc(Tb, kLocPreCash); }
			W->SetVisibility(ESlateVisibility::Collapsed);
			DeadPreCash.Add(W);
		}
	}

	// ── D7: чек-бокс фильтра 15002 в данных скрыт ──
	if (ChkOnlyMine && CVarFilterCheckHidden.GetValueOnGameThread() != 0)
	{
		ChkOnlyMine->SetVisibility(ESlateVisibility::Collapsed);
	}

	SetupCards();

	// ── Кнопки окна ──
	auto Bind = [this](UButton* B, int32 IdNum, TFunction<void(int32)> Cb)
	{
		if (!B) { return; }
		AsdaUi::BindButton(this, B, IdNum, MoveTemp(Cb), Relays);   // ★ U20: реле клика — общая точка
	};
	// ★ 15001 у эталона (`case 0x3A99`) — ПОЛНЫЙ выход из парикмахерской, а не «скрыть окно».
	Bind(BtnClose,   15001, [this](int32) { CloseWindow(); });
	Bind(BtnOrigin,  15182, [this](int32) { OnOriginHairClicked(); });
	Bind(BtnCharge,  15166, [this](int32) { OnChargeClicked(); });
	Bind(BtnBalance, 15167, [this](int32) { OnBalanceClicked(); });
	Bind(BtnNext,    15154, [this](int32) { SetPage(Page + 1); });
	Bind(BtnPrev,    15156, [this](int32) { SetPage(Page - 1); });
	Bind(BtnLast,    15158, [this](int32) { SetPage(MaxPage); });   // «FIRST» → В КОНЕЦ (M12)
	Bind(BtnFirst,   15160, [this](int32) { SetPage(0); });         // «END»   → В НАЧАЛО (M12)
	Bind(PurOk,      15178, [this](int32) { OnPanelConfirmClicked(); });
	Bind(PurCancel,  15179, [this](int32) { OnPanelCancelClicked(); });
	if (ChkOnlyMine)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 15002;
		R->CheckCallback = [this](int32, bool bOn) { SetFilterOnlyMine(bOn); };
		ChkOnlyMine->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
		AsdaUi::AnchorRelay(Relays, R, ChkOnlyMine);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	bReady = (BtnClose != nullptr) || (Cards[0].Root != nullptr);
	UE_LOG(LogAsdaHair, Log, TEXT("%s"), *DumpControls());
}

void UAsdaHairShopSubsystem::SetupCards()
{
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.RelayAnchorSplit`): разметка карточек повторная (`ApplyGridLayout`), и подписки кнопок ниже
	//   снимаются (`OnClicked.Clear`) — прежние реле карточек мертвы, держать их незачем. 0 — прежнее: копились в `Relays`.
	const bool bSplit = AsdaRelayAnchorSplitOn();
	if (bSplit) { CardRelays.Reset(); }
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	// Роль ищем регулярным выражением, а не индексами импортёра: `_21_0`, `_22_0` меняются при
	//   каждом переимпорте окна (закон `jui-importer-widget-naming`).
	auto FindRe = [&All](const FString& Pattern) -> UWidget*
	{
		const FRegexPattern Pat(Pattern);
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			FRegexMatcher M(Pat, W->GetName());
			if (M.FindNext()) { return W; }
		}
		return nullptr;
	};

	int32 Found = 0;
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		FAsdaHairCardUi& C = Cards[i];
		C.Parts.Reset(); C.AlwaysHidden.Reset(); C.HairId = -1;
		const FString NN = FString::Printf(TEXT("%02d"), i + 1);   // карточки нумеруются с 01

		// ВСЁ поддерево карточки — её собственная группа `HAIR_ITEM{nn}_grp`.
		UWidget* Root = nullptr;
		for (UWidget* W : All)
		{
			if (W && W->GetName() == FString::Printf(TEXT("HAIR_ITEM%s_grp"), *NN)) { Root = W; break; }
		}
		C.Root = Root;
		if (Root)
		{
			++Found;
			C.Parts.Add(Root);
			TArray<UWidget*> Stack; Stack.Add(Root);
			while (Stack.Num() > 0)
			{
				UWidget* Cur = Stack.Pop(EAllowShrinking::No);
				if (UPanelWidget* P = Cast<UPanelWidget>(Cur))
				{
					for (int32 k = 0; k < P->GetChildrenCount(); ++k)
					{
						if (UWidget* Kid = P->GetChildAt(k)) { C.Parts.AddUnique(Kid); Stack.Add(Kid); }
					}
				}
			}
		}
		C.NameText  = Cast<UTextBlock>(FindRe(FString::Printf(TEXT("^HAIR_ITEM_TITLE%s_Text$"), *NN)));
		C.PriceText = Cast<UTextBlock>(FindRe(FString::Printf(TEXT("^HAIR_ITEM_PRI%s$"), *NN)));
		C.LevelText = Cast<UTextBlock>(FindRe(FString::Printf(TEXT("^HAIR_ITEM_LV%s$"), *NN)));
		C.Icon      = Cast<UImage>(FindRe(FString::Printf(TEXT("^HAIR_ITEM_ICON%s_\\d+_0$"), *NN)));
		C.BuyButton = Cast<UButton>(FindRe(FString::Printf(TEXT("^HAIR_ITEM_BUY%s$"), *NN)));
		// ★ Бейдж уровня `BoxIcon_Level` — имя-план `HAIR_ITEM_LV{nn}_Icon_<n>_0` (индекс от импортёра).
		C.LevelBadge = FindRe(FString::Printf(TEXT("^HAIR_ITEM_LV%s_Icon_\\d+_0$"), *NN));
		// Подложка заголовка карточки и подпись кнопки — их подменяет отметка «Куплено».
		C.TitlePlane = Cast<UImage>(FindRe(FString::Printf(TEXT("^HAIR_ITEM_TITLE%s_\\d+_0$"), *NN)));
		C.BuyCaption = Cast<UTextBlock>(FindRe(FString::Printf(TEXT("^HAIR_ITEM_BUY%s_Text$"), *NN)));
		// Исходные цвета запоминаем ОДИН раз — по ним отметка СНИМАЕТСЯ при перелистывании страницы.
		if (!bOwnedStyleSaved && C.TitlePlane && C.NameText)
		{
			BaseTitleTint = C.TitlePlane->GetColorAndOpacity();
			BaseTitleText = C.NameText->GetColorAndOpacity().GetSpecifiedColor();
			bOwnedStyleSaved = true;
			if (C.BuyCaption) { BaseBuyCaption = C.BuyCaption->GetText(); }   // ★ 10.10.2026: ссылка ST_GameText:13249 сохраняется
		}
		// Бейджи NEW/HOT (+6/+7) — эталон держит их скрытыми всегда.
		for (const TCHAR* Pfx : { TEXT("HAIR_NEW"), TEXT("HAIR_HOT") })
		{
			if (UWidget* W = FindRe(FString::Printf(TEXT("^%s%s_\\d+_0$"), Pfx, *NN))) { C.AlwaysHidden.Add(W); }
		}

		// ★ КЛИК ПО САМОЙ КАРТОЧКЕ (эталонное сообщение 0xB → `hairWin_SelectSlotByMsg@0xCEDCD0`).
		//   В UMG `Image` кликов не ловит, поэтому кладём прозрачную кнопку во всю карточку
		//   и ОПУСКАЕМ ЕЁ В НИЗ порядка детей — иначе она перехватила бы клики у кнопки «Купить»
		//   (закон `jui-cell-chain-nesting`: клик ловим по живой геометрии, а не по слоту).
		//
		// ★★★★★ ДЕФЕКТ ПОРТА 2026-08-28 («все кнопки Купить не нажимаются»), ДОКАЗАН ХИТ-ТЕСТОМ.
		//   Опускать ячейку вниз через `UPanelWidget::ShiftChild` НЕЛЬЗЯ: он переставляет ТОЛЬКО
		//   UPROPERTY-массив `Slots` и НЕ трогает уже построенный `SConstraintCanvas`
		//   (`ShiftChild` = `Slots.RemoveAt` + `Slots.Insert`, живого слейт-слота не касается).
		//   Канвас карточки к этому моменту УЖЕ построен (окно создано в `EnsureWidget` выше),
		//   поэтому `AddChild` вешает прозрачную кнопку ПОСЛЕДНЕЙ, то есть САМОЙ ВЕРХНЕЙ, и она
		//   съедала клик у «Купить» на ВСЕХ карточках. Клик по самой карточке при этом работал —
		//   отсюда и симптом «карточка выбирается, а купить нельзя».
		//   ЛЕЧЕНИЕ: порядок задаём `ZOrder = -1`. `SConstraintCanvas::OnArrangeChildren` сортирует
		//   детей по `ZOrder` (стабильно), а хит-тест идёт по обратному порядку разложенных —
		//   значит отрицательный ZOrder кладёт хит-плашку под ВСЕ узлы карточки НАДЁЖНО, независимо
		//   от того, построен канвас или ещё нет. `ShiftChild` оставлен лишь чтобы UMG-массив был
		//   согласован при возможной перестройке виджета.
		if (UPanelWidget* Pan = Cast<UPanelWidget>(Root))
		{
			const FName HitName(*FString::Printf(TEXT("HAIR_ITEM%s_CARDHIT"), *NN));
			UButton* Hit = Cast<UButton>(AsdaUi::FindNodeExact(Widget, *HitName.ToString()));
			if (!Hit)
			{
				Hit = Widget->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), HitName);
				FButtonStyle St = Hit->GetStyle();
				St.Normal.TintColor = St.Hovered.TintColor = St.Pressed.TintColor =
					FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
				Hit->SetStyle(St);
				if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Pan->AddChild(Hit)))
				{
					S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
					S->SetAlignment(FVector2D(0.f, 0.f));
					S->SetPosition(FVector2D(0.f, 0.f));
					S->SetSize(FVector2D(212.f, 90.f));   // `.JUI` `HAIR_ITEM{nn}` Size = "212 90"
					S->SetZOrder(CVarLegacyCardHitZ.GetValueOnGameThread() != 0 ? 0 : -1);   // ★★★ ниже всех узлов карточки
				}
				Pan->ShiftChild(0, Hit);   // согласуем и UMG-массив (живой канвас двигает ZOrder)
			}
			else if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Hit))
			{
				// Повторная разметка уже существующей плашки: чиним ZOrder, если она пришла из
				//   прежней сборки, где он был 0 (тогда клик по «Купить» не доходил).
				S->SetZOrder(CVarLegacyCardHitZ.GetValueOnGameThread() != 0 ? 0 : -1);
			}
			C.HitButton = Hit;
			C.Parts.AddUnique(Hit);
		}

		// ★ ИДЕМПОТЕНТНОСТЬ. `SetupCards` зовётся ПОВТОРНО (после клонирования карточек в
		//   `ApplyGridLayout`), а `AddDynamic` не заменяет, а ДОБАВЛЯЕТ подписку: без сброса одна
		//   кнопка «Купить» открывала бы панель дважды за клик. Чистим — подписчик тут только наш.
		auto Bind = [this, bSplit](UButton* B, int32 Idx, TFunction<void(int32)> Cb)
		{
			if (!B) { return; }
			B->OnClicked.Clear();
			AsdaUi::BindButton(this, B, Idx, MoveTemp(Cb), bSplit ? CardRelays : Relays);   // ★ U20: реле клика — общая точка
		};
		Bind(C.BuyButton, i, [this](int32 Idx) { OnCardBuyClicked(Idx); });
		Bind(C.HitButton, i, [this](int32 Idx) { OnCardClicked(Idx); });
	}
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] карточки: размечено %d из %d"), Found, Cards.Num());
}

// ═══════════════════════════════ РЕНДЕР ════════════════════════════════════════════════════════
void UAsdaHairShopSubsystem::ShowCard(int32 Card, bool bVisible)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	FAsdaHairCardUi& C = Cards[Card];
	// ★ D8: карточки 11…14 не влезают в подложку 446×520 — их не показываем вовсе.
	// ★ Гейт D8 (карточки 11…14 скрыты) действует ТОЛЬКО когда сетка НЕ считается по экрану:
	//   при включённом `FitScreen` подложка растянута под нужное число строк, и прятать нечего.
	if (bVisible && CVarFitScreen.GetValueOnGameThread() == 0
	    && Card >= kCardsFit && CVarRows11to14.GetValueOnGameThread() != 0) { bVisible = false; }
	const ESlateVisibility V = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	for (const TObjectPtr<UWidget>& W : C.Parts) { if (W) { W->SetVisibility(V); } }
	// Кнопки обязаны ЛОВИТЬ мышь (закон `selfhittest-vs-hittest-invisible`).
	AsdaUi::SetShownHit(C.BuyButton, bVisible);
	AsdaUi::SetShownHit(C.HitButton, bVisible);
	AsdaUi::SetShownNoHit(C.Icon, bVisible);
	// «Всегда скрытые» гасим ПОСЛЕ общего показа — иначе они вернулись бы на экран.
	for (const TObjectPtr<UWidget>& W : C.AlwaysHidden) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
}

// Порт `refineFillMaterialSlotInfo@0xCF0660` (`item_enhance.c:1346-1368`).
void UAsdaHairShopSubsystem::PopulateCard(int32 Card, int32 HairId)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	FAsdaHairCardUi& C = Cards[Card];
	C.HairId = HairId;
	const FAsdaHairtableRow* R = (HairId >= 0) ? RowById(HairId) : nullptr;
	if (!R) { ShowCard(Card, false); return; }

	if (C.NameText)  { C.NameText->SetAutoWrapText(false); C.NameText->SetText(FText::FromString(HairText(GetGameInstance(), R, TEXT("Name"), R->Name))); }   // [ПОРТ] 09.10.2026 (мультиязычность)
	if (C.PriceText) { C.PriceText->SetAutoWrapText(false); C.PriceText->SetText(FText::FromString(AsdaUi::FormatThousands(R->Price))); }
	if (C.LevelText)
	{
		C.LevelText->SetAutoWrapText(false);
		// Эталон: `sprintf(buf, "%d %s", rec[35], loc(13000))` — при нулевом уровне строка ПУСТА
		//   (`item_enhance.c:1349-1356`). В живых данных уровень 0 у всех записей (D12).
		C.LevelText->SetText(R->RequiredLevel > 0
			? FText::FromString(FString::Printf(TEXT("%d %s"), R->RequiredLevel, *Loc(kLocLevelSuf)))
			: FText::GetEmpty());
	}
	// Иконка: атлас `Icon` (`+78`), клетка `IconCell` (`+110`), сетка 8 колонок по 64 px —
	//   эталонный `Grid_IndexToCellOffset8x64@0xEC0050`.
	if (C.Icon)
	{
		const int32 Cell = FMath::Max(0, R->IconCell);
		AsdaUi::ApplyJuiIconCell(C.Icon.Get(), R->Icon, 64 * (Cell % 8), 64 * (Cell / 8), 64, 64.f);
	}
	ShowCard(Card, true);
	// ★ Бейдж уровня показываем ТОЛЬКО когда рядом есть число. В оригинале он виден всегда
	//   (`refineFillMaterialSlotInfo` его видимость не трогает), но `RequiredLevel` в живых
	//   данных 0 у всех 228 записей — и красный значок «L» висел без значения (см. D12).
	if (C.LevelBadge)
	{
		const bool bShowBadge = (R->RequiredLevel > 0) || (CVarLevelBadgeAlways.GetValueOnGameThread() != 0);
		AsdaUi::SetShownNoHit(C.LevelBadge, bShowBadge);
	}

	// ★★ ОТМЕТКА «КУПЛЕНО» (пункт 4 владельца; расширение, ключ `asda.HairShop.MarkOwned`).
	//   Причёска «уже есть», если её строка в таблице совпадает с ТРОЙКОЙ персонажа (см. `OwnedHairId`).
	//   ⚠ ЦВЕТА ЗАДАЮТСЯ КОДОМ, А НЕ СКИНОМ: у подложки заголовка скин `Box_WhiteOt20`, и НИ ОДНОГО
	//   состояния («куплено», «выбрано») в нём нет — состояний у JUI-скинов не бывает вовсе, кроме
	//   подписей кнопок (закон `jui-caption-states`). Рисовать свою картинку запрещено
	//   (`no-invented-ui`), поэтому подкрашиваем ИМЕЮЩИЙСЯ план тинтом. Остальных элементов окна
	//   правка не касается — только заголовок карточки, которая уже надета.
	const bool bOwned = (CVarMarkOwned.GetValueOnGameThread() != 0) && (HairId >= 0) && (HairId == CachedOwnedId);
	if (C.TitlePlane) { C.TitlePlane->SetColorAndOpacity(bOwned ? kOwnedGold : BaseTitleTint); }
	if (C.NameText)   { C.NameText->SetColorAndOpacity(FSlateColor(bOwned ? kOwnedText : BaseTitleText)); }
	if (C.BuyButton)  { C.BuyButton->SetIsEnabled(!bOwned); }
	if (C.BuyCaption)
	{
		// ★ 10.10.2026 (barber_minors): «Купить» ставится тем же FText, что в ассете (ссылка на ST_GameText:13249 — язык сессии).
		const FText Cap = bOwned ? FText::FromString(Loc(kLocOwned)) : BaseBuyCaption;
		if (!Cap.IsEmpty()) { C.BuyCaption->SetText(Cap); }
	}
}

void UAsdaHairShopSubsystem::BuildPage()
{
	const int32 PerPage = FMath::Max(1, CardsPerPage());
	// Id надетой причёски считаем ОДИН раз на страницу (а не в каждой карточке).
	CachedOwnedId = OwnedHairId();
	for (int32 i = 0; i < Cards.Num(); ++i) { Cards[i].HairId = -1; ShowCard(i, false); }
	for (int32 Slot = 0; Slot < PerPage && Slot < Cards.Num(); ++Slot)
	{
		const int32 Idx = Page * PerPage + Slot;
		if (!ListIds.IsValidIndex(Idx)) { break; }
		PopulateCard(Slot, ListIds[Idx]);
	}
	MoveSelectionFrame(SelCard);
	RefreshPageLabels();
}

// Порт `refineUiUpdateLevelText@0xCF01F0`: обе метки — «номер + 1».
void UAsdaHairShopSubsystem::RefreshPageLabels()
{
	// ★ Волна 3: общая точка `AsdaShopUi::SetPageLabels` (порт `tradeWinUpdatePageLabels@0xC37BC0`: обе метки «номер + 1»).
	AsdaShopUi::SetPageLabels(TxtPage, TxtPages, Page, MaxPage);
}

void UAsdaHairShopSubsystem::RefreshCash()
{
	if (TxtCash) { TxtCash->SetText(FText::FromString(AsdaUi::FormatThousands(CashGxp))); }
}

// Порт `uiSlotControlShowAt@0xCF1230`: рамка 15149 переезжает НА ПОЗИЦИЮ выбранной карточки.
//   ⚠ Иерархия `.JUI` у нас расплющена, поэтому двигаем каждый план рамки на дельту от её
//   исходного места (закон `jui-flattened-hierarchy-geometry`).
// ★ ИСПРАВЛЕНИЕ 12.10.2026 (дедуп №2, В3-S10, остаток U15): (1) сдвиг планов — общая точка `AsdaUi::ShiftCanvasSlot`
//   (SPAN-безопасно; было наивное `SetPosition(GetPosition() + d)` — у растянутого плана сдвиг одного угла растягивает его;
//   у текущего ассета все 8 планов POINT — результат прежний); (2) дельта считается от ДОМАШНЕГО места первого плана,
//   снятого с текущего виджета (`SelFrameHome`), и его ЖИВОЙ позиции — было «новая дельта − прошлая» через член `LastSelDelta`,
//   который переживал пересоздание виджета (смена карты/сброс окна): у новой копии окна планы стоят дома, а вычиталась дельта
//   прежней — рамка уезжала от карточки на прежний сдвиг.
// ⚠ [ОТСТУПЛЕНИЕ, прежнее — не тронуто] Эталон ставит угол рамки РОВНО в угол карточки (`CControl_SetField74_75(рамка,
//   Vec2_74(карточка))`, ui_core.c:45868), порт сохраняет авторское смещение `.JUI` (рамка 216×94 в (5,13) вокруг карточки
//   212×90 в (7,15) — на 2 px шире с каждой стороны, как у магазина бустеров, где эталон сам вычитает 2: ui_core.c:10546).
//   Отличие 2 px — на решение владельца (отчёт `wave3_S10.md`).
void UAsdaHairShopSubsystem::MoveSelectionFrame(int32 Card)
{
	if (SelFrame.Num() == 0) { return; }
	const bool bShow = Cards.IsValidIndex(Card) && Cards[Card].HairId >= 0 && Cards[Card].Root != nullptr;
	if (!bShow)
	{
		for (const TObjectPtr<UWidget>& W : SelFrame) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
		return;
	}
	const UCanvasPanelSlot* CardSlot = AsdaUi::SlotOf(Cards[Card].Root.Get());
	const UCanvasPanelSlot* Ref      = AsdaUi::SlotOf(SelFrame[0].Get());
	if (!CardSlot || !Ref) { return; }
	// Рамка в `.JUI` стоит около карточки 01 (Origin 5 13 при карточке 7 15) — сохраняем её
	//   собственное смещение относительно карточки и переносим целиком.
	static const FVector2D BaseCard(7.f, 15.f);
	const FVector2D Delta = CardSlot->GetPosition() - BaseCard;
	// Где первый план стоит сейчас → куда ему надо (дом + дельта карточки); на ту же разницу едут все планы.
	const FVector2D Shift = SelFrameHome + Delta - Ref->GetPosition();
	for (const TObjectPtr<UWidget>& W : SelFrame)
	{
		if (!W) { continue; }
		AsdaUi::ShiftCanvasSlot(W.Get(), Shift);
		W->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

// ★ Пересборка сетки при изменении высоты UI (первый кадр после открытия / смена разрешения).
//   В кадре открытия у виджета ещё НЕТ геометрии, поэтому первый расчёт идёт по вьюпорту, а через
//   долю секунды проверяется по живой геометрии — если строк стало больше/меньше, раскладываем заново.
void UAsdaHairShopSubsystem::RelayoutIfNeeded()
{
	if (!bOpen || !Widget) { return; }
	const int32 Want = RowsForScreen();
	if (Want == GridRows) { return; }
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] высота UI изменилась → пересборка сетки: было %d строк, стало %d"),
		GridRows, Want);
	const int32 KeepPage = Page;
	ApplyGridLayout();
	BuildList();
	Page = FMath::Clamp(KeepPage, 0, MaxPage);
	BuildPage();
}

// ═══════════════════════════════ ПРИМЕРКА ══════════════════════════════════════════════════════
// Порт `reloadHairOnEquipChange@0xCF1530`: записать стиль/цвет ИЗ ЗАПИСИ в персонажа и
//   перезагрузить меш волос. Ничего не шлём — примерка чисто клиентская.
void UAsdaHairShopSubsystem::PreviewHair(int32 HairId)
{
	const FAsdaHairtableRow* R = RowById(HairId);
	if (!R) { return; }
	if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		WS->ApplySelfHair(R->HairStyle, R->ColorIdx);
	}
}

// Порт `charReloadHairIfChanged@0xCF23B0` (кнопка 15182 «Вернуть как было»).
void UAsdaHairShopSubsystem::RevertPreview()
{
	if (SavedStyle < 0) { return; }
	if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		WS->ApplySelfHair(SavedStyle, SavedColor);
	}
}

// ═══════════════════════════════ ПАНЕЛЬ 15168 ══════════════════════════════════════════════════
// Порт `sub_CF0E30@0xCF0E30` + нотис 13008: пока панель открыта, остальные действия недоступны.
bool UAsdaHairShopSubsystem::GateNoPanel()
{
	if (!bPanelOpen) { return true; }
	NoticeChat(kLocPanelBusy);
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] действие отклонено: открыта панель 15168 (нотис %d)"), kLocPanelBusy);
	return false;
}

// Порт `uiHairShopWndShow@0xCF0D10` → `ItemDetailWnd_Fill@0xCF0E70`.
void UAsdaHairShopSubsystem::OpenPanel(int32 HairId)
{
	const FAsdaHairtableRow* R = RowById(HairId);
	if (!R) { NoticeChat(kLocNoRecord); return; }
	PanelHairId = HairId;

	if (PurTitle)   { PurTitle->SetAutoWrapText(false); PurTitle->SetText(FText::FromString(HairText(GetGameInstance(), R, TEXT("Name"), R->Name))); }   // [ПОРТ] 09.10.2026 (мультиязычность)
	if (PurContent) { PurContent->SetText(FText::FromString(HairText(GetGameInstance(), R, TEXT("Description"), R->Description))); }   // [ПОРТ] 09.10.2026 (мультиязычность)
	if (PurLvVal)
	{
		// Эталон: при нулевом уровне поле ПУСТОЕ (`ida.c:594869-594873`).
		PurLvVal->SetAutoWrapText(false);
		PurLvVal->SetText(R->RequiredLevel > 0
			? FText::FromString(FString::Printf(TEXT("%d"), R->RequiredLevel)) : FText::GetEmpty());
	}
	if (PurMacVal)  { PurMacVal->SetAutoWrapText(false); PurMacVal->SetText(FText::FromString(AsdaUi::FormatThousands(R->Price))); }
	if (PurIcon)
	{
		const int32 Cell = FMath::Max(0, R->IconCell);
		AsdaUi::ApplyJuiIconCell(PurIcon.Get(), R->Icon, 64 * (Cell % 8), 64 * (Cell / 8), 64, 64.f);
	}

	for (const TObjectPtr<UWidget>& W : PanelTree)
	{
		if (W) { W->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	}
	if (PurOk)     { PurOk->SetVisibility(ESlateVisibility::Visible); }
	if (PurCancel) { PurCancel->SetVisibility(ESlateVisibility::Visible); }
	for (const TObjectPtr<UWidget>& W : PanelAlwaysHidden) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	bPanelOpen = true;
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] панель 15168: «%s», цена %d, уровень %d"),
		*R->Name, R->Price, R->RequiredLevel);
}

void UAsdaHairShopSubsystem::ClosePanel()
{
	for (const TObjectPtr<UWidget>& W : PanelTree) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	bPanelOpen = false;
	PanelHairId = -1;
}

// ═══════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══════════════════════════════════════════
// Порт `uiHairShopWindowOnShow@0xCEEE90` — 13 шагов (см. шапку .h).
bool UAsdaHairShopSubsystem::OpenWindow()
{
	// Шаг 2: гейт «сессия уже открыта» (эталонное поле +0x298, седьмой гейт `actionExecuteSlotAbility`).
	if (bOpen) { return true; }

	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// ★ 29.09.2026 (волна 0 торговли): гейт 3 эталона `actionExecuteSlotAbility@0xCF0900` (ui_core.c:45761) — ОКНО обмена 2008
	//   открыто (`g_bPartyInviteFlag` = `CExchange+0x2C`) → `return 0` МОЛЧА. Общая точка `UAsdaTradeStateSubsystem`
	//   (пока окна 2008 нет — false). [ПОРТ] Гейты 1/2 (`g_pShopWnd` 2075 / `sub_93B448`) и 4 (`objStateAllowsRender`) — вне волны.
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI))
	{
		UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] открытие отклонено молча: открыто окно обмена 2008 (ui_core.c:45761)"));
		return false;
	}
	// ★ 25.10.2026 (ревизия sit108, М-3) [ОРИГИНАЛ]: гейт 4 `actionExecuteSlotAbility` — `objStateAllowsRender` → `return 0`
	//   МОЛЧА (стопа сцены нет). Общая точка `IsLocalPlayerBusy`; ключ `asda.Char.SitExitEtalon 0` — прежнее (гейта нет).
	if (AAsdaPlayerCharacter::IsSitExitEtalon() && GetGameInstance() && GetGameInstance()->GetSubsystem<UAsdaSystemOptionSubsystem>() && GetGameInstance()->GetSubsystem<UAsdaSystemOptionSubsystem>()->IsLocalPlayerBusy())
	{
		UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] открытие отклонено молча: персонаж занят (actionExecuteSlotAbility)"));
		return false;
	}
	// Гейты 5/6 эталона `actionExecuteSlotAbility@0xCF0900` (ui_core.c:45766/45768): `tileInteract_isControlVisible(
	//   g_pItemMainWnd)` / `isBoundControlVisible(g_pItemQtyMakeDlg)` → `return 0` МОЛЧА.
	//   ★ Поправка волны В3 ремесла (ревизия В0–В2 С-1): это окна РЕМЕСЛА 76000 и изготовления 77500, а не «окно
	//   предметов на земле / нумпад» — прежний порт подменил их нумпадом.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] открытие отклонено молча: открыто окно ремесла 76000/77500 (ui_core.c:45766)"));
		return false;
	}
	// ★ 24.10.2026 (выход из сидения 1:1) [ОРИГИНАЛ]: `actionExecuteSlotAbility@0xCF0900` — на ходу 4008, затем `charActionStateIs7to9 ? CharFx(9) : CharFx(0)`
	//   (сидящий встаёт БЕЗ 109, по концу «встаёт» — 4024 0; иначе покой). [ПОРТ] прежде шаг не был перенесён. Ключ `asda.Char.SitExitEtalon`.
	if (APlayerController* StopPc = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (AAsdaPlayerCharacter* StopCh = Cast<AAsdaPlayerCharacter>(StopPc->GetPawn())) { StopCh->ApplyEtalonSceneStop(TEXT("парикмахерская (actionExecuteSlotAbility)")); }
	}
	// Окно количества 1410: оригинал его ЗАКРЫВАЕТ (шаг 9 ниже), а не блокирует вход — ветка сохранена как была.
	if (WM && WM->IsNumPadOpen())
	{
		// ★ Шаг 9 эталона: нумпад ЗАКРЫВАЕТСЯ, а не блокирует вход (`ui_core.c:45661-45662`).
		WM->CloseNumPad(false);
	}

	if (!EnsureWidget()) { return false; }

	// Шаг 6: запомнить причёску для отката примерки.
	if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		SavedStyle = WS->GetSelfHairStyle();
		SavedColor = WS->GetSelfHairColor();
	}
	// ★★★ Шаг 7 эталона — СНЯТЬ ШЛЕМ, чтобы видеть волосы (`CCharacter_LoadHelmetPart(char, 0)`,
	//   `ui_core.c:45658`); возврат — при выходе (`charAppearance_ApplyHairHelmet@0xCF2940`).
	//   СДЕЛАНО ОБЩЕЙ ТОЧКОЙ «примерочный оверрайд экипировки», а не частным хаком: оверрайд
	//   ложится ПОВЕРХ снимка `SelfParts`, не портя авторитет надетого (контейнер 3), поэтому
	//   любая смена экипировки во время примерки шлем обратно не вернёт. Тем же механизмом
	//   воспользуются другие примерочные системы (запись #42а `PORT_PENDING_ON_SYSTEMS.md`).
	if (CVarHideHelmet.GetValueOnGameThread() != 0)
	{
		if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			TArray<FIntPoint> Off;
			Off.Emplace(0, 0);   // слот 0 = шлем (`tagCHARACTER_SetPartsModelByType`), 0 = снять
			WS->PushEquipTryOn(kTryOnOwner, Off);
		}
	}

	// Шаг 4: полный сброс состояния окна.
	bPanelOpen = false;
	bPending   = false;
	SelCard    = -1;
	Page       = 0;
	ClosePanel();
	ApplyGridLayout();   // ★ сетка под высоту экрана (расширение, ключ `asda.HairShop.FitScreen`)
	BuildList();
	BuildPage();
	RefreshCash();

	// Шаг 10: запрос баланса премиум-валюты.
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SendPremiumBalanceQuery5466(Net->SelectedCharId);
	}

	// ★ Волна В6 ремесла (W5-7): `uiHairShopWindowOnShow` перед показом зовёт `dialogOnConfirmClose(g_pNpcWnd, 1)`
	//   (ui_core.c:45670) — разговор (вход 30029) ПРЯЧЕТСЯ под окном и живёт до его закрытия (5045). Без разговора — ничего.
	if (UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr)
	{
		Dlg->HideForChildWindow(TEXT("парикмахерская 15000 (uiHairShopWindowOnShow → dialogOnConfirmClose)"));
	}

	// Шаг 13: показать окно.
	if (WM)
	{
		WM->SetWindowVisible(kWindowId, true);
		WM->BringWindowToFront(kWindowId);
	}
	// ★ Флаг сессии ставим ПОСЛЕ показа (правило владельца «не защёлкивать до показа»).
	bOpen = true;
	// Через кадр у окна появится ЖИВАЯ геометрия — тогда сетка пересчитается по ней (см. `RelayoutIfNeeded`).
	if (UWorld* World = GI ? GI->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(RelayoutTimer);
		World->GetTimerManager().SetTimer(RelayoutTimer,
			FTimerDelegate::CreateUObject(this, &UAsdaHairShopSubsystem::RelayoutIfNeeded), 0.25f, false);
	}
	++CntOpen;

	// Шаг 11: приветственные нотисы в ЧАТ.
	NoticeChat(kLocHello1);
	NoticeChat(kLocHello2);

	if (CVarPriceWarn.GetValueOnGameThread() != 0)
	{
		// ★★★ ДЕФЕКТ ДАННЫХ D2 УСТРАНЁН СВЕДЕНИЕМ ТАБЛИЦ 2026-08-28: витрину рисует клиентская
		//   `hairtable`, а деньги списывает сервер по своей `ItemTable_Info/HairTypeTable.txt`,
		//   и цены в них разошлись (198 строк из 228). Клиентская таблица приведена к серверной
		//   инструментом `Tools/asda_hairtable_sync.py` (он же сверяет ВСЕ шесть общих полей).
		//   ⚠ Числа сюда НЕ ЗАШИТЫ: клиент серверной таблицы не видит и проверить расхождение в
		//   рантайме не может — сверка делается инструментом ДО запуска. Если владелец правит
		//   ассортимент, он обязан прогнать тот же инструмент, иначе витрина снова разъедется.
		UE_LOG(LogAsdaHair, Log,
			TEXT("[парикмахерская] витрина показывает цены из DT_hairtable; симметрия с серверной "
			     "HairTypeTable.txt поддерживается Tools/asda_hairtable_sync.py "
			     "(ключ asda.HairShop.PriceMismatch 0 убирает это напоминание)"));
	}
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] открыта: %d причёсок, страница %d/%d"),
		ListIds.Num(), Page + 1, MaxPage + 1);
	return true;
}

// Порт `hairShopWindowLeave@0xCEF3A0` (`shop_trade.c:11649-11670`) + нотис 13015.
void UAsdaHairShopSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	RevertPreview();            // ★ откат примерки — эталон `charAppearance_ApplyHairHelmet`
	ClosePanel();
	if (UGameInstance* GI = GetGameInstance())
	{
		// ★ ВЕРНУТЬ ШЛЕМ: снимаем свой примерочный оверрайд — модель пересобирается по авторитету.
		if (UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>()) { WS->PopEquipTryOn(kTryOnOwner); }
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetWindowVisible(kWindowId, false); }
		// Модалка 1559 могла остаться поверх закрытого окна — снимаем её без ответа.
		if (UAsdaPurchaseBoxSubsystem* Box = GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>())
		{
			if (Box->IsOpen() && Box->GetMode() == EAsdaPurchaseMode::HairShop) { Box->Close(); }
		}
	}
	bOpen = false;
	bPending = false;
	SelCard = -1;
	NoticeChat(kLocBye);
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] закрыта (нотис %d)"), kLocBye);
	// ★ Волна В6 ремесла (W5-7): `uiLeaveAndResetAllWindows@0xCEF180` безусловно зовёт `UI_resetAllWindowsAndLeave_5045`
	//   (ui_core.c:45705) — выход из разговора (если он спрятан под окном) или хвост 5045 без разговора. Общая точка 5045.
	if (UAsdaNpcDialogSubsystem* Dlg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr)
	{
		Dlg->ResetAllWindowsAndLeave(TEXT("закрытие парикмахерской (uiLeaveAndResetAllWindows)"));
	}
}

void UAsdaHairShopSubsystem::EscClose()
{
	if (!bOpen) { return; }
	// Эталон `boosterShopWndProc@0xCEDB90`, событие 0x15:
	//     if (this->purchaseWindow->isVisible) sub_9363F3(this);                        // панель 15168 → закрыть её
	//     else { uiLeaveAndResetAllWindows(this); uiStorageWindow_Close(g_bModalNoticeActive); }   // окно целиком
	if (bPanelOpen)
	{
		ClosePanel();
		UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] ESC: закрыта панель покупки 15168, окно открыто"));
		return;
	}
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] ESC: закрываю окно 15000"));
	CloseWindow();
}

// ═══════════════════════════════ ДЕЙСТВИЯ ══════════════════════════════════════════════════════
// Клик по САМОЙ карточке (эталонное сообщение 0xB): выбрать + ПРИМЕРИТЬ, панель не открывать.
void UAsdaHairShopSubsystem::OnCardClicked(int32 Card)
{
	if (!bOpen || !Cards.IsValidIndex(Card) || Cards[Card].HairId < 0) { return; }
	if (!GateNoPanel()) { return; }
	SelCard = Card;
	PreviewHair(Cards[Card].HairId);
	MoveSelectionFrame(Card);
}

// Кнопка «Купить» 15009+9i: то же самое И открыть панель 15168 (`uiHandleMenuButtonCommand`).
// ★★★ «НИЧЕГО НЕ ПРОИСХОДИТ» БЫТЬ НЕ ДОЛЖНО: каждый ранний выход пишет ПРИЧИНУ в журнал.
//   Раньше три условия молча возвращали, и отличить «клик не дошёл» от «клик отбит» было нечем.
void UAsdaHairShopSubsystem::OnCardBuyClicked(int32 Card)
{
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] «Купить» карточка %d (id=%d)"),
		Card, Cards.IsValidIndex(Card) ? Cards[Card].HairId : -1);
	if (!bOpen)
	{
		UE_LOG(LogAsdaHair, Warning, TEXT("[парикмахерская] «Купить» отклонено: сессия окна закрыта"));
		return;
	}
	if (!Cards.IsValidIndex(Card))
	{
		UE_LOG(LogAsdaHair, Warning, TEXT("[парикмахерская] «Купить» отклонено: карточки %d нет (всего %d)"),
			Card, Cards.Num());
		return;
	}
	if (Cards[Card].HairId < 0)
	{
		UE_LOG(LogAsdaHair, Warning,
			TEXT("[парикмахерская] «Купить» отклонено: карточка %d пуста (на странице %d/%d причёсок %d)"),
			Card, Page + 1, MaxPage + 1, ListIds.Num());
		return;
	}
	if (!GateNoPanel()) { return; }
	SelCard = Card;
	PreviewHair(Cards[Card].HairId);
	MoveSelectionFrame(Card);
	// ★ ПУНКТ 1: по умолчанию панель 15168 из пути покупки убрана — сразу общая модалка 1559.
	if (CVarLegacyConfirmPanel.GetValueOnGameThread() != 0) { OpenPanel(Cards[Card].HairId); }
	else                                                    { OpenConfirmModal(Cards[Card].HairId); }
}

void UAsdaHairShopSubsystem::OnOriginHairClicked()
{
	if (!bOpen) { return; }
	if (!GateNoPanel()) { return; }
	RevertPreview();
	SelCard = -1;
	MoveSelectionFrame(-1);
}

// Кнопка 15178 «Да» → ОБЩАЯ модалка 1559 в режиме **E** (`upgradeUiFillConfirmPanel@0xD7BD90`).
//   ★ Второй модалки не заводим — это общая точка `UAsdaPurchaseBoxSubsystem` (закон
//   `port-shared-points`). Пакет 5470 шлёт ТОЛЬКО подтверждение модалки, как в эталоне.
void UAsdaHairShopSubsystem::OnPanelConfirmClicked()
{
	if (!bPanelOpen || PanelHairId < 0)
	{
		UE_LOG(LogAsdaHair, Warning, TEXT("[парикмахерская] 15178 отклонено: панель %s, id=%d"),
			bPanelOpen ? TEXT("открыта") : TEXT("закрыта"), PanelHairId);
		return;
	}
	OpenConfirmModal(PanelHairId);
}

// ★ ОБЩАЯ МОДАЛКА 1559 в режиме **E** (`upgradeUiFillConfirmPanel@0xD7BD90`). Второй модалки не
//   заводим — это общая точка `UAsdaPurchaseBoxSubsystem` (закон `port-shared-points`).
//   Пакет 5470 шлёт ТОЛЬКО подтверждение модалки, как в эталоне.
void UAsdaHairShopSubsystem::OpenConfirmModal(int32 InHairId)
{
	const FAsdaHairtableRow* R = RowById(InHairId);
	if (!R) { NoticeChat(kLocNoRecord); return; }
	PanelHairId = InHairId;

	UGameInstance* GI = GetGameInstance();
	UAsdaPurchaseBoxSubsystem* Box = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
	if (!Box) { return; }

	FAsdaPurchaseBoxRequest Req;
	Req.Mode          = EAsdaPurchaseMode::HairShop;   // режим E (кнопка 15178)
	Req.ItemCode      = 0;                             // у причёски своя таблица — записи предмета нет
	Req.PanelMode     = 0;                             // вопрос loc 12923 «Хочешь купить эту вещь?»
	Req.Quantity      = 0;
	Req.TradeMode     = -1;                            // строка «Поместить на хранение» не заполняется
	Req.NameOverride  = HairText(GetGameInstance(), R, TEXT("Name"), R->Name);   // эталон передаёт имя ПАРАМЕТРОМ ([ПОРТ] 09.10.2026 (мультиязычность))
	Req.PriceOverride = R->Price;                      // и цену тоже (`requiredAmount`)
	const int32 HairId = InHairId;
	Box->Open(Req, [this, HairId](bool bOk)
	{
		if (!bOk) { return; }                          // «Отмена» — панель 15168 остаётся открытой
		const FAsdaHairtableRow* Rec = RowById(HairId);
		if (!Rec) { NoticeChat(kLocNoRecord); return; }
		if (bPending)
		{
			// Гейт busy отправителя (`shop_trade.c:6175-6178` / `ida.c:545110-545114`).
			NoticeChat(kLocNoAnswer);
			return;
		}
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			if (Net->SendHairShopBuy5470(Rec->Id, (uint8)Rec->HairStyle, (uint8)Rec->ColorIdx))
			{
				bPending = true;
				++CntBuy;
			}
		}
	});
}

void UAsdaHairShopSubsystem::OnPanelCancelClicked()
{
	ClosePanel();
}

void UAsdaHairShopSubsystem::OnChargeClicked()
{
	if (!GateNoPanel()) { return; }
	// ★ 27.10.2026 (владелец): внешний браузер не открываем — ссылка на area-game.ru снята до своего сайта.
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] «пополнить» (15166) → ссылка снята, ничего не открываем"));
}

// ★ Кнопка 15167 у эталона гейта «панель открыта» НЕ имеет (`case 0x3B3F` идёт сразу к 5466).
void UAsdaHairShopSubsystem::OnBalanceClicked()
{
	AsdaShopUi::QueryPremiumBalance(GetGameInstance());   // ★ волна 3: пакет 5466 с выбранным персонажем — общая точка
}

// Порт `uiListPaginationHandler@0xCEE1C0`.
void UAsdaHairShopSubsystem::SetPage(int32 NewPage)
{
	if (!bOpen) { return; }
	if (!GateNoPanel()) { return; }
	const int32 P = FMath::Clamp(NewPage, 0, MaxPage);
	if (P == Page) { return; }
	Page = P;
	SelCard = -1;
	BuildPage();
}

void UAsdaHairShopSubsystem::SetFilterOnlyMine(bool bOn)
{
	if (bOnlyMine == bOn) { return; }
	bOnlyMine = bOn;
	Page = 0;
	SelCard = -1;
	BuildList();
	BuildPage();
}

// ═══════════════════════════════ СЕТЬ ══════════════════════════════════════════════════════════
// Порт `hairShop_HandleResult@0xC4D390` (`shop_trade.c:6216-6280`).
void UAsdaHairShopSubsystem::OnHairResult(const TArray<uint8>& B)
{
	if (B.Num() < 9) { return; }
	++CntResult;
	// ★★★ D1 ИСПРАВЛЕН: busy снимает ИМЕННО приём ответа. У оригинала приёмник обнулял ДРУГОЙ
	//   флаг, и вторая покупка в сессии отбивалась нотисом 1026.
	if (CVarBusyFlagLeak.GetValueOnGameThread() == 0) { bPending = false; }

	const uint8 Code = B[0];
	if (Code == 1)
	{
		const int32 Style = (int32)B[3];
		const int32 Color = (int32)B[4];
		const int32 Rest  = (int32)((uint32)B[5] | ((uint32)B[6] << 8) | ((uint32)B[7] << 16) | ((uint32)B[8] << 24));
		if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			WS->ApplySelfHair(Style, Color);
		}
		// ★★★ D4 ИСПРАВЛЕН: баланс кладём в СВОЁ окно (15164), а не в контролы магазина 6000.
		//   ★★ ПУНКТ 7 ВЛАДЕЛЬЦА: поле +5 пакета 5471 — это СЕРВЕРНЫЙ КЭШ (`sub_43FC20`), а не база.
		//   Живые числа: 592786 → покупка → 5471 дал 592748 → 5466 из базы вернул 592767.
		//   Показываем кэш только ради мгновенной реакции, а следом сразу спрашиваем базу (5466):
		//   её ответ 5467 и перепишет надпись, и заодно ПРОШЬЁТ серверный кэш (`sub_4F4810`).
		CashGxp    = Rest;
		SavedStyle = Style;     // покупка НЕОБРАТИМА — новая причёска становится «как было»
		SavedColor = Color;
		RefreshCash();
		if (CVarTrustResultBalance.GetValueOnGameThread() == 0)
		{
			if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
			{
				Net->SendPremiumBalanceQuery5466(Net->SelectedCharId);
				UE_LOG(LogAsdaHair, Log,
					TEXT("[парикмахерская] баланс из 5471 = %d — перезапрашиваем базу (5466), ")
					TEXT("ключ asda.HairShop.TrustResultBalance 1 отключает"), Rest);
			}
		}
		// Причёска сменилась ⇒ пересобрать страницу: отметка «Куплено» должна переехать.
		//   ⚠ Только при ОТКРЫТОМ окне: ответ 5471 может прийти и после закрытия (окно закрыли,
		//   пока пакет был в полёте), а трогать снятые виджеты незачем.
		ClosePanel();
		if (bOpen) { BuildPage(); }
		NoticeChat(kResLoc[1]);   // 13005 «Прическа изменилась.»
		UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] покупка успешна: стиль=%d цвет=%d остаток GXP=%d"),
			Style, Color, Rest);
		return;
	}
	if (Code < UE_ARRAY_COUNT(kResLoc)) { NoticeChat(kResLoc[Code]); }
	UE_LOG(LogAsdaHair, Log, TEXT("[парикмахерская] отказ покупки: код=%d нотис=%d"),
		(int32)Code, Code < UE_ARRAY_COUNT(kResLoc) ? kResLoc[Code] : -1);
}

// Порт ветки 5467 (`etalon/network.c:32193-32232`): парикмахерская обновляет СВОЙ баланс 15164.
//   Сами нотисы 12942/12943/12944/12165 эмитирует общий приёмник — второй раз их не шлём.
void UAsdaHairShopSubsystem::OnBalance(uint8 Status, int32 Balance)
{
	if (Status != 1) { return; }
	CashGxp = Balance;
	RefreshCash();
}

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
FString UAsdaHairShopSubsystem::DumpControls() const
{
	FString S = TEXT("=== HAIRSHOP_WIN 15000 ===\n");
	S += FString::Printf(TEXT("  15001 крестик %s · 15182 «как было» %s · 15166 донат %s · 15167 баланс %s\n"),
		BtnClose ? TEXT("есть") : TEXT("НЕТ"), BtnOrigin ? TEXT("есть") : TEXT("НЕТ"),
		BtnCharge ? TEXT("есть") : TEXT("НЕТ"), BtnBalance ? TEXT("есть") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  пагинация 15154/15156/15158/15160: %s/%s/%s/%s · счётчики %s/%s\n"),
		BtnNext ? TEXT("+") : TEXT("−"), BtnPrev ? TEXT("+") : TEXT("−"),
		BtnLast ? TEXT("+") : TEXT("−"), BtnFirst ? TEXT("+") : TEXT("−"),
		TxtPage ? TEXT("+") : TEXT("−"), TxtPages ? TEXT("+") : TEXT("−"));
	S += FString::Printf(TEXT("  панель 15168: узлов %d · 15173 %s 15174 %s 15183 %s 15184 %s 15178 %s 15179 %s\n"),
		PanelTree.Num(), PurTitle ? TEXT("+") : TEXT("−"), PurContent ? TEXT("+") : TEXT("−"),
		PurLvVal ? TEXT("+") : TEXT("−"), PurMacVal ? TEXT("+") : TEXT("−"),
		PurOk ? TEXT("+") : TEXT("−"), PurCancel ? TEXT("+") : TEXT("−"));
	int32 Ok = 0;
	for (const FAsdaHairCardUi& C : Cards) { if (C.Root && C.BuyButton && C.NameText) { ++Ok; } }
	S += FString::Printf(TEXT("  карточек полностью размечено %d из %d (видимых по данным %d) · рамка 15149: %d узлов\n"),
		Ok, kCards, kCardsFit, SelFrame.Num());
	return S;
}

FString UAsdaHairShopSubsystem::DumpState() const
{
	return FString::Printf(
		TEXT("15000: открыто=%s панель=%s | причёсок %d стр.%d/%d по %d | GXP=%d выбрано=%d ")
		TEXT("| в полёте=%s открытий=%d покупок=%d ответов=%d | откат стиль=%d цвет=%d\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bPanelOpen ? TEXT("да") : TEXT("нет"),
		ListIds.Num(), Page + 1, MaxPage + 1, CardsPerPage(), CashGxp, SelCard,
		bPending ? TEXT("да") : TEXT("нет"), CntOpen, CntBuy, CntResult, SavedStyle, SavedColor);
}

FString UAsdaHairShopSubsystem::SelfTest() const
{
	FString S = TEXT("=== asda.HairShop.SelfTest ===\n");
	S += DumpState();
	S += DumpControls();
	if (!Widget) { S += TEXT("  ВИДЖЕТ НЕ СОЗДАН\n"); return S; }
	auto Row = [&S](const TCHAR* Tag, const UWidget* W)
	{
		if (!W) { S += FString::Printf(TEXT("  %-26s НЕТ УЗЛА\n"), Tag); return; }
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Abs = G.GetAbsolutePosition();
		const FVector2D Sz  = G.GetLocalSize();
		int32 ZIdx = -1;
		if (const UPanelWidget* P = W->GetParent())
		{
			for (int32 i = 0; i < P->GetChildrenCount(); ++i) { if (P->GetChildAt(i) == W) { ZIdx = i; break; } }
		}
		FString Txt;
		if (const UTextBlock* Tb = Cast<UTextBlock>(W)) { Txt = Tb->GetText().ToString(); }
		S += FString::Printf(TEXT("  %-26s поз(%.0f,%.0f) разм(%.0f×%.0f) вид=%d порядок=%d «%s»\n"),
			Tag, Abs.X, Abs.Y, Sz.X, Sz.Y, (int32)W->GetVisibility(), ZIdx, *Txt);
	};
	Row(TEXT("15164 баланс"),   TxtCash);
	Row(TEXT("15163 «Сейчас GXP»"), TxtCashLbl);
	Row(TEXT("15151 страница"), TxtPage);
	Row(TEXT("15152 всего"),    TxtPages);
	Row(TEXT("15182 кнопка"),   BtnOrigin);
	// ★ Подпись 15182 живёт в ОТДЕЛЬНОМ узле-тексте (его и переименовали строкой 900003).
	Row(TEXT("15182 подпись"),  AsdaUi::FindNodeExact(Widget, TEXT("HAIR_ORIGINHAIR_Text")));
	// ★ Красный бейдж уровня «L»: при нулевом уровне должен быть свёрнут (вид=1).
	if (Cards.Num() > 0) { Row(TEXT("бейдж уровня карт.0"), Cards[0].LevelBadge.Get()); }
	if (Cards.Num() > 0) { Row(TEXT("уровень карт.0"),      Cards[0].LevelText.Get()); }
	for (int32 i = 0; i < 3 && i < Cards.Num(); ++i)
	{
		Row(*FString::Printf(TEXT("карточка %d имя"), i),  Cards[i].NameText);
		Row(*FString::Printf(TEXT("карточка %d цена"), i), Cards[i].PriceText);
		Row(*FString::Printf(TEXT("карточка %d кнопка"), i), Cards[i].BuyButton);
	}
	Row(TEXT("15173 панель имя"), PurTitle);
	Row(TEXT("15178 панель «Да»"), PurOk);
	return S;
}

// ★★★ ЗОНД «КТО СЪЕЛ КЛИК» — см. объявление в .h. Печатает по каждой карточке: саму кнопку и то,
//   что Slate находит под её центром. Ответ «под центром лежит НЕ эта кнопка» и есть доказательство.
FString UAsdaHairShopSubsystem::ProbeCards(bool bSlate) const
{
	FString S = FString::Printf(TEXT("[зонд 15000] окно %s, карточек %d, на страницу %d, строк %d\n"),
		bOpen ? TEXT("ОТКРЫТО") : TEXT("закрыто"), Cards.Num(), CardsPerPage(), GridRows);
	// ⚠ Опрос Slate вынесен под флаг: он ходит по ЖИВЫМ окнам приложения, и если проверка идёт в
	//   headless-прогоне, полезно уметь снять только дерево и геометрию (аргумент `slate`).
	FSlateApplication* App = (bSlate && FSlateApplication::IsInitialized()) ? &FSlateApplication::Get() : nullptr;
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		// Пошаговая отметка: если зонд где-то встанет, в журнале видно, на какой карточке.
		UE_LOG(LogAsdaHair, Verbose, TEXT("[зонд 15000] карточка %d/%d"), i, Cards.Num());
		const FAsdaHairCardUi& C = Cards[i];
		const UButton* B = C.BuyButton.Get();
		if (!B) { S += FString::Printf(TEXT("  [%2d] кнопки «Купить» НЕТ В ДЕРЕВЕ\n"), i); continue; }
		FVector2D Mn, Mx;
		const bool bAbs = AsdaUi::WidgetAbsRect(B, Mn, Mx);
		// ★ Видимость ВСЕЙ цепочки родителей: `HitTestInvisible` у любого предка гасит клики всему
		//   поддереву (закон `selfhittest-vs-hittest-invisible`), а `Collapsed` — и подавно.
		FString Chain;
		for (const UWidget* P = B->GetParent(); P; P = P->GetParent())
		{
			Chain += FString::Printf(TEXT(" <- %s(vis=%d)"), *P->GetName(), (int32)P->GetVisibility());
		}
		S += FString::Printf(TEXT("  [%2d] %-20s id=%3d vis=%d %s абс=(%.0f,%.0f %.0fx%.0f)%s\n"),
			i, *B->GetName(), C.HairId, (int32)B->GetVisibility(),
			B->GetIsEnabled() ? TEXT("вкл") : TEXT("★ВЫКЛ"),
			bAbs ? Mn.X : -1.f, bAbs ? Mn.Y : -1.f, bAbs ? (Mx.X - Mn.X) : -1.f, bAbs ? (Mx.Y - Mn.Y) : -1.f,
			*Chain);
		if (!App || !bAbs || B->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		const FVector2D Ctr = (Mn + Mx) * 0.5f;
		const TSharedPtr<SWidget> Self = const_cast<UButton*>(B)->GetCachedWidget();
		FWidgetPath Path = App->LocateWindowUnderMouse(Ctr, App->GetInteractiveTopLevelWindows());
		bool bInPath = false;
		for (int32 k = 0; k < Path.Widgets.Num(); ++k)
		{
			if (Self.IsValid() && Path.Widgets[k].Widget == Self) { bInPath = true; break; }
		}
		FString Tail;
		for (int32 k = FMath::Max(0, Path.Widgets.Num() - 5); k < Path.Widgets.Num(); ++k)
		{
			Tail += FString::Printf(TEXT(" / %s"), *Path.Widgets[k].Widget->GetTypeAsString());
		}
		S += FString::Printf(TEXT("       под центром (%.0f,%.0f): кнопка в пути %s; хвост:%s\n"),
			Ctr.X, Ctr.Y, bInPath ? TEXT("ЕСТЬ") : TEXT("★НЕТ — ПЕРЕКРЫТА"), *Tail);
	}
	return S;
}

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
void UAsdaHairShopSubsystem::ClickCardBuy(int32 Card)
{
	if (!FSlateApplication::IsInitialized() || !Cards.IsValidIndex(Card)) { return; }
	UButton* B = Cards[Card].BuyButton.Get();
	FVector2D Mn, Mx;
	if (!B || !AsdaUi::WidgetAbsRect(B, Mn, Mx))
	{
		UE_LOG(LogAsdaHair, Warning, TEXT("[зонд 15000] у кнопки «Купить» карточки %d нет живой геометрии"), Card);
		return;
	}
	const FVector2D Ctr = (Mn + Mx) * 0.5f;
	FSlateApplication& App = FSlateApplication::Get();
	const TSharedPtr<SWidget> Self = B->GetCachedWidget();
	FWidgetPath Path = App.LocateWindowUnderMouse(Ctr, App.GetInteractiveTopLevelWindows());
	const TSharedPtr<SWidget> Leaf = (Path.Widgets.Num() > 0) ? TSharedPtr<SWidget>(Path.Widgets.Last().Widget) : nullptr;
	bool bInPath = false;
	for (int32 k = 0; k < Path.Widgets.Num(); ++k)
	{
		if (Self.IsValid() && Path.Widgets[k].Widget == Self) { bInPath = true; break; }
	}
	// ★ Эффект клика меряем по ТОМУ, ЧТО ТЕПЕРЬ ОТКРЫВАЕТСЯ: по умолчанию это общая модалка 1559,
	//   а панель 15168 остаётся в пути только при `asda.HairShop.LegacyConfirmPanel 1`.
	auto ConfirmUp = [this]() -> bool
	{
		if (bPanelOpen) { return true; }
		const UAsdaPurchaseBoxSubsystem* Box = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
		return Box && Box->IsOpen() && Box->GetMode() == EAsdaPurchaseMode::HairShop;
	};
	const bool bPanelBefore = ConfirmUp();
	// ⚠ Синтетический `ProcessMouseButtonDownEvent` в headless не годится (курсор физически не
	//   двигается) — поэтому клик подаём той же цепочкой `Press → ExecuteOnClick`, что и мышь.
	if (Leaf.IsValid() && Leaf->GetType() == TEXT("SButton"))
	{
		StaticCastSharedPtr<SButton>(Leaf)->SimulateClick();
	}
	UE_LOG(LogAsdaHair, Log,
		TEXT("[зонд 15000] клик «Купить» карточки %d в (%.0f,%.0f): под курсором %s, кнопка в пути %s; подтверждение %s → %s %s"),
		Card, Ctr.X, Ctr.Y,
		Leaf.IsValid() ? *Leaf->GetType().ToString() : TEXT("НИЧЕГО"),
		bInPath ? TEXT("ЕСТЬ") : TEXT("НЕТ — ПЕРЕКРЫТА"),
		bPanelBefore ? TEXT("было") : TEXT("не было"), ConfirmUp() ? TEXT("открыто") : TEXT("закрыто"),
		(ConfirmUp() != bPanelBefore) ? TEXT("— СРАБОТАЛО") : TEXT("— БЕЗ ЭФФЕКТА"));
}
#endif // !UE_BUILD_SHIPPING

// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ════════════════════════════════════════════
//   ★★★ ПРОВЕРКА — ПУТЁМ ИГРОКА: команды повторяют РОВНО те же входы, что мышь.
//     UnrealEditor.exe <uproject> /Game/Map/alpen/alpen -game
//       -ExecCmds="asda.HairShop.Open, asda.HairShop.SelfTest 4 quit"
namespace
{
	UAsdaHairShopSubsystem* HairSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaHairShopSubsystem>() : nullptr;
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U13): было `AddOnScreenDebugMessage` (12 с поверх экрана; в Shipping оверлей пуст) — стало общая точка `AsdaNotice::ConsoleEcho`: каждая строка в журнал (Display), непустая — в ленту Tip.
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
	void HairEcho(UWorld* W, const FString& S) { AsdaNotice::ConsoleEcho(W, S, LogAsdaHair); }
#endif // !UE_BUILD_SHIPPING
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHairOpen(
	TEXT("asda.HairShop.Open"),
	TEXT("asda.HairShop.Open — открыть парикмахерскую 15000 тем же путём, что кнопка NPC 132"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHairShopSubsystem* H = HairSub(W)) { H->OpenWindow(); HairEcho(W, H->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHairClose(
	TEXT("asda.HairShop.Close"),
	TEXT("asda.HairShop.Close — выйти из парикмахерской (откат примерки + нотис 13015)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaHairShopSubsystem* H = HairSub(W)) { H->CloseWindow(); HairEcho(W, H->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHairCard(
	TEXT("asda.HairShop.Card"),
	TEXT("asda.HairShop.Card <0..13> [buy] — клик по карточке (примерка); с `buy` — кнопка «Купить» (панель 15168)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHairShopSubsystem* H = HairSub(W);
		if (!H || A.Num() < 1) { HairEcho(W, TEXT("использование: asda.HairShop.Card <0..13> [buy]")); return; }
		const int32 I = FCString::Atoi(*A[0]);
		const bool bBuy = A.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("buy"), ESearchCase::IgnoreCase); });
		if (bBuy) { H->OnCardBuyClicked(I); } else { H->OnCardClicked(I); }
		HairEcho(W, H->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHairPanel(
	TEXT("asda.HairShop.Panel"),
	TEXT("asda.HairShop.Panel <ok|cancel> — нажать кнопку панели 15168 (ok = 15178 → общая модалка 1559)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHairShopSubsystem* H = HairSub(W);
		if (!H || A.Num() < 1) { HairEcho(W, TEXT("использование: asda.HairShop.Panel <ok|cancel>")); return; }
		if (A[0].Equals(TEXT("ok"), ESearchCase::IgnoreCase)) { H->OnPanelConfirmClicked(); }
		else { H->OnPanelCancelClicked(); }
		HairEcho(W, H->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHairPage(
	TEXT("asda.HairShop.Page"),
	TEXT("asda.HairShop.Page <номер> — перейти на страницу списка причёсок"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaHairShopSubsystem* H = HairSub(W);
		if (!H || A.Num() < 1) { HairEcho(W, TEXT("использование: asda.HairShop.Page <номер>")); return; }
		H->SetPage(FCString::Atoi(*A[0]));
		HairEcho(W, H->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHairCash(
	TEXT("asda.HairShop.SetCash"),
	TEXT("asda.HairShop.SetCash <сумма> — подставить баланс кэша, который приносит 5467 (диагностика без сервера)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный баланс обходит клиентский гейт покупки (5470).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.HairShop.SetCash"))) { return; }
		UAsdaHairShopSubsystem* H = HairSub(W);
		if (!H || A.Num() < 1) { HairEcho(W, TEXT("использование: asda.HairShop.SetCash <сумма>")); return; }
		H->DebugSetCash(FCString::Atoi(*A[0]));
		HairEcho(W, H->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ЗОНД КЛИКА — прямой ответ на замечание владельца «кнопки Купить не нажимаются».
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHairProbe(
	TEXT("asda.HairShop.Probe"),
	TEXT("asda.HairShop.Probe [задержкаСек] [slate] [quit] — дерево, геометрия и вид каждой кнопки "
	     "«Купить»; со словом `slate` ещё и живой хит-тест (кто лежит под её центром). "
	     "Без задержки геометрия НУЛЕВАЯ."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		const float Delay = FAsdaSelfTestArgs::Parse(A).Float(0, 0.f);   // ★ 11.10.2026 (В3 S1): разбор каркаса — `quit` первым аргументом не читается как задержка
		const bool bQuit  = A.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		const bool bSlate = A.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("slate"), ESearchCase::IgnoreCase); });
		auto Run = [W, bQuit, bSlate]()
		{
			if (UAsdaHairShopSubsystem* H = HairSub(W)) { HairEcho(W, H->ProbeCards(bSlate)); }
			if (bQuit && GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
		};
		if (Delay <= 0.f || !W) { Run(); return; }
		FTimerHandle T;
		W->GetTimerManager().SetTimer(T, FTimerDelegate::CreateLambda(Run), Delay, false);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHairClickBuy(
	TEXT("asda.HairShop.ClickBuy"),
	TEXT("asda.HairShop.ClickBuy <карточка> [задержкаСек] [quit] — НАСТОЯЩИЙ клик мышью по кнопке "
	     "«Купить» (хит-тест Slate + SButton::SimulateClick). Без задержки геометрии ещё НЕТ."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		if (A.Num() < 1) { HairEcho(W, TEXT("использование: asda.HairShop.ClickBuy <карточка> [задержка] [quit]")); return; }
		const int32 Card  = FCString::Atoi(*A[0]);
		const float Delay = (A.Num() > 1) ? FCString::Atof(*A[1]) : 0.f;
		const bool  bQuit = A.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		auto Run = [W, Card, bQuit]()
		{
			if (UAsdaHairShopSubsystem* H = HairSub(W)) { H->ClickCardBuy(Card); HairEcho(W, H->DumpState()); }
			if (bQuit && GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
		};
		if (Delay <= 0.f || !W) { Run(); return; }
		FTimerHandle T;
		W->GetTimerManager().SetTimer(T, FTimerDelegate::CreateLambda(Run), Delay, false);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GHairSelfTest(
	TEXT("asda.HairShop.SelfTest"),
	TEXT("asda.HairShop.SelfTest [задержкаСек] [quit] — ФАКТИЧЕСКАЯ экранная геометрия узлов "
	     "парикмахерской ПОСЛЕ отрисовки. Без задержки геометрия НУЛЕВАЯ."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		const float Delay = FAsdaSelfTestArgs::Parse(A).Float(0, 0.f);   // ★ 11.10.2026 (В3 S1): разбор каркаса — `quit` первым аргументом не читается как задержка
		const bool bQuit = A.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		auto Run = [W, bQuit]()
		{
			if (UAsdaHairShopSubsystem* H = HairSub(W)) { HairEcho(W, H->SelfTest()); }
			else { HairEcho(W, TEXT("подсистема парикмахерской не поднята")); }
			if (bQuit && GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }
		};
		if (Delay <= 0.f || !W) { Run(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Run), Delay, false);
		HairEcho(W, FString::Printf(TEXT("самотест снимет геометрию через %.1f с%s"),
			Delay, bQuit ? TEXT(" и закроет игру") : TEXT("")));
	}));
#endif // ASDA_DEV_CONSOLE
