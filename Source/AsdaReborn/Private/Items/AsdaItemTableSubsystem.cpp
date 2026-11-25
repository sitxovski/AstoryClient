// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTableSubsystem.cpp — рантайм-мост itemId→запись предмета поверх UE-ассетов DataTable.
// Порт 1:1 CItemManager (эталон AsdaStory_game.c). См. заголовок для адресов и источников данных.
// ★ Никакого чтения бинарников/CSV оригинала: всё берётся из DT_item_list / DT_item_img_table
//   и остальных DT_* по манифесту. Файлы Bin.bbz живут только в пайплайне импорта.
#include "Items/AsdaItemTableSubsystem.h"
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "Items/AsdaItemDbManifest.h"   // AsdaItemDb::GTables — список DataTable
#include "Text/AsdaLang.h"              // [ПОРТ] 09.10.2026 (мультиязычность): FAsdaLang::ResolveRow — имя предмета
#include "Text/AsdaLangRows.h"          // [ПОРТ] 09.10.2026 (мультиязычность): перевод полей КОПИЙ строк в геттерах
#include "AsdaDevConsole.h"             // ASDA_DEV_CVAR — ключи пола иконки (08.11.2026)
#include "AsdaNetSubsystem.h"           // пол своего персонажа (блок 1017) для атласа иконок
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItems, Log, All);

// ★★★ [ОРИГИНАЛ] 08.11.2026 (пачка 18, баг 1 «на броне нет женского значка»). Значка пола у предмета в эталоне НЕТ
//   (ни текстуры, ни строки loc) — у брони есть отдельная ЖЕНСКАЯ иконка в `item_img_table` (+0xAC атлас, +0xCC ячейка),
//   и эталон рисует её персонажу-женщине во всех окнах. Порт рисовал всегда мужскую. Разбор — у `GetItemIconSelf`.
static ASDA_DEV_CVAR(int32, CVarIconSelfGender,
	TEXT("asda.Item.IconSelfGender"),
	1,
	TEXT("1 = как в оригинале: иконки предметов в сумке, подсказке, панели, витринах — из атласа пола своего персонажа "
	     "(у женщины женская броня); 0 = прежний порт: всегда мужской атлас."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarIconUnisexMale,
	TEXT("asda.Item.IconUnisexMale"),
	1,
	TEXT("1 = как в оригинале: у предмета с полем пола 0 (общий) иконка всегда из мужского атласа; "
	     "0 = прежний порт: женский атлас по запросу для любого предмета."),
	ECVF_Default);

// Индекс записи = имя строки DataTable (колонка _row в CSV = порядковый номер в оригинальном файле).
// Возвращает -1, если имя строки не число (повреждённая/ручная строка).
static int32 RowNameToIndex(const FName& RowName)
{
	const FString S = RowName.ToString();
	if (S.IsEmpty() || !S.IsNumeric()) return -1;
	return FCString::Atoi(*S);
}

// Прочитать байт записи по смещению из hex-строки `DataHex` (2 символа на байт). −1 если вне строки.
// ★ D4 (аудит `code_dedup_audit_2.md`): эта пара была КАНОНОМ, по которому в шести других
//   подсистемах написали свои копии. Теперь канон живёт в `AsdaBytes` (Public/AsdaBytes.h), а здесь
//   остаётся форвард — имя `RawByteAt` продолжает читаться в формулах смещений ниже.
static FORCEINLINE int32 RawByteAt(const FString& Hex, int32 Off) { return AsdaBytes::HexByteAt(Hex, Off); }

// ★ Поля тултипа, которых нет отдельными колонками: DurabilityMax (68 = 0x44 i32),
//   Weight (72 = 0x48 u16), StatBaseHi (136 = 0x88 u16), StatBaseLo (138 = 0x8A u16).
//   ⚠ Имена полей эталона смешивают системы счисления: `field68`/`field72` — ДЕСЯТИЧНЫЕ (это
//     ручные имена), `gap_88`/`field_28` — ШЕСТНАДЦАТЕРИЧНЫЕ (авто-имена IDA). Каждое смещение
//     ниже снято с бинаря напрямую, а не выведено из имени (закон `globals-tsv-offset-shift`).
//   Битая/короткая строка ⇒ поля остаются нулями.
void UAsdaItemTableSubsystem::ReadTooltipFieldsFromRawHex(const FString& Hex, FAsdaItemRecord& Rec)
{
	// ★ D4: little-endian из hex-колонки — общая точка `AsdaBytes` (битьё/обрыв ⇒ 0, как и было).
	auto U16 = [&Hex](int32 Off) -> int32 { return AsdaBytes::HexU16At(Hex, Off); };
	auto I32 = [&Hex](int32 Off) -> int32 { return AsdaBytes::HexI32At(Hex, Off); };
	// ★★★ ИСПРАВЛЕНО 2026-08-09: имя поля эталона `itemRecord->field68` — ДЕСЯТИЧНОЕ (68 = 0x44),
	//   а порт читал `0x68` (= 104). Доказано по бинарю (capstone, `AsdaStory.exe` 13 344 768 б):
	//     EC34C4  mov eax, [ebp-0x229C]      ; itemRecord
	//     EC34CA  mov ecx, [eax + 0x44]      ; ← знаменатель строки «Прочность: %d/%d» (нотис 110308)
	//     EC34D1  movzx eax, byte ptr [edx+0xD] ; текущая прочность из записи слота
	//   Та же база `[ebp-0x229C]` рядом читает `[eax+0x84]`/`[eax+0x90]`/`[eax+0x9C]` = ItemSubType(132)
	//   /ItemVariant(144)/CategoryFlag(156) — десятичные смещения колонок `item_list` совпадают 1:1.
	//   Сверка по данным: у ЛЮБОГО оружия/брони DataHex[68..71] == 100 (или 70 у стартового),
	//   а DataHex[104..107] — посторонний код (5860, 12358, 0). Отсюда и «Прочность: 50/0».
	Rec.DurabilityMax = I32(68);
	// `gap_88[0]`/`gap_88[2]` — имена IDA ШЕСТНАДЦАТЕРИЧНЫЕ: asm EC2E6C/EC2EE8 читают
	//   `word [itemRecord+0x88]` и `word [itemRecord+0x8A]`, то есть 136/138 — это и есть 0x88/0x8A.
	Rec.StatBaseHi    = U16(0x88);
	Rec.StatBaseLo    = U16(0x8A);
	// ★ Вес единицы (`LOWORD(itemRecord->field72)`, эталон item_inventory.c:60040 и др.).
	Rec.Weight        = U16(0x48);
	// ★★★ ОТКАТ ПРИМЕНЕНИЯ (волна 1). Оба поля отдельных колонок в DT_item_list не имеют, но сырая
	//   запись 248 б лежит в `DataHex` целиком — перегенерация DataTable НЕ нужна.
	//   `i32 @0xCC` — группа отката (серверный шаблон +59), `u16 @0xD0` — длительность в мс (+60).
	//   Смещения сняты с бинаря/данных, а не из имени `gap_CC[4]` (закон `globals-tsv-offset-shift`).
	Rec.CooldownGroup = I32(0xCC);
	Rec.CooldownMs    = U16(0xD0);
	// ★★★ Группы возможных параметров (окно 50300). Обоснование смещений — у объявления полей
	//   в заголовке: дизасм `CItemTooltip_BuildOptionLines@0xB62C70` (`word [rec+0x68]` и
	//   `word [rec+0x74]`) плюс сверка по данным с колонкой `GroupId` таблицы `itemoptionrange`.
	Rec.OptionGroup0  = U16(0x68);
	Rec.OptionGroup1  = U16(0x74);
}

// Порт гейта `hotbarPushItemCooldown@0xE48FD0` — см. объявление в заголовке.
bool UAsdaItemTableSubsystem::GetItemCooldown(int32 ItemId, int32& OutGroup, int32& OutDurationMs) const
{
	OutGroup = 0;
	OutDurationMs = 0;
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) { return false; }
	// Эталон: `itemSubType == 15 || itemSubType == 16 || linkCode == 7 || 8 || 200 || 201`.
	const bool bGate = (Rec->ItemSubType == 15 || Rec->ItemSubType == 16)
		|| (Rec->LinkCode == 7 || Rec->LinkCode == 8 || Rec->LinkCode == 200 || Rec->LinkCode == 201);
	if (!bGate) { return false; }
	if (Rec->CooldownMs <= 0) { return false; }   // эталон: `if (!cooldownMs) return;`
	OutGroup      = Rec->CooldownGroup;
	OutDurationMs = Rec->CooldownMs;
	return true;
}

