// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaGameUserSettings — разбор назначения и миграции в шапке
//   `Public/Resources/AsdaGameUserSettings.h`.
#include "Resources/AsdaGameUserSettings.h"

#include "Resources/AsdaSoundSubsystem.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"   // ★ фаза 2: переопределения ключей рендера из профиля

DEFINE_LOG_CATEGORY_STATIC(LogAsdaUserSettings, Log, All);

// Секция, в которой штатные поля родителя лежали до регистрации подкласса.
static const TCHAR* kLegacyParentSection = TEXT("/Script/Engine.GameUserSettings");
// Секция, в которую прежнее окно настроек писало свои строки напрямую через `GConfig`.
static const TCHAR* kLegacyOptionsSection = TEXT("AsdaOptions");
// Текущий формат полей профиля. 1 = первая версия (19.09.2026); 2 = ручки рендера из Ctrl+N и единое
//   «Свечение» (19.09.2026, фаза 2 окна настроек — миграция `MigrateDevOverridesV2`); 3 = возврат
//   масштаба рендера выше 100 %, который миграция v2 урезала до штатного потолка (`MigrateRenderScaleV3`).
// ★ 12.10.2026: 4 = снятие переопределений анизотропии/AO/бликов — строки убраны из окна настроек и Ctrl+N (пересмотр
//   пресетов, ревизия КРИТ-1): иначе старое значение игрока применялось бы невидимо и перебивало новые уровни.
static constexpr int32 kAsdaSettingsVersion = 4;
// Секции dev-окна Ctrl+N: `r.*` — `[AsdaVideo]`, `asda.*` — `[AsdaRender]` (`UAsdaDevMenuSubsystem::ApplyCVarPersist`).
static const TCHAR* kDevVideoSection  = TEXT("AsdaVideo");
static const TCHAR* kDevRenderSection = TEXT("AsdaRender");
// ★ 09.10.2026: раздел и ключ поля `Language` (не Config-поле, пишется вручную; те же имена, что у прежнего Config-поля
//   и у `FAsdaLang` — `AsdaLangPriv::kSection/kKey`).
static const TCHAR* kLanguageSection = TEXT("/Script/AsdaReborn.AsdaGameUserSettings");
static const TCHAR* kLanguageKey = TEXT("Language");

// ═══ ТАБЛИЦА «КЛЮЧ КОНСОЛИ ↔ ПОЛЕ ПРОФИЛЯ» (единственная; по ней работают применение, dev-окно и миграция) ═══
//   Kind: 0 — целое «как есть», 1 — вещественное «как есть», 2 — «вкл/выкл» с отдельными значениями ключа.
namespace AsdaProfileCVars
{
	struct FEntry
	{
		const TCHAR* CVar;        // имя ключа консоли
		int32 Kind;               // см. выше
		const TCHAR* OnValue;     // Kind 2: значение ключа при «вкл»
		const TCHAR* OffValue;    // Kind 2: значение ключа при «выкл»
	};
	static const FEntry kEntries[] = {
		{ TEXT("r.AntiAliasingMethod"),     0, nullptr,     nullptr },
		{ TEXT("r.Tonemapper.Sharpen"),     1, nullptr,     nullptr },
		{ TEXT("r.MaxAnisotropy"),          0, nullptr,     nullptr },
		// ★ AO: «вкл» = −1 (число проходов даёт уровень PostProcessQuality), «выкл» = 0 (гейт
		//   ShouldRenderScreenSpaceAmbientOcclusion гасит и SSAO, и GTAO) — ровно цикл dev-окна { −1, 0 }.
		{ TEXT("r.AmbientOcclusionLevels"), 2, TEXT("-1"),  TEXT("0") },
		// Размытие: у проекта `r.DefaultFeature.MotionBlur=False`, поэтому «вкл» — глобальный override 0.5.
		{ TEXT("r.MotionBlur.Amount"),      2, TEXT("0.5"), TEXT("0") },
		{ TEXT("r.LensFlareQuality"),       2, TEXT("2"),   TEXT("0") },
		{ TEXT("asda.Clouds.Field"),        2, TEXT("1"),   TEXT("0") },
		{ TEXT("asda.Cull.Scale"),          1, nullptr,     nullptr },
	};

	static const FEntry* Find(const FString& Name)
	{
		for (const FEntry& E : kEntries) { if (Name.Equals(E.CVar, ESearchCase::IgnoreCase)) { return &E; } }
		return nullptr;
	}
}

// Указатель на поле профиля по имени ключа (целые — через int32*, вещественные — через float*).
static int32* AsdaIntFieldFor(UAsdaGameUserSettings* S, const FString& Name)
{
	if (Name.Equals(TEXT("r.AntiAliasingMethod"), ESearchCase::IgnoreCase))     { return &S->AntiAliasingMethod; }
	if (Name.Equals(TEXT("r.MaxAnisotropy"), ESearchCase::IgnoreCase))          { return &S->MaxAnisotropy; }
	if (Name.Equals(TEXT("r.AmbientOcclusionLevels"), ESearchCase::IgnoreCase)) { return &S->AmbientOcclusion; }
	if (Name.Equals(TEXT("r.MotionBlur.Amount"), ESearchCase::IgnoreCase))      { return &S->MotionBlur; }
	if (Name.Equals(TEXT("r.LensFlareQuality"), ESearchCase::IgnoreCase))       { return &S->LensFlare; }
	if (Name.Equals(TEXT("asda.Clouds.Field"), ESearchCase::IgnoreCase))        { return &S->VolumetricClouds; }
	return nullptr;
}
static float* AsdaFloatFieldFor(UAsdaGameUserSettings* S, const FString& Name)
{
	if (Name.Equals(TEXT("r.Tonemapper.Sharpen"), ESearchCase::IgnoreCase)) { return &S->TonemapperSharpen; }
	if (Name.Equals(TEXT("asda.Cull.Scale"), ESearchCase::IgnoreCase))      { return &S->DrawDistanceScale; }
	return nullptr;
}

