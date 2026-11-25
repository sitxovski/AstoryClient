// Copyright (c) 2026 @sitxovski (Discord)
// Реализация слоя данных браузера предметов — см. AsdaItemBrowserData.h.
// ИСТОЧНИК: только ассеты Content/ (DataTable + импортированные меши/текстуры/BP).
#include "AsdaItemBrowserData.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)

#include "Engine/DataTable.h"
#include "AsdaBytes.h"                       // ★ A4: единая читалка hex-колонки (−1 на мусоре, а не «тихий 0»)
#include "Items/AsdaItemTableSubsystem.h"    // ★ A3: namespace AsdaItemIcon — единая формула «запись → иконка»
#include "World/AsdaFxLightSettings.h"       // ★ волна 3, хвосты (15.09.2026): namespace AsdaWeaponGlow — имя свечения заточки (одна формула с рантаймом)
#include "Login/AsdaLoginData.h"     // DecodeCp1251 — строки внутри DataHex записи (cp1251)
#include "Login/AsdaText.h"          // ★ 29.10.2026: FAsdaText::Loc — подписи разбивки из GameText
#include "Text/AsdaLang.h"           // ★ 29.10.2026: FAsdaLang::FindOverlay — английские имена/описания
#include "Text/AsdaLangRows.h"       // ★ 29.10.2026: AsdaLangRows::RowNameOf — ключ оверлея по строке таблицы
#include "UI/AsdaItemTipStandalone.h"   // ★ 29.10.2026: FAsdaLangScope — подписи разбивки всегда по-русски
#include "UI/AsdaItemTooltipSubsystem.h"   // ★ 29.10.2026: GradeColorArgb / CheckGradeReachable — правила игры
#include "UI/AsdaUiHelpers.h"        // AsdaUi::SrgbU32ToLinear — цвет грейда как в игре
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemBrowser, Log, All);

// ── Пути ассетов item-БД (манифест порта: Items/AsdaItemDbManifest.h) ───────
static const TCHAR* kItemTablePath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_item_list.DT_item_list");
static const TCHAR* kImgTablePath  = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_item_img_table.DT_item_img_table");
// ★★★ ВТОРОЕ и ТРЕТЬЕ пространства кодов — без них браузер не видел ни самоцветы, ни премиум-предметы.
static const TCHAR* kSowelTablePath   = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_sowell_list.DT_sowell_list");
static const TCHAR* kSpecialTablePath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_special_itemlist.DT_special_itemlist");
// Описание предмета (item_tooltip.bin, stride 220: ItemCode + Description char[200]) — ключ ItemCode.
static const TCHAR* kTooltipTablePath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_item_tooltip.DT_item_tooltip");
// Описания самоцветов (sowell_tooltip.bin, stride 220: Id + Text char[200]) — ключ Id (= код камня).
static const TCHAR* kSowelTooltipTablePath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_sowell_tooltip.DT_sowell_tooltip");
// Описания спец-предметов (specialitem_tooltiplist.bin, stride 252: Id + Name char[32] + Tooltip char[180]).
static const TCHAR* kSpecialTooltipTablePath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_specialitem_tooltiplist.DT_specialitem_tooltiplist");
// ★ 29.10.2026: профессии (jobtype.bin: JobName + ParentJob; номер строки = UsageCategory записи предмета).
static const TCHAR* kJobTablePath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_jobtype.DT_jobtype");

// ── Пространства кодов ──────────────────────────────────────────────────────
const TCHAR* AsdaSpaceTag(EAsdaItemSpace Space)
{
	switch (Space)
	{
	case EAsdaItemSpace::SowelList:   return TEXT("камень");
	case EAsdaItemSpace::SpecialList: return TEXT("спец");
	default:                          return TEXT("предмет");
	}
}

const TCHAR* AsdaSpaceTable(EAsdaItemSpace Space)
{
	switch (Space)
	{
	case EAsdaItemSpace::SowelList:   return TEXT("sowell_list");
	case EAsdaItemSpace::SpecialList: return TEXT("special_itemlist");
	default:                          return TEXT("item_list");
	}
}

FString AsdaSpaceLabel(EAsdaItemSpace Space)
{
	switch (Space)
	{
	case EAsdaItemSpace::SowelList:   return TEXT("Самоцветы (живые камни)");
	case EAsdaItemSpace::SpecialList: return TEXT("Спец-предметы: премиум/бустеры (Table2 эталона)");
	default:                          return TEXT("Предметы, снаряжение, расходники");
	}
}

// «Имени нет в данных»: оригинал хранит там пустую строку либо прочерк «-» (RU-локализаторы
// не перевели 43 221 имя и оставили «-» — Reports/item_dedup_analysis.md §0.1).
static bool IsNameMissing(const FString& Name)
{
	const FString T = Name.TrimStartAndEnd();
	return T.IsEmpty() || T == TEXT("-");
}

// ── Смещения полей ВНУТРИ сырой записи img (DataHex = 300 байт записи 1:1) ──
// ★★★ Аудит №4 (A3). Поля ИКОНКИ отсюда УБРАНЫ: у них есть типизированные колонки
//   (`FAsdaImgRow::IconAtlasMale/IconCellMale/IconSmallFlag/IconAtlasFemale/IconCellFemale`),
//   и раскладка иконки теперь живёт ровно в одном месте — `namespace AsdaItemIcon`
//   (Public/Items/AsdaItemTableSubsystem.h). Держать здесь ВТОРУЮ копию смещений было прямым
//   риском: перегенерация DataTable развела бы две раскладки молча.
//   Остаются только поля, у которых колонки в USTRUCT НЕТ (попали в «дырки» раскладки).
namespace ImgOff
{
	constexpr int32 AuraName1       = 216;  // 0xD8  char[32] — доп. аура щита с +8 (weapon_glow_re.md §B)
	constexpr int32 AuraName2       = 252;  // 0xFC  char[32]
	constexpr int32 NameLen         = 32;
}

// ── Чтение из hex-строки DataHex ────────────────────────────────────────────
// ★★★ Аудит №4 (A4): ЕДИНСТВЕННЫЙ ЖИВОЙ `FParse::HexDigit` порта жил здесь и УБРАН.
//   `FParse::HexDigit` на НЕ-hex символе молча возвращает 0, то есть битая запись становилась
//   неотличима от записи со значением 0 (дефект, вычищенный из `Source/` дважды — A6 аудита №3).
//   Канон — `AsdaBytes::HexByteAt` (Public/AsdaBytes.h, header-only, из плагина доступен):
//   он отдаёт −1 и на «строка короткая», и на «символ не hex».
//
// Строка char[MaxLen] (cp1251, до нуля) из DataHex — тем же декодером, что и рантайм.
//   ★ −1 (обрыв строки либо мусор в hex) кладём в буфер как 0: нулевой байт для
//   `DecodeCp1251` — терминатор, то есть поле обрывается ровно там, где кончились данные.
//   Прежняя копия вела себя так же на выходе за границу; отличие в том, что теперь так же
//   обрабатывается и НЕ-hex символ, который раньше молча читался как 0x00…0x0F.
static FString HexStr(const FString& Hex, int32 ByteOff, int32 MaxLen)
{
	TArray<uint8> Buf;
	Buf.SetNumUninitialized(MaxLen);
	for (int32 i = 0; i < MaxLen; ++i)
	{
		const int32 B = AsdaBytes::HexByteAt(Hex, ByteOff + i);
		Buf[i] = (B < 0) ? 0 : (uint8)B;
	}
	return FAsdaLoginData::DecodeCp1251(Buf.GetData(), MaxLen);
}

// ── Хелперы путей ассетов ───────────────────────────────────────────────────
// Санитизация имени под UE-ассет (правило импорта: пробел/точка в имени файла клиента → '_').
static FString AssetSafeName(const FString& In)
{
	FString S = In;
	S.ReplaceInline(TEXT(" "), TEXT("_"));
	S.ReplaceInline(TEXT("."), TEXT("_"));
	return S;
}

// Существует ли пакет (без загрузки ассета).
static bool PackageExists(const FString& PackagePath)
{
	return !PackagePath.IsEmpty() && FPackageName::DoesPackageExist(PackagePath);
}

// Пакет → полный object-путь "<пакет>.<имя>".
static FString ObjectPathOf(const FString& PackagePath)
{
	return AsdaAssetPaths::PackageObjectPath(PackagePath);   // ★ волна 2 п. 7: правило реестра путей (короткое имя — как GetShortName)
}

// Варианты написания имени ассета: сырое из данных, затем санитизированное и обрезанное
// (в клиенте встречаются имена с пробелом/точкой — импортёр заменяет их на '_').
// ★ Плюс НИЖНИЙ РЕГИСТР: имена в таблицах приходят в разном регистре («Premium_icon» в
//   special_itemlist против импортированного ассета `premium_icon`), рантайм лечит это тем же
//   приёмом (UAsdaTextureSubsystem::ResolveAndCache). Без этого иконки спец-предметов не находятся.
static void NameCandidates(const FString& RawName, TArray<FString>& Out)
{
	Out.Reset();
	if (RawName.IsEmpty()) { return; }
	auto AddUnique = [&Out](const FString& S) { if (!S.IsEmpty()) { Out.AddUnique(S); } };
	AddUnique(RawName);
	AddUnique(AssetSafeName(RawName));
	AddUnique(RawName.TrimStartAndEnd());
	AddUnique(RawName.ToLower());
	AddUnique(AssetSafeName(RawName.TrimStartAndEnd()).ToLower());
}

// Пакет вида "<Prefix><Имя>" (папка = каталог ассетов).
static bool FindPackageFlat(const TCHAR* Prefix, const FString& RawName, FString& OutPackage)
{
	TArray<FString> Cand; NameCandidates(RawName, Cand);
	for (const FString& N : Cand)
	{
		const FString P = FString(Prefix) + N;
		if (PackageExists(P)) { OutPackage = P; return true; }
	}
	return false;
}

// Пакет вида "<Prefix><Имя><Mid><Имя>" (раскладка импорта: папка по имени ассета).
static bool FindPackageNested(const TCHAR* Prefix, const TCHAR* Mid, const FString& RawName, FString& OutPackage)
{
	TArray<FString> Cand; NameCandidates(RawName, Cand);
	for (const FString& N : Cand)
	{
		const FString P = FString(Prefix) + N + Mid + N;
		if (PackageExists(P)) { OutPackage = P; return true; }
	}
	return false;
}

