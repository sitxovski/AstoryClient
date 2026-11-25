// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaDevMenuSubsystem — см. AsdaDevMenuSubsystem.h (dev-меню Ctrl+N на всех картах; вкладки с 05.09.2026).
//
// ★★★ 12.10.2026 — СОКРАЩЕНИЕ МЕНЮ (решение владельца; вердикт дизайнера Reports/owner_1012/presets/01_designer.md §1,
//   отчёт этой волны — 06_devmenu_cut.md). Было 59 ручек в 7 вкладках → стало 28 в 6 (вкладка «Свет и тени» опустела и удалена).
//   УШЛО В УРОВНИ (Config/DefaultScalability.ini + BaseScalability задают значение, строки нет): VSM↔CSM, дальность динамических
//   теней ×, контактные тени, мягкость SMRT, отсев мелких теней, подробность VSM, бюджеты теней локальных/эффект-светов, дальность
//   локальных светов ×, Ambient Occlusion, группы «Листва»/«Шейдинг» (на ISM-мир не действуют), анизотропия (→ «Текстуры»),
//   «Рельеф (Enable)» и «Шероховатость из карты» ([AsdaQuality@N]).
//   УБРАНО КАК ДУБЛЬ/ПУСТЫШКА: «Качество теней (sg)», Bloom (= пункт свечения), Lens flare (решение 10.09 «блики выключены совсем»),
//   капсульные тени во вкладке теней (одна строка — «Персонажи»), временной фильтр GTAO, смещение мипов, skin cache (всегда вкл),
//   AO/делайтинг/свечение из карты (авторские, 0 = рекомендуемое), синтетический пробник железа (кнопка теперь зовёт замер кадра
//   UAsdaBenchSubsystem::StartInPlace — тот же путь, что «Автоподбор» окна настроек).
//   УБРАНО В КОНСОЛЬ (ключ жив, строки и пина нет): asda.Cull.Enable, строка-диагностика asda.Cull.Dump, asda.Vis.Enable,
//   asda.Clouds.UpdateEvery, r.Fog, asda.Clouds.Shadows.
//   ГРАБЛЯ ПИНОВ (02_engineer.md §2.5/§3.2): ключи убранных строк выкинуты из массивов персиста (GLiveCVarKeys опустел и удалён,
//   GRenderCVarKeys — 6 ключей) и из ResetTab; при старте ApplySavedSettings один раз ЧИСТИТ устаревшие пины
//   [AsdaVideo]/[AsdaRender]/[AsdaNormals] со строкой «[меню] снят устаревший пин X=Y» в лог (SweepRetiredPins).
//   САМОТЕСТ asda.DevMenu.SelfTest: все режимы считают ok/fail и кончаются «САМОТЕСТ-МЕНЮ ИТОГ: ok=N fail=M»
//   (AsdaSelfTestKit::Finish); ненайденная строка = ПРОВАЛ; новые режимы rows (инвентарь/ожидания) и pins (пины туда-обратно);
//   режим shadowrows удалён вместе со строками.
#include "UI/AsdaDevMenuSubsystem.h"

#include "Framework/Application/IInputProcessor.h"


#include "AsdaDevMenuSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaDevMenuSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaDevMenu);   // ★ R3-c 12.11.2026: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ─────────────── asda.Fog.DensityScale (регистрация единственная в Source/, grep 05.09.2026 = 0) ───────────────
// Множитель плотности тумана карты: FogDensity = авторская × Scale для всех ExponentialHeightFogComponent мира.
// Авторское значение (актор MapFog карты, результат asda_convert_map_lit.py) кэшируется при первом касании;
// накат при каждой загрузке карты (PostLoadMapWithWorld) и при смене ключа на лету.
float GAsdaFogDensityScale = 1.f;

