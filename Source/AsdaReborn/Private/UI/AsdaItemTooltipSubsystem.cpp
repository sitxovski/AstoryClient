// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaItemTooltipSubsystem — реализация. Полный разбор эталона — в шапке .h.
//   Ключевые функции оригинала:
//     ItemTooltip_Build@0xEA5260              (item_inventory.c:60296) — окно + заголовок + сравнение
//     Item_BuildTooltipText@0xEC2890          (item_inventory.c:62253) — 18 секций тела
//     item_buildStatTooltip@0xEC5300          (item_inventory.c:63162) — строки статов
//     CItemManager_BuildSowelTooltip@0xEC4F80 (item_inventory.c:63091) — агрегат самоцветов
//     Item_BuildOptionTooltip@0xEC5980        (item_inventory.c:63311) — опции спец-предмета
//     itemTooltip_FormatStatRange*@0xEC4230…  (item_inventory.c:62828…) — «%d ~ %d»
//     ItemTooltip_PopulateInfoPanel@0xD02C00  (item_inventory.c:36411) — правая инфо-панель
//     itemTooltipBuildByCode@0xEA6B80         (item_inventory.c:60709) — короткий тултип по коду
#include "UI/AsdaItemTooltipSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaItemTipStandalone.h"   // ★ [ПОРТ] 29.10.2026: «отдельный комплект» просмотрщика предметов редактора
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)

#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"   // ★ U17: синтетическая запись предмета (57 б) одной точкой
#include "UI/AsdaNpcShopSubsystem.h"         // потолок цены продажи сервера (`ServerSellUnitCap`) — одна точка
#include "UI/AsdaSowelSubsystem.h"           // источник события «нет свободных гнёзд» (SOWELL_SELBOX)
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaTextureSubsystem.h"   // атлас иконки инфо-панели (423)
#include "AsdaNetSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/DataTable.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"              // кнопки окна SOWELL_SELBOX (1532/1533)
#include "Components/ProgressBar.h"
#include "Components/CanvasPanelSlot.h"     // эталонные сдвиги контролов инфо-панели
#include "Components/TextWidgetTypes.h"     // UTextLayoutWidget — чтение Justification рефлексией
#include "UObject/UnrealType.h"             // FByteProperty
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"              // ★ Д3 круга 5: подбор кегля под ширину контрола панели
#include "HAL/IConsoleManager.h"
#include "UObject/Class.h"        // StaticEnum<EAsdaTipOwner> для диагностических строк
#include "Text/AsdaLang.h"         // [ПОРТ] 09.10.2026 (мультиязычность): формат параметра на языке сессии
#include "AsdaItemTooltipSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaItemTooltipSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaItemTip);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ★ ДЕФЕКТ ОРИГИНАЛА Д4 (ИСПРАВЛЕН, закон `fix-original-defects`).
//   `item_buildStatTooltip@0xEC5300`: если код предмета не попал НИ в диапазон обычных предметов,
//   НИ в диапазон самоцветов, локальный `lineBuffer` НЕ перезаписывается — и в итоговый текст
//   вторично уходит строка ПРЕДЫДУЩЕЙ итерации (дубль стата). Мы такие пары пропускаем.
//   Вернуть эталонное поведение: `asda.ItemTip.LegacyStatDupBug 1`.
// ★ 26.10.2026 [ОРИГИНАЛ]: достижимость класса предмета — по ОБОИМ байтам класса, как `upgradeAnyGradeReachable@0xEC8F70`
//   (разбор у `IsUsageCategoryReachable`). 0 — прежнее поведение порта (только основной класс).
// ★★★ [ОРИГИНАЛ] 31.10.2026 — баг владельца «закрыл инвентарь клавишей, подсказка самоцвета висит посреди экрана».
//   Эталон гасит общую плашку, когда окно, которое её показало, уходит с экрана: закрытие инвентаря `sowelWindowClose@0xCFF760`
//   зовёт `CControl_KillFocus(&g_ItemTooltip)`, а у прочих окон её гасит событие 0xA — скрытый контрол (`CControl_SetVisible`,
//   событие 4) выпадает из хит-теста, и «контрол под курсором» получает уход. У порта подсказки показывают ОПРОСОМ курсора
//   десятки владельцев, и почти все при закрытом окне просто выходят из опроса, не гася свою плашку (`TickInfoPanelSockets`:
//   «окно закрыто → PanelSocketHover = -1; return»). ОБЩЕЕ ЛЕЧЕНИЕ: при каждом показе плашка запоминает окно реестра под
//   курсором (верхнее по z); пока плашка видна, тик проверяет его — окно скрыто (клавиша, крестик, ESC), снято (смена карты,
//   `CloseAll`), спрятано Alt+Z → плашка гаснет. Подсказка без окна под курсором (консоль) не отслеживается.
static ASDA_DEV_CVAR(int32, CVarTipFollowsWindow,
	TEXT("asda.UI.TipFollowsWindow"),
	1,
	TEXT("1 = как в оригинале: подсказка гаснет, когда окно, над которым её показали, закрыто клавишей, крестиком, ESC, "
	     "спрятано Alt+Z или снято сменой карты; 0 = прежний порт (подсказка могла зависнуть на экране)."),
	ECVF_Default);

