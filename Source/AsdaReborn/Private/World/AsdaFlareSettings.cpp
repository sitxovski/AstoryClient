// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaFlareSettings.h"

// Числа таблицы приходят из Config/DefaultGame.ini (генератор Tools/asda_gen_flare_ini.py из
// Tools/asda_flare_sets.json). В C++ их нет намеренно — один источник, как у эффект-светов.

const UAsdaFlareSettings* UAsdaFlareSettings::Get()
{
	return GetDefault<UAsdaFlareSettings>();
}

const FAsdaFlareSet* UAsdaFlareSettings::FindSet(FName SetId) const
{
	// Пустое имя = набор по умолчанию. Отдельной «пустой» ветки у эффекта быть не должно: владелец,
	// поставивший галочку и не выбравший набор, обязан увидеть блик, а не молчание.
	const FName Wanted = SetId.IsNone() ? FName(TEXT("default")) : SetId;
	for (const FAsdaFlareSet& S : Sets)
	{
		if (S.Id == Wanted) { return &S; }
	}
	// Набора с таким именем в таблице нет — но блик всё равно должен быть виден, иначе опечатка в имени
	// выглядит как «механизм не работает». Отдаём первый набор таблицы, если он есть.
	return Sets.Num() > 0 ? &Sets[0] : nullptr;
}