// Разложить строки DataTable по ЧИСЛОВОМУ имени строки (_row при импорте CSV; он же ImgIndex).
// false — имена не числовые/вне диапазона (вызывающий переходит на порядок перечисления).
template<typename TRow>
static bool FillRowsByRowName(const UDataTable* Table, TArray<const TRow*>& Out)
{
	const TMap<FName, uint8*>& RowMap = Table->GetRowMap();
	Out.Init(nullptr, RowMap.Num());
	bool bOk = true;
	for (const TPair<FName, uint8*>& Pair : RowMap)
	{
		const FString RowName = Pair.Key.ToString();
		const int32 Idx = FCString::Atoi(*RowName);
		if (Idx < 0 || Idx >= Out.Num() || (Idx == 0 && RowName != TEXT("0")))
		{
			bOk = false;
			continue;
		}
		Out[Idx] = reinterpret_cast<const TRow*>(Pair.Value);
	}
	return bOk;
}

// ════════════════════════════════════════════════════════════════════════════
TSharedRef<FAsdaItemBrowserData> FAsdaItemBrowserData::Get()
{
	static TSharedRef<FAsdaItemBrowserData> Instance = MakeShared<FAsdaItemBrowserData>();
	return Instance;
}

bool FAsdaItemBrowserData::EnsureLoaded()
{
	if (bLoaded) { return true; }
	LoadError.Empty();

	UDataTable* DTItems = LoadObject<UDataTable>(nullptr, kItemTablePath);
	UDataTable* DTImgs  = LoadObject<UDataTable>(nullptr, kImgTablePath);
	if (!DTItems || !DTImgs)
	{
		LoadError = FString::Printf(TEXT("не найдены DataTable item-БД: %s / %s"),
			DTItems ? TEXT("DT_item_list ok") : TEXT("DT_item_list НЕТ"),
			DTImgs ? TEXT("DT_item_img_table ok") : TEXT("DT_item_img_table НЕТ"));
		UE_LOG(LogAsdaItemBrowser, Error, TEXT("[браузер] %s"), *LoadError);
		return false;
	}
	if (DTItems->GetRowStruct() != FAsdaItemRow::StaticStruct() || DTImgs->GetRowStruct() != FAsdaImgRow::StaticStruct())
	{
		LoadError = TEXT("структура строк DataTable не совпадает с FAsdaItemRow/FAsdaImgRow");
		UE_LOG(LogAsdaItemBrowser, Error, TEXT("[браузер] %s"), *LoadError);
		return false;
	}

	ItemTable.Reset(DTItems);
	ImgTable.Reset(DTImgs);

	Items.Reset(); Imgs.Reset();
	const bool bItemsOk = FillRowsByRowName<FAsdaItemRow>(DTItems, Items);
	const bool bImgsOk  = FillRowsByRowName<FAsdaImgRow>(DTImgs, Imgs);
	if (!bItemsOk || !bImgsOk)
	{
		// Фолбэк: имена строк не числовые — раскладываем в порядке перечисления (порядок импорта CSV).
		UE_LOG(LogAsdaItemBrowser, Warning,
			TEXT("[браузер] имена строк DataTable не числовые — порядок берётся из карты строк"));
		Items.Reset(); Imgs.Reset();
		for (const TPair<FName, uint8*>& P : DTItems->GetRowMap()) { Items.Add(reinterpret_cast<const FAsdaItemRow*>(P.Value)); }
		for (const TPair<FName, uint8*>& P : DTImgs->GetRowMap())  { Imgs.Add(reinterpret_cast<const FAsdaImgRow*>(P.Value)); }
	}

	// ── ВТОРОЕ и ТРЕТЬЕ пространства кодов ──
	// Отсутствие этих таблиц НЕ фатально: браузер продолжит работать по item_list, но честно
	// скажет об этом в LoadError-логе (пустой корень пространства в дереве не появится).
	Sowels.Reset(); Specials.Reset();
	if (UDataTable* DTSowels = LoadObject<UDataTable>(nullptr, kSowelTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		if (DTSowels->GetRowStruct() == FAsdaSowellRow::StaticStruct())
		{
			SowelTable.Reset(DTSowels);
			if (!FillRowsByRowName<FAsdaSowellRow>(DTSowels, Sowels))
			{
				Sowels.Reset();
				for (const TPair<FName, uint8*>& P : DTSowels->GetRowMap()) { Sowels.Add(reinterpret_cast<const FAsdaSowellRow*>(P.Value)); }
			}
		}
		else
		{
			UE_LOG(LogAsdaItemBrowser, Warning, TEXT("[браузер] DT_sowell_list: структура строк не FAsdaSowellRow — самоцветы не подключены"));
		}
	}
	else
	{
		UE_LOG(LogAsdaItemBrowser, Warning, TEXT("[браузер] DT_sowell_list не найден — самоцветов (коды 7001…10925) в списке не будет"));
	}

	if (UDataTable* DTSpecial = LoadObject<UDataTable>(nullptr, kSpecialTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		if (DTSpecial->GetRowStruct() == FAsdaSpecialItemlistRow::StaticStruct())
		{
			SpecialTable.Reset(DTSpecial);
			if (!FillRowsByRowName<FAsdaSpecialItemlistRow>(DTSpecial, Specials))
			{
				Specials.Reset();
				for (const TPair<FName, uint8*>& P : DTSpecial->GetRowMap()) { Specials.Add(reinterpret_cast<const FAsdaSpecialItemlistRow*>(P.Value)); }
			}
		}
		else
		{
			UE_LOG(LogAsdaItemBrowser, Warning, TEXT("[браузер] DT_special_itemlist: структура строк не FAsdaSpecialItemlistRow — спец-предметы не подключены"));
		}
	}
	else
	{
		UE_LOG(LogAsdaItemBrowser, Warning, TEXT("[браузер] DT_special_itemlist не найден — спец-предметов (коды 1…558) в списке не будет"));
	}

	// ── Сводный список: item_list → sowell_list → special_itemlist (порядок поиска эталона) ──
	Entries.Reset();
	Entries.Reserve(Items.Num() + Sowels.Num() + Specials.Num());
	CodeToIndex.Reset();        CodeToIndex.Reserve(Items.Num());
	SowelCodeToIndex.Reset();   SowelCodeToIndex.Reserve(Sowels.Num());
	SpecialCodeToIndex.Reset(); SpecialCodeToIndex.Reserve(Specials.Num());

	int32 Broken = 0;
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (!Items[i]) { ++Broken; continue; }
		const int32 Global = Entries.Add({ EAsdaItemSpace::ItemList, i, Items[i]->ItemCode });
		CodeToIndex.Add(Items[i]->ItemCode, Global);
	}
	for (int32 i = 0; i < Sowels.Num(); ++i)
	{
		if (!Sowels[i]) { continue; }
		const int32 Global = Entries.Add({ EAsdaItemSpace::SowelList, i, Sowels[i]->Id });
		SowelCodeToIndex.Add(Sowels[i]->Id, Global);
	}
	for (int32 i = 0; i < Specials.Num(); ++i)
	{
		if (!Specials[i]) { continue; }
		const int32 Global = Entries.Add({ EAsdaItemSpace::SpecialList, i, Specials[i]->Code });
		SpecialCodeToIndex.Add(Specials[i]->Code, Global);
	}

	bLoaded = Items.Num() > 0 && Imgs.Num() > 0 && Broken == 0;
	if (!bLoaded)
	{
		LoadError = FString::Printf(TEXT("таблицы пусты/повреждены (предметов %d, изображений %d, битых строк %d)"),
			Items.Num(), Imgs.Num(), Broken);
		UE_LOG(LogAsdaItemBrowser, Error, TEXT("[браузер] %s"), *LoadError);
		return false;
	}

	// ── ★ 29.10.2026: профессии (DT_jobtype: номер строки = UsageCategory), фасеты, английские имена, ключ поиска ──
	JobNames.Reset(); JobParents.Reset();
	if (UDataTable* DTJob = LoadObject<UDataTable>(nullptr, kJobTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		DTJob && DTJob->GetRowStruct() == FAsdaJobtypeRow::StaticStruct())
	{
		JobTable.Reset(DTJob);
		TArray<const FAsdaJobtypeRow*> Jobs;
		if (FillRowsByRowName<FAsdaJobtypeRow>(DTJob, Jobs))
		{
			for (const FAsdaJobtypeRow* J : Jobs)
			{
				JobNames.Add(J ? J->JobName.TrimStartAndEnd() : FString());
				JobParents.Add(J ? J->ParentJob.TrimStartAndEnd() : FString());
			}
		}
	}
	else
	{
		UE_LOG(LogAsdaItemBrowser, Warning, TEXT("[браузер] DT_jobtype не найден — подписи профессий будут номерами"));
	}

	Facets.SetNum(Entries.Num());
	NamesEn.SetNum(Entries.Num());
	SearchKey.SetNum(Entries.Num());
	SetCodeIndex.Reset();
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		ComputeFacets(i, Facets[i]);
		// Английское имя — языковой оверлей таблицы своего пространства (ключ `<RowName>.<поле>`, закон AsdaLangRows).
		FString En;
		switch (Entries[i].Space)
		{
		case EAsdaItemSpace::SowelList:
			if (const FAsdaSowellRow* R = GetSowelRow(i))
			{
				const FName Row = AsdaLangRows::RowNameOf(SowelTable.Get(), R);
				if (!Row.IsNone()) { FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("sowell_list"), Row.ToString() + TEXT(".Name"), En); }
			}
			break;
		case EAsdaItemSpace::SpecialList:
			if (const FAsdaSpecialItemlistRow* R = GetSpecialRow(i))
			{
				const FName Row = AsdaLangRows::RowNameOf(SpecialTable.Get(), R);
				if (!Row.IsNone()) { FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("special_itemlist"), Row.ToString() + TEXT(".Name"), En); }
			}
			break;
		default:
			if (const FAsdaItemRow* R = GetItemRow(i))
			{
				const FName Row = AsdaLangRows::RowNameOf(ItemTable.Get(), R);
				if (!Row.IsNone()) { FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("item_list"), Row.ToString() + TEXT(".ItemName"), En); }
				if (R->SetCode != 0) { SetCodeIndex.Add(R->SetCode, i); }
			}
			break;
		}
		En.TrimStartAndEndInline();
		if (IsNameMissing(En)) { En.Empty(); }
		NamesEn[i] = En;
		SearchKey[i] = FString::Printf(TEXT("%d|%s|%s"), Entries[i].Code, *GetName(i), *En).ToLower();
	}

	int32 F1 = 0, L1 = 0, F2 = 0, L2 = 0, F3 = 0, L3 = 0;
	CodeRange(EAsdaItemSpace::ItemList, F1, L1);
	CodeRange(EAsdaItemSpace::SowelList, F2, L2);
	CodeRange(EAsdaItemSpace::SpecialList, F3, L3);
	UE_LOG(LogAsdaItemBrowser, Log,
		TEXT("[браузер] загружено из ассетов: item_list %d (%d…%d), sowell_list %d (%d…%d), special_itemlist %d (%d…%d), img-записей %d; всего записей %d"),
		Items.Num(), F1, L1, Sowels.Num(), F2, L2, Specials.Num(), F3, L3, Imgs.Num(), Entries.Num());
	return true;
}

