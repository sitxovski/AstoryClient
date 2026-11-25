// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaFxLightSettings.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogAsdaFxLight);   // ★ волна 3 п.12 (15.09.2026): объявление — World/AsdaFxLightSettings.h

// ★ 13.09.2026: ревизия таблиц Families/Overrides (ключ кэшей индексов на CDO и на акторах эффектов).
//   Процесс один, таблицы правит только игровой поток (конфигуратор, консоль, Project Settings).
static int32 GAsdaFxLightTablesRevision = 0;

UAsdaFxLightSettings::UAsdaFxLightSettings()
{
	// Значения приходят из Config/DefaultGame.ini (генератор Tools/asda_gen_fx_light_ini.py из Tools/asda_fx_light.json);
	// в конструкторе — только безопасные умолчания на случай, если секции ini нет вовсе (тогда таблица пуста = света нет ни у кого)

	// ★ 27.10.2026 (владелец): тёмные карты, освещённые свечами-эффектами, — уровень 3 (Epic), как владелец видел их в редакторе.
	MapEffectsLevel.Add(TEXT("castle_of_chaos"), 3);
	MapEffectsLevel.Add(TEXT("castle_of_chess"), 3);
	MapEffectsLevel.Add(TEXT("t_castle_of_chess"), 3);     // Distorted Castle of Chess
	MapEffectsLevel.Add(TEXT("palace_of_queen"), 3);
	MapEffectsLevel.Add(TEXT("t_palace_of_queen"), 3);     // Distorted Palace of Queen
}

// ★ 14.09.2026 (реестр настроек D7): проверка метки чтения секции — один раз за процесс, при первом обращении к настройкам.
//   Только лог: значения и поведение не меняются. Гонка потоков здесь безвредна (в худшем случае строка лога повторится).
static void AsdaFxLightSettingsCheckStamp(const UAsdaFxLightSettings* S)
{
	static bool bChecked = false;   // проверка уже выполнялась в этом процессе
	if (bChecked || !S) { return; }
	bChecked = true;
	if (S->ConfigStamp == 0)
	{
		UE_LOG(LogAsdaFxLight, Warning, TEXT("[Эффект-света] Секция [/Script/AsdaReborn.AsdaFxLightSettings] из Config/DefaultGame.ini НЕ ПРОЧИТАНА "
			"(ConfigStamp=0): таблица семейств и бюджеты работают на умолчаниях C++ (правил %d) — свет эффектов не по данным. "
			"Проверьте секцию или перегенерируйте: python Tools/asda_gen_fx_light_ini.py"), S->Families.Num());
	}
	else
	{
		UE_LOG(LogAsdaFxLight, Log, TEXT("[Эффект-света] ini-секция прочитана: ConfigStamp=%d, правил семейств %d, переопределений %d"), S->ConfigStamp, S->Families.Num(), S->Overrides.Num());
	}
}

const UAsdaFxLightSettings* UAsdaFxLightSettings::Get()
{
	const UAsdaFxLightSettings* S = GetDefault<UAsdaFxLightSettings>();
	AsdaFxLightSettingsCheckStamp(S);
	return S;
}

int32 UAsdaFxLightSettings::GetTablesRevision()
{
	return GAsdaFxLightTablesRevision;
}

void UAsdaFxLightSettings::BumpTablesRevision()
{
	++GAsdaFxLightTablesRevision;
}

float UAsdaFxLightSettings::FindWorldLengthScale(FName WorldName) const
{
	// Имя приходит уже без PIE-префикса (UEDPIE_0_Login → Login) — это делает вызывающий
	for (const FAsdaFxLightWorldScale& E : WorldScales)
	{
		if (E.World == WorldName && E.LengthScale > 0.f) { return E.LengthScale; }
	}
	return 1.f;
}

