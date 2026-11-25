// Copyright (c) 2026 @sitxovski (Discord)
// Реализация `AsdaLangRows` — назначение, закон и формат ключей в шапке `Public/Text/AsdaLangRows.h`.
//   [ПОРТ] 09.10.2026 (мультиязычность, этап 1).
#include "Text/AsdaLangRows.h"
#include "Text/AsdaLang.h"
#include "Items/AsdaItemDbRows.h"      // строки item-БД (FAsda*Row)
#include "Npc/AsdaNpcDialogTypes.h"    // FAsdaNsrPageRow — страница сценария NPC
#include "Quest/AsdaQuestTypes.h"      // FAsdaQuestBookRow — книга задания (.QST)
#include "CoreGlobals.h"               // GIsEditor / GIsPlayInEditorWorld — гейт ActiveForBakeable
#include "AsdaDevConsole.h"             // ASDA_DEV_CVAR — ключ asda.Lang.DropRuPlaceholder (в Shipping — константа)

namespace AsdaLangRowsPriv
{
	// Обратный индекс одной таблицы: указатель на данные строки → имя строки. `Owner` — чья это карта (таблицу
	//   могли выгрузить и создать новую по тому же адресу), `Num` — число строк на момент построения.
	struct FIndex
	{
		TWeakObjectPtr<const UDataTable> Owner;   // таблица, по которой построена карта
		int32 Num = -1;                           // строк в таблице при построении
		TMap<const uint8*, FName> ByPtr;          // данные строки → RowName
	};
	// Индексы по адресу таблицы (игровой поток, как и все чтения таблиц порта — см. `AsdaDataTableCache.h`).
	static TMap<const UDataTable*, FIndex> GIndex;

	// Построить/перестроить индекс таблицы.
	static FIndex& Build(const UDataTable* Dt)
	{
		FIndex& I = GIndex.FindOrAdd(Dt);
		I.Owner = Dt;
		I.ByPtr.Reset();
		const TMap<FName, uint8*>& Rows = Dt->GetRowMap();
		I.Num = Rows.Num();
		I.ByPtr.Reserve(Rows.Num());
		for (const TPair<FName, uint8*>& It : Rows) { I.ByPtr.Add(It.Value, It.Key); }
		return I;
	}
}

bool AsdaLangRows::Active()
{
	return !FAsdaLang::IsRu();
}

// ★ 10.10.2026 (en_completeness): глубина `FEditorWorldScope` — сейчас идёт `OnConstruction` актора мира РЕДАКТОРА.
static int32 GEditorWorldConstructDepth = 0;

AsdaLangRows::FEditorWorldScope::FEditorWorldScope(const UObject* Ctx)
{
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	bActive = GIsEditor && (!W || !W->IsGameWorld());   // мир редактора (или актор без мира) — без перевода
	if (bActive) { ++GEditorWorldConstructDepth; }
}

AsdaLangRows::FEditorWorldScope::~FEditorWorldScope()
{
	if (bActive) { --GEditorWorldConstructDepth; }
}

bool AsdaLangRows::ActiveForBakeable()
{
	if (FAsdaLang::IsRu()) { return false; }
	if (!GIsEditor) { return true; }
	// Редактор вне PIE: результат лёг бы в UPROPERTY запечённого актора и сохранился бы в карту вместе с ним.
	//   ★ 10.10.2026 (баг владельца «в PIE имена NPC русские»): `GIsPlayInEditorWorld` поднят только ВНУТРИ тика мира
	//   PIE, а «удочерение» NPC пакетом 4067, спаун мобов и окна разговора идут из тика сети/Slate вне этого окна —
	//   там флаг ложный, и в PIE имена оставались русскими. Теперь в сеансе PIE перевод включён везде, КРОМЕ
	//   `OnConstruction` акторов мира редактора (`FEditorWorldScope` в `AAsdaNpcActor`/`AAsdaMobActor`).
	if (GEditorWorldConstructDepth > 0) { return false; }
	return GIsPlayInEditorWorld || FAsdaLang::IsPieSession();
}