// ── Доступ к сводному списку ────────────────────────────────────────────────
int32 FAsdaItemBrowserData::NumInSpace(EAsdaItemSpace Space) const
{
	switch (Space)
	{
	case EAsdaItemSpace::SowelList:   return Sowels.Num();
	case EAsdaItemSpace::SpecialList: return Specials.Num();
	default:                          return Items.Num();
	}
}

void FAsdaItemBrowserData::CodeRange(EAsdaItemSpace Space, int32& OutFirst, int32& OutLast) const
{
	OutFirst = 0; OutLast = 0;
	const TMap<int32, int32>& Map = CodeMapOf(Space);
	for (const TPair<int32, int32>& It : Map)
	{
		if (OutFirst == 0 || It.Key < OutFirst) { OutFirst = It.Key; }
		if (It.Key > OutLast) { OutLast = It.Key; }
	}
}

const TMap<int32, int32>& FAsdaItemBrowserData::CodeMapOf(EAsdaItemSpace Space) const
{
	switch (Space)
	{
	case EAsdaItemSpace::SowelList:   return SowelCodeToIndex;
	case EAsdaItemSpace::SpecialList: return SpecialCodeToIndex;
	default:                          return CodeToIndex;
	}
}

const FAsdaItemRow* FAsdaItemBrowserData::GetItemRow(int32 Index) const
{
	const FAsdaBrowserEntry* E = GetEntry(Index);
	if (!E || E->Space != EAsdaItemSpace::ItemList) { return nullptr; }
	return Items.IsValidIndex(E->RowIndex) ? Items[E->RowIndex] : nullptr;
}

const FAsdaSowellRow* FAsdaItemBrowserData::GetSowelRow(int32 Index) const
{
	const FAsdaBrowserEntry* E = GetEntry(Index);
	if (!E || E->Space != EAsdaItemSpace::SowelList) { return nullptr; }
	return Sowels.IsValidIndex(E->RowIndex) ? Sowels[E->RowIndex] : nullptr;
}

const FAsdaSpecialItemlistRow* FAsdaItemBrowserData::GetSpecialRow(int32 Index) const
{
	const FAsdaBrowserEntry* E = GetEntry(Index);
	if (!E || E->Space != EAsdaItemSpace::SpecialList) { return nullptr; }
	return Specials.IsValidIndex(E->RowIndex) ? Specials[E->RowIndex] : nullptr;
}

FString FAsdaItemBrowserData::GetName(int32 Index) const
{
	const FAsdaBrowserEntry* E = GetEntry(Index);
	if (!E) { return FString(); }
	FString Raw;
	switch (E->Space)
	{
	case EAsdaItemSpace::SowelList:   if (const FAsdaSowellRow* R = GetSowelRow(Index))   { Raw = R->Name; } break;
	case EAsdaItemSpace::SpecialList: if (const FAsdaSpecialItemlistRow* R = GetSpecialRow(Index)) { Raw = R->Name; } break;
	default:                          if (const FAsdaItemRow* R = GetItemRow(Index))      { Raw = R->ItemName; } break;
	}
	return IsNameMissing(Raw) ? FString() : Raw.TrimStartAndEnd();
}

int32 FAsdaItemBrowserData::IndexByCodeInSpace(EAsdaItemSpace Space, int32 ItemCode) const
{
	const int32* P = CodeMapOf(Space).Find(ItemCode);
	return P ? *P : INDEX_NONE;
}

int32 FAsdaItemBrowserData::IndexByCode(int32 ItemCode) const
{
	// Порядок эталона: item_list → sowell_list → special_itemlist.
	for (uint8 S = 0; S < (uint8)EAsdaItemSpace::Count; ++S)
	{
		const int32 Idx = IndexByCodeInSpace((EAsdaItemSpace)S, ItemCode);
		if (Idx != INDEX_NONE) { return Idx; }
	}
	return INDEX_NONE;
}

