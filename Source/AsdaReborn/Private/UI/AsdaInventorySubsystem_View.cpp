// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_View.cpp — ★ 23.10.2026 часть UAsdaInventorySubsystem, вынесенная из AsdaInventorySubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaInventorySubsystem.plan.json).
//   Рендер ячеек и шторки отката, ввод (перетаскивание, указатель), главное меню, мост хотбара, Tick.
//   #include — те же, что у AsdaInventorySubsystem.cpp, в том же порядке; общее — AsdaInventorySubsystemInternal.h.
#include "UI/AsdaInventorySubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEquipCellText.h"   // ★ 08.11.2026: общая точка надписи надетой ячейки
#include "UI/AsdaSoulStoneSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaItemCountStyle.h"   // ★ 26.10.2026: обводка числа стопки — общая точка (ключ asda.UI.ItemCountOutline)
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaScrollBarNomal.h"   // D3: общая полоса прокрутки скина `Nomal_Scr`            // ★ D1/D2/D5: единые UI-хелперы (дедуп 2026-08-08)
#include "UI/AsdaShowOffSubsystem.h"   // ПКМ по ячейке → меню «похвастаться» (28000)
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT_MSGBOX 1500 — AsdaLoc::kBtnOk)
#include "UI/AsdaMiniStateSubsystem.h"   // общий тултип HUD (эталон держит один g_ItemTooltip)
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // свиток возврата склада (5451 подтип 112) → окно 5500
#include "UI/AsdaSelfStoreSubsystem.h"   // билет премиум-лавки (5451 подтип 65) → 6300 (волна Л1 торговли)
#include "UI/AsdaHudSubsystem.h"       // ★ анонс редкой добычи из КОРОБКИ (NotifyItemInfo → 6616), наше добавление
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"      // ★ кнопка 454 — окно питомца 43000 (волна 2)
#include "UI/AsdaItemUpgradeSubsystem.h"  // ★ кнопка 449 — окно улучшения снаряжения 20000 (25.09.2026)
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ кнопка 452 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaDisassemblySubsystem.h"    // ★ кнопка 453 — окно разборки снаряжения 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaCraftSubsystem.h"         // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейты рюкзака и бустера — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "Resources/AsdaSoundSubsystem.h"  // ★ кнопка 449 — звук id 5 ветки 66561 (волна 5 заточки)
#include "UI/AsdaFishingToolSubsystem.h"   // ★ окно «Взять рыболовную снасть» 15900 (точки открытия/закрытия)
#include "UI/AsdaFishBookSubsystem.h"      // ★ книга рыбака 74000: использование книги-предмета (подтип 210) → диалог 79
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
#include "UI/AsdaPetHatcherySubsystem.h"   // ★ окно ИНКУБАТОРА 42000 (двойной клик по предмету subType 107)
// ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "Pet/AsdaPetSubsystem.h"          // ★ волна 6: корм (subType 106 → 6108) и расширитель ячеек (104 → 6121)
#include "UI/AsdaSkillUseSubsystem.h"      // ★ волна 2: выбранная цель для поля @0 запроса 6096
#include "UI/AsdaPackagePanelSubsystem.h"  // ★ панель состава пакета 26500 (двойной клик открывает ЕЁ)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ окно барабана гачи 10050 (двойной клик открывает ЕГО)
#include "UI/AsdaPremiumGachaSubsystem.h"  // ★ гейт `sub_922D5D@0xB6F370` — гейты №15/№15b применения лопат
#include "UI/AsdaPremiumShopSubsystem.h"   // ★ 29.09.2026: `g_pPartyTargetObj` = элитный магазин (гейты №15/№15b лопат)
#include "Items/AsdaItemTableSubsystem.h"
#include "Engine/DataTable.h"              // сырые поля шаблона предмета (DataHex) для гейтов панели
// ★ 09.10.2026 (п. 5): состояние лопаты 40/43 своего персонажа (оружие за спину)   ★ R3: комментарий снятого включения "World/AsdaPlayerCharacter.h" (файл им не пользовался) — сохранён
#include "Resources/AsdaTextureSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"   // DoubleClickTime — интервал двойного клика Slate
#include "HAL/IConsoleManager.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaScrollCallSubsystem.h"     // ★ волна 2-Г: свитки перемещения/призыва (окно 1552)
#include "AsdaInventorySubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaInventorySubsystem*.cpp (LogAsdaInven, чистые хелперы)
#include "UI/AsdaCloseGear.h"   // ★ 08.11.2026: шестерёнка у крестика — общая точка (asda.UI.CloseGearHover)

// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 16, баг 3): гейт «персонаж занят» премиум-канала — определение и разбор в
//   `AsdaInventorySubsystem_Use.cpp` (одна точка для двойного щелчка и панели быстрого доступа). true — отказ.
bool AsdaPremiumUseBusyBlocked(UGameInstance* GI, int32 ItemCode);