// Значение ключа консоли, которое даёт ЗАДАННОЕ поле. Пусто — поле «не менялось» (ключ не трогать).
static FString AsdaOverrideValue(const UAsdaGameUserSettings* S, const AsdaProfileCVars::FEntry& E)
{
	UAsdaGameUserSettings* M = const_cast<UAsdaGameUserSettings*>(S);
	if (E.Kind == 1)
	{
		const float* F = AsdaFloatFieldFor(M, E.CVar);
		return (F && *F >= 0.f) ? FString::SanitizeFloat(*F) : FString();
	}
	const int32* I = AsdaIntFieldFor(M, E.CVar);
	if (!I || *I < 0) { return FString(); }
	if (E.Kind == 2) { return (*I != 0) ? FString(E.OnValue) : FString(E.OffValue); }
	return FString::FromInt(*I);
}

UAsdaGameUserSettings::UAsdaGameUserSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Конструктор родителя зовёт свой `SetToDefaults` невиртуально — наши поля ставим сами.
	ResetAsdaFieldsToDefaults();
}

UAsdaGameUserSettings* UAsdaGameUserSettings::Get()
{
	return GEngine ? Cast<UAsdaGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

// ★ УМОЛЧАНИЯ — ИЗ ЭТАЛОНА. Громкость 128 из 256 и гамма 50 — `configInitDefaults@0x9509E0`
//   (`RE/etalon/game_main.c:869-870`); флаги вкладок — начальные значения буфера окна
//   (`FAsdaOptState`, эталонные байты +1104…+1148): всё включено, кроме «Интерфейс-3» строк 5 и 6
//   («Всегда бежать» и «Перетаскивание на панель с Shift» в поставке сняты).
void UAsdaGameUserSettings::ResetAsdaFieldsToDefaults()
{
	MusicVolume = 128.f / 256.f;
	SfxVolume = 128.f / 256.f;
	bMusicMuted = false;
	bSfxMuted = false;
	Gamma = 50;
	GlowLevel = 4;   // умолчание эталона: автопресет `CConfig_ApplyAutoGraphicsPreset` (game_main.c:817-826) ставит 4 «Все» при ОЗУ ≥ 512 МБ
	DamageTextMode = 1;   // ★ Ф5 [ПОРТ]: «все» — запрос владельца 17.10.2026 (эталон — 0, разбор у поля)
	CursorSpeedPercent = kMouseSensDefault;   // ★ 19.10.2026: чувствительность мыши — как без настройки
	CameraTurnPercent = kMouseSensDefault;    // ★ 19.10.2026: поворот камеры — как без настройки
	CrouchMode = kCrouchToggle;               // ★ 25.10.2026: присед — «Нажатие» (как с 24.10.2026)
	//   (метку `CursorSpeedRestoreLevel` сброс НЕ трогает: это не настройка, а долг вернуть скорость Windows)
	                 //   (`configInitDefaults` кладёт 0, но автопресет перекрывает его на любой современной машине)
	// Ручки из Ctrl+N: «не менялись» — действует проект/уровень качества.
	AntiAliasingMethod = -1;
	TonemapperSharpen = -1.f;
	MaxAnisotropy = -1;
	AmbientOcclusion = -1;
	MotionBlur = -1;
	LensFlare = -1;
	VolumetricClouds = -1;
	DrawDistanceScale = -1.f;
	RenderScaleAbove100 = -1;
	InterfaceCommunity.Init(1, kNumCommunity);
	InterfaceNames.Init(1, kNumNames);
	static const uint8 kOtherDefaults[kNumOther] = { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 };
	InterfaceOther.SetNum(kNumOther);
	for (int32 i = 0; i < kNumOther; ++i) { InterfaceOther[i] = kOtherDefaults[i]; }
	Render3D.Init(1, kNum3D);
}

void UAsdaGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	ResetAsdaFieldsToDefaults();
}

