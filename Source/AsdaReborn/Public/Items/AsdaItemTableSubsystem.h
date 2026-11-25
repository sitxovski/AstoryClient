// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTableSubsystem — мост itemId → запись предмета (имя меша/иконки + itemType + все поля).
// ПОРТ 1:1 клиентского CItemManager (эталон AsdaStory_game.c):
//   CItemManager_GetItemRecord    @0x9C3CE0 — base + 248*(itemCode - g_ItemCodeFirst)
//   CItemManager_GetImgRecord     @0x9C3DC0 — base + 300*imgIndex
//
// ★ ИСТОЧНИК ДАННЫХ В РАНТАЙМЕ = UE-АССЕТЫ DataTable, а НЕ файлы оригинала.
//   /Game/AsdaAssets/Data/ItemDB/DT_item_list       (FAsdaItemRow, 58878 строк)
//   /Game/AsdaAssets/Data/ItemDB/DT_item_img_table  (FAsdaImgRow,  58878 строк)
//   + остальные 49 таблиц item-БД по манифесту AsdaItemDbManifest.h.
//   Бинарники Bin.bbz (item_list.dec.bin, item_img_table.bin) и CSV-выгрузки используются
//   ТОЛЬКО на этапе импорта (Tools/decode_item_db.py → CSV → gen_itemdb_datatables.py → DT_*),
//   рантайм их не открывает.
//   Ключ строки DataTable (RowName) = порядковый номер записи в оригинальном файле,
//   он же imgIndex (эталонный загрузчик перезаписывает поле 0xF0 индексом записи).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"   // авто-ген USTRUCT-строки всех таблиц item-БД
#include "Data/AsdaSkillCoinRow.h"   // B1/B2: FAsdaSkillCoinRow — DT_SkillCoin теперь идёт через менеджер   ★ R3: FAsdaSkillCoinRow — узкий заголовок вместо AsdaSoulGuardRows.h
#include "AsdaItemTableSubsystem.generated.h"

// Запись предмета ItemRecord (248 байт). Раскладка 1:1 из RE/ida/itemrecord_fields.tsv
// (client-usage, подтверждено чтениями полей в отрисовке/тултипе/экипировке эталона).
USTRUCT(BlueprintType)
struct FAsdaItemRecord
{
	GENERATED_BODY()