// ★ Д7 — «+N%» на вкладке «Снаряжение». ★ КРУГ 2: СМЕЩЕНИЯ ИСПРАВЛЕНЫ.
//   Прошлый заход взял «+0x20 флаг / +0x23 значение» из словесного описания глобала. Считаем адреса:
//   `g_InventoryBag` = 0x2901950 (`RE/ida/renames/item_inv.tsv`), `g_aInvSlots` = 0x2901968
//   (`RE/ida/bng_012.tsv`), `g_InvBag_ItemSubId` = 0x290196B (`RE/ida/globals_out_misc3_1.tsv`),
//   `g_aInvSlot2Value` = 0x2901959 (`RE/ida/globals_out_misc2.tsv`).
//   Разности: 0x18 = **+24** (гейт), 0x1B = **+27** (i16 процент), 0x09 = **+9** (количество —
//   сходится с известной раскладкой записи, что и подтверждает базу). Старые +0x20/+0x23 попадали
//   внутрь массива пар заточки (+29) — оттуда и бралось мусорное «+-1%» из отчёта юзера.
// ★ [ИСПРАВЛЕНИЕ] 07.11.2026 (баг владельца, пачка 12). Нажатие (0xB) по НЕПУСТОМУ надетому слоту переключает вкладку сумки:
//   `uiControlMessageProc@0xC6C030` → `uiEquipSlotSelect@0xC6A860` (ui_core.c:33922): слоты 5, 6, 7, 10 → вкладка 0, остальные → вкладка 1.
//   Переключение стоит на выборе, а не на ответе 5001, поэтому работает для щелчка, двойного щелчка, захвата и при полной сумке.
//   Порт этого не делал: окно оставалось на текущей вкладке. 1 = как эталон; 0 = прежнее поведение порта (вкладка не меняется).
static ASDA_DEV_CVAR(int32, CVarEquipSelectTab,
	TEXT("asda.Inven.EquipSelectTab"), 1,
	TEXT("1 = нажатие по надетому слоту переключает вкладку сумки, как uiEquipSlotSelect (5/6/7/10 → 0, иначе → 1); 0 = вкладка не меняется."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] 07.11.2026. Снятие перетаскиванием: эталон даёт захват надетого слота (`uiControlMessageProc` 5/6/7,
//   `chatChannelDlg_ApplyChannelType@0xC6BAD0` — миснейм начала захвата) и на бросок на ячейки сумки 10010…10039 или лоток 411
//   шлёт снятие (`itemUiHandleControlEvent@0xC6BCA0` → `itemAction_SendUseEquipPacket`). Порт запрещал захват, ссылаясь на процедуру
//   ДРУГОГО окна. 1 = как эталон; 0 = прежнее поведение (надетое не тащится, снятие только двойным щелчком).
static ASDA_DEV_CVAR(int32, CVarEquipDragOff,
	TEXT("asda.Inven.EquipDragOff"), 1,
	TEXT("1 = надетую вещь можно перетащить на сетку сумки, это снятие, как в эталоне; 0 = захвата нет."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarNoBagEnchantText,
	TEXT("asda.Inven.NoBagEnchantText"),
	0,
	TEXT("Вкладка «Снаряжение»: 0 = как эталон, под иконкой «+N%» заточки (гейт запись+24, значение i16 запись+27); "
	     "1 = не показывать ничего."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarEnchantTextGradeColor,
	TEXT("asda.Inven.EnchantTextGradeColor"),
	1,
	TEXT("Цвет «+N%» в ячейке: 1 = по уровню заточки, `itemGetGradeColor` (Д9, просьба юзера); "
	     "0 = как эталон — фиксированный 0xFFFFF200 (SetField923(-3584))."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (баг 10): текст надетой ячейки по `itemUI_DrawEquipSlotIcon@0xC6B1B0` — см. `BuildEquipCellText`.
static ASDA_DEV_CVAR(int32, CVarEquipCellText,
	TEXT("asda.Inven.EquipCellText"),
	1,
	TEXT("Надетые ячейки: 1 = как эталон — «+N%» заточки на любом слоте и количество стрел в слоте 10; "
	     "0 = прежний порт — текста нет."),
	ECVF_Default);

// ★ Д10 (дефект оригинала, п.4 круга 2). У непакуемых предметов (оружие/броня/бустеры) поле
//   «количество» (+9) в записи равно НУЛЮ — сервер его просто не заполняет. Эталон на вкладке
//   «Снаряжение» такой ноль не печатает (`if (g_aInvSlot2Value[...] <= 0) SetText("")`,
//   item_inventory.c:35989), а на вкладке «Расходники» печатает `sprintf("%d", record+9)` БЕЗ
//   проверки (item_inventory.c:36167) — и рисует «0». Показывать «0» там, где предмет заведомо
//   один, — дефект: чиним по закону `fix-original-defects`, правило берём у соседней ветки эталона.
static ASDA_DEV_CVAR(int32, CVarLegacyZeroCount,
	TEXT("asda.Inven.LegacyZeroCount"),
	0,
	TEXT("Счётчик «Расходников»: 0 = количество ≤ 0 не печатается (Д10); "
	     "1 = как эталон — печатается «0»."),
	ECVF_Default);

// ★ Д11 (закон `fix-original-defects`). `hotbarAssignInventoryItem` при отказе просто возвращает 0:
//   игрок тащит предмет в панель быстрого доступа, отпускает — и НИЧЕГО не происходит, без единого
//   слова почему. Показываем короткое сообщение (нотис 2004 — тот же, которым эталон отвечает на
//   недопустимое действие с ячейкой в соседней ветке `inventoryWin_OnUiCommand`). Откат — ключ ниже.
static ASDA_DEV_CVAR(int32, CVarLegacyHotbarSilentReject,
	TEXT("asda.Inven.LegacyHotbarSilentReject"),
	0,
	TEXT("Отказ при броске предмета в панель быстрого доступа: 0 = коротко сообщить игроку (Д11); "
	     "1 = как эталон — молча ничего не делать."),
	ECVF_Default);

// ⚠ Спорное условие эталона в счётчике вкладки «Расходники» — см. комментарий в `BuildCellText`.
static ASDA_DEV_CVAR(int32, CVarEtalonTradableCountGate,
	TEXT("asda.Inven.EtalonTradableCountGate"),
	0,
	TEXT("Счётчик на вкладке «Расходники»: 0 = показываем всем (кроме золота/самоцветов/subType 32); "
	     "1 = как в теле эталона — ещё и `!tradableFlag` прячет число (поле +0x98 стоит лишь "
	     "у 1177 записей из 58878, значение поля бинарём не подтверждено)."),
	ECVF_Default);

// ══════════════════════════════ РЕНДЕР ═════════════════════════════════════════════════════════

void UAsdaInventorySubsystem::ApplyStackTextStyle(UTextBlock* Tb) const
{
	if (!Tb) { return; }
	// ★ Кегль в .JUI не хранится ВООБЩЕ (он глобальный для всего UI) — это наш параметр.
	//   Цвет/выравнивание/обводку импортёр уже кладёт из блока EXS и свойств скина
	//   (законы `ui-text-color-exs` / `ui-button-hover-click-rule`), поэтому здесь их НЕ трогаем.
	FSlateFontInfo F = Tb->GetFont();
	F.Size = AsdaItemCount::FontSize((float)StackCountFontPx);   // ★ 08.11.2026 (пачка 18, баг 6): +10% по ключу `asda.UI.ItemCountFontScale`
	// ★ [ОРИГИНАЛ] 26.10.2026 (решение владельца): чёрная обводка 1 px числа стопки (эталон: поле 926 = 0x800000,
	//   item_inventory.c:35253) — общая точка
	//   `AsdaItemCount::ApplyOutline` (ключ `asda.UI.ItemCountOutline`, 0 = прежние `StackOutlinePx`/`StackOutlineColor`).
	AsdaItemCount::ApplyOutline(F, StackOutlinePx, StackOutlineColor);
	Tb->SetFont(F);
	// Обводка Slate сдвигает заливку цифры на её толщину вниз (ревизия wave_1026 Н3) — возвращаем на место эталона.
	AsdaItemCount::CompensateGlyph(Tb, F.OutlineSettings.OutlineSize);
}

void UAsdaInventorySubsystem::ClearIcon(UImage* Img) const
{
	if (!Img) { return; }
	FSlateBrush B = Img->GetBrush();
	B.SetResourceObject(nullptr);
	B.DrawAs = ESlateBrushDrawType::NoDrawType;
	Img->SetBrush(B);
}

// ★ Волна 1 (14.09.2026, В1): тело переехало в общую точку `AsdaUi::ApplyItemIcon` — метод оставлен
//   переходником для 8 внешних вызовов. Поведение то же: заворот по модулю, мужской вариант, квадрат DrawSize.
bool UAsdaInventorySubsystem::ApplyIconToImage(UImage* Img, int32 ItemCode, float DrawSize) const
{
	return AsdaUi::ApplyItemIcon(GetGameInstance(), Img, ItemCode, FVector2D(DrawSize, DrawSize),
		AsdaUi::IconSexSelf, /*bWrap*/ true); // ★ 08.11.2026: атлас пола своего персонажа, как эталон `g_ItemImgOptionVariant`
}

// ★★★ ТЕКСТ ПОД ИКОНКОЙ. У ДВУХ ВКЛАДОК ПРАВИЛА РАЗНЫЕ — это прямо в эталоне:
//
//   Вкладка «Расходники» = контейнер 2 (`CInventoryUI_RefreshStoragePage@0xD01730`):
//     • `itemGetCategoryById(code) != 1` (надеваемое / самоцвет / вторая таблица) ⇒ ячейка
//       вообще не наполняется: ни иконки, ни текста;
//     • `itemSubType == 32` (камни души) ⇒ текста нет;
//     • `!tradableFlag` ⇒ текста нет;
//     • ★★★ `code == 20551` (ЗОЛОТО) ⇒ `goto LABEL_25` → `SetText("")` — В ЯЧЕЙКЕ ЗОЛОТА
//       ЧИСЛА НЕТ ВООБЩЕ (сумма живёт только в поле 407). Именно об этом п.4 теста;
//     • иначе — `sprintf("%d", record+9)`, т.е. количество БЕЗ разделителя разрядов
//       (и «1» тоже печатается — так в оригинале).
//
//   Вкладка «Снаряжение» = контейнер 1 (`CInventoryUI_RefreshBagIconsPage@0xD00990`) — ДВЕ ВЕТКИ,
//   и в круге 2 теста выяснилось, что порт путал их между собой:
//     • ОБЫЧНЫЙ предмет (`item_list`, оружие/броня) — item_inventory.c:35952-35968:
//         количество НЕ печатается ВООБЩЕ; печатается «+N%» ЗАТОЧКИ, если гейт-байт стоит,
//         иначе текст пустой (`SetField923(-1)` + `SetText("")`);
//     • запись ВТОРОЙ таблицы (`special_itemlist` — premium/бустеры) — item_inventory.c:35986-36008:
//         сперва КОЛИЧЕСТВО `record+9`, и ТОЛЬКО если оно > 0 (иначе `SetText("")` — вот откуда
//         должен исчезнуть «0» у непакуемых), а поверх — «+N%», если гейт-байт стоит.
//   ★★★ СМЕЩЕНИЯ (главная причина дефектов 2 и 3 круга 2): гейт = байт записи **+24**
//       (`g_aInvSlots` 0x2901968 − `g_InventoryBag` 0x2901950 = 0x18), значение = i16 записи **+27**
//       (`g_InvBag_ItemSubId` 0x290196B − база = 0x1B). Контроль базы: `g_aInvSlot2Value` 0x2901959
//       даёт +9 = «количество», что совпадает с известной раскладкой записи.
//       Прошлый заход читал +0x20/+0x23 — это внутренности массива пар заточки (+29), отсюда
//       и «+-1%» у премиум-предметов, и молчание у экипировки.
FString UAsdaInventorySubsystem::BuildCellText(int32 ContainerId, const FAsdaItemInstance& I, uint32& OutColorArgb) const
{
	OutColorArgb = 0;
	if (I.IsEmpty()) { return FString(); }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	if (ContainerId == 1)
	{
		FString S;
		// ── ветка ВТОРОЙ ТАБЛИЦЫ: количество (эталон печатает его только при > 0) ──
		if (IT && IT->IsSpecialItemCode(I.ItemCode) && I.Amount > 0)
		{
			// Цвет счётчика эталон ставит в -1 (белый), но у нас он и так берётся из данных JUI
			//   контрола (EXS TextColor) — оставляем его, как на вкладке «Расходники», чтобы обе
			//   вкладки выглядели одинаково; отдельным цветом красим только «+N%».
			S = FString::FromInt(I.Amount);
		}
		// ── «+N%» заточки: эталон печатает его последним и перекрывает количество ──
		//    (обе ветки, item_inventory.c:35957 и 36002 — гейт один и тот же).
		if (CVarNoBagEnchantText.GetValueOnGameThread() == 0 && I.GemGrade != 0)
		{
			S = FString::Printf(TEXT("+%d%%"), I.EnchantPercent);
			// Д9: эталон — фиксированный 0xFFFFF200 (`SetField923(-3584)`); по просьбе юзера
			//     по умолчанию красим по уровню заточки той же таблицей, что и заголовок тултипа.
			OutColorArgb = CVarEnchantTextGradeColor.GetValueOnGameThread() != 0
				? UAsdaItemTooltipSubsystem::EnhanceColorArgb((int32)I.GemGrade)
				: 0xFFFFF200;
		}
		return S;
	}

	// Контейнер 2 (и склад 5 — та же страница-«сетка»).
	if (I.ItemCode == kGoldItemCode) { return FString(); }               // ★ ЗОЛОТО — БЕЗ ЧИСЛА
	// Д10: непакуемые предметы приходят с количеством 0 — «0» под иконкой не рисуем.
	if (I.Amount <= 0 && CVarLegacyZeroCount.GetValueOnGameThread() == 0) { return FString(); }
	if (!IT) { return I.Amount > 1 ? FString::FromInt(I.Amount) : FString(); }
	if (IT->GetItemCategory(I.ItemCode) != 1) { return FString(); }      // самоцвет / надеваемое / Table2
	const FAsdaItemRecord* Rec = IT->GetItem(I.ItemCode);
	if (!Rec) { return FString(); }
	if (Rec->ItemSubType == 32) { return FString(); }
	// ⚠ ЧЕСТНО: у эталона в этом же условии стоит ещё `!ItemRecord->tradableFlag → текста нет`.
	//   Поле +0x98, которым порт считает `tradableFlag`, выставлено лишь у 1177 записей из 58878
	//   (ItemType 10 — 31 шт., ItemType 11 — 1146 шт.), т.е. по нему пропали бы счётчики у ~90 %
	//   расходников. Значение поля НЕ подтверждено бинарём, поэтому по умолчанию условие ВЫКЛЮЧЕНО;
	//   включается ключом для сверки с оригиналом.
	if (CVarEtalonTradableCountGate.GetValueOnGameThread() != 0 && Rec->TradableFlag == 0)
	{
		return FString();
	}
	return FString::FromInt(I.Amount);
}

void UAsdaInventorySubsystem::UpdateCellVisual(int32 Cell)
{
	if (!GridUi.IsValidIndex(Cell)) { return; }
	FAsdaInvCellUi& U = GridUi[Cell];
	// ★★★ ФОРМУЛА ЭТАЛОНА: абсолютный индекс = (idnum − 10010) + 6 · страница.
	const int32 Abs = Cell + kGridCols * ScrollRow[CurrentTab];
	const int32 Cont = GetGridContainer();
	const FAsdaItemInstance* I = GetCell(Cont, Abs);

	if (!I || I->IsEmpty())
	{
		// Индекс за пределами контейнера ⇒ ячейка рисуется ПУСТОЙ (эталон, вкладка 1).
		ClearIcon(U.Icon);
		SetVis(U.Icon, true);
		if (U.StackText) { U.StackText->SetText(FText::GetEmpty()); }
		return;
	}
	// ★ 06.11.2026 [ОРИГИНАЛ] Активированный камень души рисуется значком `bind_soulstone`: `CInventoryUI_RefreshStoragePage@0xD01730` (item_inventory.c:36145).
	bool bBoundStone = false;
	if (AsdaSoulStoneOn() && Cont == 2 && I->Raw.Num() > 50 && I->Raw[50] == 3)
	{
		const UAsdaItemTableSubsystem* StoneIT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const FAsdaItemRecord* StoneRec = StoneIT ? StoneIT->GetItem(I->ItemCode) : nullptr;
		bBoundStone = StoneRec && StoneRec->ItemSubType == 32;
	}
	if (bBoundStone && AsdaUi::ApplyFlatJuiIcon(U.Icon, TEXT("bind_soulstone"), U.IconSize.X > 0.f ? U.IconSize.X : 32.f))
	{
		SetVis(U.Icon, true);
	}
	else if (ApplyIconToImage(U.Icon, I->ItemCode, U.IconSize.X > 0.f ? U.IconSize.X : 32.f))
	{
		SetVis(U.Icon, true);
	}
	else
	{
		ClearIcon(U.Icon);
	}
	// Счётчик — ТЕКСТ самого контрола-иконки (отдельного контрола в эталоне нет).
	if (U.StackText)
	{
		uint32 Argb = 0;
		const FString S = BuildCellText(Cont, *I, Argb);
		U.StackText->SetText(S.IsEmpty() ? FText::GetEmpty() : FText::FromString(S));
		// Закон `ui-srgb-gamma-rule`: цвет из данных — в sRGB, UMG ждёт linear.
		U.StackText->SetColorAndOpacity(FSlateColor(Argb != 0
			? AsdaUi::SrgbHexToLinear(FString::Printf(TEXT("%08X"), Argb))
			: U.StackTextColor));
	}
}

// ★★★ [ИСПРАВЛЕНИЕ] 31.10.2026 (баг 10 владельца): ТЕКСТ НАДЕТОЙ ЯЧЕЙКИ. До правки порт гасил его всегда
//   («надетое не стекуется»), хотя эталон `itemUI_DrawEquipSlotIcon@0xC6B1B0` (item_inventory.c:22815-22882;
//   тот же порядок в `uiEquipPanelRefreshSlots@0xC6AB30`, ui_core.c:33993) печатает в контрол слота:
//     • сперва `SetText("")`;
//     • слот 10 (стрелы/боеприпасы) — `sprintf("%d", запись+9)`, цвет −1 (белый, `SetField923(-1)`);
//     • любой слот с гейт-байтом записи +24 — «+N%» из i16 записи +27, цвет −3584 = 0xFFFFF200;
//       печатается последним и перекрывает количество (тот же контрол).
//   Смещения те же, что у вкладки «Снаряжение» (`BuildCellText`, контейнер 1), — запись надетого
//   (`gap_50A[57·слот + 292]`) того же 57-байтового формата. Цвет «+N%» — тем же ключом
//   `asda.Inven.EnchantTextGradeColor`, что и в сумке, чтобы ячейки сверху и снизу выглядели одинаково.
//   Возврат пустой строки при `asda.Inven.EquipCellText 0` — прежнее поведение порта.
// ★ 08.11.2026 (пачка 17, баг 6): тело вынесено в общую точку `AsdaEquipCellText::Build` — её же зовёт окно 4200 (надетое
//   чужого и своего персонажа), второй копии правила нет.
FString AsdaEquipCellText::Build(int32 Slot, const FAsdaItemInstance& I, uint32& OutColorArgb)
{
	constexpr int32 kEquipArrowSlot = 10;   // слот стрел/боеприпасов (`a3 == 10` эталона)
	OutColorArgb = 0;
	if (I.IsEmpty() || CVarEquipCellText.GetValueOnGameThread() == 0) { return FString(); }
	FString S;
	if (Slot == kEquipArrowSlot)
	{
		S = FString::FromInt(I.Amount);   // эталон печатает и «0» — без гейта по количеству
		OutColorArgb = 0xFFFFFFFF;        // SetField923(-1)
	}
	// [ПОРТ] (ревизия 01.11): в слоте 10 при количестве > 0 число стоит ПОВЕРХ «+N%» — так его пишет счётчик выстрелов
	//   `uiStatLabelUpdateCount@0xC6BF80` (ui_core.c:34101: код > 0 и количество > 0 ⇒ `sprintf("%d")`, без гейта +24). Эталонная
	//   перерисовка слота показала бы «+N%» до первого выстрела; порт рисует надпись одной точкой, поэтому берёт
	//   последнее, что видит игрок в бою, — число стрел.
	if (I.GemGrade != 0 && !(Slot == kEquipArrowSlot && I.Amount > 0))
	{
		S = FString::Printf(TEXT("+%d%%"), I.EnchantPercent);
		OutColorArgb = CVarEnchantTextGradeColor.GetValueOnGameThread() != 0
			? UAsdaItemTooltipSubsystem::EnhanceColorArgb((int32)I.GemGrade)
			: 0xFFFFF200;
	}
	return S;
}

void UAsdaInventorySubsystem::UpdateEquipVisual(int32 Slot)
{
	if (!EquipUi.IsValidIndex(Slot)) { return; }
	FAsdaInvCellUi& U = EquipUi[Slot];
	const FAsdaItemInstance* I = GetCell(3, Slot);
	if (!I || I->IsEmpty())
	{
		ClearIcon(U.Icon);   // под иконкой остаётся силуэт-подсказка слота (Equip_LMd/RMd/…)
		if (U.StackText) { U.StackText->SetText(FText::GetEmpty()); }
		return;
	}
	// Эталон (item_inventory.c:22850-22855): нет записи `item_list` или пуст путь иконки ⇒ ветка «пусто» — иконка-заглушка И текст "".
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const bool bIcon = ApplyIconToImage(U.Icon, I->ItemCode, U.IconSize.X > 0.f ? U.IconSize.X : 32.f);
	if (!bIcon)
	{
		ClearIcon(U.Icon);
	}
	else
	{
		SetVis(U.Icon, true);
	}
	// Условие эталона — ДАННЫЕ (запись есть и путь иконки не пуст), а не то, загрузилась ли текстура в порту.
	const bool bDrawn = IT && IT->GetItem(I->ItemCode) != nullptr && IT->GetItemIcon(I->ItemCode, /*bMale*/ true).IsValid();
	if (U.StackText)
	{
		uint32 Argb = 0;
		const FString S = bDrawn ? AsdaEquipCellText::Build(Slot, *I, Argb) : FString();
		U.StackText->SetText(S.IsEmpty() ? FText::GetEmpty() : FText::FromString(S));
		// Закон `ui-srgb-gamma-rule`: цвет из данных — в sRGB, UMG ждёт linear.
		U.StackText->SetColorAndOpacity(FSlateColor(Argb != 0
			? AsdaUi::SrgbHexToLinear(FString::Printf(TEXT("%08X"), Argb))
			: U.StackTextColor));
	}
}

// ★★★ п.10: посадить анимированный курсор выбора РОВНО на ячейку.
//   Курсор и ячейка живут в РАЗНЫХ канвасах (`INVEN_ITEMTRAY2_grp` против `INVEN_Icon_Base_grp`,
//   смещённого на 5,5) — поэтому переводим позицию ячейки в координаты родителя курсора.
//   Считаем ПО ЖИВЫМ слотам (окно можно таскать: `UAsdaWindowSubsystem` сдвигает канвас-слоты),
//   кэш из `SetupControls` остаётся запасным вариантом.
void UAsdaInventorySubsystem::PlaceSelOnCell(UWidget* Sel, const UWidget* Icon,
                                             const FVector2D& CachedIconRoot, const FVector2D& CachedSelParentRoot)
{
	UCanvasPanelSlot* SS = Sel ? AsdaUi::SlotOf(Sel) : nullptr;
	if (!SS) { return; }
	// ★ Дедуп №2, В3-S10 (остаток U19): ячейка есть — общая точка `AsdaUi::PlaceFrameOver` (та же формула по живым слотам:
	//   «база родителя = позиция курсора в корне − его смещение», курсор = ячейка − база). Перезапуск кадров и итоговую видимость
	//   по-прежнему задают вызывающие (`RestartAnim` + `SetVis` сразу после), поэтому здесь `bRestartAnim = false`.
	if (Icon)
	{
		AsdaUi::PlaceFrameOver(Sel, Icon, /*bRestartAnim*/ false);
		return;
	}
	// Ячейки нет (узел не найден) — запасной путь по кэшу `SetupControls`, как было.
	FMargin O = SS->GetOffsets();
	O.Left = CachedIconRoot.X - CachedSelParentRoot.X;
	O.Top  = CachedIconRoot.Y - CachedSelParentRoot.Y;
	SS->SetOffsets(O);
}

// ★★★ Д6: подписи вкладок держатся на прямоугольниках СВОИХ кнопок-опций. Пересчитываем по живым
//   слотам — окно перетаскивается, а второй TextBlock создан нами и в мете .JUI его нет.
void UAsdaInventorySubsystem::SyncTabCaptions()
{
	auto Follow = [](UTextBlock* Tb, const UWidget* Btn)
	{
		UCanvasPanelSlot* TS = Tb ? AsdaUi::SlotOf(Tb) : nullptr;
		const UCanvasPanelSlot* BS = Btn ? AsdaUi::SlotOf(Btn) : nullptr;
		if (!TS || !BS) { return; }
		const FMargin B = BS->GetOffsets();
		FMargin O = TS->GetOffsets();
		if (FMath::IsNearlyEqual(O.Left, B.Left) && FMath::IsNearlyEqual(O.Right, B.Right)) { return; }
		O.Left = B.Left; O.Right = B.Right;
		TS->SetOffsets(O);
	};
	Follow(TabText,  TabOpt0);
	Follow(TabText1, TabOpt1);
}

void UAsdaInventorySubsystem::RefreshGrid()
{
	if (!bControlsReady) { return; }
	for (int32 C = 0; C < GridUi.Num(); ++C) { UpdateCellVisual(C); }
	// Курсор выбранной ячейки (409): ставится на ячейку, если она видна на текущей странице.
	if (GridSel)
	{
		const int32 First = kGridCols * ScrollRow[CurrentTab];
		const int32 Local = SelCell - First;
		const bool bVisible = (SelCell >= 0) && (Local >= 0) && (Local < kGridCells);
		if (bVisible && GridUi.IsValidIndex(Local))
		{
			// ★ п.10: позиция считается в системе координат РОДИТЕЛЯ КУРСОРА, а не ячейки.
			PlaceSelOnCell(GridSel, GridUi[Local].Icon, GridUi[Local].IconRootPos, GridSelParentRoot);
			GridSel->RestartAnim();
		}
		SetVis(GridSel, bVisible);
	}
	// Полоса — часть той же перерисовки страницы: эталон двигает бегунок ровно там, где меняет
	//   страницу (`uiCtrl_SetChildScrollbarPos` рядом с `CInventoryUI_RefreshStoragePage`).
	LayoutScrollBar();
}

void UAsdaInventorySubsystem::RefreshEquip()
{
	if (!bControlsReady) { return; }
	for (int32 S = 0; S < EquipUi.Num(); ++S) { UpdateEquipVisual(S); }
	if (EquipSel)
	{
		const bool bVisible = EquipUi.IsValidIndex(SelEquipSlot);
		if (bVisible)
		{
			PlaceSelOnCell(EquipSel, EquipUi[SelEquipSlot].Icon,
				EquipUi[SelEquipSlot].IconRootPos, EquipSelParentRoot);
			EquipSel->RestartAnim();
		}
		SetVis(EquipSel, bVisible);
	}
}

// ═══════════ ★★★ ШТОРКА ОТКАТА НА ЯЧЕЙКАХ — ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА ═══════════════
//
// РЕШЕНИЕ ЮЗЕРА 2026-08-14: «шторка отката рисуется только на панели быстрого доступа — сделать,
//   чтобы ВЕЗДЕ». В ОРИГИНАЛЕ этого окна шторка НЕ КАСАЕТСЯ, и это доказано двумя независимыми
//   методами (`Reports/item_cooldown_premium.md` §2):
//     1) все читатели реестра откатов (`hotbar+56`) перечислены поимённо, рисуют только три из них
//        (`hotbarPushItemCooldown@0xE48FD0`, `hotbar_FindSlotAndRefreshByItemRef@0xE49430`,
//        `shopItemListPopulateColorVariants@0xE49610`) и все три пишут в `g_pQuickSlotBar`;
//     2) все 12 вызовов `hotbarRefreshSlotsByItemRef@0xDF0840` первым аргументом несут
//        `g_pQuickSlotBar`; контролов со скином `CoolTime_Effect` в `INVEN_WIN` нет вовсе
//        (`COOLTIME00..27` — счётчик длительности БАФФА в окне состояния, скин `CoolTime_Mini`).
//   Отключение отступления: `asda.Cooldown.ShowEverywhere 0` — тогда шторки в окне нет, как в 2008-м.
//
// ИСТОЧНИК ДАННЫХ — ТОЛЬКО реестр панели (`UAsdaHotbarSubsystem::QueryItemCooldown`, закон
//   `port-shared-points`): своего счётчика инвентарь не заводит, поэтому шторки в сумке, в панели и
//   в книге навыков зажигаются и гаснут ОДНОВРЕМЕННО — это буквально одна и та же запись отката.
//
// ВИЗУАЛ — не выдуман (закон `no-invented-ui`): план создаётся в рантайме по геометрии иконки
//   ячейки, а атлас/кадры/UV/Diffuse копируются с плана панели (`MakeCurtainOverCell`).
//   Перегенерацию WBP не запускали — тот же приём, что у подписей ячеек панели состава пакета.
//
// ПОЧЕМУ ПОКАДРОВО, А НЕ В `RefreshGrid`. `RefreshGrid` дёргается по СОБЫТИЮ (пакет, скролл, клик),
//   а откат — величина непрерывная: без покадрового прохода шторка «замерзала» бы между событиями.
// ★ 25.10.2026: единые часы реестра откатов (определены в AsdaHotbarSubsystem.cpp рядом с ключом asda.Cooldown.RealClock).
double AsdaCooldownNowS(const UObject* Ctx);

void UAsdaInventorySubsystem::TickCooldownCurtains()
{
	if (!bControlsReady || !InvenWidget) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb) { return; }

	const bool bEnabled = UAsdaHotbarSubsystem::IsCooldownShownEverywhere();
	const double Now = AsdaCooldownNowS(GI);   // ★ 25.10.2026: часы реестра откатов (было мировое время — отставало на рывках и обнулялось порталом)
	const int32 GridCont = GetGridContainer();

	// Один шаг для любой ячейки: код предмета → состояние отката → шторка.
	//   `Container` = контейнер ячейки (1 сумка снаряжения / 2 расходники / 3 надетое).
	auto Step = [&](FAsdaInvCellUi& U, int32 Container, const FAsdaItemInstance* I, const TCHAR* NameHint)
	{
		if (!U.Icon) { return; }
		const int32 Code = (I && !I->IsEmpty()) ? I->ItemCode : 0;
		FAsdaHotbarCooldown CD;
		if (bEnabled && Code > 0) { CD = Hb->QueryItemCooldown(Code, Container); }
		// Ничего не показываем и плана ещё нет — не создаём его зря (у 11 027 из 11 081 расходников
		//   отката нет вообще, поэтому у большинства ячеек план так и не появится).
		if (CD.State == 0 && !U.Cool.Plane) { return; }
		if (!U.Cool.Plane)
		{
			U.Cool.Plane = Hb->MakeCurtainOverCell(InvenWidget, U.Icon, NameHint);
			if (!U.Cool.Plane) { return; }   // панель ещё не построена ⇒ образца нет, попробуем позже
		}
		Hb->DriveCurtain(U.Cool, Code, CD, Now);
	};

	for (int32 C = 0; C < GridUi.Num(); ++C)
	{
		// Абсолютный индекс ячейки — та же формула, что в `UpdateCellVisual`: (idnum − 10010) + 6·страница.
		const int32 Abs = C + kGridCols * ScrollRow[FMath::Clamp(CurrentTab, 0, 1)];
		Step(GridUi[C], GridCont, GetCell(GridCont, Abs), TEXT("INVEN_CoolTime"));
	}
	for (int32 E = 0; E < EquipUi.Num(); ++E)
	{
		// ★ Слоты НАДЕТОГО тоже участвуют: транспорт и спутник надеваются именно сюда (живой лог
		//   14.08: код 123 «контейнер 1, ячейка 4» → откат 30 000 мс), а раньше игрок не видел отката
		//   вообще — именно с этой жалобы и начался разбор.
		Step(EquipUi[E], 3, GetCell(3, E), TEXT("EQUIP_CoolTime"));
	}
}

// ★★★ ПОРТ `String_FormatThousands@0xEC00D0` (stl_runtime.c:30039): цифры справа налево, после
//   каждых трёх — ЗАПЯТАЯ (`sub_9272EA(v7, ",", …)`), пока остались разряды. Именно поэтому
//   в .JUI у контрола 407 стоит образец «999,999,99~».
//   ★ D7 (аудит `code_dedup_audit_2.md`): сам алгоритм переехал в `AsdaUi::FormatThousands` —
//   тултип предмета держал его вторую копию (`TipFormatThousands`), потому что этот метод был
//   приватным. Метод оставлен тонким форвардом: он публичный API инвентаря.
FString UAsdaInventorySubsystem::FormatThousands(int32 Value)
{
	return AsdaUi::FormatThousands(Value);
}

// ★★★ ПОРТ `uiSetHonorPointsDisplay@0xB75D40`: сумма денег — ОТДЕЛЬНОЕ поле клиента
//   (`g_pGameClient + 770234`), которое пишут все пакеты, несущие её. Текст поля 407 —
//   `String_FormatThousands` от этого поля.
void UAsdaInventorySubsystem::SetGoldDisplay(int32 NewGold)
{
	// ★ 30.09.2026 (волна О4 торговли): эталон `uiSetHonorPointsDisplay@0xB75D40` (ui_core.c:8381-8389) — строка, поле суммы И
	//   `invGetFreeSlotByMarker(сумма)`: ячейка 20551 контейнера 2 получает ту же сумму. Без этого список «личных вещей» обмена
	//   (`invFilterConsumablesToList` читает ЯЧЕЙКУ) показывал бы старый запас золота после любого изменения денег.
	SetGoldCellAmount(NewGold);
	if (NewGold < 0) { return; }
	GoldDisplay = NewGold;
	RefreshMoney();
}

// ★ Порт `invGetFreeSlotByMarker@0xD1E910` — объявление и эталон в `.h`.
void UAsdaInventorySubsystem::SetGoldCellAmount(int32 V)
{
	// `for (i < 60) if (ячейка.код == 20551) { if (a1 > 0) ячейка[+9] = a1; return; }` — только первая ячейка денег, без перерисовки.
	for (FAsdaItemInstance& I : Spend)
	{
		if (I.ItemCode != kGoldItemCode) { continue; }
		if (V > 0) { I.SetAmount(V); }
		return;
	}
}

void UAsdaInventorySubsystem::RefreshMoney()
{
	if (!MoneyText) { return; }
	// Эталон: `CControl_SetText(m_pMoneyTxt, honorPoints_resetDisplayIfZero(...))`, где строка —
	//   результат `String_FormatThousands` (запятая каждые три разряда).
	MoneyText->SetText(FText::FromString(FormatThousands(GetGold())));
}

void UAsdaInventorySubsystem::RefreshTabVisual()
{
	// Подписи вкладок — строки 13247 «Расходники» и 13248 «Снаряжение» из таблицы текста.
	//   ★ Переключатель `INVEN_CHOICEBUT` (402) — RadioButton с ДВУМЯ планами и ОДНИМ TextBlock:
	//   импортёр слил подписи в один контрол, поэтому показываем подпись АКТИВНОЙ вкладки.
	// ★ Д6: подписи ставятся ПОШТУЧНО, как `CListBox_SetItemString(402, 0/1, …)` в эталоне.
	if (TabText)  { TabText->SetText(FText::FromString(LocText(13247))); }
	if (TabText1) { TabText1->SetText(FText::FromString(LocText(13248))); }
	// ★ СОСТОЯНИЕ «какая вкладка активна» — ОБЩАЯ ТОЧКА (закон `port-shared-points`): активной
	//   ЧЁРНЫЙ, соседней её базовый цвет ИЗ АССЕТА (решение владельца 2026-08-27).
	const TArray<UTextBlock*> TabCaps = { TabText.Get(), TabText1.Get() };
	AsdaUi::ApplyTabCaptionState(TabCaps, CurrentTab, TabIdleColors);
	// Кадр «нажато» у активной вкладки: план 0 — левая вкладка, план 1 — правая.
	if (TabPlane0) { TabPlane0->SetRenderOpacity(CurrentTab == 0 ? 1.0f : 0.65f); }
	if (TabPlane1) { TabPlane1->SetRenderOpacity(CurrentTab == 1 ? 1.0f : 0.65f); }
}

// ══════════════════════════════ ВВОД ═══════════════════════════════════════════════════════════

// ★ D1 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::IsCursorOverWidget` — одна точка
//   хит-теста на все подсистемы. Метод оставлен тонким форвардом (зовётся из десятков мест файла).
bool UAsdaInventorySubsystem::IsCursorOver(const UWidget* W) const
{
	return AsdaUi::IsCursorOverWidget(W);
}

// ★ Курсор над ЛЮБОЙ ячейкой сетки — для эталонного броска камня из гнезда в сумку (5022).
bool UAsdaInventorySubsystem::IsCursorOverGridCell() const
{
	if (!bWindowOpen || !bControlsReady) { return false; }
	for (const FAsdaInvCellUi& U : GridUi)
	{
		if (IsCursorOver(U.Icon)) { return true; }
	}
	return false;
}

bool UAsdaInventorySubsystem::HitTest(int32& OutContainer, int32& OutCell) const
{
	const int32 First = kGridCols * ScrollRow[CurrentTab];
	for (int32 C = 0; C < GridUi.Num(); ++C)
	{
		if (IsCursorOver(GridUi[C].Icon))
		{
			OutContainer = GetGridContainer();
			OutCell = C + First;
			return true;
		}
	}
	for (int32 S = 0; S < EquipUi.Num(); ++S)
	{
		if (IsCursorOver(EquipUi[S].Icon)) { OutContainer = 3; OutCell = S; return true; }
	}
	OutContainer = 0; OutCell = -1;
	return false;
}

// ★ U1 (В2-A дедупа №2): копия распознавателя с прежним порогом окна — для самотеста (описание — у объявления).
FAsdaDblClick UAsdaInventorySubsystem::DebugDblClickProbe() const
{
	FAsdaDblClick C = CellDbl;
	C.LegacySec = (double)GetDefault<UInputSettings>()->DoubleClickTime;   // то же выражение, что в `HandleCellClickRelease`
	return C;
}

// ★★★ ОТПУСКАНИЕ ЛКМ НАД ТОЙ ЖЕ ЯЧЕЙКОЙ = КЛИК. Тело вынесено из `TickPointer`, потому что после
//   ввода порога сюда ведут ДВА пути: «тащили и вернулись в исходную ячейку» и «порог не пройден,
//   значит и не тащили вовсе». Логика у них одна, копий быть не должно (закон `port-shared-points`).
//
//   Применение — только по ВТОРОМУ клику подряд по ТОЙ ЖЕ ячейке в пределах системного интервала
//   двойного клика. Эталонное сообщение 0xD — это WM_LBUTTONDBLCLK (см. комментарий у полей
//   `LastClick*` в заголовке): `CControl_OnKeyDown@0x12DC240` требует состояние ЛКМ == 2, а имя
//   функции врёт. Одиночный клик только ВЫДЕЛЯЕТ ячейку (событие 0xB), и это уже сделано в ветке
//   нажатия.
void UAsdaInventorySubsystem::HandleCellClickRelease(int32 SrcCont, int32 SrcCell)
{
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U1): общий `FAsdaDblClick` — порог Windows `GetDoubleClickTime`, реальное время.
	//   Было: `UInputSettings::DoubleClickTime` (0,2 с, `Config/DefaultInput.ini`) — это порог ВВОДА ИГРОКА движка, а не
	//   Windows: двойной щелчок медленнее 0,2 с не срабатывал вовсе. Прежний порог — `LegacySec` (ключ `asda.UI.DblClickLegacy 1`).
	CellDbl.LegacySec = (double)GetDefault<UInputSettings>()->DoubleClickTime;
	const bool bSecond = CellDbl.Click(SrcCont, SrcCell);   // серия закрывается внутри (третий щелчок — снова первый)
	if (bSecond)
	{
		UseFromBagWindow(SrcCont, SrcCell);   // ★ волна В4 ремесла: ветки свитков `CInventory_OnSlotUse`, затем общий путь
	}
	else
	{
		UE_LOG(LogAsdaInven, Verbose,
			TEXT("[инвентарь] одиночный клик по ячейке %d:%d — только выделение ")
			TEXT("(применение эталона висит на сообщении 0xD = двойной ЛЕВЫЙ клик)"),
			SrcCont, SrcCell);
	}
}

// ★ Те же два гейта, что стояли в начале `BeginDrag` — вынесены, чтобы «взвод» в `TickPointer`
//   срабатывал РОВНО на тех ячейках, на которых прежний код поднимал `bDragActive`.
bool UAsdaInventorySubsystem::CanBeginDrag(int32 ContainerId, int32 Cell) const
{
	const FAsdaItemInstance* I = GetCell(ContainerId, Cell);
	if (!I || I->IsEmpty()) { return false; }
	// ★ 07.11.2026: захват надетого — `uiControlMessageProc@0xC6C030` события 5/6/7 (разбор у `CVarEquipDragOff`).
	if (ContainerId == 3) { return CVarEquipDragOff.GetValueOnGameThread() != 0; }
	return true;
}

void UAsdaInventorySubsystem::BeginDrag(int32 ContainerId, int32 Cell)
{
	if (!CanBeginDrag(ContainerId, Cell))
	{
		if (ContainerId == 3)
		{
			UE_LOG(LogAsdaInven, Verbose, TEXT("[инвентарь] надетое не перетаскивается (эталон); снятие — двойной клик"));
		}
		return;
	}
	bDragActive = true;
	DragContainer = ContainerId;
	DragCell = Cell;
	HideCellTooltip();

	// ★ 07.11.2026: источник захвата — ячейка сетки либо надетый слот (контейнер 3 в сетке не лежит).
	const int32 Local = Cell - kGridCols * ScrollRow[CurrentTab];
	FAsdaInvCellUi* SrcUi = (ContainerId == 3) ? (EquipUi.IsValidIndex(Cell) ? &EquipUi[Cell] : nullptr)
	                                           : (GridUi.IsValidIndex(Local) ? &GridUi[Local] : nullptr);
	if (SrcUi)
	{
		FAsdaInvCellUi& U = *SrcUi;
		if (U.StackText)
		{
			DragSavedStackText = U.StackText->GetText().ToString();
			U.StackText->SetText(FText::GetEmpty());
		}
		if (U.Icon) { U.Icon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, DragSourceOpacity)); }

		// ★ ПРИЗРАК ПЕРЕТАСКИВАНИЯ — тот же подход, что у хотбара (там он уже есть и принят юзером):
		//   в оригинале летящую иконку рисует движок (D3D-слой drag'n'drop, FillMode 5), своего
		//   виджета нет. Второй механизм не изобретаем.
		//   ★ Аудит №3 (B4): и сам механизм теперь ОДИН на порт (`FAsdaDragGhost`) — до правки это
		//   был второй из трёх экземпляров, побайтово совпадавший с книгой навыков.
		if (U.Icon && InvenWidget && InvenWidget->WidgetTree)
		{
			if (UCanvasPanel* Root = Cast<UCanvasPanel>(InvenWidget->WidgetTree->RootWidget))
			{
				DragGhost.Begin(InvenWidget, Root, U.Icon->GetBrush(),
					U.IconSize.X > 0.f ? U.IconSize : FVector2D(32.f, 32.f),
					DragGhostOpacity, DragGhostCursorOffset, TEXT("INVEN_DragGhost"),
					GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
			}
		}
	}
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] захват ячейки %d:%d"), ContainerId, Cell);
}

// ★ Аудит №3 (B4): тело слежения переехало в `FAsdaDragGhost::Tick` — здесь и в книге навыков оно
//   совпадало ПОБАЙТОВО (внутри — тот же D2-перевод «экран → канвас окна»).
void UAsdaInventorySubsystem::UpdateDragGhost()
{
	if (!bDragActive || !InvenWidget) { return; }
	DragGhost.Tick(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
}

void UAsdaInventorySubsystem::CancelDragVisual()
{
	const int32 Local = DragCell - kGridCols * ScrollRow[CurrentTab];
	FAsdaInvCellUi* SrcUi = (DragContainer == 3) ? (EquipUi.IsValidIndex(DragCell) ? &EquipUi[DragCell] : nullptr)
	                                             : (GridUi.IsValidIndex(Local) ? &GridUi[Local] : nullptr);
	if (SrcUi)
	{
		FAsdaInvCellUi& U = *SrcUi;
		if (U.Icon) { U.Icon->SetColorAndOpacity(FLinearColor::White); }
		if (U.StackText && !DragSavedStackText.IsEmpty()) { U.StackText->SetText(FText::FromString(DragSavedStackText)); }
	}
	DragGhost.Cancel();          // ★ Аудит №3 (B4): гашение призрака — общая точка
	DragSavedStackText.Reset();
	bDragActive = false;
	DragCell = -1;
	DragContainer = 0;
}

// ★ Сырое i32 шаблона `item_list` по смещению в 264-байтовой записи. Строка DataTable адресуется
//   ПОРЯДКОВЫМ НОМЕРОМ записи, а он же лежит в `FAsdaItemRecord::ImgIndex` (загрузчик эталона
//   `CItemManager_LoadItemList_BIN` перезаписывает поле 0xF0 индексом — см. шапку таблицы предметов).
int32 UAsdaInventorySubsystem::ItemRawI32(const UAsdaItemTableSubsystem* IT, int32 ItemCode, int32 Offset)
{
	if (!IT) { return -1; }
	const FAsdaItemRecord* Rec = IT->GetItem(ItemCode);
	if (!Rec) { return -1; }
	const UDataTable* Dt = IT->GetDataTable(TEXT("item_list"));
	if (!Dt) { return -1; }
	const FAsdaItemRow* Row = Dt->FindRow<FAsdaItemRow>(FName(*FString::FromInt(Rec->ImgIndex)), TEXT("ItemRawI32"), false);
	if (!Row) { return -1; }
	// ★ D4 (аудит `code_dedup_audit_2.md`): свой разбор hex убран — общая точка `AsdaBytes`.
	//   ⚠ Отличие от `HexI32At`: там «поля нет» ⇒ 0, а этот метод по контракту отдаёт −1
	//   («значение не прочитано»), поэтому обрыв ловим сами, по первому же байту хвоста.
	const FString& Hex = Row->DataHex;
	uint32 V = 0;
	for (int32 B = 3; B >= 0; --B)
	{
		const int32 Byte = AsdaBytes::HexByteAt(Hex, Offset + B);
		if (Byte < 0) { return -1; }
		V = (V << 8) | (uint32)Byte;
	}
	return (int32)V;
}

// ★★★ ПОЛОЖИТЬ ПРЕДМЕТ ЯЧЕЙКИ В СЛОТ ПАНЕЛИ — порт `hotbarAssignInventoryItem@0xD11840`
//   (ui_core.c:47231) 1:1, включая ВСЕ его гейты. Эталон различает ДВЕ ветки по контейнеру:
//     a2 == 1 (контейнер 1, `g_InventoryBag`)   — запись Table2 (special_itemlist) ИЛИ item_list;
//     a2 == 2 (контейнер 2, `g_InventoryArray2`) — только item_list, и здесь стоит гейт золота
//                                                  и самоцветов (`v5 in [SowelLo..Hi] || v5==20551 || !v5`).
//   Записываемая десятка: Type=1, SubType=a2, Index=индекс ячейки, Count=+9 записи, Id=itemCode.
bool UAsdaInventorySubsystem::AssignToHotbarSlot(int32 ContainerId, int32 CellIndex, int32 HotbarAbsSlot)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb) { return false; }
	if (HotbarAbsSlot < 0 || HotbarAbsSlot >= UAsdaHotbarSubsystem::kSlotCount) { return false; }

	const int32 SubType = ContainerToHotbarSubType(ContainerId);
	if (SubType <= 0)
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь→панель] контейнер %d в панель быстрого доступа не адресуется "
			"(эталон знает только 1 и 2; надетое 3 захватить нельзя, склад 5 в записи слота не кодируется)"), ContainerId);
		return false;
	}
	const FAsdaItemInstance* I = GetCell(ContainerId, CellIndex);
	if (!I || I->IsEmpty()) { return false; }
	const int32 Code = I->ItemCode;

	// Колонка ОРУЖИЯ (12) есть только у 5 страниц основного ряда; в T-ряду её нет.
	const bool bWeaponCol = (HotbarAbsSlot < UAsdaHotbarSubsystem::kColsPerPage * UAsdaHotbarSubsystem::kMainPages)
		&& (HotbarAbsSlot % UAsdaHotbarSubsystem::kColsPerPage == UAsdaHotbarSubsystem::kWeaponCol);

	UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	const bool bSilent = (CVarLegacyHotbarSilentReject.GetValueOnAnyThread() != 0);
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): Why — ТОЛЬКО причина для журнала; игроку уходит строка LocId (нет строки → «[#id]»).
	auto RejectWhy = [&](int32 LocId, const TCHAR* Why) -> bool
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь→панель] отказ: %s (нотис %d, предмет %d, %d:%d → слот %d)"),
			Why, LocId, Code, ContainerId, CellIndex, HotbarAbsSlot);
		if (!bSilent && LocId > 0) { Notice(LocId); }
		return false;
	};

	if (SubType == 2)
	{
		// Ветка a2 == 2 эталона. Гейт стоит ДО чтения шаблона.
		if (Code == kGoldItemCode) { return RejectWhy(2004, TEXT("Деньги в панель быстрого доступа не кладутся.")); }
		if (IT && IT->IsSowelCode(Code)) { return RejectWhy(2004, TEXT("Самоцвет в панель быстрого доступа не кладётся.")); }
		// ★ Диапазон idnum этой ветки (`0x11D33 … < 0x11D3F`) НЕ включает колонку оружия 73023 —
		//   из контейнера 2 в слот оружия эталон не кладёт вовсе (и никакого сообщения не даёт).
		if (bWeaponCol) { return RejectWhy(0, TEXT("в слот оружия кладут только из контейнера 1")); }
	}

	if (IT && IT->IsSpecialItemCode(Code))
	{
		// Ветка «код попал в Table2» (special_itemlist): в слот оружия — никогда; флаг +100 == 2 — отказ.
		if (bWeaponCol) { return RejectWhy(0, TEXT("предмет второй таблицы в слот оружия не кладётся")); }
		FAsdaSpecialItemlistRow Sp;
		if (!IT->GetSpecialItem(Code, Sp)) { return RejectWhy(0, TEXT("записи special_itemlist нет")); }
		// `Table2Record->flag_100[0] == 2` — имена полей этой записи ДЕСЯТИЧНЫЕ (ср. соседние Flag101
		//   = +0x65, Flag124 = +0x7C), значит смещение 100 = 0x64. Отдельной колонки у него нет —
		//   читаем сырой байт из `DataHex` (запись 1:1 от смещения 0).
		// ★ Аудит №3 (A5): своя лямбда `Nib` убрана — общая точка `AsdaBytes::HexByteAt`. Контракт
		//   СОВПАДАЕТ побайтово: и короткая строка, и не-hex символ дают −1, а вызывающий сравнивает
		//   результат только с 2, то есть −1 (как и раньше) означает «отказа нет».
		const int32 F100 = AsdaBytes::HexByteAt(Sp.DataHex, 100);
		if (F100 == 2) { return RejectWhy(2004, TEXT("этот предмет в панель быстрого доступа не кладётся.")); }
	}
	else
	{
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
		if (!Rec) { return RejectWhy(0, TEXT("записи item_list нет — эталон здесь возвращает 0")); }
		// `!a3 && col == 12 && ItemRecord->itemType != 9` — правило слота оружия.
		if (bWeaponCol && Rec->ItemType != 9) { return RejectWhy(0, TEXT("в слот оружия кладут только оружие (itemType 9)")); }
		// `*(_DWORD *)&ItemRecord->gap_B2[2] == 1` — поле +0xB4 шаблона, «в панель быстрого доступа
		//   не регистрируется». Единственные два чтения этого поля во всём срезе — как раз здесь.
		if (ItemRawI32(IT, Code, 0xB4) == 1)
		{
			return RejectWhy(2004, TEXT("этот предмет в панель быстрого доступа не кладётся."));
		}
	}

	// ★ Count — кэш (+9 записи контейнера). Хотбар всё равно перечитывает его из инвентаря при
	//   каждом ребилде, поэтому эталонное условие `if (ItemRecord->tradableFlag)` ветки a2==2
	//   на видимый результат не влияет; поле +0x98 в порту признано недостоверным
	//   (`asda.Inven.EtalonTradableCountGate`), кладём количество всегда.
	if (!Hb->SetSlotContent(HotbarAbsSlot, /*Type*/ 1, (uint8)SubType, CellIndex, I->Amount, (int32)(uint16)Code))
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь→панель] хотбар отклонил запись слота %d"), HotbarAbsSlot);
		return false;
	}
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь→панель] предмет %d (контейнер %d, ячейка %d, ×%d) → слот %d "
		"(подтип записи %d, страница %d уходит пакетом 5067)"),
		Code, ContainerId, CellIndex, I->Amount, HotbarAbsSlot, SubType,
		HotbarAbsSlot / UAsdaHotbarSubsystem::kColsPerPage);
	return true;
}