const FAsdaFxLightRule* UAsdaFxLightSettings::FindRule(const FString& JebName) const
{
	if (JebName.IsEmpty() || Families.Num() == 0) { return nullptr; }

	// Кэш скомпилированных шаблонов: FRegexPattern строит ICU-автомат, и делать это на каждый эффект карты
	// (castle_of_chess — 468 акторов × 11 правил) было бы 5 148 компиляций на загрузку.
	// ★ 13.09.2026: ключ кэша — ещё и ревизия таблиц: конфигуратор перечитывает ini, и при том же числе
	//   правил шаблон мог смениться (без этого кэш молча держал бы старые регулярки до перезапуска).
	if (CompiledFor != Families.Num() || CompiledRev != GAsdaFxLightTablesRevision)
	{
		Compiled.Reset();
		Compiled.Reserve(Families.Num());
		for (const FAsdaFxLightRule& R : Families)
		{
			Compiled.Add(R.Jeb.IsEmpty()
				? TSharedPtr<FRegexPattern>()
				: TSharedPtr<FRegexPattern>(MakeShared<FRegexPattern>(R.Jeb, ERegexPatternFlags::CaseInsensitive)));
		}
		CompiledFor = Families.Num();
		CompiledRev = GAsdaFxLightTablesRevision;
	}

	// СВЕРХУ ВНИЗ, срабатывает ПЕРВОЕ подходящее — включая правила-щиты (bEnabled = false):
	// щит обязан перехватить эффект раньше, чем до него доберётся правило ниже (порядок — часть данных).
	for (int32 i = 0; i < Families.Num(); ++i)
	{
		if (!Compiled.IsValidIndex(i) || !Compiled[i].IsValid()) { continue; }
		FRegexMatcher M(*Compiled[i], JebName);
		if (M.FindNext()) { return &Families[i]; }
	}
	return nullptr;
}

const FAsdaFxGlowOverride* UAsdaFxLightSettings::FindOverride(const FString& JebName, int32 ItemId) const
{
	if (JebName.IsEmpty() || Overrides.Num() == 0) { return nullptr; }

	// Индекс пересобирается по ревизии и числу записей: вызывается из тика света и из пересборки секций,
	// линейный проход с ToLower по сотням записей на каждый вызов был бы мусором на ровном месте.
	if (OverrideIndexRev != GAsdaFxLightTablesRevision || OverrideIndexNum != Overrides.Num())
	{
		OverrideIndex.Reset();
		for (int32 i = 0; i < Overrides.Num(); ++i)
		{
			const FAsdaFxGlowOverride& O = Overrides[i];
			if (O.Jeb.IsEmpty()) { continue; }
			// При повторе ключа выигрывает ПЕРВАЯ запись (генератор повторов не пропускает, это страховка ручной правки ini)
			OverrideIndex.FindOrAdd(FString::Printf(TEXT("%s|%d"), *O.Jeb.ToLower(), O.ItemId), i);
		}
		OverrideIndexRev = GAsdaFxLightTablesRevision;
		OverrideIndexNum = Overrides.Num();
	}

	const FString Low = JebName.ToLower();
	// 1. Этот предмет + этот эффект
	if (ItemId != 0)
	{
		if (const int32* P = OverrideIndex.Find(FString::Printf(TEXT("%s|%d"), *Low, ItemId)))
		{
			return Overrides.IsValidIndex(*P) ? &Overrides[*P] : nullptr;
		}
	}
	// 2. Этот эффект у всех предметов (и у эффектов без предмета)
	if (const int32* P = OverrideIndex.Find(Low + TEXT("|0")))
	{
		return Overrides.IsValidIndex(*P) ? &Overrides[*P] : nullptr;
	}
	return nullptr;
}