	/*+0x00*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   ItemCode = 0;        // код предмета (первичный ключ)
	/*+0x04*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") FString ItemName;            // имя (char[64], дешифр. -1/байт)
	/*+0x4C*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   EnchantOrSocket = 0; // уровень заточки / число сокетов
	/*+0x50*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   RequiredLevel = 0;   // требуемый уровень (гейт экипа)
	/*+0x54*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   RequiredLevelMax = 0;// верхняя граница уровня
	/*+0x58*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   ClassRestriction = 0;// ограничение по классу
	/*+0x5C*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   UsageCategory = 0;   // функц. под-класс/условие
	/*+0x60*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   LinkCode = 0;        // вторичный код поиска
	/*+0x64*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   LinkedGroupCode = 0; // связь групп (WORD в слоте)
	// ★ 2026-09-16. Прежний комментарий «2=chest, 6=weapon, 8=helmet» был ОШИБОЧЕН (тип 6 не
	//   встречается в таблице ни разу). Мапа снята с ДАННЫХ владельца (`item_list.bin` из
	//   `Donors\AsdaChroniclesInterlude_MINE\Data\Character\Bin.bbz`) группировкой имён по (тип, подтип);
	//   счётчики: 0=7765, 1=8082, 2=8067, 3=8073, 4=8064, 5=83, 7=89, 8=804, 9=6692, 10=31,
	//   11=11081, 13=47; типов 6 и 12 нет. `ItemType` = НОМЕР СЛОТА экипировки: сервер
	//   (`sub_4CDAA0@0x4CDAA0`) требует «слот == itemTbl[+26]» (отказ 11), а тип 11 отвергает
	//   сразу (отказ 12) — это не слот, а расходники/прочее.
	/*+0x80*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   ItemType = 0;        // слот/категория: 0=шлем,1=кираса,2=набедренники,3=ботинки,4=перчатки,5=кольцо,7=ожерелье,8=щит,9=оружие,10=колчан/наживка,11=расходник и прочее (подтип 200/201 = коробка),13=бустер
	// ★ 2026-09-16. Было «0=меч,1/4=двуруч,2=копьё,3=лук,5=посох» — НЕВЕРНО. Подтипы читаются
	//   прямо с английских имён предметов типа 9: One Hand Sword / Spear / Two-handed Sword /
	//   Rod-Staff / Crossbow / Bow / Hand Ballista. Совпадает с нарезкой вкладок бустер-шопа
	//   (`AsdaBoosterShopSubsystem.cpp:448`) и со списком двуручных в `AsdaEquipShopSubsystem.cpp:1450`.
	/*+0x84*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   ItemSubType = 0;     // подтип (у оружия = ТИП ОРУЖИЯ: 1=одноруч. меч,2=копьё,3=ДВУРУЧНЫЙ МЕЧ,5=посох/жезл,9=арбалет,10=ЛУК,11=баллиста)
	/*+0x8C*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   AttackRangeTiles = 0;// дальность атаки в тайлах (эталон: игрок +0x2EA10 = (это×100)²)
	/*+0x90*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   ItemVariant = 0;     // вариант (рука/слот при subType==10)
	/*+0x98*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   TradableFlag = 0;    // флаг доступности (продажа/трейд)
	/*+0x9C*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   CategoryFlag = 0;    // маркер группы (фильтр магазина)
	/*+0xA0*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   SubCategory = 0;     // категория (в паре с ItemType)
	/*+0xA4*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   Price = 0;           // цена
	/*+0xB0*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   FlagWord176 = 0;     // WORD-флаг (гейт экипа)
	/*+0xC8*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   TooltipFlag200 = 0;  // флаг тултипа/категории
	/*+0xD8*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   DurationOrEnchant = 0;// длительность/заточка-связка
	/*+0xDC*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   LinkWordCode = 0;    // WORD код связанных записей
	/*+0xE4*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   GroupCode = 0;       // код группы
	/*+0xE8*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   SetCode = 0;         // код набора/родственных записей
	/*+0xEC*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   ItemGrade = 0;       // грейд/редкость (0=co…4=le)
	// Индекс в item_img_table. ВНИМАНИЕ: в файле поле 0xF0 = плейсхолдер; активный загрузчик
	// (LoadItemList_BIN@0x9C64E0) перезаписывает его порядковым индексом записи i. Мы делаем так же
	// => ImgIndex == позиция записи в таблице == (ItemCode - ItemCodeFirst).
	UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   ImgIndex = 0;

	// ── ★ ПОЛЯ ТУЛТИПА, добавленные заходом «тултипы и характеристики» ───────────────────────────
	//   Их НЕТ отдельными колонками в DT_item_list, но сырая запись 248 б лежит в колонке `DataHex`
	//   целиком (проверено: DataHex[0..3] == ItemCode) — читаем прямо оттуда при построении кэша.
	//   Регенерация DataTable НЕ требуется.
	/*+68 = 0x44*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 DurabilityMax = 0; // знаменатель строки «Прочность:» (нотис 110308)
	//   ⚠ Имя эталона `itemRecord->field68` — ДЕСЯТИЧНОЕ (ручное имя), смещение 0x44, а НЕ 0x68:
	//     asm `EC34CA mov ecx,[eax+0x44]`. Чтение по 0x68 давало мусор ⇒ «Прочность: 50/0».
	/*+0x88*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 StatBaseHi = 0;    // u16 `gap_88[0]`  — ВЕРХНЯЯ база «Эффект ЖК» (нотис 2360), она же множитель заточки
	/*+0x8A*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 StatBaseLo = 0;    // u16 `gap_88[2]`  — НИЖНЯЯ база «Эффект ЖК»
	// ★ Вес ЕДИНИЦЫ предмета из шаблона. Эталон читает его как `LOWORD(itemRecord->field72)` и
	//   печатает с нотисом 2295 («Вес») в ПРОСТОМ тултипе `CItem_BuildSowelTooltipText@0xEA2EC0`
	//   (item_inventory.c:60040/60077/60152) и в `ItemTooltip_BuildItemInfo@0xEAEEB0` (61735:
	//   `totalValue = количество * LOWORD(field72)`). Это НЕ поле 57-байтовой записи слота
	//   (там +14 — вес всей стопки, его считает сервер), а колонка шаблона `item_list` +0x48.
	/*+0x48*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 Weight = 0;

	// ── ★★★ ОТКАТ ПРИМЕНЕНИЯ (волна 1 «применение предметов», отчёт `item_use_system.md` §5.1) ──────
	// ГРУППА ОТКАТА. Эталонный клиент держит её в записи 248 б по смещению 0xCC, сервер — в своём
	//   71-байтном шаблоне по +59 (`CheckSpendItemCoolTime@0x4DA9F0` читает таблицу `CUser+0x6F`,
	//   30 записей × 7 б: +0 группа, +1 startTick, +5 длительность). Откат на сервере — ТОЛЬКО
	//   групповой: выпил зелье здоровья — на откате вся группа 1, а не один код.
	// ⚠ Проверено ВТОРЫМ МЕТОДОМ прямо перед правкой (закон `false-negatives-method`): скан
	//   `item_list.dec.bin` (14 601 764 б = 20 б заголовка + 58 878 × 248) даёт ненулевой `u16@0xD0`
	//   ровно у 54 записей, и у этих же 54 поле `i32@0xCC` принимает ровно 4 значения:
	//     1 → 26 записей (зелья/еда здоровья), 2 → 25 (мана и эликсиры), 10 → 1 (Свиток оживления),
	//     11 → 2 (Свитки возвращения).
	//   Прежний параллельный проход утверждал, что «группы в клиенте нет» — это ЛОЖНОЕ «нет».
	/*+0xCC*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 CooldownGroup = 0;
	// ДЛИТЕЛЬНОСТЬ ОТКАТА В МИЛЛИСЕКУНДАХ (u16). В декомпиле выглядит как `itemRecord->gap_CC[4]`
	//   (0xCC + 4 = 0xD0) и читается в `hotbarPushItemCooldown@0xE48FD0`; 0 ⇒ отката нет.
	//   Значения в данных: 2000 / 5000 / 8000 / 10000 / 30000.
	/*+0xD0*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 CooldownMs = 0;

	// ── ★★★ ГРУППЫ ВОЗМОЖНЫХ ПАРАМЕТРОВ (окно «Изменение параметров предмета», JUI 50300) ────────
	//   У предмета ДВА гнезда параметров (поля записи слота +29/+31 и +33/+35). Какие значения в
	//   них вообще могут стоять — задаёт ГРУППА из шаблона `item_list`: `u16 @0x68` для гнезда 0
	//   и `u16 @0x74` для гнезда 1. Группа адресует строки `itemoptionrange` (loose
	//   `ItemOptionValue.BIN`, поле `GroupId`), каждая из которых несёт `OptionType` + диапазон
	//   `MinValue…MaxValue`.
	//   Смещения сняты ДВУМЯ методами (закон `false-negatives-method`):
	//     • дизасм `CItemTooltip_BuildOptionLines@0xB62C70`:
	//         B62F95  movzx edx, word ptr [ecx + 0x68]   ; j == 0
	//         B62FB8  movzx edx, word ptr [ecx + 0x74]   ; j == 1
	//       и оба уходят аргументом в `objFilterRefreshByType@0xB62400`, который отбирает строки
	//       20-байтовой таблицы по полю +4 (= `GroupId`);
	//     • данные: у ЛЮБОГО снаряжения `DataHex[104..105]` — сплошная нумерация групп 1,2,3…,
	//       и ровно эти значения встречаются в колонке `GroupId` таблицы `itemoptionrange`.
	//   ⚠ Имя IDA `itemRecord->gap_68` ШЕСТНАДЦАТЕРИЧНОЕ (авто-имя), поэтому `gap_68[12]` = 0x74,
	//     а не 0x68+12 в десятичном смысле — та же ловушка, что с `field68`/`DurabilityMax` выше.
	/*+0x68*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 OptionGroup0 = 0;
	/*+0x74*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 OptionGroup1 = 0;
};

// ★★★ ОДНА ПОЗИЦИЯ СОДЕРЖИМОГО КОРОБКИ/ПАКЕТА (волна 2 «применение предметов», канал 6096/6097).
//   Источник — `PackAgeTable.BIN` (в порту `DT_packagetable`, 5228 записей по 56 б).
//   Связка «код коробки → состав»: `ItemRecord.DurationOrEnchant` (+0xD8) = ИНДЕКС строки таблицы,
//   резолв 1:1 `charDataFindPackAgeRecord@0xDB14F0` (entity_char.c:48519) — прямая адресация
//   `base + 14*(index − base[0].id)`, индексы сплошные с нуля.
USTRUCT(BlueprintType)
struct FAsdaPackageEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 ItemCode = 0;   // код выдаваемого предмета
	UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32 Amount   = 0;   // количество
};

// Запись изображения ImgRecord (300 байт, item_img_table.BIN). Поля = смещения из
// чтений эталона (CCharacter_SetPartsModel_* / тултип): 4 строковых буфера по 32 байта.
USTRUCT(BlueprintType)
struct FAsdaImgRecord
{
	GENERATED_BODY()

	/*+0x04*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") FString MeshMale;    // .jbm база (муж) — модель части/предмета
	/*+0x24*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") FString TexMale;     // .TEX база (муж)
	/*+0x44*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") FString MeshFemale;  // .jbm база (жен)
	/*+0x64*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") FString TexFemale;   // .TEX база (жен)
	/*+0xD0*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   DropSoundCat = -1; // +208: категория звука ДРОПА (0..11 → Sod_drop_*; реверс loot_system_re.md §3)
	/*+0xD4*/ UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   ImgSubType = 0; // +212: суффикс-буква модели (a..l) для оружия
	/*+0x120*/UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   SubTypeMale = 0;   // +288: подтип (муж) — helmet/hair режим
	/*+0x124*/UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   SubTypeFemale = 0; // +292: подтип (жен)
	// Иконка (ячейка атласа). UV = ячейка→пиксель→/размер атласа; small→32px/16кол, иначе 64px/8кол.
	/*+0x84 */UPROPERTY(BlueprintReadOnly, Category="AsdaItem") FString IconAtlasMale;    // атлас-текстура иконки (муж)
	/*+0xA4 */UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   IconCellMale = 0; // индекс ячейки (муж)
	/*+0xA8 */UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   IconSmallFlag = 0;// 1→32px/16кол, иначе 64px/8кол
	/*+0xAC */UPROPERTY(BlueprintReadOnly, Category="AsdaItem") FString IconAtlasFemale;  // атлас-текстура иконки (жен)
	/*+0xCC */UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   IconCellFemale = 0;// индекс ячейки (жен)
};

