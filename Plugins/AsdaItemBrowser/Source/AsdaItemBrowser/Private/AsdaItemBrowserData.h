// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemBrowserData — слой ДАННЫХ браузера предметов (редактор).
//
// ★★★ ТРИ НЕЗАВИСИМЫХ ПРОСТРАНСТВА КОДОВ (реверс `inventory-port`, `item_dedup_analysis.md`).
//   Игра адресует предметы ТРЕМЯ разными таблицами, коды между ними НЕ пересекаются, и по одной
//   таблице `GetItem()` возвращает nullptr — именно поэтому раньше браузер «не видел» коды 461/462:
//     * `item_list`         20551…79428 (58 878 строк) — обычные предметы, снаряжение, расходники;
//     * `sowell_list`        7001…10925 (3 925 строк)  — самоцветы (живые камни);
//     * `special_itemlist`      1…558   (558 строк)    — премиум/бустеры (`Table2` эталона,
//                                                        `CItemManager_GetTable2Record@0x9C43A0`).
//   У каждого пространства СВОЯ таблица описаний и СВОЙ способ адресации иконки — см. ResolveIcon.
//
// ★ ИСТОЧНИК ДАННЫХ — ТОЛЬКО АССЕТЫ ПРОЕКТА (Content/), НИКОГДА не файлы оригинальной игры:
//     * записи предметов      — /Game/AsdaAssets/Data/ItemDB/DT_item_list      (FAsdaItemRow)
//     * самоцветы            — /Game/AsdaAssets/Data/ItemDB/DT_sowell_list     (FAsdaSowellRow)
//     * спец-предметы        — /Game/AsdaAssets/Data/ItemDB/DT_special_itemlist(FAsdaSpecialItemlistRow)
//     * записи изображений    — /Game/AsdaAssets/Data/ItemDB/DT_item_img_table (FAsdaImgRow)
//     * модели                — /Game/AsdaAssets/Models/...   (StaticMesh / SkeletalMesh)
//     * текстуры и атласы     — /Game/AsdaAssets/Textures/...  (Texture2D)
//     * эффекты заточки       — /Game/AsdaAssets/Effects/BP_Fx_*  (Blueprint на AAsdaEffectActor)
//   Смысл инструмента — показать, ЧТО У НАС ИМПОРТИРОВАНО. Отсутствующий ассет НЕ подменяется
//   чтением оригинала: он помечается как «нет меша / нет текстуры / нет иконки / нет эффекта».
//
// Правила соответствия «предмет → модель/текстура/иконка/эффект» взяты из РАНТАЙМА порта
// (он же — порт эталона), но применяются к нашим ассетам:
//     * AAsdaLootActor::InitFromItemCode      — модель и текстура берутся из img-записи
//                                               (строка материала внутри .jbm — мусор, не используется);
//     * UAsdaCharAssembly::AttachPart/AttachWeapon — раскладка ассетов экипа и оружия;
//     * UAsdaItemTableSubsystem::GetItemIcon  — иконка = атлас + ячейка (small→32/16кол, иначе 64/8кол);
//     * UAsdaWorldSubsystem::BuildGlowJebName — имя эффекта свечения заточки (реверс weapon_glow_re.md §B).
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Items/AsdaItemDbRows.h"

class UDataTable;

// ── Пространство кодов предмета ─────────────────────────────────────────────
// Порядок перечисления = порядок ПОИСКА эталона (`uiInventoryOnRecvItemAcquired@0xD10E80`:
// item_list → sowell_list → special_itemlist), поэтому им же сортируем корни дерева.
enum class EAsdaItemSpace : uint8
{
	ItemList,     // item_list         20551…79428 — обычные предметы
	SowelList,    // sowell_list        7001…10925 — самоцветы
	SpecialList,  // special_itemlist      1…558   — премиум/бустеры (Table2)
	Count
};

// Короткая пометка пространства для строки списка («предм» / «камень» / «спец»).
const TCHAR* AsdaSpaceTag(EAsdaItemSpace Space);
// Имя исходной таблицы item-БД («item_list» / «sowell_list» / «special_itemlist»).
const TCHAR* AsdaSpaceTable(EAsdaItemSpace Space);
// Человеческая подпись пространства для корня дерева и карточки.
FString AsdaSpaceLabel(EAsdaItemSpace Space);