FName AsdaLangRows::RowNameOf(const UDataTable* Dt, const void* RowPtr)
{
	using namespace AsdaLangRowsPriv;
	if (!Dt || !RowPtr) { return NAME_None; }
	FIndex* I = GIndex.Find(Dt);
	if (!I || I->Owner.Get() != Dt || I->Num != Dt->GetRowMap().Num()) { I = &Build(Dt); }
	if (const FName* N = I->ByPtr.Find(static_cast<const uint8*>(RowPtr))) { return *N; }
	// Промах: таблицу могли пересобрать с тем же числом строк — одна перестройка и повторный поиск.
	I = &Build(Dt);
	if (const FName* N = I->ByPtr.Find(static_cast<const uint8*>(RowPtr))) { return *N; }
	return NAME_None;
}

FString AsdaLangRows::Text(const UDataTable* Dt, const void* RowPtr, const TCHAR* Field, const FString& Ru)
{
	if (!Active() || !Dt || !RowPtr) { return Ru; }
	const FName Row = RowNameOf(Dt, RowPtr);
	if (Row.IsNone()) { return Ru; }
	return FAsdaLang::ResolveRow(Dt, Row, Field, Ru);
}

// ★★★ [ИСПРАВЛЕНИЕ] 05.11.2026 (баг игрока: «Нет» в подсказках EN-клиента, сессия gold-boast). В RU-данных «нет»/«Нет» —
//   ЗАГЛУШКА (описания item_tooltip — 2974 из 59394 строк, имена навыков/состояний/причёсок/питомцев, форматы опций, GameText),
//   а EN-извлечение (`extract_text_lang.py`, `build_coverage.is_placeholder_str`) заглушки не переводит — и цепочка «нет EN →
//   русский» печатала русское «Нет» в EN. Там, где у EN-оригинала НАСТОЯЩИЙ текст (GameText 2335 «Do not have», 9497
//   «Does not exist» … 18 строк, имена навыков skilltooltip 59 / skill_info 54), он дописан в EN-оверлей и ведомости
//   `Tools/data/loc/en/*.json` (source orig_en_placeholder). У ОСТАЛЬНЫХ строк EN-оригинал пуст или сам заглушка
//   («-», «_», «none»): для них 1 — в не-русской сессии RU-заглушка без перевода не печатается (пустая строка; заглушки
//   «-»/«_» оригинала не воспроизводим — текста они не несут); 0 — прежнее поведение (русское «Нет»).
//   Точка одна — `FAsdaLang::Resolve` (все таблицы, GameText и подписи NPC), предикат — `AsdaLangRows::DropsRuPlaceholder`.
static ASDA_DEV_CVAR(int32, CVarLangDropRuPlaceholder,
	TEXT("asda.Lang.DropRuPlaceholder"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — в английской сессии русская заглушка «Нет» без перевода (описания, имена, форматы, GameText) ")
	TEXT("не показывается; 0 — как прежде: русское «Нет» в подсказках"),
	ECVF_Default);

namespace AsdaLangRowsPriv
{
	// RU-заглушка «нет» — тот же разбор, что `build_coverage.is_placeholder_str` (обрезка пробелов и хвостовой «~»,
	//   формы «нет»/«Нет»/«НЕТ»); единственная кириллическая заглушка множества `_PH` извлечения.
	static bool IsRuPlaceholder(const FString& S)
	{
		FString T = S.TrimStartAndEnd();
		while (T.EndsWith(TEXT("~"))) { T.LeftChopInline(1); }
		T.TrimStartAndEndInline();
		// ★ Регистр — явными формами: `ESearchCase::IgnoreCase` UE сворачивает только латиницу («Нет» ≠ «нет» для него).
		return T.Equals(TEXT("нет")) || T.Equals(TEXT("Нет")) || T.Equals(TEXT("НЕТ"));
	}

}

bool AsdaLangRows::DropsRuPlaceholder(const FString& Ru)
{
	return Active() && CVarLangDropRuPlaceholder.GetValueOnGameThread() != 0 && AsdaLangRowsPriv::IsRuPlaceholder(Ru);
}

// Перевод одного поля копии: ключ `<Row>.<имя поля>` (имя поля = имя UPROPERTY, строкой из макроса).
#define ASDA_LANG_FIELD(F) R.F = FAsdaLang::ResolveRow(Dt, Row, TEXT(#F), R.F)
// Общий вход каждой перегрузки: русская сессия / нет таблицы / нет имени строки — ничего не делать.
#define ASDA_LANG_GUARD() if (!Active() || !Dt || Row.IsNone()) { return; }

void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaItemTooltipRow& R)            { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Description); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaItemoptiontableRow& R)        { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(FmtString); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaMakingRecipeRow& R)           { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(RecipeName); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSpecialItemlistRow& R)        { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSpecialitemTooltiplistRow& R) { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); ASDA_LANG_FIELD(Tooltip); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSpecialitemBuftooltiplistRow& R) { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); ASDA_LANG_FIELD(Desc); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaHairtableRow& R)              { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); ASDA_LANG_FIELD(Description); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaJobtypeRow& R)                { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(JobName); ASDA_LANG_FIELD(ParentJob); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSowellRow& R)                 { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSowellTooltipRow& R)          { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Text); }
// ★ [ИСПРАВЛЕНИЕ] 07.11.2026: разбор — у объявления `TitleDisplayName`.
static ASDA_DEV_CVAR(int32, CVarTitleStripStar,
	TEXT("asda.Title.StripStar"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 07.11.2026] 1 = имя титула показывается без служебной «*» в начале (id 99–116); 0 = как в таблице."),
	ECVF_Default);