// Разрешённая иконка предмета: атлас + пиксельный прямоугольник ячейки (для UV-подрегиона).
USTRUCT(BlueprintType)
struct FAsdaItemIcon
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="AsdaItem") FString Atlas;         // имя атлас-текстуры (без пути)
	UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   CellX = 0;     // пиксель X левого-верха ячейки
	UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   CellY = 0;     // пиксель Y
	UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   CellSize = 64; // 32 или 64 (px)
	// ★ Аудит №4 (A3): два поля ДОБАВЛЕНЫ, чтобы общая точка отдавала ВСЁ, что было у копии в
	//   плагине-браузере (он держал у себя `Cell` и `Cols`). Рантайм их не читает — пиксельного
	//   прямоугольника ему хватает, — но без них плагину пришлось бы считать сетку заново.
	UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   Cell = 0;      // индекс ячейки как есть (у самоцвета 0 — ячеек нет)
	UPROPERTY(BlueprintReadOnly, Category="AsdaItem") int32   Cols = 8;      // колонок в сетке: 16 (small) / 8 (обычная) / 1 (самоцвет)
	bool IsValid() const { return !Atlas.IsEmpty(); }
};

// ★★★ ЕДИНАЯ ФОРМУЛА «ЗАПИСЬ → ИКОНКА» — ТРИ ВЕТКИ ЭТАЛОНА В ОДНОМ МЕСТЕ (аудит №4, A3).
//
// ЧТО БЫЛО. Раскладка иконки жила ДВАЖДЫ: `UAsdaItemTableSubsystem::GetItemIcon` брала
//   ТИПИЗИРОВАННЫЕ колонки записи img, а редакторский плагин `AsdaItemBrowser` читал те же поля
//   ТРЕТЬИМ способом — сырым `DataHex` по зашитым у себя смещениям 132/164/168/172/204
//   (`namespace ImgOff`). Один факт — две несвязанные раскладки: перегенерация DataTable развела
//   бы их МОЛЧА, а комментарий плагина «1:1 из UAsdaItemTableSubsystem (namespace ImgOff)» уже
//   устарел — такого namespace в игровом модуле нет.
//
// ★ ПОЧЕМУ СВОБОДНЫЕ ФУНКЦИИ, А НЕ МЕТОД ПОДСИСТЕМЫ. `UAsdaItemTableSubsystem` — подсистема
//   `UGameInstance`, а браузер предметов работает В РЕДАКТОРЕ, где GameInstance нет вовсе.
//   Поэтому выносится ЧИСТАЯ функция «строка → иконка»: она одинаково доступна и рантайму,
//   и редакторскому плагину, и не требует ни подсистемы, ни мира.
//
// ★ ШАБЛОН, А НЕ ПЕРЕГРУЗКИ: одни и те же типизированные колонки лежат в ДВУХ структурах —
//   `FAsdaImgRecord` (рантайм-кэш подсистемы) и `FAsdaImgRow` (строка DataTable, её читает плагин).
//   Имена полей у них совпадают 1:1, поэтому шаблон покрывает обе без приведения типов.
namespace AsdaItemIcon
{
	// ── Ветка САМОЦВЕТА (`CInventoryUI_RefreshStoragePage@0xD01730`, диапазон g_SowelCodeRangeLo..Hi) ──
	//   `strcpy(dst, SowelRecord->iconPath)` → `DrawTextureRect(ctrl, tex, 0, 0, 32, 32)`:
	//   атлас = IconPath целиком, ЛЕВЫЙ-ВЕРХНИЙ квадрат 32×32 (а не вся текстура), сетки нет.
	inline FAsdaItemIcon FromSowelIconPath(const FString& IconPath)
	{
		FAsdaItemIcon Icon;
		Icon.Atlas = IconPath;
		Icon.Cell = 0;
		Icon.CellX = 0; Icon.CellY = 0;
		Icon.CellSize = 32;
		Icon.Cols = 1;                        // сетки нет — рисуется один квадрат из угла текстуры
		return Icon;
	}