// ── Описание предмета (item_tooltip) ────────────────────────────────────────
// Оригинал берёт описание отдельной таблицей по ItemCode (itemTooltipBuildText: строка описания
// печатается в конце тултипа, после характеристик). Здесь — тот же ключ и тот же текст, из АССЕТА.
void FAsdaItemBrowserData::EnsureTooltipLoaded() const
{
	if (bTooltipTried) { return; }
	bTooltipTried = true;

	// item_tooltip — описания обычных предметов (ключ ItemCode).
	if (UDataTable* T = LoadObject<UDataTable>(nullptr, kTooltipTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		T && T->GetRowStruct() == FAsdaItemTooltipRow::StaticStruct())
	{
		TooltipTable.Reset(T);
		TooltipByCode.Reserve(T->GetRowMap().Num());
		for (const TPair<FName, uint8*>& P : T->GetRowMap())
		{
			const FAsdaItemTooltipRow* R = reinterpret_cast<const FAsdaItemTooltipRow*>(P.Value);
			if (R) { TooltipByCode.Add(R->ItemCode, R); }
		}
	}
	else
	{
		UE_LOG(LogAsdaItemBrowser, Warning,
			TEXT("[браузер] DT_item_tooltip не найден/структура не совпала — описаний предметов не будет"));
	}

	// sowell_tooltip — описания самоцветов (ключ Id = код камня, эталон `sowelTable_FindByCode`).
	if (UDataTable* T = LoadObject<UDataTable>(nullptr, kSowelTooltipTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		T && T->GetRowStruct() == FAsdaSowellTooltipRow::StaticStruct())
	{
		SowelTooltipTable.Reset(T);
		SowelTooltipById.Reserve(T->GetRowMap().Num());
		for (const TPair<FName, uint8*>& P : T->GetRowMap())
		{
			const FAsdaSowellTooltipRow* R = reinterpret_cast<const FAsdaSowellTooltipRow*>(P.Value);
			if (R) { SowelTooltipById.Add(R->Id, R); }
		}
	}
	else
	{
		UE_LOG(LogAsdaItemBrowser, Warning,
			TEXT("[браузер] DT_sowell_tooltip не найден/структура не совпала — описаний самоцветов не будет"));
	}

	// specialitem_tooltiplist — описания спец-предметов. Ключ Id: загрузчик эталона форсирует
	// первой записи файла id = 1, поэтому Id совпадает с кодом предмета из special_itemlist.
	if (UDataTable* T = LoadObject<UDataTable>(nullptr, kSpecialTooltipTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		T && T->GetRowStruct() == FAsdaSpecialitemTooltiplistRow::StaticStruct())
	{
		SpecialTooltipTable.Reset(T);
		SpecialTooltipById.Reserve(T->GetRowMap().Num());
		for (const TPair<FName, uint8*>& P : T->GetRowMap())
		{
			const FAsdaSpecialitemTooltiplistRow* R = reinterpret_cast<const FAsdaSpecialitemTooltiplistRow*>(P.Value);
			if (R) { SpecialTooltipById.Add(R->Id, R); }
		}
	}
	else
	{
		UE_LOG(LogAsdaItemBrowser, Warning,
			TEXT("[браузер] DT_specialitem_tooltiplist не найден/структура не совпала — описаний спец-предметов не будет"));
	}

	UE_LOG(LogAsdaItemBrowser, Log,
		TEXT("[браузер] описаний загружено: item_tooltip %d, sowell_tooltip %d, specialitem_tooltiplist %d"),
		TooltipByCode.Num(), SowelTooltipById.Num(), SpecialTooltipById.Num());
}

const TCHAR* FAsdaItemBrowserData::DescriptionTableName(EAsdaItemSpace Space)
{
	switch (Space)
	{
	case EAsdaItemSpace::SowelList:   return TEXT("sowell_tooltip");
	case EAsdaItemSpace::SpecialList: return TEXT("specialitem_tooltiplist");
	default:                          return TEXT("item_tooltip");
	}
}

const FString& FAsdaItemBrowserData::GetDescription(EAsdaItemSpace Space, int32 Code) const
{
	static const FString Empty;
	EnsureTooltipLoaded();
	switch (Space)
	{
	case EAsdaItemSpace::SowelList:
		if (const FAsdaSowellTooltipRow* const* F = SowelTooltipById.Find(Code)) { return *F ? (*F)->Text : Empty; }
		return Empty;
	case EAsdaItemSpace::SpecialList:
		if (const FAsdaSpecialitemTooltiplistRow* const* F = SpecialTooltipById.Find(Code)) { return *F ? (*F)->Tooltip : Empty; }
		return Empty;
	default:
		if (const FAsdaItemTooltipRow* const* F = TooltipByCode.Find(Code)) { return *F ? (*F)->Description : Empty; }
		return Empty;
	}
}

// ── Подписи категорий ───────────────────────────────────────────────────────
// Значения itemType подтверждены рантаймом порта (AAsdaLootActor::InitFromItemCode: 0=hcap, 1=chst,
// 2=pant, 3=shoe, 4=grov, 8=щит, 9=оружие) и самими данными (имена моделей ring/neck/arrw + имена
// предметов). Неизвестный тип подписывается номером — не выдумываем.
FString FAsdaItemBrowserData::ItemTypeLabel(int32 Type)
{
	switch (Type)
	{
	case 0:  return TEXT("Головные уборы (hcap)");
	case 1:  return TEXT("Нагрудники (chst)");
	case 2:  return TEXT("Штаны (pant)");
	case 3:  return TEXT("Ботинки (shoe)");
	case 4:  return TEXT("Перчатки (grov)");
	case 5:  return TEXT("Кольца (ring)");
	case 7:  return TEXT("Ожерелья (neck)");
	case 8:  return TEXT("Щиты (shid)");
	case 9:  return TEXT("Оружие");
	case 10: return TEXT("Боеприпасы/колчаны (arrw)");
	case 11: return TEXT("Прочее (расходники/материалы)");
	case 13: return TEXT("Бустеры");
	default: return FString::Printf(TEXT("Тип %d"), Type);
	}
}

// Класс оружия = itemSubType. Пары «модель дропа / база эффекта» — из рантайма:
//   InitFromItemCode (swrd/spir/thnd/stap/cbow/lbow/bbow) и AsdaWeaponGlow::BaseByWeaponSubType (ohs/spr/ths/stf/xbow/bow/bali).
FString FAsdaItemBrowserData::WeaponSubTypeLabel(int32 SubType)
{
	switch (SubType)
	{
	case 0:  return TEXT("Одноручное (ohs)");
	case 1:  return TEXT("Меч (swrd / ohs)");
	case 2:  return TEXT("Копьё (spir / spr)");
	case 3:  return TEXT("Двуручное (thnd / ths)");
	case 5:  return TEXT("Посох (stap / stf)");
	case 8:  return TEXT("Лук (bow)");
	case 9:  return TEXT("Арбалет (cbow / xbow)");
	case 10: return TEXT("Лук (lbow / bow)");
	case 11: return TEXT("Баллиста (bbow / bali)");
	default: return FString::Printf(TEXT("Подтип %d"), SubType);
	}
}

// ── Дерево ──────────────────────────────────────────────────────────────────
// Ключи группировки внутри пространства. Берутся ИЗ ДАННЫХ, ничего не выдумываем:
//   item_list        — ItemType / ItemSubType (те же поля, по которым эталон разводит предметы);
//   sowell_list      — BonusStatType (тип бонуса камня), подкатегорий нет;
//   special_itemlist — SubType (@0x50, «функция» предмета — ключ populateBooster), подкатегорий нет.
void FAsdaItemBrowserData::GroupKeysOf(int32 Index, int32& OutType, int32& OutSub) const
{
	OutType = 0; OutSub = 0;
	if (const FAsdaItemRow* R = GetItemRow(Index))              { OutType = R->ItemType; OutSub = R->ItemSubType; return; }
	if (const FAsdaSowellRow* R = GetSowelRow(Index))           { OutType = R->BonusStatType; return; }
	if (const FAsdaSpecialItemlistRow* R = GetSpecialRow(Index)){ OutType = R->SubType; return; }
}

// Подпись категории внутри пространства.
FString FAsdaItemBrowserData::CategoryLabelOf(EAsdaItemSpace Space, int32 TypeId, const FString& SampleName)
{
	switch (Space)
	{
	case EAsdaItemSpace::SowelList:
		return SampleName.IsEmpty()
			? FString::Printf(TEXT("Бонус-стат %d (поле BonusStatType)"), TypeId)
			: FString::Printf(TEXT("Бонус-стат %d (поле BonusStatType, напр. «%s»)"), TypeId, *SampleName);
	case EAsdaItemSpace::SpecialList:
		return SampleName.IsEmpty()
			? FString::Printf(TEXT("Функция %d (поле SubType)"), TypeId)
			: FString::Printf(TEXT("Функция %d (поле SubType, напр. «%s»)"), TypeId, *SampleName);
	default:
		return ItemTypeLabel(TypeId);
	}
}

void FAsdaItemBrowserData::BuildTree(const FString& Filter, TArray<TSharedPtr<FAsdaBrowserNode>>& OutRoots,
	TOptional<EAsdaItemSpace> SpaceFilter) const
{
	OutRoots.Reset();
	if (!bLoaded) { return; }

	const FString F = Filter.TrimStartAndEnd();
	const bool bHasFilter = !F.IsEmpty();

	// ★ Числовой запрос трактуем как КОД. Коды трёх пространств не пересекаются, но подстрочный
	//   поиск («461» находит и 24610 из item_list) их путает, поэтому точные попадания выносим
	//   отдельным корнем — с явной пометкой, в каком пространстве найдено.
	bool bNumeric = bHasFilter;
	for (const TCHAR C : F) { if (!FChar::IsDigit(C)) { bNumeric = false; break; } }
	const int32 ExactCode = bNumeric ? FCString::Atoi(*F) : 0;

	auto SpaceAllowed = [&SpaceFilter](EAsdaItemSpace S)
	{
		return !SpaceFilter.IsSet() || SpaceFilter.GetValue() == S;
	};

	// Создать узел предмета по глобальному индексу.
	auto MakeItemNode = [this](int32 GlobalIndex, int32 TypeId, int32 SubId)
	{
		TSharedPtr<FAsdaBrowserNode> N = MakeShared<FAsdaBrowserNode>();
		N->Kind = EAsdaBrowserNodeKind::Item;
		N->Space = GetSpace(GlobalIndex);
		N->TypeId = TypeId;
		N->SubId = SubId;
		N->ItemIndex = GlobalIndex;
		return N;
	};

	// ── Корень «точное совпадение по коду» ──
	if (bNumeric && ExactCode > 0)
	{
		TSharedPtr<FAsdaBrowserNode> Hit = MakeShared<FAsdaBrowserNode>();
		Hit->Kind = EAsdaBrowserNodeKind::CodeHit;
		for (uint8 S = 0; S < (uint8)EAsdaItemSpace::Count; ++S)
		{
			const EAsdaItemSpace Space = (EAsdaItemSpace)S;
			if (!SpaceAllowed(Space)) { continue; }
			const int32 Idx = IndexByCodeInSpace(Space, ExactCode);
			if (Idx == INDEX_NONE) { continue; }
			int32 T = 0, Sub = 0; GroupKeysOf(Idx, T, Sub);
			Hit->Children.Add(MakeItemNode(Idx, T, Sub));
			++Hit->Count;
		}
		if (Hit->Count > 0)
		{
			Hit->Label = (Hit->Count == 1)
				? FString::Printf(TEXT("★ Точное совпадение по коду %d — 1 (таблица %s)"),
					ExactCode, AsdaSpaceTable(Hit->Children[0]->Space))
				: FString::Printf(TEXT("★ Точное совпадение по коду %d — %d в РАЗНЫХ пространствах"),
					ExactCode, Hit->Count);
			OutRoots.Add(Hit);
		}
	}

	// ── Корни пространств ──
	for (uint8 SpaceIdx = 0; SpaceIdx < (uint8)EAsdaItemSpace::Count; ++SpaceIdx)
	{
		const EAsdaItemSpace Space = (EAsdaItemSpace)SpaceIdx;
		if (!SpaceAllowed(Space) || NumInSpace(Space) == 0) { continue; }

		// тип → подтип → узел (порядок ключей задаём сортировкой в конце)
		TMap<int32, TSharedPtr<FAsdaBrowserNode>> ByType;
		TMap<int32, TMap<int32, TSharedPtr<FAsdaBrowserNode>>> BySub;
		int32 Shown = 0;

		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			if (Entries[i].Space != Space) { continue; }
			if (bHasFilter)
			{
				const FString Name = GetName(i);
				const bool bByName = !Name.IsEmpty() && Name.Contains(F, ESearchCase::IgnoreCase);
				const bool bById = FString::FromInt(Entries[i].Code).Contains(F);
				if (!bByName && !bById) { continue; }
			}
			++Shown;

			int32 T = 0, Sub = 0; GroupKeysOf(i, T, Sub);

			TSharedPtr<FAsdaBrowserNode>& Cat = ByType.FindOrAdd(T);
			if (!Cat.IsValid())
			{
				Cat = MakeShared<FAsdaBrowserNode>();
				Cat->Kind = EAsdaBrowserNodeKind::Category;
				Cat->Space = Space;
				Cat->TypeId = T;
			}
			++Cat->Count;

			TSharedPtr<FAsdaBrowserNode>& SubNode = BySub.FindOrAdd(T).FindOrAdd(Sub);
			if (!SubNode.IsValid())
			{
				SubNode = MakeShared<FAsdaBrowserNode>();
				SubNode->Kind = EAsdaBrowserNodeKind::SubCategory;
				SubNode->Space = Space;
				SubNode->TypeId = T;
				SubNode->SubId = Sub;
			}
			++SubNode->Count;
			SubNode->Children.Add(MakeItemNode(i, T, Sub));
		}

		if (Shown == 0) { continue; }

		TSharedPtr<FAsdaBrowserNode> Root = MakeShared<FAsdaBrowserNode>();
		Root->Kind = EAsdaBrowserNodeKind::Space;
		Root->Space = Space;
		Root->Count = Shown;
		int32 First = 0, Last = 0; CodeRange(Space, First, Last);
		Root->Label = FString::Printf(TEXT("%s — %s, коды %d…%d — показано %d из %d"),
			AsdaSpaceTable(Space), *AsdaSpaceLabel(Space), First, Last, Shown, NumInSpace(Space));

		// Сборка: если у типа один подтип — подкатегорию не показываем (ТЗ: «подкатегории если есть»).
		TArray<int32> TypeKeys; ByType.GetKeys(TypeKeys); TypeKeys.Sort();
		for (int32 T : TypeKeys)
		{
			TSharedPtr<FAsdaBrowserNode> Cat = ByType[T];
			TMap<int32, TSharedPtr<FAsdaBrowserNode>>& Subs = BySub[T];
			TArray<int32> SubKeys; Subs.GetKeys(SubKeys); SubKeys.Sort();

			// Пример имени для подписи категории — первая ЗАПИСЬ С ИМЕНЕМ в ветке.
			FString SampleName;
			for (int32 S : SubKeys)
			{
				for (const TSharedPtr<FAsdaBrowserNode>& C : Subs[S]->Children)
				{
					SampleName = GetName(C->ItemIndex);
					if (!SampleName.IsEmpty()) { break; }
				}
				if (!SampleName.IsEmpty()) { break; }
			}
			Cat->Label = FString::Printf(TEXT("%s — %d"), *CategoryLabelOf(Space, T, SampleName), Cat->Count);

			if (SubKeys.Num() <= 1)
			{
				Cat->Children = Subs[SubKeys[0]]->Children;
			}
			else
			{
				for (int32 S : SubKeys)
				{
					TSharedPtr<FAsdaBrowserNode> SubNode = Subs[S];
					// Подпись подтипа: у оружия item_list — класс из рантайма; иначе номер + пример из ДАННЫХ.
					FString SubLabel;
					if (Space == EAsdaItemSpace::ItemList && T == 9)
					{
						SubLabel = WeaponSubTypeLabel(S);
					}
					else
					{
						FString Sample;
						for (const TSharedPtr<FAsdaBrowserNode>& C : SubNode->Children)
						{
							Sample = GetName(C->ItemIndex);
							if (!Sample.IsEmpty()) { break; }
						}
						SubLabel = Sample.IsEmpty()
							? FString::Printf(TEXT("Подтип %d"), S)
							: FString::Printf(TEXT("Подтип %d (напр. «%s»)"), S, *Sample);
					}
					SubNode->Label = FString::Printf(TEXT("%s — %d"), *SubLabel, SubNode->Count);
					Cat->Children.Add(SubNode);
				}
			}
			Root->Children.Add(Cat);
		}
		OutRoots.Add(Root);
	}
}

// ── Иконка ──────────────────────────────────────────────────────────────────
// ★★★ ТРИ ВЕТКИ — 1:1 с UAsdaItemTableSubsystem::GetItemIcon (он же 1:1 с эталоном). Раньше
//   работала только первая, поэтому у самоцветов и спец-предметов иконки не было в принципе.
FAsdaBrowserIcon FAsdaItemBrowserData::ResolveIcon(int32 Index, bool bMale) const
{
	FAsdaBrowserIcon Icon;
	const FAsdaBrowserEntry* E = GetEntry(Index);
	if (!E) { return Icon; }
	Icon.Space = E->Space;

	// Найти атлас среди импортированных UI-текстур (имена в данных приходят в разном регистре).
	auto ResolveAtlas = [&Icon]()
	{
		if (Icon.Atlas.IsEmpty()) { return; }
		FString Pkg;
		if (FindPackageFlat(AsdaAssetPaths::kJuiPrefix, Icon.Atlas, Pkg))
		{
			Icon.AtlasObjectPath = ObjectPathOf(Pkg);
		}
	};

	// ★ Аудит №4 (A3): перенос результата ОБЩЕЙ ТОЧКИ (`AsdaItemIcon::*`) в структуру браузера.
	//   Своих формул сетки у плагина больше нет — только поля, которых нет у рантайма
	//   (`Space` и `AtlasObjectPath` — это уже редакторский резолв ассета, а не раскладка данных).
	auto Take = [&Icon](const FAsdaItemIcon& Src)
	{
		Icon.Atlas    = Src.Atlas;
		Icon.Cell     = Src.Cell;
		Icon.CellX    = Src.CellX;
		Icon.CellY    = Src.CellY;
		Icon.CellSize = Src.CellSize;
		Icon.Cols     = Src.Cols;
	};

	// ── самоцветы: атлас = IconPath целиком, левый-верхний квадрат 32×32 (ячеек НЕТ) ──
	if (const FAsdaSowellRow* Sow = GetSowelRow(Index))
	{
		Take(AsdaItemIcon::FromSowelIconPath(Sow->IconPath.TrimStartAndEnd()));
		ResolveAtlas();
		return Icon;
	}
	// ── спец-предметы: атлас = IconPath, ячейка IconCell, сетка ЖЁСТКО 8 колонок × 64 px ──
	if (const FAsdaSpecialItemlistRow* Sp = GetSpecialRow(Index))
	{
		Take(AsdaItemIcon::FromSpecialIcon(Sp->IconPath.TrimStartAndEnd(), Sp->IconCell));
		ResolveAtlas();
		return Icon;
	}

	// ── обычные предметы: атлас и ячейка из img-записи ──
	const FAsdaItemRow* R = GetItemRow(Index);
	if (!R) { return Icon; }
	const FAsdaImgRow* Img = GetImg(R->ImgIndex);
	if (!Img) { return Icon; }

	// ★★★ Аудит №4 (A3): ТРЕТИЙ СПОСОБ ЧТЕНИЯ УБРАН. Раньше здесь разбирался сырой `DataHex` по
	//   собственным смещениям 132/164/168/172/204 — при том что у ВСЕХ пяти полей иконки есть
	//   типизированные колонки (`AsdaItemDbRows.h:52-56`), которыми пользуется рантайм.
	//   Теперь и рантайм, и браузер зовут одну функцию над одними и теми же колонками.
	Take(AsdaItemIcon::FromImg(*Img, bMale));
	ResolveAtlas();
	return Icon;
}

// ── Свечение заточки (формула 1:1 с UAsdaWorldSubsystem::BuildGlowJebName) ──
// ★ Волна 3, хвосты пп.12/16 (15.09.2026): своя копия таблицы баз, таблицы грейдов и сборки имени УДАЛЕНА —
//   браузер зовёт ту же формулу, что рантайм: AsdaWeaponGlow (World/AsdaFxLightSettings.cpp игрового модуля).
//   Здесь остались только отличия браузера: запись берётся из строки DataTable, свечение — только у itemType 8/9.
//   Эквивалентность прежней копии — стенд на всех записях item_list (Reports/dedup_audit/wave3_quality_fxlight.md, «Хвосты»).

// ★ Свечение заточки есть ТОЛЬКО у оружия и щитов item_list: ни самоцвет, ни спец-предмет
//   заточку не носят (в эталоне ветка свечения живёт внутри разбора записи item_list).
FString FAsdaItemBrowserData::BuildGlowJebName(int32 Index, int32 Enhance) const
{
	const FAsdaItemRow* R = GetItemRow(Index);
	if (!R || Enhance < AsdaWeaponGlow::MinEnhance) { return FString(); }
	const bool bShield = (R->ItemType == 8);
	const bool bWeapon = (R->ItemType == 9);
	if (!bShield && !bWeapon) { return FString(); }

	// Спец-предметы эталона (ветка ДО таблиц): 41956/56048 → test-000.
	if (bWeapon && AsdaWeaponGlow::IsTestGlowItem(R->ItemCode)) { return AsdaWeaponGlow::TestGlowJeb(); }

	const FAsdaImgRow* Img = GetImg(R->ImgIndex);
	if (!Img) { return FString(); }

	// База (оружие по подтипу / щит), буква imgSubType (0 = буквы нет ⇒ свечения нет), грейд и ступень — общая формула
	return AsdaWeaponGlow::ComposeJeb(bShield, R->ItemSubType, Img->ImgSubType, R->ItemGrade, Enhance);
}

// ── Полный разбор ───────────────────────────────────────────────────────────
// Найти меш по имени в трёх раскладках импорта порта. Возвращает вид найденного (Missing — имя
// есть, а ассета нет). bPreferChar — сначала смотреть Models/Char (модели транспорта спец-предметов).
static EAsdaBrowserMeshKind FindMeshAsset(const FString& MeshName, bool bPreferChar, FString& OutObjectPath)
{
	OutObjectPath.Empty();
	if (MeshName.IsEmpty()) { return EAsdaBrowserMeshKind::None; }

	FString Pkg;
	EAsdaBrowserMeshKind Kind = EAsdaBrowserMeshKind::Missing;
	if (!bPreferChar && FindPackageNested(AsdaAssetPaths::kEquipPartsPrefix, TEXT("/"), MeshName, Pkg))
	{
		Kind = EAsdaBrowserMeshKind::EquipSkeletal;
	}
	else if (FindPackageNested(AsdaAssetPaths::kCharModelsPrefix, TEXT("/StaticMeshes/"), MeshName, Pkg))
	{
		Kind = EAsdaBrowserMeshKind::CharStatic;
	}
	else if (FindPackageNested(AsdaAssetPaths::kItemModelsPrefix, TEXT("/StaticMeshes/"), MeshName, Pkg))
	{
		Kind = EAsdaBrowserMeshKind::ItemStatic;
	}
	else if (FindPackageNested(AsdaAssetPaths::kEquipPartsPrefix, TEXT("/"), MeshName, Pkg))
	{
		Kind = EAsdaBrowserMeshKind::EquipSkeletal;
	}
	if (Kind != EAsdaBrowserMeshKind::Missing) { OutObjectPath = ObjectPathOf(Pkg); }
	return Kind;
}

// Текстура модели: сначала «своё» пространство текстур по виду меша, затем второе.
static FString FindTexAsset(const FString& TexName, bool bCharSpace)
{
	if (TexName.IsEmpty()) { return FString(); }
	FString Pkg;
	const TCHAR* First  = bCharSpace ? AsdaAssetPaths::kCharTexturePrefix
									 : AsdaAssetPaths::kItemTexturePrefix;
	const TCHAR* Second = bCharSpace ? AsdaAssetPaths::kItemTexturePrefix
									 : AsdaAssetPaths::kCharTexturePrefix;
	if (FindPackageFlat(First, TexName, Pkg) || FindPackageFlat(Second, TexName, Pkg))
	{
		return ObjectPathOf(Pkg);
	}
	return FString();
}

// ── Ветка item_list: модель/текстура/аура из img-записи ──
void FAsdaItemBrowserData::ResolveItemListAssets(FAsdaBrowserItemAssets& A, const FAsdaItemRow* R, bool bMale, int32 /*Enhance*/) const
{
	A.Row = R;
	A.ItemType = R->ItemType;
	A.ItemSubType = R->ItemSubType;
	A.ItemGrade = R->ItemGrade;
	A.ImgIndex = R->ImgIndex;

	const FAsdaImgRow* Img = GetImg(R->ImgIndex);
	if (Img)
	{
		A.ImgSubType = Img->ImgSubType;
		A.MeshName = bMale ? Img->MeshMale : Img->MeshFemale;
		A.TexName  = bMale ? Img->TexMale  : Img->TexFemale;
		A.AuraName1 = HexStr(Img->DataHex, ImgOff::AuraName1, ImgOff::NameLen);
		A.AuraName2 = HexStr(Img->DataHex, ImgOff::AuraName2, ImgOff::NameLen);
	}

	// Раскладка импорта порта: экип (броня/шлемы) — скелетал EquipParts; оружие/щиты — статик Char;
	// прочее — статик Items (дроп-модели).
	A.MeshKind = FindMeshAsset(A.MeshName, /*bPreferChar*/false, A.MeshObjectPath);
	A.TexObjectPath = FindTexAsset(A.TexName,
		A.MeshKind == EAsdaBrowserMeshKind::EquipSkeletal || A.MeshKind == EAsdaBrowserMeshKind::CharStatic);
}

// ── Ветка sowell_list: у камня СВОЙ меш (MeshEntry), текстуры модели в записи нет ──
void FAsdaItemBrowserData::ResolveSowelAssets(FAsdaBrowserItemAssets& A, const FAsdaSowellRow* R) const
{
	A.SowelRow = R;
	// Полей ItemType/ItemSubType/ItemGrade у камня НЕТ — они принадлежат только item_list.
	// В карточку идут собственные поля записи (BonusStatType, PartGrade и т.д.), см. UI.
	A.ItemType = -1; A.ItemSubType = -1; A.ItemGrade = -1; A.ImgIndex = -1; A.ImgSubType = -1;

	A.MeshName = R->MeshEntry.TrimStartAndEnd();
	if (A.MeshName.IsEmpty()) { A.MeshName = R->MeshEntry2.TrimStartAndEnd(); }
	A.MeshKind = FindMeshAsset(A.MeshName, /*bPreferChar*/false, A.MeshObjectPath);
	// Отдельной колонки текстуры модели у sowell_list нет (проверено по раскладке записи 188 б),
	// поэтому текстуру не подставляем — материал импортированного меша уже несёт свою.
	A.TexName.Empty();
	A.TexObjectPath.Empty();
}

// ── Ветка special_itemlist: модель транспорта ModelEntry + текстура <ModelEntry>_t ──
void FAsdaItemBrowserData::ResolveSpecialAssets(FAsdaBrowserItemAssets& A, const FAsdaSpecialItemlistRow* R) const
{
	A.SpecialRow = R;
	A.ItemType = -1; A.ItemSubType = R->SubType; A.ItemGrade = -1; A.ImgIndex = -1; A.ImgSubType = -1;

	// `ModelEntry` (+230) — меш+анимы транспорта `itm1_n_96_vehicle_00NN`, текстура `<ModelEntry>_t`
	// (реверс Reports/mount_system_re.md, SetParts_RidingSystem п.7). Заполнен у 215 из 558 строк:
	// у остальных модели НЕТ и в оригинале (свитки, зелья — только иконка).
	A.MeshName = R->ModelEntry.TrimStartAndEnd();
	A.MeshKind = FindMeshAsset(A.MeshName, /*bPreferChar*/true, A.MeshObjectPath);
	if (!A.MeshName.IsEmpty())
	{
		A.TexName = A.MeshName + TEXT("_t");
		A.TexObjectPath = FindTexAsset(A.TexName, /*bCharSpace*/true);
	}

	// Иконка баффа: атлас `Field197` (+197) + ячейка байт +229; наличие — байт +116 (== 2).
	// ★ A4: через общую точку `AsdaBytes::HexByteAt`. Она отдаёт −1 на обрыве/мусоре, а поля
	//   здесь только СРАВНИВАЮТСЯ с ожидаемыми значениями (== 2 / индекс ячейки), поэтому −1
	//   приводим к 0 — ровно то «нет поля», которое отдавала прежняя копия за границей строки.
	const int32 RawFlag116 = AsdaBytes::HexByteAt(R->DataHex, 116);
	const int32 RawIconCell = AsdaBytes::HexByteAt(R->DataHex, 229);
	A.BuffFlag116  = (RawFlag116  < 0) ? 0 : RawFlag116;
	A.BuffIconCell = (RawIconCell < 0) ? 0 : RawIconCell;
	A.BuffIconAtlas = R->Field197.TrimStartAndEnd();
	if (!A.BuffIconAtlas.IsEmpty())
	{
		FString Pkg;
		if (FindPackageFlat(AsdaAssetPaths::kJuiPrefix, A.BuffIconAtlas, Pkg))
		{
			A.BuffIconObjectPath = ObjectPathOf(Pkg);
		}
	}
}

FAsdaBrowserItemAssets FAsdaItemBrowserData::ResolveAssets(int32 Index, bool bMale, int32 Enhance) const
{
	FAsdaBrowserItemAssets A;
	const FAsdaBrowserEntry* E = GetEntry(Index);
	if (!E) { return A; }

	A.Space = E->Space;
	A.ItemCode = E->Code;
	A.Name = GetName(Index);
	A.bHasName = !A.Name.IsEmpty();
	A.Description = GetDescription(E->Space, E->Code);
	A.DescriptionTable = DescriptionTableName(E->Space);

	if (const FAsdaItemRow* R = GetItemRow(Index))                    { ResolveItemListAssets(A, R, bMale, Enhance); }
	else if (const FAsdaSowellRow* S = GetSowelRow(Index))            { ResolveSowelAssets(A, S); }
	else if (const FAsdaSpecialItemlistRow* P = GetSpecialRow(Index)) { ResolveSpecialAssets(A, P); }
	else { return A; }

	A.Icon = ResolveIcon(Index, bMale);

	// Эффект свечения заточки (только item_list — оружие/щиты).
	A.GlowJebName = BuildGlowJebName(Index, Enhance);
	if (!A.GlowJebName.IsEmpty())
	{
		const FString BpName = AsdaAssetPaths::FxBpName(A.GlowJebName);   // ★ волна 2 п. 7: правила реестра путей
		const FString Pkg = AsdaAssetPaths::FxPackageFromBp(BpName);
		if (PackageExists(Pkg))
		{
			A.GlowBpObjectPath = AsdaAssetPaths::FxClassPathFromBp(BpName);   // = Pkg + "." + BpName + "_C"
		}
	}
	return A;
}

// ════════════════════════════════════════════════════════════════════════════
// ★★★ 29.10.2026 — ТОЧНАЯ РАЗБИВКА (шапка — AsdaItemBrowserData.h)
// ════════════════════════════════════════════════════════════════════════════

// Строка GameText по-русски без тегов (подписи разбивки — язык данных порта). "" — строки нет.
static FString LocRu(int32 Id)
{
	FAsdaLangScope Ru(EAsdaLang::Ru);
	return FAsdaText::Loc(Id).TrimStartAndEnd();
}

// Подпись из GameText с запасной (если строки нет) — запасная помечается номером нотиса, чтобы было видно источник.
static FString LocOr(int32 Id, const TCHAR* Fallback)
{
	const FString S = LocRu(Id);
	return S.IsEmpty() ? FString::Printf(TEXT("%s [#%d]"), Fallback, Id) : S;
}

// `itemTypeToCategory_44F0@0xD444F0` (item_inventory.c:47062) — подтип расходника → вкладка биржи (1…5),
//   1:1 с `AsdaExch_SubTypeToCategory` (AsdaExchangeSubsystem.cpp; функция там во внутреннем namespace).
static int32 ExchSubTypeToCategory(int32 SubType)
{
	const uint32 S = (uint32)(uint8)SubType;
	if (S < 6)                            { return 1; }
	if (S < 14 || (S >= 44 && S <= 53))   { return 2; }
	if (S == 14)                          { return 3; }
	if (S >= 15 && S < 17)                { return 4; }
	if (S >= 70 && S <= 89)               { return 4; }
	if (S == 17)                          { return 5; }
	if (S >= 105 && S <= 109)             { return 3; }
	return 5;
}

// `item_getLevelTier@0xD53030` (item_inventory.c:49279): 0 → 1; 1…100 → десяток; иначе — вне шкалы (11).
static int32 LevelTierOf(int32 Level)
{
	if (Level >= 1 && Level <= 100) { return (Level - 1) / 10 + 1; }
	if (Level == 0) { return 1; }
	return 11;
}

void FAsdaItemBrowserData::ComputeFacets(int32 Index, FAsdaBrowserFacets& Out) const
{
	Out = FAsdaBrowserFacets();
	if (const FAsdaItemRow* R = GetItemRow(Index))
	{
		const int32 T = R->ItemType, S = R->ItemSubType;
		Out.Sub = S;
		switch (T)
		{
		case 9:
			Out.Cat = EAsdaItemCat::Weapon;
			// Секция 3 подсказки: у `itemVariant == 1` вид — «Удочка» (111074), иначе 8552 + подтип.
			if (R->ItemVariant == 1) { Out.Sub = 1000; }
			break;
		case 8:  Out.Cat = EAsdaItemCat::Shield;   break;
		case 0:  Out.Cat = EAsdaItemCat::Helmet;   break;
		case 1:  Out.Cat = EAsdaItemCat::Armor;    break;
		case 2:  Out.Cat = EAsdaItemCat::Pants;    break;
		case 3:  Out.Cat = EAsdaItemCat::Boots;    break;
		case 4:  Out.Cat = EAsdaItemCat::Gloves;   break;
		case 5:  Out.Cat = EAsdaItemCat::Ring;     break;
		case 7:  Out.Cat = EAsdaItemCat::Necklace; break;
		case 10: Out.Cat = EAsdaItemCat::Ammo;     break;
		case 13: Out.Cat = EAsdaItemCat::Booster;  break;
		case 11:
			if (S == 200 || S == 201)       { Out.Cat = EAsdaItemCat::Box; }        // коробки/пакеты (`IsPackageItem`)
			else if (S >= 60 && S <= 68)    { Out.Cat = EAsdaItemCat::Material; }   // `itemGetCategoryIndexBySubtype`
			else
			{
				switch (ExchSubTypeToCategory(S))
				{
				case 1:  Out.Cat = EAsdaItemCat::Potion;    break;
				case 2:  Out.Cat = EAsdaItemCat::HolyWater; break;
				case 3:  Out.Cat = EAsdaItemCat::CardPet;   break;
				case 4:  Out.Cat = EAsdaItemCat::Scroll;    break;
				default: Out.Cat = EAsdaItemCat::Misc11;    break;
				}
			}
			break;
		default:
			Out.Cat = EAsdaItemCat::OtherType;
			Out.Sub = T;      // незнакомый тип — ветка по номеру типа
			break;
		}
		Out.Usage = R->UsageCategory;
		Out.ClassRestriction = R->ClassRestriction;
		Out.LvMin = R->RequiredLevel;
		Out.LvMax = R->RequiredLevelMax;
		Out.Grade = R->ItemGrade;
		Out.bPremium = (R->CategoryFlag == 3);
	}
	else if (const FAsdaSowellRow* W = GetSowelRow(Index))
	{
		Out.Cat = EAsdaItemCat::Sowel;
		Out.Sub = W->BonusStatType;
		Out.LvMin = W->RequiredLevel;
		Out.Grade = W->GradeColorIndex;
	}
	else if (const FAsdaSpecialItemlistRow* P = GetSpecialRow(Index))
	{
		Out.Cat = EAsdaItemCat::Special;
		Out.Sub = P->SubType;
		Out.LvMin = P->ItemLevel;
		Out.Grade = P->DropColorIndex;
		Out.bPremium = true;
	}
	Out.Tier = LevelTierOf(Out.LvMin);
}

FString FAsdaItemBrowserData::CatLabel(EAsdaItemCat Cat)
{
	switch (Cat)
	{
	// «Место в снаряжении» — нотис 8569 + itemType (подсказка самоцвета, `CItemManager_GetSowelOptionText`).
	case EAsdaItemCat::Weapon:   return LocOr(8569 + 9, TEXT("Оружие"));
	case EAsdaItemCat::Shield:   return LocOr(8569 + 8, TEXT("Щит"));
	case EAsdaItemCat::Helmet:   return LocOr(8569 + 0, TEXT("Шлем"));
	case EAsdaItemCat::Armor:    return LocOr(8569 + 1, TEXT("Доспех"));
	case EAsdaItemCat::Pants:    return LocOr(8569 + 2, TEXT("Штаны"));
	case EAsdaItemCat::Boots:    return LocOr(8569 + 3, TEXT("Ботинки"));
	case EAsdaItemCat::Gloves:   return LocOr(8569 + 4, TEXT("Перчатки"));
	// Вкладки биржи 12528/12529.
	case EAsdaItemCat::Ring:     return LocOr(12528, TEXT("Кольцо"));
	case EAsdaItemCat::Necklace: return LocOr(12529, TEXT("Ожерелье"));
	// [ПОРТ] Подписей у этих групп в тексте игры нет — названы по составу и правилу, которое их выделяет.
	case EAsdaItemCat::Ammo:      return TEXT("Колчаны, заряды, наживка (тип 10)");
	case EAsdaItemCat::Potion:    return TEXT("Зелья, эликсиры, лекарства (тип 11, подтипы 0–5)");
	case EAsdaItemCat::HolyWater: return FString::Printf(TEXT("%s, эликсиры ЖК (тип 11, подтипы 6–13, 44–53)"), *LocOr(12531, TEXT("Святая вода")));
	case EAsdaItemCat::Scroll:    return FString::Printf(TEXT("%s (тип 11, подтипы 15–16, 70–89)"), *LocOr(12532, TEXT("Свиток")));
	case EAsdaItemCat::Material:  return TEXT("Материалы ремесла (тип 11, подтипы 60–68)");
	case EAsdaItemCat::CardPet:   return TEXT("Карточки, питомцы (тип 11, подтипы 14, 105–109)");
	case EAsdaItemCat::Box:       return TEXT("Коробки и пакеты (тип 11, подтипы 200–201)");
	case EAsdaItemCat::Misc11:    return TEXT("Прочие расходники (тип 11)");
	case EAsdaItemCat::Booster:   return TEXT("Бустеры (тип 13)");
	case EAsdaItemCat::OtherType: return TEXT("Прочие типы item_list");
	case EAsdaItemCat::Sowel:     return FString::Printf(TEXT("%s — sowell_list"), *LocOr(8584, TEXT("Живой камень")));
	case EAsdaItemCat::Special:   return TEXT("Спец-предметы (премиум/бустеры) — special_itemlist");
	default:                      return TEXT("?");
	}
}

FString FAsdaItemBrowserData::SubLabel(EAsdaItemCat Cat, int32 Sub, int32 SampleIndex) const
{
	const FString Sample = (SampleIndex != INDEX_NONE) ? GetName(SampleIndex) : FString();
	auto WithSample = [&Sample](const FString& Head)
	{
		return Sample.IsEmpty() ? Head : FString::Printf(TEXT("%s (напр. «%s»)"), *Head, *Sample);
	};
	switch (Cat)
	{
	case EAsdaItemCat::Weapon:
		// Секция 3 подсказки: нотис 9564 «Вид» : 8552 + подтип либо 111074 «Удочка».
		return (Sub == 1000) ? LocOr(111074, TEXT("Удочка"))
		                     : FString::Printf(TEXT("%s (подтип %d)"), *LocOr(8552 + Sub, TEXT("Вид")), Sub);
	case EAsdaItemCat::Material:
		// Пункты 13410 биржи 12747…12755 (Камень/Дерево/Металл/Кожа/Ткань/Кованое изделие/Запчасть/Тип карточки/Разное).
		return FString::Printf(TEXT("%s (подтип %d)"), *LocOr(12747 + (Sub - 60), TEXT("Материал")), Sub);
	case EAsdaItemCat::Sowel:
	{
		// Имя бонуса самоцвета — нотис 8510 + BonusStatType (строка «Владелец …» подсказки).
		FString B = LocRu(8510 + Sub);
		B.TrimStartAndEndInline();
		while (B.EndsWith(TEXT("+")) || B.EndsWith(TEXT(":"))) { B.LeftChopInline(1); B.TrimEndInline(); }
		return B.IsEmpty() ? FString::Printf(TEXT("Бонус %d"), Sub) : FString::Printf(TEXT("%s (бонус %d)"), *B, Sub);
	}
	case EAsdaItemCat::Special:
		return WithSample(FString::Printf(TEXT("Функция %d"), Sub));
	case EAsdaItemCat::OtherType:
		return WithSample(FString::Printf(TEXT("itemType %d"), Sub));
	default:
		return WithSample(FString::Printf(TEXT("Подтип %d"), Sub));
	}
}

int32 FAsdaItemBrowserData::JobBranch(int32 Usage)
{
	if (Usage <= 0)  { return 0; }
	if (Usage <= 11) { return 1; }
	if (Usage <= 22) { return 2; }
	if (Usage <= 33) { return 3; }
	if (Usage <= 39) { return 4; }
	if (Usage <= 44) { return 5; }
	return 6;
}

FString FAsdaItemBrowserData::JobBranchLabel(int32 Branch) const
{
	// Архетип — `ParentJob` первой строки ветки в DT_jobtype (Боец/Лучник/Маг/Вторая профессия).
	static const int32 kFirst[] = { 0, 1, 12, 23, 34, 40 };
	static const TCHAR* const kRange[] = { TEXT("0"), TEXT("1–11"), TEXT("12–22"), TEXT("23–33"), TEXT("34–39"), TEXT("40–44") };
	if (Branch >= 0 && Branch <= 5)
	{
		const FString P = (Branch == 0) ? JobName(0) : (Branch == 5 ? FString(TEXT("Ранг профессии любой ветки")) : JobParent(kFirst[Branch]));
		return FString::Printf(TEXT("%s (требование %s)"), P.IsEmpty() ? TEXT("?") : *P, kRange[Branch]);
	}
	return TEXT("Требование вне 0…44");
}

FString FAsdaItemBrowserData::TierLabel(int32 Tier)
{
	if (Tier >= 1 && Tier <= 10) { return FString::Printf(TEXT("Уровень %s"), *LocOr(12729 + Tier - 1, TEXT("диапазон"))); }
	return TEXT("Уровень свыше 100");
}

FString FAsdaItemBrowserData::GradeLabel(int32 Grade)
{
	return FString::Printf(TEXT("%s (грейд %d)"), *LocOr(1421 + Grade, TEXT("Грейд")), Grade);
}

FLinearColor FAsdaItemBrowserData::GradeColor(int32 Grade)
{
	return AsdaUi::SrgbU32ToLinear(UAsdaItemTooltipSubsystem::GradeColorArgb(Grade));
}

FString FAsdaItemBrowserData::GetDescriptionEn(EAsdaItemSpace Space, int32 Code) const
{
	EnsureTooltipLoaded();
	FString Out;
	switch (Space)
	{
	case EAsdaItemSpace::SowelList:
		if (const FAsdaSowellTooltipRow* const* F = SowelTooltipById.Find(Code))
		{
			const FName Row = AsdaLangRows::RowNameOf(SowelTooltipTable.Get(), *F);
			if (!Row.IsNone()) { FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("sowell_tooltip"), Row.ToString() + TEXT(".Text"), Out); }
		}
		break;
	case EAsdaItemSpace::SpecialList:
		if (const FAsdaSpecialitemTooltiplistRow* const* F = SpecialTooltipById.Find(Code))
		{
			const FName Row = AsdaLangRows::RowNameOf(SpecialTooltipTable.Get(), *F);
			if (!Row.IsNone()) { FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("specialitem_tooltiplist"), Row.ToString() + TEXT(".Tooltip"), Out); }
		}
		break;
	default:
		if (const FAsdaItemTooltipRow* const* F = TooltipByCode.Find(Code))
		{
			const FName Row = AsdaLangRows::RowNameOf(TooltipTable.Get(), *F);
			if (!Row.IsNone()) { FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("item_tooltip"), Row.ToString() + TEXT(".Description"), Out); }
		}
		break;
	}
	return Out;
}