// ═══════════════ ★★★ КОРОБКИ/ПАКЕТЫ И ГАЧА-ПРЕДМЕТЫ (волна 2 «применение предметов») ════════════
//
// Обе таблицы адресуются ОДНИМ И ТЕМ ЖЕ полем записи предмета — `DurationOrEnchant` (+0xD8):
//   • `itemSubType ∈ {200,201}` ⇒ это индекс строки `packagetable` (состав коробки);
//   • иначе                    ⇒ это код строки `booster_list` (барабан гачи).
// Так же поступает и сервер: у него оба индекса лежат в ОДНОМ поле шаблона `+63`, и он сам
//   называет его «GaCha» (`GameServer.c:75196` — строка проверки «item_table.txt GaCha >
//   PackageItem_Table.txt.txt»).

// Строка `packagetable` по ИНДЕКСУ. Ключ строки DataTable = порядковый номер записи в файле,
//   а поле `Index` в данных ему равно (проверено: индексы сплошные 0…5227), поэтому прямой
//   `FindRow` — точный порт `charDataFindPackAgeRecord@0xDB14F0` (`base + 14*(idx − base[0].id)`).
const FAsdaPackagetableRow* UAsdaItemTableSubsystem::FindPackageRow(int32 PackageIndex) const
{
	if (PackageIndex < 0) { return nullptr; }
	const UDataTable* Dt = GetDataTable(TEXT("packagetable"));
	if (!Dt) { return nullptr; }
	static const FString Ctx(TEXT("AsdaPackageTable"));
	const FAsdaPackagetableRow* Row =
		Dt->FindRow<FAsdaPackagetableRow>(FName(*FString::FromInt(PackageIndex)), Ctx, /*bWarn*/ false);
	// Самопроверка индекса — ровно та же, что делает СЕРВЕР (`Recv_OpenPackageItem@0x55A820`,
	//   GameServer.c:133309: `pkgIdx != *(i32*)(table + 34*pkgIdx)` ⇒ отказ кодом 9).
	if (Row && Row->Index != PackageIndex)
	{
		UE_LOG(LogAsdaItems, Warning,
			TEXT("[коробки] строка packagetable %d несёт Index=%d — сервер на это отвечает кодом 9"),
			PackageIndex, Row->Index);
		return nullptr;
	}
	return Row;
}

bool UAsdaItemTableSubsystem::IsPackageItem(int32 ItemId) const
{
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) { return false; }
	if (Rec->ItemSubType != 200 && Rec->ItemSubType != 201) { return false; }
	return FindPackageRow(Rec->DurationOrEnchant) != nullptr;
}

bool UAsdaItemTableSubsystem::GetPackageContents(int32 ItemId, TArray<FAsdaPackageEntry>& OutEntries) const
{
	OutEntries.Reset();
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) { return false; }
	if (Rec->ItemSubType != 200 && Rec->ItemSubType != 201) { return false; }
	const FAsdaPackagetableRow* Row = FindPackageRow(Rec->DurationOrEnchant);
	if (!Row) { return false; }
	// ⚠ РОВНО ПЯТЬ ПОЗИЦИЙ. Колонки импортёра `Item0..Item4` / `Qty0..Qty4` соответствуют файловым
	//   парам (+4/+8 … +36/+40); шестая пара (`Item5`/`Qty5`, файловые +44/+48) НЕ выдаётся ни
	//   клиентом (загрузчик кладёт её в +44/+48, куда никто не смотрит), ни сервером (его запись
	//   34 б физически содержит только 5 пар, цикл `j < 5`, GameServer.c:70583-70592).
	const int32 Codes[5] = { Row->Item0, Row->Item1, Row->Item2, Row->Item3, Row->Item4 };
	const int32 Qtys [5] = { Row->Qty0,  Row->Qty1,  Row->Qty2,  Row->Qty3,  Row->Qty4  };
	for (int32 i = 0; i < 5; ++i)
	{
		// Пустая позиция в данных — код 0 или −1 (в таблице встречаются оба).
		if (Codes[i] <= 0) { continue; }
		FAsdaPackageEntry& E = OutEntries.AddDefaulted_GetRef();
		E.ItemCode = Codes[i];
		E.Amount   = FMath::Max(Qtys[i], 1);
	}
	return OutEntries.Num() > 0;
}

// Строка `booster_list` по КОДУ. В данных `_row == Code − 1` у всех 4134 строк, но полагаться на
//   это нельзя: эталон ищет ЛИНЕЙНО по полю +0 (`CItemManager_GetBoosterRecord@0xD1F3F0`),
//   поэтому сначала пробуем прямой ключ и СВЕРЯЕМ поле `Code`, а при расхождении честно
//   перебираем таблицу — так поведение совпадает с оригиналом при любой пересборке данных.
const FAsdaBoosterListRow* UAsdaItemTableSubsystem::FindBoosterRow(int32 BoosterCode) const
{
	if (BoosterCode <= 0) { return nullptr; }
	const UDataTable* Dt = GetDataTable(TEXT("booster_list"));
	if (!Dt) { return nullptr; }
	static const FString Ctx(TEXT("AsdaBoosterList"));
	if (const FAsdaBoosterListRow* Fast =
			Dt->FindRow<FAsdaBoosterListRow>(FName(*FString::FromInt(BoosterCode - 1)), Ctx, /*bWarn*/ false))
	{
		if (Fast->Code == BoosterCode) { return Fast; }
	}
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaBoosterListRow* R = reinterpret_cast<const FAsdaBoosterListRow*>(It.Value);
		if (R && R->Code == BoosterCode) { return R; }
	}
	return nullptr;
}

bool UAsdaItemTableSubsystem::IsBoosterItem(int32 ItemId) const
{
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) { return false; }
	// ⚠ Коробка тоже адресует `DurationOrEnchant`, но в ДРУГУЮ таблицу — отсекаем её первой,
	//   иначе индекс пакета случайно совпал бы с кодом строки барабана.
	if (Rec->ItemSubType == 200 || Rec->ItemSubType == 201) { return false; }
	return FindBoosterRow(Rec->DurationOrEnchant) != nullptr;
}

bool UAsdaItemTableSubsystem::GetBoosterPreview(int32 ItemId, TArray<int32>& OutItemCodes) const
{
	OutItemCodes.Reset();
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) { return false; }
	if (Rec->ItemSubType == 200 || Rec->ItemSubType == 201) { return false; }
	const FAsdaBoosterListRow* Row = FindBoosterRow(Rec->DurationOrEnchant);
	if (!Row) { return false; }
	// Запись 284 б = код + 35 пар `(код i32, вес i32)`; импортёр разложил коды в `Slot00..Slot34`.
	//   Веса в данных есть, но клиент оригинала их не читает — розыгрыш целиком серверный
	//   (`Recv_GachaItemUse@0x4F48C0`, накопительный проход по `GachaItemRateTable.txt`).
	const int32 Slots[35] = {
		Row->Slot00ItemCode, Row->Slot01ItemCode, Row->Slot02ItemCode, Row->Slot03ItemCode, Row->Slot04ItemCode,
		Row->Slot05ItemCode, Row->Slot06ItemCode, Row->Slot07ItemCode, Row->Slot08ItemCode, Row->Slot09ItemCode,
		Row->Slot10ItemCode, Row->Slot11ItemCode, Row->Slot12ItemCode, Row->Slot13ItemCode, Row->Slot14ItemCode,
		Row->Slot15ItemCode, Row->Slot16ItemCode, Row->Slot17ItemCode, Row->Slot18ItemCode, Row->Slot19ItemCode,
		Row->Slot20ItemCode, Row->Slot21ItemCode, Row->Slot22ItemCode, Row->Slot23ItemCode, Row->Slot24ItemCode,
		Row->Slot25ItemCode, Row->Slot26ItemCode, Row->Slot27ItemCode, Row->Slot28ItemCode, Row->Slot29ItemCode,
		Row->Slot30ItemCode, Row->Slot31ItemCode, Row->Slot32ItemCode, Row->Slot33ItemCode, Row->Slot34ItemCode,
	};
	for (int32 i = 0; i < 35; ++i)
	{
		if (Slots[i] > 0) { OutItemCodes.Add(Slots[i]); }
	}
	return OutItemCodes.Num() > 0;
}