	// ── Ветка ВТОРОЙ ТАБЛИЦЫ / спец-предметов (`CInventoryUI_RefreshBagIconsPage@0xD00990`) ──
	//   `Grid_IndexToCellOffset8x64(Table2Record->iconCell)` → `DrawTextureRect(..., 64, 64)`:
	//   сетка ЖЁСТКО 8 колонок × 64 px (флага «small» у этой таблицы нет).
	inline FAsdaItemIcon FromSpecialIcon(const FString& IconPath, int32 IconCell)
	{
		FAsdaItemIcon Icon;
		Icon.Atlas = IconPath;
		Icon.Cell = IconCell;
		Icon.CellSize = 64;
		Icon.Cols = 8;
		Icon.CellX = 64 * (IconCell % 8);     // Grid_IndexToCellOffset8x64
		Icon.CellY = 64 * (IconCell / 8);
		return Icon;
	}

	// ── Ветка ОБЫЧНОГО предмета: атлас/ячейка из записи img (`GetImgOptionPtr/Value/Mask`) ──
	//   `IconSmallFlag == 1` ⇒ ячейка 32 px и 16 колонок, иначе 64 px и 8 колонок.
	//   TImg — `FAsdaImgRecord` (рантайм) либо `FAsdaImgRow` (строка DataTable): поля одноимённые.
	template<typename TImg>
	FAsdaItemIcon FromImg(const TImg& Img, bool bMale)
	{
		FAsdaItemIcon Icon;
		Icon.Atlas = bMale ? Img.IconAtlasMale : Img.IconAtlasFemale;
		Icon.Cell  = bMale ? Img.IconCellMale  : Img.IconCellFemale;
		const bool bSmall = (Img.IconSmallFlag == 1);
		Icon.CellSize = bSmall ? 32 : 64;
		Icon.Cols     = bSmall ? 16 : 8;
		Icon.CellX = Icon.CellSize * (Icon.Cell % Icon.Cols);   // пиксель левого-верха (эталон Grid_IndexToCellOffset)
		Icon.CellY = Icon.CellSize * (Icon.Cell / Icon.Cols);
		return Icon;
	}
}

