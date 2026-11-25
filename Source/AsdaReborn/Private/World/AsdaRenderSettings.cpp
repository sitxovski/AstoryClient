// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaRenderSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRenderSettings, Log, All);

UAsdaRenderSettings::UAsdaRenderSettings()
{
	// Секция ini = [/Script/AsdaReborn.AsdaRenderSettings] (config=Game). Чисел пресетов в коде нет — их даёт
	// Tools/asda_gen_render_settings_ini.py из Tools/asda_light_presets.json (один источник).
	CategoryName = TEXT("Asda");
	SectionName  = TEXT("Render");
}

const FAsdaLightPresetSpec* UAsdaRenderSettings::FindPreset(EAsdaLightPreset Key) const
{
	if (Key == EAsdaLightPreset::Custom) { return nullptr; }
	const FAsdaLightPresetSpec* P = LightPresets.Find(Key);
	if (!P)
	{
		// Один раз на ключ: пресет не сгенерирован в DefaultGame.ini
		static TSet<uint8> Warned;
		if (!Warned.Contains((uint8)Key))
		{
			Warned.Add((uint8)Key);
			UE_LOG(LogAsdaRenderSettings, Warning, TEXT("Пресет света %s отсутствует в [/Script/AsdaReborn.AsdaRenderSettings] LightPresets — "
				"запустите python Tools/asda_gen_render_settings_ini.py (источник Tools/asda_light_presets.json)"), *UEnum::GetValueAsString(Key));
		}
	}
	return P;
}

// ★ 14.09.2026 (реестр настроек D7): проверка метки чтения секции — один раз за процесс, при первом обращении к настройкам.
//   Только лог: значения и поведение не меняются. Гонка потоков здесь безвредна (в худшем случае строка лога повторится).
static void AsdaRenderSettingsCheckStamp(const UAsdaRenderSettings* S)
{
	static bool bChecked = false;   // проверка уже выполнялась в этом процессе
	if (bChecked || !S) { return; }
	bChecked = true;
	if (S->ConfigStamp == 0)
	{
		UE_LOG(LogAsdaRenderSettings, Warning, TEXT("[Настройки рендера] Секция [/Script/AsdaReborn.AsdaRenderSettings] из Config/DefaultGame.ini НЕ ПРОЧИТАНА "
			"(ConfigStamp=0): пресеты локальных светов и бюджеты теней работают на страховочных умолчаниях C++ (например фонарь IntensityRelSun 1.0 "
			"вместо 3.0 из данных). Проверьте секцию или перегенерируйте: python Tools/asda_gen_render_settings_ini.py"));
	}
	else
	{
		UE_LOG(LogAsdaRenderSettings, Log, TEXT("[Настройки рендера] ini-секция прочитана: ConfigStamp=%d, пресетов света %d"), S->ConfigStamp, S->LightPresets.Num());
	}
}

const UAsdaRenderSettings* UAsdaRenderSettings::Get()
{
	const UAsdaRenderSettings* S = GetDefault<UAsdaRenderSettings>();
	AsdaRenderSettingsCheckStamp(S);
	return S;
}