// Бросок на ХОТБАР (эталон `inventoryWin_OnUiCommand`, диапазоны 73011…73023 и 73161…73173 →
//   `hotbarAssignInventoryItem@0xD11840`). Слот хотбара хранит «контейнер + индекс ячейки».
//   ⚠⚠ Источник приходит ПАРАМЕТРАМИ: к моменту вызова `EndDrag` уже погасил визуал драга, а
//   `CancelDragVisual` обнуляет `DragContainer`/`DragCell` (см. объявление в заголовке).
bool UAsdaInventorySubsystem::TryDropToHotbar(int32 SrcContainer, int32 SrcCell)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb) { return false; }
	// Хит-тест слота панели = эталонная проверка «idnum контрола под курсором попал в диапазон».
	const int32 Abs = Hb->HitTestSlotUnderCursor();
	if (Abs < 0) { return false; }
	// ★ Перекрытие окон. У эталона под курсором ВСЕГДА РОВНО ОДИН контрол — тот, что сверху; если
	//   окно инвентаря накрыло слот панели, событие уйдёт инвентарю, и в диапазон 73011…73023 оно
	//   не попадёт (бросок просто пропадёт). Наш хит-тест геометрический и «видит» слот сквозь
	//   окно — восстанавливаем приоритет явно. Ячейку/корзину `EndDrag` уже проверил выше.
	//   ⚠ ПОРЯДОК ВАЖЕН: «призрак» перетаскивания — это UImage В КАНВАСЕ ОКНА, и он висит ровно под
	//   курсором. `EndDrag` гасит его (`CancelDragVisual`) ДО этого вызова, иначе хит-тест окна был
	//   бы истинным ВСЕГДА и бросок в панель не проходил бы никогда.
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		if (WM->IsCursorOverWindow(kInvenWindowId))
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь→панель] слот %d панели закрыт окном инвентаря — "
				"эталон в этом месте бросок не обрабатывает"), Abs);
			return true;
		}
	}
	// ★ Цель НАШЛАСЬ ⇒ бросок обработан в любом случае: отказ гейта эталон тоже считает
	//   «событие моё» (дальше идёт только `CInventoryWindow_RefreshPendingSlot`).
	AssignToHotbarSlot(SrcContainer, SrcCell, Abs);
	return true;
}