// ★★★ ТЕ ЖЕ 35 позиций, но БЕЗ фильтрации и БЕЗ сжатия — позиция здесь значима.
//   Эталонное окно «Вид» (`boosterWindowPopulate@0xB82D90`) читает запись двумя разными циклами:
//     • ПЯТЬ «особых» призов — `boosterRecord->itemName[8*i]`,      i = 0…4   ⇒ позиции 0…4;
//     • ТРИДЦАТЬ «обычных»   — `boosterRecord->itemName[8*i + 40]`, i = 0…29  ⇒ позиции 5…34.
//   Смещение 40 = 5 пар по 8 байт, то есть это ровно продолжение того же массива пар.
//   Пустые позиции (код 0) окно рисует заглушкой, поэтому выбрасывать их нельзя.
bool UAsdaItemTableSubsystem::GetBoosterSlotsRaw(int32 ItemId, TArray<int32>& OutSlots35) const
{
	OutSlots35.Reset();
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) { return false; }
	if (Rec->ItemSubType == 200 || Rec->ItemSubType == 201) { return false; }
	const FAsdaBoosterListRow* Row = FindBoosterRow(Rec->DurationOrEnchant);
	if (!Row) { return false; }
	OutSlots35 = {
		Row->Slot00ItemCode, Row->Slot01ItemCode, Row->Slot02ItemCode, Row->Slot03ItemCode, Row->Slot04ItemCode,
		Row->Slot05ItemCode, Row->Slot06ItemCode, Row->Slot07ItemCode, Row->Slot08ItemCode, Row->Slot09ItemCode,
		Row->Slot10ItemCode, Row->Slot11ItemCode, Row->Slot12ItemCode, Row->Slot13ItemCode, Row->Slot14ItemCode,
		Row->Slot15ItemCode, Row->Slot16ItemCode, Row->Slot17ItemCode, Row->Slot18ItemCode, Row->Slot19ItemCode,
		Row->Slot20ItemCode, Row->Slot21ItemCode, Row->Slot22ItemCode, Row->Slot23ItemCode, Row->Slot24ItemCode,
		Row->Slot25ItemCode, Row->Slot26ItemCode, Row->Slot27ItemCode, Row->Slot28ItemCode, Row->Slot29ItemCode,
		Row->Slot30ItemCode, Row->Slot31ItemCode, Row->Slot32ItemCode, Row->Slot33ItemCode, Row->Slot34ItemCode,
	};
	return true;
}

bool UAsdaItemTableSubsystem::BuildItemCacheFromDataTables()
{
	const UDataTable* ListDt = GetDataTable(TEXT("item_list"));
	const UDataTable* ImgDt  = GetDataTable(TEXT("item_img_table"));
	if (!ListDt || !ImgDt)
	{
		UE_LOG(LogAsdaItems, Error,
			TEXT("[items] нет DataTable-ассетов (DT_item_list=%d DT_item_img_table=%d) — таблицы предметов не построены"),
			ListDt != nullptr, ImgDt != nullptr);
		return false;
	}

	// ── предметы ─────────────────────────────────────────────────────────────
	const TMap<FName, uint8*>& ListRows = ListDt->GetRowMap();
	Items.Reset();
	Items.SetNum(ListRows.Num());          // индекс = RowName = порядок записи в оригинале
	CodeToIndex.Reset();
	CodeToIndex.Reserve(ListRows.Num());
	int32 BadRows = 0;
	for (const TPair<FName, uint8*>& It : ListRows)
	{
		const int32 Idx = RowNameToIndex(It.Key);
		if (!Items.IsValidIndex(Idx)) { ++BadRows; continue; }
		const FAsdaItemRow* Row = reinterpret_cast<const FAsdaItemRow*>(It.Value);
		FAsdaItemRecord& Rec = Items[Idx];
		Rec.ItemCode          = Row->ItemCode;
		Rec.ItemName          = Row->ItemName;
		Rec.EnchantOrSocket   = Row->EnchantOrSocket;
		Rec.RequiredLevel     = Row->RequiredLevel;
		Rec.RequiredLevelMax  = Row->RequiredLevelMax;
		Rec.ClassRestriction  = Row->ClassRestriction;
		Rec.UsageCategory     = Row->UsageCategory;
		Rec.LinkCode          = Row->LinkCode;
		Rec.LinkedGroupCode   = Row->LinkedGroupCode;
		Rec.ItemType          = Row->ItemType;
		Rec.ItemSubType       = Row->ItemSubType;
		Rec.AttackRangeTiles  = Row->AttackRangeTiles;
		Rec.ItemVariant       = Row->ItemVariant;
		Rec.TradableFlag      = Row->TradableFlag;
		Rec.CategoryFlag      = Row->CategoryFlag;
		Rec.SubCategory       = Row->SubCategory;
		Rec.Price             = Row->Price;
		Rec.FlagWord176       = Row->FlagWord176 & 0xFFFF;
		Rec.TooltipFlag200    = Row->TooltipFlag200;
		Rec.DurationOrEnchant = Row->DurationOrEnchant;
		Rec.LinkWordCode      = Row->LinkWordCode & 0xFFFF;
		Rec.GroupCode         = Row->GroupCode;
		Rec.SetCode           = Row->SetCode;
		Rec.ItemGrade         = Row->ItemGrade;
		// ВАЖНО (эталон CItemManager_LoadItemList_BIN@0x9C64E0, стр.48376):
		//   *((_DWORD*)itemTable + 62*i + 60) = i;  — поле imgIndex(0xF0) НЕ берётся из данных
		//   (в оригинале там плейсхолдер, колонка ImgIndex таблицы хранит именно его),
		//   а ПЕРЕЗАПИСЫВАЕТСЯ порядковым индексом записи.
		//   => imgIndex == индекс строки == (itemCode - ItemCodeFirst) на сплошном участке.
		Rec.ImgIndex          = Idx;
		// ★ Поля тултипа из СЫРОЙ записи (248 б) — отдельных колонок у них нет, но `DataHex` хранит
		//   запись 1:1 от смещения 0 (DataHex[0..3] == ItemCode). Эталон: `itemRecord->field68`
		//   (Item_BuildTooltipText, item_inventory.c:62622) и `itemRecord->gap_88[0]/[2]`
		//   (там же, :62497-62501 — базы «Эффект ЖК» и множитель заточки).
		ReadTooltipFieldsFromRawHex(Row->DataHex, Rec);
		CodeToIndex.Add(Rec.ItemCode, Idx);
	}
	if (Items.Num() == 0)
	{
		UE_LOG(LogAsdaItems, Error, TEXT("[items] DT_item_list пуст"));
		return false;
	}
	ItemCodeFirst = Items[0].ItemCode;
	ItemCodeLast  = Items.Last().ItemCode;

	// ── изображения (меши/текстуры/иконки/звук дропа) ────────────────────────
	const TMap<FName, uint8*>& ImgRows = ImgDt->GetRowMap();
	ImgTable.Reset();
	ImgTable.SetNum(ImgRows.Num());
	for (const TPair<FName, uint8*>& It : ImgRows)
	{
		const int32 Idx = RowNameToIndex(It.Key);
		if (!ImgTable.IsValidIndex(Idx)) { ++BadRows; continue; }
		const FAsdaImgRow* Row = reinterpret_cast<const FAsdaImgRow*>(It.Value);
		FAsdaImgRecord& Img = ImgTable[Idx];
		Img.MeshMale        = Row->MeshMale;
		Img.TexMale         = Row->TexMale;
		Img.MeshFemale      = Row->MeshFemale;
		Img.TexFemale       = Row->TexFemale;
		Img.DropSoundCat    = Row->DropSoundCat;
		Img.ImgSubType      = Row->ImgSubType;
		Img.SubTypeMale     = Row->SubTypeMale;
		Img.SubTypeFemale   = Row->SubTypeFemale;
		Img.IconAtlasMale   = Row->IconAtlasMale;
		Img.IconCellMale    = Row->IconCellMale;
		Img.IconSmallFlag   = Row->IconSmallFlag;
		Img.IconAtlasFemale = Row->IconAtlasFemale;
		Img.IconCellFemale  = Row->IconCellFemale;
	}
	if (BadRows > 0)
	{
		UE_LOG(LogAsdaItems, Warning, TEXT("[items] строк с нечисловым ключом пропущено: %d"), BadRows);
	}
	return true;
}

void UAsdaItemTableSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ── ЕДИНСТВЕННЫЙ источник данных: UE-ассеты DataTable ────────────────────
	const double T0 = FPlatformTime::Seconds();
	const uint64 Mem0 = FPlatformMemory::GetStats().UsedPhysical;
	LoadDataTables();                       // все DT_* по манифесту + ключевые индексы
	const double T1 = FPlatformTime::Seconds();
	const uint64 Mem1 = FPlatformMemory::GetStats().UsedPhysical;
	bLoaded = BuildItemCacheFromDataTables();
	const double T2 = FPlatformTime::Seconds();
	const uint64 Mem2 = FPlatformMemory::GetStats().UsedPhysical;

	if (!bLoaded)
	{
		UE_LOG(LogAsdaItems, Error, TEXT("[items] таблицы предметов НЕ построены (см. ошибку выше)"));
		return;
	}

	UE_LOG(LogAsdaItems, Log,
		TEXT("[items] из DataTable: %d предметов (код %d..%d), %d изображений | загрузка DT %.0f мс (+%.1f МБ), индексация %.0f мс (+%.1f МБ)"),
		Items.Num(), ItemCodeFirst, ItemCodeLast, ImgTable.Num(),
		(T1 - T0) * 1000.0, double(int64(Mem1) - int64(Mem0)) / (1024.0 * 1024.0),
		(T2 - T1) * 1000.0, double(int64(Mem2) - int64(Mem1)) / (1024.0 * 1024.0));

	// Тест-лог по DataTable: примеры GetSowell/GetTitle/GetItemName.
	{
		FAsdaSowellRow SowRow;
		if (GetSowell(7001, SowRow))
			UE_LOG(LogAsdaItems, Log, TEXT("[items] GetSowell(7001) name='%s' iconPath='%s'"), *SowRow.Name, *SowRow.IconPath);
		FAsdaTitleRow TitRow;
		if (GetTitle(1, TitRow))
			UE_LOG(LogAsdaItems, Log, TEXT("[items] GetTitle(1) name='%s' desc='%s'"), *TitRow.Name, *TitRow.Desc);
		FAsdaPriceRow PriRow;
		if (GetPrice(ItemCodeFirst, PriRow))
			UE_LOG(LogAsdaItems, Log, TEXT("[items] GetPrice(%d) price=%d"), ItemCodeFirst, PriRow.Price);
		UE_LOG(LogAsdaItems, Log, TEXT("[items] GetItemName(%d)='%s' (есть имя=%d)"),
			ItemCodeFirst, *GetItemName(ItemCodeFirst), (int32)HasRuName(ItemCodeFirst));
		UE_LOG(LogAsdaItems, Log, TEXT("[items] DataTable загружено: %d таблиц"), DataTables.Num());
	}

	// ── ТЕСТ-ЛОГ: itemId из живого лога экипа + несколько реальных кодов ──────
	// Из charselect_equip.md: живой лог показал ячейку itemId=1 (пустой слот, код < First)
	// и реальные экип-коды начинаются с ItemCodeFirst (20551 = "Gold").
	// 1 = вне диапазона (пустой слот в живом логе); 20551 Gold; 20552 chest; 20554 pants; last.
	// Плюс языковая проверка имён из DT_item_list: RU / EN / AR / KO / без имени («-»).
	const int32 TestIds[] = { 1, ItemCodeFirst, 20552 /*chst*/, 20554 /*pant*/, ItemCodeLast,
	                          20917 /*RU Реагент уровня D*/, 20625 /*EN*/, 23317 /*AR*/,
	                          22705 /*KO*/, 20626 /*без имени → «-»*/ };
	for (int32 Id : TestIds)
	{
		const FAsdaItemRecord* Rec = GetItem(Id);
		if (!Rec)
		{
			UE_LOG(LogAsdaItems, Log, TEXT("[items] itemId=%d -> (вне диапазона / нет записи)"), Id);
			continue;
		}
		UE_LOG(LogAsdaItems, Log,
			TEXT("[items] itemId=%d name='%s' type=%d imgIdx=%d meshM='%s' meshF='%s' iconM='%s' grade=%d price=%d reqLvl=%d"),
			Id, *Rec->ItemName, Rec->ItemType, Rec->ImgIndex,
			*GetItemMeshName(Id, true), *GetItemMeshName(Id, false),
			*GetItemIconName(Id, true), Rec->ItemGrade, Rec->Price, Rec->RequiredLevel);
	}
}

void UAsdaItemTableSubsystem::Deinitialize()
{
	Items.Empty();
	ImgTable.Empty();
	CodeToIndex.Empty();
	DataTables.Empty();
	KeyIndex.Empty();
	bLoaded = false;
	Super::Deinitialize();
}

const FAsdaItemRecord* UAsdaItemTableSubsystem::GetItem(int32 ItemId) const
{
	if (Items.Num() == 0) return nullptr;
	// Быстрый путь: прямая индексация по диапазону (эталон GetItemRecord: base+248*(code-First)).
	if (ItemId >= ItemCodeFirst && ItemId <= ItemCodeLast)
	{
		const int32 Idx = ItemId - ItemCodeFirst;
		if (Items.IsValidIndex(Idx) && Items[Idx].ItemCode == ItemId)
		{
			return &Items[Idx];
		}
	}
	// Резервный путь (эталон: линейный поиск по коду при промахе диапазона/дырах).
	if (const int32* Found = CodeToIndex.Find(ItemId))
	{
		return &Items[*Found];
	}
	return nullptr;
}

const FAsdaImgRecord* UAsdaItemTableSubsystem::GetImgRecord(int32 ImgIndex) const
{
	return ImgTable.IsValidIndex(ImgIndex) ? &ImgTable[ImgIndex] : nullptr;
}

// ★ Категория звука падения (0…11). Раскладка — та же, что у наземного лута: у обычного предмета
//   она лежит в img-записи (+0xD0), у самоцвета — сырым байтом записи `sowell_list` (+0xAA).
//   Читаем через общую точку `AsdaBytes::HexByteAt`: она честно возвращает −1 и на не-hex, и на
//   короткой строке (прежняя копия на `FParse::HexDigit` превращала мусор в «валидную» категорию 0).
int32 UAsdaItemTableSubsystem::GetDropSoundCat(int32 ItemCode) const
{
	if (const FAsdaItemRecord* R = GetItem(ItemCode))
	{
		if (const FAsdaImgRecord* Img = GetImgRecord(R->ImgIndex)) { return Img->DropSoundCat; }
		return INDEX_NONE;
	}
	FAsdaSowellRow S;
	if (GetSowell(ItemCode, S)) { return AsdaBytes::HexByteAt(S.DataHex, 0xAA); }
	return INDEX_NONE;
}

FString UAsdaItemTableSubsystem::GetItemMeshName(int32 ItemId, bool bMale) const
{
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) return FString();
	const FAsdaImgRecord* Img = GetImgRecord(Rec->ImgIndex);
	if (!Img) return FString();
	return bMale ? Img->MeshMale : Img->MeshFemale;
}

FString UAsdaItemTableSubsystem::GetItemIconName(int32 ItemId, bool bMale) const
{
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) return FString();
	const FAsdaImgRecord* Img = GetImgRecord(Rec->ImgIndex);
	if (!Img) return FString();
	return bMale ? Img->TexMale : Img->TexFemale;
}

FString UAsdaItemTableSubsystem::GetItemTexName(int32 ItemId, bool bMale) const
{
	// Эталонная модельная текстура = ImgRecord.TexMale/TexFemale (та же, что GetItemIconName, но семантически
	//   это ТЕКСТУРА МОДЕЛИ предмета для рендера в мире, а не иконка). Отдельный метод для ясности вызова.
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) return FString();
	const FAsdaImgRecord* Img = GetImgRecord(Rec->ImgIndex);
	if (!Img) return FString();
	return bMale ? Img->TexMale : Img->TexFemale;
}

// ★★★ ЭТАЛОН `itemIsWeaponOrEquipClass@0xECA4D0` (item_inventory.c:64514-64525) — «оружие или снаряжение»:
//   запись item_list есть и `itemType <= 4u` (беззнаково) или 8…9. Общая точка (волна 9 заточки).
bool UAsdaItemTableSubsystem::IsWeaponOrEquipClass(int32 Code) const
{
	// `if (!ItemRecord) return 0; if (itemType <= 4u) return 1; return itemType >= 8 && itemType <= 9;`
	const FAsdaItemRecord* Rec = GetItem(Code);
	if (!Rec) { return false; }
	return (uint32)Rec->ItemType <= 4u || (Rec->ItemType >= 8 && Rec->ItemType <= 9);
}

