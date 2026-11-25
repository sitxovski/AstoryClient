// Copyright (c) 2026 @sitxovski (Discord)
#include "Login/AsdaText.h"
#include "Login/AsdaGameText.h"
#include "Login/AsdaLoginData.h"
#include "Engine/DataTable.h"
#include "Text/AsdaLang.h"   // ★ 09.10.2026: язык сессии

FString FAsdaText::Loc(int32 Id)
{
	return FAsdaLoginData::Loc(Id);   // DT_GameText (error list.bin), Strip применён
}

FString FAsdaText::LocRaw(int32 Id)
{
	return FAsdaLoginData::LocRaw(Id);   // сырой текст с тегами
}

FString FAsdaText::Strip(const FString& In)
{
	return FAsdaLoginData::StripMarkup(In);
}

UDataTable* FAsdaText::FindTable(const FString& Table)
{
	// ★ 15.09.2026 (dt_gc_hold): поиск и кэш вынесены из `Get` без изменений — `Get` зовёт это же.
	static TMap<FString, TWeakObjectPtr<UDataTable>> Cache;
	UDataTable* DT = nullptr;
	if (TWeakObjectPtr<UDataTable>* P = Cache.Find(Table)) { DT = P->Get(); }
	if (!DT)
	{
		// категории игрового деления (full-structural: как игра делит текст)
		static const TCHAR* const Cats[] = {
			TEXT("NPC"), TEXT("Quest"), TEXT("Dialogue"), TEXT("Skill"), TEXT("Emotion"),
			TEXT("World"), TEXT("Item"), TEXT("Shop"), TEXT("Craft"), TEXT("Tutorial"),
			TEXT("Clan"), TEXT("Fishing")
		};
		for (const TCHAR* C : Cats)
		{
			const FString Path = FString::Printf(TEXT("/Game/AsdaAssets/Text/%s/DT_%s.DT_%s"), C, *Table, *Table);
			// LOAD_NoWarn|LOAD_Quiet: таблица лежит в ОДНОЙ категории — прочие пути отсутствуют, не шумим варнингами.
			if (UDataTable* D = LoadObject<UDataTable>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)) { DT = D; break; }
		}
		Cache.Add(Table, DT);
	}
	return DT;
}

FString FAsdaText::Get(const FString& Table, int32 Id, const FString& Field)
{
	UDataTable* DT = FindTable(Table);
	if (!DT) return FString();
	const FAsdaTextRow* R = DT->FindRow<FAsdaTextRow>(FName(*FString::FromInt(Id)), TEXT("FAsdaText::Get"), false);
	if (!R) return FString();
	// `tex` — имя текстуры, не текст: языку не подлежит.
	if (Field == TEXT("tex"))      return R->tex;
	const TCHAR* F = TEXT("name");
	const FString* Ru = &R->name;
	if (Field == TEXT("desc"))          { F = TEXT("desc");     Ru = &R->desc; }
	else if (Field == TEXT("subtitle")) { F = TEXT("subtitle"); Ru = &R->subtitle; }
	else if (Field == TEXT("text"))     { F = TEXT("text");     Ru = &R->text; }
	else if (Field == TEXT("family"))   { F = TEXT("family");   Ru = &R->family; }
	else if (Field == TEXT("village"))  { F = TEXT("village");  Ru = &R->village; }
	// ★★★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `DT_Loc_<Table>_<lang>`, ключ `<id>.<поле>`; русская сессия —
	//   прежний путь (`FAsdaLang::Resolve` возвращает русскую строку без поиска).
	return FAsdaLang::ResolveId(*Table, Id, F, *Ru);
}