FString AsdaLangRows::TitleDisplayName(const FString& Raw)
{
	if (CVarTitleStripStar.GetValueOnGameThread() == 0 || !Raw.StartsWith(TEXT("*"))) { return Raw; }
	int32 I = 0;
	while (I < Raw.Len() && (Raw[I] == TEXT('*') || Raw[I] == TEXT(' '))) { ++I; }
	return Raw.Mid(I);
}

void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaTitleRow& R)                  { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); ASDA_LANG_FIELD(Desc); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaTitleOptionRow& R)            { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(OptionStatName); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSoulmateskilltableRow& R)     { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(DisplayName); ASDA_LANG_FIELD(Text); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSoulsoulmatetextRow& R)       { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Text); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaPetexptableRow& R)            { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(StageName); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaPetoptiontableRow& R)         { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Format); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaPetRow& R)                    { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSkilltooltipRow& R)           { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); ASDA_LANG_FIELD(Text); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaEmotiontipRow& R)             { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaLoginhelpertextRow& R)        { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Text); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaStateinfotipRow& R)           { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); ASDA_LANG_FIELD(Text); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSoulstateinfotipRow& R)       { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); ASDA_LANG_FIELD(Text); }
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaSkillInfoRow& R)              { ASDA_LANG_GUARD(); ASDA_LANG_FIELD(Name); }

// Страница сценария NPC: реплика + подписи шагов (`Steps.<i>.Caption`).
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaNsrPageRow& R)
{
	ASDA_LANG_GUARD();
	ASDA_LANG_FIELD(Text);
	for (int32 i = 0; i < R.Steps.Num(); ++i)
	{
		R.Steps[i].Caption = FAsdaLang::ResolveRow(Dt, Row, *FString::Printf(TEXT("Steps.%d.Caption"), i), R.Steps[i].Caption);
	}
}

// Книга задания: заголовок, описание, тексты стадий (`Steps.<стадия>`; пустые стадии не ищутся).
void AsdaLangRows::Localize(const UDataTable* Dt, const FName& Row, FAsdaQuestBookRow& R)
{
	ASDA_LANG_GUARD();
	ASDA_LANG_FIELD(Title);
	ASDA_LANG_FIELD(Desc);
	for (int32 i = 0; i < R.Steps.Num(); ++i)
	{
		if (R.Steps[i].IsEmpty()) { continue; }
		R.Steps[i] = FAsdaLang::ResolveRow(Dt, Row, *FString::Printf(TEXT("Steps.%d"), i), R.Steps[i]);
	}
}

#undef ASDA_LANG_GUARD
#undef ASDA_LANG_FIELD