// ★ [ОРИГИНАЛ] 07.11.2026: подсказка камня при НАВЕДЕНИИ на список «Socketable sowel» 430…435 и на лоток окна 1530 1540…1544
//   (`TickInfoPanelSockets`, `TickSowelSelBox`). 0 — прежний порт: подсказки нет.
ASDA_DEV_CVAR(int32, CVarSowelHoverTip,
	TEXT("asda.Sowel.HoverTip"), 1,
	TEXT("1 = наведение на камень списка 430…435 и лотка окна 1530 показывает подсказку камня (как оригинал); 0 = прежний порт, подсказки нет."),
	ECVF_Default);

// ★★★ ОТКАТ РАЗДЕЛЕНИЯ ПАЙПЛАЙНОВ ТУЛТИПА (заход «тултипы по типам предметов», 2026-08-09).
//   1 — гнать ВСЁ через полный 18-секционный `Item_BuildTooltipText`, как порт делал раньше
//   (у свитка/зелья снова появятся «Эффект ЖК», «Количество живых камней», «Прочность» и т.д.).
ASDA_DEV_CVAR(int32, CVarLegacyOnePipeline,
	TEXT("asda.ItemTip.LegacyOnePipeline"), 0,
	TEXT("1 — один тултип на все типы предметов (как до правки); 0 — эталонные три пайплайна"),
	ECVF_Default);