void UAsdaInventorySubsystem::EndDrag()
{
	const int32 SrcCont = DragContainer;
	const int32 SrcCell = DragCell;
	if (SrcCell < 0) { CancelDragVisual(); return; }

	int32 TgtCont = 0, TgtCell = -1;
	const bool bOverCell = HitTest(TgtCont, TgtCell);
	const bool bOverTrash = IsCursorOver(BtnTrash);
	// ★ 07.11.2026: лоток 411 `INVEN_ITEMTRAY1` — цель броска надетого наравне с ячейками.
	const bool bOverTray = (SrcCont == 3) && IsCursorOver(FindNode(TEXT("INVEN_ITEMTRAY1_grp"), TEXT("INVEN_ITEMTRAY1_grp")));
	CancelDragVisual();
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] `asda.Inven.EquipDragOff`: `itemUiHandleControlEvent@0xC6BCA0` — бросок надетого на ячейки 10010…10039
	//   или лоток 411 = снять (та же точка, что двойной щелчок); над слотами экипировки, корзиной и прочим — ничего.
	if (SrcCont == 3)
	{
		if ((bOverCell && TgtCont != 3) || bOverTray) { UseFromBagWindow(3, SrcCell); }
		RefreshEquip();
		return;
	}
	// ★ [ОРИГИНАЛ] 26.10.2026: гейт сообщения 7 (ui_core.c:47780-47800) — до разбора цели, для ЛЮБОЙ цели броска.
	if (DragGateRefuses(TEXT("бросок предмета"))) { RefreshGrid(); return; }

	if (bOverTrash)
	{
		// Корзина 405: эталон при `m_nSelKind` 0/1 зовёт `sowelHandleSlotSelect(this, 1)` — уничтожение.
		DestroyItem(SrcCont, SrcCell);
		return;
	}
	if (bOverCell)
	{
		if (TgtCont == 3)
		{
			// Бросок на манекен = НАДЕТЬ. ★ [ОРИГИНАЛ] 26.10.2026: слот под курсором — цель броска (эталон
			//   `netItem_SendUseRequest(this, Field91)`, item_inventory.c:41771: кольцо на 605/606 → ровно в этот слот).
			EquipItemToSlot(SrcCont, SrcCell, TgtCell);
			return;
		}
		if (TgtCont == SrcCont && TgtCell != SrcCell)
		{
			MoveItem(SrcCont, SrcCell, TgtCont, TgtCell);
			return;
		}
		if (TgtCont != SrcCont)
		{
			// Между вкладками сервер перенос ЗАПРЕЩАЕТ (в `CharItemMove` ветки 1↔2 нет вовсе).
			Notice(2014);
			return;
		}
		return;   // бросили в ту же ячейку — ничего
	}
	// Курсор вне сетки: сначала пробуем хотбар, затем — «наружу».
	//   ⚠ Источник передаётся ЯВНО: `CancelDragVisual` выше уже обнулил `DragContainer`/`DragCell`.
	if (TryDropToHotbar(SrcCont, SrcCell)) { return; }
	// ★★★ [ОРИГИНАЛ] 26.10.2026: прежний комментарий «эталон ничего не делает (только подсказка)» был ЛОЖНЫМ «нет» — смотрели
	//   ветки `Field91`, а ветку `a3 == nullptr` (под курсором нет ни одного контрола) — нет. Там вопрос уничтожения 2059.
	//   Над ЛЮБЫМ окном (чужим или своим вне ячеек/корзины) эталон `Field91` не узнаёт ⇒ `CInventoryWindow_RefreshPendingSlot`
	//   — ничего. Призрак уже погашен `CancelDragVisual` выше, иначе хит-тест окон был бы истинным всегда (тот же порядок,
	//   что у хотбара). Ключ `asda.Inven.DropOutsideDelete` 0 — прежнее поведение.
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	FName OverZone = NAME_None;
	if (WM && WM->IsCursorOverAnyUiWindow(OverZone))
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] бросок над окном «%s» (не ячейка/корзина/панель) — эталон ничего не делает"),
			*OverZone.ToString());
		RefreshGrid();
		return;
	}
	if (!DropOutsideWindows(SrcCont, SrcCell)) { RefreshGrid(); }
}

