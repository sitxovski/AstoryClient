// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTooltipSubsystemInternal.h — ★ R2 10.11.2026 (разрез AsdaItemTooltipSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaItemTooltipSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaItemTooltipSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaItemTooltipSubsystem.h"
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
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaMiniStateSubsystem.h"   // ★ R5-fix Г15: LegacySprintfOneInt — общий разбор printf-шаблона с одним целым
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaTextureSubsystem.h"   // атлас иконки инфо-панели (423)

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

// Категория журнала частей AsdaItemTooltipSubsystem*.cpp: определение — AsdaItemTooltipSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaItemTip, Log, All );

// Окно реестра под курсором в момент показа плашки — верхнее по z среди видимых, кроме окон самих плашек.
//   NAME_None — курсор не над окном (показ из консоли и т. п.): такая подсказка за окном не следит.
static FName TipSourceWindowUnderCursor(UGameInstance* GI, const UAsdaTipPanel* Main, const UAsdaTipPanel* Compare)
{
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return NAME_None; }
	const UUserWidget* MainW = Main ? Main->GetWidget() : nullptr;
	const UUserWidget* CmpW = Compare ? Compare->GetWidget() : nullptr;
	FName Best = NAME_None;
	int32 BestZ = INT32_MIN;
	for (const FName& Id : WM->GetRegisteredWindowIds())
	{
		const UUserWidget* W = WM->FindWindow(Id);
		if (!W || W == MainW || W == CmpW || !WM->IsWindowVisible(Id) || !WM->IsCursorOverWindow(Id)) { continue; }
		const int32 Z = WM->GetWindowZOrder(Id);
		if (Z > BestZ) { BestZ = Z; Best = Id; }
	}
	return Best;
}

// Ключ CVarSowelHoverTip: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
extern TAutoConsoleVariable<int32> CVarSowelHoverTip;
#else
extern TAsdaConstCVar<int32> CVarSowelHoverTip;
#endif

// Ключ CVarLegacyOnePipeline: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
extern TAutoConsoleVariable<int32> CVarLegacyOnePipeline;
#else
extern TAsdaConstCVar<int32> CVarLegacyOnePipeline;
#endif

// Ключ CVarBagNonGearSimple: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
extern TAutoConsoleVariable<int32> CVarBagNonGearSimple;
#else
extern TAsdaConstCVar<int32> CVarBagNonGearSimple;
#endif

// Предикат исправления бага 11: премиум-предмет (categoryFlag 3 — только он попадает на вкладку «Снаряжение/Премиум», будучи
//   не снаряжением) с itemType 10/11, показанный полным путём (SourceKind 1). ★ r2 (ревизия 01.11): без проверки категории
//   под правку попадали и не-премиальные 10/11 в окнах, которые зовут полный путь сами (ссылка в чате, награда задания, рыбалка,
//   чужой персонаж, опции, сброс, магазин извлечения), — их состав остаётся прежним. Одна точка для показа, дампа и просмотрщика.
static bool AsdaBagNonGearUsesSimple(const FAsdaItemRecord* R)
{
	return R && (R->ItemType == 10 || R->ItemType == 11) && R->CategoryFlag == 3
		&& CVarBagNonGearSimple.GetValueOnGameThread() != 0;
}

// Ключ CVarGoldAmountTip: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
extern TAutoConsoleVariable<int32> CVarGoldAmountTip;
#else
extern TAsdaConstCVar<int32> CVarGoldAmountTip;
#endif

// Код предмета «Золото» — эталон сравнивает с литералом 20551 во всех ветках (item_inventory.c:60016, 61181).
static constexpr int32 kGoldItemCode = 20551;

// Предикат исправления «золото только суммой»: одна точка для показа и дев-дампа `asda.ItemTip.Pipelines`.
static bool AsdaGoldUsesAmountTip(int32 ItemCode)
{
	return ItemCode == kGoldItemCode && CVarGoldAmountTip.GetValueOnGameThread() != 0;
}

static const FName  kTipWindowId(TEXT("ITEM_TIPWIN"));