// Строка ini с набором флагов могла быть короче/длиннее эталона (ручная правка, старый формат):
//   недостающее добиваем умолчанием, лишнее отбрасываем — индексы окна обязаны быть валидными.
void UAsdaGameUserSettings::NormalizeFlagArrays()
{
	auto Fix = [](TArray<uint8>& A, int32 N, uint8 Def)
	{
		const int32 Old = A.Num();
		A.SetNum(N);
		for (int32 i = Old; i < N; ++i) { A[i] = Def; }
		for (uint8& V : A) { V = (V != 0) ? 1 : 0; }
	};
	Fix(InterfaceCommunity, kNumCommunity, 1);
	Fix(InterfaceNames, kNumNames, 1);
	Fix(InterfaceOther, kNumOther, 1);
	Fix(Render3D, kNum3D, 1);
	MusicVolume = FMath::Clamp(MusicVolume, 0.f, 1.f);
	SfxVolume = FMath::Clamp(SfxVolume, 0.f, 1.f);
	Gamma = FMath::Clamp(Gamma, 1, 100);
	GlowLevel = FMath::Clamp(GlowLevel, 0, 4);
	if (DamageTextMode < 0 || DamageTextMode > 2) { DamageTextMode = 1; }   // ★ Ф5: ручная правка ini вне 0…2 — умолчание
	CursorSpeedPercent = FMath::Clamp(CursorSpeedPercent, kMouseSensMin, kMouseSensMax);   // ★ 19.10.2026
	CameraTurnPercent  = FMath::Clamp(CameraTurnPercent, kMouseSensMin, kMouseSensMax);
	if (CrouchMode != kCrouchToggle && CrouchMode != kCrouchHold) { CrouchMode = kCrouchToggle; }   // ★ 25.10.2026: ручная правка ini — умолчание
	if (CursorSpeedRestoreLevel < 0 || CursorSpeedRestoreLevel > 20) { CursorSpeedRestoreLevel = 0; }   // уровни Windows 1…20
	// Ручные правки ini: значение вне допустимого — считаем «не менялось», а не придумываем своё.
	if (AntiAliasingMethod > 4) { AntiAliasingMethod = -1; }
	if (TonemapperSharpen > 10.f) { TonemapperSharpen = -1.f; }
	if (MaxAnisotropy > 16) { MaxAnisotropy = -1; }
	AmbientOcclusion = (AmbientOcclusion < 0) ? -1 : (AmbientOcclusion != 0 ? 1 : 0);
	MotionBlur       = (MotionBlur < 0) ? -1 : (MotionBlur != 0 ? 1 : 0);
	LensFlare        = (LensFlare < 0) ? -1 : (LensFlare != 0 ? 1 : 0);
	VolumetricClouds = (VolumetricClouds < 0) ? -1 : (VolumetricClouds != 0 ? 1 : 0);
	if (DrawDistanceScale >= 0.f) { DrawDistanceScale = FMath::Clamp(DrawDistanceScale, 0.5f, 2.f); }
}

// ── ОДНОРАЗОВАЯ МИГРАЦИЯ СЕКЦИИ РОДИТЕЛЯ ─────────────────────────────────────────────────────────
//   Делается ДО штатного чтения: копируем ключи старой секции в секцию подкласса, и `LoadConfig`
//   читает их уже как свои. Копируются ВСЕ ключи, с перезаписью: признак «миграция ещё не делалась»
//   (нет метки `AsdaSettingsVersion`) означает, что подкласс ни разу не сохранял свою секцию, и всё,
//   что в ней сейчас видно, — это умолчания `DefaultGameUserSettings.ini`. ⚠ Проверять «ключа ещё нет»
//   здесь НЕЛЬЗЯ: слитая секция уже содержит умолчание `ResolutionSizeX=1920`, и настоящее разрешение
//   игрока (2560 у владельца) молча потерялось бы. Повторной миграции не бывает — метку ставит
//   `LoadSettings` сразу после переноса.
//   ⚠ ВАЖНО ДЛЯ `Version`. Если у объекта неверная версия, штатный `ValidateSettings` сносит ВЕСЬ
//   файл пользовательских настроек (`FConfigBranch::Delete`) — вместе с `[AsdaVideo]`, `[AsdaLogin]`
//   и группами качества. Поэтому ключ `Version` переносится вместе с остальными, а в
//   `Config/DefaultGameUserSettings.ini` у секции подкласса стоит то же `Version=5`, что и у родителя.
bool UAsdaGameUserSettings::MigrateFromLegacyConfig()
{
	if (!GConfig) { return false; }
	const FString NewSection = GetClass()->GetPathName();

	int32 Ver = 0;
	if (GConfig->GetInt(*NewSection, TEXT("AsdaSettingsVersion"), Ver, GGameUserSettingsIni) && Ver > 0)
	{
		return false;   // профиль подкласса уже сохранялся — миграция была
	}

	// Ключ → все его значения (у массивов-полей, напр. `WindowPositions`, строк несколько).
	TArray<FString> Lines;
	GConfig->GetSection(kLegacyParentSection, Lines, GGameUserSettingsIni);
	TMap<FString, TArray<FString>> ByKey;
	TArray<FString> KeyOrder;
	for (const FString& L : Lines)
	{
		FString K, V;
		if (!L.Split(TEXT("="), &K, &V)) { continue; }
		K.TrimStartAndEndInline();
		if (K.IsEmpty()) { continue; }
		if (!ByKey.Contains(K)) { KeyOrder.Add(K); }
		ByKey.FindOrAdd(K).Add(V);
	}
	int32 Copied = 0;
	for (const FString& K : KeyOrder)
	{
		const TArray<FString>& Vals = ByKey[K];
		if (Vals.Num() == 1) { GConfig->SetString(*NewSection, *K, *Vals[0], GGameUserSettingsIni); }
		else                 { GConfig->SetArray(*NewSection, *K, Vals, GGameUserSettingsIni); }
		++Copied;
	}
	// На диск — сразу: родительский `LoadSettings(true)` (его зовёт `ValidateSettings`) перечитывает
	//   файл заново, и перенос, живущий только в памяти, пропал бы.
	GConfig->Flush(false, GGameUserSettingsIni);
	UE_LOG(LogAsdaUserSettings, Log,
		TEXT("[профиль] миграция: из [%s] в [%s] перенесено ключей %d (всего в старой секции %d)"),
		kLegacyParentSection, *NewSection, Copied, KeyOrder.Num());
	return true;
}