static FAutoConsoleVariableRef CVarAsdaFogDensityScale(
	TEXT("asda.Fog.DensityScale"),
	GAsdaFogDensityScale,
	TEXT("Множитель ПЛОТНОСТИ тумана карты (0.25…2.0): FogDensity = авторская × DensityScale / DistanceScale; 1 = как в акторе карты. Умолчание в коде 1; в проекте задано 0.270944 в Config/DefaultEngine.ini [SystemSettings] (подбор владельца 09.09.2026, пара с asda.Fog.DistanceScale)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFogChanged),
	ECVF_Default);

// ─────────────── asda.Fog.DistanceScale (★ волна панели 06.09.2026; регистрация единственная в Source/, grep = 0) ───────────────
// Множитель ДАЛЬНОСТИ начала тумана карты: StartDistance = авторская × Scale (туман начинается ближе при <1, дальше при >1).
// Чтобы дальний туман не «сгущался», плотность одновременно делится на этот множитель (см. ApplyFogScale). Авторские значения
// (актор MapFog карты) кэшируются при первом касании компонента; накат при каждой загрузке карты и при движении ползунка.
float GAsdaFogDistanceScale = 1.f;
static FAutoConsoleVariableRef CVarAsdaFogDistanceScale(
	TEXT("asda.Fog.DistanceScale"),
	GAsdaFogDistanceScale,
	TEXT("Множитель ДАЛЬНОСТИ начала тумана карты (0.5…2.0): StartDistance = авторская × DistanceScale, плотность делится на него; 1 = как в акторе карты. Умолчание в коде 1; в проекте задано 2.0 в Config/DefaultEngine.ini [SystemSettings] (подбор владельца 09.09.2026, пара с asda.Fog.DensityScale)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFogChanged),
	ECVF_Default);

// ─────────────── ★★★ 16.10.2026: ОБЪЁМНЫЙ ТУМАН ДЛЯ ЛУЧЕЙ ЛОКАЛЬНЫХ СВЕТОВ (регистрация единственная в Source/, grep = 0) ───────────────
//   Жалоба владельца: «источники света типа HolyLight_SpotLight (логин) на всех картах не рендерятся ни на одном пресете».
//   Луч такого источника — это рассеяние в ОБЪЁМНОМ тумане (у HolyLight VolumetricScatteringIntensity 150). Его не было по трём причинам
//   (отчёт Reports/holylight_1016/HOLYLIGHT_REPORT.md):
//   1) у тумана всех 22 карт bEnableVolumetricFog=false (дамп E-0) — прохода нет вовсе, на любом уровне (ключ asda.Fog.Volumetric ниже);
//   2) Низкое/Среднее: r.VolumetricFog=0 (BS) — снято в Config/DefaultScalability.ini [ShadowQuality@0/1];
//   3) в игре плотность тумана = авторская × 0.270944 / 2.0 (подбор владельца 09.09) — в Login 0.02732 → 0.00370, а луч ∝ плотность ×
//      рассеяние света, т. е. в 7.4 раза слабее, чем в редакторе, где его ставили (замер: вклад рассеяния 150 в кадр ≈ шум).
//   Подбор дымки владельца НЕ меняется: плотность остаётся его, компенсируется только рассеяние ЛОКАЛЬНЫХ светов (солнце — нет,
//   иначе посветлела бы вся дымка).
//   ★ asda.Fog.Volumetric по умолчанию 0 (авторский флаг актора): замер 16.10 — принудительный объёмный туман на alpen засвечивает
//     ВСЮ ближнюю дымку (солнце рассеивается в объёме, Δ кадра 10–17 из 255 на 5 ракурсах, maps/set0/cmp_alpen_1.jpg) — это ломает
//     подбор тумана владельца. Включать на картах — только решением владельца (одна строка в DefaultEngine.ini [SystemSettings]).
//     В Login объёмный туман включён САМИМ владельцем в акторе — там лучи работают и без ключа.
int32 GAsdaFogVolumetric = 0;
static FAutoConsoleVariableRef CVarAsdaFogVolumetric(
	TEXT("asda.Fog.Volumetric"),
	GAsdaFogVolumetric,
	TEXT("Объёмный туман у тумана карт в игре: 0 = авторский флаг актора (по умолчанию; у Login включён владельцем, у 22 карт выключен), 1 = включить у всех ExponentialHeightFog игрового мира (лучи/ореолы локальных светов, но дымка от солнца заметно светлеет)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFogChanged),
	ECVF_Default);
static int32 GAsdaFogVolumetricLightComp = 1;
static FAutoConsoleVariableRef CVarAsdaFogVolumetricLightComp(
	TEXT("asda.Fog.VolumetricLightComp"),
	GAsdaFogVolumetricLightComp,
	TEXT("Компенсация разрежения тумана для лучей локальных светов: 1 = рассеяние света × DistanceScale/DensityScale (луч как в редакторе при авторской плотности), 0 = авторское рассеяние."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaFogChanged),
	ECVF_Default);

// Миры, где ApplyFogScale разредил туман (только там компенсация лучей имеет смысл; редактор и первая карта PIE — нет).
TArray<TWeakObjectPtr<UWorld>> GFogScaledWorlds;

float UAsdaDevMenuSubsystem::FogVolumetricLightScale(const UWorld* World)
{
	// Компенсация — ровно там, где разрежён туман: в мирах, прошедших ApplyFogScale (OnPostLoadMap / ApplyFogScaleAllWorlds)
	if (GAsdaFogVolumetricLightComp == 0 || !World || !World->IsGameWorld()) { return 1.f; }
	if (!GFogScaledWorlds.ContainsByPredicate([World](const TWeakObjectPtr<UWorld>& W) { return W.Get() == World; })) { return 1.f; }
	// Обратная величина множителя плотности из ApplyFogScale (DensityScale / DistanceScale); защита от нуля — как там
	return FMath::Max(GAsdaFogDistanceScale, 0.01f) / FMath::Max(GAsdaFogDensityScale, 0.01f);
}

// ★ Бинд dev-меню (cmd 91) из раскладки. Раскладку держит контроллер; если его ещё нет
//   (логин-карта до спавна) — падаем на эталонный дефолт CTRL+N, чтобы меню не пропало.
bool UAsdaDevMenuSubsystem::MatchesDevMenuKey(const FKeyEvent& Ev) const
{
	constexpr int32 kCmdDevMenu = 91;
	int32 WantVk = 78;                       // N
	EAsdaKeyMod WantMod = EAsdaKeyMod::Ctrl;
	const UGameInstance* GI = GetGameInstance();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (const AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr)
	{
		FAsdaKeyBind B;
		if (PC->Keys && PC->Keys->GetBind(kCmdDevMenu, B))
		{
			if (B.Vk == 0) { return false; }   // «NONE» — меню намеренно отключено раскладкой
			WantVk = B.Vk; WantMod = B.Mod;
		}
	}
	if (Ev.GetKey() != UAsdaKeybindConfig::VkToFKey(WantVk)) { return false; }
	switch (WantMod)
	{
	case EAsdaKeyMod::Shift: return Ev.IsShiftDown();
	case EAsdaKeyMod::Ctrl:  return Ev.IsControlDown();
	case EAsdaKeyMod::Alt:   return Ev.IsAltDown();
	default:                 return true;
	}
}

// ─────────────────────── Slate input-preprocessor (Ctrl+N/Esc) ───────────────────────
// Регистрируется ОДИН раз на GameInstance → работает на любой карте и в любом input-режиме
// (UIOnly логина / GameAndUI мира): препроцессоры Slate видят клавиши раньше любого роутинга.
class FAsdaDevMenuKeyProcessor : public IInputProcessor
{
public:
	TWeakObjectPtr<UAsdaDevMenuSubsystem> Owner;
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& App, const FKeyEvent& Ev) override
	{
		if (!Owner.IsValid()) { return false; }
		// Esc закрывает ОТКРЫТОЕ меню (закрытое — Esc идёт игре: админ-камера и т.п.).
		if (Ev.GetKey() == EKeys::Escape && Owner->IsMenuOpen())
		{
			Owner->CloseMenu();
			return true;
		}
		// ★ Клавиша из конфига: «EXT DEV MENU:» (cmd 91), дефолт прежний CTRL+N. Сравниваем
		//   пришедшее событие с биндом, а не с зашитой клавишей — владелец меняет его в
		//   `system/Shortcutkey.ini` без пересборки.
		if (Owner->MatchesDevMenuKey(Ev))
		{
			// ★ Разруливание конфликта с оригинальным биндом Ctrl+N = cmd 67 CHAT NORMAL:
			//   фокус в текстовом поле (SEditableText/SMultiLineEditableText — чат/логин-инпут) →
			//   ПРОПУСКАЕМ событие (чат-переключатель канала нужен именно при наборе текста).
			//   Вне текстового фокуса — dev-меню приоритетнее (решение юзера); событие поглощается,
			//   до PlayerInput не доходит → cmd 67 дублем не стреляет.
			TSharedPtr<SWidget> Focused = App.GetKeyboardFocusedWidget();
			if (Focused.IsValid() && Focused->GetTypeAsString().Contains(TEXT("EditableText")))
			{
				return false;
			}
			Owner->ToggleMenu();
			return true;   // поглотить (не печатать 'n', не будить игровые бинды)
		}
		return false;
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("AsdaDevMenuKey"); }
};

// ───────────────────────────── Инициализация ─────────────────────────────
void UAsdaDevMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Видео-подсистема первой: её Initialize кладёт дефолты первого запуска (маркер [AsdaVideo] bInitialized),
	// после чего наши сохранённые оверрайды накатываются ПОВЕРХ.
	Collection.InitializeDependency(UAsdaVideoSettingsSubsystem::StaticClass());

	if (FSlateApplication::IsInitialized())
	{
		KeyProc = MakeShared<FAsdaDevMenuKeyProcessor>();
		KeyProc->Owner = this;
		FSlateApplication::Get().RegisterInputPreProcessor(KeyProc);
	}

	ApplySavedSettings();
	// Параметры, живущие на акторах/коллекциях мира (туман, MPC_AsdaNormals) — при каждой загрузке карты.
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UAsdaDevMenuSubsystem::OnPostLoadMap);
	// ★ E-0b: смена уровня sg.* (пресет Ctrl+N, консоль `Scalability N`, автопробник) → перенакат нашего per-level слоя
	//   [AsdaQuality@N] (рельеф MPC_AsdaNormals) во все игровые миры. Движковые ключи уровня движок накатывает сам.
	ScalabilityChangedHandle = Scalability::OnScalabilitySettingsChanged.AddUObject(this, &UAsdaDevMenuSubsystem::OnScalabilityChanged);
}