void FAsdaItemBrowserData::SetSiblings(int32 Index, TArray<int32>& OutIndices) const
{
	OutIndices.Reset();
	const FAsdaItemRow* R = GetItemRow(Index);
	if (!R || R->SetCode == 0) { return; }
	SetCodeIndex.MultiFind(R->SetCode, OutIndices);
	OutIndices.Remove(Index);
	OutIndices.Sort([this](int32 A, int32 B) { return GetCode(A) < GetCode(B); });
}

void FAsdaItemBrowserData::Reload()
{
	bLoaded = false;
	bTooltipTried = false;
	TooltipByCode.Reset(); SowelTooltipById.Reset(); SpecialTooltipById.Reset();
	TooltipTable.Reset(); SowelTooltipTable.Reset(); SpecialTooltipTable.Reset();
	ItemTable.Reset(); ImgTable.Reset(); SowelTable.Reset(); SpecialTable.Reset(); JobTable.Reset();
	Items.Reset(); Imgs.Reset(); Sowels.Reset(); Specials.Reset();
	Entries.Reset(); Facets.Reset(); NamesEn.Reset(); SearchKey.Reset();
	CodeToIndex.Reset(); SowelCodeToIndex.Reset(); SpecialCodeToIndex.Reset(); SetCodeIndex.Reset();
	EnsureLoaded();
}