// ── Старая секция `[AsdaOptions]` прежнего окна (формат `SaveStateToConfig` до 19.09.2026):
//   наборы флагов строками «1101…», громкость 0…256, «заглушено» 0/1, гамма, режим мерцания.
//   Разрешение и оконный режим оттуда НЕ берём: они и тогда реально жили в штатных полях родителя,
//   которые уже перенесла `MigrateFromLegacyConfig` (в `[AsdaOptions]` лежала лишь их копия).
bool UAsdaGameUserSettings::ImportLegacyAsdaOptions()
{
	if (!GConfig || !GConfig->DoesSectionExist(kLegacyOptionsSection, GGameUserSettingsIni)) { return false; }
	auto GetArr = [](const TCHAR* Key, TArray<uint8>& Arr)
	{
		FString S;
		if (!GConfig->GetString(kLegacyOptionsSection, Key, S, GGameUserSettingsIni)) { return; }
		for (int32 i = 0; i < Arr.Num() && i < S.Len(); ++i) { Arr[i] = (S[i] == TEXT('1')) ? 1 : 0; }
	};
	GetArr(TEXT("Int1"), InterfaceCommunity);
	GetArr(TEXT("Int2"), InterfaceNames);
	GetArr(TEXT("Int3"), InterfaceOther);
	GetArr(TEXT("D3"), Render3D);
	int32 V = 0;
	if (GConfig->GetInt(kLegacyOptionsSection, TEXT("MusicVolume"), V, GGameUserSettingsIni)) { MusicVolume = FMath::Clamp(V, 0, 256) / 256.f; }
	if (GConfig->GetInt(kLegacyOptionsSection, TEXT("SfxVolume"), V, GGameUserSettingsIni))   { SfxVolume = FMath::Clamp(V, 0, 256) / 256.f; }
	if (GConfig->GetInt(kLegacyOptionsSection, TEXT("MusicMuted"), V, GGameUserSettingsIni))  { bMusicMuted = (V != 0); }
	if (GConfig->GetInt(kLegacyOptionsSection, TEXT("SfxMuted"), V, GGameUserSettingsIni))    { bSfxMuted = (V != 0); }
	if (GConfig->GetInt(kLegacyOptionsSection, TEXT("Gamma"), V, GGameUserSettingsIni))       { Gamma = V; }
	// ★ «FlareMode» прежнего окна сюда НЕ переносится: это значение не применялось нигде (комбо 70121
	//   лишь сохраняло его), а действовавший уровень свечения лежал в `[AsdaVideo] GlowLevel` — его и
	//   переносит миграция v2 (`MigrateDevOverridesV2`).
	UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] миграция: значения прежнего окна настроек перенесены из [%s]"),
		kLegacyOptionsSection);
	return true;
}