// Тултип выше всплывашек (kTopmostZ 9000) и выше окон (инвентарь 720), ниже модалок.
static constexpr int32 kTipZ     = 9100;

// ── цвета эталона (ARGB, sRGB) ───────────────────────────────────────────────────────────────────
static constexpr uint32 kColRequire   = 0xFFF45151;  // «мало уровня» / «не тот класс» (colorTitle/colorReqLevel)

static constexpr uint32 kColSealed    = 0xFF3D9AF8;  // примечание «запечатан» (нотис 2044)

static constexpr uint32 kColSpecOpt   = 0xFF00BFF3;  // опции спец-предмета / сет-бонус

static constexpr uint32 kColDurZero   = 0xFFFF0000;  // прочность == 0

// ── id строк таблицы текста (эталонные нотисы) ───────────────────────────────────────────────────
static constexpr int32 kLocClassLabel     = 2297;    // «Профессия»
static constexpr int32 kLocClassNameBase  = 12556;   // + usageCategory → имя профессии (см. ниже)
static constexpr int32 kLocGradeLabel     = 1420;    // «Тип»
static constexpr int32 kLocGradeNameBase  = 1421;    // + itemGrade → «Обычный/Редкий/…»
static constexpr int32 kLocLevelLabel     = 2330;    // «Уровень»
static constexpr int32 kLocLevelSuffix    = 2331;    // «минимум»

static constexpr int32 kLocPowerLevel     = 110641;  // «Уровень возможностей»

static constexpr int32 kLocLevelShort     = 12215;   // «Ур.»

static constexpr int32 kLocStatEffect     = 2360;    // «Эффект ЖК»
static constexpr int32 kLocWeight         = 2295;    // «Вес»

static constexpr int32 kLocUpgradesLeft   = 50024;   // «Доступно улучшений:»
static constexpr int32 kLocDurability     = 110308;  // «Прочность:»

static constexpr int32 kLocNoTrade        = 8401;    // «Эта вещь не передается.»
static constexpr int32 kLocNoTradeEquip   = 8503;    // «Не передается после экипировки»
static constexpr int32 kLocNoTradeUse     = 8504;    // «Не передается после использования.»
static constexpr int32 kLocNoTradeUse2    = 8508;
static constexpr int32 kLocSealedNote     = 2044;    // примечание при заточке >= 30
static constexpr int32 kLocEnhanceNameBase= 50100;   // + уровень заточки → «[Шедевр]/[Гениально]/…»
static constexpr int32 kLocSowelBonusBase = 8510;    // + bonusType → имя бонуса самоцвета
static constexpr int32 kLocSowelAtk       = 110420;  // «Атака ЖК»
static constexpr int32 kLocSowelMAtk      = 110421;  // «Магическая атака ЖК»
static constexpr int32 kLocSowelDef       = 110422;  // «Защита ЖК»

// ★ Нотис 6829 — «Количество использований». ВАЖНО: в эталоне он относится к контролу **415**
//   (`INVEN_JOB`, `pTooltip[58]`) и ТОЛЬКО в ветке записи ВТОРОЙ таблицы, при `flag_57 == 2`
//   и `count(+9) > 0` (item_inventory.c:36678-36688). Порт печатал его в 479 у обычных предметов —
//   это была ВЫДУМКА, снята 2026-08-09. Строка используется ВЕТКОЙ `special_itemlist` (см. ниже).
static constexpr int32 kLocUseCountSpecial = 6829;
// ★ 108512 «Уровень + N» (контрол 425) и 108513 «Сила улучшения + N%» (контрол 497) — это строки
//   ВЕТКИ ЗАПИСЕЙ ВТОРОЙ ТАБЛИЦЫ (`special_itemlist`, subType 204/206), item_inventory.c:36692-36717.
//   В ветке обычного предмета их НЕТ.
static constexpr int32 kLocEnhLevelLblSpecial = 108512;
static constexpr int32 kLocEnhPowerLblSpecial = 108513;
static constexpr int32 kLocEliteFooter    = 109832;  // подвал «элитного» предмета (плашка 5005/5055)  // «Сила улучшения» (инфо-панель 497)