bool FAsdaItemBrowserData::PassesFilter(int32 Index, const FAsdaBrowserFilter& F) const
{
	const FAsdaBrowserFacets* Fc = GetFacets(Index);
	if (!Fc) { return false; }
	if (F.Space.IsSet() && GetSpace(Index) != F.Space.GetValue()) { return false; }
	if (F.Cat >= 0 && (int32)Fc->Cat != F.Cat) { return false; }
	if (F.Grade >= 0 && Fc->Grade != F.Grade) { return false; }
	if (F.Premium >= 0 && (Fc->bPremium ? 1 : 0) != F.Premium) { return false; }
	if (F.LvFrom > 0 && Fc->LvMin < F.LvFrom) { return false; }
	if (F.LvTo > 0 && Fc->LvMin > F.LvTo) { return false; }
	if (F.CharLevel > 0)
	{
		if (Fc->LvMin > F.CharLevel) { return false; }
		if (Fc->LvMax > 0 && Fc->LvMax < F.CharLevel) { return false; }
	}
	if (F.Job >= 0)
	{
		// У самоцвета/спец-предмета требования класса нет — под фильтр класса они не попадают.
		if (Fc->Usage < 0) { return false; }
		switch (F.JobMode)
		{
		case EAsdaJobFilterMode::Branch:    if (JobBranch(Fc->Usage) != JobBranch(F.Job)) { return false; } break;
		case EAsdaJobFilterMode::Reachable: if (!UAsdaItemTooltipSubsystem::CheckGradeReachable(F.Job, Fc->Usage)) { return false; } break;
		default:                            if (Fc->Usage != F.Job) { return false; } break;
		}
	}
	// Текст фильтра уже в нижнем регистре (BuildTreeEx), ключ поиска — тоже.
	if (!F.Text.IsEmpty() && SearchKey.IsValidIndex(Index))
	{
		if (!SearchKey[Index].Contains(F.Text, ESearchCase::CaseSensitive)) { return false; }
	}
	return true;
}