UCLASS()
class ASDAREBORN_API UAsdaItemTableSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Резолв записи предмета по itemCode. nullptr если код вне диапазона/не загружено.
	const FAsdaItemRecord* GetItem(int32 ItemId) const;

	// ★ ВЕСЬ КЭШ `item_list` в порядке файла (он же порядок по `ItemCode`).
	//   ЗАЧЕМ: эталон в нескольких местах ПРОХОДИТ ВСЮ таблицу — например
	//   `uiPopulateItemComposeDialog@0xB81E70` (панель покупки магазина бустеров) собирает
	//   «варианты» предмета циклом `for (i = g_ItemCodeFirst; i < count + g_ItemCodeFirst; ++i)`
	//   с отбором по `itemSubType`/`setCode`. Без этой точки каждый такой вызывающий заводил бы
	//   свой перебор кодов через `GetItem` (закон `port-shared-points`).
	const TArray<FAsdaItemRecord>& GetAllItems() const { return Items; }

	// Резолв записи изображения по imgIndex. nullptr если вне диапазона.
	const FAsdaImgRecord* GetImgRecord(int32 ImgIndex) const;

	// ★ Категория звука падения предмета (0…11) — индекс в таблице `Sod_drop_*`
	//   (эталон `CItemManager_InitDropSounds@0x9C4610`, std::string[12] @0x290863C).
	//   Берётся из `imgRecord.DropSoundCat` (+0xD0) у обычных предметов и из `sowelRecord +0xAA`
	//   у самоцветов. −1 = категории нет (код не резолвится или запись битая).
	//   ⚠ ОДНА точка на весь порт: этим же значением пользуются и наземный лут, и выброс из сумки —
	//   вторая копия резолва означала бы разный звук у одного и того же предмета.
	int32 GetDropSoundCat(int32 ItemCode) const;

	// Имя базового меша (.jbm без расширения) для предмета: itemId -> ItemRecord.ImgIndex ->
	// ImgRecord.MeshMale/MeshFemale (bMale выбирает пол). Пусто если не найдено.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	FString GetItemMeshName(int32 ItemId, bool bMale = true) const;

	// Имя базовой иконки/текстуры (.TEX без расширения). Пусто если не найдено.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	FString GetItemIconName(int32 ItemId, bool bMale = true) const;

	// ★ Имя МОДЕЛЬНОЙ ТЕКСТУРЫ (ImgRecord.TexMale/TexFemale, .TEX без расширения) — ЭТАЛОННАЯ текстура
	//   предмета. КРИТИЧНО: НЕ совпадает с текстурой, зашитой в .jbm (там бывает чужая/мусор), и один меш
	//   (MeshMale) шарится разными предметами с РАЗНЫМИ TexMale (рекрас). Движок ставит именно TexMale.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	FString GetItemTexName(int32 ItemId, bool bMale = true) const;

	// ★★★ ОТКАТ ПРИМЕНЕНИЯ РАСХОДНИКА. Порт гейта из `hotbarPushItemCooldown@0xE48FD0`:
	//   откат ставится, только если `itemSubType ∈ {15, 16}` (свитки оживления/возвращения — им
	//   `linkCode == 0`) ЛИБО `linkCode ∈ {7, 8, 200, 201}` (мгновенные и по времени HP/MP-зелья);
	//   длительность — `u16 @0xD0`, ноль ⇒ отката нет (эталон в этом случае выходит из функции).
	//   Возвращает false, если записи нет, гейт не прошёл или длительность нулевая.
	//   `OutGroup` — `i32 @0xCC`, серверная группа отката (`CheckSpendItemCoolTime@0x4DA9F0`).
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool GetItemCooldown(int32 ItemId, int32& OutGroup, int32& OutDurationMs) const;

	// ══════════ ★★★ КОРОБКИ И ПАКЕТЫ (канал 6096 `OP_RECV_OPEN_PACKAGE_ITEM` → 6097) ══════════
	//   Гейт «это коробка» — ровно эталонный (`itemHasNoEmptySowelSlots@0xDAAAB0`,
	//   item_inventory.c:53612-53618; `itemPackAgeToggleSendAction@0xDAB6E0`, :53957-53959):
	//   запись в `item_list` есть, `itemSubType ∈ {200, 201}`, и по `DurationOrEnchant` находится
	//   строка `packagetable`. В данных этому подходит 5 045 кодов (5 044 подтипа 200 + 1 подтип 201),
	//   у всех `itemType == 11` и `linkCode == 0` — самый массовый применяемый класс.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool IsPackageItem(int32 ItemId) const;

	// ★★★ СОСТАВ КОРОБКИ — РОВНО 5 ПОЗИЦИЙ (пустые отброшены).
	//   ⚠⚠ ЛОВУШКА РАСКЛАДКИ, ПРОВЕРЕНА ДВУМЯ МЕТОДАМИ. В ФАЙЛЕ запись 56 б лежит ПАРАМИ
	//   `(код, количество)`: +4/+8, +12/+16, +20/+24, +28/+32, +36/+40, +44/+48, +52 резерв —
	//   именно так её разобрал импортёр (колонки `Item0..Item5` / `Qty0..Qty5`).
	//   Но загрузчик оригинала `CCharData_LoadPackAgeTable@0xDB1590` (file_vfs.c:1227-1245)
	//   ПЕРЕКЛАДЫВАЕТ запись в SoA: `dst+4..+20` = коды (src +4/+12/+20/+28/+36),
	//   `dst+24..+40` = количества (src +8/+16/+24/+32/+40) — поэтому оба потребителя эталона
	//   (`stampWnd_PopulateItemSlots@0xDAB0F0` и `itemFindSowelSlots@0xDAACF0`) читают
	//   `rec[i+1]` как КОД, а `rec[i+6]` как КОЛИЧЕСТВО. Первый прочтённый вариант («клиент читает
	//   код/кол-во вперемешку — дефект») оказался ЛОЖНЫМ и снят чтением самого загрузчика
	//   (закон `false-negatives-method`).
	//   ⚠ ШЕСТАЯ ПАРА (`Item5`/`Qty5`, файловые +44/+48) НЕ ВЫДАЁТСЯ НИКОГДА: загрузчик копирует
	//   её как есть в +44/+48, но ни один потребитель туда не смотрит, а серверная
	//   `PackageItem_Table.txt` (запись 34 б = id + 5 пар) шестой пары не имеет физически.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool GetPackageContents(int32 ItemId, TArray<FAsdaPackageEntry>& OutEntries) const;

	// ══════════ ★★★ ГАЧА-ПРЕДМЕТ («бустер», канал 5468 → 5469) ══════════
	//   Гейт эталона `itemBoosterWnd_Open@0xD1F4A0` (item_inventory.c:43043): по
	//   `ItemRecord.DurationOrEnchant` ищется строка `booster_list` (`Booster_list.BIN`,
	//   запись 284 б) через `CItemManager_GetBoosterRecord@0xD1F3F0` — ЛИНЕЙНЫЙ поиск по полю +0.
	//   Нет строки ⇒ предмет обычный. Возвращает индекс строки в `OutRowKey` (ключ таблицы).
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool IsBoosterItem(int32 ItemId) const;

	// ★ Превью барабана гачи: коды 35 возможных наград из строки `booster_list`
	//   (эталон `craftWnd_populateBoosterList@0xD1FBB0` — все слоты; `boosterWindowPopulate@0xB82D90`
	//   — первые 5, они же «редкие»). Веса в записи есть, но клиент оригинала их НЕ читает.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool GetBoosterPreview(int32 ItemId, TArray<int32>& OutItemCodes) const;

	// ★ Те же 35 позиций строки `booster_list`, но БЕЗ фильтрации нулей: 0…4 — «особые» призы,
	//   5…34 — «обычные». Позиция значима для окна «Вид» 15600 (см. реализацию).
	bool GetBoosterSlotsRaw(int32 ItemId, TArray<int32>& OutSlots35) const;

	// Резолв строк двух таблиц, адресуемых полем `DurationOrEnchant` (см. комментарии выше).
	//   Публичны, потому что тот же резолв нужен инвентарю для лога/подсказки состава.
	const FAsdaPackagetableRow* FindPackageRow(int32 PackageIndex) const;
	const FAsdaBoosterListRow*  FindBoosterRow(int32 BoosterCode) const;

	// Иконка предмета как ЯЧЕЙКА АТЛАСА (эталон): itemId → img → атлас+ячейка→пиксельный rect.
	//   small-флаг: 1→32px (16 колонок), иначе 64px (8 колонок). Ячейка: x=size*(cell%cols), y=size*(cell/cols).
	//   ★★★ [ОРИГИНАЛ] 08.11.2026 (баг владельца «на броне нет женского значка»): у обычного предмета эталон
	//   `CItemManager_GetImgOptionPtr/Value@0x9C3DF0/0x9C3F40` берёт ЖЕНСКИЙ атлас/ячейку только если у строки
	//   `item_list[imgIndex]` поле +0x58 (`ClassRestriction`) не 0; при 0 — всегда мужской, что бы ни попросили.
	//   Здесь это правило (ключ `asda.Item.IconUnisexMale`); bMale — ЯВНЫЙ пол. Пол СВОЕГО персонажа — `GetItemIconSelf`.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	FAsdaItemIcon GetItemIcon(int32 ItemId, bool bMale = true) const;

	// ★★★ [ОРИГИНАЛ] 08.11.2026: иконка в атласе ПОЛА СВОЕГО ПЕРСОНАЖА — так рисует эталон: 305 из 313 вызовов
	//   `GetImgOptionPtr/Value` передают `g_ItemImgOptionVariant` (байт 0x2906287 = `g_aCharSelectData`+0x1B =
	//   `LocalPlayer`+0x41C+0x1B = `CCharacter::gender` +0x437, блок 1017 копируется `qmemcpy(LocalPlayer+1052, …, 0x44)`).
	//   Исключение эталона — окно осмотра чужого персонажа (`uiRefreshItemOptionSlots@0xDA77E0`: пол ТОГО персонажа).
	FAsdaItemIcon GetItemIconSelf(int32 ItemId) const { return GetItemIcon(ItemId, IsSelfIconMale()); }

	// Пол атласа иконок своего персонажа: true = мужской. Ключ `asda.Item.IconSelfGender` (1 = эталон: пол из 1017,
	//   иначе общий `ResolveSelfGender`; 0 = прежний порт: всегда мужской атлас).
	bool IsSelfIconMale() const;

	// Пол иконки «по коду вызывающего»: -1 = свой персонаж (`IsSelfIconMale`), 0 = женский, 1 (и прочее) = мужской.
	//   Нужен общим точкам, у которых пол приходит параметром (`AsdaUi::ApplyItemIcon`, сетка ячеек).
	bool ResolveIconMale(int32 IconSex) const { return IconSex < 0 ? IsSelfIconMale() : (IconSex != 0); }

	// itemType (part-slot) предмета, -1 если не найдено.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	int32 GetItemType(int32 ItemId) const;

	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	int32 GetItemCount() const { return Items.Num(); }

	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool IsLoaded() const { return bLoaded; }

	// ── DataTable-слой (нативные UE DataTable, браузятся/ищутся в редакторе + BP) ──

	// Число загруженных DataTable item-БД.
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	int32 GetTableCount() const { return DataTables.Num(); }

	// Резолв загруженного DataTable по имени таблицы (напр. "sowell_list", "pricetable").
	// nullptr если таблица не загружена. В BP далее — стандартный узел GetDataTableRow.
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	UDataTable* GetDataTable(FName TableName) const;

	// Общий доступ: имя строки (RowName) DataTable по числовому ключу таблицы (id/code).
	// Работает для таблиц с ключевым полем (sowell_list/title_list/pettable/pricetable/item_tooltip).
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool FindRowName(FName TableName, int32 Id, FName& OutRowName) const;

	// ── Типизированные геттеры (bool + out-строка) ────────────────────────────
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetSowell(int32 Id, FAsdaSowellRow& OutRow) const;

	// ★ Текст подсказки самоцвета (`sowell_tooltip`, эталон `sowelTable_FindByCode` → строка на +4).
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetSowellTooltip(int32 Id, FAsdaSowellTooltipRow& OutRow) const;

	// ★★★ Байт СЫРОЙ записи `sowell_list` из колонки `DataHex` (−1 при битой/короткой строке).
	//   Нужен полям без отдельной колонки: +38 вес (`tooltipParam`), +47 третий слот «Место в
	//   снаряжении», +66 флаг «не передаётся». Смещения сняты с бинаря — см. .cpp.
	static int32 SowelRawByte(const FAsdaSowellRow& Row, int32 Off);

	// ★★★ ВТОРАЯ ТАБЛИЦА ПРЕДМЕТОВ (`special_itemlist`, эталон `CItemManager_GetTable2Record@0x9C43A0`).
	//   Отдельное пространство кодов `g_Table2CodeFirst..g_Table2CodeLast` (в данных = 1…558), НЕ
	//   пересекается с `item_list` (20551…79428). Такие записи РЕАЛЬНО лежат в сумке (контейнер 1):
	//   живой лог хотбара 5059 — «подтип=1 … id=461/123/500/462 → иконка НЕТ».
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetSpecialItem(int32 Code, FAsdaSpecialItemlistRow& OutRow) const;

	// ★★★ Байт СЫРОЙ записи `special_itemlist` (300 б) из колонки `DataHex` (−1 при битой строке).
	//   База колонки проверена: `DataHex[0..3] == Code`, `u16@68 == RequiredLevel`,
	//   `i32@80 == SubType`, `i32@96 == CooldownBase`, `u8@117 == LinkGroupId`.
	static int32 SpecialItemRawByte(const FAsdaSpecialItemlistRow& Row, int32 Off);

	// ★★★ РЕЖИМ СПИСАНИЯ ПРЕМИУМ/ФУНКЦИОНАЛЬНОГО ПРЕДМЕТА — байт записи +0x57 (87).
	//   Эталон `netParseInventorySlotUpdate@0xF28B20` (network.c:36919-36936) — общий хвост ВСЕХ
	//   веток ответа 5451, именно он и списывает предмет:
	//       1 ⇒ uiInventoryRemoveItemAndRebuild + OnRecv_RemoveSlot           (расход ВСЕГДА)
	//       2 ⇒ то же, но OnRecv_RemoveSlot только при `*(int*)(запись+13) <= 0` (стопка кончилась)
	//       иное ⇒ слот не трогаем (транспорт 3, спутник 4, прочее 0)
	//   ⚠ СМЕЩЕНИЕ 0x57, А НЕ 57. Имя эталона `flag_57` — ШЕСТНАДЦАТЕРИЧНОЕ, в отличие от соседних
	//   `flag_101`/`flag_102`/`type_103` (те десятичные, asm `8a4866`/`8a4867`). Доказано двумя
	//   методами: (а) бинарь `0xF28B7B: 8a 48 57` = `mov cl,[eax+0x57]`; (б) данные — по смещению 57
	//   у ВСЕХ 558 записей ноль (ветка была бы мёртвой), а по 87 осмысленное распределение:
	//   2→290, 3→217 (из них 214 — весь транспорт subType 15), 1→40, 0→6, 4→5 (спутники subType 25).
	static int32 SpecialItemConsumeMode(const FAsdaSpecialItemlistRow& Row)
	{
		return SpecialItemRawByte(Row, 0x57);
	}

	// Границы кодовых пространств (эталон: g_Table2CodeFirst/Last, g_SowelCodeRangeLo/Hi —
	//   первая и последняя записи соответствующей таблицы).
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool IsSpecialItemCode(int32 Code) const { return SpecialCodeFirst > 0 && Code >= SpecialCodeFirst && Code <= SpecialCodeLast; }

	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool IsSowelCode(int32 Code) const { return SowelCodeFirst > 0 && Code >= SowelCodeFirst && Code <= SowelCodeLast; }

	// Категория предмета (эталон `itemGetCategoryById@0xECA700`):
	//   3 — надеваемое (itemType 0..4 / 8..9 / 13), 1 — прочее из item_list,
	//   2 — самоцвет (sowell_list), 4 — запись second-таблицы, 0 — код неизвестен.
	//   ⚠ Именно по «== 1» вкладка «Расходники» решает, рисовать ли иконку и счётчик.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	int32 GetItemCategory(int32 Code) const;

	// Порт `itemIsWeaponOrEquipClass@0xECA4D0` (item_inventory.c:64514): запись item_list есть и
	//   `itemType <= 4u` (БЕЗЗНАКОВО) или 8…9. Общая точка (волна 9 заточки): её зовут подсчёт
	//   `UAsdaInventorySubsystem::CountItemById` и окно 78200 — своих копий условия не держим.
	bool IsWeaponOrEquipClass(int32 Code) const;

	// ★★★ ПОДСКАЗКА СПЕЦ-ПРЕДМЕТА — вторая, ОТДЕЛЬНАЯ таблица `SpecialItem_TooltipList.BIN`
	//   (`specialitem_tooltiplist`, запись 252 б: 16 MD5 + 236 данных). НЕ путать со
	//   `special_itemlist` (316 б): у них разный шифр и разное правило кода.
	//   Загрузчик `itemLoadSpecialTooltipTable` (item_inventory.c:63708-63722):
	//     `id = idИзФайла − 36`, строки на +4 и +36 расшифровываются «−1 побайтово»,
	//     а **id ПЕРВОЙ записи принудительно = 1** (`**(_DWORD **)… = 1;`), после чего
	//     `g_SpecialItemTooltipFirstId/LastId` берутся из уже исправленных значений.
	//   Резолв `itemGetTooltipById@0xEC73B0` — ПОЗИЦИОННЫЙ, а не по ключу:
	//     `if (code >= First && code <= Last) return base + 236*(code − First);`
	//   Мы воспроизводим ровно это (индекс = code − FirstId в порядке файла).
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetSpecialItemTooltip(int32 Code, FAsdaSpecialitemTooltiplistRow& OutRow) const;

	// Границы кодов таблицы подсказок спец-предметов (`g_SpecialItemTooltipFirstId/LastId`).
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool IsSpecialTooltipCode(int32 Code) const
	{ return SpecialTooltipFirstId > 0 && Code >= SpecialTooltipFirstId && Code <= SpecialTooltipLastId; }

	// ★★★ ДИАПАЗОН «Эффект ЖК» shell-предмета ПО УРОВНЮ ПЕРСОНАЖА.
	//   Эталон `inventoryFindMatchingStackSlot@0xD34260` ищет строку в таблице
	//   `PremiumLevelTable.bin` (загрузчик `PremiumLevelTable_Load@0xD34100`, header 20 б + N×28 б):
	//     level = min(уровень игрока, pricetable[11]);
	//     itemType == 9 → совпасть (Level, ItemType, ItemSubType, ItemGrade);
	//     иначе         → совпасть (Level, ItemType, UsageCategory, ItemGrade);
	//     найдено ⇒ min = строка[+20], max = строка[+24].
	//   Возвращает false, когда эталон вернул бы 0 (нет записи предмета / нет строки pricetable /
	//   ни одна строка не подошла) — вызывающий обязан оставить базы `gap_88`.
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool FindPremiumStatRange(int32 ItemCode, int32 PlayerLevel, int32& OutMin, int32& OutMax) const;

	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetTitle(int32 Id, FAsdaTitleRow& OutRow) const;

	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetPet(int32 Id, FAsdaPetRow& OutRow) const;

	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetPrice(int32 ItemCode, FAsdaPriceRow& OutRow) const;

	// ★ 25.09.2026 (заточка, окно 20000). Строка `UpgradeTable.BIN` по коду вида свитка — порт
	//   `CItemManager_GetEventRecord@0x9C3BC0` (item_inventory.c:1579): запись = база + 28·(код − код[0])
	//   с проверкой диапазона [код[0] … код[последний]]. Ключ — `EventId` (коды 70…101). Имя эталона
	//   «EventRecord» — наследие IDA; по смыслу это таблица свитков заточки (`gear_upgrade_re_report.md` §7.4).
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetUpgradeTable(int32 EventId, FAsdaUpgradetableRow& OutRow) const;

	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetItemTooltip(int32 ItemCode, FAsdaItemTooltipRow& OutRow) const;

	// Диапазон значения опции по RowId (loose ItemOptionValue.BIN → itemoptionrange).
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetItemOptionRange(int32 RowId, FAsdaItemoptionrangeRow& OutRow) const;

	// Все диапазоны значений опций одного набора (GroupId = optionSetId предмета).
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	void GetItemOptionRangesByGroup(int32 GroupId, TArray<FAsdaItemoptionrangeRow>& OutRows) const;

	// ★★★ B1/B2 (аудит `code_dedup_audit_2.md`): ЕДИНАЯ точка доступа к `SkillCoinTable.bin`
	//   (`DT_SkillCoin`, 1255 записей, ключ = SkillId; три числа под иконкой слота навыка).
	//   БЫЛО: таблицу грузили ДВА независимых потребителя собственным `LoadObject` мимо менеджера
	//   (книга навыков и хранитель души), и её строки лежали в памяти ДВУМЯ копиями — при правке
	//   данных копии разъезжались. Теперь загрузка одна (см. `GExtraTables` в .cpp), а копия строк —
	//   только у того, кому нужен свой кэш.
	//   ⚠ Таблица лежит ВНЕ каталога ItemDB, поэтому её нет в авто-манифесте `AsdaItemDbManifest.h`
	//   (он генерируется по каталогу); регистрация ручная и переживает перегенерацию манифеста.
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetSkillCoin(int32 SkillId, FAsdaSkillCoinRow& OutRow) const;

	// ★ Аудит №3 (A14): подсказка ЭМОЦИИ (`emotiontip`, запись 265 б, эталон `EmotionTip_Load@0xC529D0`).
	//   Ключевого поля у таблицы НЕТ — ни в данных (колонок id нет вовсе), ни в оригинале: эталон
	//   адресует массив ПО ИНДЕКСУ записи (`emotionTip_lookupByCode@0xC52870` считает индекс из кода).
	//   Поэтому резолв ПОЗИЦИОННЫЙ: RecordIdx = RowName = порядковый номер записи в файле.
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetEmotionTip(int32 RecordIdx, FAsdaEmotiontipRow& OutRow) const;

	// ★ Аудит №3 (A3/A14): запись `skilltooltip` (504 б) — имя/текст подсказки навыка и сырой рекорд
	//   в `DataHex` (иконка = ASCII-поле +36). Ключ = `Id`; в данных `Id` совпадает с порядковым
	//   номером записи (0..1254 сплошь), поэтому и прежний позиционный `FindRow` по номеру строки,
	//   и резолв по ключу дают ОДНУ И ТУ ЖЕ строку — поведение сохранено 1:1.
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetSkillTooltip(int32 SkillCode, FAsdaSkilltooltipRow& OutRow) const;

	// ★ Аудит №3 (A3): строка `stateinfotip` — имя/иконка/ячейка атласа/текст состояния (баффа).
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetStateInfoTip(int32 Id, FAsdaStateinfotipRow& OutRow) const;

	// ★ Аудит №3 (A3): то же для состояний СОУЛМЕЙТА (`soulstateinfotip`, раскладка совпадает).
	UFUNCTION(BlueprintCallable, Category="AsdaItem|DataTable")
	bool GetSoulStateInfoTip(int32 Id, FAsdaSoulstateinfotipRow& OutRow) const;

	// ── Имя предмета ──────────────────────────────────────────────────────────
	// Имя берётся ИЗ ТАБЛИЦЫ DT_item_list (колонка ItemName). Там уже лежат русские имена
	// оригинального RU-клиента, а для 42196 предметов, у которых RU-клиент даёт «-»,
	// имена восстановлены из EN/AR/KO-сборок на этапе импорта (Tools/asda_fill_item_names.py).
	// Для 1025 предметов имени нет ни в одном источнике — вернётся «-» как в оригинале.
	// ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — на ЯЗЫКЕ СЕССИИ (`FAsdaLang`, оверлей `item_list`, ключ
	//   `<_row>.ItemName`; самоцвет/спец-предмет — через свои геттеры). `FAsdaItemRecord::ItemName` из `GetItem`
	//   остаётся РУССКИМ (кэш) — показывать игроку только через этот геттер. Так же все типизированные геттеры
	//   строк ниже (`GetSowell`, `GetTitle`, `GetPet`, `GetSkillTooltip`, …) отдают КОПИЮ с текстом на языке сессии
	//   (`AsdaLangRows::Localize`); строки самих DataTable не меняются.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	FString GetItemName(int32 ItemId) const;

	// Есть ли у предмета осмысленное имя (не «-» и не пусто).
	// Оставлено под прежним именем ради совместимости вызовов из Blueprint.
	UFUNCTION(BlueprintCallable, Category="AsdaItem")
	bool HasRuName(int32 ItemId) const;