void UAsdaInventorySubsystem::ShowCellTooltip(int32 ContainerId, int32 Cell)
{
	const FAsdaItemInstance* I = GetCell(ContainerId, Cell);
	if (!I || I->IsEmpty()) { return; }
	// ★★ ОСНОВНОЙ ПУТЬ — настоящий тултип `ITEM_TIPWIN` (5000) через мост подсистемы тултипа.
	//   Мост устанавливается в `UAsdaItemTooltipSubsystem` (AsdaItemTooltipSubsystem.cpp:3592-3596),
	//   то есть в рабочей игре ветка ниже НЕ выполняется. Прежний комментарий «ТОЧКА ПОДКЛЮЧЕНИЯ
	//   СЛЕДУЮЩЕГО ЗАХОДА» устарел (К3 аудита `code_dedup_audit_2.md`) — мост давно подключён.
	if (Tooltip.ShowItemTooltip)
	{
		const FVector2D Cur = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
		Tooltip.ShowItemTooltip(ContainerId, Cell, Cur);
		bTipShown = true;
		return;
	}
	// ФОЛБЭК: подсистемы тултипа нет / мост не установлен (сломанная сборка, ранний кадр). Показываем
	//   ровно то, что ЕСТЬ в данных, общей плашкой HUD — как это делает хотбар.
	//   ★★★ ЗАКОН `ui-shared-tip-ownership` (B7 аудита): владелец ОБЯЗАТЕЛЕН. Раньше здесь молча
	//   работал `EAsdaTipOwner::Legacy` по умолчанию, и покадровые опросчики курсора чужих окон
	//   гасили эту плашку. Владелец тот же, что у основного пути — `Item`.
	UGameInstance* GI = GetGameInstance();
	UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
	if (!Mini) { return; }
	UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	FString Title = IT ? IT->GetItemName(I->ItemCode) : FString();
	if (Title.IsEmpty()) { Title = UAsdaTextSubsystem::FormatPrintf(LocText(990463), { FString::FromInt(I->ItemCode) }); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Предмет %d»
	Mini->ShowSharedTip(Title, I->ToLine(), EAsdaTipOwner::Item);
	bTipShown = true;
}

void UAsdaInventorySubsystem::HideCellTooltip()
{
	if (!bTipShown) { return; }
	bTipShown = false;
	if (Tooltip.HideItemTooltip) { Tooltip.HideItemTooltip(); return; }
	// ★★★ B7: тот же владелец, что при показе фолбэком — иначе `HideSharedTip` не найдёт своего
	//   и плашка останется висеть (закон `ui-shared-tip-ownership`: гасит только владелец).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaMiniStateSubsystem* Mini = GI->GetSubsystem<UAsdaMiniStateSubsystem>())
		{
			Mini->HideSharedTip(EAsdaTipOwner::Item);
		}
	}
}

