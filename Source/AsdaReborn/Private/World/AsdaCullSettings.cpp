// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaCullSettings.h"
#include "Misc/ConfigCacheIni.h"   // GConfig->GetSection: сырая проверка «ключ реально пришёл из ini» (ValidateConfig)
#include "CoreGlobals.h"           // GGameIni — имя объединённого Game-конфига (Config/DefaultGame.ini + слои Saved)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCullSettings, Log, All);

UAsdaCullSettings::UAsdaCullSettings()
{
	// Секция ini = [/Script/AsdaReborn.AsdaCullSettings] (config=Game). Рабочие числа — в Config/DefaultGame.ini;
	// значения ниже нужны только на случай «ini нет вовсе» (страховка, как ShadowBudgetFallback у UAsdaRenderSettings)
	// и чтобы страница Project Settings не была пустой при первом открытии.
	//
	// ★★★ ПОЧЕМУ ЧИСЛА ЗДЕСЬ ТЕ ЖЕ, ЧТО В ini (ревизия review_drawdistance.md §3). Ревизия предложила либо заведомо
	//   отличимые дефолты C++, либо предупреждение в лог. ВЫБРАНО ПРЕДУПРЕЖДЕНИЕ (ValidateConfig ниже), потому что
	//   «заведомо отличимые» дефолты означают: при опечатке в ini игра МОЛЧА поедет на числах, которых никто не мерил,
	//   и владелец увидит незнакомую картинку вместо диагностики. Здесь наоборот: страховка = ровно измеренная
	//   конфигурация, а расхождение с ini становится ВИДНЫМ поимённым списком в логе при каждом старте карты.
	//   Случай «секция не прочиталась целиком» ловит канарейка ConfigStamp (дефолт C++ 0, в ini дата).
	CategoryName = TEXT("Asda");
	SectionName  = TEXT("Cull");

	// Террейн и небо — один примитив на всю карту: отсечка означала бы «карта исчезла». Никогда.
	{ FAsdaCullClassSpec S; S.bCull = false; Classes.Add(EAsdaCullClass::Terrain, S); }
	{ FAsdaCullClassSpec S; S.bCull = false; Classes.Add(EAsdaCullClass::Sky,     S); }
	// Одиночные статичные меши и ISM карты. ★ Максимум 30000 юн (было 20000 = дальность динамических теней; поднято
	// 08.09.2026 ЗАМЕРОМ на nightvalley, камера «долина с NPC»: 25000 эффективных юн → 1.84 % изменённых пикселей при
	// шуме 0.00 %, 37500 → 0.06 %). Аргумент «выше границы теней отсечка не даёт ничего» верен для ТЕНЕЙ, но крупные
	// объекты за этой границей видны в базовом проходе; подробности и цена — Config/DefaultGame.ini над Classes.
	{ FAsdaCullClassSpec S; S.bCull = true; S.SizeFactor = 45.f; S.MinDistance = 6000.f;  S.MaxDistance = 30000.f; Classes.Add(EAsdaCullClass::Prop,      S); }
	{ FAsdaCullClassSpec S; S.bCull = true; S.SizeFactor = 45.f; S.MinDistance = 6000.f;  S.MaxDistance = 30000.f; Classes.Add(EAsdaCullClass::Instanced, S); }
	// Скелеталы (NPC/мобы/чужие игроки/питомцы). ★ 12.10.2026 (владелец: NPC и мобы должны попадать в дальность при любом
	//   ползунке/уровне): bCull=false — из отсечки по дальности выведены (их ограничивает зона интереса сервера: мобы в
	//   радиусе 30 тайлов = 3000 юн, а правило × ползунок 0.5 × уровень 0.4 давало 1200 юн). Числа правила оставлены
	//   для asda.Cull.Set на лету; рабочее значение — Config/DefaultGame.ini (здесь страховка «нет ini», числа те же).
	{ FAsdaCullClassSpec S; S.bCull = false; S.SizeFactor = 60.f; S.MinDistance = 6000.f;  S.MaxDistance = 12000.f; S.bUseFogFloor = false; Classes.Add(EAsdaCullClass::Skeletal, S); }
	// Миры-исключения (страховка «нет ini»; рабочий список — в Config/DefaultGame.ini)
	SkipWorlds.Add(FName(TEXT("Login")));
	SkipWorlds.Add(FName(TEXT("L_Loading")));
	// Эффекты: по умолчанию НЕ отсекаем — у AAsdaEffectActor своя отсечка (поле CullDistance, AsdaEffectActor.cpp:729),
	// дублировать её незачем. Числа оставлены на случай, если владелец включит класс в ini.
	{ FAsdaCullClassSpec S; S.bCull = false; S.SizeFactor = 60.f; S.MinDistance = 5000.f; S.MaxDistance = 10000.f; S.bUseFogFloor = false; Classes.Add(EAsdaCullClass::Effect, S); }
}