// ═══════════════════ доступ к сырым байтам записи предмета ═══════════════════
// ★ D4 (аудит `code_dedup_audit_2.md`): чтение полей записи переехало в `AsdaBytes` (вне границ ⇒ 0).
//   `RawI32` — это `*(int *)(pItemSlot + 9)` эталона (количество/счётчик), НЕвыровненный i32.
static FORCEINLINE int32 RawU8 (const FAsdaItemInstance& It, int32 Off) { return AsdaBytes::SafeU8 (It.Raw, Off); }
static FORCEINLINE int32 RawI16(const FAsdaItemInstance& It, int32 Off) { return AsdaBytes::SafeI16(It.Raw, Off); }
static FORCEINLINE int32 RawI32(const FAsdaItemInstance& It, int32 Off) { return AsdaBytes::SafeI32(It.Raw, Off); }

// ★ Байт СЫРОЙ записи `special_itemlist` (300 б после снятия MD5) из колонки `DataHex`.
//   Нужен для полей, у которых нет отдельной колонки: `flag_57` (+57) и `flag_124` (+124).
//   −1, если строка короткая/битая (вызывающий трактует как «условие не выполнено»).
// ★ D4: тело в `AsdaBytes::HexByteAt` (общая точка разбора колонки `DataHex`).
static FORCEINLINE int32 SpecialRawByte(const FString& Hex, int32 Off) { return AsdaBytes::HexByteAt(Hex, Off); }

// Подстановка ОДНОГО целого в printf-шаблон таблицы (`ItemOptionTable.BIN` +4).
//   ⚠ Это НЕ `UAsdaMiniStateSubsystem::SubstituteDaysKeepPercents`: там намеренно СОХРАНЯЮТСЯ
//   «%%» как два символа (обход дефекта оригинала в строке 110922), а здесь шаблоны корректные
//   («Критический удар при атаке +%d%%») и «%%» обязано схлопнуться в «%», как в C-sprintf.
//   ⚠ И это НЕ `AsdaUi::FormatPrintfArgs` (D8 аудита `code_dedup_audit_2.md`): та подставляет
//   ГОТОВЫЕ СТРОКИ в `%s`/`%d` подряд и спецификаторов не разбирает, а здесь нужен полноценный
//   разбор `%[флаги/ширина/точность][diu]` с ОДНИМ числовым аргументом и «съеданием» чужих
//   спецификаторов (второй `%d` в шаблоне даёт «0», как у эталонного `sprintf` с одним аргументом).
//   Алгоритмы разные — сводить их нельзя, это было бы изменением поведения.
// ★ R5-fix Г15 (13.11.2026, чистка): тело дословно повторяло `UAsdaMiniStateSubsystem::LegacySprintfOneInt` (тот же разбор
//   `%%`, флагов/ширины/точности, `d|i|u` с одним аргументом и «0» для следующих, «съедание» чужих спецификаторов; единственное
//   различие в тексте — «%» в самом конце строки: здесь `break`, там `i = j; continue` — оба заканчивают цикл). Теперь это
//   вызов общей функции; равенство вывода держит самотест `asda.SelfTest.SprintfOneInt`.
static FString FormatOneInt(const FString& Fmt, int32 Arg)
{
	return UAsdaMiniStateSubsystem::LegacySprintfOneInt(Fmt, Arg);
}

// ★ D7/К2 (аудит `code_dedup_audit_2.md`): копия `String_FormatThousands@0xEC00D0` убрана —
//   алгоритм живёт в `AsdaUi::FormatThousands`. Прежний комментарий консервировал дубль
//   («тамошний метод приватный, а файл инвентаря этим заходом трогать нельзя») — ограничение
//   захода снято, дубля больше нет. Строку 20551 («<N> Золото») собирает эталонный простой
//   билдер, item_inventory.c:60020.
static FString Colored(uint32 Argb, const FString& S)
{
	return FString::Printf(TEXT("<FC=%08X>%s</F>"), Argb, *S);
}