// ── Вид найденного меша предмета ────────────────────────────────────────────
enum class EAsdaBrowserMeshKind : uint8
{
	None,           // имени меша в данных нет (у предмета модели нет и в оригинале)
	Missing,        // имя есть, но ассет НЕ импортирован
	EquipSkeletal,  // Models/Skeletal/EquipParts/<n>/<n>            — части экипа (броня/шлемы)
	CharStatic,     // Models/Char/<n>/StaticMeshes/<n>              — оружие/щиты, модели транспорта
	ItemStatic      // Models/Items/<n>/StaticMeshes/<n>             — дроп-модели (расходники, камни)
};

// ── Иконка предмета: атлас + ячейка (порт GetItemIcon) ──────────────────────
// ★ Три пространства адресуют иконку ПО-РАЗНОМУ (1:1 с UAsdaItemTableSubsystem::GetItemIcon):
//   * item_list        — атлас из img-записи + ячейка, сетка 8×64 либо 16×32 (флаг small);
//   * sowell_list      — атлас = `IconPath` ЦЕЛИКОМ, рисуется левый-верхний квадрат 32×32
//                        (эталон `CInventoryUI_RefreshStoragePage@0xD01730`: DrawTextureRect(...,0,0,32,32));
//   * special_itemlist — атлас = `IconPath`, ячейка `IconCell`, сетка ЖЁСТКО 8 колонок × 64 px
//                        (эталон `CInventoryUI_RefreshBagIconsPage@0xD00990` → Grid_IndexToCellOffset8x64).
struct FAsdaBrowserIcon
{
	EAsdaItemSpace Space = EAsdaItemSpace::ItemList;
	FString Atlas;              // имя атлас-текстуры (без пути), "" = в данных атласа нет
	FString AtlasObjectPath;    // /Game/AsdaAssets/Textures/JUI/<atlas>.<atlas>
	int32   Cell = 0;           // индекс ячейки (у самоцвета всегда 0 — ячеек нет)
	int32   CellX = 0;          // пиксель левого-верха
	int32   CellY = 0;
	int32   CellSize = 64;      // 32 (small / самоцвет) или 64
	int32   Cols = 8;           // 16 (small), 8 (обычная сетка), 1 (самоцвет — вся текстура)
	bool HasAtlasName() const { return !Atlas.IsEmpty(); }
};

// ── Полный разбор предмета для правой панели/превью ─────────────────────────
struct FAsdaBrowserItemAssets
{
	EAsdaItemSpace Space = EAsdaItemSpace::ItemList;
	int32 ItemCode = 0;
	FString Name;                     // "" — имени в данных НЕТ (в оригинале там «-» либо пусто)
	bool bHasName = false;
	int32 ItemType = 0, ItemSubType = 0, ItemGrade = 0, ImgIndex = 0, ImgSubType = 0;

	FString MeshName;                 // img-запись: MeshMale/MeshFemale
	FString MeshObjectPath;           // путь найденного ассета ("" если не найден)
	EAsdaBrowserMeshKind MeshKind = EAsdaBrowserMeshKind::None;

	FString TexName;                  // img-запись: TexMale/TexFemale (эталонная текстура модели)
	FString TexObjectPath;            // "" если ассета нет

	FAsdaBrowserIcon Icon;

	// Эффекты предмета
	FString GlowJebName;              // имя .jeb свечения для выбранной заточки ("" = свечения нет)
	FString GlowBpObjectPath;         // путь BP_Fx_* ("" = BP не собран)
	FString AuraName1, AuraName2;     // доп. аура щита с +8 (ImgRecord+216/+252) — в наших данных пусто