void UAsdaInventorySubsystem::TickPointer()
{
	if (!bWindowOpen || !bControlsReady || !FSlateApplication::IsInitialized()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ⚠ МОДАЛКА ИЗ ЭТОГО УСЛОВИЯ УБРАНА и проверяется НИЖЕ, под `Mouse.Update()` — см. гейт там.
	//   Чужой слой поверх окна по-прежнему выходит ДО опроса: там латч не трогается сознательно.
	if (WM->IsPointerBlockedByTopmost(kInvenWindowId)) { return; }

	// ★ Аудит №3 (B3): опрос обеих кнопок и фронты — общая точка. Вызов стоит РОВНО там, где стоял
	//   прежний опрос ЛКМ: ранние выходы выше (окно закрыто, модалка, чужой слой поверх) латч не
	//   трогают. ПКМ раньше опрашивалась НИЖЕ по функции — в пределах кадра это то же состояние.
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ★ ПОЧЕМУ ПРОВЕРКА МОДАЛКИ ПЕРЕЕХАЛА СЮДА, ПОД ОПРОС. Раньше она стояла одним условием с
	//     `IsPointerBlockedByTopmost` ВЫШЕ `Mouse.Update()` и защёлки НЕ сбрасывала: латч замирал
	//     в доносном состоянии, и на возврате из-под нотиса выстреливал ЛОЖНЫЙ фронт ОТПУСКАНИЯ —
	//     то есть «применить/снять вещь», которого игрок не заказывал. `Reset()` кладёт латч в
	//     «кнопка отпущена», поэтому удержанная кнопка даст потом ЧЕСТНЫЙ фронт нажатия.
	if (AsdaUi::IsModalDispatchBlocked(GI))
	{
		// Начатый захват снимаем ТОЛЬКО визуально: `EndDrag` выполнил бы перенос, а это отмена.
		if (bDragActive) { CancelDragVisual(); RefreshGrid(); }
		DragArm.Disarm();
		CellDbl.Reset();   // накопитель двойного клика
		TickScrollBar(/*bLmb*/false, /*bLmbDownEdge*/false);        // снять латч захвата бегунка
		if (HoverCell >= 0) { HoverContainer = 0; HoverCell = -1; HideCellTooltip(); }
		if (HintShown != 0)
		{
			HintShown = 0;
			if (Tooltip.HideItemTooltip) { Tooltip.HideItemTooltip(); }
		}
		Mouse.Reset();
		return;
	}

	// ── ПОЛОСА ПРОКРУТКИ (ребёнок колёсного окна 494) ──
	//   Геометрия уже пересчитана в `Tick` (окно таскается мышью, полоса сидит на ЖИВОЙ позиции
	//   ячейки 0). Ввод разбираем ДО сетки: в эталоне полоса ловит мышь раньше содержимого окна.
	TickScrollBar(Mouse.bLmb, Mouse.bLmbDown);
	// Пока полоса держит мышь, окно не должно ни выбирать ячейку, ни показывать подсказку.
	//   Пересечений с ячейками у полосы нет (она правее сетки), но проверка дешёвая и явная.
	const bool bScrollInput = ScrollBar.bThumbGrab || IsCursorOverScrollBar();

	int32 UnderCont = 0, UnderCell = -1;
	const bool bUnder = HitTest(UnderCont, UnderCell) && !bScrollInput;

	// ── события 8/9/0xA: наведение → тултип ──
	// ★ 21.10.2026 (ревизия самоцветов, МЕЛОЧЬ-2): «в руке» ЧУЖОЙ перенос (камень из гнезда/списка правой панели —
	//   `UAsdaSowelSubsystem`) — подсказки ячеек не всплывают, как и при переносе своего предмета: признак переноса у
	//   оригинала один на весь интерфейс (`CUIManager+0x04`, его порт — общий реестр `FAsdaDragGhost::IsAnyActive`).
	const bool bForeignDrag = !bDragActive && FAsdaDragGhost::IsAnyActive();
	if (bForeignDrag)
	{
		if (HoverCell >= 0) { HoverContainer = 0; HoverCell = -1; HideCellTooltip(); }
	}
	else if (!bDragActive)
	{
		if (bUnder && (UnderCont != HoverContainer || UnderCell != HoverCell))
		{
			HoverContainer = UnderCont; HoverCell = UnderCell;
			HideCellTooltip();
			ShowCellTooltip(UnderCont, UnderCell);
		}
		else if (!bUnder && HoverCell >= 0)
		{
			HoverContainer = 0; HoverCell = -1;
			HideCellTooltip();
		}
	}
	else
	{
		UpdateDragGhost();
	}

	// ── Спец-ПОДСКАЗКИ служебных контролов (эталон, событие 8, ui_core.c:47816-47836) ──
	//   449 `Inven_ItemUpgrade_Btn` → заголовок 98300 / тело 98400;
	//   10008 `INVEN_EQUIICON_BAR`  → заголовок 110325 / тело 110300.
	//   ★ Третьего кейса эталона (20195 → 110113/110112) в ЭТОМ окне нет: контрола 20195 в
	//     `gamemain_jui.ini` не существует вовсе — «ссылок в срезе нет».
	if (Tooltip.ShowHintTooltip && !bDragActive && !bForeignDrag)
	{
		int32 WantHint = 0;                 // 0 — нет, 449 / 10008 — какой именно
		if (IsCursorOver(HintUpgradeBtn))   { WantHint = 449; }
		else if (IsCursorOver(HintDuriBar)) { WantHint = 10008; }
		if (WantHint != HintShown)
		{
			HintShown = WantHint;
			if (WantHint == 449)        { Tooltip.ShowHintTooltip(98300, 98400); }
			else if (WantHint == 10008) { Tooltip.ShowHintTooltip(110325, 110300); }
			else if (Tooltip.HideItemTooltip) { Tooltip.HideItemTooltip(); }
		}
	}

	// ★ Д6: подписи вкладок следуют за своими кнопками (окно можно таскать мышью).
	SyncTabCaptions();

	// Наведение на кнопку закрытия 403: эталон прячет `Inven_Gear` и показывает `Inven_AniGear`.
	//   ★ 08.11.2026: при `asda.UI.CloseGearHover 1` шестерёнки ведёт общая точка UAsdaCloseGearSubsystem.
	if ((Gear || AniGear) && !AsdaCloseGear::IsCommonOn())
	{
		const bool bOverClose = IsCursorOver(BtnClose);
		SetVis(Gear, !bOverClose);
		if (AniGear)
		{
			if (bOverClose && AniGear->GetVisibility() == ESlateVisibility::Collapsed) { AniGear->RestartAnim(); }
			SetVis(AniGear, bOverClose);
		}
	}

	// ★★★ ВОЛНА 9 ЗАТОЧКИ (С-1): общая блокировка `uiCheckRecentActivity@0xD326A0` — пока окно 78200 ведёт
	//   переплавку (и секунду после ответа 6501), эталон `CGameWindow_OnCommand` глушит сообщения 5 (захват),
	//   6 (подсветка броска), 7 (бросок), 0xB (выбор ячейки) и 0xD (двойной клик = применить) — ui_core.c:47768,
	//   :47776, :47784, :47932, :47991. Наведение/тултип и ПКМ (0xE) НЕ глушатся. Разбор условий и мест —
	//   у `AsdaUi::CheckRecentActivity`. Без этого выбранное в 78200 снаряжение можно было перетащить во время
	//   полосы, и 6500 ушёл бы по ячейке, где теперь другой предмет.
	// ★★★ ВОЛНА 11 (СРЕДН-1): НАДЕТЫЕ СЛОТЫ (контейнер 3) из-под блокировки ВЫВЕДЕНЫ. У эталона слоты
	//   600…610 и 83531…83539 — группа 6, `ui_DispatchCommandEvent` отдаёт их НЕ в `CGameWindow_OnCommand`,
	//   а в `uiControlMessageProc(g_pEquipSetWindow)@0xC6C030` (ui_core.c:34823 → :34127-34283). В этой
	//   процедуре `uiCheckRecentActivity` не зовётся НИ В ОДНОЙ ветке (8/9/0xA тултип, 0xB
	//   `uiEquipSlotSelect`, 0xD `itemAction_SendUseEquipPacket` = снять, 5/6/7 захват/бросок); шесть мест
	//   вызова блокировки — только `craftWnd_Close` и `CGameWindow_OnCommand` (скан бинаря, волна 10).
	//   Поэтому нажатие по надетому слоту и отпускание взведённого надетого слота идут ОБЫЧНЫМ разбором:
	//   0xB (выбор) и 0xD (двойной клик = снять) работают и во время переплавки. Захвата у надетых слотов в
	//   порту нет (`CanBeginDrag` = false), активный захват из сумки (начат до блокировки) сюда не пускаем —
	//   его бросок (7) эталон глушит в процедуре инвентаря, и ветка блокировки ниже снимает его визуально.
	const bool bRecentLock = AsdaUi::CheckRecentActivity(GI);
	const bool bEquipBypass = bRecentLock && ((!bDragActive
		&& ((Mouse.bLmbDown && bUnder && UnderCont == 3)                               // нажатие (0xB) по надетому
		    || (!Mouse.bLmbDown && DragArm.IsArmed() && DragArm.KeyA == 3)))            // удержание/отпускание (0xD)
		|| (bDragActive && DragContainer == 3));                                        // ★ 07.11.2026: захват надетого (5/6/7) — та же процедура, гейта нет
	if (bRecentLock && !bEquipBypass)
	{
		if (Mouse.bLmbUp)
		{
			// Бросок (7) и применение (0xD) не выполняются; начатый захват снимаем ТОЛЬКО визуально.
			if (bDragActive) { CancelDragVisual(); RefreshGrid(); }
			CellDbl.Reset();
		}
		DragArm.Disarm();   // нажатие (0xB/5) не принято — взводить и начинать захват нечего
	}
	else
	{
	// ── события 5 (нажатие) / 7 (отпускание) / 0xD (двойной клик = применить) ──
	if (Mouse.bLmbDown)
	{
		if (bUnder)
		{
			// Событие 0xB «взять слот в выбор» (`CInventoryUI_OnSlotClick`) — курсор выбора.
			// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] `asda.Inven.EquipSelectTab`: `uiEquipSlotSelect` — вкладка до наполнения инфо-панели (`SetTab` её гасит).
			if (UnderCont == 3 && CVarEquipSelectTab.GetValueOnGameThread() != 0)
			{
				const FAsdaItemInstance* Worn = GetCell(3, UnderCell);
				if (Worn && !Worn->IsEmpty())
				{
					SetTab((UnderCell == 5 || UnderCell == 6 || UnderCell == 7 || UnderCell == 10) ? 0 : 1);
				}
			}
			if (UnderCont == 3) { SelEquipSlot = UnderCell; RefreshEquip(); }
			else                { SelCell = UnderCell; RefreshGrid(); }
			// ★ Эталон на ЭТОМ ЖЕ событии наполняет правую инфо-панель
			//   (`ItemTooltip_PopulateInfoPanel(this, 1, -1)` — item_inventory.c:36278,
			//    для экип-слота — `(…, 3, slot)`, ui_core.c:33972).
			if (Tooltip.FillInfoPanel) { Tooltip.FillInfoPanel(UnderCont, UnderCell); }
			// ★ Тем же событием эталон пересобирает СПИСОК САМОЦВЕТОВ и перерисовывает гнёзда
			//   (`inventorySowelWin_BuildSowelList` в той же ветке клика, item_inventory.c:36322).
			SowelNotifySelect(GetGameInstance(), UnderCont, UnderCell);
			// ★★★ КРУГ 2, п.1: панель подробностей раскрывается САМА при выборе снаряжения —
			//   это эталон, а не выдумка: `if (!IsVisible(m_pRightSideBack)) uiPanelExpand(this)`
			//   в обеих ветках клика по ячейке сумки (item_inventory.c:36325 и 36337) и в клике
			//   по НАДЕТОМУ слоту (ui_core.c:33973). Вкладку проверяет сам `AutoExpandOnSelect`.
			AutoExpandOnSelect(UnderCont, UnderCell);
			// ★★★ ЗАХВАТ ЗДЕСЬ БОЛЬШЕ НЕ НАЧИНАЕТСЯ — только ВЗВОДИТСЯ (общая точка `FAsdaDragArm`).
			//   Оригинал (`CControl_OnLButtonDown@0x12DB870`) начинает тем же кадром, что и нажатие;
			//   порог — осознанное отступление по требованию юзера, ключ отката
			//   `AsdaUi::FAsdaDragArm::kDragStartPixels = 0`. Разбор: `Reports/icon_drag_threshold.md`.
			//   Взводим ровно те ячейки, на которых прежний код поднимал `bDragActive`.
			// ★★★ ИСПРАВЛЕНО 2026-08-26 (дефект «снять вещь невозможно»). Взвод — это НЕ ТОЛЬКО
			//   «начало перетаскивания», это ЕЩЁ И латч клика: ветка отпускания ниже опознаёт
			//   двойной клик по `DragArm.IsArmed()`. Надетые вещи (контейнер 3) тащить нельзя
			//   (`CanBeginDrag` = false), и из-за этого по ним НЕ ПРИХОДИЛО ВООБЩЕ НИКАКОГО клика:
			//   `HandleCellClickRelease` для контейнера 3 не вызывался никогда, а значит и
			//   `UseByDoubleClick(3, …) → UnequipItem` — тоже. У эталона такой дыры нет: сообщение
			//   0xD приходит контролам 600…610 напрямую (`ui_core.c:34267`) и шлёт
			//   `itemAction_SendUseEquipPacket@0xC69900`.
			//   Поэтому ВЗВОДИМ и непустой надетый слот — а «сдвинули ⇒ тащим» ниже отдельно
			//   проверяет `CanBeginDrag` и для контейнера 3 захват не начинает.
			const FAsdaItemInstance* UnderItem = GetCell(UnderCont, UnderCell);
			const bool bArmForClick = (UnderCont == 3) && UnderItem && !UnderItem->IsEmpty();
			if (CanBeginDrag(UnderCont, UnderCell) || bArmForClick) { DragArm.Arm(UnderCont, UnderCell, Mouse.Cursor); }
			else                                                    { DragArm.Disarm(); }
		}
		else
		{
			DragArm.Disarm();          // нажали мимо ячеек — взводить нечего
		}
	}

	// ── «ЗАЖАЛИ И СДВИНУЛИ» ⇒ вот теперь захват (порог живёт в общей точке, не здесь) ──
	if (Mouse.bLmb && DragArm.ShouldStart(Mouse.Cursor))
	{
		// ★ При `asda.Inven.EquipDragOff 0` взведён может быть НЕТАСКАЕМЫЙ надетый слот (он взводится ради клика — см. выше).
		//   Для него захват НЕ начинаем и взвод НЕ снимаем: отпускание над той же ячейкой обязано остаться кликом.
		if (CanBeginDrag(DragArm.KeyA, DragArm.KeyB))
		{
			// ★ [ОРИГИНАЛ] 26.10.2026: гейт сообщения 5 (ui_core.c:47768-47774) — барабан бустера / свиток вызова хранилища.
			//   ★ 07.11.2026: у надетого слота своя процедура (`uiControlMessageProc`), этого гейта в ней нет.
			if (DragArm.KeyA != 3 && DragGateRefuses(TEXT("захват предмета")))
			{
				DragArm.Disarm();
			}
			else
			{
				BeginDrag(DragArm.KeyA, DragArm.KeyB);
				DragArm.MarkStarted();
			}
		}
	}

	if (Mouse.bLmbUp)
	{
		if (bDragActive)
		{
			const int32 SrcCont = DragContainer, SrcCell = DragCell;
			if (bUnder && UnderCont == SrcCont && UnderCell == SrcCell)
			{
				// ★★★ ИСПРАВЛЕНО: применение — только по ВТОРОМУ клику подряд по ТОЙ ЖЕ ячейке
				//   в пределах системного интервала двойного клика. Эталонное сообщение 0xD — это
				//   WM_LBUTTONDBLCLK (см. комментарий у полей `LastClick*` в заголовке); раньше
				//   порт применял по одиночному отпусканию и один физический двойной клик срабатывал
				//   дважды. Одиночный клик по-прежнему только ВЫДЕЛЯЕТ ячейку (событие 0xB),
				//   что уже сделано выше в ветке `bLmbDown`.
				CancelDragVisual();
				RefreshGrid();
				HandleCellClickRelease(SrcCont, SrcCell);
			}
			else
			{
				CellDbl.Reset();
				EndDrag();
			}
		}
		// ★★★ ПОРОГ НЕ ПРОЙДЕН ⇒ это был ОБЫЧНЫЙ КЛИК по ячейке, а не перетаскивание.
		//   Раньше сюда попадать было нельзя: захват начинался прямо на нажатии, поэтому клик
		//   разбирался веткой `bDragActive` выше. Визуал восстанавливать НЕ НАДО — `BeginDrag`
		//   не вызывался, иконка и текст стопки ячейки не трогались.
		else if (DragArm.IsArmed() && bUnder && UnderCont == DragArm.KeyA && UnderCell == DragArm.KeyB)
		{
			HandleCellClickRelease(DragArm.KeyA, DragArm.KeyB);
		}
		DragArm.Disarm();              // кнопку отпустили — взвод в любом случае снят
	}
	}   // ← конец ветки «блокировки `uiCheckRecentActivity` нет»
	// ── событие 14 (0xE) — ПКМ по ячейке: меню «похвастаться предметом» `Open_Item_Win` (28000) ──
	//   Эталон `ui_core.c:48036`: открывается ТОЛЬКО по непустой ячейке, позиция = курсор+(32,32).
	//   Проверку пустоты и гашение своего тултипа делает сама подсистема хвастовства.
	if (Mouse.bRmbDown && bUnder && !bDragActive)
	{
		if (UAsdaShowOffSubsystem* SO = GI->GetSubsystem<UAsdaShowOffSubsystem>())
		{
			if (const FAsdaItemInstance* R = GetCell(UnderCont, UnderCell))
			{
				SO->OpenMenuForCell(UnderCont, UnderCell, R->ItemCode);
			}
		}
	}
	// ★ Аудит №3 (B3): обе записи латча («ЛКМ» и «ПКМ») делает `Mouse.Update()` в начале функции.
}

// ══════════════════════════════ ГЛАВНОЕ МЕНЮ (кнопка 73402) ════════════════════════════════════

void UAsdaInventorySubsystem::EnsureMenuBinding()
{
	UGameInstance* GI = GetGameInstance();
	if (!bMenuBound)
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI ? GI->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr)
		{
			Menu->OnMenuCommand.AddDynamic(this, &UAsdaInventorySubsystem::OnMenuCommand);
			// ★★★ ЕДИНЫЙ МЕХАНИЗМ: подключаем окно к своей кнопке. Дальше подсветку кнопки
			//   ведёт сама панель меню — из фактической видимости `INVEN_WIN`, каким бы путём
			//   окно ни открылось/закрылось (клавиша «I», кнопка 73402, крестик, пакет, смерть).
			Menu->RegisterWindow(kMenuButtonIdNum);
			bMenuBound = true;
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] подписка на кнопку меню %d выполнена"), kMenuButtonIdNum);
		}
	}
	// ★ Клавиша «I»: контроллер появляется позже подсистемы — подписываемся лениво (та же схема,
	//   что у хотбара; DYNAMIC-делегат ⇒ AddDynamic).
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* Pc = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (Pc && Pc != BoundController.Get())
	{
		if (AAsdaPlayerController* Old = BoundController.Get())
		{
			Old->OnGameCommand.RemoveDynamic(this, &UAsdaInventorySubsystem::OnGameCommand);
		}
		Pc->OnGameCommand.AddDynamic(this, &UAsdaInventorySubsystem::OnGameCommand);
		BoundController = Pc;
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] подписка на клавишу «Инвентарь» (команда %d) выполнена"),
			kCmdInterfaceInventory);
	}
}