const FAsdaCullClassSpec* UAsdaCullSettings::FindClass(EAsdaCullClass Key) const
{
	const FAsdaCullClassSpec* S = Classes.Find(Key);
	if (!S)
	{
		// Один раз на ключ: правила класса нет в ini — класс не отсекается (безопасное умолчание)
		static TSet<uint8> Warned;
		if (!Warned.Contains((uint8)Key))
		{
			Warned.Add((uint8)Key);
			UE_LOG(LogAsdaCullSettings, Warning, TEXT("Правила отсечки для класса %s нет в [/Script/AsdaReborn.AsdaCullSettings] Classes — "
				"класс не отсекается (дополните Config/DefaultGame.ini)"), *UEnum::GetValueAsString(Key));
		}
	}
	return S;
}

void UAsdaCullSettings::ValidateConfig()
{
	// Один раз за процесс: иначе строки повторялись бы на каждой загрузке карты и в дампе
	static bool bDone = false;
	if (bDone) { return; }
	bDone = true;

	const UAsdaCullSettings* S = Get();
	const TCHAR* Section = TEXT("/Script/AsdaReborn.AsdaCullSettings");

	// Сырая секция ini: сколько ключей реально прочитано движком (GGameIni = объединённый Config/DefaultGame.ini + слои Saved)
	TArray<FString> Keys;
	int32 KeyCount = 0;
	if (GConfig)
	{
		TArray<FString> Lines;
		GConfig->GetSection(Section, Lines, GGameIni);
		KeyCount = Lines.Num();
		for (const FString& L : Lines)
		{
			FString K, V;
			if (L.Split(TEXT("="), &K, &V)) { Keys.Add(K.TrimStartAndEnd()); }
		}
	}

	// Канарейка: штамп есть только в ini, в C++ его нет (0)
	if (S->ConfigStamp == 0)
	{
		UE_LOG(LogAsdaCullSettings, Error, TEXT("[Отсечка] СЕКЦИЯ ini НЕ ПРОЧИТАНА: [%s] в Config/DefaultGame.ini не найдена или пуста "
			"(ключей в секции %d, ConfigStamp=0). Работают СТРАХОВОЧНЫЕ дефолты C++ — числа те же, что измерены, но правки ini не действуют."),
			Section, KeyCount);
	}
	else
	{
		UE_LOG(LogAsdaCullSettings, Log, TEXT("[Отсечка] ini [%s] прочитан: ключей %d, ConfigStamp=%d"), Section, KeyCount, S->ConfigStamp);
	}

	// Поимённо: ключ есть в ini → значение из ini; ключа нет → работает страховка C++ (и это НЕ ошибка, но должно быть видно)
	auto Check = [&Keys](const TCHAR* Key, const FString& Value)
	{
		const bool bHas = Keys.ContainsByPredicate([Key](const FString& K) { return K.Equals(Key, ESearchCase::IgnoreCase) || K.Equals(FString(TEXT("+")) + Key, ESearchCase::IgnoreCase); });
		if (bHas) { UE_LOG(LogAsdaCullSettings, Log,     TEXT("[Отсечка]   %-20s = %-12s [ini]"), Key, *Value); }
		else      { UE_LOG(LogAsdaCullSettings, Warning, TEXT("[Отсечка]   %-20s = %-12s [СТРАХОВКА C++ — ключа нет в Config/DefaultGame.ini или он битый]"), Key, *Value); }
	};
	Check(TEXT("Classes"),           FString::Printf(TEXT("правил %d"), S->Classes.Num()));
	Check(TEXT("VisibilityFloorMin"),FString::Printf(TEXT("%.0f"), S->VisibilityFloorMin));
	Check(TEXT("FogSafetyFactor"),   FString::Printf(TEXT("%.2f"), S->FogSafetyFactor));
	Check(TEXT("FogFloorMax"),       FString::Printf(TEXT("%.0f"), S->FogFloorMax));
	Check(TEXT("FogDiagOpacity"),    FString::Printf(TEXT("%.2f"), S->FogDiagOpacity));
	Check(TEXT("ApplyThrottleSec"),  FString::Printf(TEXT("%.2f"), S->ApplyThrottleSec));
	Check(TEXT("SkipWorlds"),        FString::Printf(TEXT("миров %d"), S->SkipWorlds.Num()));
	Check(TEXT("bApplyInPIE"),       S->bApplyInPIE ? TEXT("True") : TEXT("False"));

	// Согласованность чисел между собой: потолок ниже пола молча съел бы пол видимости
	if (S->FogFloorMax > 0.f && S->VisibilityFloorMin > S->FogFloorMax)
	{
		UE_LOG(LogAsdaCullSettings, Warning, TEXT("[Отсечка] VisibilityFloorMin %.0f > FogFloorMax %.0f — пол видимости срезается потолком до %.0f юн. "
			"Это почти наверняка опечатка в Config/DefaultGame.ini."), S->VisibilityFloorMin, S->FogFloorMax, S->FogFloorMax);
	}
}

const UAsdaCullSettings* UAsdaCullSettings::Get()
{
	return GetDefault<UAsdaCullSettings>();
}