	// ── Только special_itemlist: иконка БАФФА, который вешает предмет ──
	// Источник 1:1 с рантаймом (UAsdaEffectsSubsystem::GetTable2): атлас = поле `Field197` (+197),
	// ячейка = байт +229 сырой записи, признак наличия = байт +116 (== 2 у 356 из 558 строк).
	// Полей +116 и +229 в USTRUCT нет (попали в «дырки» раскладки) — читаем из колонки DataHex.
	FString BuffIconAtlas;
	FString BuffIconObjectPath;
	int32   BuffIconCell = 0;
	int32   BuffFlag116 = 0;

	// ── Текстовое описание и сырая строка записи (для карточки свойств) ──
	// Описание берётся из ТАБЛИЦЫ СВОЕГО ПРОСТРАНСТВА: item_tooltip / sowell_tooltip /
	// specialitem_tooltiplist ("" = записи нет).
	FString Description;
	FString DescriptionTable;         // имя таблицы, откуда взято описание (для честной подписи)
	// Строка исходной таблицы как есть (память принадлежит удержанному DataTable). Заполнен РОВНО
	// один из трёх указателей — по Space. Карточка печатает ТОЛЬКО те поля, смысл которых
	// подтверждён реверсом (RE/ida/itemrecord_fields.tsv, Docs/Port/systems/ITEM_DB_REFERENCE.md).
	const FAsdaItemRow* Row = nullptr;
	const FAsdaSowellRow* SowelRow = nullptr;
	const FAsdaSpecialItemlistRow* SpecialRow = nullptr;
};

// ════════════════════════════════════════════════════════════════════════════
// ★★★ 29.10.2026 — ТОЧНАЯ РАЗБИВКА (переделка просмотрщика, Reports/item_viewer/REPORT.md).
//   Категории — ИГРОВАЯ классификация, а не номера полей:
//     • снаряжение: «Место в снаряжении» = нотис 8569 + itemType (Шлем/Доспех/Штаны/Ботинки/Перчатки/…/Щит/Оружие —
//       та же таблица, что печатает подсказка самоцвета `CItemManager_GetSowelOptionText`); кольцо/ожерелье — подписи
//       вкладок биржи 12528/12529; вид оружия — нотис 8552 + itemSubType (секция 3 подсказки, «Удочка» 111074);
//     • расходники (itemType 11) — группы биржи: `itemTypeToCategory_44F0@0xD444F0` (подтип → вкладка) и
//       `itemGetCategoryIndexBySubtype@0xD53380` (подтипы 60…68 → материалы 12747…12755), коробки — 200/201
//       (`IsPackageItem`); самоцветы — тип бонуса (нотис 8510 + BonusStatType), спец-предметы — функция SubType.
//   Класс — `UsageCategory` записи (тот же байт, что «Профессия» подсказки) и `jobtype` (имя + родитель-архетип);
//   доступность классу — правило игры `upgradeCheckGradeReachable@0xEC9010` (`CheckGradeReachable`).
//   Уровень — RequiredLevel…RequiredLevelMax; разряд десятками — `item_getLevelTier@0xD53030` (как биржа).
//   ★ Ни одного жёсткого списка кодов: новые предметы (перенос WorldOfDreams) попадают в ветки по своим полям,
//     незнакомый тип/подтип — в ветку «Тип N» / «подтип N (напр. «…»)».
// ════════════════════════════════════════════════════════════════════════════

// Категория верхнего уровня (порядок значений = порядок в дереве).
enum class EAsdaItemCat : int32
{
	Weapon = 0, Shield, Helmet, Armor, Pants, Boots, Gloves,   // снаряжение (itemType 9, 8, 0…4)
	Ring, Necklace,                                            // аксессуары (5, 7)
	Ammo,                                                      // колчаны, заряды, наживка (10)
	Potion, HolyWater, Scroll, Material, CardPet, Box, Misc11, // расходники (11) по группам биржи
	Booster,                                                   // бустеры (13)
	OtherType,                                                 // любой иной itemType (Sub = itemType)
	Sowel,                                                     // sowell_list
	Special,                                                   // special_itemlist
	Count
};

// Как группировать дерево.
enum class EAsdaBrowserGrouping : uint8
{
	ByType,    // категория → подкатегория → предметы
	ByClass,   // архетип → профессия (требование) → категория → предметы
	ByLevel,   // разряд уровня (1~10 … 91~100, свыше) → категория → предметы
	ByGrade,   // грейд → категория → предметы
	Count
};