// ★ волна 3 п.16 (15.09.2026): линейная форма той же формулы, что FindOverride (см. объявление)
const FAsdaFxGlowOverride* UAsdaFxLightSettings::FindOverrideIn(const TArray<FAsdaFxGlowOverride>& Arr, const FString& JebName, int32 ItemId)
{
	if (JebName.IsEmpty() || Arr.Num() == 0) { return nullptr; }
	// 1. Этот предмет + этот эффект
	if (ItemId != 0)
	{
		const int32 Own = IndexOfOverride(Arr, JebName, ItemId);
		if (Own != INDEX_NONE) { return &Arr[Own]; }
	}
	// 2. Этот эффект у всех предметов (и у эффектов без предмета)
	const int32 Eff = IndexOfOverride(Arr, JebName, 0);
	return Eff != INDEX_NONE ? &Arr[Eff] : nullptr;
}

int32 UAsdaFxLightSettings::IndexOfOverride(const TArray<FAsdaFxGlowOverride>& Arr, const FString& JebName, int32 ItemId)
{
	return Arr.IndexOfByPredicate([&JebName, ItemId](const FAsdaFxGlowOverride& O) { return O.ItemId == ItemId && O.Jeb.Equals(JebName, ESearchCase::IgnoreCase); });
}

// ═══ ★ волна 3 п.16 (15.09.2026): ИМЯ .jeb СВЕЧЕНИЯ ЗАТОЧКИ (AsdaWeaponGlow) ═══════════════════════════════════════════
// Таблица баз по itemSubType (эталон 0xA4CB10; строки в EXE @0x13AEA40/3C/38/30/2C/24/20). ★ Её разбирает
//   Tools/asda_gen_fx_light_ini.py --check — формат строк «{ подтип, TEXT("база") }» не менять.
static const struct { int32 SubType; const TCHAR* Base; } GAsdaWeaponGlowBases[] = {
	{ 0, TEXT("ohs") },     // одноручный меч
	{ 1, TEXT("ohs") },     // одноручный меч
	{ 2, TEXT("spr") },     // копьё
	{ 3, TEXT("ths") },     // двуручный меч
	{ 5, TEXT("stf") },     // посох
	{ 8, TEXT("bow") },     // лук
	{ 9, TEXT("xbow") },    // арбалет
	{ 10, TEXT("bow") },    // лук
	{ 11, TEXT("bali") },   // балиста
};
// База щита (эталон itemBuildJebEffectName, строка @0x13AEAC8) — разбирается тем же --check
static const TCHAR* const GAsdaShieldGlowBase = TEXT("shd");

const TCHAR* AsdaWeaponGlow::BaseByWeaponSubType(int32 SubType)
{
	for (const auto& E : GAsdaWeaponGlowBases) { if (E.SubType == SubType) { return E.Base; } }
	return nullptr;   // прочие подтипы свечения не имеют (эталон: Buffer пуст)
}

const TCHAR* AsdaWeaponGlow::ShieldBase() { return GAsdaShieldGlowBase; }

bool AsdaWeaponGlow::IsTestGlowItem(int32 ItemId) { return ItemId == 41956 || ItemId == 56048; }

const TCHAR* AsdaWeaponGlow::TestGlowJeb() { return TEXT("test-000"); }

FString AsdaWeaponGlow::ComposeJeb(bool bShield, int32 WeaponSubType, int32 ImgSubType, int32 ItemGrade, int32 Enhance)
{
	if (Enhance < MinEnhance) { return FString(); }
	const TCHAR* Base = bShield ? ShieldBase() : BaseByWeaponSubType(WeaponSubType);
	if (!Base) { return FString(); }
	// Буква модели: ImgRecord+212 (1..12 → a..l). 0 = записи эффекта нет (эталон оставляет поле пустым,
	//   имя "base__grade_ma_NN.jeb" в ресурсах отсутствует ⇒ свечения нет).
	if (ImgSubType < 1 || ImgSubType > 12) { return FString(); }
	const TCHAR Letter = (TCHAR)('a' + ImgSubType - 1);
	// Редкость предмета (ItemRecord+0xEC)
	const TCHAR* Grade = nullptr;
	switch (ItemGrade)
	{
	case 0: Grade = TEXT("co"); break;
	case 1: Grade = TEXT("uc"); break;
	case 2: Grade = TEXT("ra"); break;
	case 3: Grade = TEXT("he"); break;
	case 4: Grade = TEXT("le"); break;   // ассетов *_le_* нет ни в одной сборке клиента (weapon_glow_re.md §E)
	default: return FString();
	}
	// Скин: оба сайта эталона зовут билдер с a4=0 ⇒ всегда "ma". Состояние по заточке: 5→"01", 6→"02", >=7→"03".
	const TCHAR* State = (Enhance == 5) ? TEXT("01") : ((Enhance == 6) ? TEXT("02") : TEXT("03"));
	return FString::Printf(TEXT("%s_%c_%s_ma_%s"), Base, Letter, Grade, State);
}