// ═══ ★★★ МИГРАЦИЯ v2 (19.09.2026, фаза 2 окна настроек): СТРОКИ dev-ОКНА → ПОЛЯ ПРОФИЛЯ ═══════════════
//   До фазы 2 восемь ручек рендера и уровень свечения жили ТОЛЬКО в dev-окне Ctrl+N: оно писало их строками
//   `[AsdaVideo] r.*` / `[AsdaRender] asda.*` и накатывало при старте с приоритетом консоли. В этих строках
//   лежит КАЛИБРОВКА ВЛАДЕЛЬЦА (FXAA, резкость 0.6, …) — её надо перенести БЕЗ ИЗМЕНЕНИЙ, иначе после
//   обновления игра стартовала бы с проектными значениями. Переносится ровно то, что действовало:
//     • ключи таблицы `AsdaProfileCVars` — как есть (у «вкл/выкл» — по сравнению со значением «выкл»);
//       ★ ИСКЛЮЧЕНИЕ — `r.LensFlareQuality` > 0: у проекта `r.DefaultFeature.LensFlare=False`, то есть сила
//       бликов в пост-обработке 0, и строка dev-окна «Lens flare: вкл» на экране НИЧЕГО не меняла. Перенос её
//       как «вкл» впервые ПОКАЗАЛ бы блики (новое поле включает и силу) — это сбило бы вид игры владельца,
//       поэтому переносится только явное «выкл» (0), а «вкл» остаётся «не менялось»;
//     • `[AsdaVideo] GlowLevel` → `GlowLevel` (его и читала видео-подсистема), а `r.BloomQuality=0`
//       dev-строки «Bloom: выкл» — это «свечения нет», то есть пункт 0;
//     • `[AsdaVideo] DisplayGamma` → `Gamma` по обратной формуле `ApplyGammaSetting` (2.2 → 50): dev-окно
//       накатывало её ПОСЛЕ профиля, то есть при старте действовала именно она (та самая гонка);
//     • `[AsdaVideo] r.ScreenPercentage` → штатный масштаб рендера профиля (`SetResolutionScaleValueEx`,
//       потолок движка 100 %); прежде этот ключ с приоритетом консоли перебивал масштаб профиля.
//   Старые строки НЕ удаляются (данные игрока не стираем) — dev-окно их больше просто не читает.
bool UAsdaGameUserSettings::MigrateDevOverridesV2()
{
	if (!GConfig) { return false; }
	int32 Moved = 0;
	FString V;
	for (const AsdaProfileCVars::FEntry& E : AsdaProfileCVars::kEntries)
	{
		const TCHAR* Section = FString(E.CVar).StartsWith(TEXT("asda.")) ? kDevRenderSection : kDevVideoSection;
		if (!GConfig->GetString(Section, E.CVar, V, GGameUserSettingsIni)) { continue; }
		V.TrimStartAndEndInline();
		if (V.IsEmpty()) { continue; }
		if (E.Kind == 1)
		{
			if (float* F = AsdaFloatFieldFor(this, E.CVar)) { *F = FCString::Atof(*V); ++Moved; }
			UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] миграция v2: [%s] %s=%s → поле профиля"), Section, E.CVar, *V);
			continue;
		}
		int32* I = AsdaIntFieldFor(this, E.CVar);
		if (!I) { continue; }
		if (E.Kind == 2)
		{
			const bool bOff = FMath::IsNearlyEqual(FCString::Atof(*V), FCString::Atof(E.OffValue), 1e-4f);
			if (!bOff && FString(E.CVar).Equals(TEXT("r.LensFlareQuality"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogAsdaUserSettings, Log,
					TEXT("[профиль] миграция v2: [%s] %s=%s НЕ перенесён как «вкл» — у проекта сила бликов 0 (r.DefaultFeature.LensFlare=False), видимого действия строка не имела"),
					Section, E.CVar, *V);
				continue;
			}
			*I = bOff ? 0 : 1;
		}
		else
		{
			*I = FCString::Atoi(*V);
		}
		++Moved;
		UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] миграция v2: [%s] %s=%s → поле профиля"), Section, E.CVar, *V);
	}
	int32 Glow = 0;
	if (GConfig->GetInt(kDevVideoSection, TEXT("GlowLevel"), Glow, GGameUserSettingsIni))
	{
		GlowLevel = FMath::Clamp(Glow, 0, 4);
		++Moved;
	}
	if (GConfig->GetString(kDevVideoSection, TEXT("r.BloomQuality"), V, GGameUserSettingsIni))
	{
		V.TrimStartAndEndInline();
		if (!V.IsEmpty() && FCString::Atoi(*V) == 0)
		{
			GlowLevel = 0;   // «Bloom: выкл» dev-окна = свечения нет (пункт 0)
			++Moved;
		}
	}
	float DisplayGammaDev = 0.f;
	if (GConfig->GetFloat(kDevVideoSection, TEXT("DisplayGamma"), DisplayGammaDev, GGameUserSettingsIni) && DisplayGammaDev > 0.f)
	{
		// Обратная к `ApplyGammaSetting`: DisplayGamma = Lerp(3.0, 1.4, Gamma / 100).
		Gamma = FMath::Clamp(FMath::RoundToInt((3.0f - DisplayGammaDev) / 1.6f * 100.f), 1, 100);
		++Moved;
	}
	float ScreenPct = 0.f;
	if (GConfig->GetFloat(kDevVideoSection, TEXT("r.ScreenPercentage"), ScreenPct, GGameUserSettingsIni) && ScreenPct > 0.f)
	{
		SetResolutionScaleValueEx(FMath::Min(ScreenPct, 100.f));
		++Moved;
	}
	UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] миграция v2: из строк dev-окна перенесено значений %d"), Moved);
	return Moved > 0;
}

// ★ v3 (19.09.2026). Миграция v2 перенесла `[AsdaVideo] r.ScreenPercentage` dev-окна в штатное поле с
//   потолком 100 % (`FMath::Min(…, 100)`) — у владельца стояло 125 %, и оно молча стало 100. Строку v2 не
//   удаляла, поэтому исходное значение цело: берём его, если оно выше 100 и игрок ещё не задавал своё.
void UAsdaGameUserSettings::MigrateRenderScaleV3()
{
	float ScreenPct = 0.f;
	if (GConfig && RenderScaleAbove100 <= 100
		&& GConfig->GetFloat(kDevVideoSection, TEXT("r.ScreenPercentage"), ScreenPct, GGameUserSettingsIni)
		&& ScreenPct > 100.f)
	{
		SetRenderScalePercent(FMath::RoundToInt(ScreenPct));
		UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] миграция v3: масштаб рендера %d %% возвращён из dev-окна"), RenderScaleAbove100);
	}
}

int32 UAsdaGameUserSettings::GetRenderScalePercent() const
{
	if (RenderScaleAbove100 > 100) { return FMath::Clamp(RenderScaleAbove100, 101, kRenderScaleMax); }
	const float Std = ScalabilityQuality.ResolutionQuality;
	if (Std > 0.f) { return FMath::Clamp(FMath::RoundToInt(Std), kRenderScaleMin, 100); }
	// Штатное 0 = «масштаб проекта» (движок ключ не трогает) — показываем то, что действует на деле.
	IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	return CV ? FMath::Clamp(FMath::RoundToInt(CV->GetFloat()), kRenderScaleMin, kRenderScaleMax) : 100;
}