void UAsdaInventorySubsystem::OnGameCommand(int32 Cmd)
{
	if (Cmd != kCmdInterfaceInventory) { return; }
	ToggleWindow();   // эталон `uiToggleInventoryWindow(a1, 1)`
}

void UAsdaInventorySubsystem::OnMenuCommand(int32 ButtonIdNum, int32 /*TargetWindowId*/, bool bOpen)
{
	if (ButtonIdNum != kMenuButtonIdNum) { return; }
	// ⚠ Открытие идёт ОТЛОЖЕННО: колбэк прилетает изнутри обработчика чекбокса меню.
	PendingWindowAction = bOpen ? 1 : 2;
}

// ══════════════════════════════ МОСТ ДЛЯ ХОТБАРА ═══════════════════════════════════════════════

int32 UAsdaInventorySubsystem::HotbarSubTypeToContainer(int32 HotbarSubType)
{
	// ★★★ ЯВНАЯ ТАБЛИЦА (обоснование — в шапке объявления). Источник: `hotbarRebuildSlots@0xDEC380`
	//   (ui_core.c:66949-66964). Совпадение чисел — ФАКТ ЭТАЛОНА, а не «оставили как есть».
	switch (HotbarSubType)
	{
	case 1:  return 1;   // хотбар «сумка»    → контейнер 1 (ShellInven, 30) — вкладка «Снаряжение»
	case 2:  return 2;   // хотбар «2»        → контейнер 2 (SpendCellobInven, 60) — «Расходники»
	case 3:  return 3;   // хотбар «комплект» → контейнер 3 (НАДЕТОЕ, 11)
	default: return 0;
	}
}