// Как читать фильтр класса.
enum class EAsdaJobFilterMode : uint8
{
	Exact,      // требование предмета == выбранная профессия
	Branch,     // требование предмета в ветке выбранного архетипа (1–11 / 12–22 / 23–33 / 34–39 / 40–44)
	Reachable,  // предмет МОЖЕТ надеть персонаж выбранной профессии (правило игры CheckGradeReachable)
};

// Фасеты одной записи (считаются один раз при загрузке).
struct FAsdaBrowserFacets
{
	EAsdaItemCat Cat = EAsdaItemCat::Misc11;
	int32 Sub = 0;               // ключ подкатегории внутри Cat (подтип / вид оружия / тип бонуса / функция)
	int32 Usage = -1;            // UsageCategory (0…44); −1 — у таблицы поля нет (самоцвет, спец-предмет)
	int32 ClassRestriction = -1; // сырое поле item_list +0x58 (−1 — нет)
	int32 LvMin = 0;             // требуемый уровень
	int32 LvMax = 0;             // верхняя граница (0 — не задана)
	int32 Tier = 1;              // разряд уровня `item_getLevelTier`: 1…10; 11 — уровень вне 0…100
	int32 Grade = 0;             // грейд (item_list ItemGrade / sowell GradeColorIndex / special DropColorIndex)
	bool  bPremium = false;      // премиум: item_list categoryFlag == 3 («элитный», подвал 109832); special_itemlist — всегда
};

// Фильтр дерева (все условия — И).
struct FAsdaBrowserFilter
{
	FString Text;                              // подстрока имени (RU и EN) или кода
	TOptional<EAsdaItemSpace> Space;           // таблица (не задано — все три)
	int32 Cat = -1;                            // EAsdaItemCat (−1 — любая)
	int32 Job = -1;                            // профессия 0…44 (−1 — любая)
	EAsdaJobFilterMode JobMode = EAsdaJobFilterMode::Exact;
	int32 LvFrom = 0, LvTo = 0;                // требуемый уровень в [LvFrom; LvTo] (0 — граница не задана)
	int32 CharLevel = 0;                       // предмет годится персонажу уровня N (RequiredLevel ≤ N ≤ Max); 0 — не проверять
	int32 Grade = -1;                          // грейд (−1 — любой)
	int32 Premium = -1;                        // −1 все, 0 обычные, 1 премиум
	EAsdaBrowserGrouping Grouping = EAsdaBrowserGrouping::ByType;
};

// ── Узел дерева категорий ───────────────────────────────────────────────────
// Space — корень пространства кодов (появился вместе с подключением трёх таблиц);
// CodeHit — синтетический корень «точное совпадение по коду» (собирает попадания из всех
// пространств сразу, чтобы поиск по числу не путал их между собой).
enum class EAsdaBrowserNodeKind : uint8 { Space, CodeHit, Category, SubCategory, Item };

struct FAsdaBrowserNode
{
	EAsdaBrowserNodeKind Kind = EAsdaBrowserNodeKind::Category;
	EAsdaItemSpace Space = EAsdaItemSpace::ItemList;
	FString Label;                  // подпись категории/подкатегории (у предмета не используется)
	FLinearColor LabelColor = FLinearColor::White;   // цвет подписи (ветки грейда — цветом грейда как в игре)
	bool bColored = false;          // LabelColor задан
	int32 TypeId = -1;
	int32 SubId = -1;
	int32 ItemIndex = INDEX_NONE;   // ГЛОБАЛЬНЫЙ индекс записи (FAsdaItemBrowserData::Entries)
	int32 Count = 0;                // число предметов в ветке
	TArray<TSharedPtr<FAsdaBrowserNode>> Children;
};

// ── Запись сводного списка: «пространство + строка внутри него» ─────────────
// Единый ГЛОБАЛЬНЫЙ индекс по всем трём таблицам — им адресуются дерево, иконки и превью.
struct FAsdaBrowserEntry
{
	EAsdaItemSpace Space = EAsdaItemSpace::ItemList;
	int32 RowIndex = INDEX_NONE;    // индекс в массиве своего пространства (Items/Sowels/Specials)
	int32 Code = 0;                 // код предмета в СВОЁМ пространстве
};