void UAsdaGameUserSettings::SetRenderScalePercent(int32 Percent)
{
	const int32 P = FMath::Clamp(Percent, kRenderScaleMin, kRenderScaleMax);
	if (P > 100)
	{
		RenderScaleAbove100 = P;
		SetResolutionScaleValueEx(100.f);   // штатное — на потолке; сверху кладёт `ApplyRenderOverrides`
	}
	else
	{
		RenderScaleAbove100 = -1;
		SetResolutionScaleValueEx((float)P);
	}
}

void UAsdaGameUserSettings::MigrateRetiredOverridesV4()
{
	// Старые значения — в журнал (вернуть можно ключом консоли), поле → «не менялось» (−1): ApplyRenderOverrides его пропустит,
	//   а приоритет консоли с ключа снимается — вернётся значение уровня.
	struct FRetired { const TCHAR* CVar; int32* Field; };
	FRetired Items[] = {
		{ TEXT("r.MaxAnisotropy"),          &MaxAnisotropy },
		{ TEXT("r.AmbientOcclusionLevels"), &AmbientOcclusion },
		{ TEXT("r.LensFlareQuality"),       &LensFlare },
	};
	for (FRetired& It : Items)
	{
		if (*It.Field < 0) { continue; }
		UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] v4: снято переопределение %s (поле было %d) — теперь его ведёт уровень качества"), It.CVar, *It.Field);
		*It.Field = -1;
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(It.CVar)) { V->Unset(ECVF_SetByConsole); }
	}
}

void UAsdaGameUserSettings::LoadSettings(bool bForceReload)
{
	const bool bMigrating = MigrateFromLegacyConfig();
	Super::LoadSettings(bForceReload);
	// ★ 09.10.2026: `Language` — не Config-поле (см. объявление): читаем ключ сами, СРАЗУ после штатного чтения и ДО
	//   миграций ниже — они зовут `SaveSettings`, а та пишет поле в ini (непустое) и не должна видеть его ещё не прочитанным.
	Language.Reset();
	if (GConfig)
	{
		GConfig->GetString(kLanguageSection, kLanguageKey, Language, GGameUserSettingsIni);
	}
	if (bMigrating)
	{
		ImportLegacyAsdaOptions();
	}
	// ★ v2 — и для профиля первой версии, и для совсем старого (после переноса v0 → v1 выше).
	const bool bNeedV2 = (AsdaSettingsVersion < 2);
	if (bNeedV2) { MigrateDevOverridesV2(); }
	// ★ v3 (19.09.2026): миграция v2 урезала масштаб рендера до 100 % — возвращаем значение игрока.
	const bool bNeedV3 = (AsdaSettingsVersion < 3);
	if (bNeedV3) { MigrateRenderScaleV3(); }
	// ★ v4 (12.10.2026, пересмотр пресетов): анизотропия, AO и блики больше не настраиваются игроком — их ведут уровни
	//   качества (анизотропия 2/4/8/16/16, AO по PostProcessQuality, бликов нет). Старое переопределение снимается.
	const bool bNeedV4 = (AsdaSettingsVersion < 4);
	if (bNeedV4) { MigrateRetiredOverridesV4(); }
	NormalizeFlagArrays();
	if ((bNeedV2 || bNeedV3 || bNeedV4) && !bMigrating)
	{
		// Метку v2 ставим и сохраняем сразу — повторного переноса не будет.
		AsdaSettingsVersion = kAsdaSettingsVersion;
		SaveSettings();
	}
	if (bMigrating)
	{
		// Метку ставим и сохраняем СРАЗУ: следующая загрузка пойдёт штатным путём без переноса.
		AsdaSettingsVersion = kAsdaSettingsVersion;
		SaveSettings();
		// В самостоятельной игре окно уже создано по данным `PreloadResolutionSettings`, которые при
		//   первом запуске после обновления читали ещё пустую секцию подкласса. Перенесённые разрешение
		//   и оконный режим накатываем штатным путём, чтобы игрок не получил один запуск «не своим»
		//   окном. В редакторе окна игры нет — там это не нужно.
		//   ⚠ Не `ApplyResolutionSettings`: на этапе загрузки профиля Slate может быть ещё не поднят, а
		//   тот зовёт `GetClosestMonitorIndex`. Штатный `RequestResolutionChange` лишь ставит заявку
		//   `r.SetRes`, которую движок исполнит сам на ближайшем кадре (ключи командной строки
		//   `-ResX/-ResY/-windowed` по-прежнему главнее — `bInDoOverrides = true`).
		if (!GIsEditor)
		{
			RequestResolutionChange(ResolutionSizeX, ResolutionSizeY, GetFullscreenMode(), /*bInDoOverrides*/ true);
		}
	}
	UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] загружен: %s"), *Describe());
}