// ★★★ ЭТАЛОН `itemGetCategoryById@0xECA700` — три таблицы, три категории.
int32 UAsdaItemTableSubsystem::GetItemCategory(int32 Code) const
{
	if (const FAsdaItemRecord* Rec = GetItem(Code))
	{
		const int32 T = Rec->ItemType;
		const bool bWearable = (T >= 0 && T <= 4) || (T >= 8 && T <= 9) || (T == 13);
		return bWearable ? 3 : 1;
	}
	if (IsSowelCode(Code))   { return 2; }
	if (IsSpecialItemCode(Code)) { return 4; }
	return 0;
}

// ★★★ РАЗРЕШЕНИЕ ИКОНКИ — 1:1 три ветки эталона:
//   • самоцвет  (`CInventoryUI_RefreshStoragePage@0xD01730`, ветка g_SowelCodeRangeLo..Hi):
//       `strcpy(dst, SowelRecord->iconPath)` → `DrawTextureRect(ctrl, tex, 0, 0, 32, 32)`
//       ⇒ атлас = IconPath целиком, левый-верхний квадрат 32×32 (НЕ вся текстура);
//   • second-таблица (`CInventoryUI_RefreshBagIconsPage@0xD00990`, ветка g_Table2CodeFirst..Last):
//       `Grid_IndexToCellOffset8x64(Table2Record->iconCell)` → `DrawTextureRect(..., 64, 64)`
//       ⇒ атлас = IconPath, сетка 8 колонок × 64 px;
//   • обычный предмет: `GetImgOptionPtr/Value/Mask` над item_img_table (было и раньше).
FAsdaItemIcon UAsdaItemTableSubsystem::GetItemIcon(int32 ItemId, bool bMale) const
{
	FAsdaItemIcon Icon;
	if (ItemId <= 0) { return Icon; }

	// ★ Аудит №4 (A3): сами ФОРМУЛЫ трёх веток переехали в `namespace AsdaItemIcon`
	//   (Public/Items/AsdaItemTableSubsystem.h) — чистые функции, доступные и редакторскому
	//   плагину-браузеру, у которого GameInstance-подсистемы нет. Здесь остаётся РЕЗОЛВ записи
	//   (кодовые пространства + кэши подсистемы), он рантайм-специфичен.

	// ── самоцветы (sowell_list) ──
	if (IsSowelCode(ItemId))
	{
		FAsdaSowellRow Sow;
		if (GetSowell(ItemId, Sow) && !Sow.IconPath.IsEmpty())
		{
			Icon = AsdaItemIcon::FromSowelIconPath(Sow.IconPath);
		}
		return Icon;
	}
	// ── записи второй таблицы (special_itemlist) ──
	if (IsSpecialItemCode(ItemId))
	{
		FAsdaSpecialItemlistRow Sp;
		if (GetSpecialItem(ItemId, Sp) && !Sp.IconPath.IsEmpty())
		{
			Icon = AsdaItemIcon::FromSpecialIcon(Sp.IconPath, Sp.IconCell);
		}
		return Icon;
	}

	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) { return Icon; }
	const FAsdaImgRecord* Img = GetImgRecord(Rec->ImgIndex);
	if (!Img) { return Icon; }
	// ★★★ [ОРИГИНАЛ] 08.11.2026: `if (!*((_DWORD*)itemTable + 62*imgIndex + 22)) → мужской блок` — строка item_list
	//   с НОМЕРОМ imgIndex (stride 248, +0x58 = ClassRestriction), а не строка самого предмета. На наших данных
	//   номер картинки совпадает с номером строки (проверено по всем 59 390 записям: расхождений 0), берём 1:1.
	//   Без правила 991 «общих» предмета (ClassRestriction 0, но разные картинки) у женщины рисовались бы женской.
	bool bUseMale = bMale;
	if (!bUseMale && CVarIconUnisexMale.GetValueOnAnyThread() != 0)
	{
		const bool bRowValid = Items.IsValidIndex(Rec->ImgIndex);
		const int32 Gate = bRowValid ? Items[Rec->ImgIndex].ClassRestriction : Rec->ClassRestriction;
		if (Gate == 0) { bUseMale = true; }
	}
	return AsdaItemIcon::FromImg(*Img, bUseMale);   // small → 32px/16кол, иначе 64px/8кол
}

bool UAsdaItemTableSubsystem::IsSelfIconMale() const
{
	if (CVarIconSelfGender.GetValueOnAnyThread() == 0) { return true; }   // прежний порт: всегда мужской атлас
	const UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return true; }
	// Эталон читает ровно байт блока 1017 (+27). Он приходит до первого показа сумки; пока его нет —
	//   общий резолвер пола (снапшот появления → запись char-list → муж), без своей копии приоритетов.
	if (Net->SelfGender == 1 || Net->SelfGender == 2) { return Net->SelfGender != 2; }
	return Net->ResolveSelfGender() != 2;
}

int32 UAsdaItemTableSubsystem::GetItemType(int32 ItemId) const
{
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	return Rec ? Rec->ItemType : -1;
}

// ════════════════════════════════════════════════════════════════════════════
//  DataTable-слой
// ════════════════════════════════════════════════════════════════════════════

template<typename TRow>
void UAsdaItemTableSubsystem::BuildKeyIndex(const TCHAR* TableName, int32 TRow::* KeyMember,
                                            TMap<int32, FName>& OutIndex)
{
	const UDataTable* Dt = GetDataTable(FName(TableName));
	if (!Dt) return;
	OutIndex.Reset();
	OutIndex.Reserve(Dt->GetRowMap().Num());
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const TRow* Row = reinterpret_cast<const TRow*>(It.Value);
		OutIndex.Add(Row->*KeyMember, It.Key);   // при дублях ключа — последняя строка
	}
}

// Позиционный кэш: индекс = RowName (колонка `_row` CSV = порядковый номер записи в файле).
//   Строки с нечисловым ключом пропускаются — как и в BuildItemCacheFromDataTables.
template<typename TRow>
void UAsdaItemTableSubsystem::BuildOrderedCache(const TCHAR* TableName, TArray<TRow>& OutRows)
{
	OutRows.Reset();
	const UDataTable* Dt = GetDataTable(FName(TableName));
	if (!Dt) { return; }
	const TMap<FName, uint8*>& Rows = Dt->GetRowMap();
	OutRows.SetNum(Rows.Num());
	for (const TPair<FName, uint8*>& It : Rows)
	{
		const int32 Idx = RowNameToIndex(It.Key);
		if (!OutRows.IsValidIndex(Idx)) { continue; }
		OutRows[Idx] = *reinterpret_cast<const TRow*>(It.Value);
	}
}

// ★★★ B1/B2 (аудит `code_dedup_audit_2.md`): ТАБЛИЦЫ ВНЕ КАТАЛОГА ItemDB.
//   `AsdaItemDbManifest.h` генерируется скриптом ПО КАТАЛОГУ `Data/ItemDB`, поэтому дописывать в
//   него руками нельзя — перегенерация затрёт. Таблицы из других каталогов регистрируем здесь:
//   формат тот же, а список переживает любую перегенерацию манифеста.
//     `SkillCoin` — `SkillCoinTable.bin` (1255 записей, ключ SkillId). До сведения её грузили ДВА
//     потребителя собственным `LoadObject` мимо менеджера, держа в памяти две копии одних чисел.
//     `PlusPackage` — `PlusPackageTable.BIN` (запись 140 б: id@0, нотис заголовка@4, нотис тела@8 +
//     базы .jeb крыльев). ★ Аудит №3 (A3, дубль данных Дд3): её грузили ТРИ независимых потребителя
//     собственным `LoadObject` (мини-статус — дважды, мир — один раз). Теперь загрузка одна.
//     ⚠ Ключевой индекс здесь НЕ строится намеренно: строка `FAsdaPlusPackageRow` объявлена в
//     `World/AsdaWorldSubsystem.h`, и тянуть мировой заголовок в item-БД ради одной карты не нужно —
//     потребители берут таблицу через `GetDataTable("PlusPackage")` и адресуют её по RowName (= wingId),
//     ровно как делали до сведения.
static const AsdaItemDb::FTableInfo GExtraTables[] = {
	{ TEXT("SkillCoin"),   TEXT("/Game/AsdaAssets/Data/DT_SkillCoin.DT_SkillCoin"),     TEXT("SkillId") },
	{ TEXT("PlusPackage"), TEXT("/Game/AsdaAssets/Data/DT_PlusPackage.DT_PlusPackage"), TEXT("Id") },
	// ★ [ИСПРАВЛЕНИЕ] 01.11.2026 (клиент 5.1.4, P100_ChargeStack): ключи слияния зарядов спецпредметов — общий источник с
	//   сервером (`charge_groups_client.csv`, 173 кода; импорт `Tools/asda_import_charge_groups.py`). Имя «ChargeGroup» =
	//   `AsdaChargeStack::TableName()`; кэш «код → ключ/заряды» строит `AsdaChargeStack` (строка `FAsdaChargeGroupRow`).
	{ TEXT("ChargeGroup"), TEXT("/Game/AsdaAssets/Data/DT_ChargeGroup.DT_ChargeGroup"), TEXT("Code") },
};
static constexpr int32 GExtraTableCount = UE_ARRAY_COUNT(GExtraTables);