// Раздел верхнего уровня группировки «по типу».
static int32 SectionOf(EAsdaItemCat Cat)
{
	if (Cat <= EAsdaItemCat::Gloves)    { return 0; }   // снаряжение
	if (Cat <= EAsdaItemCat::Necklace)  { return 1; }   // аксессуары
	if (Cat <= EAsdaItemCat::OtherType) { return 2; }   // расходники и прочее
	if (Cat == EAsdaItemCat::Sowel)     { return 3; }
	return 4;
}

static FString SectionLabel(int32 S)
{
	switch (S)
	{
	case 0:  return LocOr(98201, TEXT("Снаряжение"));                 // вкладка инвентаря «Снаряжение»
	case 1:  return TEXT("Аксессуары (кольца, ожерелья)");
	case 2:  return FString::Printf(TEXT("%s и прочее"), *LocOr(98200, TEXT("Расходники")));   // вкладка «Расходники»
	case 3:  return FString::Printf(TEXT("%s (самоцветы)"), *LocOr(8584, TEXT("Живой камень")));
	default: return TEXT("Спец-предметы (special_itemlist)");
	}
}

void FAsdaItemBrowserData::PathOf(int32 Index, EAsdaBrowserGrouping G, TArray<FPathKey>& Out) const
{
	Out.Reset();
	const FAsdaBrowserFacets* Fc = GetFacets(Index);
	if (!Fc) { return; }
	auto Add = [&Out](int64 K, const FString& L) { FPathKey P; P.Key = K; P.Label = L; Out.Add(P); };
	switch (G)
	{
	case EAsdaBrowserGrouping::ByClass:
	{
		const int32 Br = (Fc->Usage < 0) ? 7 : JobBranch(Fc->Usage);
		Add(Br, Br == 7 ? FString(TEXT("Без требования класса (самоцветы, спец-предметы)")) : JobBranchLabel(Br));
		// Ветка 0 («для всех») — одна профессия: второй уровень с тем же именем не нужен.
		if (Fc->Usage >= 0 && Br != 0)
		{
			const FString J = JobName(Fc->Usage);
			Add(Fc->Usage, FString::Printf(TEXT("%s (требование %d)"), J.IsEmpty() ? TEXT("?") : *J, Fc->Usage));
		}
		Add((int32)Fc->Cat, CatLabel(Fc->Cat));
		break;
	}
	case EAsdaBrowserGrouping::ByLevel:
		Add(Fc->Tier, TierLabel(Fc->Tier));
		Add((int32)Fc->Cat, CatLabel(Fc->Cat));
		break;
	case EAsdaBrowserGrouping::ByGrade:
	{
		FPathKey P; P.Key = Fc->Grade; P.Label = GradeLabel(Fc->Grade); P.Color = GradeColor(Fc->Grade); P.bColored = true;
		Out.Add(P);
		Add((int32)Fc->Cat, CatLabel(Fc->Cat));
		break;
	}
	default:
	{
		// Самоцветы и спец-предметы — своя таблица = своя категория: раздел над ней был бы повтором подписи.
		const int32 S = SectionOf(Fc->Cat);
		if (S <= 2) { Add(S, SectionLabel(S)); }
		Add((int32)Fc->Cat, CatLabel(Fc->Cat));
		Add(Fc->Sub, FString());   // подпись подкатегории — с примером имени, ставится при сборке
		break;
	}
	}
}