#if WITH_EDITOR
void UAsdaFxLightSettings::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	// Любая правка страницы (в том числе поля внутри элемента массива) — индексы и шаблоны могли устареть
	BumpTablesRevision();
}

bool UAsdaFxLightSettings::ReloadGlowTablesFromDefaultIni(FString& OutError)
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultGame.ini")));
	TArray<FString> Lines;
	// LoadFileToStringArray сам снимает UTF-8 BOM (DefaultGame.ini проекта — с BOM)
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path))
	{
		OutError = FString::Printf(TEXT("не читается %s"), *Path);
		return false;
	}

	static const FString Header = TEXT("[/Script/AsdaReborn.AsdaFxLightSettings]");
	static const FString FamKey = TEXT("+Families=");
	static const FString OvKey = TEXT("+Overrides=");
	bool bIn = false, bFound = false;
	TArray<FAsdaFxLightRule> NewFamilies;
	TArray<FAsdaFxGlowOverride> NewOverrides;
	for (int32 LineNo = 0; LineNo < Lines.Num(); ++LineNo)
	{
		const FString L = Lines[LineNo].TrimStartAndEnd();
		if (L.StartsWith(TEXT("[")))
		{
			bIn = L.Equals(Header, ESearchCase::IgnoreCase);
			bFound |= bIn;
			continue;
		}
		if (!bIn) { continue; }
		// Тот же импорт текста структуры, которым UObject::LoadConfig разбирает элемент TArray<struct>
		if (L.StartsWith(FamKey))
		{
			FAsdaFxLightRule R;
			if (!FAsdaFxLightRule::StaticStruct()->ImportText(*L + FamKey.Len(), &R, nullptr, PPF_None, GLog, TEXT("FAsdaFxLightRule")))
			{
				OutError = FString::Printf(TEXT("строка %d: +Families не разобралась"), LineNo + 1);
				return false;
			}
			NewFamilies.Add(MoveTemp(R));
		}
		else if (L.StartsWith(OvKey))
		{
			FAsdaFxGlowOverride O;
			if (!FAsdaFxGlowOverride::StaticStruct()->ImportText(*L + OvKey.Len(), &O, nullptr, PPF_None, GLog, TEXT("FAsdaFxGlowOverride")))
			{
				OutError = FString::Printf(TEXT("строка %d: +Overrides не разобралась"), LineNo + 1);
				return false;
			}
			NewOverrides.Add(MoveTemp(O));
		}
	}
	if (!bFound || NewFamilies.Num() == 0)
	{
		// Пустая таблица семейств = «света нет ни у кого» — такой результат не применяем, это почти наверняка сбой записи
		OutError = FString::Printf(TEXT("в %s нет секции %s или в ней нет ни одного +Families"), *Path, *Header);
		return false;
	}

	UAsdaFxLightSettings* Mutable = GetMutableDefault<UAsdaFxLightSettings>();
	Mutable->Families = MoveTemp(NewFamilies);
	Mutable->Overrides = MoveTemp(NewOverrides);
	BumpTablesRevision();
	UE_LOG(LogAsdaFxLight, Log, TEXT("[свечение] таблицы перечитаны из %s: семейств %d, переопределений %d (ревизия %d)"),
		*Path, Mutable->Families.Num(), Mutable->Overrides.Num(), GAsdaFxLightTablesRevision);
	return true;
}
#endif