void UAsdaItemTableSubsystem::LoadDataTables()
{
	DataTables.Reset();
	int32 Ok = 0;
	for (int32 i = 0; i < AsdaItemDb::GTableCount; ++i)
	{
		const AsdaItemDb::FTableInfo& Ti = AsdaItemDb::GTables[i];
		UDataTable* Dt = LoadObject<UDataTable>(nullptr, Ti.DataTablePath);
		if (Dt)
		{
			DataTables.Add(FName(Ti.Table), Dt);
			++Ok;
		}
		else
		{
			// ★ Было `Verbose` — потеря таблицы проходила МОЛЧА, а строка «загружено N/N» ниже
			//   выглядела как «всё хорошо» (так незаметно пропала `skill_info`, см. манифест).
			UE_LOG(LogAsdaItems, Warning, TEXT("[items] DataTable НЕ ЗАГРУЖЕН: %s (таблица '%s')"),
				Ti.DataTablePath, Ti.Table);
		}
	}
	// Таблицы вне манифеста (см. GExtraTables выше) — тем же способом и в тот же реестр.
	for (int32 i = 0; i < GExtraTableCount; ++i)
	{
		const AsdaItemDb::FTableInfo& Ti = GExtraTables[i];
		if (UDataTable* Dt = LoadObject<UDataTable>(nullptr, Ti.DataTablePath))
		{
			DataTables.Add(FName(Ti.Table), Dt);
			++Ok;
		}
		else
		{
			UE_LOG(LogAsdaItems, Warning, TEXT("[items] DataTable вне манифеста не найден: %s"), Ti.DataTablePath);
		}
	}
	UE_LOG(LogAsdaItems, Log, TEXT("[items] DataTable загружено %d/%d"), Ok,
		AsdaItemDb::GTableCount + GExtraTableCount);
	// Ключевой индекс таблицы монет — чтобы `GetSkillCoin` резолвил по SkillId, а не линейно.
	BuildKeyIndex<FAsdaSkillCoinRow>(TEXT("SkillCoin"), &FAsdaSkillCoinRow::SkillId,
		KeyIndex.FindOrAdd(TEXT("SkillCoin")));

	// Индексы id->RowName для типизированных геттеров.
	BuildKeyIndex<FAsdaSowellRow>     (TEXT("sowell_list"),  &FAsdaSowellRow::Id,        KeyIndex.FindOrAdd(TEXT("sowell_list")));
	// ★ Тексты подсказок самоцветов (`sowell_tooltip`, эталон `sowelTable_FindByCode`) — ключ = Id.
	BuildKeyIndex<FAsdaSowellTooltipRow>(TEXT("sowell_tooltip"), &FAsdaSowellTooltipRow::Id, KeyIndex.FindOrAdd(TEXT("sowell_tooltip")));
	BuildKeyIndex<FAsdaTitleRow>      (TEXT("title_list"),   &FAsdaTitleRow::Id,         KeyIndex.FindOrAdd(TEXT("title_list")));
	BuildKeyIndex<FAsdaPetRow>        (TEXT("pettable"),     &FAsdaPetRow::Id,           KeyIndex.FindOrAdd(TEXT("pettable")));
	BuildKeyIndex<FAsdaPriceRow>      (TEXT("pricetable"),   &FAsdaPriceRow::ItemCode,   KeyIndex.FindOrAdd(TEXT("pricetable")));
	// ★ 25.09.2026 (заточка): таблица свитков `UpgradeTable.BIN`, ключ = код вида свитка (`GetUpgradeTable`).
	BuildKeyIndex<FAsdaUpgradetableRow>(TEXT("upgradetable"), &FAsdaUpgradetableRow::EventId, KeyIndex.FindOrAdd(TEXT("upgradetable")));
	BuildKeyIndex<FAsdaItemTooltipRow>(TEXT("item_tooltip"), &FAsdaItemTooltipRow::ItemCode, KeyIndex.FindOrAdd(TEXT("item_tooltip")));
	BuildKeyIndex<FAsdaItemoptionrangeRow>(TEXT("itemoptionrange"), &FAsdaItemoptionrangeRow::RowId, KeyIndex.FindOrAdd(TEXT("itemoptionrange")));
	// ★ Аудит №3 (A3): таблицы, которые до сведения грузили ВТОРЫМ `LoadObject` подсистемы эффектов,
	//   хранителя и хотбара. Ключ у всех трёх — `Id`, и он в данных совпадает с порядковым номером
	//   записи (skilltooltip 0..1254, stateinfotip 0..348, soulstateinfotip 0..15 — сплошь, без дублей),
	//   поэтому резолв по ключу возвращает ТУ ЖЕ строку, что прежний `FindRow` по имени-номеру.
	BuildKeyIndex<FAsdaSkilltooltipRow>    (TEXT("skilltooltip"),     &FAsdaSkilltooltipRow::Id,     KeyIndex.FindOrAdd(TEXT("skilltooltip")));
	BuildKeyIndex<FAsdaStateinfotipRow>    (TEXT("stateinfotip"),     &FAsdaStateinfotipRow::Id,     KeyIndex.FindOrAdd(TEXT("stateinfotip")));
	BuildKeyIndex<FAsdaSoulstateinfotipRow>(TEXT("soulstateinfotip"), &FAsdaSoulstateinfotipRow::Id, KeyIndex.FindOrAdd(TEXT("soulstateinfotip")));
	// ★ ВТОРАЯ ТАБЛИЦА ПРЕДМЕТОВ — её записи ЛЕЖАТ В СУМКЕ и рисуются своей иконкой
	//   (`CInventoryUI_RefreshBagIconsPage@0xD00990`, ветка `itemCode ∈ [Table2First..Table2Last]`).
	BuildKeyIndex<FAsdaSpecialItemlistRow>(TEXT("special_itemlist"), &FAsdaSpecialItemlistRow::Code, KeyIndex.FindOrAdd(TEXT("special_itemlist")));
	// ★★★ ТАБЛИЦА ПОДСКАЗОК спец-предметов (`SpecialItem_TooltipList.BIN`). Ключевого индекса у неё
	//   раньше не было вовсе, поэтому ветка инфо-панели для премиум/бустер-предметов была
	//   недостижима. Строим И ключевой индекс (общий `FindRowName`/Blueprint), И позиционный кэш —
	//   эталон `itemGetTooltipById@0xEC73B0` резолвит АРИФМЕТИКОЙ, а не по ключу.
	BuildKeyIndex<FAsdaSpecialitemTooltiplistRow>(TEXT("specialitem_tooltiplist"),
		&FAsdaSpecialitemTooltiplistRow::Id, KeyIndex.FindOrAdd(TEXT("specialitem_tooltiplist")));
	BuildOrderedCache<FAsdaSpecialitemTooltiplistRow>(TEXT("specialitem_tooltiplist"), SpecialTooltips);
	// `g_SpecialItemTooltipFirstId/LastId` = id ПЕРВОЙ и ПОСЛЕДНЕЙ записи файла (после того, как
	//   загрузчик применил «−36» и форсировал первой записи id = 1).
	SpecialTooltipFirstId = SpecialTooltips.Num() > 0 ? SpecialTooltips[0].Id      : 0;
	SpecialTooltipLastId  = SpecialTooltips.Num() > 0 ? SpecialTooltips.Last().Id  : 0;
	// ★ Диагностика расхождения данных: эталон читает строку ПОЗИЦИОННО, поэтому дыра в нумерации
	//   означала бы сдвиг подсказок. Один раз проверяем сплошность и говорим об этом вслух.
	if (SpecialTooltips.Num() > 0
	    && SpecialTooltipLastId - SpecialTooltipFirstId + 1 != SpecialTooltips.Num())
	{
		UE_LOG(LogAsdaItems, Warning,
			TEXT("[items] specialitem_tooltiplist: id НЕ сплошные (%d..%d при %d строках) — позиционный резолв эталона даст сдвиг"),
			SpecialTooltipFirstId, SpecialTooltipLastId, SpecialTooltips.Num());
	}
	// ★ PremiumLevelTable — позиционный кэш (линейный поиск «первое совпадение», как в эталоне).
	BuildOrderedCache<FAsdaPremiumleveltableRow>(TEXT("premiumleveltable"), PremiumLevels);

	// Границы кодовых пространств: эталон берёт код ПЕРВОЙ и ПОСЛЕДНЕЙ записи файла; таблицы
	//   отсортированы по коду, поэтому это же самое — min/max по индексу.
	auto RangeOf = [this](const TCHAR* Table, int32& OutFirst, int32& OutLast)
	{
		OutFirst = 0; OutLast = 0;
		const TMap<int32, FName>* Idx = KeyIndex.Find(FName(Table));
		if (!Idx || Idx->Num() == 0) { return; }
		for (const TPair<int32, FName>& It : *Idx)
		{
			if (OutFirst == 0 || It.Key < OutFirst) { OutFirst = It.Key; }
			if (It.Key > OutLast) { OutLast = It.Key; }
		}
	};
	RangeOf(TEXT("special_itemlist"), SpecialCodeFirst, SpecialCodeLast);
	RangeOf(TEXT("sowell_list"),      SowelCodeFirst,   SowelCodeLast);
	UE_LOG(LogAsdaItems, Log,
		TEXT("[items] кодовые пространства: special_itemlist (Table2) %d..%d | sowell_list %d..%d | подсказки спец-предметов %d..%d (%d строк) | PremiumLevelTable %d строк"),
		SpecialCodeFirst, SpecialCodeLast, SowelCodeFirst, SowelCodeLast,
		SpecialTooltipFirstId, SpecialTooltipLastId, SpecialTooltips.Num(), PremiumLevels.Num());
}