// ════════════════════════════════════════════════════════════════════════════
class FAsdaItemBrowserData
{
public:
	// Единственный экземпляр (живёт в модуле; таблицы держатся загруженными, пока открыт редактор).
	static TSharedRef<FAsdaItemBrowserData> Get();

	// Ленивая загрузка DataTable-ассетов. true — данные готовы.
	bool EnsureLoaded();
	bool IsLoaded() const { return bLoaded; }
	const FString& GetLoadError() const { return LoadError; }

	// ── Сводный список по ВСЕМ трём пространствам ──
	int32 Num() const { return Entries.Num(); }
	int32 NumInSpace(EAsdaItemSpace Space) const;
	// Границы кодов пространства (min/max по загруженным строкам). 0/0 — таблица не загружена.
	void CodeRange(EAsdaItemSpace Space, int32& OutFirst, int32& OutLast) const;

	const FAsdaBrowserEntry* GetEntry(int32 Index) const { return Entries.IsValidIndex(Index) ? &Entries[Index] : nullptr; }
	EAsdaItemSpace GetSpace(int32 Index) const { const FAsdaBrowserEntry* E = GetEntry(Index); return E ? E->Space : EAsdaItemSpace::ItemList; }
	int32 GetCode(int32 Index) const { const FAsdaBrowserEntry* E = GetEntry(Index); return E ? E->Code : 0; }
	// Имя из данных. Пустая строка = имени в данных НЕТ (оригинал хранит там «-» либо пусто) —
	// вызывающий обязан показать код и честную пометку, а не выдумывать название.
	FString GetName(int32 Index) const;

	// Строка исходной таблицы. Не-nullptr только если запись принадлежит соответствующему пространству.
	const FAsdaItemRow* GetItemRow(int32 Index) const;
	const FAsdaSowellRow* GetSowelRow(int32 Index) const;
	const FAsdaSpecialItemlistRow* GetSpecialRow(int32 Index) const;

	const FAsdaImgRow*  GetImg(int32 ImgIndex) const { return Imgs.IsValidIndex(ImgIndex) ? Imgs[ImgIndex] : nullptr; }

	// Поиск по коду. ★ Коды трёх пространств не пересекаются, но искать надо ВО ВСЕХ: раньше
	// метод смотрел только item_list, поэтому коды 461/462 (special_itemlist) не находились.
	int32 IndexByCode(int32 ItemCode) const;
	int32 IndexByCodeInSpace(EAsdaItemSpace Space, int32 ItemCode) const;

	// Дерево: корень на каждое пространство → категория → подкатегория → предметы.
	// Фильтр: подстрока имени ИЛИ подстрока кода (регистронезависимо). Точные совпадения по коду
	// выносятся отдельным корнем «Точное совпадение по коду N» со всеми пространствами сразу.
	// SpaceFilter не задан — показать все три пространства.
	void BuildTree(const FString& Filter, TArray<TSharedPtr<FAsdaBrowserNode>>& OutRoots,
		TOptional<EAsdaItemSpace> SpaceFilter = TOptional<EAsdaItemSpace>()) const;

	// ★ 29.10.2026: дерево по полному фильтру и выбранной группировке (см. шапку разбивки выше).
	//   Корни: «точное совпадение по коду» (если запрос — число) + ветки группировки. OutShown — сколько записей прошло.
	void BuildTreeEx(const FAsdaBrowserFilter& F, TArray<TSharedPtr<FAsdaBrowserNode>>& OutRoots, int32& OutShown) const;
	// Проходит ли запись фильтр (без учёта группировки).
	bool PassesFilter(int32 Index, const FAsdaBrowserFilter& F) const;