void UAsdaGameUserSettings::SaveSettings()
{
	Super::SaveSettings();   // штатно: scalability + `SaveConfig` всех Config-полей (Language среди них нет)
	if (!GConfig)
	{
		return;
	}
	// ★ 09.10.2026 (решение владельца): ключ `Language` пишется ТОЛЬКО непустым — пока игрок язык не выбрал, строки нет.
	//   Пустая строка, которую дописывали прежние сборки (тогда поле было Config), удаляется; непустое значение в файле при
	//   пустом поле НЕ трогается (не стираем выбор игрока, если поле почему-то не прочитано).
	if (!Language.IsEmpty())
	{
		GConfig->SetString(kLanguageSection, kLanguageKey, *Language, GGameUserSettingsIni);
		// ★ 09.10.2026 (ревизия КРИТ-1): `Super::SaveSettings` уже сбросил файл ДО этой строки — без своего Flush ключ
		//   висел «грязным» до выхода процесса, а `FAsdaLang::RestartGame` стартует новый процесс раньше, чем старый выйдет.
		GConfig->Flush(false, GGameUserSettingsIni);
		return;
	}
	FString OnDisk;
	if (GConfig->GetString(kLanguageSection, kLanguageKey, OnDisk, GGameUserSettingsIni) && OnDisk.IsEmpty())
	{
		GConfig->RemoveKeyFromSection(kLanguageSection, FName(kLanguageKey), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);   // ★ 09.10.2026 (КРИТ-1): удаление пустой строки — тоже сразу на диск
	}
}

void UAsdaGameUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();
	ApplyAudioSettings();
	ApplyGammaSetting();
	// ★ Фаза 2: ручки рендера из профиля — ПОСЛЕ родителя, то есть поверх значений уровня качества.
	ApplyRenderOverrides();
}

// ═══ ПЕРЕОПРЕДЕЛЕНИЯ РЕНДЕРА ИЗ ПРОФИЛЯ ════════════════════════════════════════════════════════════
//   Приоритет консоли — тот же, каким их ставило dev-окно (`ApplyCVarPersist`): ниже него пины проекта
//   (`r.AntiAliasingMethod` в RendererSettings, `asda.Cull.Scale` в [SystemSettings]) выбор игрока бы не
//   пропустили. Незаданные поля ключ НЕ трогают — работает проект/уровень качества.
void UAsdaGameUserSettings::ApplyRenderOverrides() const
{
	// ★ 19.09.2026: масштаб рендера выше 100 % — приоритетом консоли поверх уровня качества (тот ставит
	//   `r.ScreenPercentage` приоритетом Scalability и не выше 100). Когда игрок вернулся к ≤ 100 %, приоритет
	//   консоли надо СНЯТЬ, иначе штатное значение уровня качества ключ больше не пробьёт.
	if (IConsoleVariable* SP = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		if (RenderScaleAbove100 > 100)
		{
			SP->Set((float)FMath::Min(RenderScaleAbove100, kRenderScaleMax), ECVF_SetByConsole);
		}
		else if ((SP->GetFlags() & ECVF_SetByMask) == ECVF_SetByConsole)
		{
			SP->Unset(ECVF_SetByConsole);
			// Без истории ключей (`UE_ALLOW_CVAR_HISTORY`) снятие не срабатывает — тогда ставим штатное
			//   значение тем же приоритетом (0 штатного = «как в проекте» = 100 %).
			if ((SP->GetFlags() & ECVF_SetByMask) == ECVF_SetByConsole)
			{
				const float Std = ScalabilityQuality.ResolutionQuality;
				SP->Set(Std > 0.f ? Std : 100.f, ECVF_SetByConsole);
			}
		}
	}
	int32 Applied = 0;
	for (const AsdaProfileCVars::FEntry& E : AsdaProfileCVars::kEntries)
	{
		const FString V = AsdaOverrideValue(this, E);
		if (V.IsEmpty()) { continue; }
		if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(E.CVar))
		{
			CV->Set(*V, ECVF_SetByConsole);
			++Applied;
		}
	}
	if (Applied > 0)
	{
		UE_LOG(LogAsdaUserSettings, Verbose, TEXT("[профиль] переопределений рендера применено: %d"), Applied);
	}
}

bool UAsdaGameUserSettings::IsProfileOwnedCVar(const FString& Name)
{
	return AsdaProfileCVars::Find(Name) != nullptr;
}

bool UAsdaGameUserSettings::SetCVarOverride(const FString& Name, const FString& Value, bool bSave)
{
	const AsdaProfileCVars::FEntry* E = AsdaProfileCVars::Find(Name);
	if (!E) { return false; }
	if (E->Kind == 1)
	{
		if (float* F = AsdaFloatFieldFor(this, E->CVar)) { *F = FMath::Max(0.f, FCString::Atof(*Value)); }
	}
	else if (int32* I = AsdaIntFieldFor(this, E->CVar))
	{
		*I = (E->Kind == 2)
			? (FMath::IsNearlyEqual(FCString::Atof(*Value), FCString::Atof(E->OffValue), 1e-4f) ? 0 : 1)
			: FMath::Max(0, FCString::Atoi(*Value));
	}
	NormalizeFlagArrays();
	const FString Applied = AsdaOverrideValue(this, *E);
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(E->CVar))
	{
		if (!Applied.IsEmpty()) { CV->Set(*Applied, ECVF_SetByConsole); }
	}
	if (bSave) { SaveSettings(); }
	UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] %s = %s (поле профиля%s)"), E->CVar, *Applied, bSave ? TEXT(", сохранено") : TEXT(""));
	return true;
}