void UAsdaDevMenuSubsystem::Deinitialize()
{
	CloseMenu();
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	if (ScalabilityChangedHandle.IsValid())
	{
		Scalability::OnScalabilitySettingsChanged.Remove(ScalabilityChangedHandle);
		ScalabilityChangedHandle.Reset();
	}
	if (KeyProc.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(KeyProc);
	}
	KeyProc.Reset();
	Super::Deinitialize();
}

// ★ 12.10.2026 (ревизия СРЕДН-2): убранная строка «Skin cache (рестарт)» писала r.SkinCache.CompileShaders в ПОЛЬЗОВАТЕЛЬСКИЙ
//   Engine.ini ([/Script/Engine.RendererSettings]) — чистка пинов GameUserSettings.ini её не видит, а сброс вкладки больше не
//   возвращает. Снимаем ключ (действует проектное значение DefaultEngine.ini) с записью старого значения в журнал; READONLY-CVar,
//   поэтому действует со следующего запуска.
static void SweepRetiredSkinCache()
{
	if (!GConfig) { return; }
	FString Old;
	if (GConfig->GetString(TEXT("/Script/Engine.RendererSettings"), TEXT("r.SkinCache.CompileShaders"), Old, GEngineIni))
	{
		GConfig->RemoveKey(TEXT("/Script/Engine.RendererSettings"), TEXT("r.SkinCache.CompileShaders"), GEngineIni);
		GConfig->Flush(false, GEngineIni);
		UE_LOG(LogAsdaDevMenu, Log, TEXT("[меню] снят устаревший пин r.SkinCache.CompileShaders=%s (Engine.ini игрока) — со следующего запуска проектное значение"), *Old);
	}
}