	// Фасеты записи (nullptr — индекс вне списка).
	const FAsdaBrowserFacets* GetFacets(int32 Index) const { return Facets.IsValidIndex(Index) ? &Facets[Index] : nullptr; }
	// Английское имя (оверлей L10N/en таблицы своего пространства, ключ `<_row>.ItemName|Name`); "" — перевода нет.
	FString GetNameEn(int32 Index) const { return NamesEn.IsValidIndex(Index) ? NamesEn[Index] : FString(); }
	// Английское описание (оверлей item_tooltip / sowell_tooltip / specialitem_tooltiplist); "" — нет.
	FString GetDescriptionEn(EAsdaItemSpace Space, int32 Code) const;

	// Подписи разбивки (русский текст игры, где он есть; см. шапку).
	static FString CatLabel(EAsdaItemCat Cat);
	FString SubLabel(EAsdaItemCat Cat, int32 Sub, int32 SampleIndex) const;
	// Профессии из DT_jobtype: имя профессии и её архетип (родитель). "" — строки нет.
	int32 NumJobs() const { return JobNames.Num(); }
	FString JobName(int32 Usage) const { return JobNames.IsValidIndex(Usage) ? JobNames[Usage] : FString(); }
	FString JobParent(int32 Usage) const { return JobParents.IsValidIndex(Usage) ? JobParents[Usage] : FString(); }
	// Ветка требований класса (эталонные диапазоны CheckGradeReachable): 0 «для всех», 1 = 1–11, 2 = 12–22, 3 = 23–33,
	//   4 = 34–39 (вторые профессии), 5 = 40–44 («N-я профессия» — ранг любой ветки).
	static int32 JobBranch(int32 Usage);
	FString JobBranchLabel(int32 Branch) const;
	// Подпись разряда уровня (нотисы 12729…12738 «1 ~ 10»…«91 ~ 100»; 11 — «свыше 100»).
	static FString TierLabel(int32 Tier);
	// Подпись грейда (нотис 1421 + грейд) и его цвет (палитра подсказки `GradeColorArgb`).
	static FString GradeLabel(int32 Grade);
	static FLinearColor GradeColor(int32 Grade);

	// Связи записи (item_list): другие записи с тем же SetCode («код набора/родственных записей»).
	void SetSiblings(int32 Index, TArray<int32>& OutIndices) const;

	// ★ Перечитать таблицы (после переимпорта/добавления предметов): сбрасывает загрузку, следующий EnsureLoaded
	//   читает DataTable-ассеты заново.
	void Reload();

	// Иконка предмета (только разбор данных; наличие ассета/попадание ячейки проверяет UI).
	FAsdaBrowserIcon ResolveIcon(int32 Index, bool bMale) const;

	// Полный разбор: модель + текстура + иконка + эффект для заданной заточки.
	//   Enhance: 0 = без заточки (эффекта нет), 5/6/7 — ступени свечения.
	FAsdaBrowserItemAssets ResolveAssets(int32 Index, bool bMale, int32 Enhance) const;

	// Имя .jeb свечения заточки — ФОРМУЛА 1:1 с UAsdaWorldSubsystem::BuildGlowJebName
	//   (реверс Reports/weapon_glow_re.md §B). Возвращает "" если свечения нет.
	//   ★ Волна 3, хвосты (15.09.2026): таблицы и сборка имени — общие, AsdaWeaponGlow игрового модуля (своей копии нет).
	FString BuildGlowJebName(int32 Index, int32 Enhance) const;

	// Подпись типа/подтипа (для дерева и правой панели).
	static FString ItemTypeLabel(int32 Type);
	static FString WeaponSubTypeLabel(int32 SubType);

	// Описание предмета из таблицы ЕГО пространства ("" — записи нет или пусто).
	//   item_list        → DT_item_tooltip.Description            (ключ ItemCode)
	//   sowell_list      → DT_sowell_tooltip.Text                 (ключ Id)
	//   special_itemlist → DT_specialitem_tooltiplist.Tooltip     (ключ Id; у первой записи файла
	//                      загрузчик эталона форсирует id = 1 — ключ совпадает с кодом предмета)
	// Таблицы подгружаются ЛЕНИВО при первом обращении (только при открытой вкладке).
	const FString& GetDescription(EAsdaItemSpace Space, int32 Code) const;
	// Имя таблицы описаний для подписи в карточке.
	static const TCHAR* DescriptionTableName(EAsdaItemSpace Space);

private:
	bool bLoaded = false;
	FString LoadError;