bool UAsdaGameUserSettings::ClearCVarOverride(const FString& Name, bool bSave)
{
	const AsdaProfileCVars::FEntry* E = AsdaProfileCVars::Find(Name);
	if (!E) { return false; }
	if (E->Kind == 1) { if (float* F = AsdaFloatFieldFor(this, E->CVar)) { *F = -1.f; } }
	else if (int32* I = AsdaIntFieldFor(this, E->CVar)) { *I = -1; }
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(E->CVar))
	{
		CV->Unset(ECVF_SetByConsole);   // история приоритетов вернёт значение проекта/уровня качества
	}
	if (bSave) { SaveSettings(); }
	UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] %s — переопределение снято (действует проект/уровень)"), E->CVar);
	return true;
}

// Графический API — штатная секция движка (см. объявление).
bool UAsdaGameUserSettings::GetPendingRHIIsDx12()
{
	FString Rhi;
	if (GConfig && GConfig->GetString(TEXT("D3DRHIPreference"), TEXT("PreferredRHI"), Rhi, GGameUserSettingsIni))
	{
		return !Rhi.Equals(TEXT("dx11"), ESearchCase::IgnoreCase);
	}
	return true;   // ключа нет → проектный DefaultGraphicsRHI_DX12
}

void UAsdaGameUserSettings::SetPendingRHI(bool bDx12)
{
	if (!GConfig) { return; }
	GConfig->SetString(TEXT("D3DRHIPreference"), TEXT("PreferredRHI"), bDx12 ? TEXT("dx12") : TEXT("dx11"), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	UE_LOG(LogAsdaUserSettings, Log, TEXT("[профиль] PreferredRHI=%s (вступит в силу после перезапуска)"), bDx12 ? TEXT("dx12") : TEXT("dx11"));
}

void UAsdaGameUserSettings::ApplyAudioSettings() const
{
	UAsdaSoundSubsystem::ApplyUserVolumes(MusicVolume, bMusicMuted, SfxVolume, bSfxMuted);
}

// Яркость: та же формула, что у `UAsdaVideoSettingsSubsystem::SetGamma` (config.txt GAMMA 0…100,
//   50 = нейтраль 2.2, 0 → 3.0 темнее, 100 → 1.4 светлее) — видео-подсистема зовёт сюда же.
void UAsdaGameUserSettings::ApplyGammaSetting() const
{
	// ★ 19.09.2026 (ревизия, МЕЛК): в РЕДАКТОРЕ не трогаем — иначе при гамме ≠ 50 менялась бы картинка
	//   во вьюпорте редактора. Видео-подсистема уже держит ту же проверку; здесь её не было.
	if (GEngine && !GIsEditor)
	{
		GEngine->DisplayGamma = FMath::Lerp(3.0f, 1.4f, FMath::Clamp((float)Gamma, 0.f, 100.f) / 100.f);
	}
}

float UAsdaGameUserSettings::GetCameraTurnScale()
{
	const UAsdaGameUserSettings* P = Get();
	return P ? (float)FMath::Clamp(P->CameraTurnPercent, kMouseSensMin, kMouseSensMax) / 100.f : 1.f;
}

// ★ 25.10.2026: режим Ctrl приседа — разбор у поля `CrouchMode`; значение вне 0/1 (не прошло нормализацию) — «Нажатие».
int32 UAsdaGameUserSettings::GetCrouchMode()
{
	const UAsdaGameUserSettings* P = Get();
	return (P && P->CrouchMode == kCrouchHold) ? kCrouchHold : kCrouchToggle;
}

FString UAsdaGameUserSettings::Describe() const
{
	auto Bits = [](const TArray<uint8>& A)
	{
		FString S;
		for (uint8 V : A) { S += V ? TEXT("1") : TEXT("0"); }
		return S;
	};
	const FIntPoint R = GetScreenResolution();
	return FString::Printf(
		TEXT("версия=%d экран=%dx%d режим=%d | музыка %.3f%s эффекты %.3f%s | гамма %d свечение %d | ")
		TEXT("интерфейс %s/%s/%s 3D %s | AA %d резкость %.2f аниз %d AO %d размытие %d блики %d облака %d дальность %.2f масштаб %d %% язык «%s» мышь %d %% камера %d %% присед %s"),
		AsdaSettingsVersion, R.X, R.Y, (int32)GetFullscreenMode(),
		MusicVolume, bMusicMuted ? TEXT(" (заглушена)") : TEXT(""),
		SfxVolume, bSfxMuted ? TEXT(" (заглушены)") : TEXT(""),
		Gamma, GlowLevel, *Bits(InterfaceCommunity), *Bits(InterfaceNames), *Bits(InterfaceOther), *Bits(Render3D),
		AntiAliasingMethod, TonemapperSharpen, MaxAnisotropy, AmbientOcclusion, MotionBlur, LensFlare, VolumetricClouds,
		DrawDistanceScale, GetRenderScalePercent(), Language.IsEmpty() ? TEXT("не выбран") : *Language,
		CursorSpeedPercent, CameraTurnPercent, CrouchMode == kCrouchHold ? TEXT("зажатие") : TEXT("нажатие"));
}