void FAsdaItemBrowserData::BuildTreeEx(const FAsdaBrowserFilter& F, TArray<TSharedPtr<FAsdaBrowserNode>>& OutRoots, int32& OutShown) const
{
	OutRoots.Reset();
	OutShown = 0;
	if (!bLoaded) { return; }

	const FString Q = F.Text.TrimStartAndEnd();
	bool bNumeric = !Q.IsEmpty();
	for (const TCHAR C : Q) { if (!FChar::IsDigit(C)) { bNumeric = false; break; } }
	FAsdaBrowserFilter FF = F;
	FF.Text = Q.ToLower();

	auto MakeItemNode = [this](int32 GlobalIndex)
	{
		TSharedPtr<FAsdaBrowserNode> N = MakeShared<FAsdaBrowserNode>();
		N->Kind = EAsdaBrowserNodeKind::Item;
		N->Space = GetSpace(GlobalIndex);
		N->ItemIndex = GlobalIndex;
		return N;
	};

	// ── Корень «точное совпадение по коду» (без прочих фильтров: ищут код — показываем код) ──
	if (bNumeric)
	{
		const int32 Code = FCString::Atoi(*Q);
		TSharedPtr<FAsdaBrowserNode> Hit = MakeShared<FAsdaBrowserNode>();
		Hit->Kind = EAsdaBrowserNodeKind::CodeHit;
		for (uint8 S = 0; S < (uint8)EAsdaItemSpace::Count; ++S)
		{
			const int32 Idx = IndexByCodeInSpace((EAsdaItemSpace)S, Code);
			if (Idx == INDEX_NONE) { continue; }
			Hit->Children.Add(MakeItemNode(Idx));
			++Hit->Count;
		}
		if (Hit->Count > 0)
		{
			Hit->Label = (Hit->Count == 1)
				? FString::Printf(TEXT("★ Точное совпадение по коду %d (таблица %s)"), Code, AsdaSpaceTable(Hit->Children[0]->Space))
				: FString::Printf(TEXT("★ Точное совпадение по коду %d — %d в РАЗНЫХ таблицах"), Code, Hit->Count);
			OutRoots.Add(Hit);
		}
	}

	// ── Ветки группировки ── (узел пути держит детей по ключу; предметы — в листе пути)
	struct FBuild
	{
		TSharedPtr<FAsdaBrowserNode> Node;
		TMap<int64, TSharedPtr<FBuild>> Kids;
		int32 FirstItem = INDEX_NONE;   // пример имени для подписи подкатегории (первая запись с именем)
	};
	FBuild Root;
	Root.Node = MakeShared<FAsdaBrowserNode>();
	TArray<FPathKey> Path;
	const bool bTypeGrouping = (F.Grouping == EAsdaBrowserGrouping::ByType);

	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (!PassesFilter(i, FF)) { continue; }
		++OutShown;
		PathOf(i, F.Grouping, Path);
		FBuild* Cur = &Root;
		for (int32 L = 0; L < Path.Num(); ++L)
		{
			TSharedPtr<FBuild>& K = Cur->Kids.FindOrAdd(Path[L].Key);
			if (!K.IsValid())
			{
				K = MakeShared<FBuild>();
				K->Node = MakeShared<FAsdaBrowserNode>();
				K->Node->Kind = (L + 1 == Path.Num()) ? EAsdaBrowserNodeKind::SubCategory : EAsdaBrowserNodeKind::Category;
				K->Node->Space = GetSpace(i);
				K->Node->TypeId = (int32)Facets[i].Cat;
				K->Node->SubId = (int32)Path[L].Key;
				K->Node->Label = Path[L].Label;
				K->Node->LabelColor = Path[L].Color;
				K->Node->bColored = Path[L].bColored;
				K->FirstItem = i;
			}
			++K->Node->Count;
			if (GetName(K->FirstItem).IsEmpty() && !GetName(i).IsEmpty()) { K->FirstItem = i; }
			Cur = K.Get();
		}
		Cur->Node->Children.Add(MakeItemNode(i));
	}

	// Сборка: дети по возрастанию ключа, подписи «… — N»; у ветки «по типу» с ОДНИМ подтипом подкатегорию снимаем.
	TFunction<void(FBuild&, int32)> Finish = [&](FBuild& B, int32 Depth)
	{
		TArray<int64> Keys; B.Kids.GetKeys(Keys); Keys.Sort();
		for (int64 K : Keys)
		{
			FBuild& C = *B.Kids[K];
			if (bTypeGrouping && C.Kids.Num() == 0)
			{
				C.Node->Label = SubLabel((EAsdaItemCat)C.Node->TypeId, (int32)K, C.FirstItem);
			}
			Finish(C, Depth + 1);
			C.Node->Label = FString::Printf(TEXT("%s — %d"), *C.Node->Label, C.Node->Count);
			B.Node->Children.Add(C.Node);
		}
		if (bTypeGrouping && Depth >= 1 && B.Node->Children.Num() == 1
			&& B.Node->Children[0]->Kind == EAsdaBrowserNodeKind::SubCategory)
		{
			TArray<TSharedPtr<FAsdaBrowserNode>> Only = B.Node->Children[0]->Children;
			B.Node->Children = MoveTemp(Only);
		}
	};
	Finish(Root, 0);
	// Предметы внутри листа уже идут по возрастанию глобального индекса = порядок строк таблицы (= порядок кодов).
	for (const TSharedPtr<FAsdaBrowserNode>& N : Root.Node->Children) { OutRoots.Add(N); }
}