// ★★★ ОБРАТНАЯ ТАБЛИЦА. Источник — `hotbarAssignInventoryItem@0xD11840` (ui_core.c:47231):
//   ветка a2 == 1 читает `g_InventoryBag` (контейнер 1) и пишет `BYTE1(Src[0]) = 1`,
//   ветка a2 == 2 читает `g_InventoryArray2` (контейнер 2) и пишет `BYTE1(Src[0]) = 2`.
//   Третьей ветки в этой функции НЕТ: надетое (контейнер 3) в панель не кладут вовсе —
//   слоты экипировки только ЦЕЛЬ броска (`uiDialog_DispatchControlMsg`), захватить их нельзя.
//   ⚠ SubType 3 в записи слота при этом существует и приходит с сервера (пакет 5059) — читаем
//   его прямой таблицей выше, но САМИ такую запись не создаём: эталон её не создаёт тоже.
int32 UAsdaInventorySubsystem::ContainerToHotbarSubType(int32 ContainerId)
{
	switch (ContainerId)
	{
	case 1:  return 1;   // контейнер 1 (ShellInven, 30)        → SubType 1
	case 2:  return 2;   // контейнер 2 (SpendCellobInven, 60)  → SubType 2
	default: return 0;   // 3 (надетое) не захватывается, 5 (склад) в записи слота не кодируется
	}
}

void UAsdaInventorySubsystem::BindHotbarBridge()
{
	if (bHotbarBridgeBound) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb) { return; }

	FAsdaHotbarInventoryBridge B;

	// Чтение ячейки. `Container` приходит в ПРОСТРАНСТВЕ SubType записи хотбара — транслируем.
	B.GetEntry = [this](int32 HotbarSubType, int32 CellIndex) -> FAsdaHotbarItemEntry
	{
		FAsdaHotbarItemEntry E;
		const int32 Cont = HotbarSubTypeToContainer(HotbarSubType);
		const FAsdaItemInstance* I = GetCell(Cont, CellIndex);
		if (!I || I->IsEmpty()) { return E; }   // bValid=false ⇒ хотбар молча чистит слот (LABEL_29)
		E.bValid = true;
		E.ItemCode = I->ItemCode;
		E.Count = I->Amount;
		if (UGameInstance* G = GetGameInstance())
		{
			if (UAsdaItemTableSubsystem* IT = G->GetSubsystem<UAsdaItemTableSubsystem>())
			{
				if (const FAsdaItemRecord* R = IT->GetItem(I->ItemCode))
				{
					E.ItemType = R->ItemType;
					E.ItemSubType = R->ItemSubType;
					E.bHasItemRecord = true;   // эталонное `ItemRecord != nullptr` (см. заголовок)
				}
			}
		}
		// ★★★ Raw57 ОБЯЗАТЕЛЕН: пакет 5000 несёт ПОЛЯ ЗАПИСИ (+5, +7, +4, +9, +14), а не индекс ячейки.
		E.Raw57 = I->Raw;
		return E;
	};

	// ★★★ Гейт «контейнер уже получен целиком» — без него хотбар чистит слоты по ещё не пришедшему
	//   содержимому и увозит пустую страницу на сервер пакетом 5067. Транслируем SubType→контейнер
	//   той же таблицей, что и `GetEntry`.
	B.IsContainerReady = [this](int32 HotbarSubType) -> bool
	{
		return IsContainerReceived(HotbarSubTypeToContainer(HotbarSubType));
	};

	// «Использовать из сумки» — эталон `net_sendInventoryItemUse@0xD11B20`: 5000, dstType = 3 (НАДЕТЬ),
	//   dstSlot = ItemRecord->itemType. Гейт эталона: только `itemType != 11` и не самоцвет.
	B.UseFromBag = [this](int32 CellIndex)
	{
		const FAsdaItemInstance* I = GetCell(1, CellIndex);
		if (!I || I->IsEmpty()) { return; }
		UGameInstance* G = GetGameInstance();
		UAsdaItemTableSubsystem* IT = G ? G->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const FAsdaItemRecord* R = IT ? IT->GetItem(I->ItemCode) : nullptr;
		// ★★★ ВОЛНА 2. Эталонный `Hotbar_SlotActivate@0xDEDDD0`, ветка «тип 1, подтип 1»: если записи
		//   в `item_list` НЕТ, но есть запись в `special_itemlist`, слот уходит в ПРЕМИУМ-канал
		//   (`CItemManager_UseInventoryItem@0xA0FDF0` → 5450/5455), а не в 5000. Раньше порт в этом
		//   случае молча выходил — премиум-предметы с панели не работали вовсе.
		if (!R && IT && IT->IsSpecialItemCode(I->ItemCode))
		{
			// ★ 07.10.2026 (волна 2-Г, ревизия С-1): прежде `UsePremiumItem` напрямую — свитки 38/64 с панели уходили 5450
			//   мимо окна 1552. Теперь — общая точка развилки `CItemManager_UseInventoryItem`.
			// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 16, баг 3): гейт «персонаж занят» эталона (ui_core.c:67403-67420) — разбор у функции.
			if (AsdaPremiumUseBusyBlocked(G, I->ItemCode)) { return; }
			UseSpecialItemDispatch(1, CellIndex);
			return;
		}
		FAsdaSowellRow SowelRow;
		if (!R || R->ItemType == 11 || (IT && IT->GetSowell(I->ItemCode, SowelRow))) { return; }
		EquipItem(1, CellIndex);
	};

	// Двойной клик: bFromBag = true → контейнер 1, false → контейнер 2 (эталон `itemUseDoubleClick`).
	B.UseByDoubleClick = [this](int32 CellIndex, bool bFromBag)
	{
		UseByDoubleClick(bFromBag ? 1 : 2, CellIndex);
	};

	// «Применить надетое» = СНЯТЬ (эталон `itemAction_SendUseEquipPacket`, srcType = 3).
	B.UseFromEquipSet = [this](int32 EquipSlotIndex) -> bool
	{
		return UnequipItem(EquipSlotIndex);
	};

	// ★★★ УДАЛЕНО 2026-08-15: тут стоял `B.IsInventoryOpen = ...`, по которому хотбар запрещал
	//   применение слотов и смену страницы при открытом инвентаре. Гейт эталона
	//   `CObject_GetField254(g_pInventoryWnd)` (`ui_core.c:67365`, `:67722`) означает
	//   «открыт барабан гачи» (`set_oFE@0xD1F7D0`), а не «открыт инвентарь» — окно инвентаря
	//   панель быстрого доступа в оригинале не блокирует. Подробности — в шапке
	//   `UAsdaHotbarSubsystem::IsActivationBlocked`.

	// Тултип предмета ячейки — тот же путь, что и у окна (мост следующего захода либо плашка HUD).
	B.ShowTooltip = [this](int32 HotbarSubType, int32 CellIndex)
	{
		ShowCellTooltip(HotbarSubTypeToContainer(HotbarSubType), CellIndex);
	};

	Hb->SetInventoryBridge(B);
	bHotbarBridgeBound = true;
	UE_LOG(LogAsdaInven, Log,
		TEXT("[инвентарь] МОСТ ХОТБАРА подключён: GetEntry/UseFromBag/UseByDoubleClick/UseFromEquipSet/"
		     "ShowTooltip; трансляция SubType→контейнер 1→1, 2→2, 3→3"));
}

// ══════════════════════════════ TICK ═══════════════════════════════════════════════════════════

// Разбор одиночных пакетов ВНЕ колбэка сети (закон `no-reentrant-net-callbacks`): колбэк только
//   кладёт тело в очередь, вся работа — здесь. Вынесено из `Tick` отдельным методом, чтобы
//   PIE-проверка могла прокрутить очередь без входа в мир (`asda.Inven.Pump`): у `Tick` стоит
//   гейт «мы в мире», а PIE без сервера в мир не входит.
void UAsdaInventorySubsystem::DrainPendingPackets()
{
	// ★ Аудит №3 (B10): `Drain` = прежнее «под замком забрать всё разом и очистить».
	TArray<FPendingPacket> Packets;
	PendingPackets.Drain(Packets);
	for (const FPendingPacket& P : Packets)
	{
		switch (P.Kind)
		{
		case 5001: ApplyMoveResult(P.Body);  break;
		case 5003: ApplySlotUpdate(P.Body);  break;
		case 4999: ApplySlotRemoved(P.Body); break;
		case 5029: ApplyUseResult(P.Body);   break;
		case 5007: ApplyDumpResult(P.Body);  break;
		case 1:    ApplyLootRecord(P.Body);  break;
		// ★★★ ВОЛНА 2 — те же правила: колбэк сети только кладёт тело в очередь
		//   ([[no-reentrant-net-callbacks]]), вся работа делается здесь, в Tick.
		case 6097: ApplyPackageResult(P.Body);  break;
		case 5469: ApplyGachaResult(P.Body);    break;
		case 5451: ApplyFunctionResult(P.Body); break;
		case 5457: ApplyRepairResult(P.Body);   break;
		case 6570: ApplySlotDurability6570(P.Body); break;   // износ предмета (рыбалка В1)
		case 5456: ApplyBagExpand(P.Body);      break;   // расширение склада (подтип 34)
		default: break;
		}
	}
}

void UAsdaInventorySubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Inventory");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	// Инвентарь живёт только В МИРЕ (эталон: окно создаётся при входе, g_GameState == 1).
	if (!LM || LM->CurrentMapId < 0 || !WS || !IsValid(WS->LocalPlayerActor))
	{
		bWasInWorld = false;
		return;
	}

	// ★★★ ВХОД В МИР. Подсистема живёт на GameInstance и переживает перезаход. Содержимое контейнеров
	//   прошлой сессии снимает сброс `LeftWorld`/`EnteringWorld` (★ [ИСПРАВЛЕНИЕ] 25.10.2026, `ResetSessionState`;
	//   раньше оно здесь «перезаписывалось новыми батчами» — но батч несёт только ЗАНЯТЫЕ ячейки, и вещи
	//   прошлой учётки оставались в пустых ячейках новой). Признак «получено целиком»
	//   от ПРОШЛОЙ сессии обязан быть снят: иначе хотбар примет ещё не пришедшее содержимое за
	//   «ячейка пуста» и почистит слоты (см. `IsContainerReceived`).
	if (!bWasInWorld)
	{
		bWasInWorld = true;
		ContainerReceivedMask = 0;
	}

	// ⚠ Приём содержимого применяем ЗДЕСЬ, а не в колбэке пакета (закон no-reentrant-net-callbacks).
	// ★★★ ПОРЯДОК ЗНАЧИМ: содержимое контейнеров применяем ДО `BindHotbarBridge()`. Регистрация
	//   моста дёргает `UAsdaHotbarSubsystem::RebuildSlots()`, а тот по пустой ячейке ЧИСТИТ слот
	//   панели (эталон LABEL_29). По проводу оригинал шлёт 4045/4048 РАНЬШЕ 5059 — восстанавливаем
	//   ровно этот порядок. (Одного порядка мало: мост может встать в кадр, когда батчей ещё нет
	//   вовсе — поэтому есть и гейт `IsContainerReceived`.)
	ApplyPendingBatches();

	EnsureMenuBinding();   // меню появляется позже подсистемы — подписываемся лениво
	BindHotbarBridge();    // хотбар тоже: мост ставим сразу, как только он есть

	// ⚠ То же для одиночных пакетов: колбэк только положил тело в очередь.
	DrainPendingPackets();

	// ★ 09.10.2026 (автолопата): кадр цикла — ПОСЛЕ разбора 5029 (ответ на повтор перезапускает таймер в этом же кадре).
	TickAutoDig();

	// Отложенное открытие/закрытие окна (пришло из колбэка меню или консоли).
	if (PendingWindowAction != 0)
	{
		const int32 A = PendingWindowAction;
		PendingWindowAction = 0;
		if (A == 1) { OpenWindow(); } else { CloseWindow(); }
	}

	// Предохранитель по флагу ожидания: эталон его по таймауту НЕ снимает (потерянный 5001 «вешал»
	//   инвентарь до перезахода) — у нас окно разблокируется само.
	if (bActionPending && (FPlatformTime::Seconds() - ActionSentAtS) > (double)ActionPendingTimeoutS)
	{
		bActionPending = false;
		UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь] ответ 5001 не пришёл за %.1f с — флаг ожидания снят"),
			ActionPendingTimeoutS);
	}

	TickAutoArrange();

	if (bWindowOpen)
	{
		if (!EnsureWidget()) { return; }
		// Полосу раскладываем ДО `TickPointer`: тот выходит раньше при открытой модалке и при
		//   перекрытии окна сверху, а нарисована полоса должна быть в любом случае (в эталоне она
		//   часть контрола 494 и от блокировки ввода не исчезает).
		LayoutScrollBar();
		// ★ Шторки отката — ДО `TickPointer`: тот выходит раньше при открытой модалке и при
		//   перекрытии окна сверху, а откат идёт независимо от того, доступно ли окно для ввода
		//   (та же причина, по которой выше стоит `LayoutScrollBar`).
		TickCooldownCurtains();
		TickPointer();
	}
}
