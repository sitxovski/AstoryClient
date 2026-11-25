// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTooltipSubsystem_Build.cpp — ★ R2 10.11.2026 (разрез AsdaItemTooltipSubsystem.cpp, дословный перенос): сборка подсказки: строки/секции, Table2/Sowel/Simple/BagNonGear.
//   Общее для частей — в AsdaItemTooltipSubsystemInternal.h; шапка и реверс — в AsdaItemTooltipSubsystem.cpp.
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
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaTextureSubsystem.h"   // атлас иконки инфо-панели (423)
#include "Text/AsdaTextSubsystem.h"
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

static ASDA_DEV_CVAR(int32, CVarItemReachSubJob,
	TEXT("asda.Item.ReachSubJob"), 1,
	TEXT("1 — строка «Профессия» подсказки предмета красится по основному классу ИЛИ подклассу (как эталон upgradeAnyGradeReachable); ")
	TEXT("0 — как прежде: только по основному классу."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarLegacyStatDupBug,
	TEXT("asda.ItemTip.LegacyStatDupBug"), 0,
	TEXT("1 — воспроизводить дефект оригинала: повтор предыдущей строки стата при нерезолвнутом коде"),
	ECVF_Default);

// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 (баг «Pig Pang … shall not be open till level 10», сессия item-level-boxes): простая подсказка
//   `itemType 11` (коробки, бустеры 11/66, расходники) у эталона — «Вес» + описание, БЕЗ уровня, хотя минимальный уровень
//   теперь проверяется при применении (клиент `AsdaItemLevelGate`, сервер P95). 1 — первой строкой «Уровень : N минимум»
//   (красным, если персонаж ниже), когда RequiredLevel > 1; 0 — как эталон.
static ASDA_DEV_CVAR(int32, CVarTipLevelLine,
	TEXT("asda.ItemTip.LevelLine"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — строка минимального уровня в простой подсказке itemType 11 (RequiredLevel > 1); 0 — как эталон"),
	ECVF_Default);

// ★★★ ОТКАТ ИСПРАВЛЕНИЯ ДЕФЕКТА ОРИГИНАЛА В СТРОКЕ «Профессия» ТУЛТИПА САМОЦВЕТА.
//   Эталон вычисляет нотис 8564 + `partGrade`, но печатает ЛИТЕРАЛ 8564 («Для всех») — asm
//   EA31DC/EA3202. 1 — вернуть эталонный литерал, 0 — печатать вычисленный (8564/8565/8566).
// ★ [ИСПРАВЛЕНИЕ] 07.11.2026, слово владельца «Да, внедрить»: сервер (`sub_4CDAA0`, код 14) требует уровень камня в гнезде вещи, а оригинал показывает его
//   только в подсказке самого камня. Камень ставится при создании вещи и бывает выше её уровня, поэтому отказ выглядел необъяснимым.
//   1 — строка уровня камня под уровнем вещи, если камень выше; 0 — как оригинал.
static ASDA_DEV_CVAR(int32, CVarTipSowelLevelLine,
	TEXT("asda.ItemTip.SowelLevelLine"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — в подсказке снаряжения строка уровня живого камня, если он выше уровня вещи (красная, если персонаж ниже); 0 — как эталон."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarLegacySowelJobLine,
	TEXT("asda.ItemTip.LegacySowelJobLine"), 0,
	TEXT("1 — «Профессия» самоцвета всегда «Для всех» (дефект оригинала 1:1); 0 — по partGrade"),
	ECVF_Default);

// ★★★ ОТКАТ: СТРОКА «Прочность 0/0» У ПРЕДМЕТОВ БЕЗ ПРОЧНОСТИ.
//   Эталон печатает строку прочности БЕЗУСЛОВНО (`item_inventory.c:37022`: `sprintf` +
//   `CControl_SetVisible(pTooltip[64], 1)` в обеих ветках), поэтому у расходника в подсказке
//   появляется бессмысленная красная «Прочность: 0/0» — `DurabilityMax` у него 0. Владелец
//   потребовал строку не выводить (2026-08-28). Скрываем ТОЛЬКО когда прочность И текущая,
//   И предельная равны нулю: у сломанной вещи (`0/N`) строка обязана остаться красной.
static ASDA_DEV_CVAR(int32, CVarLegacyZeroDurabilityLine,
	TEXT("asda.ItemTip.LegacyZeroDurabilityLine"), 0,
	TEXT("1 — печатать «Прочность 0/0» у предметов без прочности (эталон 1:1); 0 — строку скрывать"),
	ECVF_Default);

static constexpr uint32 kColBound     = 0xFFFAD508;  // «не передаётся» (colorBound/stateColor)

static constexpr uint32 kColOption    = 0xFFFDBC49;  // строки опций самоцветов (colorOption)
static constexpr uint32 kColStat      = 0xFF01DF0B;  // строки статов (item_buildStatTooltip)
static constexpr uint32 kColStatPlus  = 0xFFFFFF99;  // парный стат «+» (statIndex 3, тип 96/97)
static constexpr uint32 kColStatMinus = 0xFFFFCC66;  // парный стат «−» (statIndex 4)

static constexpr uint32 kColDurOk     = 0xFFFFFFFF;  // прочность > 0

static constexpr int32 kLocKindLabel      = 9564;    // «Вид»
static constexpr int32 kLocWeaponKindBase = 8552;    // + itemSubType → «Короткий меч/Меч/Копье/…»
static constexpr int32 kLocFishingRod     = 111074;  // «Удочка» (itemVariant == 1)

static constexpr int32 kLocSowelCount     = 2298;    // «Количество живых камней»

static constexpr int32 kLocPairStatPlus   = 111054;  // префикс парного стата (тип 96)
static constexpr int32 kLocPairStatMinus  = 111055;  // префикс парного стата (тип 97)
static constexpr int32 kLocSpecOptTail    = 110310;  // хвост Item_BuildOptionTooltip

// ── строки ВТОРОГО пайплайна (простой тултип по типу, `CItem_BuildSowelTooltipText@0xEA2EC0`) ──
static constexpr int32 kLocGold        = 2304;    // «Золото» — хвост строки у кода 20551
static constexpr int32 kLocArrowCount  = 2305;    // «Количество стрел» (itemType 10, subType != 204)
static constexpr int32 kLocBaitStock   = 111079;  // «Запас наживки»    (itemType 10, subType == 204)
// `sub_922EFC@0x922EFC` → thunk на `notice_FormatById@0xEB1080` (skill_buff.c:37089):
//   строка = `UI_FormatLocalizedNotice(linkCode + 8587)`. Печатается у itemType 5/6/7.
static constexpr int32 kLocLinkCodeBase = 8587;
// ── строки ВЕТКИ ЗАПИСЕЙ ВТОРОЙ ТАБЛИЦЫ в `ItemTooltip_Build` ──
//   ⚠ 6787 в таблице текста = «Вес», и эталон печатает с ним `LOWORD(Table2Record->requiredLevel)`
//   (item_inventory.c:60597/60614/60620). То есть поле +0x44 записи `special_itemlist`, которое
//   декодер назвал `RequiredLevel`, по СМЫСЛУ — вес; имя колонки менять не стали (чужой файл).
static constexpr int32 kLocWeightSpecial = 6787;
static constexpr int32 kLocUseCountTail  = 6828;  // «счет» — хвост строки «Количество использований : N»

// ── цвета ВТОРОГО пайплайна (ARGB; эталонные знаковые константы = 0x100000000 − |N|) ──
static constexpr uint32 kColSimpleBind   = 0xFFDE98D8; // −2189096 `colorB`: имя расходника subType 44..53/8/9/12
static constexpr uint32 kColSimpleEffect = 0xFFE1B7B8; // −1984584 `colorEffect`: строка эффекта (linkCode)
static constexpr uint32 kColSimpleReqBad = 0xFFFB5151; // −306863  `colorReqLevelFail`: «мало уровня» у 5/6/7

// ── ТУЛТИП САМОЦВЕТА (`CItem_BuildSowelTooltipText`, первая половина) ───────────────────────────
static constexpr int32 kLocSowelJobLabel    = 9563;   // «Профессия »
static constexpr int32 kLocSowelJobBase     = 8564;   // + partGrade → «Для всех»/«Не ниже второй»/«Не ниже третьей»
static constexpr int32 kLocSowelWeaponsLabel= 9573;   // «Доступное оружие»
static constexpr int32 kLocSowelWeaponBase  = 9455;   // + номер бита маски `OptionMask` (0..18)
static constexpr int32 kLocSowelKindPlain   = 8584;   // «Живой камень»   (specialFlag == 0)
static constexpr int32 kLocSowelKindCombo   = 8585;   // «Составной ЖК»   (specialFlag != 0)
static constexpr int32 kLocSowelPlaceLabel  = 2299;   // «Место в снаряжении»
static constexpr int32 kLocSowelSocketBase  = 8569;   // + байт +45/+46/+47 (только при значении <= 0x0E)
static constexpr int32 kLocSowelOwner       = 6832;   // «Владелец»
static constexpr uint32 kColSowelGrade      = 0xFF478AEF; // `colorGrade = -12088593` — строка владельца

// ═══════════════════ источники данных ═══════════════════
// ★ [ПОРТ] 29.10.2026: экземпляр «отдельного комплекта» просмотрщика предметов (UI/AsdaItemTipStandalone.h) берёт
//   таблицы и текст из комплекта — его GameInstance-владелец служебный и подсистем не держит. Игровой экземпляр — прежний путь.
UAsdaItemTableSubsystem* UAsdaItemTooltipSubsystem::Items() const
{
	if (AsdaItemTipStandalone::IsStandaloneTip(this)) { return AsdaItemTipStandalone::Items(); }
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
}
UAsdaTextSubsystem* UAsdaItemTooltipSubsystem::Text() const
{
	if (AsdaItemTipStandalone::IsStandaloneTip(this)) { return AsdaItemTipStandalone::Text(); }
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
}
// ★ D10 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::LocRaw`. Фолбэк тултипа —
//   ПУСТАЯ строка (bDebugFallback = false): пустой блок тултипа просто не рисуется, а «[#N]» (до 11.10.2026 — «[loc N]», N9)
//   уехал бы игроку на экран. Поведение сохранено 1:1.
FString UAsdaItemTooltipSubsystem::LocRaw(int32 Id) const
{
	// ★ [ПОРТ] 29.10.2026: экземпляр комплекта просмотрщика — тот же `FormatNotice` текста комплекта (язык — текущий FAsdaLang).
	if (AsdaItemTipStandalone::IsStandaloneTip(this))
	{
		UAsdaTextSubsystem* T = AsdaItemTipStandalone::Text();
		return T ? T->FormatNotice(Id) : FString();
	}
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocRaw` (поведение то же).
	return AsdaNotice::LocRaw(GetGameInstance(), Id, /*bDebugFallback*/false);
}
int32 UAsdaItemTooltipSubsystem::SelfLevel() const
{
	// ★ [ПОРТ] 29.10.2026: у комплекта просмотрщика — уровень «профиля зрителя».
	if (AsdaItemTipStandalone::IsStandaloneTip(this)) { return AsdaItemTipStandalone::ViewerLevel(); }
	// ★ Волна 1 (14.09.2026, исправление E4): общая точка `AsdaSelf::GetLevel`.
	//   БЫЛО: при `SelfLevel == 0` (до 4005/4036) бралась ПЕРВАЯ запись char-list с уровнем > 0 — любой персонаж
	//   аккаунта, а не выбранный; от неё красился «требуемый уровень» и выбирался премиум-диапазон.
	//   ЭТАЛОН: уровень игрока в тултипе — байт своего персонажа `g_LocalPlayer + 1082` (item_inventory.c:6805),
	//   то есть ВЫБРАННОГО; чужих записей выбора персонажа тултип не читает. Фолбэк порта по выбранному слоту —
	//   тот же, что у HUD, мини-статуса, биржи и меню смерти.
	return AsdaSelf::GetLevel(GetGameInstance());
}
int32 UAsdaItemTooltipSubsystem::SelfClass() const
{
	// ★ [ПОРТ] 29.10.2026: у комплекта просмотрщика — класс «профиля зрителя».
	if (AsdaItemTipStandalone::IsStandaloneTip(this)) { return AsdaItemTipStandalone::ViewerClass(); }
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return 0; }
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5d): класс — общая точка `AsdaSelf::GetClassId` (байт +0x438, фолбэк — запись char-select rec+24).
	//   Было: фолбэк на `SelfJob` (профессия +0x445). «Неизвестен» — по-прежнему 0.
	return FMath::Max(AsdaSelf::GetClassId(Net), 0);
}

// ═══════════════════ цвета ═══════════════════
uint32 UAsdaItemTooltipSubsystem::GradeColorArgb(int32 Grade)
{
	// `CItemManager_InitDropColors@0x9C4510` — 9 инициализируемых значений; индекс > 9 → элемент 0
	//   (`CItemManager_GetDropColor@0x9C45C0`). Индекс 9 в InitDropColors НЕ пишется — берём элемент 0
	//   (в оригинале там неинициализированная память; воспроизводить мусор нельзя).
	static const uint32 kDrop[9] = {
		0xFFFFFFFF, 0xFFFFFF46, 0xFFFF96DC, 0xFF96FF32, 0xFFFF7800,
		0xFF4141FF, 0xFFBE50FF, 0xFFFA1E46, 0xFF9BEBEB
	};
	if (Grade < 0 || Grade > 8) { return kDrop[0]; }
	return kDrop[Grade];
}

uint32 UAsdaItemTooltipSubsystem::EnhanceColorArgb(int32 Enhance)
{
	// `itemGetGradeColor@0xD29940` (item_inventory.c:44261) — 1:1.
	switch (Enhance)
	{
	case 5: return 0xFFFFFFFF;
	case 6: return 0xFFFFFF46;
	case 7: return 0xFFFF96DC;
	case 8: return 0xFF96FF32;
	case 9: return 0xFFFF7800;
	default: return 0xFFFEF078;
	}
}

// `upgradeCheckGradeReachable@0xEC9010` (item_enhance.c:9716) — 1:1, включая структуру ветвлений.
bool UAsdaItemTooltipSubsystem::CheckGradeReachable(int32 A1, int32 A2)
{
	if (A2 == 0) { return true; }                                   // предмет без требований к классу
	auto InRange = [](int32 V, int32 Lo, int32 Hi) { return V >= Lo && V <= Hi; };
	// Три «расы» по 11 ступеней: 1..11, 12..22, 23..33; далее ремёсла 34..39 и «любой N-й ранг» 40..44.
	if (InRange(A2, 1, 11) || InRange(A2, 12, 22) || InRange(A2, 23, 33))
	{
		// Внутри одной ветки достаточно, чтобы класс игрока был не ниже требуемой ступени.
		const int32 Base = InRange(A2, 1, 11) ? 1 : (InRange(A2, 12, 22) ? 12 : 23);
		return InRange(A1, Base, Base + 10) && A1 >= A2;
	}
	if (InRange(A2, 34, 39))
	{
		switch (A2)
		{
		case 34: return A1 == 34 || A1 == 35;
		case 35: return A1 == 35;
		case 36: return A1 == 36 || A1 == 37;
		case 37: return A1 == 37;
		case 38: return A1 == 38 || A1 == 39;
		default: return A1 == 39;
		}
	}
	switch (A2)
	{
	case 40: return A1 > 0;
	case 41: return InRange(A1, 2, 11) || InRange(A1, 13, 22) || InRange(A1, 24, 39);
	case 42: return InRange(A1, 3, 11) || InRange(A1, 14, 22) || InRange(A1, 25, 39);
	case 43: return InRange(A1, 4, 11) || InRange(A1, 15, 22) || InRange(A1, 26, 39);
	case 44:
	{
		static const int32 kFinal[15] = { 5,7,9,11,16,18,20,22,27,29,31,33,35,37,39 };
		for (int32 V : kFinal) { if (A1 == V) { return true; } }
		return false;
	}
	default: return false;
	}
}

bool UAsdaItemTooltipSubsystem::IsUsageCategoryReachable(int32 UsageCategory) const
{
	// Эталон `upgradeAnyGradeReachable@0xEC8F70` (item_enhance.c:9705-9713) читает ДВА байта класса: `gap_438[0]` и `gap_438[1]`
	//   (основной класс и подкласс) — `reachable(класс) || reachable(подкласс)`.
	// ★ 26.10.2026 [ОРИГИНАЛ] (ключ `asda.Item.ReachSubJob`, 1): второй байт — подкласс `AsdaSelf::GetSubJob` (`SelfSubJob` =
	//   +0x439, 1017/5042). Прежде порт брал только основной класс (комментарий «второй байт нигде не принимается» устарел): у 4-й
	//   профессии (основной класс 3/14/25, ступень в подклассе) требование «4-й ранг» (43) и классы 4…7/15…18/26…29 красились
	//   красным, у эталона — нет. 0 — прежнее поведение (только основной класс).
	const int32 C = SelfClass();
	if (CheckGradeReachable(C, UsageCategory)) { return true; }
	if (CVarItemReachSubJob.GetValueOnGameThread() == 0) { return false; }
	// ★ [ПОРТ] 29.10.2026: у комплекта просмотрщика подкласс — из «профиля зрителя».
	const int32 Sub = AsdaItemTipStandalone::IsStandaloneTip(this) ? AsdaItemTipStandalone::ViewerSubJob()
	                                                                : AsdaSelf::GetSubJob(GetGameInstance());
	return Sub > 0 && CheckGradeReachable(Sub, UsageCategory);
}

// ═══════════════════ вспомогательные строители ═══════════════════
FString UAsdaItemTooltipSubsystem::FormatOptionValue(int32 OptionId, int32 RawValue) const
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || OptionId <= 0) { return FString(); }
	const UDataTable* Dt = IT->GetDataTable(TEXT("itemoptiontable"));
	if (!Dt) { return FString(); }
	// `CItemManager_FindRecordById@0xB62200` — ЛИНЕЙНЫЙ поиск по полю id (ключевого индекса у таблицы нет).
	for (const TPair<FName, uint8*>& P : Dt->GetRowMap())
	{
		const FAsdaItemoptiontableRow* R = reinterpret_cast<const FAsdaItemoptiontableRow*>(P.Value);
		if ((int32)R->OptionId != OptionId) { continue; }
		// Эталон: значение делится на 100 при id >= 300 (проценты хранятся сотыми долями).
		const int32 V = (OptionId < 300) ? RawValue : (int32)((double)RawValue / 100.0);
		// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `itemoptiontable`, ключ `<_row>.FmtString` (имя строки — ключ карты).
		return FormatOneInt(FAsdaLang::ResolveRow(Dt, P.Key, TEXT("FmtString"), R->FmtString), V);
	}
	return FString();
}

// `item_buildStatTooltip@0xEC5300` — 5 пар (тип, значение) из записи, шаг 4 начиная с +29.
FString UAsdaItemTooltipSubsystem::BuildStatLines(const FAsdaItemInstance& It) const
{
	FString Out;
	for (int32 i = 0; i < 5; ++i)
	{
		const int32 Type = RawI16(It, 29 + 4 * i);
		const int32 Val  = RawI16(It, 31 + 4 * i);
		if (Type <= 0) { continue; }
		FString Line = FormatOptionValue(Type, Val);
		if (Line.IsEmpty())
		{
			// Д4: эталон здесь ПОВТОРЯЕТ прошлую строку (см. комментарий у CVarLegacyStatDupBug).
			if (CVarLegacyStatDupBug.GetValueOnAnyThread() != 0 && !Out.IsEmpty())
			{
				Out += Out;   // 1:1 «залипший» буфер оригинала
			}
			continue;
		}
		FString Rendered = Colored(kColStat, Line);
		// Особые «парные» статы: индексы 3 и 4 при типе 96/97 получают ПРЕФИКС-нотис и свой цвет.
		if (i == 3 && (Type == 96 || Type == 97))
		{
			Rendered = LocRaw(Type == 96 ? kLocPairStatPlus : kLocPairStatMinus) + TEXT(" ") + Colored(kColStatPlus, Line);
		}
		else if (i == 4 && (Type == 96 || Type == 97))
		{
			Rendered = LocRaw(Type == 96 ? kLocPairStatPlus : kLocPairStatMinus) + TEXT(" ") + Colored(kColStatMinus, Line);
		}
		Out += Rendered + TEXT(" \n");
	}
	return Out;
}

// `CItemManager_BuildSowelTooltip@0xEC4F80` — гнёзда 1..3 (+18/+20/+22), агрегация по типу бонуса.
FString UAsdaItemTooltipSubsystem::BuildSowelLines(const FAsdaItemInstance& It) const
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT) { return FString(); }
	struct FAgg { int32 Code = 0; int32 BonusType = -1; int32 Value = 0; };
	FAgg Agg[3];
	int32 Count = 0;
	for (int32 i = 1; i <= 3; ++i)
	{
		const int32 Code = RawI16(It, 16 + 2 * i);
		if (Code <= 0) { continue; }
		FAsdaSowellRow Row;
		if (!IT->GetSowell(Code, Row)) { continue; }
		int32 Slot = INDEX_NONE;
		for (int32 k = 0; k < Count; ++k) { if (Agg[k].BonusType == Row.BonusStatType) { Slot = k; break; } }
		if (Slot >= 0)
		{
			Agg[Slot].Code = Code; Agg[Slot].BonusType = Row.BonusStatType;
			Agg[Slot].Value += Row.BonusValue;                       // ЭТАЛОН: значения суммируются
		}
		else if (Count < 3)
		{
			Agg[Count].Code = Code; Agg[Count].BonusType = Row.BonusStatType;
			Agg[Count].Value = Row.BonusValue;
			++Count;
		}
	}
	FString Out;
	for (int32 j = 0; j < Count; ++j)
	{
		if (Agg[j].Code <= 0) { continue; }
		const FString Name = LocRaw(kLocSowelBonusBase + Agg[j].BonusType);
		Out += (Agg[j].Value <= 0) ? (Name + TEXT("\n"))
		                           : FString::Printf(TEXT("%s %d\n"), *Name, Agg[j].Value);
	}
	return Out;
}

// `Item_BuildOptionTooltip@0xEC5980` — 2 пары (тип, значение) записи спец-предмета + хвост 110310.
FString UAsdaItemTooltipSubsystem::BuildSpecialOptionLines(const FAsdaItemInstance& It) const
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT) { return FString(); }
	// Эталон читает WORD-поля записи `CItemManager_GetTable2Record_0` = pricetable (13 dword, шаг 52)
	//   по байтовым смещениям +24/+28 и +32/+36 → это dw06/dw07 и dw08/dw09.
	FAsdaPriceRow Price;
	if (!IT->GetPrice(It.ItemCode, Price)) { return FString(); }
	const int32 Pairs[2][2] = { { Price.dw06, Price.dw07 }, { Price.dw08, Price.dw09 } };
	FString Out;
	for (int32 i = 0; i < 2; ++i)
	{
		if (Pairs[i][0] <= 0) { continue; }
		const FString Line = FormatOptionValue(Pairs[i][0], Pairs[i][1]);
		if (Line.IsEmpty()) { continue; }
		Out += Colored(kColSpecOpt, Line) + TEXT(" \n");
	}
	// Хвост: у всех, кроме «зарядного» предмета (itemSubType == 10 && itemVariant == 1).
	if (const FAsdaItemRecord* R = IT->GetItem(It.ItemCode))
	{
		if (!(R->ItemSubType == 10 && R->ItemVariant == 1))
		{
			const FString Tail = LocRaw(kLocSpecOptTail);
			if (!Tail.IsEmpty()) { Out += Colored(kColSpecOpt, Tail) + TEXT(" \n"); }
		}
	}
	return Out;
}

// `itemTooltip_FormatStatRangeCat9/9Alt/3/8` — все четыре имеют ОДИНАКОВОЕ тело, различаются
//   только условием входа (itemType == 9 / без проверки / itemType == 8). Формула:
//     eff = pct * (base/100) + base;  результат = (int)(бонусСамоцвета * eff/100)
//   base — `ItemRecord->gap_88[0]` (верх) и `gap_88[2]` (низ), либо стек-запись при categoryFlag==3.
FString UAsdaItemTooltipSubsystem::FormatSowelStatRange(const FAsdaItemInstance& It, int32 BonusType) const
{
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT) { return FString(); }
	const FAsdaItemRecord* R = IT->GetItem(It.ItemCode);
	const int32 SowelCode = RawI16(It, 16);
	FAsdaSowellRow Sow;
	if (!R || SowelCode <= 0 || !IT->GetSowell(SowelCode, Sow)) { return FString(); }

	// Условие входа эталона: bonusType 0/1 → itemType == 9, 2 → без проверки, 3 → itemType == 8.
	bool bEnter = false;
	switch (BonusType)
	{
	case 0: case 1: bEnter = (R->ItemType == 9); break;
	case 2:         bEnter = true;               break;
	case 3:         bEnter = (R->ItemType == 8); break;
	default:        bEnter = false;              break;
	}
	int32 Min = 0, Max = 0;
	if (bEnter)
	{
		// ★★★ Д2 КРУГА 5 «У оружия пишет Атака 0 ~ 0» — ЗАКРЫТО.
		//   Причина: у shell-предмета (`categoryFlag == 3`, все премиум-баллисты) эталон берёт базы
		//   НЕ из `ItemRecord->gap_88`, а из строки `inventoryFindMatchingStackSlot@0xD34260`
		//   (item_inventory.c:62864-62871: `*(int*)(row+20)` и `*(int*)(row+24)`). Порт эту ветку
		//   оставлял пустой с пометкой «структуры нет» — и получал ровно 0 ~ 0. Структура давно
		//   найдена (круг 3, П16): это `./Data/PremiumLevelTable.bin` → `DT_premiumleveltable`,
		//   и поиск 1:1 уже реализован в `UAsdaItemTableSubsystem::FindPremiumStatRange`
		//   (им же считается строка 497 «Эффект ЖК», которая на том же скриншоте показывает 18 ~ 186).
		//   Уровень игрока — как у эталона: `CClass_GetField142C(&g_InventoryBag)->level`.
		double BaseHi = (double)R->StatBaseHi;   // `gap_88[0]` → statValMin
		double BaseLo = (double)R->StatBaseLo;   // `gap_88[2]` → statValMax
		bool bHaveBase = true;
		if (R->CategoryFlag == 3)
		{
			int32 PremMin = 0, PremMax = 0;
			if (IT->FindPremiumStatRange(It.ItemCode, SelfLevel(), PremMin, PremMax))
			{
				BaseHi = (double)PremMin;    // строка[+20]
				BaseLo = (double)PremMax;    // строка[+24]
			}
			else
			{
				// Эталон при `!MatchingStackSlot` НЕ трогает statVal* — остаются нули. 1:1.
				bHaveBase = false;
			}
		}
		if (bHaveBase)
		{
			const double Pct = (double)RawI16(It, 27);
			const double EffHi = Pct * (BaseHi / 100.0) + BaseHi;
			const double EffLo = Pct * (BaseLo / 100.0) + BaseLo;
			Min = (int32)((double)Sow.BonusValue * (EffHi / 100.0));
			Max = (int32)((double)Sow.BonusValue * (EffLo / 100.0));
		}
	}
	return FString::Printf(TEXT("%d ~ %d"), Min, Max);
}

// ═══════════════════ ГЛАВНОЕ: сборка тела тултипа ═══════════════════
//   Секции 0..17 (буфер эталона textBuf[260*i]) и их источники:
//     0  «Профессия : <класс>»              нотис 2297 + 12556+usageCategory; красный если класс не подходит
//     1  «Тип : <грейд>»                    нотис 1420 + 1421+itemGrade
//     2  «Уровень : N минимум»              нотисы 2330/2331; КРАСНЫЙ если уровень игрока меньше
//     3  «Вид : <оружие>»                   нотис 9564 + 8552+itemSubType (или 111074 «Удочка»)
//     4  «Уровень возможностей : N Ур.»     нотисы 110641/12215 (только categoryFlag == 3)
//     5  «Эффект ЖК : min ~ max (+N%)»      нотис 2360
//     6  «Вес : N»                          нотис 2295, запись +14 (i16)
//     7  «Количество живых камней : n/m»    нотис 2298 (занятых гнёзд / всего)
//     8  «Доступно улучшений: c/m»          нотис 50024 (запись +26 − +25 / pricetable.dw03)
//     9  «Прочность: c/m»                   нотис 110308 (запись +13 / ItemRecord+0x68); 0 → красный
//     10 НЕ ПИШЕТСЯ НИКЕМ (ссылок в срезе нет) — секция остаётся пустой
//     11 имя бонуса самоцвета + диапазон    нотис 8510+bonusType + «%d ~ %d»
//     12 строки статов                      item_buildStatTooltip
//     13 опции спец-предмета                Item_BuildOptionTooltip (только categoryFlag == 3)
//     14 «не передаётся»                    нотисы 8401 / 8503
//     15 описание предмета (+ «запечатан»)  Item_tooltip.BIN; нотис 2044 при заточке >= 30
//     16 агрегат самоцветов                 CItemManager_BuildSowelTooltip
//     17 «Атака/Маг.атака/Защита ЖК : N»    нотисы 110420/110421/110422
void UAsdaItemTooltipSubsystem::BuildSections(const FAsdaItemInstance& It, bool bNoTitleLine, bool bNotBound,
                                              TArray<FString>& Sec, FString& OutTitle, FLinearColor& OutTitleColor) const
{
	Sec.Reset();
	Sec.SetNum(18);
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT) { return; }
	const FAsdaItemRecord* R = IT->GetItem(It.ItemCode);
	if (!R) { return; }

	const int32 Enhance = RawU8(It, 24);            // уровень заточки
	const int32 EnhPct  = RawI16(It, 27);           // процент заточки
	const int32 PlayerLv = SelfLevel();

	// ── ЗАГОЛОВОК (эталон ItemTooltip_Build) ──
	const FString Name = IT->GetItemName(It.ItemCode);
	OutTitleColor = AsdaUi::SrgbU32ToLinear(GradeColorArgb(R->ItemGrade));
	if (Enhance != 0)
	{
		const uint32 EC = EnhanceColorArgb(Enhance);
		if (Enhance < 5)
		{
			OutTitle = FString::Printf(TEXT("<FC=%08X>+%d%%</F> %s"), EC, EnhPct, *Name);
		}
		else
		{
			OutTitle = FString::Printf(TEXT("<FC=%08X>+%d%%</F> <FC=%08X>%s</F> %s"),
				EC, EnhPct, EC, *LocRaw(kLocEnhanceNameBase + Enhance), *Name);
		}
	}
	else
	{
		OutTitle = Name;
	}

	// ── подсчёты, общие для обеих веток ──
	int32 StatCount = 0;                                     // занятых гнёзд самоцветов (4 штуки)
	for (int32 i = 0; i < 4; ++i) { if (RawI16(It, 16 + 2 * i) > 0) { ++StatCount; } }
	const int32 SlotCount = (R->EnchantOrSocket == 4) ? 0 : (R->EnchantOrSocket + 1);

	// ── секция 0: профессия ──
	if (!bNoTitleLine)
	{
		const FString Line = FString::Printf(TEXT("%s : %s"), *LocRaw(kLocClassLabel), *LocRaw(kLocClassNameBase + R->UsageCategory));
		Sec[0] = IsUsageCategoryReachable(R->UsageCategory) ? (Line + TEXT("\n"))
		                                                    : (Colored(kColRequire, Line) + TEXT("\n"));
	}
	// ── секция 1: грейд ──
	Sec[1] = FString::Printf(TEXT("%s : %s \n"), *LocRaw(kLocGradeLabel), *LocRaw(kLocGradeNameBase + R->ItemGrade));
	// ── секция 2: требуемый уровень ──
	{
		const FString Line = FString::Printf(TEXT("%s : %d %s"), *LocRaw(kLocLevelLabel), R->RequiredLevel, *LocRaw(kLocLevelSuffix));
		Sec[2] = (PlayerLv < R->RequiredLevel) ? (Colored(kColRequire, Line) + TEXT(" \n"))
		                                       : (Line + TEXT(" \n"));
		if (CVarTipSowelLevelLine.GetValueOnGameThread() != 0)
		{
			int32 StoneLv = 0;   // самый высокий уровень камня в гнёздах записи +16…+22
			for (int32 i = 0; i < 4; ++i)
			{
				FAsdaSowellRow Row;
				const int32 Code = RawI16(It, 16 + 2 * i);
				if (Code > 0 && IT->GetSowell(Code, Row)) { StoneLv = FMath::Max(StoneLv, Row.RequiredLevel); }
			}
			if (StoneLv > R->RequiredLevel)
			{
				const FString Stone = FString::Printf(TEXT("%s - %s : %d %s"), *LocRaw(kLocSowelKindPlain), *LocRaw(kLocLevelLabel), StoneLv, *LocRaw(kLocLevelSuffix));
				Sec[2] += (PlayerLv < StoneLv) ? (Colored(kColRequire, Stone) + TEXT(" \n")) : (Stone + TEXT(" \n"));
			}
		}
	}
	// ── секция 15: описание (+ примечание при заточке >= 30) ──
	{
		FAsdaItemTooltipRow Tip;
		const FString Desc = IT->GetItemTooltip(It.ItemCode, Tip) ? Tip.Description : FString();
		Sec[15] = (Enhance < 30) ? FString::Printf(TEXT("%s \n"), *Desc)
		                         : FString::Printf(TEXT("%s\n\n%s \n"), *Desc, *Colored(kColSealed, LocRaw(kLocSealedNote)));
	}
	// ── секция 14: «не передаётся» ──
	if (RawU8(It, 50) != 0)
	{
		Sec[14] = FString::Printf(TEXT("\n%s\n\n"), *Colored(kColBound, LocRaw(kLocNoTrade)));
	}
	else if (R->TooltipFlag200 == 1)
	{
		Sec[14] = FString::Printf(TEXT("\n%s\n\n"), *Colored(kColBound, LocRaw(kLocNoTradeEquip)));
	}
	if (!bNotBound)   // эталонный аргумент notBound == 0 перекрывает секцию безусловно
	{
		Sec[14] = FString::Printf(TEXT("\n%s\n\n"), *Colored(kColBound, LocRaw(kLocNoTrade)));
	}
	// ── секция 3: вид оружия (только itemType == 9) ──
	if (R->ItemType == 9)
	{
		const FString Kind = (R->ItemVariant == 1) ? LocRaw(kLocFishingRod) : LocRaw(kLocWeaponKindBase + R->ItemSubType);
		Sec[3] = FString::Printf(TEXT("%s : %s\n"), *LocRaw(kLocKindLabel), *Kind);
	}

	// ── прочность / улучшения ──
	const int32 UpgradeCur = RawU8(It, 26) - RawU8(It, 25);
	// ⚠ `itemGetField3ByCode(pItem)` — ТЕЛА В СРЕЗЕ НЕТ. Форма функции восстановлена по близнецу
	//   `itemGetTable2RecordField3@0xD299C0` (запись по коду → DWORD-поле №3), а таблица — по
	//   `CItemManager_GetTable2Record_0@0x9C39F0` = pricetable (13 dword, шаг 52) ⇒ поле №3 = dw03.
	//   Сходится с данными: у экипировки dw03 = 5 («Доступно улучшений: N/5»).
	int32 UpgradeMax = 0;
	{
		FAsdaPriceRow Price;
		if (IT->GetPrice(It.ItemCode, Price)) { UpgradeMax = Price.dw03; }
	}
	const int32 DurCur = RawU8(It, 13);

	// ── атака (секция 5) ──
	const double Pct = (double)EnhPct;
	const int32 AtkHi = (int32)(Pct * ((double)R->StatBaseHi / 100.0) + (double)R->StatBaseHi);
	const int32 AtkLo = (int32)(Pct * ((double)R->StatBaseLo / 100.0) + (double)R->StatBaseLo);
	const bool  bCharged = (R->ItemSubType == 10 && R->ItemVariant == 1);   // «зарядный» предмет (удочка и т.п.)

	auto AttackLine = [&]() -> FString
	{
		return (EnhPct <= 0)
			? FString::Printf(TEXT("%s : %d ~ %d \n"), *LocRaw(kLocStatEffect), AtkHi, AtkLo)
			: FString::Printf(TEXT("%s : %d ~ %d (+%d%%) \n"), *LocRaw(kLocStatEffect), AtkHi, AtkLo, EnhPct);
	};

	if (R->CategoryFlag == 3)
	{
		// ── shell-предмет ──
		// Требуемый «уровень возможностей»: clamp(уровень игрока, dw10, dw11) + бонус за грейд.
		int32 PowerLv = 0;
		FAsdaPriceRow Price;
		if (IT->GetPrice(It.ItemCode, Price))
		{
			if (PlayerLv > Price.dw10) { PowerLv = (PlayerLv < Price.dw11) ? PlayerLv : Price.dw11; }
			else                       { PowerLv = Price.dw10; }
			if (R->ItemGrade == 1) { PowerLv += 3; }
			if (R->ItemGrade == 2) { PowerLv += 6; }
			if (R->ItemGrade == 3) { PowerLv += 9; }
			if (R->ItemGrade == 4) { PowerLv += 12; }
		}
		Sec[4] = FString::Printf(TEXT("%s : %d%s \n"), *LocRaw(kLocPowerLevel), PowerLv, *LocRaw(kLocLevelShort));
		if (!bCharged)
		{
			Sec[5] = AttackLine();
			Sec[8] = FString::Printf(TEXT("%s %d/%d \n"), *LocRaw(kLocUpgradesLeft), UpgradeCur, UpgradeMax);
		}
		Sec[6] = FString::Printf(TEXT("%s : %d \n"), *LocRaw(kLocWeight), It.StackWeight);
		Sec[7] = FString::Printf(TEXT("%s : %d/%d \n"), *LocRaw(kLocSowelCount), StatCount, SlotCount);
		Sec[9] = FString::Printf(TEXT("%s %d \n"), *LocRaw(kLocDurability), DurCur);
	}
	else
	{
		const uint32 DurColor = (DurCur == 0) ? kColDurZero : kColDurOk;
		if (!bCharged)
		{
			Sec[5] = AttackLine();
			Sec[7] = FString::Printf(TEXT("%s : %d/%d \n"), *LocRaw(kLocSowelCount), StatCount, SlotCount);
			Sec[8] = FString::Printf(TEXT("%s %d/%d \n"), *LocRaw(kLocUpgradesLeft), UpgradeCur, UpgradeMax);
		}
		Sec[6] = FString::Printf(TEXT("%s : %d \n"), *LocRaw(kLocWeight), It.StackWeight);
		// ★ Предмет без прочности (`0/0`) строки не получает — разбор у `CVarLegacyZeroDurabilityLine`.
		if (DurCur != 0 || R->DurabilityMax != 0 || CVarLegacyZeroDurabilityLine.GetValueOnAnyThread() != 0)
		{
			Sec[9] = Colored(DurColor, FString::Printf(TEXT("%s %d/%d"), *LocRaw(kLocDurability), DurCur, R->DurabilityMax)) + TEXT(" \n");
		}
	}

	// ── самоцвет гнезда 0: имя бонуса (11) + значение (17) ──
	const int32 Sowel0 = RawI16(It, 16);
	if (Sowel0 > 0)
	{
		FAsdaSowellRow Sow;
		if (IT->GetSowell(Sowel0, Sow))
		{
			const FString BonusName = LocRaw(kLocSowelBonusBase + Sow.BonusStatType);
			switch (Sow.BonusStatType)
			{
			case 0: Sec[17] = FString::Printf(TEXT("%s : %d \n"), *LocRaw(kLocSowelAtk),  Sow.BonusValue); break;
			case 1: Sec[17] = FString::Printf(TEXT("%s : %d \n"), *LocRaw(kLocSowelMAtk), Sow.BonusValue); break;
			case 2: Sec[17] = FString::Printf(TEXT("%s : %d \n"), *LocRaw(kLocSowelDef),  Sow.BonusValue); break;
			default: break;      // bonusType 3 — только диапазон, отдельной строки эталон не пишет
			}
			Sec[11] = FString::Printf(TEXT("%s %s \n"), *BonusName, *FormatSowelStatRange(It, Sow.BonusStatType));
		}
	}

	// ── секции 12/13/16 ──
	{
		const FString Stats = BuildStatLines(It);
		if (Stats.Len() > 1) { Sec[12] = Stats + TEXT(" \n"); }
	}
	{
		const FString SowelAgg = BuildSowelLines(It);
		if (SowelAgg.Len() > 1) { Sec[16] = Colored(kColOption, SowelAgg) + TEXT("\n"); }
	}
	if (R->CategoryFlag == 3)
	{
		const FString Opts = BuildSpecialOptionLines(It);
		if (Opts.Len() > 1) { Sec[13] = Opts + TEXT(" \n"); }
	}
}

FAsdaItemTooltipText UAsdaItemTooltipSubsystem::BuildTooltip(const FAsdaItemInstance& It, bool bNoTitleLine, bool bNotBound) const
{
	FAsdaItemTooltipText Out;
	if (It.IsEmpty()) { return Out; }
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || !IT->GetItem(It.ItemCode)) { return BuildTooltipByCode(It.ItemCode); }

	TArray<FString> Sec;
	BuildSections(It, bNoTitleLine, bNotBound, Sec, Out.Title, Out.TitleColor);
	if (Sec.Num() != 18) { return Out; }
	// Тот же цвет в sRGB-ARGB — его ждёт плашка (`CControl_SetField923(titleControl, DropColor)`).
	if (const FAsdaItemRecord* Rec = IT->GetItem(It.ItemCode)) { Out.TitleArgb = GradeColorArgb(Rec->ItemGrade); }

	// ЖЁСТКИЙ порядок склейки — массив `sectionOrder` эталона (item_inventory.c:62655-62674).
	static const int32 kOrder[18] = { 11, 17, 5, 0, 1, 2, 3, 4, 7, 8, 9, 10, 6, 12, 13, 14, 15, 16 };
	for (int32 i : kOrder) { Out.Body += Sec[i]; }
	Out.bValid = true;
	return Out;
}

// `itemTooltipBuildByCode@0xEA6B80`: заголовок = имя предмета, тело = описание (Item_tooltip.BIN).
FAsdaItemTooltipText UAsdaItemTooltipSubsystem::BuildTooltipByCode(int32 ItemCode) const
{
	FAsdaItemTooltipText Out;
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return Out; }
	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	if (!R) { return Out; }
	FAsdaItemTooltipRow Tip;
	IT->GetItemTooltip(ItemCode, Tip);
	Out.Title = IT->GetItemName(ItemCode);
	Out.TitleArgb  = GradeColorArgb(R->ItemGrade);
	Out.TitleColor = AsdaUi::SrgbU32ToLinear(Out.TitleArgb);
	Out.Body  = Tip.Description;
	Out.bValid = true;
	return Out;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ВЕТКА ЗАПИСЕЙ ВТОРОЙ ТАБЛИЦЫ (`special_itemlist`, коды 1…558) в `ItemTooltip_Build@0xEA5260`
//  Эталон: item_inventory.c:60430 (`if (код < g_Table2CodeFirst || код > g_Table2CodeLast)`) —
//  ветка ELSE, строки 60548-60697. `Item_BuildTooltipText` тут НЕ вызывается.
//  Состав (порядок строк 1:1):
//    заголовок          `table2Text->name`, цвет = GetDropColor(dropColorIndex)
//    строка «Вес»       нотис 6787 + `LOWORD(requiredLevel)` (+0x44 записи)
//    строка «Тип»       нотис 1420 + (1421 + dropColorIndex)              [в составе priceLine]
//    строка «Уровень»   нотис 2330 + itemLevel + 2331, красная при нехватке [в составе priceLine]
//    subType == 15      + строка состояния (8401 / 8503 / 8504 / 8508)
//    subType != 15      + «Количество использований : N счет» при `i32(запись+9) > 0`
//    subType 204/206 и заточка != 0 → вместо этого «Уровень +N» / «Сила улучшения +N%»
//    описание           `table2Text->description`
//  Ширины: clamp(текст, 90, 218) + 38; строк = перенос + 2 (60677-60693).
// ════════════════════════════════════════════════════════════════════════════════════════════════
FAsdaItemTooltipText UAsdaItemTooltipSubsystem::BuildTooltipTable2(const FAsdaItemInstance& It,
                                                                   FAsdaTipMetrics& OutM) const
{
	FAsdaItemTooltipText Out;
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT) { return Out; }

	FAsdaSpecialItemlistRow Sp;
	FAsdaSpecialitemTooltiplistRow Tip;
	// `if (!Table2Record || !table2Text) return;` — эталон молча не показывает ничего.
	if (!IT->GetSpecialItem(It.ItemCode, Sp) || !IT->GetSpecialItemTooltip(It.ItemCode, Tip))
	{
		return Out;
	}

	// ★ Д5 (КРУГ 5, доработка). ДЫРА В ДАННЫХ КЛИЕНТА, а не в резолве: у части кодов
	//   `Special_ItemList.BIN` имя — один байт 0x2D («-»), и подсказка тоже «-». Проверено по сырым
	//   байтам записи (`DataHex`, смещение +4): 461 и 462 → `2d 00 00 …`, тогда как 500 →
	//   `d1 e2 e8 f2 ee ea …` = «Свиток ремонта брони (30)». То есть сервер оперирует кодами, для
	//   которых в клиентских таблицах AR содержимого нет. Пишем это в лог, чтобы «безымянный»
	//   тултип не принимали за сбой резолва.
	if (Tip.Name.IsEmpty() || Tip.Name == TEXT("-"))
	{
		UE_LOG(LogAsdaItemTip, Warning,
			TEXT("[тултип] спец-предмет %d: в SpecialItem_TooltipList имя и описание = «-» (дыра в данных клиента, не резолв)"),
			It.ItemCode);
	}

	const int32 PlayerLv = SelfLevel();
	Out.Title      = Tip.Name;
	Out.TitleArgb  = GradeColorArgb(Sp.DropColorIndex);
	Out.TitleColor = AsdaUi::SrgbU32ToLinear(Out.TitleArgb);

	// categoryLine = «Тип : <грейд>» (60567-60569).
	const FString CategoryLine = FString::Printf(TEXT("%s: %s"),
		*LocRaw(kLocGradeLabel), *LocRaw(kLocGradeNameBase + Sp.DropColorIndex));
	// priceLine = categoryLine + «\nУровень : N минимум» (красная, если уровня не хватает).
	const FString LevelLine = FString::Printf(TEXT("%s : %d %s"),
		*LocRaw(kLocLevelLabel), Sp.ItemLevel, *LocRaw(kLocLevelSuffix));
	const FString PriceLine = FString::Printf(TEXT("%s\n%s"), *CategoryLine,
		(PlayerLv < Sp.ItemLevel) ? *Colored(kColRequire, LevelLine) : *LevelLine);

	const FString Desc    = Tip.Tooltip;
	const int32   Weight  = Sp.RequiredLevel;      // ⚠ поле +0x44 = ВЕС (нотис 6787), см. шапку
	const int32   UseCnt  = RawI32(It, 9);
	const int32   Enhance = RawU8(It, 24);

	if (Sp.SubType == 15)
	{
		// Строка состояния (60576-60610). ★ Ветка `default` буфер НЕ пишет — строка пустая.
		FString StateLine;
		const int32 Flag124 = Sp.Flag124;
		if (RawU8(It, 50) != 0 || Flag124 == 0xA) { StateLine = Colored(kColBound, LocRaw(kLocNoTrade)); }
		else if (Flag124 == 1)                    { StateLine = Colored(kColBound, LocRaw(kLocNoTradeEquip)); }
		else if (Flag124 == 2)                    { StateLine = Colored(kColBound, LocRaw(kLocNoTradeUse)); }
		else if (Flag124 == 3)                    { StateLine = Colored(kColBound, LocRaw(kLocNoTradeUse2)); }
		Out.Body = FString::Printf(TEXT("\n%s : %d\n%s\n\n%s\n\n%s"),
			*LocRaw(kLocWeightSpecial), Weight, *PriceLine, *StateLine, *Desc);
	}
	else
	{
		if (UseCnt <= 0)
		{
			Out.Body = FString::Printf(TEXT("%s : %d\n%s\n\n%s"),
				*LocRaw(kLocWeightSpecial), Weight, *PriceLine, *Desc);
		}
		else
		{
			Out.Body = FString::Printf(TEXT("\n%s : %d\n%s\n\n%s : %d %s\n\n%s"),
				*LocRaw(kLocWeightSpecial), Weight, *PriceLine,
				*LocRaw(kLocUseCountSpecial), UseCnt, *LocRaw(kLocUseCountTail), *Desc);
		}
		// «Зарядные» спец-предметы 204/206 с заточкой: тело ПЕРЕСОБИРАЕТСЯ целиком (60651-60672).
		if ((Sp.SubType == 204 || Sp.SubType == 206) && Enhance != 0)
		{
			// Эталонная лесенка цвета (60654-60661). ★ Две последние строки — обе `>= 0xC`,
			//   вторая перекрывает первую: 0xFFFF96DC недостижим. Дефект оригинала, оставлен 1:1.
			uint32 EC = 0xFFFFFF00;
			if (Enhance <= 5)                    { EC = 0xFFFFFFFF; }
			if (Enhance >= 6 && Enhance <= 8)    { EC = 0xFFFFFF46; }
			if (Enhance >= 0xC)                  { EC = 0xFF96FF32; }
			const FString EnhLvl = FString::Printf(TEXT("<FC=%08X> +%d</F>"), EC, Enhance);
			const FString EnhPct = FString::Printf(TEXT("<FC=%08X> +%d%%</F>"), EC, RawI16(It, 27));
			// ★ Формат эталона — БЕЗ « : » между нотисом 6787 и числом: `"%s%d\n…"`.
			Out.Body = FString::Printf(TEXT("%s%d\n%s\n%s%s\n%s%s\n\n%s"),
				*LocRaw(kLocWeightSpecial), Weight, *PriceLine,
				*LocRaw(kLocEnhLevelLblSpecial), *EnhLvl,
				*LocRaw(kLocEnhPowerLblSpecial), *EnhPct, *Desc);
		}
	}

	// Ширина/строки — как у shell-предмета: clamp(текст, 90, 218) + 38, строк = перенос + 2.
	OutM = FAsdaTipMetrics();
	OutM.FixedWidth = 0.f;
	OutM.WidthAdd   = 38.f;
	OutM.ExtraLines = 2;
	// `if (Table2Record->flag_106[0] == 2)` → `itemTipWinSetModeVisible(mgr, режим, 1)` (60558-60563):
	//   у спец-предмета «элитный» подвал включает флаг +106, а не `categoryFlag == 3`.
	OutM.bPremiumFooter = (SpecialRawByte(Sp.DataHex, 106) == 2);
	OutM.FooterText = OutM.bPremiumFooter ? LocRaw(kLocEliteFooter) : FString();

	Out.bValid = true;
	return Out;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ПРОСТОЙ ТУЛТИП ПО ТИПУ ПРЕДМЕТА — `CItem_BuildSowelTooltipText@0xEA2EC0` (item_inventory.c:59497)
//
//  ★★★ ЭТО ОТДЕЛЬНЫЙ ПАЙПЛАЙН, А НЕ ВАРИАНТ ОФОРМЛЕНИЯ. В него ведёт
//      `CControl_SetState@0xEA1D30` → `tooltipBuildByTypeAndShow@0xEA2590` (ui_core.c:82739):
//      `kind` 0/1 → сюда, 4 → квест-эпизод, 5 → `ItemTooltip_BuildItemInfo`, 7 → «количество».
//      Тут НЕТ ни «Эффекта ЖК», ни гнёзд, ни «Доступно улучшений», ни «Прочности» — их печатает
//      ТОЛЬКО полный `Item_BuildTooltipText`, и только для вкладки «Снаряжение»/надетого.
//
//  Состав по `ItemRecord->itemType` (эталон 60013-60250):
//   код 20551 (золото) : «<N с запятыми> Золото»,                             ширина 116
//   10, subType 204    : «Вес : W» + «Запас наживки : Q» + описание,          ширина 218
//   10, иначе          : «Вес : W» + «Количество стрел : Q»,                  ширина 218
//   11 (расходники)    : имя «(Q)» при tradableFlag; «Вес : Q*W» + описание;
//                        у нетоварного «Вес : W» + описание;
//                        у нетоварного subType 32 при a4 == 0 ещё и «не передаётся» (8401);
//                        ширина clamp(max(имя, описание), 90, 218) + 38
//   5, 6, 7            : «Уровень : N минимум» (красный при нехватке) + «Вес : W»
//                        + строка эффекта (нотис linkCode+8587, при linkedGroupCode>0 с числом)
//                        + «не передаётся» (8401) при tooltipFlag200 + описание, ширина 220
//   прочие типы        : ТОЛЬКО заголовок, тело пустое (эталон в `Str` ничего не пишет)
//   записи 2-й таблицы : «Вес : requiredLevel» + описание,                    ширина 220
//
//  ⚠ САМОЦВЕТЫ (код в диапазоне sowel) идут В ЭТУ ЖЕ функцию, но в её ПЕРВУЮ половину
//    (59623-59989) — отдельный состав из 8 строк. Она НЕ портирована: полю `tooltipParam`
//    (строка «Вес» самоцвета) в `sowell_list.bin` не сопоставлена колонка, а угадывать смещение
//    запрещено. Для самоцвета возвращаем «не собрано» и пишем предупреждение в лог.
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ══════════════════ ТУЛТИП САМОЦВЕТА (`CItem_BuildSowelTooltipText`, первая половина) ══════════
// Эталон: item_inventory.c:59782-59995. Порядок строк тела — ровно эталонный `sprintf(Str, …)`:
//   v180 «Профессия : <8564>»            (нотисы 9563 + 8564)
//   v136 «Доступное оружие : a, b, …»    (нотис 9573 + 9455+i по битам маски +0x44, ТОЛЬКО если бит есть)
//   v173 «Тип : <грейд>» + «\n» + «Уровень : N минимум»   (v152 склеен ВНУТРЬ v173, 1420/1421+idx, 2330/2331)
//   v170 «Вид : <8584|8585>»             (нотис 9564, ветка по `specialFlag` +0x2C)
//   v164 «Место в снаряжении : …»        (нотис 2299 + `CItemManager_GetSowelOptionText`)
//   v167 «Вес : N»                       (нотис 2295 + `tooltipParam` = БАЙТ +38)
//   v198 «Владелец <имя бонуса> N[%]»    (нотис 6832 + 8510+bonusType; %d% при bonusType == 3)
//   [8401 жёлтым]                        только при байте +66 != 0
//   описание                             `sowell_tooltip.Text`
//   блок «следующий грейд» (код+1)       только при `GradeReq != 0 && != 7`
// Ширина 220 (`charEquipTableA = 220`), перенос по 206 = 220 − 14, строк = перенос + 2.
FAsdaItemTooltipText UAsdaItemTooltipSubsystem::BuildTooltipSowel(int32 ItemCode, FAsdaTipMetrics& OutM) const
{
	FAsdaItemTooltipText Out;
	OutM = FAsdaTipMetrics();
	OutM.ExtraLines = 2;
	OutM.FixedWidth = 220.f;      // `pThis->charEquipTableA = (void *)220`
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT) { return Out; }

	FAsdaSowellRow S;
	FAsdaSowellTooltipRow Txt;
	// `if (!sowelRecord || !sowelTextRecord) return;` — эталон молча выходит (59787).
	if (!IT->GetSowell(ItemCode, S) || !IT->GetSowellTooltip(ItemCode, Txt)) { return Out; }

	// Заголовок: имя из `sowell_list`, цвет = `GetDropColor(gradeColorIndex)` (байт +0xA9).
	Out.Title      = S.Name;
	Out.TitleArgb  = GradeColorArgb(S.GradeColorIndex);
	Out.TitleColor = AsdaUi::SrgbU32ToLinear(Out.TitleArgb);

	// ── «Профессия» ────────────────────────────────────────────────────────────────────────────
	// ★ ДЕФЕКТ ОРИГИНАЛА (исправлен, закон `fix-original-defects`). Эталон СЧИТАЕТ нужный нотис —
	//   `uiNotice_SetLocalizedById(partGrade)` = 8564 + `partGrade` (ui_core.c:83212) — кладёт его
	//   в буфер `Buffer` и БОЛЬШЕ НИГДЕ ЕГО НЕ ИСПОЛЬЗУЕТ: в строку идёт литерал 8564
	//   (asm EA31DC…EA3202: `movzx ecx,[eax+0x25]` → вызов, затем `push 0x2174` = 8564).
	//   Из-за этого у ЛЮБОГО камня печаталось «Профессия : Для всех», хотя есть 8565 «Не ниже
	//   второй» и 8566 «Не ниже третьей». Печатаем вычисленный нотис; вернуть 1:1 —
	//   `asda.ItemTip.LegacySowelJobLine 1`.
	const int32 JobId = (CVarLegacySowelJobLine.GetValueOnGameThread() != 0)
		? kLocSowelJobBase : (kLocSowelJobBase + S.PartGrade);
	const FString JobLine = FString::Printf(TEXT("%s : %s"), *LocRaw(kLocSowelJobLabel), *LocRaw(JobId));

	// ── «Доступное оружие» по 19-битной маске `OptionMask` (+0x44) ──────────────────────────────
	FString OptLine;
	{
		int32 Cnt = 0;
		for (int32 i = 0; i < 19; ++i)
		{
			if (((S.OptionMask >> i) & 1) == 0) { continue; }
			const FString Name = LocRaw(kLocSowelWeaponBase + i);
			if (Cnt == 0) { OptLine = FString::Printf(TEXT("%s : %s"), *LocRaw(kLocSowelWeaponsLabel), *Name); }
			else          { OptLine += FString::Printf(TEXT(", %s"), *Name); }
			++Cnt;
		}
	}

	// ── «Тип» + «Уровень» (эталон склеивает их в ОДИН буфер v173) ───────────────────────────────
	const FString GradeLine = FString::Printf(TEXT("%s : %s"),
		*LocRaw(kLocGradeLabel), *LocRaw(kLocGradeNameBase + S.GradeColorIndex));
	const int32   PlayerLv  = SelfLevel();
	const FString LvlRaw    = FString::Printf(TEXT("%s : %d %s"),
		*LocRaw(kLocLevelLabel), S.RequiredLevel, *LocRaw(kLocLevelSuffix));
	// ★ Здесь эталон красит НЕ `colorReqLevelFail`, а `colorRed = -65536` = 0xFFFF0000 (59834).
	const FString LvlLine   = (PlayerLv < S.RequiredLevel) ? Colored(kColDurZero, LvlRaw) : LvlRaw;
	const FString TypeBlock = GradeLine + TEXT("\n") + LvlLine;

	// ── «Вид» (8584 «Живой камень» / 8585 «Составной ЖК») ───────────────────────────────────────
	const FString KindLine = FString::Printf(TEXT("%s : %s"),
		*LocRaw(kLocKindLabel), *LocRaw(S.SpecialFlag ? kLocSowelKindCombo : kLocSowelKindPlain));

	// ── «Место в снаряжении» (`CItemManager_GetSowelOptionText@0xEB0CD0`) ───────────────────────
	//   Три байта +45/+46/+47; каждый берётся, только если <= 0x0E, и даёт нотис 8569 + N.
	//   ★ Склейка эталона: три непустых → «a, b, c»; ровно два → «a, b»; иначе — только первый
	//     (третий БЕЗ второго теряется — дефект оригинала, воспроизведён 1:1).
	FString PlaceValue;
	{
		auto OptText = [&](int32 Raw) -> FString
		{
			return (Raw >= 0 && Raw <= 0x0E) ? LocRaw(kLocSowelSocketBase + Raw) : FString();
		};
		const FString A = OptText(S.SocketTargetType);
		const FString B = OptText(S.SocketTargetType2);
		const FString C = OptText(UAsdaItemTableSubsystem::SowelRawByte(S, 47));
		if (!A.IsEmpty() && !B.IsEmpty() && !C.IsEmpty()) { PlaceValue = A + TEXT(", ") + B + TEXT(", ") + C; }
		else if (!A.IsEmpty() && !B.IsEmpty())            { PlaceValue = A + TEXT(", ") + B; }
		else if (!A.IsEmpty())                            { PlaceValue = A; }
	}
	const FString PlaceLine = FString::Printf(TEXT("%s : %s"), *LocRaw(kLocSowelPlaceLabel), *PlaceValue);

	// ── «Вес» — `sowelRecord->tooltipParam` = БАЙТ +38 сырой записи ─────────────────────────────
	const int32   WeightRaw  = UAsdaItemTableSubsystem::SowelRawByte(S, 38);
	const FString WeightLine = FString::Printf(TEXT("%s : %d"), *LocRaw(kLocWeight), FMath::Max(WeightRaw, 0));

	// ── строка владельца: «6832 <имя бонуса> N» (или «N%» при bonusType == 3) ───────────────────
	auto OwnerLineFor = [this](int32 BonusType, int32 BonusValue) -> FString
	{
		const FString BonusName = LocRaw(kLocSowelBonusBase + BonusType);
		const FString Owner     = LocRaw(kLocSowelOwner);
		// bonusType 0x1A..0x1D — БЕЗ числа (эталон 59891-59896).
		if (BonusType >= 0x1A && BonusType <= 0x1D)
		{
			return Colored(kColSowelGrade, FString::Printf(TEXT("%s %s"), *Owner, *BonusName));
		}
		return Colored(kColSowelGrade, (BonusType == 3)
			? FString::Printf(TEXT("%s %s %d%%"), *Owner, *BonusName, BonusValue)
			: FString::Printf(TEXT("%s %s %d"),  *Owner, *BonusName, BonusValue));
	};
	const FString OwnerLine = OwnerLineFor(S.BonusStatType, S.BonusValue);

	// ── склейка тела (эталон 59898-59943): 4 варианта по «есть флаг +66» × «есть строка опций» ──
	const bool bNoTrade = (UAsdaItemTableSubsystem::SowelRawByte(S, 66) > 0);
	const bool bHasOpt  = !OptLine.IsEmpty();
	FString Body;
	{
		FString Head = JobLine;
		if (bHasOpt) { Head += TEXT("\n") + OptLine; }
		Head += TEXT("\n") + TypeBlock + TEXT("\n") + KindLine + TEXT("\n") + PlaceLine + TEXT("\n") + WeightLine;
		// ★ У эталона `v198` (строка владельца) заполняется ВСЕГДА, поэтому ветка `else` проверки
		//   `if (v198[0])` недостижима; воспроизводим достижимую.
		Body = Head + TEXT("\n\n") + OwnerLine;
		if (bNoTrade) { Body += FString::Printf(TEXT("\n\n<FC=FFFFFF00>%s</F>"), *LocRaw(kLocNoTrade)); }
		Body += TEXT("\n\n") + Txt.Text;
	}

	// ── блок «следующий грейд» (эталон 59944-59991): камень с кодом +1 ──────────────────────────
	if (S.GradeReq != 0 && S.GradeReq != 7)
	{
		FAsdaSowellRow N;
		if (IT->GetSowell(ItemCode + 1, N))
		{
			const FString NextName = Colored(0xFFFFFFFF, N.Name);
			const FString NextLvlRaw = FString::Printf(TEXT("%s : %d %s"),
				*LocRaw(kLocLevelLabel), N.RequiredLevel, *LocRaw(kLocLevelSuffix));
			// `colorNextReqLevel = -65536` (0xFFFF0000) при нехватке, иначе `colorReqLevelOk = -129` (0xFFFFFF7F).
			const FString NextLvl = Colored((PlayerLv < N.RequiredLevel) ? 0xFFFF0000u : 0xFFFFFF7Fu, NextLvlRaw);
			// ★ Эталон здесь берёт `gradeStrId` ТЕКУЩЕГО камня (он не пересчитывается), а значение —
			//   у СЛЕДУЮЩЕГО, и ветвится по `nextSowelRecord->bonusType`. Воспроизведено 1:1.
			const FString NextOwnerName = LocRaw(kLocSowelBonusBase + S.BonusStatType);
			const FString Owner         = LocRaw(kLocSowelOwner);
			FString NextOwner;
			if (N.BonusStatType >= 0x1A && N.BonusStatType <= 0x1D)
			{
				NextOwner = Colored(kColSowelGrade, FString::Printf(TEXT("%s %s"), *Owner, *NextOwnerName));
			}
			else
			{
				NextOwner = Colored(kColSowelGrade, (N.BonusStatType == 3)
					? FString::Printf(TEXT("%s %s %d%%"), *Owner, *NextOwnerName, N.BonusValue)
					: FString::Printf(TEXT("%s %s %d"),  *Owner, *NextOwnerName, N.BonusValue));
			}
			Body += TEXT("\n\n") + NextName + TEXT("\n") + NextLvl + TEXT("\n") + NextOwner;
		}
	}

	Out.Body   = Body;
	Out.bValid = true;
	return Out;
}

FAsdaItemTooltipText UAsdaItemTooltipSubsystem::BuildTooltipSimple(int32 ItemCode, int32 Quantity,
                                                                   int32 BindArg,
                                                                   FAsdaTipMetrics& OutM) const
{
	FAsdaItemTooltipText Out;
	OutM = FAsdaTipMetrics();
	OutM.ExtraLines = 2;
	UAsdaItemTableSubsystem* IT = Items();
	if (!IT || ItemCode <= 0) { return Out; }

	// ★★★ САМОЦВЕТ — ПЕРВАЯ проверка того же эталонного билдера (item_inventory.c:59782):
	//   `if (itemCode >= g_SowelCodeRangeLo && itemCode <= g_SowelCodeRangeHi) { … return; }`
	if (IT->IsSowelCode(ItemCode)) { return BuildTooltipSowel(ItemCode, OutM); }

	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	FAsdaSpecialItemlistRow Sp;
	const bool bSpecial = IT->GetSpecialItem(ItemCode, Sp);
	// `if (itemRecord || itemTable2Record)` — иначе эталон не показывает ничего (60249).
	if (!R && !bSpecial) { return Out; }

	FAsdaItemTooltipRow TipRow;
	const FString Desc = (R && IT->GetItemTooltip(ItemCode, TipRow)) ? TipRow.Description : FString();

	if (R)
	{
		Out.Title = IT->GetItemName(ItemCode);
		if (ItemCode == 20551)
		{
			// Золото: «<N> Золото», ширина 116 (60018-60027).
			Out.Body = FString::Printf(TEXT("%s %s"),
				*AsdaUi::FormatThousands(Quantity), *LocRaw(kLocGold));
			OutM.FixedWidth = 116.f;
		}
		else
		{
			switch (R->ItemType)
			{
			case 10:
				if (R->ItemSubType == 204)
				{
					Out.Body = FString::Printf(TEXT("%s : %d\n%s : %d\n%s"),
						*LocRaw(kLocWeight), R->Weight, *LocRaw(kLocBaitStock), Quantity, *Desc);
				}
				else
				{
					Out.Body = FString::Printf(TEXT("%s : %d\n%s : %d"),
						*LocRaw(kLocWeight), R->Weight, *LocRaw(kLocArrowCount), Quantity);
				}
				OutM.FixedWidth = 218.f;
				break;

			case 11:
				if (R->TradableFlag != 0)
				{
					// Имя со счётчиком стопки (60061-60071) и вес ВСЕЙ стопки.
					if (Quantity != 0)
					{
						Out.Title = FString::Printf(TEXT("%s (%d)"), *Out.Title, Quantity);
					}
					Out.Body = FString::Printf(TEXT("%s : %d\n\n%s"),
						*LocRaw(kLocWeight), Quantity * R->Weight, *Desc);
				}
				else if (R->ItemSubType != 32 || BindArg != 0)
				{
					Out.Body = FString::Printf(TEXT("%s : %d\n\n%s"), *LocRaw(kLocWeight), R->Weight, *Desc);
				}
				else
				{
					Out.Body = FString::Printf(TEXT("%s : %d\n\n%s\n\n%s"),
						*LocRaw(kLocWeight), R->Weight, *LocRaw(kLocNoTrade), *Desc);
				}
				OutM.FixedWidth = 0.f;      // clamp(текст, 90, 218) + 38
				OutM.WidthAdd   = 38.f;
				// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026: минимальный уровень первой строкой (формат и цвет — как у ветки 5/6/7 ниже).
				if (CVarTipLevelLine.GetValueOnGameThread() != 0 && R->RequiredLevel > 1)
				{
					const FString LevelLine = FString::Printf(TEXT("%s : %d %s"),
						*LocRaw(kLocLevelLabel), R->RequiredLevel, *LocRaw(kLocLevelSuffix));
					const int32 MyLv = SelfLevel();
					Out.Body = ((MyLv > 0 && MyLv < R->RequiredLevel) ? Colored(kColSimpleReqBad, LevelLine) : LevelLine)
						+ TEXT("\n") + Out.Body;   // высоту считает перенос текста (`строк = перенос + ExtraLines`) — запас не трогаем
				}
				break;

			case 5:
			case 6:
			case 7:
			{
				const FString LevelLine = FString::Printf(TEXT("%s : %d %s"),
					*LocRaw(kLocLevelLabel), R->RequiredLevel, *LocRaw(kLocLevelSuffix));
				const FString Lvl = (SelfLevel() < R->RequiredLevel)
					? Colored(kColSimpleReqBad, LevelLine) : LevelLine;
				const FString WeightLine = FString::Printf(TEXT("%s : %d"), *LocRaw(kLocWeight), R->Weight);
				const FString Bound = (R->TooltipFlag200 != 0) ? LocRaw(kLocNoTrade) : FString();
				if (R->LinkCode <= 0)
				{
					Out.Body = (R->TooltipFlag200 != 0)
						? FString::Printf(TEXT("%s\n%s\n\n%s\n\n%s"), *Lvl, *WeightLine, *Bound, *Desc)
						: FString::Printf(TEXT("%s\n%s\n\n%s"), *Lvl, *WeightLine, *Desc);
				}
				else
				{
					// `sub_922EFC(linkCode)` = нотис (linkCode + 8587).
					const FString LinkText = LocRaw(R->LinkCode + kLocLinkCodeBase);
					const FString Eff = (R->LinkedGroupCode <= 0)
						? Colored(kColSimpleEffect, LinkText)
						: Colored(kColSimpleEffect, FString::Printf(TEXT("%s %d"), *LinkText, R->LinkedGroupCode));
					Out.Body = (R->TooltipFlag200 != 0)
						? FString::Printf(TEXT("%s\n%s\n%s\n\n%s\n\n%s"), *Lvl, *WeightLine, *Eff, *Bound, *Desc)
						: FString::Printf(TEXT("%s\n%s\n%s\n\n%s"), *Lvl, *WeightLine, *Eff, *Desc);
				}
				OutM.FixedWidth = 220.f;
				break;
			}

			default:
				// ★ Эталон для прочих типов в тело НЕ пишет НИЧЕГО (в `Str` остаётся пусто), а ширину
				//   берёт из поля объекта, оставшегося от ПРЕДЫДУЩЕГО тултипа (`charEquipTableA`) —
				//   дефект оригинала, воспроизводить нечего: считаем ширину по тексту.
				Out.Body.Reset();
				OutM.FixedWidth = 0.f;
				OutM.WidthAdd   = 38.f;
				break;
			}
		}
		// ★ В конце ветки эталон БЕЗУСЛОВНО ставит заголовку цвет грейда (60250), перекрывая и
		//   белый у золота/типа 10, и `colorB` у типа 11 — те присвоения мертвы. Оставлено 1:1;
		//   `kColSimpleBind` держим объявленным, чтобы разбор не потерялся.
		(void)kColSimpleBind;
		Out.TitleArgb  = GradeColorArgb(R->ItemGrade);
		Out.TitleColor = AsdaUi::SrgbU32ToLinear(Out.TitleArgb);
	}
	else
	{
		// Запись ВТОРОЙ таблицы (60252-60274): имя+описание из `SpecialItem_TooltipList`, «Вес».
		//   ⚠ Эталон ищет строку подсказки ПО ИМЕНИ (`itemMgrFindTooltipByName@0xEC7420`), а мы —
		//   арифметикой по коду (`itemGetTooltipById@0xEC73B0`, та же таблица). Отличие видно
		//   только при дублях имён; порядок и содержимое строк идентичны.
		FAsdaSpecialitemTooltiplistRow Tip;
		if (!IT->GetSpecialItemTooltip(ItemCode, Tip)) { return Out; }
		Out.Title      = Tip.Name;
		Out.TitleArgb  = GradeColorArgb(Sp.DropColorIndex);
		Out.TitleColor = AsdaUi::SrgbU32ToLinear(Out.TitleArgb);
		Out.Body       = FString::Printf(TEXT("%s : %d\n\n%s"),
			*LocRaw(kLocWeight), Sp.RequiredLevel, *Tip.Tooltip);
		OutM.FixedWidth = 220.f;
	}

	Out.bValid = true;
	return Out;
}

// ★ [ИСПРАВЛЕНИЕ] баг 11: включён ли для записи путь «премиум не-снаряжение» (ключ `asda.ItemTip.BagNonGearSimple`; при откате
//   `asda.ItemTip.LegacyOnePipeline 1` всё идёт одним полным билдером — и этот путь тоже выключен, как в `ShowItemTooltip`).
bool UAsdaItemTooltipSubsystem::UsesBagNonGearTip(const FAsdaItemRecord* R) const
{
	return CVarLegacyOnePipeline.GetValueOnGameThread() == 0 && AsdaBagNonGearUsesSimple(R);
}

// ★ [ИСПРАВЛЕНИЕ] баг 11, r2 (ревизия 01.11): подсказка премиум не-снаряжения, показанного полным путём.
//   Основа — простой билдер типа 11 (`CItem_BuildSowelTooltipText@0xEA2EC0`, item_inventory.c:60074-60110): имя «(Q)» у товарного,
//   «Вес» (Q·W у товарного), описание; строки «Грейд» у эталона в этой ветке НЕТ — не печатаем. Из полного билдера
//   (`Item_BuildTooltipText`, item_inventory.c:62254, порт `BuildSections`) взяты две строки, без которых предметы теряются:
//     • «Профессия : <класс>» (секция 0; красная, если класс недоступен) — у 6385 из 8143 затронутых класс конкретный
//       (UsageCategory 1/12/23), 1241 группа различается только классом (76318/76319/76320). При UsageCategory 0 «Новичок»
//       и 40 «1-я работа» (общие — на любой класс) строку не печатаем;
//     • привязка (секция 14): 8401 «не передаётся» при байте +50 ≠ 0, иначе 8503 «не передаётся после экипировки» при
//       TooltipFlag200 == 1 (7910 предметов). Покрывает и эталонный особый случай подтипа 32 (8401 при a4 == 0, т. е. +50 == 3).
//   Подвал премиума 109832 (`itemTipWinSetModeVisible(mgr, 0, 1)` полного пути у categoryFlag 3) сохраняется.
//   Тип 10 — состав простого билдера как есть (у типа 10 с категорией 3 записей в item_list нет).
FAsdaItemTooltipText UAsdaItemTooltipSubsystem::BuildTooltipBagNonGear(const FAsdaItemInstance& It, FAsdaTipMetrics& OutM) const
{
	UAsdaItemTableSubsystem* IT = Items();
	const FAsdaItemRecord* R = IT ? IT->GetItem(It.ItemCode) : nullptr;
	const int32 BindByte = RawU8(It, 50);
	const int32 Quantity = RawI32(It, 9);
	// Эталонный `a4` простого билдера вне 5..7 (`invSlotBuildTooltip`, 37447-37457): 0 у привязанного подтипа 32, иначе −1.
	const int32 BindArg = (R && BindByte == 3 && R->ItemSubType == 32) ? 0 : -1;
	FAsdaItemTooltipText Out = BuildTooltipSimple(It.ItemCode, Quantity, BindArg, OutM);
	if (!Out.bValid || !R) { return Out; }

	if (R->ItemType == 11)
	{
		FAsdaItemTooltipRow TipRow;
		const FString Desc = IT->GetItemTooltip(It.ItemCode, TipRow) ? TipRow.Description : FString();
		const int32 Weight = (R->TradableFlag != 0) ? Quantity * R->Weight : R->Weight;
		FString Body;
		// «Профессия» — формат и цвет секции 0 полного билдера.
		if (R->UsageCategory != 0 && R->UsageCategory != 40)
		{
			const FString Line = FString::Printf(TEXT("%s : %s"), *LocRaw(kLocClassLabel), *LocRaw(kLocClassNameBase + R->UsageCategory));
			Body += (IsUsageCategoryReachable(R->UsageCategory) ? Line : Colored(kColRequire, Line)) + TEXT("\n");
		}
		// «Уровень» — как в простом билдере типа 11 (`asda.ItemTip.LevelLine`).
		if (CVarTipLevelLine.GetValueOnGameThread() != 0 && R->RequiredLevel > 1)
		{
			const FString LevelLine = FString::Printf(TEXT("%s : %d %s"), *LocRaw(kLocLevelLabel), R->RequiredLevel, *LocRaw(kLocLevelSuffix));
			const int32 MyLv = SelfLevel();
			Body += ((MyLv > 0 && MyLv < R->RequiredLevel) ? Colored(kColSimpleReqBad, LevelLine) : LevelLine) + TEXT("\n");
		}
		Body += FString::Printf(TEXT("%s : %d\n\n"), *LocRaw(kLocWeight), Weight);
		// Привязка — условия и цвет секции 14 полного билдера.
		if (BindByte != 0)                { Body += Colored(kColBound, LocRaw(kLocNoTrade)) + TEXT("\n\n"); }
		else if (R->TooltipFlag200 == 1)  { Body += Colored(kColBound, LocRaw(kLocNoTradeEquip)) + TEXT("\n\n"); }
		Body += Desc;
		Out.Body = Body;
	}
	if (R->CategoryFlag == 3)
	{
		OutM.bPremiumFooter = true;
		OutM.FooterText = LocRaw(kLocEliteFooter);
	}
	return Out;
}

FString UAsdaItemTooltipSubsystem::DumpTooltip(const FAsdaItemInstance& It) const
{
	const FAsdaItemTooltipText T = BuildTooltip(It);
	return FString::Printf(TEXT("[тултип %d] заголовок «%s»\n%s"), It.ItemCode, *T.Title, *T.Body);
}