UDataTable* UAsdaItemTableSubsystem::GetDataTable(FName TableName) const
{
	const TObjectPtr<UDataTable>* Found = DataTables.Find(TableName);
	// IsValid: ассет мог не закукаться в билде (в карту не попал) либо быть помечен на удаление —
	//   отдаём nullptr, вызывающий уходит на fallback, а не разыменовывает мусор.
	if (!Found || !IsValid(*Found))
	{
		UE_LOG(LogAsdaItems, Warning, TEXT("[items] DataTable '%s' недоступен (не загружен/не закукан)"), *TableName.ToString());
		return nullptr;
	}
	return *Found;
}

bool UAsdaItemTableSubsystem::FindRowName(FName TableName, int32 Id, FName& OutRowName) const
{
	if (const TMap<int32, FName>* Idx = KeyIndex.Find(TableName))
	{
		if (const FName* Row = Idx->Find(Id))
		{
			OutRowName = *Row;
			return true;
		}
	}
	return false;
}

// Универсальный резолвер строки по числовому ключу через индекс + FindRow.
template<typename TRow>
static bool ResolveRow(const UAsdaItemTableSubsystem* Self, FName TableName, int32 Id, TRow& OutRow)
{
	FName RowName;
	if (!Self->FindRowName(TableName, Id, RowName)) return false;
	const UDataTable* Dt = Self->GetDataTable(TableName);
	if (!Dt) return false;
	static const FString Ctx(TEXT("AsdaItemDb"));
	const TRow* Found = Dt->FindRow<TRow>(RowName, Ctx, false);
	if (!Found) return false;
	OutRow = *Found;
	// ★ [ПОРТ] 09.10.2026 (мультиязычность): текстовые поля КОПИИ — на язык сессии (ключ `<RowName>.<поле>`).
	//   Строка таблицы в памяти не трогается; у таблиц без текста (цены, коды) перегрузка пустая; RU — без поиска.
	AsdaLangRows::Localize(Dt, RowName, OutRow);
	return true;
}

bool UAsdaItemTableSubsystem::GetSowell(int32 Id, FAsdaSowellRow& OutRow) const
{
	return ResolveRow<FAsdaSowellRow>(this, TEXT("sowell_list"), Id, OutRow);
}
// ★ Текст подсказки самоцвета (эталон `sowelTable_FindByCode` → строка на +4).
bool UAsdaItemTableSubsystem::GetSowellTooltip(int32 Id, FAsdaSowellTooltipRow& OutRow) const
{
	return ResolveRow<FAsdaSowellTooltipRow>(this, TEXT("sowell_tooltip"), Id, OutRow);
}
// ★★★ Байт СЫРОЙ записи `sowell_list` (188 б) из колонки `DataHex`. Нужен полям, которым в
//   `Tools/decode_item_db.py` не сопоставлена колонка, хотя дешифровка их разбирает побайтово
//   (`for o in (36,37,38,39): sub_b(...)`, `(44,45,46,47)`, `(64,65,66)`):
//     +38 (0x26) `tooltipParam` — ВЕС самоцвета, печатается с нотисом 2295;
//     +47 (0x2F) третий байт `gap_2D` — третья строка «Место в снаряжении» (нотис 8569+N);
//     +66 (0x42) `gap_41[1]`   — флаг «Эта вещь не передается» (нотис 8401).
//   Смещения сняты с бинаря (`CItem_BuildSowelTooltipText@0xEA2EC0`):
//     EA36B3 movzx ecx, byte ptr [eax+0x26]  → push 0x8F7 (2295 «Вес»)
//     EA380E movzx ecx, byte ptr [eax+0x42]  → ветка со строкой 8401
//   Контроль базы в том же теле: [eax+0x24] bonusType(+8510), [eax+0x25] partGrade,
//   [eax+0x2C] specialFlag, [eax+0x41] requiredLevel, [eax+0xA9] gradeColorIndex, [eax+0x44] маска.
int32 UAsdaItemTableSubsystem::SowelRawByte(const FAsdaSowellRow& Row, int32 Off)
{
	return RawByteAt(Row.DataHex, Off);
}
// ★★★ Байт сырой записи `special_itemlist` (300 б). Нужен полю +0x57 «режим списания» — колонки
//   у него нет, а ветка списания предмета в 5451 держится ровно на нём (см. `SpecialItemConsumeMode`).
int32 UAsdaItemTableSubsystem::SpecialItemRawByte(const FAsdaSpecialItemlistRow& Row, int32 Off)
{
	return RawByteAt(Row.DataHex, Off);
}
// ★ Запись ВТОРОЙ таблицы предметов (эталон `CItemManager_GetTable2Record@0x9C43A0`).
bool UAsdaItemTableSubsystem::GetSpecialItem(int32 Code, FAsdaSpecialItemlistRow& OutRow) const
{
	return ResolveRow<FAsdaSpecialItemlistRow>(this, TEXT("special_itemlist"), Code, OutRow);
}
// ★★★ Подсказка спец-предмета — 1:1 `itemGetTooltipById@0xEC73B0` (item_inventory.c:63738):
//   таблица не загружена → nullptr; код вне [First..Last] → nullptr; иначе ПОЗИЦИЯ code−First.
bool UAsdaItemTableSubsystem::GetSpecialItemTooltip(int32 Code, FAsdaSpecialitemTooltiplistRow& OutRow) const
{
	if (SpecialTooltips.Num() == 0) { return false; }
	if (Code < SpecialTooltipFirstId || Code > SpecialTooltipLastId) { return false; }
	const int32 Idx = Code - SpecialTooltipFirstId;
	if (!SpecialTooltips.IsValidIndex(Idx)) { return false; }
	OutRow = SpecialTooltips[Idx];
	// ★ [ПОРТ] 09.10.2026 (мультиязычность): позиционный кэш построен по RowName = индексу записи (`BuildOrderedCache`),
	//   поэтому имя строки таблицы — это `Idx`; перевод — на копии.
	if (AsdaLangRows::Active())
	{
		AsdaLangRows::Localize(GetDataTable(TEXT("specialitem_tooltiplist")), FName(*FString::FromInt(Idx)), OutRow);
	}
	return true;
}