// ★★★ [ИСПРАВЛЕНИЕ] 01.11.2026 (баг 11 владельца «Reindeer Costume — подсказка как у снаряжения», сессия bug-booster-tooltip).
//   Предмет `categoryFlag == 3` (премиум) живёт на вкладке «Снаряжение/Премиум» (`g_InventoryBag`, правило вкладки —
//   `AsdaInventorySubsystem_Net.cpp`: `itemType <= 9 || categoryFlag == 3`), а для этой вкладки эталон
//   (`invSlotBuildTooltip@0xD06670`, item_inventory.c:37411-37421) зовёт полный `ItemTooltip_Build` БЕЗ проверки
//   `itemType`; полный `Item_BuildTooltipText` (item_inventory.c:62254) печатает и не-снаряжению «Эффект ЖК : 0 ~ 0»,
//   «Профессия», «Уровень возможностей», «Количество живых камней 0/0», «Доступно улучшений 0/0», «Прочность 0».
//   Данные не виноваты: поля 76318…76326 (и всех 8143 затронутых, `Reports/bug_booster_tooltip/affected.csv`)
//   совпадают с EN-эталоном и с серверным item_table (тип 11, подтип 66, категория 3). Дефект оригинала —
//   исправляем: премиум не-снаряжение (`categoryFlag` 3, `itemType` 10/11: бустеры 66/67, наборы 200/201 и прочие), показанное
//   полным путём, получает состав простой подсказки `CItem_BuildSowelTooltipText`, что на вкладке «Расходники» и в надетом
//   слоте (эталонное правило `equipSlotUpdateTooltip`: 10/11 → простой билдер), плюс «Профессия», привязка и премиум-подвал
//   из полного билдера (`BuildTooltipBagNonGear`, r2 по ревизии 01.11). Окно сравнения у неё не открывается (как у
//   простого билдера эталона). 1 — исправление; 0 — как эталон (полный билдер снаряжения).
ASDA_DEV_CVAR(int32, CVarBagNonGearSimple,
	TEXT("asda.ItemTip.BagNonGearSimple"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — премиум бустеры/наборы/прочее не-снаряжение (categoryFlag 3, itemType 10/11) в полной подсказке ")
	TEXT("получают простую: профессия, уровень, вес, привязка, описание — без полей снаряжения; 0 — как эталон: поля снаряжения"),
	ECVF_Default);

// ★★★ [ИСПРАВЛЕНИЕ] 05.11.2026 (баг игрока «похвастался золотом — подсказка как у снаряжения без суммы», сессия gold-boast).
//   Золото — обычная запись 57 б с кодом 20551 в контейнере 2 (сумма = i32 @+9), поэтому ПКМ по ячейке золота открывает
//   меню хвастовства 28000, сервер (`Recv_ItemInfoDisplay@0x55BF80`) находит запись и рассылает 6130 с ней. Наведение на
//   строку окна 28100 у эталона (`ui_core.c:47894-47912`) зовёт ПОЛНЫЙ `ItemTooltip_Build@0xEA5260` для любого кода вне
//   самоцветов, а в полном билдере ветки 20551 НЕТ (она есть только в простом `CItem_BuildSowelTooltipText`,
//   item_inventory.c:60016-60027, и в магазинном `Item_BuildTooltipText_EAC520`, 61181) — отсюда «Эффект ЖК 0~0,
//   Профессия, Тип, Уровень, Живые камни 0/0, Улучшения 0/0, Вес 0» и описание-заглушка вместо суммы. Дефект оригинала.
//   1 — золото, показанное полным путём (окно хвастовства, прочие вызовы `ShowItemTooltip` с источником 1/3), получает
//   эталонную золотую ветку простого билдера: «<N с разделителями> Золото», ширина 116; 0 — как эталон (полный билдер).
ASDA_DEV_CVAR(int32, CVarGoldAmountTip,
	TEXT("asda.ItemTip.GoldAmountOnly"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — подсказка золота (20551) в любом окне показывает только сумму «N Золото» (ветка золота простого ")
	TEXT("билдера эталона); 0 — как эталон: окно хвастовства и другие полные пути печатают поля снаряжения без суммы"),
	ECVF_Default);

static constexpr uint32 kColInfoName  = 0xFF004080;  // инфо-панель: цвет имени ДО грейда

static constexpr int32 kLocPremiumOpt     = 110311;  // заголовок сет-бонуса (477)

// ═══════════════════ жизненный цикл ═══════════════════
void UAsdaItemTooltipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.Show"),
		TEXT("asda.ItemTip.Show <контейнер> <ячейка> — показать тултип предмета ячейки (эталон ItemTooltip_Build)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			if (!Inv || A.Num() < 2) { UE_LOG(LogAsdaItemTip, Warning, TEXT("нужно: asda.ItemTip.Show <контейнер> <ячейка>")); return; }
			const int32 Cont = FCString::Atoi(*A[0]);
			const FAsdaItemInstance* It = Inv->GetCell(Cont, FCString::Atoi(*A[1]));
			if (!It || It->IsEmpty()) { UE_LOG(LogAsdaItemTip, Warning, TEXT("ячейка пуста")); return; }
			// ★ Контейнер = эталонный `slotSource`: он выбирает ПАЙПЛАЙН тултипа, а не оформление.
			ShowItemTooltip(*It, /*bSuppressCompare=*/false, EAsdaTipOwner::Console, Cont);
			UE_LOG(LogAsdaItemTip, Log, TEXT("%s"), *DumpTooltip(*It));
			UE_LOG(LogAsdaItemTip, Display, TEXT("%s"), *DebugTipState());
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.Code"),
		TEXT("asda.ItemTip.Code <itemCode> [заточка] [%заточки] [гнездо0] — тултип по коду (синтетическая запись)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			if (A.Num() < 1) { UE_LOG(LogAsdaItemTip, Warning, TEXT("нужно: asda.ItemTip.Code <itemCode> …")); return; }
			// ★ U17: запись поверх состояния `Clear()` порта (+5/+7 = −1) — та же, что прежняя ручная сборка.
			FAsdaItemInstance It;
			FAsdaItemRecordWriter Rw;
			Rw.PortClearSlots();
			const int32 Code = FCString::Atoi(*A[0]);
			Rw.Code(Code);
			Rw.Amount(1);                                                   // amount = 1
			if (A.Num() > 1) { Rw.GemGrade(FCString::Atoi(*A[1])); }        // уровень заточки (+24)
			if (A.Num() > 2) { Rw.EnchantPercent(FCString::Atoi(*A[2])); }  // процент заточки (+27)
			if (A.Num() > 3) { Rw.Sowel(0, FCString::Atoi(*A[3])); }        // гнездо 0 (+16)
			Rw.Durability(100);                                             // прочность (+13)
			Rw.Byte26(5);                                                   // «улучшений всего» (+26)
			Rw.ApplyTo(It);
			ShowItemTooltip(It, /*bSuppressCompare=*/false, EAsdaTipOwner::Console);
			UE_LOG(LogAsdaItemTip, Log, TEXT("%s"), *DumpTooltip(It));
			UE_LOG(LogAsdaItemTip, Display, TEXT("%s"), *DebugTipState());
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★★★ ПРОВЕРКА РАЗДЕЛЕНИЯ ПАЙПЛАЙНОВ БЕЗ ЖИВОГО ТЕСТА: печатает, КАКОЙ билдер эталона выбран
	//   для кода при каждом из трёх источников, и полный текст, который он собрал.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.Pipelines"),
		TEXT("asda.ItemTip.Pipelines <itemCode> [количество] — какой билдер эталона выберется для источников 1/2/3 и что он соберёт"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			if (A.Num() < 1) { UE_LOG(LogAsdaItemTip, Warning, TEXT("нужно: asda.ItemTip.Pipelines <itemCode> [количество]")); return; }
			const int32 Code = FCString::Atoi(*A[0]);
			const int32 Qty  = (A.Num() > 1) ? FCString::Atoi(*A[1]) : 1;
			UAsdaItemTableSubsystem* IT = Items();
			if (!IT) { return; }
			const FAsdaItemRecord* R = IT->GetItem(Code);
			FAsdaItemInstance It;
			FAsdaItemRecordWriter().PortClearSlots().Code(Code).Amount(Qty).ApplyTo(It);   // ★ U17: поверх `Clear()` порта
			UE_LOG(LogAsdaItemTip, Display,
				TEXT("[тултип] код %d: item_list=%s (itemType=%d subType=%d) · special_itemlist=%s · sowel=%s"),
				Code, R ? TEXT("есть") : TEXT("НЕТ"), R ? R->ItemType : -1, R ? R->ItemSubType : -1,
				IT->IsSpecialItemCode(Code) ? TEXT("есть") : TEXT("нет"),
				IT->IsSowelCode(Code) ? TEXT("да") : TEXT("нет"));
			for (int32 Kind = 1; Kind <= 3; ++Kind)
			{
				FAsdaTipMetrics M;
				FAsdaItemTooltipText T;
				const TCHAR* Who = TEXT("ItemTooltip_Build (18 секций)");
				if (!R && IT->IsSpecialItemCode(Code))
				{
					if (Kind == 2) { Who = TEXT("CItem_BuildSowelTooltipText, ветка 2-й таблицы"); T = BuildTooltipSimple(Code, Qty, -1, M); }
					else           { Who = TEXT("ItemTooltip_Build, ветка 2-й таблицы"); T = BuildTooltipTable2(It, M); }
				}
				else if (R && AsdaGoldUsesAmountTip(Code))   // ★ [ИСПРАВЛЕНИЕ] 05.11: золото — только сумма (как в `ShowItemTooltip`)
				{
					Who = TEXT("CItem_BuildSowelTooltipText, ветка золота (исправление 05.11)");
					T = BuildTooltipSimple(Code, Qty, -1, M);
				}
				else if (R && Kind == 1 && AsdaBagNonGearUsesSimple(R))   // ★ баг 11: как в `ShowItemTooltip`
				{
					Who = TEXT("простой билдер + профессия/привязка (исправление бага 11)");
					T = BuildTooltipBagNonGear(It, M);
				}
				else if (R && (Kind == 2 || (Kind == 3 && ItemTypeUsesSimpleTooltip(R->ItemType))))
				{
					Who = TEXT("CItem_BuildSowelTooltipText (по itemType)");
					T = BuildTooltipSimple(Code, Qty, -1, M);
				}
				else { T = BuildTooltip(It); }
				UE_LOG(LogAsdaItemTip, Display, TEXT("  источник %d → %s%s\n    заголовок: %s\n    тело:\n%s"),
					Kind, Who, T.bValid ? TEXT("") : TEXT("  [НЕ СОБРАН]"), *T.Title, *T.Body);
			}
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ ПРОВЕРКА РАЗМЕРА РАМКИ ПОД КОНТЕНТ (требование владельца 2026-08-23). Показывает ПРОСТУЮ
	//   текстовую подсказку с произвольным заголовком и телом и печатает получившийся размер, —
	//   так проверяются все три случая разом: короткая строка, длинная строка, многострочная.
	//   Аргументы разделяются знаком `|`, «\n» в теле даёт перевод строки.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.Text"),
		TEXT("asda.ItemTip.Text <заголовок>|<тело> — показать простую подсказку и напечатать размер рамки"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			FString Joined = FString::Join(A, TEXT(" "));
			FString Title = Joined, Body;
			Joined.Split(TEXT("|"), &Title, &Body);
			FAsdaTipMetrics M; M.FontPx = 8; M.MaxW = 218.f;
			ShowTextTip(EAsdaTipOwner::Console, Title.TrimStartAndEnd(), Body.TrimStartAndEnd(), M);
			UE_LOG(LogAsdaItemTip, Display, TEXT("%s"), *DebugTipState());
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.Hide"), TEXT("Спрятать тултип предмета (эталон CControl_KillFocus)"),
		FConsoleCommandDelegate::CreateLambda([this]() { HideTooltip(); }), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ Проверка БЕЗ наведения: состояние обеих плашек (создана / видима / владелец / размер / позиция).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.State"),
		TEXT("Состояние тултипа: плашки, владелец, размер, позиция, привязка узлов и моста"),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			UE_LOG(LogAsdaItemTip, Display, TEXT("%s"), *DebugTipState());
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ Проверка укладки: показать тултип по коду и оставить его висеть у курсора.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.Pin"),
		TEXT("asda.ItemTip.Pin <itemCode> — показать короткий тултип по коду и НЕ прятать (проверка позиции у курсора)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			if (A.Num() < 1) { UE_LOG(LogAsdaItemTip, Warning, TEXT("нужно: asda.ItemTip.Pin <itemCode>")); return; }
			ShowItemTooltipByCode(FCString::Atoi(*A[0]), EAsdaTipOwner::Console);
			UE_LOG(LogAsdaItemTip, Display, TEXT("%s"), *DebugTipState());
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.Info"),
		TEXT("asda.ItemTip.Info <контейнер> <ячейка> — заполнить ПРАВУЮ инфо-панель окна инвентаря"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			if (!Inv || A.Num() < 2) { UE_LOG(LogAsdaItemTip, Warning, TEXT("нужно: asda.ItemTip.Info <контейнер> <ячейка>")); return; }
			const int32 Cont = FCString::Atoi(*A[0]);
			const FAsdaItemInstance* It = Inv->GetCell(Cont, FCString::Atoi(*A[1]));
			if (!It || It->IsEmpty()) { ClearInfoPanel(); UE_LOG(LogAsdaItemTip, Warning, TEXT("ячейка пуста — панель очищена")); return; }
			FillInfoPanel(*It, Cont == 3 ? 3 : 1);
			NoteInfoPanelSource(Cont, FCString::Atoi(*A[1]));
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.InfoClear"), TEXT("Очистить правую инфо-панель окна инвентаря"),
		FConsoleCommandDelegate::CreateLambda([this]() { ClearInfoPanel(); }), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★★★ Д5 (КРУГ 5). «Описание от другого предмета» — проверка РЕЗОЛВА по коду на живых данных.
	//   Печатает ВСЁ, во что код разворачивается: строку `item_list`, строку `special_itemlist`
	//   (арифметика `code − FirstCode`), строку `SpecialItem_TooltipList` (эталон
	//   `itemGetTooltipById@0xEC73B0`: `таблица[code − g_SpecialItemTooltipFirstId]`) и запись
	//   `sowell_list`. Так видно, что «(30)» в «Свиток ремонта брони (30)» — часть ИМЕНИ записи,
	//   а не количество, и что сдвига id между таблицами нет.
	// ★★★ Д5 (КРУГ 5, доработка). СЛЕПОК СЕТКИ: ячейка → rect плана → код → имя, плюс ячейка под
	//   курсором по НЕЗАВИСИМОМУ хит-тесту. Проверяет разом попадание, смещение страницы и резолв.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.Grid"),
		TEXT("Слепок сетки инвентаря: видимая ячейка → абсолютная → код/кол-во/имя + ячейка под курсором"),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			UGameInstance* GI = GetGameInstance();
			UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			UAsdaItemTableSubsystem* IT = Items();
			if (!Inv || !IT || !InvenWindow) { UE_LOG(LogAsdaItemTip, Warning, TEXT("окно инвентаря не привязано")); return; }
			const int32 Page = Inv->GetScrollRow();
			const int32 Cont = Inv->GetGridContainer();
			FString S = FString::Printf(
				TEXT("[сетка] вкладка %d, контейнер %d (%d ячеек), страница %d, формула абс = видимая + 12·стр"),   // ★ 29.10.2026 (инвентарь ×2): 12 колонок
				Inv->GetTab(), Cont, Inv->GetContainerSize(Cont), Page);
			for (int32 N = 0; N < UAsdaInventorySubsystem::kGridCells; ++N)   // ★ 29.10.2026 (инвентарь ×2): 60 контролов
			{
				const UWidget* W = InfoPlane(*FString::Printf(TEXT("INVEN_SET%d"), N), 0);
				const int32 Abs = N + UAsdaInventorySubsystem::kGridCols * Page;
				const FAsdaItemInstance* I = Inv->GetCell(Cont, Abs);
				const int32 Code = (I && !I->IsEmpty()) ? I->ItemCode : 0;
				FString Name;
				if (Code > 0)
				{
					Name = IT->GetItemName(Code);
					if (Name.IsEmpty())
					{
						FAsdaSpecialitemTooltiplistRow Tip;
						if (IT->GetSpecialItemTooltip(Code, Tip)) { Name = Tip.Name + TEXT(" [special_itemlist]"); }
					}
				}
				FVector2D P(0, 0), Sz(0, 0);
				if (W) { const FGeometry& G = W->GetCachedGeometry(); P = G.GetAbsolutePosition(); Sz = G.GetLocalSize() * G.Scale; }
				S += FString::Printf(TEXT("\n  видимая %2d → абс %2d | план (%.0f,%.0f %.0fx%.0f)%s | код=%d кол-во=%d «%s»"),
					N, Abs, P.X, P.Y, Sz.X, Sz.Y, (W && IsCursorOverWidget(W)) ? TEXT(" ← КУРСОР") : TEXT(""),
					Code, (I && !I->IsEmpty()) ? I->Amount : 0, *Name);
			}
			const int32 VisN = HitTestGridCellVisual();
			S += FString::Printf(TEXT("\n  под курсором: видимая %d ⇒ абсолютная %d"), VisN, (VisN >= 0) ? (VisN + UAsdaInventorySubsystem::kGridCols * Page) : -1);
			UE_LOG(LogAsdaItemTip, Display, TEXT("%s"), *S);
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.Resolve"),
		TEXT("asda.ItemTip.Resolve <код> — во что код разворачивается в item_list / special_itemlist / SpecialItem_TooltipList / sowell_list"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			UAsdaItemTableSubsystem* IT = Items();
			if (!IT || A.Num() < 1) { UE_LOG(LogAsdaItemTip, Warning, TEXT("нужно: asda.ItemTip.Resolve <код>")); return; }
			const int32 Code = FCString::Atoi(*A[0]);
			const FAsdaItemRecord* R = IT->GetItem(Code);
			FAsdaSpecialItemlistRow Sp;    const bool bSp  = IT->GetSpecialItem(Code, Sp);
			FAsdaSpecialitemTooltiplistRow Tip; const bool bTip = IT->GetSpecialItemTooltip(Code, Tip);
			FAsdaSowellRow Sow;            const bool bSow = IT->GetSowell(Code, Sow);
			UE_LOG(LogAsdaItemTip, Display,
				TEXT("[резолв %d]\n  item_list:                %s\n  special_itemlist:         %s\n  SpecialItem_TooltipList:  %s\n  sowell_list:              %s\n  экип-тип для панели: %s"),
				Code,
				R ? *FString::Printf(TEXT("«%s» тип=%d подтип=%d грейд=%d catFlag=%d"), *IT->GetItemName(Code), R->ItemType, R->ItemSubType, R->ItemGrade, R->CategoryFlag) : TEXT("нет"),
				bSp  ? *FString::Printf(TEXT("Code=%d «%s» подтип=%d уровень=%d"), Sp.Code, *Sp.Name, Sp.SubType, Sp.ItemLevel) : TEXT("нет"),
				bTip ? *FString::Printf(TEXT("Id=%d «%s» описание %d симв."), Tip.Id, *Tip.Name, Tip.Tooltip.Len()) : TEXT("нет"),
				bSow ? *FString::Printf(TEXT("Id=%d бонус тип=%d знач=%d"), Sow.Id, Sow.BonusStatType, Sow.BonusValue) : TEXT("нет"),
				(R && IsEquipInfoPanelType(R->ItemType)) ? TEXT("ДА") : TEXT("НЕТ (панель скрывается)"));
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ Состояние ПЛАНОВ панели: иконка 423 и обе полосы (10008 прочность / 424 состояние).
	//   Нужна, чтобы отличить «данных нет» от «виджет не нашёлся» — планы точного имени не имеют.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.InfoPlanes"),
		TEXT("Состояние планов правой инфо-панели: иконка 423, полоса прочности 10008, полоса состояния 424"),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			auto Rect = [](const UWidget* W) -> FString
			{
				const UCanvasPanelSlot* S = W ? AsdaUi::SlotOf(W) : nullptr;
				if (!S) { return TEXT("нет"); }
				const FMargin O = S->GetOffsets();
				return FString::Printf(TEXT("(%.0f,%.0f %.0fx%.0f) %s"), O.Left, O.Top, O.Right, O.Bottom,
					W->GetVisibility() == ESlateVisibility::Collapsed ? TEXT("скрыт") : TEXT("виден"));
			};
			UE_LOG(LogAsdaItemTip, Display,
				TEXT("[инфо-панель] окно=%s\n  423 иконка: %s%s\n  10008 прочность: ES0 %s | ES1 %s (100%% = %.0f, left %.0f)\n  424 состояние:  ES0 %s | ES1 %s (100%% = %.0f, left %.0f)"),
				InvenWindow ? TEXT("есть") : TEXT("НЕТ"),
				*Rect(IconPlane),
				(IconPlane && IconPlane->GetBrush().GetResourceObject()) ? TEXT(" [текстура есть]") : TEXT(" [текстуры нет]"),
				*Rect(DuriBack), *Rect(DuriFill), DuriFullW, DuriLeft,
				*Rect(CondBack), *Rect(CondFill), CondFullW, CondLeft);
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.TestAll"),
		TEXT("asda.ItemTip.TestAll [сколько] — прогнать сборку тултипа по ВСЕМ типам предметов (по одному образцу на itemType/categoryFlag) и вывести текст"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			UAsdaItemTableSubsystem* IT = Items();
			if (!IT) { UE_LOG(LogAsdaItemTip, Warning, TEXT("таблица предметов не загружена")); return; }
			const int32 Limit = A.Num() > 0 ? FCString::Atoi(*A[0]) : 1;
			TMap<int32, int32> Seen;   // ключ = itemType*100 + categoryFlag
			int32 Shown = 0;
			for (int32 Code = 20551; Code < 20551 + 60000; ++Code)
			{
				const FAsdaItemRecord* R = IT->GetItem(Code);
				if (!R || R->ItemCode != Code) { continue; }
				const int32 Key = R->ItemType * 100 + R->CategoryFlag;
				int32& N = Seen.FindOrAdd(Key);
				if (N >= Limit) { continue; }
				++N;
				FAsdaItemInstance It;
				FAsdaItemRecordWriter().PortClearSlots().Code(Code).Amount(1).Durability(100).Byte26(5).ApplyTo(It);   // ★ U17
				UE_LOG(LogAsdaItemTip, Log, TEXT("=== itemType=%d categoryFlag=%d subType=%d ===\n%s"),
					R->ItemType, R->CategoryFlag, R->ItemSubType, *DumpTooltip(It));
				++Shown;
			}
			UE_LOG(LogAsdaItemTip, Log, TEXT("[тултип] прогнано образцов: %d, различных типов: %d"), Shown, Seen.Num());
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★ ОКНО ВЫБОРА КАМНЯ. Источник — подсистема самоцветов: она ловит «свободных гнёзд нет»
	//   и, ровно как эталон, НЕ шлёт пакет, а просит показать окно (делегат появился в её заходе).
	if (UAsdaSowelSubsystem* Sw = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSowelSubsystem>() : nullptr)
	{
		Sw->OnNeedSowelStonesWindow.AddWeakLambda(this, [this](const TArray<uint8>& Rec57, int32 SlotMode)
		{
			ShowSowelStonesWindow(Rec57, SlotMode);
		});
	}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.SowelWin"),
		TEXT("asda.ItemTip.SowelWin <контейнер> <ячейка> [slotMode] — показать окно выбора камня SOWELL_SELBOX по предмету ячейки"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			if (!Inv || A.Num() < 2) { UE_LOG(LogAsdaItemTip, Warning, TEXT("нужно: asda.ItemTip.SowelWin <контейнер> <ячейка> [slotMode]")); return; }
			const FAsdaItemInstance* It = Inv->GetCell(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]));
			if (!It || It->IsEmpty()) { UE_LOG(LogAsdaItemTip, Warning, TEXT("ячейка пуста")); return; }
			int32 SlotMode = 3;
			if (A.Num() > 2) { SlotMode = FCString::Atoi(*A[2]); }
			else if (const UAsdaItemTableSubsystem* IT = Items())
			{
				// Эталон передаёт `ItemRecord->enchantOrSocket` (не +1).
				if (const FAsdaItemRecord* R = IT->GetItem(It->ItemCode)) { SlotMode = R->EnchantOrSocket; }
			}
			ShowSowelStonesWindow(It->Raw, SlotMode);
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.SowelWinPick"),
		TEXT("asda.ItemTip.SowelWinPick <1..3> — выбрать камень в лотке окна SOWELL_SELBOX"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& A)
		{
			SelectSowelStone(A.Num() ? FCString::Atoi(*A[0]) : 1);
			UE_LOG(LogAsdaItemTip, Display, TEXT("[камни] выбрано гнездо %d"), SowelWinSel);
		}), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	ConsoleCmds.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.ItemTip.SowelWinClose"), TEXT("Закрыть окно выбора камня (эталон ui_HideControlsAndRefresh)"),
		FConsoleCommandDelegate::CreateLambda([this]() { HideSowelStonesWindow(); }), ECVF_Default));
#endif // ASDA_DEV_CONSOLE

	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» для окна выбора камня `SOWELL_SELBOX` (IDNum 1530) —
	//   порт события 21 (`CControl_OnEvent21@0x12DCC30`). Окно стоит в эталонной таблице
	//   `kEscCloseWindows`: признак `CControl+0x228` ему ставит `CMsgBoxMgr_InitControls@0xD6AD20`
	//   (`helper_setByteField552(mgr->pSowelSelBox, 1)`).
	//   ЧТО ИМЕННО ДЕЛАЕТ ЭТАЛОН ПО ESC (доказано, пункт С10-б/1 реестра отложенного):
	//   процедура семейства msgbox — `uiItemUpgradeWnd_OnCommand@0xD79260` (имя в IDA ВРЁТ);
	//   её ветка `case 0x15u` при `a3 == 1530` делает `sub_92CA8D(g_pWindowMgr, 1)` и
	//   `OnSendItemUpgrade(mgr, 1533)` — ТОТ ЖЕ номер кнопки, что у `SOWELL_BUT01` «Отмена»
	//   (loc 2378) ⇒ ESC здесь = ОТМЕНА, а не «ОК». Поэтому подписываем ИМЕННО обработчик
	//   кнопки отмены: он гасит лотки, курсоры и само окно (`ui_HideControlsAndRefresh@0xD6E0E0`)
	//   и пакета НЕ шлёт — снятие виджета мимо него оставило бы подсистеме выбранное гнездо.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(FName(TEXT("SOWELL_SELBOX")),
			FSimpleDelegate::CreateUObject(this, &UAsdaItemTooltipSubsystem::OnSowelSelBoxCancel));
	}

	UE_LOG(LogAsdaItemTip, Log, TEXT("[тултип] подсистема поднята (порт g_ItemTooltip: ITEM_TIPWIN 5000 + ItemCompareTip_Win 5050 + SOWELL_SELBOX 1530)"));
}

void UAsdaItemTooltipSubsystem::Deinitialize()
{
	for (IConsoleObject* C : ConsoleCmds) { if (C) { IConsoleManager::Get().UnregisterConsoleObject(C); } }
	ConsoleCmds.Reset();
	// ★ Д1 (КРУГ 6): снимаем подписку на изменения ячеек (порядок гашения подсистем не задан).
	if (CellWatchHandle.IsValid())
	{
		if (UAsdaInventorySubsystem* Inv = Cast<UAsdaInventorySubsystem>(WatchedInv.Get()))
		{
			Inv->OnCellChanged.Remove(CellWatchHandle);
		}
		CellWatchHandle.Reset();
		WatchedInv = nullptr;
	}
	MainPanel = nullptr; ComparePanel = nullptr; InvenWindow = nullptr;
	bInfoBound = false; bBridgeBound = false; bInfoPanelFilled = false;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaItemTooltipSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Плашка — ОБЩАЯ (закон `ui-shared-tip-ownership`), её виджеты живут в умершем мире.
	// ★ 31.10.2026 [ОРИГИНАЛ] (`asda.UI.TipFollowsWindow`): сброс без смены мира (разрыв связи, смена канала) оставлял
	//   прежнюю плашку на экране — указатель обнулялся, а сам виджет так и висел видимым. Сначала гасим, потом забываем.
	if (CVarTipFollowsWindow.GetValueOnGameThread() != 0)
	{
		if (MainPanel) { MainPanel->Hide(); }
		if (ComparePanel) { ComparePanel->Hide(); }
	}
	MainPanel = nullptr; ComparePanel = nullptr; InvenWindow = nullptr;
	bInfoBound = false; bBridgeBound = false;
	bInfoPanelFilled = false;
	InfoPanelSrcCont = -1; InfoPanelSrcCell = -1; InfoPanelSrcRow = -1;
	bCompareShown = false;
	bPendingPanelCollapse = false;
	SowelWinRec.Reset();
	bSowelWinTwo = false;
	bSowelWinBound = false;
	SowelMouse.Reset();
	(void)Reason;
}

void UAsdaItemTooltipSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.ItemTooltip");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★ [ПОРТ] 29.10.2026: экземпляр комплекта просмотрщика — только построитель текста, окон и мыши у него нет.
	if (AsdaItemTipStandalone::IsStandaloneTip(this)) { return; }
	if (!bBridgeBound) { TryBindInventoryBridge(); }
	// ★★★ 21.10.2026 — ИНФО-ПАНЕЛЬ С ЗАГЛУШКАМИ ДИЗАЙНЕРА (жалоба игроков: «Booster — …», «Option 1…7»,
	//   «Extra Sowel 1…4: content», «Blah-blah-blah 200 chars…»). Корень — НЕ разметка и НЕ наполнение, а ПРИВЯЗКА:
	//   панель привязывалась к окну инвентаря ОДИН раз (`bBridgeBound`). Окно же пересоздаётся
	//   (`UAsdaInventorySubsystem::EnsureWidget`: прежний WBP «не живой» — другой мир после портала/канала/перезахода
	//   или вынут из вьюпорта), а `GetWindowWidget()` до пересоздания отдаёт СТАРЫЙ экземпляр — сброс сессии
	//   (`MapChanged`/`LeftWorld`) обнулял нашу привязку, следующий же тик привязывал её снова к старому окну, и
	//   `FillInfoPanel`/`ClearInfoPanel` писали в невидимое окно. Новое окно показывало голые надписи из ассета
	//   (они же — заглушки дизайнера в .JUI оригинала; оригинал их перекрывает в `InitControls` и
	//   `ItemTooltip_PopulateInfoPanel`, у нас — `BindInfoPanel` → `ClearInfoPanel` и `FillInfoPanel`).
	//   Лечение — то же правило, что у соседей по окну (`UAsdaSowelSubsystem::EnsureWidgets`,
	//   `UAsdaFishingToolSubsystem::EnsureWidgets`): окно сменилось — перепривязаться (сдвиги раскладки, стили,
	//   планы и очистка — всё внутри `BindInfoPanel`, «новое окно» он распознаёт сам).
	else
	{
		RebindInfoPanelIfWindowChanged();
	}
	// Окно выбора камня: наведение/клик по иконкам лотка (у них нет UButton, см. BindSowelSelBox).
	TickSowelSelBox();
	// ★ Д1 (КРУГ 5) + Д1 (КРУГ 6): досворачивание правой панели — и инвентарь, и смена вкладки
	//   раскрывают её ПОСЛЕ того, как мост уже сходил в панель.
	EnforceInfoPanelSelectionRule();
	// ★ Д4 (КРУГ 5): подсказка камня при наведении на гнёзда 441…444 правой панели.
	TickInfoPanelSockets();
	// ★ 31.10.2026 [ОРИГИНАЛ] (`asda.UI.TipFollowsWindow`, разбор у ключа): окно, над которым показали плашку, ушло с экрана —
	//   гасим плашку того владельца, что её показал (как `CControl_KillFocus(&g_ItemTooltip)` при закрытии окна в эталоне).
	if (MainPanel && MainPanel->IsShown() && !MainPanel->GetSourceWindow().IsNone() && CVarTipFollowsWindow.GetValueOnGameThread() != 0)
	{
		const FName Src = MainPanel->GetSourceWindow();
		const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		const UUserWidget* SrcW = WM ? WM->FindWindow(Src) : nullptr;
		const TCHAR* Why = !WM ? TEXT("нет реестра окон")
			: !SrcW ? TEXT("окно снято с реестра (смена карты / CloseAll)")
			: !WM->IsWindowLive(SrcW) ? TEXT("окно не во вьюпорте своего мира")
			: !WM->IsWindowVisible(Src) ? TEXT("окно скрыто (клавиша / крестик / ESC)")
			: AsdaUi::IsWidgetInHiddenUi(SrcW) ? TEXT("интерфейс спрятан Alt+Z")
			: nullptr;
		if (Why)
		{
			const EAsdaTipOwner Was = MainPanel->GetOwner();
			UE_LOG(LogAsdaItemTip, Log, TEXT("[тултип] окно-источник «%s» ушло с экрана (%s) — плашка владельца %d погашена"),
				*Src.ToString(), Why, (int32)Was);
			HideTooltipFor(Was);
		}
	}
	// Эталонное событие 9 «курсор движется над контролом» — доводка позиции тултипа за курсором
	//   (`Tooltip_LayoutNearCursor`). Держим только пока тултип показан.
	if (MainPanel && MainPanel->IsShown())
	{
		MainPanel->LayoutAtCursor(bCompareShown && ComparePanel ? (float)ComparePanel->GetLastSize().X : 0.f);
		PlaceComparePanel();
	}
}