// Накат сохранённых живых настроек при старте игры: ★ 12.10.2026 сначала чистка устаревших пинов (см. SweepRetiredPins), затем
// CVar-оверрайды из [AsdaRender] + вкладка. Разрешение/режим окна/scalability/VSync/лимит FPS применяет сам движок из
// UGameUserSettings; r.*-строки меню — поля профиля (UAsdaGameUserSettings::ApplyRenderOverrides). ECVF_SetByConsole —
// единственный приоритет, гарантированно перекрывающий и [SystemSettings], и ProjectSetting.
void UAsdaDevMenuSubsystem::ApplySavedSettings()
{
	if (!GConfig) { return; }
	SweepRetiredSkinCache();   // ★ 12.10.2026 (ревизия СРЕДН-2): пин убранной строки skin cache в Engine.ini игрока
	const int32 Swept = SweepRetiredPins(TEXT("AsdaVideo"), /*bRemove*/true) + SweepRetiredPins(TEXT("AsdaRender"), /*bRemove*/true) + SweepRetiredNormals(/*bRemove*/true);
	if (Swept > 0)
	{
		GConfig->Flush(false, GGameUserSettingsIni);
		UE_LOG(LogAsdaDevMenu, Log, TEXT("[меню] чистка профиля: снято устаревших пинов %d (строки убраны из Ctrl+N 12.10.2026)"), Swept);
	}
	for (const TCHAR* Key : GRenderCVarKeys)
	{
		FString Val;
		if (GConfig->GetString(TEXT("AsdaRender"), Key, Val, GGameUserSettingsIni) && !Val.IsEmpty())
		{
			if (IConsoleVariable* CV = FindCVar(Key))
			{
				CV->Set(*Val, ECVF_SetByConsole);
				UE_LOG(LogAsdaDevMenu, Log, TEXT("[AsdaRender] накат %s=%s"), Key, *Val);
			}
		}
	}

	// ★★★ 19.09.2026 (фаза 2): `[AsdaVideo] DisplayGamma` здесь БОЛЬШЕ НЕ НАКАТЫВАЕТСЯ. Это и была гонка
	//   яркости: профиль игрока ставил свою гамму (полоса 70058 окна настроек), а dev-окно следом — свою,
	//   и при старте всегда побеждало оно. Значение один раз перенесено в поле `Gamma` профиля (миграция v2),
	//   строка «Яркость» ниже пишет туда же через `UAsdaVideoSettingsSubsystem::SetGamma`.
	int32 Tab = 0;
	if (GConfig->GetInt(TEXT("AsdaVideo"), TEXT("DevMenuTab"), Tab, GGameUserSettingsIni))
	{
		ActiveTab = (EAsdaDevTab)FMath::Clamp(Tab, 0, (int32)EAsdaDevTab::Count - 1);   // ★ 12.10.2026: вкладок стало 6 — старый индекс зажимается
	}
}

// Загрузка карты: туман (множитель поверх авторской плотности) и MPC_AsdaNormals (экземпляр коллекции мира).
void UAsdaDevMenuSubsystem::OnPostLoadMap(UWorld* World)
{
	if (!World || !World->IsGameWorld()) { return; }
	// Чистка кэша авторских значений от компонентов ушедшего мира (обе карты — плотность и дальность).
	for (auto It = FogBaseDensity.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	for (auto It = FogBaseStartDistance.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	for (auto It = FogBaseVolumetric.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }    // ★ 16.10.2026
	for (auto It = LightBaseVolScatter.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }  // ★ 16.10.2026
	ApplyFogScale(World);
	ApplyNormals(World);
}