// ★★★ 1:1 `inventoryFindMatchingStackSlot@0xD34260` (item_inventory.c:45573).
//   Эталонный порядок проверок сохранён полностью, включая ранние выходы «вернуть 0».
bool UAsdaItemTableSubsystem::FindPremiumStatRange(int32 ItemCode, int32 PlayerLevel,
                                                   int32& OutMin, int32& OutMax) const
{
	OutMin = 0; OutMax = 0;
	// `ItemRecord = GetItemRecord(*a2); if (!ItemRecord) return 0;`
	const FAsdaItemRecord* Rec = GetItem(ItemCode);
	if (!Rec) { return false; }
	// `Table2Record_0 = GetTable2Record_0(*a2); if (!Table2Record_0) return 0;` — это pricetable.
	FAsdaPriceRow Price;
	if (!GetPrice(ItemCode, Price)) { return false; }
	// `if (level > Table2Record_0[11]) level = Table2Record_0[11];` — кламп СВЕРХУ, без нижнего.
	int32 Level = PlayerLevel;
	if (Level > Price.dw11) { Level = Price.dw11; }

	for (const FAsdaPremiumleveltableRow& Row : PremiumLevels)
	{
		const bool bHit = (Rec->ItemType == 9)
			? (Row.Level == Level && Row.ItemType == Rec->ItemType
			   && Row.ItemSubType == Rec->ItemSubType && Row.ItemGrade == Rec->ItemGrade)
			: (Row.Level == Level && Row.ItemType == Rec->ItemType
			   && Row.UsageCategory == Rec->UsageCategory && Row.ItemGrade == Rec->ItemGrade);
		if (bHit)
		{
			OutMin = Row.StatMin;   // строка[+20]
			OutMax = Row.StatMax;   // строка[+24]
			return true;
		}
	}
	return false;
}

bool UAsdaItemTableSubsystem::GetTitle(int32 Id, FAsdaTitleRow& OutRow) const
{
	if (!ResolveRow<FAsdaTitleRow>(this, TEXT("title_list"), Id, OutRow)) { return false; }
	OutRow.Name = AsdaLangRows::TitleDisplayName(OutRow.Name);   // ★ [ИСПРАВЛЕНИЕ] 07.11.2026: без служебной «*» (плашка, отчёт охоты)
	return true;
}
bool UAsdaItemTableSubsystem::GetPet(int32 Id, FAsdaPetRow& OutRow) const
{
	return ResolveRow<FAsdaPetRow>(this, TEXT("pettable"), Id, OutRow);
}
bool UAsdaItemTableSubsystem::GetPrice(int32 ItemCode, FAsdaPriceRow& OutRow) const
{
	return ResolveRow<FAsdaPriceRow>(this, TEXT("pricetable"), ItemCode, OutRow);
}
// ★ Порт `CItemManager_GetEventRecord@0x9C3BC0` — строка таблицы свитков заточки по коду вида (см. .h).
bool UAsdaItemTableSubsystem::GetUpgradeTable(int32 EventId, FAsdaUpgradetableRow& OutRow) const
{
	return ResolveRow<FAsdaUpgradetableRow>(this, TEXT("upgradetable"), EventId, OutRow);
}
bool UAsdaItemTableSubsystem::GetItemTooltip(int32 ItemCode, FAsdaItemTooltipRow& OutRow) const
{
	return ResolveRow<FAsdaItemTooltipRow>(this, TEXT("item_tooltip"), ItemCode, OutRow);
}
bool UAsdaItemTableSubsystem::GetItemOptionRange(int32 RowId, FAsdaItemoptionrangeRow& OutRow) const
{
	return ResolveRow<FAsdaItemoptionrangeRow>(this, TEXT("itemoptionrange"), RowId, OutRow);
}
// ★ B1/B2: одна загрузка `DT_SkillCoin` на всю игру (см. GExtraTables). Резолв по ключу SkillId.
bool UAsdaItemTableSubsystem::GetSkillCoin(int32 SkillId, FAsdaSkillCoinRow& OutRow) const
{
	return ResolveRow<FAsdaSkillCoinRow>(this, TEXT("SkillCoin"), SkillId, OutRow);
}

// Резолвер строки по ПОРЯДКОВОМУ НОМЕРУ записи (RowName = «0», «1», …) — для таблиц, у которых
//   ключевого поля нет и в оригинале, а эталон адресует массив индексом.
template<typename TRow>
static bool ResolveRowByIndex(const UAsdaItemTableSubsystem* Self, FName TableName, int32 Idx, TRow& OutRow)
{
	if (Idx < 0) { return false; }
	const UDataTable* Dt = Self->GetDataTable(TableName);
	if (!Dt) { return false; }
	static const FString Ctx(TEXT("AsdaItemDb"));
	const FName RowName(*FString::FromInt(Idx));
	const TRow* Found = Dt->FindRow<TRow>(RowName, Ctx, false);
	if (!Found) { return false; }
	OutRow = *Found;
	AsdaLangRows::Localize(Dt, RowName, OutRow);   // ★ [ПОРТ] 09.10.2026 (мультиязычность): как в ResolveRow выше
	return true;
}

// ★ Аудит №3 (A14): подсказка эмоции — ПОЗИЦИОННО (у `emotiontip` нет ключевой колонки, см. .h).
bool UAsdaItemTableSubsystem::GetEmotionTip(int32 RecordIdx, FAsdaEmotiontipRow& OutRow) const
{
	return ResolveRowByIndex<FAsdaEmotiontipRow>(this, TEXT("emotiontip"), RecordIdx, OutRow);
}
// ★ Аудит №3 (A3/A14): `skilltooltip` по коду навыка (он же порядковый номер записи — см. .h).
bool UAsdaItemTableSubsystem::GetSkillTooltip(int32 SkillCode, FAsdaSkilltooltipRow& OutRow) const
{
	return ResolveRow<FAsdaSkilltooltipRow>(this, TEXT("skilltooltip"), SkillCode, OutRow);
}
// ★ Аудит №3 (A3): `stateinfotip` по id состояния.
bool UAsdaItemTableSubsystem::GetStateInfoTip(int32 Id, FAsdaStateinfotipRow& OutRow) const
{
	return ResolveRow<FAsdaStateinfotipRow>(this, TEXT("stateinfotip"), Id, OutRow);
}
// ★ Аудит №3 (A3): `soulstateinfotip` по id состояния соулмейта.
bool UAsdaItemTableSubsystem::GetSoulStateInfoTip(int32 Id, FAsdaSoulstateinfotipRow& OutRow) const
{
	return ResolveRow<FAsdaSoulstateinfotipRow>(this, TEXT("soulstateinfotip"), Id, OutRow);
}
void UAsdaItemTableSubsystem::GetItemOptionRangesByGroup(int32 GroupId, TArray<FAsdaItemoptionrangeRow>& OutRows) const
{
	OutRows.Reset();
	const UDataTable* Dt = GetDataTable(TEXT("itemoptionrange"));
	if (!Dt) return;
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaItemoptionrangeRow* Row = reinterpret_cast<const FAsdaItemoptionrangeRow*>(It.Value);
		if (Row->GroupId == GroupId) OutRows.Add(*Row);
	}
}

// ── Имя предмета ────────────────────────────────────────────────────────────
// Имя лежит В САМОЙ ТАБЛИЦЕ DT_item_list (колонка ItemName): русские имена оригинального
// RU-клиента + восстановленные из EN/AR/KO-сборок (подставлены на этапе импорта,
// Tools/asda_fill_item_names.py). Никаких рантайм-оверлеев из CSV больше нет.

FString UAsdaItemTableSubsystem::GetItemName(int32 ItemId) const
{
	if (const FAsdaItemRecord* Rec = GetItem(ItemId))
	{
		// ★ [ПОРТ] 09.10.2026 (мультиязычность): кэш `Items` хранит РУССКОЕ имя (на нём `HasRuName` и журнал), язык
		//   сессии — здесь. Ключ — имя строки `DT_item_list` (колонка `_row` = `ImgIndex`), НЕ ItemCode.
		if (!AsdaLangRows::Active()) { return Rec->ItemName; }
		return FAsdaLang::ResolveRow(GetDataTable(TEXT("item_list")), FName(*FString::FromInt(Rec->ImgIndex)),
			TEXT("ItemName"), Rec->ItemName);
	}
	// ★ Эталон `itemResolveDataByCode` смотрит ВСЕ ТРИ таблицы, а не только item_list
	//   (`uiInventoryOnRecvItemAcquired@0xD10E80`: item_list → sowell_list → special_itemlist).
	if (IsSowelCode(ItemId))
	{
		FAsdaSowellRow Sow;
		if (GetSowell(ItemId, Sow)) { return Sow.Name; }
	}
	else if (IsSpecialItemCode(ItemId))
	{
		FAsdaSpecialItemlistRow Sp;
		if (GetSpecialItem(ItemId, Sp)) { return Sp.Name; }
	}
	return FString();
}

bool UAsdaItemTableSubsystem::HasRuName(int32 ItemId) const
{
	const FAsdaItemRecord* Rec = GetItem(ItemId);
	if (!Rec) return false;
	const FString Trimmed = Rec->ItemName.TrimStartAndEnd();
	return !Trimmed.IsEmpty() && Trimmed != TEXT("-");   // "-" = имени нет и в оригинале
}