private:
	// Построение рантайм-кэша Items/ImgTable из DataTable-ассетов (DT_item_list,
	// DT_item_img_table). Единственный источник данных предметов в игре.
	bool BuildItemCacheFromDataTables();

	// Дочитать из сырой записи (колонка DataHex, 248 б) поля, которых нет отдельными колонками:
	// DurabilityMax(+0x68), StatBaseHi(+0x88), StatBaseLo(+0x8A). Нужны тултипу предмета.
	static void ReadTooltipFieldsFromRawHex(const FString& Hex, FAsdaItemRecord& Rec);

	// Загрузка всех DataTable по манифесту (LoadObject) + построение id-индексов.
	void LoadDataTables();
	// Построить индекс id->RowName для таблицы по её ключевому int32-полю.
	template<typename TRow>
	void BuildKeyIndex(const TCHAR* TableName, int32 TRow::* KeyMember, TMap<int32, FName>& OutIndex);
	// Позиционный кэш таблицы (индекс = RowName = порядок записи в оригинальном файле).
	//   Нужен там, где ЭТАЛОН адресует арифметикой по индексу, а не по ключу.
	template<typename TRow>
	void BuildOrderedCache(const TCHAR* TableName, TArray<TRow>& OutRows);

	// Имя таблицы -> загруженный DataTable-ассет.
	// ★★★ UPROPERTY ОБЯЗАТЕЛЕН = GC-рут. Без него TObjectPtr в не-рефлексируемом поле НЕ держит ссылку:
	//   в РЕДАКТОРЕ ассеты живут за счёт content browser/asset registry и всё работает, а в СОБРАННОМ БИЛДЕ
	//   первый же GC (~60 с после старта) собирает все 51 DataTable (+231 МБ) → указатели в карте висячие →
	//   первое же обращение (GetRowNames/FindRow, напр. ResolveHairMeshName при показе персонажа в слоте)
	//   читает мусорный RowMap.Num() → «Trying to resize TSet to an invalid size of 1844674407365...» → краш.
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UDataTable>> DataTables;
	// Ключевые индексы (id/code -> RowName) для типизированных геттеров.
	TMap<FName, TMap<int32, FName>> KeyIndex;

	// Рантайм-кэш предметов из DT_item_list (индекс = RowName = порядок записи в оригинале,
	// он же imgIndex; порядок совпадает с сортировкой по ItemCode).
	TArray<FAsdaItemRecord> Items;
	// Рантайм-кэш изображений из DT_item_img_table (индексируется imgIndex напрямую).
	TArray<FAsdaImgRecord> ImgTable;
	// itemCode -> индекс в Items (для GetItem без арифметики диапазона; но храним и First/Last).
	TMap<int32, int32> CodeToIndex;
	int32 ItemCodeFirst = 0;  // g_ItemCodeFirst = Items[0].ItemCode
	int32 ItemCodeLast  = 0;  // g_ItemCodeLast  = Items[last].ItemCode
	// ★ Границы второго и третьего кодовых пространств (эталон `CItemManager_LoadSpecialItemBin`
	//   стр. 1035-1036: `g_Table2CodeFirst = table[0].code`, `g_Table2CodeLast = table[last].code`;
	//   `CItemManager_LoadSowelBin` стр. 835-836 — то же для самоцветов).
	int32 SpecialCodeFirst = 0;  // g_Table2CodeFirst   (special_itemlist)
	int32 SpecialCodeLast  = 0;  // g_Table2CodeLast
	int32 SowelCodeFirst   = 0;  // g_SowelCodeRangeLo  (sowell_list)
	int32 SowelCodeLast    = 0;  // g_SowelCodeRangeHi

	// ★ Подсказки спец-предметов В ПОРЯДКЕ ФАЙЛА: эталон адресует их арифметикой
	//   `base + 236*(code − FirstId)`, поэтому позиционный кэш обязателен (ключевой индекс
	//   строится дополнительно — им пользуется общий `FindRowName` и Blueprint).
	TArray<FAsdaSpecialitemTooltiplistRow> SpecialTooltips;
	int32 SpecialTooltipFirstId = 0;  // g_SpecialItemTooltipFirstId (загрузчик форсирует 1)
	int32 SpecialTooltipLastId  = 0;  // g_SpecialItemTooltipLastId

	// ★ PremiumLevelTable в порядке файла. Поиск в эталоне — ЛИНЕЙНЫЙ «первое совпадение»,
	//   поэтому храним массив и сканируем так же (никаких своих индексов: при дублях ключа
	//   карта вернула бы ДРУГУЮ строку).
	TArray<FAsdaPremiumleveltableRow> PremiumLevels;

	bool  bLoaded = false;
};