	TStrongObjectPtr<UDataTable> ItemTable;
	TStrongObjectPtr<UDataTable> ImgTable;
	TStrongObjectPtr<UDataTable> SowelTable;      // DT_sowell_list
	TStrongObjectPtr<UDataTable> SpecialTable;    // DT_special_itemlist

	// Таблицы описаний: грузятся отдельно и лениво — дереву/иконкам они не нужны.
	mutable TStrongObjectPtr<UDataTable> TooltipTable;         // item_tooltip
	mutable TStrongObjectPtr<UDataTable> SowelTooltipTable;    // sowell_tooltip
	mutable TStrongObjectPtr<UDataTable> SpecialTooltipTable;  // specialitem_tooltiplist
	mutable TMap<int32, const FAsdaItemTooltipRow*> TooltipByCode;
	mutable TMap<int32, const FAsdaSowellTooltipRow*> SowelTooltipById;
	mutable TMap<int32, const FAsdaSpecialitemTooltiplistRow*> SpecialTooltipById;
	mutable bool bTooltipTried = false;
	void EnsureTooltipLoaded() const;

	// Указатели на строки живых DataTable (память принадлежит ассету, он удержан TStrongObjectPtr).
	TArray<const FAsdaItemRow*> Items;              // порядок = _row (он же ImgIndex)
	TArray<const FAsdaImgRow*>  Imgs;               // индексируется ImgIndex
	TArray<const FAsdaSowellRow*> Sowels;           // порядок = _row
	TArray<const FAsdaSpecialItemlistRow*> Specials;// порядок = _row

	// Сводный список по трём пространствам + индексы «код → глобальный индекс» по пространствам.
	TArray<FAsdaBrowserEntry> Entries;
	TMap<int32, int32> CodeToIndex;          // item_list
	TMap<int32, int32> SowelCodeToIndex;     // sowell_list
	TMap<int32, int32> SpecialCodeToIndex;   // special_itemlist

	const TMap<int32, int32>& CodeMapOf(EAsdaItemSpace Space) const;

	// ★ 29.10.2026: фасеты, имена для поиска и профессии (параллельно Entries).
	TArray<FAsdaBrowserFacets> Facets;
	TArray<FString> NamesEn;          // английское имя ("" — нет)
	TArray<FString> SearchKey;        // «код|имя ru|имя en» в нижнем регистре — для поиска
	TArray<FString> JobNames;         // DT_jobtype.JobName по номеру строки = UsageCategory
	TArray<FString> JobParents;       // DT_jobtype.ParentJob
	TStrongObjectPtr<UDataTable> JobTable;
	TMultiMap<int32, int32> SetCodeIndex;   // SetCode → глобальный индекс (item_list, SetCode != 0)
	// Разложить запись по категориям (правила — шапка разбивки).
	void ComputeFacets(int32 Index, FAsdaBrowserFacets& Out) const;
	// Один уровень пути дерева для записи в выбранной группировке.
	struct FPathKey { int64 Key = 0; FString Label; FLinearColor Color = FLinearColor::White; bool bColored = false; };
	void PathOf(int32 Index, EAsdaBrowserGrouping G, TArray<FPathKey>& Out) const;

	// Ключи группировки записи в дереве (по полям СВОЕЙ таблицы) и подпись категории.
	void GroupKeysOf(int32 Index, int32& OutType, int32& OutSub) const;
	static FString CategoryLabelOf(EAsdaItemSpace Space, int32 TypeId, const FString& SampleName);

	// Разбор одной записи по пространствам (внутренние ветки ResolveAssets).
	void ResolveItemListAssets(FAsdaBrowserItemAssets& A, const FAsdaItemRow* R, bool bMale, int32 Enhance) const;
	void ResolveSowelAssets(FAsdaBrowserItemAssets& A, const FAsdaSowellRow* R) const;
	void ResolveSpecialAssets(FAsdaBrowserItemAssets& A, const FAsdaSpecialItemlistRow* R) const;
};
